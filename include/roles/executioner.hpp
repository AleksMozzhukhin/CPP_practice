#pragma once
#include <string>
#include <vector>
#include <optional>

#include "roles/base_player.hpp"
#include "core/types.hpp"
#include "core/moderator.hpp"

namespace roles {

/**
 * Палач (Executioner) — дополнительная роль из ТЗ.
 *
 * Правила:
 *  - Активируется ТОЛЬКО при ничьей днём (несколько лидеров по голосам).
 *  - Может выбрать одного из лидеров для казни ИЛИ воздержаться (ничего не делать).
 *  - Никаких ночных действий не совершает.
 *  - Команда: Мирные.
 *
 * Интеграция:
 *  - Moderator при разрешении дневной фазы (resolve_day_lynch) при ничьей
 *    обращается к alive Палачу(ам) и запрашивает решение.
 *  - В данной реализации Палач-бот выбирает случайно:
 *      50% — казнить случайного из списка лидеров;
 *      50% — воздержаться.
 *    (Если потребуется интерактивный выбор — добавим позже через Human-обработчик.)
 */
class Executioner final : public BasePlayer {
public:
    Executioner(core::PlayerId id,
                std::string name,
                smart::shared_like<core::GameState> state,
                core::Rng& rng) noexcept
        : BasePlayer(id, std::move(name), Role::Executioner, Team::Town, std::move(state), rng) {}

    // Хуки фаз
    void on_day(core::Moderator& /*mod*/) override {}         // нет спец. действий до окончания голосования
    core::PlayerId vote_day(core::Moderator& /*mod*/) override {  // голосует как обычный мирный (случайно среди живых≠self)
        // Используем уже существующую базовую стратегию из BasePlayer (если есть),
        // либо простую случайную реализацию:
        const auto& ps = state_->players();
        std::vector<core::PlayerId> cand;
        cand.reserve(ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (!ps[i] || !ps[i]->is_alive()) continue;
            if (static_cast<core::PlayerId>(i) == id_) continue;
            cand.push_back(static_cast<core::PlayerId>(i));
        }
        if (cand.empty()) return id_;
        auto it = rng_->choose(cand.begin(), cand.end());
        return *it;
    }
    void on_night(core::Moderator& /*mod*/) override {}       // ночью не ходит

    // Решение палача при ничьей.
    // Вход: список кандидатов-лидеров по голосам.
    // Выход: id выбранной жертвы или std::nullopt (воздержаться).
    std::optional<core::PlayerId> decide_execution(core::Moderator& /*mod*/,
                                                   const std::vector<core::PlayerId>& leaders) {
        if (!is_alive() || leaders.empty()) return std::nullopt;
        // 50/50: казнить или воздержаться
        const int coin = rng_->uniform_int(0, 1);
        if (coin == 0) return std::nullopt;
        auto it = rng_->choose(leaders.begin(), leaders.end());
        return *it;
    }
};

} // namespace roles
