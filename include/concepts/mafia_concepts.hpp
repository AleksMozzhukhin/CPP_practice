#pragma once
#include <concepts>
#include <type_traits>
#include <vector>
#include <string_view>
#include <iterator>

#include "core/types.hpp"
#include "roles/i_player.hpp"

namespace concepts_mafia {

    /**
     * UniformRng — концепт для ГПСЧ, совместимого с текущим интерфейсом core::Rng.
     *
     * Требования (минимальный контракт):
     *  - int uniform_int(int a, int b);
     *  - It  choose(It first, It last);   // возвращает итератор того же типа
     *  - void shuffle(It first, It last);
     *
     * Примечание: для проверки choose/shuffle используем «зонд» на типе итератора
     * std::vector<int>::iterator — это достаточно надёжно для статической валидации
     * наличия требуемых шаблонных перегрузок.
     */
    template<class R>
    concept UniformRng = requires(R r, int a, int b, std::vector<int>& v) {
        { r.uniform_int(a, b) } -> std::convertible_to<int>;
        { r.shuffle(v.begin(), v.end()) } -> std::same_as<void>;
        { r.choose(v.begin(), v.end()) } -> std::same_as<typename std::vector<int>::iterator>;
    };

    /**
     * PlayerLike — любой тип, порождённый от roles::IPlayer.
     * (Используем std::derived_from — он корректно работает и для const-/ref-вариантов.)
     */
    template<class T>
    concept PlayerLike =
        std::derived_from<std::remove_reference_t<T>, roles::IPlayer>;

    /**
     * SharedLikeOf<T> — «умный указатель-подобный» тип, у которого:
     *  - p.get() возвращает указатель на T (или конвертируемый в T*);
     *  - *p даёт T&;
     *  - p-> доступен и совместим с T*.
     *
     * Подходит для нашего smart::shared_like<T>, но не привязан к его имени.
     */
    template<class P, class T>
    concept SharedLikeOf = requires(P p) {
        { p.get() } -> std::convertible_to<T*>;
        { *p }      -> std::same_as<T&>;
        { p.operator->() } -> std::convertible_to<T*>;
    };

    /** Частный случай: «умный» указатель на IPlayer. */
    template<class P>
    concept SharedLikePlayer = SharedLikeOf<P, roles::IPlayer>;

} // namespace concepts_mafia
