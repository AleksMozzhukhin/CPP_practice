#include "roles/mafia.hpp"
#include "core/moderator.hpp"
#include "core/game_state.hpp"

namespace roles {

    void Mafia::on_night(core::Moderator& mod) {
        // Предпочитаем цель из мирных; если нет доступных — случайный живой не-self.
        const auto target = random_alive_town_except_self();
        if (target == id_) {
            // Нет валидной цели кроме себя — пропускаем голос ночи
            mod.log_info("Night: mafia has no non-self targets alive; vote skipped");
            return;
        }
        mod.mafia_vote_target(id_, target);
    }

} // namespace roles

