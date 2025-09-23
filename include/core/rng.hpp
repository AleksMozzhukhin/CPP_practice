#pragma once
#include <random>
#include <type_traits>
#include <algorithm>
#include <iterator>
#include <cstdint>

namespace core {

    class Rng {
    public:
        using engine_t = std::mt19937_64;

        // Конструкторы реализованы в src/core/rng.cpp
        Rng();
        explicit Rng(std::uint64_t seed);

        // Фактически использованный сид (для повторяемости экспериментов)
        std::uint64_t seed() const noexcept { return seed_snapshot_; }

        // Равномерное целое [lo, hi]
        template <class Int>
        Int uniform_int(Int lo, Int hi) {
            static_assert(std::is_integral_v<Int>, "Rng::uniform_int requires integral type");
            if (lo > hi) std::swap(lo, hi);
            std::uniform_int_distribution<Int> dist(lo, hi);
            return dist(eng_);
        }

        // Равномерное вещественное [0, 1)
        double uniform_01() {
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            return dist(eng_);
        }

        // Перемешивание диапазона
        template <class It>
        void shuffle(It first, It last) {
            std::shuffle(first, last, eng_);
        }

        // Выбор случайного элемента из RandomAccess-диапазона
        template <class RAIt>
        RAIt choose(RAIt first, RAIt last) {
            auto n = static_cast<std::ptrdiff_t>(std::distance(first, last));
            if (n <= 0) return last;
            auto idx = uniform_int<std::ptrdiff_t>(0, n - 1);
            return first + idx;
        }

    private:
        engine_t eng_;
        std::uint64_t seed_snapshot_{0};

        static std::uint64_t seed_from_device_(); // реализовано в rng.cpp
    };

} // namespace core
