#include "core/moderator.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>      // для сборки строк логов
#include <filesystem>  // файловые пути
#include <fstream>     // запись файлов
#include <sstream>     // форматирование строк
#include <iomanip>

#include "core/game_state.hpp"
#include "core/rng.hpp"
#include "util/logger.hpp"
#include "roles/i_player.hpp"

// Доп. роли
#include "roles/executioner.hpp"   // для dynamic_cast и вызова decide_execution

namespace core {

using roles::IPlayer;
using roles::Team;
using roles::Role;

// -------------------- локальные помощники для печати --------------------

namespace {
    inline const char* role_ru(Role r) noexcept {
        switch (r) {
            case Role::Citizen:     return "Мирный житель";
            case Role::Mafia:       return "Мафия";
            case Role::Detective:   return "Комиссар";
            case Role::Doctor:      return "Доктор";
            case Role::Maniac:      return "Маньяк";
            case Role::Executioner: return "Палач";
            case Role::Journalist:  return "Журналист";
            case Role::Eavesdropper:return "Ушастик";
        }
        return "Неизвестно";
    }
    inline const char* team_ru(Team t) noexcept {
        switch (t) {
            case Team::Town:   return "Мирные";
            case Team::Mafia:  return "Мафия";
            case Team::Maniac: return "Маньяк";
        }
        return "Неизвестно";
    }
    inline std::string player_tag(const IPlayer& p, std::size_t idx) {
        std::ostringstream os;
        os << "#" << (idx + 1) << " " << p.name();
        return os.str();
    }
}

// -------------------- Moderator --------------------

Moderator::Moderator(const GameConfig& cfg,
                     smart::shared_like<GameState> state,
                     util::Logger& root_logger,
                     Rng& rng)
    : cfg_(cfg)
    , state_(std::move(state))
    , root_(&root_logger)
    , rng_(&rng)
{
    const auto n = state_->players().size();

    // Дневные структуры
    day_votes_.resize(n);

    // Ночные структуры
    mafia_vote_counts_.assign(n, 0);
    detective_shot_.reset();
    doctor_heal_.reset();
    maniac_target_.reset();

    journalist_queries_.clear();
    eavesdrop_requests_.clear();

    // Статистика
    stats_votes_given_day_.assign(n, 0);
    stats_votes_received_day_.assign(n, 0);
    stats_mafia_votes_.assign(n, 0);
    stats_detective_shots_.assign(n, 0);
    stats_doctor_heals_.assign(n, 0);
    stats_maniac_targets_.assign(n, 0);
    stats_died_round_.assign(n, 0);

    day_voted_flag_.assign(n, false);

    // раундовый лог
    round_index_   = 0;
    round_written_ = false;
    round_log_.clear();
}

// ---------- День ----------

void Moderator::clear_day_votes() {
    {
        const auto n = state_->players().size();
        std::scoped_lock lk(mu_);

        // Дневные буферы
        day_votes_.assign(n, std::nullopt);
        day_voted_flag_.assign(n, false);

        // Гарантируем размеры статистик (сохраняя накопленное)
        if (stats_votes_given_day_.size()      < n) stats_votes_given_day_.resize(n, 0);
        if (stats_votes_received_day_.size()   < n) stats_votes_received_day_.resize(n, 0);
        if (stats_mafia_votes_.size()          < n) stats_mafia_votes_.resize(n, 0);
        if (stats_detective_shots_.size()      < n) stats_detective_shots_.resize(n, 0);
        if (stats_doctor_heals_.size()         < n) stats_doctor_heals_.resize(n, 0);
        if (stats_maniac_targets_.size()       < n) stats_maniac_targets_.resize(n, 0);
        if (stats_died_round_.size()           < n) stats_died_round_.resize(n, 0);

        // Ночная структура tally мафии на случай изменения n
        if (mafia_vote_counts_.size() != n) mafia_vote_counts_.assign(n, 0);

        // Начало раунда (дневная часть)
        round_begin_day_();
    }

    if (root_) {
        root_->info("Day: round " + std::to_string(round_index_) + " begins");
    }
}

void Moderator::submit_day_vote(PlayerId voter, PlayerId target) {
    const auto n = state_->players().size();
    const auto& ps = state_->players();
    if (voter >= n || target >= n) return;
    if (!ps[voter] || !ps[target]) return;
    if (!ps[voter]->is_alive() || !ps[target]->is_alive()) return;

    {
        std::scoped_lock lk(mu_);
        if (day_votes_.size() != n) day_votes_.resize(n);

        // статистика: засчитываем «отдал голос сегодня» только один раз за день
        if (voter < day_voted_flag_.size() && !day_voted_flag_[voter]) {
            day_voted_flag_[voter] = true;
            if (voter < stats_votes_given_day_.size())
                ++stats_votes_given_day_[voter];
        }

        day_votes_[voter] = target;

        // файл-лог (раунд): фиксируем каждый (возможно, сменяемый) голос
        {
            std::ostringstream os;
            os << "DAY: vote " << player_tag(*ps[voter], voter)
               << " -> " << player_tag(*ps[target], target) << "\n";
            round_append_(os.str());
        }
    }

    if (is_full_logs_() && root_) {
        root_->info("Day vote: #" + std::to_string(voter + 1) +
                    " -> #" + std::to_string(target + 1));
    }
}

std::optional<PlayerId> Moderator::resolve_day_lynch() {
    const auto& ps = state_->players();
    const auto n = ps.size();

    // Снимем снапшот голосов под мьютексом
    std::vector<std::optional<PlayerId>> votes_copy;
    {
        std::scoped_lock lk(mu_);
        if (day_votes_.size() != n) day_votes_.resize(n);
        votes_copy = day_votes_;
    }

    if (is_full_logs_() && root_) {
        std::string s = "Day votes:";
        for (std::size_t i = 0; i < n; ++i) {
            if (!ps[i] || !ps[i]->is_alive()) continue;
            s += " #" + std::to_string(i + 1) + "->";
            if (votes_copy[i].has_value()) {
                s += "#" + std::to_string(votes_copy[i].value() + 1);
            } else {
                s += "-";
            }
        }
        root_->info(s);
    }

    std::vector<int> tally(n, 0);

    // Посчитаем валидные конечные голоса и одновременно обновим «полученные голоса»
    {
        for (std::size_t v = 0; v < n; ++v) {
            if (!ps[v] || !ps[v]->is_alive()) continue;
            if (!votes_copy[v].has_value()) continue;
            auto t = votes_copy[v].value();
            if (t >= n || !ps[t] || !ps[t]->is_alive()) continue;
            ++tally[t];
        }
        // Статистика «полученные голоса» (по финальным бюллетеням)
        std::scoped_lock lk(mu_);
        for (std::size_t i = 0; i < n; ++i) {
            if (tally[i] > 0 && i < stats_votes_received_day_.size()) {
                stats_votes_received_day_[i] += tally[i];
            }
        }
    }

    // Найти максимум голосов
    int max_votes = 0;
    for (auto c : tally) max_votes = std::max(max_votes, c);
    if (max_votes <= 0) {
        if (root_) root_->info("Day: no valid votes; nobody is lynched");
        round_append_("DAY: no lynch\n");
        return std::nullopt;
    }

    // Кандидаты с максимумом
    std::vector<PlayerId> leaders;
    for (std::size_t i = 0; i < n; ++i) {
        if (tally[i] == max_votes) leaders.push_back(static_cast<PlayerId>(i));
    }

    if (leaders.size() > 1) {
        if (cfg_.tie_policy == GameConfig::TiePolicy::None) {
            // Попытка разрешить ничью через Палача (Executioner)
            auto ex_victim = resolve_tie_via_executioner_(leaders);
            if (!ex_victim.has_value()) {
                if (root_) root_->info("Day: tie detected; tie policy = none -> nobody is lynched");
                round_append_("DAY: tie -> no lynch\n");
                return std::nullopt;
            }
            // Палач выбрал жертву
            PlayerId victim = ex_victim.value();
            {
                std::ostringstream os;
                os << "DAY: executioner-lynch " << player_tag(*ps[victim], victim)
                   << " (" << role_ru(ps[victim]->role()) << ")\n";
                round_append_(os.str());
            }
            if (root_) {
                root_->info("Day: executioner chose victim #" + std::to_string(victim + 1));
            }

            // Зафиксировать раунд смерти
            {
                std::scoped_lock lk(mu_);
                if (victim < stats_died_round_.size() && stats_died_round_[victim] == 0) {
                    stats_died_round_[victim] = round_index_;
                }
            }
            kill_player(victim);
            return victim;
        }

        if (cfg_.tie_policy == GameConfig::TiePolicy::Random) {
            auto it = rng_->choose(leaders.begin(), leaders.end());
            PlayerId victim = *it;
            if (root_) root_->info("Day: tie detected; victim chosen randomly");
            round_append_("DAY: tie -> victim chosen randomly\n");

            // Лог и убийство
            {
                std::ostringstream os;
                os << "DAY: lynch victim " << player_tag(*ps[victim], victim)
                   << " (" << role_ru(ps[victim]->role()) << ")\n";
                round_append_(os.str());
            }
            if (root_) {
                if (is_open_()) {
                    root_->info("Day: lynched #" + std::to_string(victim + 1) +
                                " (" + role_ru(ps[victim]->role()) + ")");
                } else {
                    root_->info("Day: lynched player #" + std::to_string(victim + 1));
                }
            }
            {
                std::scoped_lock lk(mu_);
                if (victim < stats_died_round_.size() && stats_died_round_[victim] == 0) {
                    stats_died_round_[victim] = round_index_;
                }
            }
            kill_player(victim);
            return victim;
        }
    }

    // Обычный случай — единственный лидер
    PlayerId victim = leaders.front();

    // Лог до убийства — чтобы в файл записать роль
    {
        std::ostringstream os;
        os << "DAY: lynch victim " << player_tag(*ps[victim], victim)
           << " (" << role_ru(ps[victim]->role()) << ")\n";
        round_append_(os.str());
    }

    if (root_) {
        if (is_open_()) {
            root_->info("Day: lynched #" + std::to_string(victim + 1) +
                        " (" + role_ru(ps[victim]->role()) + ")");
        } else {
            root_->info("Day: lynched player #" + std::to_string(victim + 1));
        }
    }

    // Зафиксировать раунд смерти для дневной казни
    {
        std::scoped_lock lk(mu_);
        if (victim < stats_died_round_.size() && stats_died_round_[victim] == 0) {
            stats_died_round_[victim] = round_index_;
        }
    }

    kill_player(victim);
    return victim;
}

// ---------- Ночь ----------

void Moderator::mafia_vote_target(PlayerId mafia_id, PlayerId target) {
    const auto n = state_->players().size();
    const auto& ps = state_->players();
    if (mafia_id >= n || target >= n) return;
    if (!ps[mafia_id] || !ps[target]) return;
    if (!ps[mafia_id]->is_alive() || !ps[target]->is_alive()) return;
    if (!is_mafia(mafia_id)) return;

    {
        std::scoped_lock lk(mu_);
        if (mafia_vote_counts_.size() != n) mafia_vote_counts_.assign(n, 0);
        ++mafia_vote_counts_[target];
        if (mafia_id < stats_mafia_votes_.size()) ++stats_mafia_votes_[mafia_id];

        // файл-лог (раунд): запись голоса мафии
        std::ostringstream os;
        os << "NIGHT: mafia-vote " << player_tag(*ps[mafia_id], mafia_id)
           << " -> " << player_tag(*ps[target], target) << "\n";
        round_append_(os.str());
    }

    if (is_full_logs_() && is_open_() && root_) {
        root_->info("Night (open): mafia vote by #" + std::to_string(mafia_id + 1) +
                    " -> #" + std::to_string(target + 1));
    }
}

bool Moderator::investigate(PlayerId /*detective_id*/, PlayerId target) const {
    // Маньяк считается "не маф"
    return is_mafia(target);
}

void Moderator::set_detective_shot(PlayerId detective_id, PlayerId target) {
    const auto& ps = state_->players();
    const auto n = ps.size();
    if (detective_id >= n || target >= n) return;
    if (!ps[detective_id] || !ps[target]) return;
    if (!ps[detective_id]->is_alive() || !ps[target]->is_alive()) return;

    {
        std::scoped_lock lk(mu_);
        detective_shot_ = target;
        if (detective_id < stats_detective_shots_.size()) ++stats_detective_shots_[detective_id];

        std::ostringstream os;
        os << "NIGHT: detective-shot -> " << player_tag(*ps[target], target) << "\n";
        round_append_(os.str());
    }
    if (is_full_logs_() && is_open_() && root_) {
        root_->info("Night (open): detective shot -> #" + std::to_string(target + 1));
    }
}

void Moderator::set_doctor_heal(PlayerId doctor_id, PlayerId target) {
    const auto& ps = state_->players();
    const auto n = ps.size();
    if (doctor_id >= n || target >= n) return;
    if (!ps[doctor_id] || !ps[target]) return;
    if (!ps[doctor_id]->is_alive() || !ps[target]->is_alive()) return;

    {
        std::scoped_lock lk(mu_);
        doctor_heal_ = target;
        if (doctor_id < stats_doctor_heals_.size()) ++stats_doctor_heals_[doctor_id];

        std::ostringstream os;
        os << "NIGHT: doctor-heal " << player_tag(*ps[target], target) << "\n";
        round_append_(os.str());
    }
    if (is_full_logs_() && is_open_() && root_) {
        root_->info("Night (open): doctor heals #" + std::to_string(target + 1));
    }
}

void Moderator::set_maniac_target(PlayerId maniac_id, PlayerId target) {
    const auto& ps = state_->players();
    const auto n = ps.size();
    if (maniac_id >= n || target >= n) return;
    if (!ps[maniac_id] || !ps[target]) return;
    if (!ps[maniac_id]->is_alive() || !ps[target]->is_alive()) return;
    if (!is_maniac(maniac_id)) return;

    {
        std::scoped_lock lk(mu_);
        maniac_target_ = target;
        if (maniac_id < stats_maniac_targets_.size()) ++stats_maniac_targets_[maniac_id];

        std::ostringstream os;
        os << "NIGHT: maniac-target -> " << player_tag(*ps[target], target) << "\n";
        round_append_(os.str());
    }
    if (is_full_logs_() && is_open_() && root_) {
        root_->info("Night (open): maniac targets #" + std::to_string(target + 1));
    }
}

// Журналист
void Moderator::set_journalist_compare(PlayerId journalist_id, PlayerId a, PlayerId b) {
    const auto& ps = state_->players();
    const auto n = ps.size();
    if (journalist_id >= n || a >= n || b >= n) return;
    if (a == b) return;
    if (journalist_id == a || journalist_id == b) return;
    if (!ps[journalist_id] || !ps[a] || !ps[b]) return;
    if (!ps[journalist_id]->is_alive() || !ps[a]->is_alive() || !ps[b]->is_alive()) return;

    {
        std::scoped_lock lk(mu_);
        journalist_queries_.push_back({journalist_id, a, b});

        std::ostringstream os;
        os << "NIGHT: journalist-compare by " << player_tag(*ps[journalist_id], journalist_id)
           << " -> " << player_tag(*ps[a], a) << " vs " << player_tag(*ps[b], b) << "\n";
        round_append_(os.str());
    }
}

// Ушастик
void Moderator::set_eavesdropper_target(PlayerId eavesdropper_id, PlayerId target) {
    const auto& ps = state_->players();
    const auto n = ps.size();
    if (eavesdropper_id >= n || target >= n) return;
    if (eavesdropper_id == target) return;
    if (!ps[eavesdropper_id] || !ps[target]) return;
    if (!ps[eavesdropper_id]->is_alive() || !ps[target]->is_alive()) return;

    {
        std::scoped_lock lk(mu_);
        eavesdrop_requests_.emplace_back(eavesdropper_id, target);

        std::ostringstream os;
        os << "NIGHT: eavesdropper-target by " << player_tag(*ps[eavesdropper_id], eavesdropper_id)
           << " -> " << player_tag(*ps[target], target) << "\n";
        round_append_(os.str());
    }
}

std::vector<PlayerId> Moderator::resolve_night() {
    const auto& ps = state_->players();
    const auto n = ps.size();

    // ---- Снимем снапшоты ночных намерений под мьютексом ----
    std::vector<int> mafia_counts_copy;
    std::optional<PlayerId> detective_shot_copy;
    std::optional<PlayerId> doctor_heal_copy;
    std::optional<PlayerId> maniac_target_copy;

    std::vector<JournalistQuery> journalist_copy;
    std::vector<std::pair<PlayerId, PlayerId>> eavesdrop_copy;

    {
        std::scoped_lock lk(mu_);
        if (mafia_vote_counts_.size() != n) mafia_vote_counts_.assign(n, 0);
        mafia_counts_copy   = mafia_vote_counts_;
        detective_shot_copy = detective_shot_;
        doctor_heal_copy    = doctor_heal_;
        maniac_target_copy  = maniac_target_;

        journalist_copy     = journalist_queries_;
        eavesdrop_copy      = eavesdrop_requests_;
    }

    if (is_full_logs_() && root_) {
        if (is_open_()) {
            // Покажем расклад голосов мафии (агрегированно)
            std::string s = "Night (open): mafia tally:";
            bool any = false;
            for (std::size_t i = 0; i < n; ++i) {
                if (mafia_counts_copy[i] > 0 && ps[i] && ps[i]->is_alive()) {
                    s += " #" + std::to_string(i + 1) + "(" + std::to_string(mafia_counts_copy[i]) + ")";
                    any = true;
                }
            }
            if (!any) s += " none";
            root_->info(s);
        } else {
            root_->info("Night: actions recorded (closed)");
        }
    }

    // Для файла — агрегирование голосов мафии:
    {
        std::ostringstream os;
        os << "NIGHT: mafia-tally";
        bool any = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (mafia_counts_copy[i] > 0 && ps[i] && ps[i]->is_alive()) {
                os << " " << player_tag(*ps[i], i) << "(" << mafia_counts_copy[i] << ")";
                any = true;
            }
        }
        if (!any) os << " none";
        os << "\n";
        round_append_(os.str());
    }

