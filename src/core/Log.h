// The one logger, with a category per part.
//
// docs/design.md § What every part does the same way: "One logger, in core,
// with a category per part and levels. No printf, no std::cout outside a
// program's own startup." And its Determinism bullet names the logger as the
// one global mutable state the design sanctions.
//
// Lines are written by the thread that logged them, under a lock, rather than
// handed to a background writer (docs/specs/UTA-0002-core-foundations.md
// section 3): the lines immediately before a crash are the ones worth having,
// and those are the ones a queue loses.

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace uta {

enum class LogLevel : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Off,
};

/// The short, stable name of a level. Never empty.
[[nodiscard]] std::string_view logLevelName(LogLevel level) noexcept;

/// One per part, declared once at that part's own scope. The minimum is
/// settable at runtime and read on every log call, so it is atomic.
class LogCategory {
public:
    explicit LogCategory(std::string_view name, LogLevel minimum = LogLevel::Info)
        : name_(name), minimum_(minimum) {}

    LogCategory(const LogCategory&) = delete;
    LogCategory& operator=(const LogCategory&) = delete;

    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    [[nodiscard]] LogLevel minimum() const noexcept {
        return minimum_.load(std::memory_order_relaxed);
    }

    void setMinimum(LogLevel level) noexcept {
        minimum_.store(level, std::memory_order_relaxed);
    }

    /// The test UTA_LOG makes BEFORE formatting its arguments (INV-4).
    [[nodiscard]] bool enabled(LogLevel level) const noexcept {
        return level >= minimum() && level != LogLevel::Off;
    }

private:
    std::string_view name_;
    std::atomic<LogLevel> minimum_;
};

/// What a sink receives. Its views are valid for the duration of the sink
/// call and no longer -- a sink that keeps anything copies it.
struct LogRecord {
    std::string_view category;
    LogLevel level;
    std::string_view text;
    std::chrono::system_clock::time_point time;
    std::thread::id thread;
};

using LogSink = std::function<void(const LogRecord&)>;

/// The one global the design sanctions. The macro formats the message; this
/// stamps, locks and dispatches, so no two sink calls overlap (INV-5).
class Logger {
public:
    [[nodiscard]] static Logger& instance() noexcept;

    void addSink(LogSink sink);

    /// Drops every sink. For tests, and for a program tearing down its own
    /// logging deliberately.
    void clearSinks();

    /// Never throws and never reports failure: a logger that could fail would
    /// turn every log line into a branch. A throwing sink is caught here and
    /// the remaining sinks still run (INV-3).
    void write(const LogCategory& category, LogLevel level, std::string_view text) noexcept;

private:
    Logger() = default;

    std::mutex mutex_;
    std::vector<LogSink> sinks_;
};

/// Writes to stderr. A program installs this at startup; nothing else does.
[[nodiscard]] LogSink consoleSink();

/// Appends to `path`, opening it once. A failure to open is reported by the
/// returned sink doing nothing rather than by throwing, for the reason above.
[[nodiscard]] LogSink fileSink(const std::filesystem::path& path);

/// core's own category.
extern LogCategory logCore;

}  // namespace uta

/// The only way a part logs.
///
/// The level test happens BEFORE std::format is called, so a suppressed
/// message costs a relaxed load and a branch and does not evaluate its
/// arguments (INV-4).
#define UTA_LOG(category, level, ...)                                        \
    do {                                                                     \
        const ::uta::LogCategory& utaCat = (category);                       \
        const ::uta::LogLevel utaLvl = (level);                              \
        if (utaCat.enabled(utaLvl))                                          \
            ::uta::Logger::instance().write(utaCat, utaLvl,                  \
                                            ::std::format(__VA_ARGS__));     \
    } while (false)
