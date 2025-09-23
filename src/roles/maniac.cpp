#include "roles/maniac.hpp"
#include "core/moderator.hpp"
#include "core/game_state.hpp"

namespace roles {

    void Maniac::on_night(core::Moderator& mod) {
        // Одиночный выстрел по случайной живой цели (не по себе)
        const auto target = random_alive_except_self();
        mod.set_maniac_target(id_, target);
    }

} // namespace roles
