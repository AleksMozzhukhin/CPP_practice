#pragma once
#include <string>
#include "roles/base_player.hpp"

namespace roles {

    /**
     * Маньяк (Maniac).
     * Играет сам за себя.
     * Днём голосует против случайной живой цели (не себя).
     * Ночью выбирает одиночную цель для убийства.
     */
    class Maniac final : public BasePlayer {
    public:
        Maniac(core::PlayerId id,
               std::string name,
               smart::shared_like<core::GameState> state,
               core::Rng& rng) noexcept
            : BasePlayer(id, std::move(name), Role::Maniac, Team::Maniac, std::move(state), rng) {}

        void on_day(core::Moderator& /*mod*/) override {}

        core::PlayerId vote_day(core::Moderator& /*mod*/) override {
            return random_alive_except_self();
        }

        void on_night(core::Moderator& mod) override;
    };

} // namespace roles
