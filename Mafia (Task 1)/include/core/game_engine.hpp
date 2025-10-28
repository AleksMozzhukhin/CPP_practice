#pragma once

#include <memory>
#include <vector>
#include <thread>   // std::jthread
#include <barrier>  // std::barrier
#include <atomic>

#include "rng.hpp"
#include "core/game_config.hpp"
#include "core/types.hpp"
#include "smart/shared_like.hpp"


namespace util { class Logger; }

namespace core {

class GameState;
class Moderator;
class Rng;

enum class Phase;
using PlayerId = std::size_t;


/**
 * GameEngine — главный цикл игры.
 * Отвечает за:
 *   - инициализацию игроков;
 *   - запуск потоков игроков (по одному на игрока) и синхронизацию фаз через барьеры;
 *   - последовательность фаз День/Ночь;
 *   - проверку условий завершения и финализацию логов.
 */
class GameEngine {
public:
    GameEngine(const GameConfig& cfg, util::Logger& root_logger);
    ~GameEngine();

    // Запустить симуляцию. Бросает исключение при фатальных ошибках конфигурации.
    void run();

private:
    // --- служебные методы ---
    void setup_players_();
    void start_player_threads_();
    void stop_player_threads_();

    void do_day_cycle_mt_();    // День: барьеры + сбор голосов + линч
    void do_night_cycle_mt_();  // Ночь: барьеры + сбор намерений + разрешение ночи

    bool check_end_conditions_();          // проверка победителя, запись summary
    void player_thread_loop_(std::size_t); // тело потока игрока (День->Ночь в цикле)

private:
    // --- конфигурация / зависимости ---
    GameConfig cfg_;
    util::Logger* root_{nullptr};

    Rng rng_;                         // общий ГПСЧ для ведущего/модератора (однопоточное использование)
    std::vector<Rng> rng_per_player_; // независимые ГПСЧ для потоков игроков (детерминированно от seed и id)
    smart::shared_like<GameState>  state_;

    smart::shared_like<Moderator>  moderator_;

    // --- синхронизация фаз (инициализируются в run()) ---
    std::unique_ptr<std::barrier<>> day_start_;
    std::unique_ptr<std::barrier<>> day_end_;
    std::unique_ptr<std::barrier<>> night_start_;
    std::unique_ptr<std::barrier<>> night_end_;

    // --- управление потоками игроков ---
    std::vector<std::jthread> player_threads_;
    std::atomic<bool> stop_{false};
};

} // namespace core