    // ---- Выбрать цель мафии по максимуму голосов (при ничьей — случайно) ----
    std::optional<PlayerId> mafia_target;
    {
        int maxv = 0;
        for (int c : mafia_counts_copy) maxv = std::max(maxv, c);
        if (maxv > 0) {
            std::vector<PlayerId> cands;
            for (std::size_t i = 0; i < n; ++i) {
                if (mafia_counts_copy[i] == maxv &&
                    ps[i] && ps[i]->is_alive()) {
                    cands.push_back(static_cast<PlayerId>(i));
                }
            }
            if (!cands.empty()) {
                mafia_target = *rng_->choose(cands.begin(), cands.end());
            }
        }
    }

    // ---- Список "подстреленных" ----
    std::vector<bool> to_kill(n, false);

    auto mark_shot = [&](std::optional<PlayerId> tid, const char* src) {
        if (!tid.has_value()) return;
        auto t = tid.value();
        if (t < n && ps[t] && ps[t]->is_alive()) {
            to_kill[t] = true;
            std::ostringstream os;
            os << "NIGHT: marked-by-" << src << " " << player_tag(*ps[t], t) << "\n";
            round_append_(os.str());
        }
    };

    mark_shot(mafia_target, "mafia");
    mark_shot(detective_shot_copy, "detective");
    mark_shot(maniac_target_copy, "maniac");

