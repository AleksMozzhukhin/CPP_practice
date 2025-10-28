#pragma once
#include <string>
#include <vector>

#include "roles/base_player.hpp"
#include "core/types.hpp"
#include "core/moderator.hpp"

namespace roles {

/**
 * Ушастик (Eavesdropper) — дополнительная роль из ТЗ.
 *
 * Правила (строго по ТЗ):
 *  - Ночью выбирает ЖЕРТВУ наблюдения (не себя).
 *  - Узнаёт, БЫЛО ЛИ этой ночью действие на выбранного персонажа и КАКОЕ ИМЕННО.
 *    (В нашей симуляции под «действиями» понимаем: голос мафии на цель, выстрел комиссара,
 *     лечение доктора, цель маньяка. Итоговую информацию фиксирует ведущий.)
 *  - Сам исход ночи Ушастик не меняет.
 *  - Команда: Мирные.
 *
 * Интеграция:
 *  - on_night вызывает Moderator::set_eavesdropper_target(id_, target).
 *  - Ведущий при разрешении ночи формирует итог для Ушастика по зафиксированным действиям
 *    (mafia votes tally на цель, detective shot == цель, doctor heal == цель, maniac target == цель),
 *    и пишет это в раундовый файл (для полноты логирования согласно задания).
 */
class Eavesdropper final : public BasePlayer {
public:
    Eavesdropper(core::PlayerId id,
                 std::string name,
                 smart::shared_like<core::GameState> state,
                 core::Rng& rng) noexcept
        : BasePlayer(id, std::move(name), Role::Eavesdropper, Team::Town, std::move(state), rng) {}

    // Днём — обычное поведение мирного
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

    // Ночью выбирает цель наблюдения (не себя)
    void on_night(core::Moderator& mod) override {
        const auto& ps = state_->players();

        std::vector<core::PlayerId> alive;
        alive.reserve(ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (!ps[i] || !ps[i]->is_alive()) continue;
            if (static_cast<core::PlayerId>(i) == id_) continue; // нельзя выбирать себя
            alive.push_back(static_cast<core::PlayerId>(i));
        }
        if (alive.empty()) return;

        auto it = rng_->choose(alive.begin(), alive.end());
        mod.set_eavesdropper_target(id_, *it);
    }
};

} // namespace roles
