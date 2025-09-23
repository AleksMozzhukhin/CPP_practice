#include "core/game_engine.hpp"

#include <algorithm>
#include <numeric>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>
#include <barrier>
#include <atomic>
#include <optional>
#include <random>

#include "core/game_state.hpp"
#include "core/moderator.hpp"
#include "core/phases.hpp"
#include "core/types.hpp"
#include "core/rng.hpp"
#include "util/logger.hpp"

// Роли (базовые)
#include "roles/i_player.hpp"
#include "roles/citizen.hpp"
#include "roles/mafia.hpp"
#include "roles/detective.hpp"
#include "roles/doctor.hpp"
#include "roles/maniac.hpp"
#include "roles/human.hpp"

// Доп. роли из ТЗ
#include "roles/executioner.hpp"
#include "roles/journalist.hpp"
#include "roles/eavesdropper.hpp"

namespace core {

using roles::IPlayer;
using roles::Role;
using roles::Team;

namespace {

// Вспомогательная функция: собрать список живых id
std::vector<PlayerId> alive_ids(const GameState& st) {
    std::vector<PlayerId> ids;
    const auto& ps = st.players();
    ids.reserve(ps.size());
    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (ps[i] && ps[i]->is_alive()) ids.push_back(static_cast<PlayerId>(i));
    }
    return ids;
}

// Выбрать валидную цель для голосования (если роль вернула невалидную)
PlayerId ensure_valid_day_target(PlayerId voter, PlayerId wanted,
                                 const GameState& st, Rng& rng) {
    if (wanted != voter &&
        wanted < st.players().size() &&
        st.players()[wanted] && st.players()[wanted]->is_alive()) {
        return wanted;
    }
    // Иначе случайный живой не-self
    auto ids = alive_ids(st);
    ids.erase(std::remove(ids.begin(), ids.end(), voter), ids.end());
    if (ids.empty()) return voter; // крайний случай (не должен случаться)
    auto it = rng.choose(ids.begin(), ids.end());
    return *it;
}

inline Team team_of(Role r) noexcept {
    switch (r) {
        case Role::Citizen:     return Team::Town;
        case Role::Detective:   return Team::Town;
        case Role::Doctor:      return Team::Town;
        case Role::Executioner: return Team::Town;
        case Role::Journalist:  return Team::Town;
        case Role::Eavesdropper:return Team::Town;
        case Role::Mafia:       return Team::Mafia;
        case Role::Maniac:      return Team::Maniac;
    }
    return Team::Town;
}

} // namespace

// ------------------------------------------------------------

GameEngine::GameEngine(const GameConfig& cfg, util::Logger& root_logger)
    : cfg_(cfg)
    , root_(&root_logger)
    , rng_(cfg.seed == 0 ? Rng() : Rng(cfg.seed))
    , state_(smart::make_shared_like<GameState>())
    , moderator_(smart::make_shared_like<Moderator>(cfg_, state_, root_logger, rng_))
{
    // Создадим каталог логов заранее (тихий режим)
    std::error_code ec;
    std::filesystem::create_directories(cfg_.logs_dir, ec);
}

GameEngine::~GameEngine() {
    stop_player_threads_();
}

void GameEngine::run() {
    setup_players_();

    // Инициализация барьеров и запуск потоков
    const std::size_t n = state_->players().size();
    if (n == 0) throw std::runtime_error("GameEngine::run: no players");

    const std::ptrdiff_t participants = static_cast<std::ptrdiff_t>(n + 1); // + ведущий (движок)

    day_start_   = std::make_unique<std::barrier<>>(participants);
    day_end_     = std::make_unique<std::barrier<>>(participants);
    night_start_ = std::make_unique<std::barrier<>>(participants);
    night_end_   = std::make_unique<std::barrier<>>(participants);

    start_player_threads_();

    // Основной цикл: День -> проверка конца -> Ночь -> проверка конца -> следующий раунд
    while (!stop_.load(std::memory_order_acquire) && !state_->is_game_over()) {
        state_->set_phase(Phase::Day);
        do_day_cycle_mt_();
        if (check_end_conditions_()) break;

        state_->set_phase(Phase::Night);
        do_night_cycle_mt_();
        if (check_end_conditions_()) break;

        state_->next_round();
    }

    // Акуратное завершение: сообщим потокам об остановке и «выпадем» из всех барьеров
    stop_player_threads_();
}