    // Применить лечение доктора (если лечил)
    if (doctor_heal_copy.has_value()) {
        auto h = doctor_heal_copy.value();
        if (h < n && ps[h] && ps[h]->is_alive()) {
            if (is_full_logs_() && is_open_() && root_) {
                root_->info("Night (open): heal cancels death of #" + std::to_string(h + 1));
            }
            std::ostringstream os;
            os << "NIGHT: heal-cancels " << player_tag(*ps[h], h) << "\n";
            round_append_(os.str());
            to_kill[h] = false;
        }
    }

    // ---- Обработать запросы Журналиста ----
    for (const auto& q : journalist_copy) {
        if (q.a >= n || q.b >= n) continue;
        if (!ps[q.a] || !ps[q.b]) continue;
        // Сравниваем «статусы» как принадлежность к лагерю (Team)
        const bool same = (ps[q.a]->team() == ps[q.b]->team());
        std::ostringstream os;
        os << "NIGHT: journalist-result by " << player_tag(*ps[q.jid], q.jid)
           << " -> " << player_tag(*ps[q.a], q.a) << " vs " << player_tag(*ps[q.b], q.b)
           << " : " << (same ? "SAME" : "DIFFERENT") << "\n";
        round_append_(os.str());
    }

    // ---- Обработать запросы Ушастика ----
    for (const auto& [eid, tgt] : eavesdrop_copy) {
        if (tgt >= n || !ps[tgt]) continue;
        std::ostringstream os;
        os << "NIGHT: eavesdropper-result for " << player_tag(*ps[tgt], tgt)
           << " by " << player_tag(*ps[eid], eid) << " ->";
        bool any = false;

        if (tgt < mafia_counts_copy.size() && mafia_counts_copy[tgt] > 0) {
            os << " mafia(" << mafia_counts_copy[tgt] << ")";
            any = true;
        }
        if (detective_shot_copy.has_value() && detective_shot_copy.value() == tgt) {
            os << (any ? "," : "") << " det-shot";
            any = true;
        }
        if (doctor_heal_copy.has_value() && doctor_heal_copy.value() == tgt) {
            os << (any ? "," : "") << " doc-heal";
            any = true;
        }
        if (maniac_target_copy.has_value() && maniac_target_copy.value() == tgt) {
            os << (any ? "," : "") << " maniac";
            any = true;
        }
        if (!any) os << " none";
        os << "\n";
        round_append_(os.str());
    }

