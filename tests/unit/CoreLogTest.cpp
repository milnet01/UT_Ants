// Locks INV-4, INV-5 and the Logger half of INV-3, per
// docs/specs/UTA-0002-core-foundations.md.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "core/Log.h"

using uta::LogCategory;
using uta::Logger;
using uta::LogLevel;
using uta::LogRecord;

namespace {

/// Installs sinks for one test and takes them away again, so no test can be
/// affected by what another installed.
class SinkScope {
public:
    SinkScope() { Logger::instance().clearSinks(); }
    ~SinkScope() { Logger::instance().clearSinks(); }
    SinkScope(const SinkScope&) = delete;
    SinkScope& operator=(const SinkScope&) = delete;
};

}  // namespace

// INV-4 -- a message below its category's minimum is not formatted at all:
// the arguments are not evaluated.
TEST_CASE("a suppressed message does not evaluate its arguments", "[core][log]") {
    const SinkScope scope;

    // A sink IS installed. Without one, an implementation could skip
    // formatting for a different reason and this would pass for free.
    std::atomic<int> delivered{0};
    Logger::instance().addSink([&delivered](const LogRecord&) { ++delivered; });

    LogCategory quiet{"quiet", LogLevel::Warning};

    int evaluations = 0;
    const auto countMe = [&evaluations]() { ++evaluations; return 1; };

    UTA_LOG(quiet, LogLevel::Trace, "suppressed {}", countMe());
    CHECK(evaluations == 0);
    CHECK(delivered.load() == 0);

    UTA_LOG(quiet, LogLevel::Error, "delivered {}", countMe());
    CHECK(evaluations == 1);
    CHECK(delivered.load() == 1);
}

TEST_CASE("a category's minimum is settable at runtime", "[core][log]") {
    const SinkScope scope;
    std::atomic<int> delivered{0};
    Logger::instance().addSink([&delivered](const LogRecord&) { ++delivered; });

    LogCategory cat{"cat", LogLevel::Error};
    UTA_LOG(cat, LogLevel::Info, "before");
    CHECK(delivered.load() == 0);

    cat.setMinimum(LogLevel::Info);
    UTA_LOG(cat, LogLevel::Info, "after");
    CHECK(delivered.load() == 1);
}

// INV-5 -- sinks are invoked serially: no two calls overlap.
//
// Stated as overlap rather than as "no line is spliced" on purpose. A sink is
// handed one whole LogRecord per call, so it sees intact text whether or not
// write() locks -- a text-integrity assertion would pass against a logger
// that never locked, and would be a green check on an unimplemented property.
TEST_CASE("sink calls never overlap", "[core][log]") {
    const SinkScope scope;

    std::atomic<int> inFlight{0};
    std::atomic<int> maxInFlight{0};
    std::atomic<int> delivered{0};

    Logger::instance().addSink([&](const LogRecord&) {
        const int now = inFlight.fetch_add(1) + 1;
        int previousMax = maxInFlight.load();
        while (now > previousMax &&
               !maxInFlight.compare_exchange_weak(previousMax, now)) {
        }
        // Widen the window an overlapping implementation would be caught in.
        std::this_thread::yield();
        ++delivered;
        inFlight.fetch_sub(1);
    });

    LogCategory cat{"concurrent", LogLevel::Trace};

    constexpr int kThreads = 8;
    constexpr int kPerThread = 50;
    std::vector<std::jthread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&cat, t] {
            for (int i = 0; i < kPerThread; ++i)
                UTA_LOG(cat, LogLevel::Info, "thread {} line {}", t, i);
        });
    }
    threads.clear();  // joins

    CHECK(delivered.load() == kThreads * kPerThread);
    CHECK(maxInFlight.load() == 1);
}

// INV-3, the Logger half -- a throwing sink is caught and dropped, and the
// sinks after it still run.
TEST_CASE("a throwing sink does not escape or stop the others", "[core][log]") {
    const SinkScope scope;

    std::atomic<int> reachedSecond{0};
    Logger::instance().addSink(
        [](const LogRecord&) { throw std::runtime_error("sink failed"); });
    Logger::instance().addSink(
        [&reachedSecond](const LogRecord&) { ++reachedSecond; });

    LogCategory cat{"throwing", LogLevel::Trace};

    // UTA_LOG is a statement, not an expression, so it cannot be wrapped in
    // CHECK_NOTHROW. It does not need to be: if the sink's exception escaped,
    // it would leave this test through Catch2's own handler and be reported
    // as an unexpected exception. Reaching the next line IS the assertion.
    UTA_LOG(cat, LogLevel::Error, "one");
    CHECK(reachedSecond.load() == 1);

    UTA_LOG(cat, LogLevel::Error, "two");
    CHECK(reachedSecond.load() == 2);
}

TEST_CASE("a record carries the category, level and text it was logged with",
          "[core][log]") {
    const SinkScope scope;

    std::string seenCategory;
    std::string seenText;
    LogLevel seenLevel = LogLevel::Off;
    Logger::instance().addSink([&](const LogRecord& record) {
        // Copied, not kept: the record's views are valid for this call only.
        seenCategory = std::string(record.category);
        seenText = std::string(record.text);
        seenLevel = record.level;
    });

    LogCategory cat{"upkg", LogLevel::Trace};
    UTA_LOG(cat, LogLevel::Warning, "{} names in {}", 42, "DM-Deck16.unr");

    CHECK(seenCategory == "upkg");
    CHECK(seenLevel == LogLevel::Warning);
    CHECK(seenText == "42 names in DM-Deck16.unr");
}
