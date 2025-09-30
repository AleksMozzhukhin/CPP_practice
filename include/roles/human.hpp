#pragma once
#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <limits>
#include <algorithm>

#include "roles/base_player.hpp"
#include "roles/i_player.hpp"
#include "core/types.hpp"
#include "core/moderator.hpp"
#include "core/game_state.hpp"    // нужна полная декларация для state_->players()
#include "util/gstate_views.hpp" // генераторы alive_ids/alive_except (корутины)

namespace roles {

/**
 * Human — интерактивный игрок.
 * Поддерживаемые роли: все базовые + доп. роли (Палач, Журналист, Ушастик).
 *
 * Внимание: Палач не совершает ночных действий — его особое действие
 * вызывается ведущим при ничьей днём через decide_execution(...).
 *
 * В этом варианте для построения списков кандидатов используются корутины
 * (см. util::views::alive_ids и alive_except).
 */
class Human final : public BasePlayer {
public:
    Human(core::PlayerId id,
          std::string name,
          Role role,
          Team team,
          smart::shared_like<core::GameState> state,
          core::Rng& rng) noexcept
        : BasePlayer(id, std::move(name), role, team, std::move(state), rng) {}

    // День: вывод подсказок (минимальных) и обычное голосование
    void on_day(core::Moderator& /*mod*/) override {
        // без дополнительных подсказок, чтобы не шуметь в вывод
    }

    core::PlayerId vote_day(core::Moderator& /*mod*/) override {
        auto cands = alive_except_self_();
        if (cands.empty()) return id_;

        std::cout << "\n[HUMAN] День. Выберите, против кого голосовать:\n";
        print_candidates_(cands);
        auto choice = prompt_pick_(cands, /*allow_skip_self=*/false);
        if (!choice.has_value()) return id_;
        return *choice;
    }

    // Ночь: действия зависят от роли
    void on_night(core::Moderator& mod) override {
        switch (role_) {
            case Role::Citizen:
                // нет ночных действий
                break;

            case Role::Mafia: {
                auto cands = alive_except_self_();
                if (cands.empty()) break;
                std::cout << "\n[HUMAN] Ночь (Мафия). Кого помечаем на убийство?\n";
                print_candidates_(cands);
                auto choice = prompt_pick_(cands, /*allow_skip_self=*/false);
                if (choice.has_value()) {
                    mod.mafia_vote_target(id_, *choice);
                }
                break;
            }

            case Role::Detective: {
                // Для соответствия текущей модели — только выстрел.
                auto cands = alive_except_self_();
                if (cands.empty()) break;
                std::cout << "\n[HUMAN] Ночь (Комиссар). Выберите мишень для выстрела (или 0 — никого):\n";
                print_candidates_(cands, /*with_zero_skip=*/true);
                auto choice = prompt_pick_optional_(cands);
                if (choice.has_value()) {
                    mod.set_detective_shot(id_, *choice);
                }
                break;
            }

            case Role::Doctor: {
                auto cands = alive_including_self_();
                if (cands.empty()) break;
                std::cout << "\n[HUMAN] Ночь (Доктор). Кого лечить?\n";
                print_candidates_(cands);
                auto choice = prompt_pick_(cands, /*allow_skip_self=*/true);
                if (choice.has_value()) {
                    mod.set_doctor_heal(id_, *choice);
                }
                break;
            }

            case Role::Maniac: {
                auto cands = alive_except_self_();
                if (cands.empty()) break;
                std::cout << "\n[HUMAN] Ночь (Маньяк). Кого убить?\n";
                print_candidates_(cands);
                auto choice = prompt_pick_(cands, /*allow_skip_self=*/false);
                if (choice.has_value()) {
                    mod.set_maniac_target(id_, *choice);
                }
                break;
            }

            case Role::Executioner:
                // Особое действие Палача вызывается ведущим при ничьей днём (см. decide_execution()).
                break;

            case Role::Journalist: {
                // выбрать ДВУХ различных живых игроков, отличных от себя
                auto cands = alive_except_self_();
                if (cands.size() < 2) break;

                std::cout << "\n[HUMAN] Ночь (Журналист). Выберите ПЕРВУЮ цель сравнения:\n";
                print_candidates_(cands);
                auto a = prompt_pick_(cands, /*allow_skip_self=*/false);
                if (!a.has_value()) break;

                // второй список — без выбранного a
                std::vector<core::PlayerId> cands2;
                cands2.reserve(cands.size() - 1);
                for (auto pid : cands) if (pid != *a) cands2.push_back(pid);

                std::cout << "\n[HUMAN] Ночь (Журналист). Выберите ВТОРУЮ цель сравнения:\n";
                print_candidates_(cands2);
                auto b = prompt_pick_(cands2, /*allow_skip_self=*/false);
                if (!b.has_value()) break;

                mod.set_journalist_compare(id_, *a, *b);
                break;
            }

            case Role::Eavesdropper: {
                auto cands = alive_except_self_();
                if (cands.empty()) break;
                std::cout << "\n[HUMAN] Ночь (Ушастик). На кого подслушивать действия?\n";
                print_candidates_(cands);
                auto choice = prompt_pick_(cands, /*allow_skip_self=*/false);
                if (choice.has_value()) {
                    mod.set_eavesdropper_target(id_, *choice);
                }
                break;
            }
        }
    }