    // Сформировать список умерших и применить убийство
    std::vector<PlayerId> deaths;
    for (std::size_t i = 0; i < n; ++i) {
        if (to_kill[i]) {
            deaths.push_back(static_cast<PlayerId>(i));
        }
    }
    for (auto id : deaths) {
        // Для открытого режима сообщим роль до фактического убийства
        if (root_ && is_open_()) {
            root_->info("Night (open): #" + std::to_string(id + 1) +
                        " died (" + role_ru(ps[id]->role()) + ")");
        }
        // Статистика: раунд смерти
        {
            std::scoped_lock lk(mu_);
            if (id < stats_died_round_.size() && stats_died_round_[id] == 0)
                stats_died_round_[id] = round_index_;
        }
        // Файл-лог
        {
            std::ostringstream os;
            os << "NIGHT: death " << player_tag(*ps[id], id)
               << " (" << role_ru(ps[id]->role()) << ")\n";
            round_append_(os.str());
        }
        kill_player(id);
    }

    // Очистить буферы ночи
    clear_night_intents_();

    if (root_) {
        if (deaths.empty()) {
            root_->info("Night: no deaths");
        } else if (!is_open_()) {
            // Закрытые объявления — без ролей и причин
            std::string s = "Night: deaths:";
            for (auto id : deaths) {
                s += " #" + std::to_string(id + 1) + " (" + team_ru(ps[id]->team()) + ")";
            }
            root_->info(s);
        }
        // В открытом режиме уже напечатали поштучно выше
    }

