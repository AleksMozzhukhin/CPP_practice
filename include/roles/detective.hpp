#pragma once
#include <string>
#include <vector>
#include <optional>
#include <algorithm>

#include "roles/base_player.hpp"
#include "core/rng.hpp"
#include "core/moderator.hpp"


namespace roles {

/**
 * Комиссар (Detective).
 * Ночью делает одно из двух: ИЛИ проверяет цель (получая ответ "маф/не маф"),
 * ИЛИ стреляет по цели. В этой простой ИИ-версии:
 *  - если есть "подтверждённые мафы" вживых, с вероятностью 1/2 стреляет по одному из них,
 *    иначе предпочитает проверку случайной живой цели (не себя);
 *  - результаты проверок накапливаются в known_mafia_.
 * Днём голосует против известного мафа (если есть), иначе — случайный живой не-self.
 */
class Detective final : public BasePlayer {
public:
    Detective(core::PlayerId id,
              std::string name,
              smart::shared_like<core::GameState> state,
              core::Rng& rng) noexcept
        : BasePlayer(id, std::move(name), Role::Detective, Team::Town, std::move(state), rng) {}

    // Болтовня опускается
    void on_day(core::Moderator& /*mod*/) override {}

    core::PlayerId vote_day(core::Moderator& /*mod*/) override {
        prune_dead_known_mafia_();
        if (!known_mafia_.empty()) {
            // Голосуем за одного из известных мафов
            auto it = rng_->choose(known_mafia_.begin(), known_mafia_.end());
            return *it;
        }
        return random_alive_except_self();
    }

    void on_night(core::Moderator& mod) override {
        prune_dead_known_mafia_();

        bool shoot_now = false;
        if (!known_mafia_.empty()) {
            // Если есть известные мафы — подбросим монету (1/2) на выстрел
            shoot_now = (rng_->uniform_int(0, 1) == 1);
        }

        if (shoot_now) {
            // Стреляем по одному из известных мафов
            auto it = rng_->choose(known_mafia_.begin(), known_mafia_.end());
            mod.set_detective_shot(id_, *it);
            return;
        }

        // Иначе проверяем случайную живую цель (не себя)
        auto target = random_alive_except_self();
        // Не тратим проверку на уже известного мафа, если есть выбор
        if (!known_mafia_.empty() && std::find(known_mafia_.begin(), known_mafia_.end(), target) != known_mafia_.end()) {
            // попробуем найти иную цель
            auto alive = alive_ids();
            alive.erase(std::remove(alive.begin(), alive.end(), id_), alive.end());
            // удалим всех уже известных мафов
            for (auto m : known_mafia_) {
                alive.erase(std::remove(alive.begin(), alive.end(), m), alive.end());
            }
            if (!alive.empty()) {
                auto it2 = rng_->choose(alive.begin(), alive.end());
                target = *it2;
            }
        }

        const bool is_maf = mod.investigate(id_, target);
        if (is_maf) {
            // Добавим в known_mafia_, если ещё жив и не добавлен
            if (std::find(known_mafia_.begin(), known_mafia_.end(), target) == known_mafia_.end()) {
                known_mafia_.push_back(target);
            }
        }
    }

private:
    void prune_dead_known_mafia_() {
        // Очистим список от мёртвых/дубликатов
        auto ids = alive_ids();
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

        auto it = std::remove_if(known_mafia_.begin(), known_mafia_.end(),
                                 [&](core::PlayerId pid){
                                     return !std::binary_search(ids.begin(), ids.end(), pid);
                                 });
        known_mafia_.erase(it, known_mafia_.end());
    }

private:
    std::vector<core::PlayerId> known_mafia_;
};

} // namespace roles
