#pragma once
#include <string>
#include "roles/base_player.hpp"

namespace roles {

    /**
     * Мирный житель (Citizen).
     * Днём голосует против случайной живой цели (не себя).
     * Ночью действий не имеет.
     */
    class Citizen final : public BasePlayer {
    public:
        Citizen(core::PlayerId id,
                std::string name,
                smart::shared_like<core::GameState> state,
                core::Rng& rng) noexcept
            : BasePlayer(id, std::move(name), Role::Citizen, Team::Town, std::move(state), rng) {}

        // Простая болтовня (ничего не делает в ИИ-версии)
        void on_day(core::Moderator& /*mod*/) override {}

        // Выбор цели дневного голосования — случайный живой не-self
        core::PlayerId vote_day(core::Moderator& /*mod*/) override {
            return random_alive_except_self();
        }

        // Ночью не действует
        void on_night(core::Moderator& /*mod*/) override {}
    };

} // namespace roles
