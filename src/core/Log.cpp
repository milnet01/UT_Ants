#include "core/Log.h"

#include <cstdio>
#include <exception>
#include <memory>

namespace uta {

// constinit: the design sanctions exactly one global, so that one is
// constructed at compile time and cannot participate in static
// initialisation order at all.
constinit LogCategory logCore{"core"};

std::string_view logLevelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace:   return "trace";
    case LogLevel::Debug:   return "debug";
    case LogLevel::Info:    return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error:   return "error";
    case LogLevel::Off:     return "off";
    }
    return "info";
}

Logger& Logger::instance() noexcept {
    // Function-local static: initialised on first use, thread-safe by
    // [stmt.dcl], and never destroyed before the last logger call, which a
    // namespace-scope object could not promise.
    static Logger logger;
    return logger;
}

void Logger::addSink(LogSink sink) {
    const std::lock_guard lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::clearSinks() {
    const std::lock_guard lock(mutex_);
    sinks_.clear();
}

void Logger::write(const LogCategory& category, LogLevel level,
                   std::string_view text) noexcept {
    const LogRecord record{
        .category = category.name(),
        .level = level,
        .text = text,
        .time = std::chrono::system_clock::now(),
        .thread = std::this_thread::get_id(),
    };

    // The lock is held across every sink call, which is what makes INV-5's
    // no-overlap property true. It also means a slow sink slows every thread
    // that logs -- the trade section 3 took deliberately.
    const std::lock_guard lock(mutex_);
    for (const LogSink& sink : sinks_) {
        if (!sink) continue;
        try {
            sink(record);
        } catch (...) {
            // INV-3. A sink is somebody else's code; one that throws must not
            // take down the call site or stop the sinks after it. There is
            // nowhere to report this to -- reporting is what just failed.
        }
    }
}

LogSink consoleSink() {
    return [](const LogRecord& record) {
        // stderr, not stdout: a program's own output is stdout's, and a log
        // line interleaved into it is the thing design rule "no printf"
        // exists to stop.
        (void)std::fprintf(stderr, "[%.*s] %.*s: %.*s\n",
                     static_cast<int>(logLevelName(record.level).size()),
                     logLevelName(record.level).data(),
                     static_cast<int>(record.category.size()), record.category.data(),
                     static_cast<int>(record.text.size()), record.text.data());
    };
}

LogSink fileSink(const std::filesystem::path& path) {
    // Opened once and shared by every copy of the returned sink, and closed
    // when the last copy goes -- the shared_ptr's deleter is what owns it, so
    // there is no paired open/close for anyone to get wrong.
    //
    // A failure to open yields a null handle and the sink then does nothing.
    // The logger has no channel to report its own failure through, which is
    // the same reason write() swallows a throwing sink.
    std::FILE* raw =
#ifdef _WIN32
        _wfopen(path.c_str(), L"ab");
#else
        std::fopen(path.c_str(), "ab");
#endif
    const std::shared_ptr<std::FILE> stream(
        raw, [](std::FILE* f) { if (f != nullptr) (void)std::fclose(f); });

    return [stream](const LogRecord& record) {
        if (!stream) return;
        (void)std::fprintf(stream.get(), "[%.*s] %.*s: %.*s\n",
                     static_cast<int>(logLevelName(record.level).size()),
                     logLevelName(record.level).data(),
                     static_cast<int>(record.category.size()), record.category.data(),
                     static_cast<int>(record.text.size()), record.text.data());
        (void)std::fflush(stream.get());
    };
}

}  // namespace uta
