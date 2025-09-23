#include "core/rng.hpp"
#include <random>

namespace core {

    Rng::Rng() {
        seed_snapshot_ = seed_from_device_();
        eng_.seed(seed_snapshot_);
    }

    Rng::Rng(std::uint64_t seed) {
        if (seed == 0) {
            seed_snapshot_ = seed_from_device_();
        } else {
            seed_snapshot_ = seed;
        }
        eng_.seed(seed_snapshot_);
    }

    std::uint64_t Rng::seed_from_device_() {
        std::random_device rd;
        // Сконструируем 64-битный сид из двух вызовов random_device
        std::uint64_t high = static_cast<std::uint64_t>(rd()) << 32;
        std::uint64_t low  = static_cast<std::uint64_t>(rd());
        return high ^ low;
    }

} // namespace core