    // Записать файл раунда (день+ночь завершены)
    round_write_file_(true);
    return deaths;
}

// ---------- Общие операции ----------

void Moderator::kill_player(PlayerId id) {
    const auto& ps = state_->players();
    const auto n = ps.size();
    if (id >= n) return;
    if (!ps[id]) return;
    if (!ps[id]->is_alive()) return;

    ps[id]->kill();

    if (root_ && !is_open_()) {
        // В закрытом режиме остаёмся на нейтральной формулировке
        root_->info("Player #" + std::to_string(id + 1) + " has died");
    }
}

Winner Moderator::evaluate_winner() const {
    const auto maf = alive_mafia_count_();
    const auto man = alive_maniac_count_();
    const auto town= alive_town_count_();

    // 1) Победа мирных: нет мафии и нет маньяка
    if (maf == 0 && man == 0) return Winner::Town;

    // 2) Победа маньяка: остались ровно маньяк и один мирный
    if (man == 1 && town == 1 && maf == 0) return Winner::Maniac;

    // 3) Победа мафии: паритет/превосходство над остальными
    if (maf > 0 && maf >= (town + man)) return Winner::Mafia;

    return Winner::None;
}

void Moderator::log_info(std::string_view msg) {
    if (root_) root_->info(std::string(msg));
}

