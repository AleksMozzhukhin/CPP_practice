#pragma once
#include <string>
#include <optional>
#include <algorithm>

#include "roles/base_player.hpp"

namespace roles {

    /**
     * Доктор (Doctor).
     * Ночью выбирает цель для лечения (может лечить себя).
     * Ограничение: нельзя лечить одного и того же игрока две ночи подряд.
     *
     * В простой ИИ-стратегии:
     *  - если предыдущая цель ещё жива, пытаемся выбрать другую живую цель;
     *  - если альтернатив нет (остались только он сам или прежняя цель), допускаем повтор
     *    только если это единственно возможная валидная цель.
     * Днём голосует случайно (не за себя).
     */
    class Doctor final : public BasePlayer {
    public:
        Doctor(core::PlayerId id,
               std::string name,
               smart::shared_like<core::GameState> state,
               core::Rng& rng) noexcept
            : BasePlayer(id, std::move(name), Role::Doctor, Team::Town, std::move(state), rng) {}

        void on_day(core::Moderator& /*mod*/) override {}

        core::PlayerId vote_day(core::Moderator& /*mod*/) override {
            return random_alive_except_self();
        }

        void on_night(core::Moderator& mod) override;

    private:
        std::optional<core::PlayerId> prev_heal_;
    };

} // namespace roles
