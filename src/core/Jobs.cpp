#include "core/Jobs.h"

#include <exception>
#include <utility>

#include "core/Log.h"

namespace uta {

thread_local JobSystem* JobSystem::currentSystem_ = nullptr;

namespace {

/// Reports a job failure that was contained.
///
/// No local try/catch: UTA_LOG contains its own throw now, so every noexcept
/// caller is covered rather than each one rediscovering the hazard. This
/// wrapper is kept for the shared message.
void logContainedFailure(std::string_view detail) noexcept {
    UTA_LOG(logCore, LogLevel::Error, "a job threw and was contained: {}", detail);
}

/// The worker count, with the underflow guard the arithmetic needs.
[[nodiscard]] unsigned resolveWorkerCount(unsigned requested) {
    if (requested != 0) return requested;

    const unsigned reported = std::thread::hardware_concurrency();
    // Test for 0 and 1 BEFORE subtracting. hardware_concurrency() returns
    // unsigned, so 0 - 1 is UINT_MAX and clamping afterwards would leave it.
    if (reported <= 1) return 1;
    return reported - 1;
}

}  // namespace

JobSystem::JobSystem(unsigned workerCount) {
    const unsigned count = resolveWorkerCount(workerCount);
    workers_.reserve(count);
    try {
        for (unsigned i = 0; i < count; ++i)
            workers_.emplace_back([this] { workerLoop(); });
    } catch (...) {
        // std::thread's constructor throws when the OS refuses a thread, and
        // an abandoned constructor never runs ~JobSystem -- so workers_ would
        // be destroyed holding JOINABLE threads, which is defined to call
        // std::terminate. Measured: the process aborted with "terminate called
        // without an active exception" and no diagnostic at all.
        //
        // So stop and join what did start, then let the caller see the real
        // exception.
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (std::thread& worker : workers_)
            if (worker.joinable()) worker.join();
        throw;
    }
}

JobSystem::~JobSystem() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();

    // The workers drain the queue before they exit -- see workerLoop -- so by
    // the time the last join returns, every submitted job has run (INV-12).
    for (std::thread& worker : workers_)
        if (worker.joinable()) worker.join();
}

JobHandle JobSystem::submit(std::function<void()> job) {
    auto state = std::make_shared<JobHandle::State>();
    state->owner = this;
    {
        const std::lock_guard lock(mutex_);
        queue_.push_back(Job{std::move(job), state});
    }
    cv_.notify_all();
    return JobHandle(std::move(state));
}

std::size_t JobSystem::parallelFor(std::size_t count,
                                   const std::function<void(std::size_t)>& body) {
    if (count == 0) return 0;

    std::vector<JobHandle> handles;
    handles.reserve(count);

    // `body` is captured by REFERENCE, so every job queued here must finish
    // before this frame goes. submit() allocates and can throw, so on a throw
    // part-way through, the jobs already queued would still hold that
    // reference -- and a caller passing a temporary lambda, which is the
    // natural shape, destroys it at the end of the full expression while
    // workers are still calling it. Wait for what was queued, then rethrow.
    try {
        for (std::size_t i = 0; i < count; ++i)
            handles.push_back(submit([&body, i] { body(i); }));
    } catch (...) {
        for (const JobHandle& handle : handles) wait(handle);
        throw;
    }

    for (const JobHandle& handle : handles) wait(handle);

    // Counted after every job has completed, so each handle's outcome is
    // published. A count rather than a flag: a caller deciding whether a bake
    // is usable needs to know how much of it failed.
    std::size_t failures = 0;
    for (const JobHandle& handle : handles)
        if (handle.failed()) ++failures;
    return failures;
}

void JobSystem::runJob(Job& job) noexcept {
    bool threw = false;
    try {
        if (job.body) job.body();
    } catch (const std::exception& e) {
        threw = true;
        logContainedFailure(e.what());
    } catch (...) {
        threw = true;
        logContainedFailure("a non-exception type");
    }

    // Marked under the mutex so a waiter cannot check the flag, miss it, and
    // then wait past the notification.
    {
        const std::lock_guard lock(mutex_);
        // failed BEFORE done, and the release store on done is what publishes
        // it: a waiter that sees completion sees the outcome with it (INV-16).
        job.state->failed.store(threw, std::memory_order_relaxed);
        job.state->done.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

void JobSystem::workerLoop() {
    currentSystem_ = this;

    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });

            // Drain before exiting: stopping_ with work left still runs it.
            if (queue_.empty()) {
                currentSystem_ = nullptr;
                return;
            }

            job = std::move(queue_.front());
            queue_.pop_front();
        }
        runJob(job);
    }
}

void JobSystem::wait(const JobHandle& handle) {
    if (handle.done()) return;

    // A handle from another JobSystem is completed by that system's condition
    // variable, never this one, so waiting here would block until something
    // unrelated woke us -- on an idle pool, forever. Say so and return rather
    // than hang: a worker parked this way never returns to its loop, and the
    // destructor's join would then never return either.
    if (handle.state_ != nullptr && handle.state_->owner != this) {
        UTA_LOG(logCore, LogLevel::Error,
                "wait() was given a JobHandle from a different JobSystem; "
                "returning without waiting");
        return;
    }

    std::unique_lock lock(mutex_);

    if (currentSystem_ == this) {
        // Called from inside a job. Blocking here would deadlock a pool whose
        // every worker is doing the same thing -- on a pool of one, it would
        // deadlock immediately. Run pending work instead (INV-10).
        while (!handle.done()) {
            if (queue_.empty()) {
                cv_.wait(lock);
                continue;
            }
            Job job = std::move(queue_.front());
            queue_.pop_front();

            lock.unlock();
            runJob(job);
            lock.lock();
        }
        return;
    }

    cv_.wait(lock, [&handle] { return handle.done(); });
}

}  // namespace uta
