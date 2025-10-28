#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <algorithm> // std::transform
#include <cctype>    // std::tolower

#include "util/logger.hpp"
#include "core/game_config.hpp"
#include "core/game_engine.hpp"
#include "core/game_engine_coro.hpp"
#include "yaml/yaml_loader.hpp"

using core::GameConfig;

/**
 * @brief Печатает справку по CLI-опциям.
 *
 * Формат опций согласован с парсером ниже:
 *  - Числовые значения передаются отдельным токеном (например, `--n 12`).
 *  - Булевы флаги могут быть:
 *      * просто флагом (`--open`, `--coro`),
 *      * либо иметь явное значение (`--human 0|1|true|false` или `--human=true|false`).
 */
static void print_usage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options]\n"
              << "Options:\n"
              << "  --n <int>               number of players (default 9)\n"
              << "  --seed <uint>           RNG seed (0 => random)\n"
              << "  --human [0|1|true|false]  enable/disable one interactive player (default: enable if no value)\n"
              << "  --log <short|full>      console log verbosity\n"
              << "  --open                  open announcements (debug)\n"
              << "  --logs-dir <path>       directory for round_*.txt and summary.txt\n"
              << "  --tie <none|random>     tie policy at day\n"
              << "  --k-mafia-div <int>     mafia divisor (>=1)\n"
              << "  --exec <0|1>            Executioner count\n"
              << "  --journ <0|1>           Journalist count\n"
              << "  --ears <0|1>            Eavesdropper count\n"
              << "  --yaml <path>           load config from YAML file\n"
              << "  --coro                  run coroutine engine instead of threads\n"
              << "  -h, --help              show this help\n";
}

/**
 * @brief Точка входа.
 *
 * Алгоритм и приоритеты конфигурации:
 *  1) Предскан аргументов на наличие `--yaml` — если указан, конфигурация загружается из YAML как базовые значения.
 *  2) Полный разбор CLI — значения из командной строки ПЕРЕКРЫВАЮТ YAML.
 *  3) Валидации/нормализации (например, защита от `--human` вместе с `--coro`).
 *  4) Создание (при необходимости) каталога логов.
 *  5) Вывод «effective» конфигурации (то, что реально будет использоваться).
 *  6) Запуск выбранного движка (потокового или корутинного).
 *
 * Обработка ошибок:
 *  - Неверные/неполные опции — печать справки и выход с кодом 1.
 *  - Исключения при инициализации/работе движка — логируются и приводят к выходу с кодом 1.
 */
