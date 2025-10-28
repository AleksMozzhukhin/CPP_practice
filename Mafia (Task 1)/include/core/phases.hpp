#pragma once
#include <string_view>
#include <string>

namespace core {

    // Игровая фаза
    enum class Phase {
        Day,
        Night
    };

    inline std::string_view to_string_view(Phase p) noexcept {
        switch (p) {
            case Phase::Day:   return "Day";
            case Phase::Night: return "Night";
        }
        return "Day";
    }

    inline std::string to_string(Phase p) {
        return std::string{to_string_view(p)};
    }

} // namespace core
