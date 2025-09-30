#include "core/game_engine_coro.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/game_config.hpp"
#include "core/game_state.hpp"
#include "core/moderator.hpp"
#include "core/phases.hpp"
#include "core/rng.hpp"

#include "coro/scheduler.hpp"
#include "coro/task.hpp"

#include "roles/i_player.hpp"
#include "roles/human.hpp"
#include "roles/citizen.hpp"
#include "roles/mafia.hpp"
#include "roles/detective.hpp"
#include "roles/doctor.hpp"
#include "roles/maniac.hpp"
#include "roles/executioner.hpp"
#include "roles/journalist.hpp"
#include "roles/eavesdropper.hpp"

#include "smart/shared_like.hpp"
#include "util/logger.hpp"

namespace core {

// Единственная версия: core::team_of(roles::Role) -> roles::Team
inline roles::Team team_of(roles::Role r) noexcept {
    using roles::Role;
    using roles::Team;
    switch (r) {
        case Role::Citizen:      return Team::Town;
        case Role::Detective:    return Team::Town;
        case Role::Doctor:       return Team::Town;
        case Role::Executioner:  return Team::Town;
        case Role::Journalist:   return Team::Town;
        case Role::Eavesdropper: return Team::Town;
        case Role::Mafia:        return Team::Mafia;
        case Role::Maniac:       return Team::Maniac;
    }
    return Team::Town;
}

namespace {
    using core::GameState;
    using core::PlayerId;
    using core::Rng;

    std::vector<PlayerId> alive_ids(const GameState& st) {
        std::vector<PlayerId> ids;
        const auto& ps = st.players();
        ids.reserve(ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (ps[i] && ps[i]->is_alive()) {
                ids.push_back(static_cast<PlayerId>(i));
            }
        }
        return ids;
    }

    // Возвращает валидную цель (живая и не self). Если только self жив — вернёт self.
    PlayerId ensure_valid_day_target(PlayerId voter, PlayerId wanted,
                                     const GameState& st, Rng& rng) {
        const auto& ps = st.players();
        const auto n = ps.size();

        auto ok = [&](PlayerId id) -> bool {
            return id < n && ps[id] && ps[id]->is_alive() && id != voter;
        };

        if (ok(wanted)) {
            return wanted;
        }

        auto ids = alive_ids(st);
        ids.erase(std::remove(ids.begin(), ids.end(), voter), ids.end());
        if (ids.empty()) {
            return voter; // крайний случай
        }

        auto it = rng.choose(ids.begin(), ids.end());
        return *it;
    }
} // namespace

// ==============================================================
// GameEngineCoro
// ==============================================================


GameEngineCoro::GameEngineCoro(const GameConfig& cfg, util::Logger& root_logger)
        : cfg_(cfg)
        , root_(&root_logger)
        , rng_(cfg.seed)
        , rng_per_player_()
        // GameState конструируется по умолчанию (в вашем интерфейсе нет ctor(GameConfig))
        , state_(smart::make_shared_like<GameState>())
        // По сообщениям компилятора у вас moderator_ — std::unique_ptr<Moderator>
        , moderator_(std::make_unique<Moderator>(cfg_, state_, root_logger, rng_))
        , scheduler_()
        , day_start_{0}
    , day_end_{0}
    , night_start_{0}
    , night_end_{0}
    , stop_{false}
{
}


void GameEngineCoro::run() {
    // Создать игроков и их личные RNG
    init_players_();

    const std::size_t n = state_->players().size();
    if (n == 0) {
        if (root_) root_->warn("No players to run; exiting");
        return;
    }

    // Инициализация барьеров на n участников (ровно по корутине на игрока)
    day_start_.set_expected(n);
    day_end_.set_expected(n);
    night_start_.set_expected(n);
    night_end_.set_expected(n);


    // Колбэки фаз
    day_start_.set_on_complete([this]() {
        // Начало дня: очистить дневные структуры и перевести фазу
        moderator_->clear_day_votes();
        state_->set_phase(Phase::Day);
    });

    day_end_.set_on_complete([this]() {
        // Конец дня: посчитать казнь / ничью
        (void)moderator_->resolve_day_lynch();

        // Проверить победителя; если есть — выставляем stop_
        if (check_end_conditions_()) {
            stop_ = true;
            return;
        }
        // Иначе — переходим в Night
        state_->set_phase(Phase::Night);
    });

    night_start_.set_on_complete([this]() {
        // На старте ночи специальных действий не требуется
    });

    night_end_.set_on_complete([this]() {
        // Конец ночи: разрешить все ночные действия
        (void)moderator_->resolve_night();

        // Проверить победителя
        if (check_end_conditions_()) {
            stop_ = true;
            return;
        }
        // Следующий раунд
        state_->next_round();
    });

    // Запланировать по корутине на игрока
    std::vector<coro::task> tasks;
    tasks.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        tasks.emplace_back(player_task_(i));
    }

