#pragma once
#include <vector>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>

#include "core/types.hpp"
#include "core/game_config.hpp"
#include "core/rng.hpp"
#include "core/game_state.hpp"
#include "core/moderator.hpp"

#include "smart/shared_like.hpp"
#include "util/logger.hpp"

#include "coro/task.hpp"
#include "coro/scheduler.hpp"

namespace core {

/**
 * GameEngineCoro — кооперативный движок игры на корутинах.
 * Не использует потоки. Управление фазами — через coro::PhaseBarrier:
 *   - day_start_, day_end_, night_start_, night_end_.
 *
 * Игроки представлены корутинами, каждая корутина автоматически
 * выполняет дневные/ночные действия до завершения игры.
 */
class GameEngineCoro {
public:
    explicit GameEngineCoro(const GameConfig& cfg, util::Logger& root_logger);

    /// Запуск полного цикла игры (до победы одной из сторон).
    void run();

private:
    // --- построение состава игроков (точно так же, как в обычном движке)
    void init_players_();

    // --- корутина-»агент» игрока с индексом idx
    coro::task player_task_(std::size_t idx);

    // --- коллбэки фаз (вызываются PhaseBarrier при достижении кворума)
    void on_day_start_();
    void on_day_end_();     // подсчёт голосов, проверка завершения
    void on_night_start_();
    void on_night_end_();   // разрешение ночи, проверка завершения

    bool check_end_conditions_();

private:
    const GameConfig cfg_;
    util::Logger* root_{nullptr};

    Rng rng_;                       // общий RNG (для выбора ролей и т.д.)
    std::vector<Rng> rng_per_player_;  // детерминированный RNG на игрока

    smart::shared_like<GameState>  state_;
    std::unique_ptr<Moderator>  moderator_;

    // Кооперативная «планировка» корутин
    coro::Scheduler scheduler_;

    // Многоразовые барьеры для фаз
    coro::PhaseBarrier day_start_{0};
    coro::PhaseBarrier day_end_{0};
    coro::PhaseBarrier night_start_{0};
    coro::PhaseBarrier night_end_{0};

    std::atomic<bool> stop_{false};
};

} // namespace core
