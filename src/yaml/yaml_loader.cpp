#include "yaml/yaml_loader.hpp"

#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>

#include "core/game_config.hpp"
#include "util/logger.hpp"

namespace yaml {

namespace {

inline void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        [](unsigned char ch){ return !std::isspace(ch); }));
}
inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
        [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
}
inline void trim(std::string& s) { ltrim(s); rtrim(s); }

inline std::string to_lower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return v;
}

bool parse_bool(std::string v, bool& out) {
    v = to_lower(v);
    if (v == "true" || v == "yes" || v == "on"  || v == "1") { out = true;  return true; }
    if (v == "false"|| v == "no"  || v == "off" || v == "0") { out = false; return true; }
    return false;
}

bool parse_size(std::string v, std::size_t& out) {
    trim(v);
    if (v.empty()) return false;
    char* end = nullptr;
    unsigned long val = std::strtoul(v.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<std::size_t>(val);
    return true;
}

bool parse_uint(std::string v, unsigned& out) {
    trim(v);
    if (v.empty()) return false;
    char* end = nullptr;
    unsigned long val = std::strtoul(v.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<unsigned>(val);
    return true;
}

} // namespace

bool load_config_from_yaml(const std::string& path,
                           core::GameConfig& cfg,
                           util::Logger* log)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        if (log) log->error(std::string("yaml: cannot open file: ") + path);
        return false;
    }

    if (log) log->info(std::string("yaml: loading config from ") + path);

    std::string line;
    std::size_t lineno = 0;

    auto warn = [&](const std::string& msg){
        if (log) log->warn("yaml: " + msg);
    };
    auto info = [&](const std::string& msg){
        if (log) log->info("yaml: " + msg);
    };

    while (std::getline(ifs, line)) {
        ++lineno;

        // Удалить комментарий (#...) если есть
        auto hash_pos = line.find('#');
        if (hash_pos != std::string::npos) line.erase(hash_pos);

        trim(line);
        if (line.empty()) continue;

        // key : value
        auto colon = line.find(':');
        if (colon == std::string::npos) {
            warn("line " + std::to_string(lineno) + ": missing ':'");
            continue;
        }

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        rtrim(key);
        ltrim(val);

        key = to_lower(key);

        // --- разбор ключей ---
        if (key == "n_players") {
            std::size_t v{};
            if (parse_size(val, v) && v >= 1) {
                cfg.n_players = v;
                info("n_players = " + std::to_string(v));
            } else warn("line " + std::to_string(lineno) + ": invalid n_players");
        }
        else if (key == "seed") {
            unsigned v{};
            if (parse_uint(val, v)) {
                cfg.seed = v;
                info("seed = " + std::to_string(v));
            } else warn("line " + std::to_string(lineno) + ": invalid seed");
        }
        else if (key == "human") {
            bool v{};
            if (parse_bool(val, v)) {
                cfg.human = v;
                info(std::string("human = ") + (v ? "true" : "false"));
            } else warn("line " + std::to_string(lineno) + ": invalid human");
        }
        else if (key == "log") {
            std::string v = to_lower(val);
            if (v == "short") { cfg.log_mode = core::GameConfig::LogMode::Short; info("log = short"); }
            else if (v == "full") { cfg.log_mode = core::GameConfig::LogMode::Full; info("log = full"); }
            else warn("line " + std::to_string(lineno) + ": invalid log (use short|full)");
        }
        else if (key == "open" || key == "open_announcements") {
            bool v{};
            if (parse_bool(val, v)) {
                cfg.open_announcements = v;
                info(std::string("open_announcements = ") + (v ? "true" : "false"));
            } else warn("line " + std::to_string(lineno) + ": invalid open/open_announcements");
        }
        else if (key == "logs_dir") {
            trim(val);
            if (!val.empty()) {
                cfg.logs_dir = val;
                info("logs_dir = " + val);
            } else warn("line " + std::to_string(lineno) + ": empty logs_dir");
        }
        else if (key == "tie") {
            std::string v = to_lower(val);
            if (v == "none") { cfg.tie_policy = core::GameConfig::TiePolicy::None; info("tie = none"); }
            else if (v == "random") { cfg.tie_policy = core::GameConfig::TiePolicy::Random; info("tie = random"); }
            else warn("line " + std::to_string(lineno) + ": invalid tie (use none|random)");
        }
        else if (key == "k_mafia_div" || key == "k_mafia_divisor") {
            std::size_t v{};
            if (parse_size(val, v) && v >= 1) {
                cfg.k_mafia_divisor = v;
                info("k_mafia_div = " + std::to_string(v));
            } else warn("line " + std::to_string(lineno) + ": invalid k_mafia_div (>=1 required)");
        }
        // --- дополнительные роли из ТЗ ---
        else if (key == "executioner_count") {
            std::size_t v{};
            if (parse_size(val, v) && (v == 0 || v == 1)) {
                cfg.executioner_count = v;
                info("executioner_count = " + std::to_string(v));
            } else warn("line " + std::to_string(lineno) + ": invalid executioner_count (0 or 1)");
        }
        else if (key == "journalist_count") {
            std::size_t v{};
            if (parse_size(val, v) && (v == 0 || v == 1)) {
                cfg.journalist_count = v;
                info("journalist_count = " + std::to_string(v));
            } else warn("line " + std::to_string(lineno) + ": invalid journalist_count (0 or 1)");
        }
        else if (key == "eavesdropper_count") {
            std::size_t v{};
            if (parse_size(val, v) && (v == 0 || v == 1)) {
                cfg.eavesdropper_count = v;
                info("eavesdropper_count = " + std::to_string(v));
            } else warn("line " + std::to_string(lineno) + ": invalid eavesdropper_count (0 or 1)");
        }
        else {
            // неизвестный ключ — просто предупреждение
            warn("line " + std::to_string(lineno) + ": unknown key '" + key + "'");
        }
    }

    return true;
}

} // namespace yaml
