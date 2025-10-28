#pragma once
#include <concepts>
#include <type_traits>
#include <vector>
#include <optional>

#include "concepts/mafia_concepts.hpp" // PlayerLike
#include "core/types.hpp"              // PlayerId

// Достаточно forward-декларации для параметров методов
namespace core { class Moderator; }

namespace concepts_mafia {

    /**
     * BasicRole — минимальный контракт роли игрока в нашей игре.
     *
     * Требования:
     *  - тип T унаследован от roles::IPlayer (см. PlayerLike);
     *  - методы:
     *      void on_day(core::Moderator&);
     *      core::PlayerId vote_day(core::Moderator&);
     *      void on_night(core::Moderator&);
     */
    template<class T>
    concept BasicRole =
        PlayerLike<T> &&
        requires (T& self, core::Moderator& mod) {
        { self.on_day(mod) }   -> std::same_as<void>;
        { self.vote_day(mod) } -> std::convertible_to<core::PlayerId>;
        { self.on_night(mod) } -> std::same_as<void>;
        };

    /**
     * ExecutionerRole — расширенный контракт для ролей,
     * которые принимают решение при дневной ничьей (Палач, Human).
     *
     * Требования:
     *  - удовлетворяет BasicRole;
     *  - метод:
     *      std::optional<core::PlayerId>
     *      decide_execution(core::Moderator&, const std::vector<core::PlayerId>& leaders);
     */
    template<class T>
    concept ExecutionerRole =
        BasicRole<T> &&
        requires (T& self, core::Moderator& mod, const std::vector<core::PlayerId>& leaders) {
        { self.decide_execution(mod, leaders) } -> std::convertible_to<std::optional<core::PlayerId>>;
        };

} // namespace concepts_mafia
