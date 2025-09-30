#include "roles/doctor.hpp"
#include "core/moderator.hpp"
#include "core/game_state.hpp"
#include "core/rng.hpp"
#include <algorithm>  // требуется полное определение для rng_->choose(...)


namespace roles {

    void Doctor::on_night(core::Moderator& mod) {
        // Список всех живых (включая себя — селф-хил разрешён)
        auto alive = alive_ids();

        // Жёсткий запрет на лечение одного и того же две ночи подряд:
        // исключаем прошлую цель из множества кандидатов без «вынужденного повтора».
        if (prev_heal_.has_value()) {
            alive.erase(std::remove(alive.begin(), alive.end(), *prev_heal_), alive.end());
        }

        // Если альтернатив не осталось — лечение пропускается
        if (alive.empty()) {
            mod.log_info("Night: doctor skips heal (no alternative to avoid consecutive heal)");
            prev_heal_.reset();
            return;
        }

        auto it = rng_->choose(alive.begin(), alive.end());
        auto target = (it == alive.end()) ? id_ : *it;

        mod.set_doctor_heal(id_, target);
        prev_heal_ = target;
    }

} // namespace roles