int main(int argc, char** argv) {
    util::Logger root("mafia-sim");
    GameConfig cfg;

    // ─────────────────────────────────────────────────────────────────────────────
    // 1) Предскан аргументов: ищем только --yaml, чтобы загрузить YAML до CLI-override
    // ─────────────────────────────────────────────────────────────────────────────
    for (int j = 1; j < argc; ++j) {
        std::string a = argv[j];
        if (a == "--yaml") {
            if (j + 1 < argc) {
                cfg.yaml_path = argv[j + 1];
            } else {
                root.error("Option --yaml requires a value");
                print_usage(argv[0]);
                return 1;
            }
            break; // достаточно первого вхождения
        }
    }
    if (!cfg.yaml_path.empty()) {
        // YAML загружается как набор базовых значений (CLI позже их перекроет)
        (void)yaml::load_config_from_yaml(cfg.yaml_path, cfg, &root);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // 2) Полный разбор CLI с перекрытием YAML
    // ─────────────────────────────────────────────────────────────────────────────
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // Утилита для опций, требующих последующее значение
        auto need_val = [&](const char* name)->bool{
            if (i + 1 >= argc) {
                root.error(std::string("Option ") + name + " requires a value");
                print_usage(argv[0]);
                return false;
            }
            return true;
        };

        if (arg == "--n") {
            if (!need_val("--n")) return 1;
            cfg.n_players = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--seed") {
            if (!need_val("--seed")) return 1;
            cfg.seed = static_cast<unsigned>(std::stoul(argv[++i]));
        }
        // Булев флаг --human: поддерживает формы `--human`, `--human 0|1|true|false`, `--human=...`
        else if (arg == "--human" || arg.rfind("--human=", 0) == 0) {
            auto parse_bool = [](std::string s) -> std::optional<bool> {
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
                if (s == "1" || s == "true"  || s == "yes" || s == "on")  return true;
                if (s == "0" || s == "false" || s == "no"  || s == "off") return false;
                return std::nullopt;
            };

            if (arg == "--human") {
                // Попытка прочитать булево значение из следующего токена (если он не опция).
                bool set = false;
                if (i + 1 < argc) {
                    std::string next = argv[i + 1];
                    if (!next.empty() && next[0] != '-') {
                        if (auto v = parse_bool(next)) {
                            cfg.human = *v;
                            ++i;       // потребили значение
                            set = true;
                        }
                    }
                }
                if (!set) {
                    cfg.human = true; // просто присутствие флага без значения
                }
            } else {
                // Формат --human=VALUE
                std::string val = arg.substr(std::string("--human=").size());
                if (auto v = parse_bool(val)) {
                    cfg.human = *v;
                } else {
                    root.warn("Invalid value for --human: '" + val + "', expected 0|1|true|false|yes|no|on|off. Using 'true'.");
                    cfg.human = true;
                }
            }
        } else if (arg == "--log") {
            if (!need_val("--log")) return 1;
            std::string v = argv[++i];
            if (v == "short") cfg.log_mode = GameConfig::LogMode::Short;
            else if (v == "full") cfg.log_mode = GameConfig::LogMode::Full;
            else {
                root.error("Invalid --log (use short|full)");
                return 1;
            }
        } else if (arg == "--open") {
            cfg.open_announcements = true;
        } else if (arg == "--logs-dir" || arg == "--logs_dir") {
            if (!need_val(arg.c_str())) return 1;
            cfg.logs_dir = argv[++i];   // CLI overrides YAML
        } else if (arg == "--tie") {
            if (!need_val("--tie")) return 1;
            std::string v = argv[++i];
            if (v == "none") cfg.tie_policy = core::TiePolicy::None;
            else if (v == "random") cfg.tie_policy = core::TiePolicy::Random;
            else {
                root.error("Invalid --tie (use none|random)");
                return 1;
            }
        } else if (arg == "--k-mafia-div") {
            if (!need_val("--k-mafia-div")) return 1;
            cfg.k_mafia_divisor = static_cast<std::size_t>(std::stoul(argv[++i]));
            if (cfg.k_mafia_divisor < 1) cfg.k_mafia_divisor = 1;
        } else if (arg == "--exec") {
            if (!need_val("--exec")) return 1;
            cfg.executioner_count = static_cast<std::size_t>(std::stoul(argv[++i]));
            if (cfg.executioner_count > 1) cfg.executioner_count = 1;
        } else if (arg == "--journ") {
            if (!need_val("--journ")) return 1;
            cfg.journalist_count = static_cast<std::size_t>(std::stoul(argv[++i]));
            if (cfg.journalist_count > 1) cfg.journalist_count = 1;
        } else if (arg == "--ears") {
            if (!need_val("--ears")) return 1;
            cfg.eavesdropper_count = static_cast<std::size_t>(std::stoul(argv[++i]));
            if (cfg.eavesdropper_count > 1) cfg.eavesdropper_count = 1;
        } else if (arg == "--yaml") {
            // Уже обработан на предскане; здесь лишь потребляем значение,
            // чтобы общий цикл корректно перемещал индекс i.
            if (!need_val("--yaml")) return 1;
            cfg.yaml_path = argv[++i];
        } else if (arg == "--coro") {
            cfg.use_coroutines = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            root.warn(std::string("Unknown option: ") + arg);
            print_usage(argv[0]);
            return 1;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // 3) Нормализации/валидации
    // ─────────────────────────────────────────────────────────────────────────────
    // Блокирующий stdin в интерактивном режиме несовместим с кооперативным коро-планировщиком —
    // откатываемся на потоковый движок.
    if (cfg.use_coroutines && cfg.human) {
        cfg.use_coroutines = false;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // 4) Гарантируем существование каталога логов (Moderator затем очистит старые логи)
    // ─────────────────────────────────────────────────────────────────────────────
    {
        std::error_code ec;
        if (cfg.logs_dir.empty()) {
            cfg.logs_dir = "logs";
        }
        std::filesystem::create_directories(cfg.logs_dir, ec);
        if (ec) {
            root.warn("cannot create logs_dir '" + cfg.logs_dir + "': " + ec.message());
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // 5) Вывод эффективной конфигурации
    // ─────────────────────────────────────────────────────────────────────────────
    root.info("effective: n_players = " + std::to_string(cfg.n_players));
    root.info("effective: seed = " + std::to_string(cfg.seed));
    root.info("effective: logs_dir = " + cfg.logs_dir);
    root.info("mafia-sim: initializing");

    // ─────────────────────────────────────────────────────────────────────────────
    // 6) Запуск движка (исключения логируются)
    // ─────────────────────────────────────────────────────────────────────────────
    try {
        if (cfg.use_coroutines) {
            core::GameEngineCoro engine(cfg, root);
            engine.run();
        } else {
            core::GameEngine engine(cfg, root);
            engine.run();
        }
        root.info("mafia-sim: finished");
    } catch (const std::exception& ex) {
        root.error(std::string("fatal: ") + ex.what());
        return 1;
    }

    return 0;
}