// ---------- файловые операции / раундовые логи и summary ----------

void Moderator::round_begin_day_() {
    // Начинаем новый раунд
    ++round_index_;
    round_written_ = false;
    round_log_.clear();

    const auto& ps = state_->players();

    std::ostringstream os;
    os << "=== ROUND " << round_index_ << " (Day) ===\n";
    os << "Alive at start of day:\n";
    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (ps[i] && ps[i]->is_alive()) {
            os << "  " << player_tag(*ps[i], i)
               << " | role=" << role_ru(ps[i]->role())
               << " | team=" << team_ru(ps[i]->team())
               << "\n";
        }
    }
    round_log_ += os.str();
}

void Moderator::round_append_(std::string_view line) {
    // line уже содержит '\n'
    round_log_ += std::string(line);
}

void Moderator::round_write_file_(bool night_completed) {
    // Пишем файл только один раз за раунд
    if (round_written_) return;

    std::error_code ec;
    std::filesystem::create_directories(cfg_.logs_dir, ec);

    const std::string fname = cfg_.logs_dir + "/round_" + std::to_string(round_index_) + ".txt";
    std::ofstream ofs(fname, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        if (root_) root_->error("Failed to write round file: " + fname);
        round_written_ = true; // чтобы не зациклиться попытками
        return;
    }

    // UTF-8 BOM для корректного отображения в редакторах Windows
    ofs << "\xEF\xBB\xBF";

    ofs << round_log_;
    ofs << "=== ROUND " << round_index_ << " END"
        << (night_completed ? " (night completed)" : " (no night)") << " ===\n";
    ofs.close();

    round_written_ = true;
}

