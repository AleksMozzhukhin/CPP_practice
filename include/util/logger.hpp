#pragma once
#include <mutex>
#include <fstream>
#include <string>
#include <string_view>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <optional>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace util {

class Logger {
public:
    enum class Level { Trace, Debug, Info, Warn, Error };

    // Если file не задан — вывод только в stdout.
    explicit Logger(std::optional<std::filesystem::path> file = std::nullopt)
        : file_path_(std::move(file))
    {
        if (file_path_) {
            open_file_(*file_path_);
        }
    }

    // Переназначить файл вывода.
    void set_file(const std::filesystem::path& file) {
        std::scoped_lock lk(mu_);
        file_path_ = file;
        open_file_(file);
    }

    // Убрать файловый вывод.
    void clear_file() {
        std::scoped_lock lk(mu_);
        file_path_.reset();
        stream_.close();
    }

    void set_level(Level lv) noexcept { level_.store(static_cast<int>(lv), std::memory_order_relaxed); }

    Level level() const noexcept {
        return static_cast<Level>(level_.load(std::memory_order_relaxed));
    }

    // Универсальный метод логирования.
    void log(Level lv, std::string_view msg) {
        if (lv < level()) return;
        auto line = format_line_(lv, msg);
        std::scoped_lock lk(mu_);
        // stdout
        std::cout << line << std::endl;
        // файл (если настроен)
        if (file_path_) {
            if (!stream_.is_open()) open_file_(*file_path_);
            stream_ << line << '\n';
            stream_.flush();
        }
    }

    // Шорткаты
    void trace(std::string_view msg) { log(Level::Trace, msg); }
    void debug(std::string_view msg) { log(Level::Debug, msg); }
    void info (std::string_view msg) { log(Level::Info , msg); }
    void warn (std::string_view msg) { log(Level::Warn , msg); }
    void error(std::string_view msg) { log(Level::Error, msg); }

private:
    std::mutex mu_;
    std::optional<std::filesystem::path> file_path_;
    std::ofstream stream_;
    std::atomic<int> level_{static_cast<int>(Level::Info)};

    static const char* level_str_(Level lv) noexcept {
        switch (lv) {
            case Level::Trace: return "TRACE";
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO";
            case Level::Warn:  return "WARN";
            case Level::Error: return "ERROR";
        }
        return "INFO";
    }

    static std::string timestamp_() {
        using clock = std::chrono::system_clock;
        auto now = clock::now();
        auto t   = clock::to_time_t(now);
        auto us  = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1'000'000;

        std::tm tm{};
        #if defined(_WIN32)
            localtime_s(&tm, &t);
        #else
            localtime_r(&t, &tm);
        #endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.'
            << std::setw(6) << std::setfill('0') << us;
        return oss.str();
    }

    static std::string format_line_(Level lv, std::string_view msg) {
        std::ostringstream oss;
        oss << '[' << timestamp_() << "] [" << level_str_(lv) << "] " << msg;
        return oss.str();
    }

    void open_file_(const std::filesystem::path& file) {
        std::error_code ec;
        auto dir = file.parent_path();
        if (!dir.empty()) {
            std::filesystem::create_directories(dir, ec);
        }
        stream_.close();
        stream_.open(file, std::ios::out | std::ios::app);
    }
};

} // namespace util
