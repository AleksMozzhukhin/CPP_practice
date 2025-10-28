#pragma once
#include <string>

namespace util { class Logger; }

namespace core { struct GameConfig; }

namespace yaml {

    /**
     * Загрузка параметров из YAML-файла в GameConfig.
     *
     * Поддерживаемые ключи верхнего уровня (простая YAML-схема без вложенности):
     *   n_players:            int
     *   seed:                 uint
     *   human:                bool               # true/false/yes/no/1/0
     *   log:                  short|full
     *   open:                 bool               # синоним open_announcements
     *   open_announcements:   bool
     *   logs_dir:             string
     *   tie:                  none|random
     *   k_mafia_div:          int>=1
     *
     *   # Дополнительные роли из ТЗ:
     *   executioner_count:    0|1                # Палач
     *   journalist_count:     0|1                # Журналист
     *   eavesdropper_count:   0|1                # Ушастик
     *
     *   # Режим движка:
     *   use_coroutines:       bool               # true => корутинный движок
     *   engine:               coro|threads       # синоним: engine: coro -> use_coroutines=true
     *
     * Формат файла — «ключ: значение», комментарии начинаются с '#'.
     * Пробелы вокруг ':' допустимы. Пустые строки и комментарии игнорируются.
     *
     * Возвращает true при успешной загрузке (файл прочитан; валидные ключи применены).
     * При ошибках парсинга значения конкретного ключа — ключ пропускается, пишется предупреждение.
     * При невозможности открыть файл — возвращает false.
     */
    bool load_config_from_yaml(const std::string& path,
                               core::GameConfig& cfg,
                               util::Logger* log = nullptr);

} // namespace yaml
