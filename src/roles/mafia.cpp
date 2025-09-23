#include "roles/mafia.hpp"
#include "core/moderator.hpp"
#include "core/game_state.hpp"

namespace roles {

    void Mafia::on_night(core::Moderator& mod) {
        // Предпочитаем цель из мирных; если нет доступных — случайный живой не-self.
        const auto target = random_alive_town_except_self();
        mod.mafia_vote_target(id_, target);
    }

} // namespace roles
