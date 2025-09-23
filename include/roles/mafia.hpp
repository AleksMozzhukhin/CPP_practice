#pragma once
#include <string>
#include "roles/base_player.hpp"

namespace roles {

    /**
     * Мафия.
     * Днём голосует как обычный игрок (случайная живая цель, предпочтительно горожанин).
     * Ночью голосует за цель мафии через Moderator::mafia_vote_target().
     */
    class Mafia final : public BasePlayer {
    public:
        Mafia(core::PlayerId id,
              std::string name,
              smart::shared_like<core::GameState> state,
              core::Rng& rng) noexcept
            : BasePlayer(id, std::move(name), Role::Mafia, Team::Mafia, std::move(state), rng) {}

        // Дневная "речь" не делает ничего в ИИ-версии
        void on_day(core::Moderator& /*mod*/) override {}

        // Днём стараемся голосовать против мирного (иначе — случайный живой не-self)
        core::PlayerId vote_day(core::Moderator& /*mod*/) override {
            return random_alive_town_except_self();
        }

        // Ночью мафия отдаёт "голос" за цель; итог выбирает модератор по большинству/случайно
        void on_night(core::Moderator& mod) override;
    };

} // namespace roles
