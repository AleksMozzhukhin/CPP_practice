#pragma once
#include <cstddef>
#include <string>

namespace core {

    /**
     * GameConfig — параметры запуска симуляции.
     *
     * Источники:
     *  - Значения по умолчанию ниже.
     *  - Параметры командной строки (см. app/main.cpp).
     *  - YAML-файл (если указан yaml_path), имеет приоритет над умолчаниями.
     *
     * Важное:
     *  - Для соответствия ТЗ включены 3 дополнительные роли: Палач, Журналист, Ушастик.
     *    Их количество управляется *_count. По умолчанию — по одному экземпляру каждой роли.
     *    Если игроков мало, движок скорректирует состав (бросит исключение при нехватке мест).
     */
    struct GameConfig {
        // --- базовые параметры ---
        std::size_t n_players{9};       // число игроков
        unsigned    seed{0};            // 0 => случайная инициализация RNG
        bool        human{false};       // один интерактивный игрок (--human)

        // Логи
        enum class LogMode { Short, Full };
        LogMode     log_mode{LogMode::Short};
        bool        open_announcements{false};   // "открытое" объявление ролей в логах
        std::string logs_dir{"logs"};            // каталог для файлов логов

        // Политика ничьи днём
        enum class TiePolicy { None, Random };
        TiePolicy   tie_policy{TiePolicy::None};

        // Баланс: делитель для вычисления числа мафии: mafia_cnt = max(1, n_players / max(3, k_mafia_divisor))
        std::size_t k_mafia_divisor{3};

        // --- YAML ---
        // Если непустой, будет попытка прочитать конфиг из YAML и применить поля (см. yaml_loader).
        std::string yaml_path{};

        // --- Дополнительные роли из ТЗ ---
        // По умолчанию включаем по одной штуке каждой роли.
        // Допустимые значения: 0 или 1 (если хотите совсем отключить роль — поставьте 0).
        std::size_t executioner_count{1};  // Палач
        std::size_t journalist_count{1};   // Журналист
        std::size_t eavesdropper_count{1}; // Ушастик
    };

} // namespace core
