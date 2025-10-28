#pragma once
#include <vector>
#include <optional>
#include <string>
#include <string_view>
#include <mutex>
#include <utility>

#include "core/types.hpp"
#include "core/game_config.hpp"
#include "smart/shared_like.hpp"

namespace util { class Logger; }
namespace roles { class IPlayer; }

namespace core {

class GameState;
class Rng;        // forward-declare
enum class Phase;

/**
 * Moderator — арбитр игры. Собирает голоса, фиксирует ночные действия,
 * применяет эффекты и проверяет условия завершения.
 *
 * Потокобезопасность:
 *  - Внешняя синхронизация фаз обеспечивается барьерами в GameEngine.
 *  - Внутренняя синхронизация доступа к буферам/логам — через mutex mu_.
 *
 * Дополнительно (файлы):
 *  - По окончании каждой НОЧИ формируется файл лога раунда logs/round_<R>.txt.
 *  - Если игра закончилась днём, незавершённый раунд дописывается.
 *  - По окончании игры записывается logs/summary.txt со сводной статистикой.
 *
 * Доп. роли из ТЗ:
 *  - Палач (Executioner): срабатывает при ДНЕВНОЙ НИЧЬЕ (leaders.size() > 1).
 *  - Журналист (Journalist): ночью сравнивает двух игроков на одинаковость статусов (лагерь).
 *  - Ушастик (Eavesdropper): ночью выбирает цель и узнаёт, какие действия были на неё.
 */
class Moderator {
public:
    Moderator(const GameConfig& cfg,
              smart::shared_like<GameState> state,
              util::Logger& root_logger,
              Rng& rng);

    // ---------- День ----------
    void clear_day_votes();                         // вызвать перед стартом дня (движок)
    void submit_day_vote(PlayerId voter, PlayerId target);
    std::optional<PlayerId> resolve_day_lynch();    // вызвать после барьера конца дня

    // ---------- Ночь ----------
    void mafia_vote_target(PlayerId mafia_id, PlayerId target);
    bool investigate(PlayerId detective_id, PlayerId target) const;
    void set_detective_shot(PlayerId detective_id, PlayerId target);
    void set_doctor_heal(PlayerId doctor_id, PlayerId target);
    void set_maniac_target(PlayerId maniac_id, PlayerId target);

    // Журналист: запрос сравнить двух игроков (i!=j, и оба != journalist_id)
    void set_journalist_compare(PlayerId journalist_id, PlayerId a, PlayerId b);

    // Ушастик: цель наблюдения (!= eavesdropper_id)
    void set_eavesdropper_target(PlayerId eavesdropper_id, PlayerId target);

    // Разрешение ночи
    std::vector<PlayerId> resolve_night();

    // ---------- Общие операции ----------
    void kill_player(PlayerId id);
    Winner evaluate_winner() const;
    void log_info(std::string_view msg);

    // ---------- Файлы логов / статистика ----------
    void finalize_round_file_if_pending();  // если игра завершилась днём (без ночи)
    void write_summary_file() const;        // итоговый файл со статистикой

private:
    // Вспомогательные проверки (чтение состояния)
    bool is_alive(PlayerId id) const;
    bool is_mafia(PlayerId id) const;
    bool is_maniac(PlayerId id) const;
    bool is_town(PlayerId id) const;

    // Подсчёты
    std::size_t alive_count_() const;
    std::size_t alive_mafia_count_() const;
    std::size_t alive_town_count_() const;
    std::size_t alive_maniac_count_() const;

    // Внутренний сброс ночных намерений (вызывает движок после resolve_night)
    void clear_night_intents_();

    // --- режимы логирования в консоль ---
    inline bool is_full_logs_() const noexcept { return cfg_.log_mode == GameConfig::LogMode::Full; }
    inline bool is_open_() const noexcept { return cfg_.open_announcements; }

    // --- работа с файловым раунд-логом ---
    void round_begin_day_();                      // установить заголовок раунда и очистить буфер
    void round_append_(std::string_view line);    // добавить строку в буфер
    void round_write_file_(bool night_completed); // сохранить logs/round_<round_>.txt

    // --- разрешение дневной ничьей через Палача ---
    std::optional<PlayerId> resolve_tie_via_executioner_(const std::vector<PlayerId>& leaders);

private:
    GameConfig cfg_;
    smart::shared_like<GameState> state_;
    util::Logger* root_{nullptr};
    Rng* rng_{nullptr};

    // ---- синхронизация внутренних структур ----
    mutable std::mutex mu_;

    // ---- буферы дня ----
    std::vector<std::optional<PlayerId>> day_votes_;

    // ---- буферы ночи ----
    std::vector<int> mafia_vote_counts_;
    std::optional<PlayerId> detective_shot_;
    std::optional<PlayerId> doctor_heal_;
    std::optional<PlayerId> maniac_target_;

    // Журналист: накопим запросы на сравнение (может быть несколько журналистов)
    struct JournalistQuery { PlayerId jid, a, b; };
    std::vector<JournalistQuery> journalist_queries_;

    // Ушастик: пары (eavesdropper_id, target)
    std::vector<std::pair<PlayerId, PlayerId>> eavesdrop_requests_;

    // ---- счётчики для summary ----
    std::vector<int> stats_votes_given_day_;      // сколько раз игрок голосовал днём (по факту голосования)
    std::vector<int> stats_votes_received_day_;   // сколько конечных голосов получил
    std::vector<int> stats_mafia_votes_;          // голоса мафии, отданные игроком
    std::vector<int> stats_detective_shots_;      // выстрелы комиссара
    std::vector<int> stats_doctor_heals_;         // лечения доктора
    std::vector<int> stats_maniac_targets_;       // цели маньяка
    std::vector<int> stats_died_round_;           // номер раунда, когда умер (или 0, если жив к финалу)

    // Временный флаг "голосовал ли сегодня" (чтобы не считать повторно при смене голоса)
    std::vector<bool> day_voted_flag_;

    // ---- раундовый файл-лог ----
    int  round_index_{0};            // начинается с 1 после первого clear_day_votes()
    bool round_written_{false};      // сохранён ли файл за текущий round_index_
    std::string round_log_;          // буфер строк текущего раунда
};

} // namespace core
