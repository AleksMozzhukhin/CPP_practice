#include "core/rng.hpp"
#include <random>
#include <algorithm> // std::swap

namespace core {

    Rng::Rng() noexcept {
        seed_snapshot_ = seed_from_device_();
        eng_.seed(seed_snapshot_);
    }

    Rng::Rng(std::uint64_t seed) noexcept {
        if (seed == 0) {
            seed_snapshot_ = seed_from_device_();
        } else {
            seed_snapshot_ = seed;
        }
        eng_.seed(seed_snapshot_);
    }

    // Реализация, которой не хватало (её требуют вызовы choose(...) и код ролей)
    int Rng::uniform_int(int a, int b) noexcept {
        if (a > b) std::swap(a, b);
        std::uniform_int_distribution<int> dist(a, b);
        return dist(eng_);
    }

    std::uint64_t Rng::seed_from_device_() noexcept {
        std::random_device rd;
        // Сконструируем 64-битный сид из двух вызовов random_device
        std::uint64_t high = static_cast<std::uint64_t>(rd()) << 32;
        std::uint64_t low  = static_cast<std::uint64_t>(rd());
        return high ^ low;
    }

} // namespace core
