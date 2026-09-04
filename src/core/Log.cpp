#include "core/Log.h"

#include <cstdio>
#include <exception>
#include <memory>

namespace uta {

// constinit: the design sanctions exactly one global, so that one is
// constructed at compile time and cannot participate in static
// initialisation order at all.
constinit LogCategory logCore{"core"};

namespace {

/// Render text safe to put on one log line (CWE-117). Message text carries
/// filesystem paths and, once upkg lands, bytes out of somebody else's
/// package -- so an embedded newline could forge a log line and an embedded
/// escape sequence could rewrite the terminal or hide the very message being
/// reported. Shared by both sinks: fixing one would half-fix it.
[[nodiscard]] std::string sanitised(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        if (c < 0x20 || c == 0x7f) {
            static constexpr char kHex[] = "0123456789abcdef";
            out += "\\x";
            out += kHex[c >> 4];
            out += kHex[c & 0x0f];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

}  // namespace

std::string_view logLevelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace:   return "trace";
    case LogLevel::Debug:   return "debug";
    case LogLevel::Info:    return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error:   return "error";
    case LogLevel::Off:     return "off";
    }
    return "unknown";
}

Logger& Logger::instance() noexcept {
    // Deliberately leaked. A function-local static is destroyed in reverse
    // order of construction, so a Logger built on first use is destroyed
    // BEFORE every static constructed earlier -- and a static whose destructor
    // logs would then touch a destroyed mutex and vector. Never freeing it
    // makes logging valid for the whole life of the process, which is what a
    // diagnostic subsystem has to promise. The OS reclaims it at exit.
    static Logger* const logger = new Logger;
    return *logger;
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
    // The lock is held across arbitrary sink code, which INV-5 requires. So a
    // sink that logs -- the first thing a sink author reaches for when its own
    // write fails -- would re-enter here and self-lock a non-recursive mutex,
    // which is undefined and in practice hangs the process. A hang is the
    // worst failure a diagnostic subsystem can have, and nothing detects it:
    // ThreadSanitizer does not do deadlock detection.
    //
    // Drop the nested line instead. Per thread, so an unrelated thread logging
    // at the same time is unaffected.
    static thread_local bool inWrite = false;
    if (inWrite) return;
    inWrite = true;
    struct Leaving {
        bool& flag;
        ~Leaving() { flag = false; }
    } leaving{inWrite};

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
        const std::string text = sanitised(record.text);
        (void)std::fprintf(stderr, "[%.*s] %.*s: %.*s\n",
                     static_cast<int>(logLevelName(record.level).size()),
                     logLevelName(record.level).data(),
                     static_cast<int>(record.category.size()), record.category.data(),
                     static_cast<int>(text.size()), text.data());
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
        const std::string text = sanitised(record.text);
        (void)std::fprintf(stream.get(), "[%.*s] %.*s: %.*s\n",
                     static_cast<int>(logLevelName(record.level).size()),
                     logLevelName(record.level).data(),
                     static_cast<int>(record.category.size()), record.category.data(),
                     static_cast<int>(text.size()), text.data());
        (void)std::fflush(stream.get());
    };
}

}  // namespace uta
