#include "core/Jobs.h"

#include <exception>
#include <utility>

#include "core/Log.h"

namespace uta {

thread_local JobSystem* JobSystem::currentSystem_ = nullptr;

namespace {

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
    for (unsigned i = 0; i < count; ++i)
        workers_.emplace_back([this] { workerLoop(); });
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
    {
        const std::lock_guard lock(mutex_);
        queue_.push_back(Job{std::move(job), state});
    }
    cv_.notify_all();
    return JobHandle(std::move(state));
}

void JobSystem::parallelFor(std::size_t count,
                            const std::function<void(std::size_t)>& body) {
    if (count == 0) return;

    std::vector<JobHandle> handles;
    handles.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        handles.push_back(submit([&body, i] { body(i); }));

    for (const JobHandle& handle : handles) wait(handle);
}

void JobSystem::runJob(Job& job) noexcept {
    try {
        if (job.body) job.body();
    } catch (const std::exception& e) {
        UTA_LOG(logCore, LogLevel::Error, "a job threw and was contained: {}", e.what());
    } catch (...) {
        UTA_LOG(logCore, LogLevel::Error, "a job threw a non-exception and was contained");
    }

    // Marked under the mutex so a waiter cannot check the flag, miss it, and
    // then wait past the notification.
    {
        const std::lock_guard lock(mutex_);
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
