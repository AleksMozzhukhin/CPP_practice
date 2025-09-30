#pragma once
#include <cstddef>
#include <string>

namespace core {

    /** Политика разрешения ничьей днём. */
    enum class TiePolicy {
        None,    ///< Никто не казнён (если есть Палач — он может принять решение)
        Random   ///< Случайный выбор среди лидеров
    };

    /** Режим консольного лога. */
    struct GameConfig {
        enum class LogMode { Short, Full };

        // --- базовые параметры ---
        std::size_t n_players        = 9;          ///< количество игроков
        unsigned    seed             = 0;          ///< 0 => random_device, иначе фиксированный сид
        bool        human            = false;      ///< один интерактивный игрок
        LogMode     log_mode         = LogMode::Short;
        bool        open_announcements = false;    ///< «открытые» объявления (для отладки/демо)
        std::string logs_dir         = "logs";     ///< каталог для round_*.txt и summary.txt
        TiePolicy   tie_policy       = TiePolicy::None;
        std::size_t k_mafia_divisor  = 3;          ///< делитель для подсчёта мафии (>=1)

        // --- дополнительные роли (из ТЗ) ---
        std::size_t executioner_count  = 1;        ///< Палач: 0 или 1
        std::size_t journalist_count   = 1;        ///< Журналист: 0 или 1
        std::size_t eavesdropper_count = 1;        ///< Ушастик: 0 или 1

        // --- YAML (если конфиг загружается из файла) ---
        std::string yaml_path{};                   ///< путь к YAML (опционально)

        // --- режим движка ---
        bool use_coroutines = false;               ///< true => запускать корутинный движок (альтернатива нитям)
    };

} // namespace core
