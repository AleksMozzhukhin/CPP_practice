#pragma once
#include <concepts>
#include <type_traits>
#include <cstddef>

#include "concepts/mafia_concepts.hpp"

namespace concepts_mafia {

    /**
     * PlayerContainer — контейнер игроков.
     *
     * Требования:
     *  - Имеет value_type;
     *  - value_type удовлетворяет SharedLikePlayer (умный указатель на IPlayer);
     *  - Имеет методы size() и operator[](size_t);
     *  - operator[] возвращает ссылку/значение, совместимое с value_type.
     */
    template<class C>
    concept PlayerContainer =
        requires(C c) {
        typename std::remove_cvref_t<C>::value_type;
        { c.size() } -> std::convertible_to<std::size_t>;
        { c[std::size_t{}] };
        }
    &&
    SharedLikePlayer<typename std::remove_cvref_t<C>::value_type>;

} // namespace concepts_mafia
