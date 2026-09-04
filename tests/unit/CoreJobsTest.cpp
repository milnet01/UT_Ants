// Locks INV-9 to INV-13 of docs/specs/UTA-0002-core-foundations.md.
//
// Every fixture here is race-free in ITSELF. INV-13 requires this file clean
// under ThreadSanitizer, so a shared std::vector appended by several workers
// would report the test rather than the pool -- each job writes its own slot
// instead.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "core/Jobs.h"

using uta::JobHandle;
using uta::JobSystem;

// INV-9 -- every submitted job runs exactly once.
TEST_CASE("every submitted job runs exactly once", "[core][jobs]") {
    JobSystem jobs(4);

    constexpr std::size_t kCount = 500;
    // One counter per job: a shared counter would prove the total but not
    // that no single job ran twice.
    std::vector<std::atomic<int>> counters(kCount);

    std::vector<JobHandle> handles;
    handles.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i)
        handles.push_back(jobs.submit([&counters, i] { ++counters[i]; }));

    for (const JobHandle& handle : handles) jobs.wait(handle);

    for (std::size_t i = 0; i < kCount; ++i)
        REQUIRE(counters[i].load() == 1);
}

TEST_CASE("parallelFor covers every index exactly once", "[core][jobs]") {
    JobSystem jobs(4);

    constexpr std::size_t kCount = 256;
    std::vector<std::atomic<int>> seen(kCount);

    jobs.parallelFor(kCount, [&seen](std::size_t i) { ++seen[i]; });

    for (std::size_t i = 0; i < kCount; ++i) REQUIRE(seen[i].load() == 1);
}

TEST_CASE("parallelFor over nothing does nothing", "[core][jobs]") {
    JobSystem jobs(2);
    std::atomic<int> calls{0};
    jobs.parallelFor(0, [&calls](std::size_t) { ++calls; });
    CHECK(calls.load() == 0);
}

// INV-10 -- wait() from inside a job does not deadlock.
//
// The pool size is what isolates the rule. With spare workers another worker
// takes the inner job and a blocking wait() would pass; with one worker, a
// blocking wait() deadlocks immediately. The TIMEOUT on catch_discover_tests
// is what turns that deadlock into a failure rather than a hung run.
TEST_CASE("waiting from inside a job does not deadlock a single worker",
          "[core][jobs]") {
    JobSystem jobs(1);
    REQUIRE(jobs.workerCount() == 1);

    std::atomic<int> inner{0};

    const JobHandle outer = jobs.submit([&jobs, &inner] {
        const JobHandle nested = jobs.submit([&inner] { ++inner; });
        jobs.wait(nested);
        // Reaching here at all is the assertion: the nested job ran on this
        // same worker, inside wait().
        ++inner;
    });

    jobs.wait(outer);
    CHECK(inner.load() == 2);
}

TEST_CASE("nested waits several levels deep still complete", "[core][jobs]") {
    JobSystem jobs(1);
    std::atomic<int> depthReached{0};

    const JobHandle outer = jobs.submit([&jobs, &depthReached] {
        const JobHandle middle = jobs.submit([&jobs, &depthReached] {
            const JobHandle innermost =
                jobs.submit([&depthReached] { depthReached = 3; });
            jobs.wait(innermost);
        });
        jobs.wait(middle);
    });

    jobs.wait(outer);
    CHECK(depthReached.load() == 3);
}

// INV-11 -- an exception from a job body does not escape the worker, and the
// pool keeps running.
TEST_CASE("a throwing job is contained and later jobs still run",
          "[core][jobs]") {
    JobSystem jobs(2);

    const JobHandle thrower =
        jobs.submit([] { throw std::runtime_error("job failed"); });
    const JobHandle nonException = jobs.submit([] { throw 42; });

    std::atomic<int> after{0};
    const JobHandle counter = jobs.submit([&after] { ++after; });

    jobs.wait(thrower);
    jobs.wait(nonException);
    jobs.wait(counter);

    // A contained throw still completes its handle -- a caller waiting on a
    // job that threw must not wait forever.
    CHECK(thrower.done());
    CHECK(nonException.done());
    CHECK(after.load() == 1);
}

// INV-12 -- the destructor drains and joins; every submitted job has run by
// the time it returns.
TEST_CASE("the destructor runs every submitted job before returning",
          "[core][jobs]") {
    constexpr std::size_t kCount = 200;
    std::vector<std::atomic<int>> written(kCount);

    {
        JobSystem jobs(4);
        for (std::size_t i = 0; i < kCount; ++i)
            (void)jobs.submit([&written, i] { ++written[i]; });
        // Deliberately no wait: leaving scope is what must drain them.
    }

    for (std::size_t i = 0; i < kCount; ++i)
        REQUIRE(written[i].load() == 1);
}

TEST_CASE("a pool with nothing submitted destroys cleanly", "[core][jobs]") {
    { JobSystem jobs(4); }
    SUCCEED("no hang and no crash");
}

TEST_CASE("the default worker count is at least one", "[core][jobs]") {
    JobSystem jobs;
    // The underflow guard: hardware_concurrency() reporting 0 must not become
    // UINT_MAX workers. Any real machine reports more, so this asserts the
    // floor rather than the arithmetic -- the arithmetic is asserted by the
    // constructor being able to start at all.
    CHECK(jobs.workerCount() >= 1);

    std::atomic<int> ran{0};
    jobs.wait(jobs.submit([&ran] { ++ran; }));
    CHECK(ran.load() == 1);
}

TEST_CASE("waiting on an already-finished job returns at once", "[core][jobs]") {
    JobSystem jobs(2);
    std::atomic<int> ran{0};
    const JobHandle handle = jobs.submit([&ran] { ++ran; });
    jobs.wait(handle);
    jobs.wait(handle);  // second wait must not block
    CHECK(ran.load() == 1);
    CHECK(handle.done());
}

TEST_CASE("a default-constructed handle is already complete", "[core][jobs]") {
    JobSystem jobs(2);
    const JobHandle empty;
    CHECK(empty.done());
    jobs.wait(empty);  // must return immediately
    SUCCEED("waiting on an empty handle returned");
}
