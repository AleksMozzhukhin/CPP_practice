#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdlib>

#include "util/logger.hpp"
#include "core/game_engine.hpp"
#include "core/game_config.hpp"
#include "yaml/yaml_loader.hpp"

using std::string;
using std::string_view;

namespace {

void print_usage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "Options:\n"
        << "  --n <int>                 Number of players (default: 9)\n"
        << "  --seed <uint>             RNG seed (default: 0 => random)\n"
        << "  --human                   Enable one interactive human player\n"
        << "  --log <short|full>        Console log verbosity (default: short)\n"
        << "  --open                    Open announcements (show roles/targets in logs)\n"
        << "  --logs-dir <path>         Directory for round and summary files (default: logs)\n"
        << "  --tie <none|random>       Day tie policy (default: none)\n"
        << "  --k-mafia-div <int>       Mafia divisor (default: 3)\n"
        << "  --yaml <path>             Load config overrides from YAML file\n"
        << "  --exec <0|1>              Executioner (Палач) count (default: 1)\n"
        << "  --journ <0|1>             Journalist (Журналист) count (default: 1)\n"
        << "  --ears <0|1>              Eavesdropper (Ушастик) count (default: 1)\n"
        << "  -h, --help                Show this help\n";
}

bool parse_uint(string_view s, unsigned& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    unsigned long v = std::strtoul(std::string(s).c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    out = static_cast<unsigned>(v);
    return true;
}

bool parse_size(string_view s, std::size_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    unsigned long v = std::strtoul(std::string(s).c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    out = static_cast<std::size_t>(v);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    util::Logger root("mafia-sim");

    core::GameConfig cfg; // значения по умолчанию

    // --- CLI ---
    std::vector<string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];

        if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (a == "--n" && i + 1 < args.size()) {
            if (!parse_size(args[++i], cfg.n_players)) {
                std::cerr << "Invalid --n value\n"; return 1;
            }
        } else if (a == "--seed" && i + 1 < args.size()) {
            if (!parse_uint(args[++i], cfg.seed)) {
                std::cerr << "Invalid --seed value\n"; return 1;
            }
        } else if (a == "--human") {
            cfg.human = true;
        } else if (a == "--log" && i + 1 < args.size()) {
            string v = args[++i];
            if (v == "short") cfg.log_mode = core::GameConfig::LogMode::Short;
            else if (v == "full") cfg.log_mode = core::GameConfig::LogMode::Full;
            else { std::cerr << "Invalid --log value\n"; return 1; }
        } else if (a == "--open") {
            cfg.open_announcements = true;
        } else if (a == "--logs-dir" && i + 1 < args.size()) {
            cfg.logs_dir = args[++i];
        } else if (a == "--tie" && i + 1 < args.size()) {
            string v = args[++i];
            if (v == "none") cfg.tie_policy = core::GameConfig::TiePolicy::None;
            else if (v == "random") cfg.tie_policy = core::GameConfig::TiePolicy::Random;
            else { std::cerr << "Invalid --tie value\n"; return 1; }
        } else if (a == "--k-mafia-div" && i + 1 < args.size()) {
            if (!parse_size(args[++i], cfg.k_mafia_divisor) || cfg.k_mafia_divisor == 0) {
                std::cerr << "Invalid --k-mafia-div value\n"; return 1;
            }
        } else if (a == "--yaml" && i + 1 < args.size()) {
            cfg.yaml_path = args[++i];
        } else if (a == "--exec" && i + 1 < args.size()) {
            if (!parse_size(args[++i], cfg.executioner_count) || cfg.executioner_count > 1) {
                std::cerr << "Invalid --exec (allowed 0 or 1)\n"; return 1;
            }
        } else if (a == "--journ" && i + 1 < args.size()) {
            if (!parse_size(args[++i], cfg.journalist_count) || cfg.journalist_count > 1) {
                std::cerr << "Invalid --journ (allowed 0 or 1)\n"; return 1;
            }
        } else if (a == "--ears" && i + 1 < args.size()) {
            if (!parse_size(args[++i], cfg.eavesdropper_count) || cfg.eavesdropper_count > 1) {
                std::cerr << "Invalid --ears (allowed 0 or 1)\n"; return 1;
            }
        } else {
            std::cerr << "Unknown option: " << a << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // --- YAML (если задан) ---
    if (!cfg.yaml_path.empty()) {
        if (!yaml::load_config_from_yaml(cfg.yaml_path, cfg, &root)) {
            std::cerr << "Failed to load YAML config: " << cfg.yaml_path << "\n";
            return 1;
        }
    }

    root.info("mafia-sim: initializing");

    try {
        core::GameEngine engine(cfg, root);
        engine.run();
        root.info("mafia-sim: finished");
    } catch (const std::exception& ex) {
        root.error(std::string("Fatal: ") + ex.what());
        return 2;
    }

    return 0;
}
