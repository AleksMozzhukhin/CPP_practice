#include "roles/doctor.hpp"
#include "core/moderator.hpp"
#include "core/game_state.hpp"
#include "core/rng.hpp"   // требуется полное определение для rng_->choose(...)


namespace roles {

    void Doctor::on_night(core::Moderator& mod) {
        // Список всех живых (включая себя — селф-хил разрешён)
        auto alive = alive_ids();

        // Если есть предыдущая цель лечения — постараемся её не повторять
        if (prev_heal_.has_value()) {
            // Уберём предыдущую цель, если остаются альтернативы
            if (alive.size() > 1) {
                alive.erase(std::remove(alive.begin(), alive.end(), *prev_heal_), alive.end());
                // Если альтернатив не осталось (только прежняя цель) — оставим как есть,
                // допускаем повтор как вынужденный (иначе лечить будет некого).
                if (alive.empty()) {
                    alive.push_back(*prev_heal_);
                }
            }
        }

        // Выбираем случайную цель из оставшихся
        auto it = rng_->choose(alive.begin(), alive.end());
        auto target = (it == alive.end()) ? id_ : *it;

        mod.set_doctor_heal(id_, target);
        prev_heal_ = target;
    }

} // namespace roles
