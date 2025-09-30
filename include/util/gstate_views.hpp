#pragma once
#include <cstddef>
#include "util/generator.hpp"
#include "core/types.hpp"
#include "core/game_state.hpp"

namespace util::views {

    /**
     * Перебор id всех ЖИВЫХ игроков.
     * Использует корутины C++20 (generator<PlayerId>).
     */
    inline util::generator<core::PlayerId>
    alive_ids(const core::GameState& st) {
        const auto& ps = st.players();
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (ps[i] && ps[i]->is_alive()) {
                co_yield static_cast<core::PlayerId>(i);
            }
        }
    }

    /**
     * Перебор id всех ЖИВЫХ игроков, КРОМЕ except_id.
     */
    inline util::generator<core::PlayerId>
    alive_except(const core::GameState& st, core::PlayerId except_id) {
        const auto& ps = st.players();
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (!ps[i] || !ps[i]->is_alive()) continue;
            const auto pid = static_cast<core::PlayerId>(i);
            if (pid == except_id) continue;
            co_yield pid;
        }
    }

} // namespace util::views