    // --- Специально для Палача: решение при дневной ничьей ---
    // leaders — список лидирующих по голосам игроков (из них можно казнить одного),
    // либо верните std::nullopt для воздержания.
    std::optional<core::PlayerId>
    decide_execution(core::Moderator& /*mod*/, const std::vector<core::PlayerId>& leaders) {
        if (!is_alive() || leaders.empty()) return std::nullopt;

        std::cout << "\n[HUMAN] Дневная ничья (Палач). Вы можете казнить одного из лидеров или воздержаться.\n";
        std::cout << "Введите 0, чтобы ВОЗДЕРЖАТЬСЯ, или номер из списка:\n";
        print_candidates_(leaders, /*with_zero_skip=*/true);

        auto choice = prompt_pick_optional_(leaders);
        return choice; // nullopt => воздержался, иначе — кого казнить
    }

private:
    // ----- построение списков кандидатов через корутины -----

    std::vector<core::PlayerId> alive_except_self_() const {
        std::vector<core::PlayerId> out;
        // наполняем из генератора (ленивый перебор живых кроме self)
        for (auto pid : util::views::alive_except(*state_, id_)) {
            out.push_back(pid);
        }
        return out;
    }

    std::vector<core::PlayerId> alive_including_self_() const {
        std::vector<core::PlayerId> out;
        for (auto pid : util::views::alive_ids(*state_)) {
            out.push_back(pid);
        }
        return out;
    }

    void print_candidates_(const std::vector<core::PlayerId>& cands, bool with_zero_skip = false) const {
        const auto& ps = state_->players();
        if (with_zero_skip) {
            std::cout << "  0) воздержаться / никто\n";
        }
        for (std::size_t k = 0; k < cands.size(); ++k) {
            const auto pid = cands[k];
            std::cout << "  " << (k + 1) << ") #" << (pid + 1) << " " << ps[pid]->name() << "\n";
        }
    }

    // Возвращает выбранный PID из списка cands (нумерация 1..|cands|). Ноль — недопустим.
    std::optional<core::PlayerId> prompt_pick_(const std::vector<core::PlayerId>& cands, bool /*allow_skip_self*/) const {
        for (;;) {
            std::cout << "Ваш выбор (1-" << cands.size() << "): ";
            std::size_t k{};
            if (!(std::cin >> k)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }
            if (k >= 1 && k <= cands.size()) {
                return cands[k - 1];
            }
            std::cout << "Некорректно. Повторите.\n";
        }
    }

    // Как prompt_pick_, но допускает 0 (воздержаться/никого) => вернёт std::nullopt
    std::optional<core::PlayerId> prompt_pick_optional_(const std::vector<core::PlayerId>& cands) const {
        for (;;) {
            std::cout << "Ваш выбор (0-" << cands.size() << "): ";
            std::size_t k{};
            if (!(std::cin >> k)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }
            if (k == 0) return std::nullopt;
            if (k >= 1 && k <= cands.size()) {
                return cands[k - 1];
            }
            std::cout << "Некорректно. Повторите.\n";
        }
    }
};

} // namespace roles
