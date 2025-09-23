#pragma once
#include <vector>
#include <string>
#include <cassert>

#include "core/types.hpp"
#include "core/phases.hpp"
#include "smart/shared_like.hpp"

namespace roles { class IPlayer; }

namespace core {

    /**
     * Хранит текущее состояние игры:
     *  - список игроков (в порядке их идентификаторов);
     *  - номер раунда и текущую фазу;
     *  - флаг завершения и победителя.
     *
     * Класс не потокобезопасен сам по себе — синхронизацию выполняет Moderator/Engine.
     */
    class GameState {
    public:
        GameState() = default;

        // Доступ к игрокам
        const std::vector<smart::shared_like<roles::IPlayer>>& players() const noexcept { return players_; }
        std::vector<smart::shared_like<roles::IPlayer>>&       players_mut() noexcept   { return players_; }

        // Идентификатор игрока — его индекс в векторе
        static PlayerId to_id(std::size_t idx) noexcept { return static_cast<PlayerId>(idx); }

        // Раунд/фаза
        std::size_t round() const noexcept { return round_; }
        Phase       phase() const noexcept { return phase_; }

        void set_phase(Phase p) noexcept { phase_ = p; }
        void next_round() noexcept { ++round_; }

        // Завершение
        bool   is_game_over() const noexcept { return game_over_; }
        Winner winner() const noexcept { return winner_; }

        void set_game_over(Winner w) noexcept {
            game_over_ = (w != Winner::None);
            winner_ = w;
        }

    private:
        std::vector<smart::shared_like<roles::IPlayer>> players_;
        std::size_t round_{1};
        Phase       phase_{Phase::Day};

        bool   game_over_{false};
        Winner winner_{Winner::None};
    };

} // namespace core