// -------------------- private --------------------

void GameEngine::setup_players_() {
    auto& vec = state_->players_mut();
    vec.clear();
    vec.reserve(cfg_.n_players);

    // Подсчёт ролей
    const std::size_t total = cfg_.n_players;

    // Базовая формула мафии
    std::size_t mafia_cnt = std::max<std::size_t>(1, total / std::max<std::size_t>(3, cfg_.k_mafia_divisor));

    // Обязательные роли
    const std::size_t detective_cnt = 1;
    const std::size_t doctor_cnt    = 1;
    const std::size_t maniac_cnt    = 1;

    // Доп. роли из ТЗ (из конфига)
    const std::size_t executioner_cnt  = std::min<std::size_t>(cfg_.executioner_count, 1);
    const std::size_t journalist_cnt   = std::min<std::size_t>(cfg_.journalist_count, 1);
    const std::size_t eavesdropper_cnt = std::min<std::size_t>(cfg_.eavesdropper_count, 1);

    // Всего нефиллерами
    std::size_t fixed = mafia_cnt + detective_cnt + doctor_cnt + maniac_cnt
                        + executioner_cnt + journalist_cnt + eavesdropper_cnt;

    if (fixed > total) {
        throw std::runtime_error("GameEngine::setup_players_: not enough slots for mandatory + extra roles");
    }

    const std::size_t citizens_cnt = total - fixed;

    // Сформируем «мешок» ролей
    std::vector<Role> bag;
    bag.insert(bag.end(), mafia_cnt,       Role::Mafia);
    bag.insert(bag.end(), detective_cnt,   Role::Detective);
    bag.insert(bag.end(), doctor_cnt,      Role::Doctor);
    bag.insert(bag.end(), maniac_cnt,      Role::Maniac);
    bag.insert(bag.end(), executioner_cnt, Role::Executioner);
    bag.insert(bag.end(), journalist_cnt,  Role::Journalist);
    bag.insert(bag.end(), eavesdropper_cnt,Role::Eavesdropper);
    bag.insert(bag.end(), citizens_cnt,    Role::Citizen);

    rng_.shuffle(bag.begin(), bag.end());

    // Если нужен один human — выберем случайный индекс
    std::optional<std::size_t> human_idx;
    if (cfg_.human && !bag.empty()) {
        human_idx = static_cast<std::size_t>(rng_.uniform_int(0, static_cast<int>(bag.size() - 1)));
    }

    rng_per_player_.clear();
    rng_per_player_.reserve(bag.size());
    for (std::size_t i = 0; i < bag.size(); ++i) {
        unsigned seed_i;
        if (cfg_.seed == 0) {
            seed_i = static_cast<unsigned>(std::random_device{}());
        } else {
            // Разнесём последовательности по id: простая смесь от общего seed
            seed_i = cfg_.seed ^ (0x9E3779B9u * static_cast<unsigned>(i + 1));
        }
        rng_per_player_.emplace_back(seed_i);
    }

    // Создание игроков
    for (std::size_t i = 0; i < bag.size(); ++i) {
        const auto rid = static_cast<PlayerId>(i);
        const std::string pname = (human_idx && *human_idx == i)
                                  ? "You"
                                  : "Player_" + std::to_string(i + 1);

        // Если этот индекс — человек, делаем интерактив с реальной ролью
        if (human_idx && *human_idx == i) {
            const Role r = bag[i];
            const Team t = team_of(r);
            vec.emplace_back(smart::make_shared_like<roles::Human>(rid, pname, r, t, state_, rng_per_player_[i]));
            continue;
        }

        // Иначе — ИИ-роль
        switch (bag[i]) {
            case Role::Citizen:
                vec.emplace_back(smart::make_shared_like<roles::Citizen>(rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Mafia:
                vec.emplace_back(smart::make_shared_like<roles::Mafia>(rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Detective:
                vec.emplace_back(smart::make_shared_like<roles::Detective>(rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Doctor:
                vec.emplace_back(smart::make_shared_like<roles::Doctor>(rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Maniac:
                vec.emplace_back(smart::make_shared_like<roles::Maniac>(rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Executioner:
                vec.emplace_back(smart::make_shared_like<roles::Executioner>(rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Journalist:
                vec.emplace_back(smart::make_shared_like<roles::Journalist>(rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Eavesdropper:
                vec.emplace_back(smart::make_shared_like<roles::Eavesdropper>(rid, pname, state_, rng_per_player_[i]));
                break;
        }
    }

    if (root_) {
        root_->info("GameEngine: players initialized: " + std::to_string(vec.size()) +
                    (cfg_.human ? " (with 1 human)" : ""));
    }
}

void GameEngine::start_player_threads_() {
    const auto n = state_->players().size();
    player_threads_.clear();
    player_threads_.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        player_threads_.emplace_back([this, i](std::stop_token){
            player_thread_loop_(i);
        });
    }
}

void GameEngine::stop_player_threads_() {
    // Сообщаем об остановке
    stop_.store(true, std::memory_order_release);

    // «Выпадаем» ведущим из всех барьеров, чтобы не держать игроков
    if (day_start_)   day_start_->arrive_and_drop();
    if (day_end_)     day_end_->arrive_and_drop();
    if (night_start_) night_start_->arrive_and_drop();
    if (night_end_)   night_end_->arrive_and_drop();

    // jthread сам join-ится в деструкторе; очистим вектор, чтобы дождаться
    player_threads_.clear();
}

void GameEngine::do_day_cycle_mt_() {
    moderator_->clear_day_votes();

    // Сигнал старт дня и ждём, пока все игроки завершат речь/голосование
    day_start_->arrive_and_wait();
    day_end_->arrive_and_wait();

    // Подсчёт и применение результата
    (void)moderator_->resolve_day_lynch();
}

void GameEngine::do_night_cycle_mt_() {
    // Сигнал старт ночи и ожидание завершения действий
    night_start_->arrive_and_wait();
    night_end_->arrive_and_wait();

    // Разрешить ночь
    (void)moderator_->resolve_night();
}

bool GameEngine::check_end_conditions_() {
    auto w = moderator_->evaluate_winner();
    if (w != Winner::None) {
        state_->set_game_over(w);
        if (root_) {
            std::string msg = "Game over. Winner: ";
            switch (w) {
                case Winner::Town:   msg += "Town";   break;
                case Winner::Mafia:  msg += "Mafia";  break;
                case Winner::Maniac: msg += "Maniac"; break;
                default:             msg += "None";   break;
            }
            root_->info(msg);
        }

        // Дополнительно: закрыть незаписанный раунд (если игра завершилась днём)
        // и записать итоговую сводку.
        moderator_->finalize_round_file_if_pending();
        moderator_->write_summary_file();

        return true;
    }
    return false;
}

void GameEngine::player_thread_loop_(std::size_t idx) {
    const auto my_id = static_cast<PlayerId>(idx);

    for (;;) {
        if (stop_.load(std::memory_order_acquire)) break;

        // -------- День --------
        day_start_->arrive_and_wait();
        if (stop_.load(std::memory_order_acquire)) break;

        // Дневное действие и голосование
        {
            const auto& players = state_->players();
            auto& self = players[idx];
            if (self && self->is_alive()) {
                self->on_day(*moderator_);
                PlayerId target = self->vote_day(*moderator_);
                target = ensure_valid_day_target(my_id, target, *state_, rng_per_player_[idx]);
                moderator_->submit_day_vote(my_id, target);
            }
        }

        day_end_->arrive_and_wait();
        if (stop_.load(std::memory_order_acquire)) break;

        // -------- Ночь --------
        night_start_->arrive_and_wait();
        if (stop_.load(std::memory_order_acquire)) break;

        {
            const auto& players = state_->players();
            auto& self = players[idx];
            if (self && self->is_alive()) {
                self->on_night(*moderator_);
            }
        }

        night_end_->arrive_and_wait();
        if (stop_.load(std::memory_order_acquire)) break;
    }
}

} // namespace core