    while (!stop_.load(std::memory_order_acquire) && !state_->is_game_over()) {
        bool progressed = false;
        for (auto& t : tasks) {
            if (!t.done()) { t.resume(); progressed = true; }
        }
        if (!progressed) {
            for (auto& t : tasks) if (!t.done()) t.resume();
            bool any = false; for (auto& t : tasks) any |= !t.done();
            if (!any) break;
        }
    }


    // На случай финала днём — дозакрыть файл раунда
    moderator_->finalize_round_file_if_pending();
}

// --------------------------------------------------------------
// Локальная логика
// --------------------------------------------------------------

void GameEngineCoro::init_players_() {
    using roles::Role;
    using roles::Team;

    // Готовим контейнер игроков к заполнению
    auto& vec = state_->players_mut();
    vec.clear();
    vec.reserve(cfg_.n_players);

    // --- Подсчёт ролей ---
    const std::size_t total = cfg_.n_players;

    // Базовая формула мафии (как в потоковом движке)
    std::size_t mafia_cnt =
        std::max<std::size_t>(1, total / std::max<std::size_t>(3, cfg_.k_mafia_divisor));

    // Обязательные роли
    const std::size_t detective_cnt = 1;
    const std::size_t doctor_cnt    = 1;
    const std::size_t maniac_cnt    = 1;

    // Доп. роли из ТЗ (из конфига; не более одной каждой)
    const std::size_t executioner_cnt  = std::min<std::size_t>(cfg_.executioner_count,  1);
    const std::size_t journalist_cnt   = std::min<std::size_t>(cfg_.journalist_count,   1);
    const std::size_t eavesdropper_cnt = std::min<std::size_t>(cfg_.eavesdropper_count, 1);

    // Всего нефиллерами
    std::size_t fixed = mafia_cnt + detective_cnt + doctor_cnt + maniac_cnt
                        + executioner_cnt + journalist_cnt + eavesdropper_cnt;

    if (fixed > total) {
        throw std::runtime_error("GameEngineCoro::init_players_: not enough slots for mandatory + extra roles");
    }

    const std::size_t citizens_cnt = total - fixed;

    // --- «Мешок» ролей и тасование ---
    std::vector<Role> bag;
    bag.insert(bag.end(), mafia_cnt,        Role::Mafia);
    bag.insert(bag.end(), detective_cnt,    Role::Detective);
    bag.insert(bag.end(), doctor_cnt,       Role::Doctor);
    bag.insert(bag.end(), maniac_cnt,       Role::Maniac);
    bag.insert(bag.end(), executioner_cnt,  Role::Executioner);
    bag.insert(bag.end(), journalist_cnt,   Role::Journalist);
    bag.insert(bag.end(), eavesdropper_cnt, Role::Eavesdropper);
    bag.insert(bag.end(), citizens_cnt,     Role::Citizen);

    rng_.shuffle(bag.begin(), bag.end());

    // --- Выбор интерактивного игрока (если включено) ---
    std::optional<std::size_t> human_idx;
    if (cfg_.human && !bag.empty()) {
        human_idx = static_cast<std::size_t>(
            rng_.uniform_int(0, static_cast<int>(bag.size() - 1)));
    }

    // --- Персональные RNG на игрока ---
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

    // --- Создание игроков ---
    for (std::size_t i = 0; i < bag.size(); ++i) {
        const auto rid = static_cast<PlayerId>(i);
        const std::string pname = (human_idx && *human_idx == i)
                                  ? "You"
                                  : std::string("Player_") + std::to_string(i + 1);

        // Если этот индекс — человек, делаем интерактив с реальной ролью
        if (human_idx && *human_idx == i) {
            const roles::Role r = bag[i];
            const roles::Team t = core::team_of(r);
            vec.emplace_back(smart::make_shared_like<roles::Human>(rid, pname, r, t, state_, rng_per_player_[i]));
            continue;
        }

        // Иначе — ИИ-роль
        switch (bag[i]) {
            case Role::Citizen:
                vec.emplace_back(smart::make_shared_like<roles::Citizen>(
                    rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Mafia:
                vec.emplace_back(smart::make_shared_like<roles::Mafia>(
                    rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Detective:
                vec.emplace_back(smart::make_shared_like<roles::Detective>(
                    rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Doctor:
                vec.emplace_back(smart::make_shared_like<roles::Doctor>(
                    rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Maniac:
                vec.emplace_back(smart::make_shared_like<roles::Maniac>(
                    rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Executioner:
                vec.emplace_back(smart::make_shared_like<roles::Executioner>(
                    rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Journalist:
                vec.emplace_back(smart::make_shared_like<roles::Journalist>(
                    rid, pname, state_, rng_per_player_[i]));
                break;
            case Role::Eavesdropper:
                vec.emplace_back(smart::make_shared_like<roles::Eavesdropper>(
                    rid, pname, state_, rng_per_player_[i]));
                break;
        }
    }

    if (root_) {
        root_->info("GameEngineCoro: players initialized: " + std::to_string(vec.size()) +
                    (cfg_.human ? " (with 1 human)" : ""));
    }
}


coro::task GameEngineCoro::player_task_(std::size_t idx) {
    for (;;) {
        // --- День ---
        co_await day_start_.arrive();
        if (stop_) co_return;

        {
            auto& players = state_->players_mut();
            auto& self = players[idx];
            if (self && self->is_alive()) {
                self->on_day(*moderator_);
                PlayerId raw = self->vote_day(*moderator_);
                PlayerId valid = ensure_valid_day_target(static_cast<PlayerId>(idx), raw, *state_, rng_per_player_[idx]);
                moderator_->submit_day_vote(static_cast<PlayerId>(idx), valid);
            }
        }



        co_await day_end_.arrive();
        if (stop_) co_return;

        // --- Ночь ---
        co_await night_start_.arrive();
        if (stop_) co_return;

        {
            auto& players = state_->players_mut();
            auto& self = players[idx];
            if (self && self->is_alive()) {
                self->on_night(*moderator_);
            }
        }

        co_await night_end_.arrive();
        if (stop_) co_return;
    }
}


bool GameEngineCoro::check_end_conditions_() {
    auto w = moderator_->evaluate_winner();
    if (w != Winner::None) {
        state_->set_game_over(w);

        // Если финал наступил днём — дозакрыть раундовый файл
        moderator_->finalize_round_file_if_pending();

        moderator_->write_summary_file();
        if (root_) {
            root_->info(std::string("Game over. Winner: ")
                        + (w == Winner::Town   ? "Town" :
                           w == Winner::Mafia  ? "Mafia" :
                           w == Winner::Maniac ? "Maniac" : "None"));
        }
        return true;
    }
    return false;
}

} // namespace core