void Moderator::finalize_round_file_if_pending() {
    std::scoped_lock lk(mu_);
    if (round_index_ > 0 && !round_written_) {
        round_write_file_(false);
    }
}

void Moderator::write_summary_file() const {
    std::error_code ec;
    std::filesystem::create_directories(cfg_.logs_dir, ec);

    const std::string fname = cfg_.logs_dir + "/summary.txt";
    std::ofstream ofs(fname, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        if (root_) root_->error("Failed to write summary file: " + fname);
        return;
    }

    // UTF-8 BOM
    ofs << "\xEF\xBB\xBF";

    // Заголовок и победитель
    const auto winner = const_cast<Moderator*>(this)->evaluate_winner();
    ofs << "=== SUMMARY ===\n";
    ofs << "Winner: ";
    switch (winner) {
        case Winner::Town:   ofs << "Town\n";   break;
        case Winner::Mafia:  ofs << "Mafia\n";  break;
        case Winner::Maniac: ofs << "Maniac\n"; break;
        default:             ofs << "None\n";   break;
    }

    const auto& ps = state_->players();
    const std::size_t n = ps.size();

    ofs << "\n#  Name            Role             Team      Status     Died@Round  "
           "VotesGiven  VotesRecv  MafiaVotes  DetShots  DocHeals  ManiacTargets\n";
    ofs << "-------------------------------------------------------------------------------------------------------------\n";

    auto pad = [](const std::string& s, std::size_t w) {
        if (s.size() >= w) return s.substr(0, w);
        return s + std::string(w - s.size(), ' ');
    };

    for (std::size_t i = 0; i < n; ++i) {
        if (!ps[i]) continue;
        const std::string nm   = ps[i]->name();
        const std::string rl   = role_ru(ps[i]->role());
        const std::string tm   = team_ru(ps[i]->team());
        const bool alive       = ps[i]->is_alive();

        const int died_round   = (i < stats_died_round_.size() ? stats_died_round_[i] : 0);
        const int vg           = (i < stats_votes_given_day_.size() ? stats_votes_given_day_[i] : 0);
        const int vr           = (i < stats_votes_received_day_.size() ? stats_votes_received_day_[i] : 0);
        const int mv           = (i < stats_mafia_votes_.size() ? stats_mafia_votes_[i] : 0);
        const int ds           = (i < stats_detective_shots_.size() ? stats_detective_shots_[i] : 0);
        const int dh           = (i < stats_doctor_heals_.size() ? stats_doctor_heals_[i] : 0);
        const int mt           = (i < stats_maniac_targets_.size() ? stats_maniac_targets_[i] : 0);

        ofs << std::setw(2) << (i + 1) << " "
            << pad(nm, 15) << " "
            << pad(rl, 16) << " "
            << pad(tm, 9)  << " "
            << pad(alive ? "ALIVE" : "DEAD", 9) << " "
            << std::setw(10) << (died_round > 0 ? std::to_string(died_round) : "-") << " "
            << std::setw(10) << vg << " "
            << std::setw(10) << vr << " "
            << std::setw(11) << mv << " "
            << std::setw(8)  << ds << " "
            << std::setw(9)  << dh << " "
            << std::setw(14) << mt
            << "\n";
    }

    ofs.close();
}

