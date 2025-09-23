#pragma once
#include <string>
#include <vector>
#include <algorithm>

#include "roles/base_player.hpp"
#include "core/types.hpp"
#include "core/moderator.hpp"

namespace roles {

/**
 * Журналист — дополнительная роль из ТЗ.
 *
 * Правила (ТЗ):
 *  - Не проверяет, но СРАВНИВАЕТ двух жителей на предмет «одинаковости» статусов.
 *  - Не может сравнивать с собой.  :contentReference[oaicite:1]{index=1}
 *
 * В данной реализации:
 *  - Под «статусом» понимается ПРИНАДЛЕЖНОСТЬ К ЛАГЕРЮ (Team: Town/Mafia/Maniac),
 *    без раскрытия конкретной роли (строго соответствует формулировке).
 *  - Ночью выбирает двух различных живых игроков, отличных от себя, и передаёт
 *    запрос ведущему (Moderator::set_journalist_compare).
 *  - Результат сравнения (same/different) фиксируется в файловом логе раунда
 *    ведущим при разрешении ночи.
 */
class Journalist final : public BasePlayer {
public:
    Journalist(core::PlayerId id,
               std::string name,
               smart::shared_like<core::GameState> state,
               core::Rng& rng) noexcept
        : BasePlayer(id, std::move(name), Role::Journalist, Team::Town, std::move(state), rng) {}

    // Днём ведёт себя как обычный мирный
    void on_day(core::Moderator& /*mod*/) override {}

    core::PlayerId vote_day(core::Moderator& /*mod*/) override {
        const auto& ps = state_->players();
        std::vector<core::PlayerId> cand;
        cand.reserve(ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (!ps[i] || !ps[i]->is_alive()) continue;
            if (static_cast<core::PlayerId>(i) == id_) continue;
            cand.push_back(static_cast<core::PlayerId>(i));
        }
        if (cand.empty()) return id_;
        auto it = rng_->choose(cand.begin(), cand.end());
        return *it;
    }

    // Ночью отправляет запрос на сравнение пары (i,j), i!=j, i!=self, j!=self.
    void on_night(core::Moderator& mod) override {
        const auto& ps = state_->players();

        std::vector<core::PlayerId> alive;
        alive.reserve(ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (!ps[i] || !ps[i]->is_alive()) continue;
            if (static_cast<core::PlayerId>(i) == id_) continue; // нельзя сравнивать с собой
            alive.push_back(static_cast<core::PlayerId>(i));
        }
        if (alive.size() < 2) return; // недостаточно целей

        // случайная пара без повторов
        auto it1 = rng_->choose(alive.begin(), alive.end());
        core::PlayerId a = *it1;
        // выберем второй отличный от a
        core::PlayerId b = a;
        while (b == a) {
            auto it2 = rng_->choose(alive.begin(), alive.end());
            b = *it2;
        }

        mod.set_journalist_compare(id_, a, b);
    }
};

} // namespace roles
