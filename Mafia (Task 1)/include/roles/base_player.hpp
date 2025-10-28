#pragma once
#include <string>
#include <vector>
#include <optional>

#include "roles/i_player.hpp"
#include "smart/shared_like.hpp"

namespace core {
    class GameState;
    class Rng;
    class Moderator;
}

namespace roles {

/**
 * Базовый класс для всех ролей. Реализует общие поля/методы:
 *  - id, name, role, team, alive/kill
 *  - доступ к GameState и Rng
 *  - вспомогательные выборки целей (случайная живая цель и т.п.)
 *
 * Чисто виртуальные методы IPlayer (vote_day/on_night) реализуются в дочерних классах.
 */
class BasePlayer : public IPlayer {
public:
    BasePlayer(core::PlayerId id,
               std::string name,
               Role role,
               Team team,
               smart::shared_like<core::GameState> state,
               core::Rng& rng) noexcept;

    // --- IPlayer ---
    core::PlayerId id() const noexcept override          { return id_; }
    const std::string& name() const noexcept override    { return name_; }
    Role role() const noexcept override                  { return role_; }
    Team team() const noexcept override                  { return team_; }
    bool is_alive() const noexcept override              { return alive_; }
    void kill() noexcept override                        { alive_ = false; }

protected:
    // Возвращает список ID всех живых игроков.
    std::vector<core::PlayerId> alive_ids() const;

    // Случайная живая цель, отличная от self; если никого нет — вернёт self.
    core::PlayerId random_alive_except_self() const;

    // Случайная живая цель среди ГРАЖДАН (Town), исключая self; если нет — fallback на random_alive_except_self().
    core::PlayerId random_alive_town_except_self() const;

    // Проверка «жив ли игрок id»
    bool is_alive(core::PlayerId pid) const;

protected:
    core::PlayerId id_;
    std::string    name_;
    Role           role_;
    Team           team_;
    bool           alive_{true};

    smart::shared_like<core::GameState> state_;
    core::Rng* rng_; // не владеем
};

} // namespace roles
