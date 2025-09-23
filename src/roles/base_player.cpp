//
// Created by mozhu on 23.09.2025.
//
#include "roles/base_player.hpp"

#include <algorithm>

#include "core/game_state.hpp"
#include "core/rng.hpp"
#include "roles/i_player.hpp"

namespace roles {

    BasePlayer::BasePlayer(core::PlayerId id,
                           std::string name,
                           Role role,
                           Team team,
                           smart::shared_like<core::GameState> state,
                           core::Rng& rng) noexcept
        : id_(id)
        , name_(std::move(name))
        , role_(role)
        , team_(team)
        , state_(std::move(state))
        , rng_(&rng)
    {}

    std::vector<core::PlayerId> BasePlayer::alive_ids() const {
        std::vector<core::PlayerId> out;
        const auto& ps = state_->players();
        out.reserve(ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (ps[i] && ps[i]->is_alive()) {
                out.push_back(static_cast<core::PlayerId>(i));
            }
        }
        return out;
    }

    core::PlayerId BasePlayer::random_alive_except_self() const {
        auto ids = alive_ids();
        ids.erase(std::remove(ids.begin(), ids.end(), id_), ids.end());
        if (ids.empty()) return id_;
        auto it = rng_->choose(ids.begin(), ids.end());
        return *it;
    }

    core::PlayerId BasePlayer::random_alive_town_except_self() const {
        const auto& ps = state_->players();

        std::vector<core::PlayerId> ids;
        ids.reserve(ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (!ps[i] || !ps[i]->is_alive()) continue;
            if (static_cast<core::PlayerId>(i) == id_) continue;
            if (ps[i]->team() == Team::Town) {
                ids.push_back(static_cast<core::PlayerId>(i));
            }
        }
        if (ids.empty()) return random_alive_except_self();
        auto it = rng_->choose(ids.begin(), ids.end());
        return *it;
    }

    bool BasePlayer::is_alive(core::PlayerId pid) const {
        const auto& ps = state_->players();
        return pid < ps.size() && ps[pid] && ps[pid]->is_alive();
    }

} // namespace roles
