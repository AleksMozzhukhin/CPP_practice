#pragma once
#include <cstdint>
#include <random>
#include <iterator>
#include <algorithm>
#include <type_traits>

#include "concepts/mafia_concepts.hpp"

namespace core {

    /**
     * Лёгкая обёртка над std::mt19937_64 с удобными методами:
     *  - uniform_int(a, b)              — равномерное целое в [a; b]
     *  - choose(first, last)            — случайный элемент из диапазона итераторов
     *  - shuffle(first, last)           — тасование диапазона (Fisher–Yates / std::shuffle)
     *
     * Потокобезопасность: НЕ потокобезопасен. Для параллельных потоков используйте
     * отдельный экземпляр Rng на поток/игрока.
     */
    class Rng {
    public:
        Rng() noexcept;
        explicit Rng(std::uint64_t seed) noexcept;

        /// Равномерное целое из закрытого диапазона [a; b].
        int uniform_int(int a, int b) noexcept;

        /// Выбрать случайный итератор из [first; last). Требует ForwardIterator.
        template <std::forward_iterator It>
        It choose(It first, It last) noexcept {
            const auto n = std::distance(first, last);
            if (n <= 0) return last;
            const int idx = uniform_int(0, static_cast<int>(n - 1));
            std::advance(first, idx);
            return first;
        }

        /// Перетасовать диапазон [first; last). Требует RandomAccessIterator (для std::shuffle).
        template <std::random_access_iterator It>
        void shuffle(It first, It last) noexcept {
            std::shuffle(first, last, eng_);
        }

    private:
        std::mt19937_64 eng_;
        std::uint64_t   seed_snapshot_{0};

        static std::uint64_t seed_from_device_() noexcept;
    };


    // Гарантируем на этапе компиляции, что core::Rng удовлетворяет контракту UniformRng.
    static_assert(concepts_mafia::UniformRng<Rng>,
                  "core::Rng must satisfy UniformRng concept (uniform_int/choose/shuffle)");

} // namespace core
