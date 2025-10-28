#pragma once
#include <type_traits>
#include <utility> // std::declval

#include "core/game_state.hpp"
#include "concepts/player_container.hpp"

namespace state_checks {

    /**
     * Проверяем на этапе компиляции, что тип, возвращаемый
     * GameState::players(), удовлетворяет концепту PlayerContainer.
     */
    using PlayersContainerT =
        std::remove_reference_t<decltype(std::declval<const core::GameState&>().players())>;

    static_assert(concepts_mafia::PlayerContainer<PlayersContainerT>,
                  "GameState::players() must return a PlayerContainer of smart::shared_like<IPlayer>");

} // namespace state_checks
