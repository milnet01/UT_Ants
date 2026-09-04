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
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/Error.h"

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
    /// Takes const char*, not std::string_view, on purpose. The name is
    /// BORROWED and read on every log line, so it must outlive the category --
    /// a literal, or anything with static storage duration. A string_view
    /// parameter would silently accept a std::string temporary and then print
    /// freed memory; with const char* a std::string needs an explicit
    /// .c_str(), which is a visible act at the call site.
    constexpr explicit LogCategory(const char* name,
                                   LogLevel minimum = LogLevel::Info)
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
    // Defaulted so a caller constructing one by hand cannot read an
    // indeterminate level; every record this library makes sets it.
    LogLevel level = LogLevel::Info;
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

/// Appends to `path`, opening it once, and says why when it cannot.
///
/// This returns a Result where write() does not, and the difference is not an
/// inconsistency. write() is called from everywhere, including noexcept
/// functions, and has no channel of its own. fileSink is a factory called once
/// at startup by a caller that HAS one, and it sits on a module boundary,
/// where docs/design.md requires std::expected. Its failure path is ordinary
/// rather than exotic: on a first run the log directory does not exist, and a
/// sink that silently does nothing leaves a program running with logging off
/// and no way to find out.
///
/// The code is errorCodeFromErrno's, so a missing directory is NotFound.
[[nodiscard]] Result<LogSink> fileSink(const std::filesystem::path& path);

/// core's own category.
extern LogCategory logCore;

}  // namespace uta

/// The only way a part logs.
///
/// The level test happens BEFORE std::format is called, so a suppressed
/// message costs a relaxed load and a branch and does not evaluate its
/// arguments (INV-4).
///
/// The format call runs in the CALLER's expression, outside write()'s
/// noexcept, and std::format can throw -- so it is caught here rather than at
/// each call site. Without that, logging from any noexcept function is a
/// std::terminate waiting for an allocation failure, and every such caller
/// has to rediscover it.
#define UTA_LOG(category, level, ...)                                        \
    do {                                                                     \
        const ::uta::LogCategory& utaCat = (category);                       \
        const ::uta::LogLevel utaLvl = (level);                              \
        if (utaCat.enabled(utaLvl)) {                                        \
            try {                                                            \
                ::uta::Logger::instance().write(utaCat, utaLvl,              \
                                                ::std::format(__VA_ARGS__)); \
            } catch (...) {                                                  \
            }                                                                \
        }                                                                    \
    } while (false)