// ---------- приватные методы ----------

bool Moderator::is_alive(PlayerId id) const {
    const auto& ps = state_->players();
    return id < ps.size() && ps[id] && ps[id]->is_alive();
}

bool Moderator::is_mafia(PlayerId id) const {
    const auto& ps = state_->players();
    return id < ps.size() && ps[id] && ps[id]->is_alive() && ps[id]->team() == Team::Mafia;
}

bool Moderator::is_maniac(PlayerId id) const {
    const auto& ps = state_->players();
    return id < ps.size() && ps[id] && ps[id]->is_alive() && ps[id]->team() == Team::Maniac;
}

bool Moderator::is_town(PlayerId id) const {
    const auto& ps = state_->players();
    return id < ps.size() && ps[id] && ps[id]->is_alive() && ps[id]->team() == Team::Town;
}

std::size_t Moderator::alive_count_() const {
    const auto& ps = state_->players();
    std::size_t c = 0;
    for (const auto& p : ps) if (p && p->is_alive()) ++c;
    return c;
}

std::size_t Moderator::alive_mafia_count_() const {
    const auto& ps = state_->players();
    std::size_t c = 0;
    for (const auto& p : ps) if (p && p->is_alive() && p->team() == Team::Mafia) ++c;
    return c;
}

std::size_t Moderator::alive_town_count_() const {
    const auto& ps = state_->players();
    std::size_t c = 0;
    for (const auto& p : ps) if (p && p->is_alive() && p->team() == Team::Town) ++c;
    return c;
}

std::size_t Moderator::alive_maniac_count_() const {
    const auto& ps = state_->players();
    std::size_t c = 0;
    for (const auto& p : ps) if (p && p->is_alive() && p->team() == Team::Maniac) ++c;
    return c;
}

void Moderator::clear_night_intents_() {
    const auto n = state_->players().size();
    std::scoped_lock lk(mu_);
    mafia_vote_counts_.assign(n, 0);
    detective_shot_.reset();
    doctor_heal_.reset();
    maniac_target_.reset();
    journalist_queries_.clear();
    eavesdrop_requests_.clear();
}

// --- разрешение дневной ничьей через Палача ---
std::optional<PlayerId> Moderator::resolve_tie_via_executioner_(const std::vector<PlayerId>& leaders) {
    const auto& ps = state_->players();
    // Переберём всех живых Палачей
    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (!ps[i] || !ps[i]->is_alive()) continue;
        if (ps[i]->role() != Role::Executioner) continue;

        // безопасный dynamic_cast
        auto* ex = dynamic_cast<roles::Executioner*>(ps[i].get());
        if (!ex) continue;

        auto decision = ex->decide_execution(*this, leaders);
        if (!decision.has_value()) {
            // Лог в файл: воздержался
            std::ostringstream os;
            os << "DAY: executioner abstains (" << player_tag(*ps[i], i) << ")\n";
            round_append_(os.str());
            // ищем дальше другого палача (если есть)
            continue;
        }

        PlayerId victim = decision.value();
        // проверим, что выбранный — действительно из лидеров
        if (std::find(leaders.begin(), leaders.end(), victim) == leaders.end()) {
            // выбор некорректен — игнорируем
            std::ostringstream os;
            os << "DAY: executioner invalid choice by " << player_tag(*ps[i], i) << "\n";
            round_append_(os.str());
            continue;
        }

        // зафиксируем выбор
        std::ostringstream os;
        os << "DAY: executioner chooses " << player_tag(*ps[victim], victim) << "\n";
        round_append_(os.str());
        return victim;
    }
    return std::nullopt;
}

} // namespace core
