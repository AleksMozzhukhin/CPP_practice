#include "roles/maniac.hpp"
#include "core/moderator.hpp"
#include "core/game_state.hpp"

namespace roles {

    void Maniac::on_night(core::Moderator& mod) {
        // Одиночный выстрел по случайной живой цели (не по себе)
        const auto target = random_alive_except_self();
        if (target == id_) {
            // Нет валидной цели кроме себя — пропускаем действие
            mod.log_info("Night: maniac has no non-self targets alive; action skipped");
            return;
        }
        mod.set_maniac_target(id_, target);
    }

} // namespace roles
