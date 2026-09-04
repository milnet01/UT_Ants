// The one job system.
//
// docs/design.md § What every part does the same way: "One job system, in
// core. Simulation is single-threaded and deterministic. Rendering, asset
// loading and baking use jobs."
//
// COMPLETION ORDER IS UNSPECIFIED, and jobs run on unspecified threads. So no
// RESULT may depend on the order jobs finish in. That is a constraint on
// callers, not a ban on reproducible work: the design requires baking to use
// jobs and also requires one map, recipe and baker version to hash to one
// bundle on any machine, so ubake combines its job results in submission
// order and both hold.
//
// One shared, lock-guarded queue -- not per-worker work-stealing deques.
// Nothing measures contention yet, and the queue is private, so the faster
// design can replace it without touching a caller
// (docs/specs/UTA-0002-core-foundations.md section 3 and section 8).

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace uta {

class JobSystem;

/// Handle to submitted work. Copyable and cheap; it shares the job's
/// completion state rather than owning the job.
class JobHandle {
public:
    /// A handle to nothing, and therefore already complete. Exists so a
    /// caller can hold one as a member before it has work to put in it.
    JobHandle() = default;

    /// Whether the job has run. A handle is never invalid: a default-
    /// constructed one is already complete, so waiting on it returns at once.
    [[nodiscard]] bool done() const noexcept {
        return state_ == nullptr || state_->done.load(std::memory_order_acquire);
    }

private:
    friend class JobSystem;

    struct State {
        std::atomic<bool> done{false};
    };

    explicit JobHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}

    std::shared_ptr<State> state_;
};

class JobSystem {
public:
    /// 0 means one fewer than hardware_concurrency(), because the submitting
    /// thread is usually working too -- and one worker where that reports 0
    /// or 1.
    ///
    /// NOT `hardware_concurrency() - 1` clamped to 1: that return is
    /// unsigned, so 0 - 1 is UINT_MAX and a clamp against it changes nothing.
    /// The subtraction happens only after the 0-or-1 test.
    explicit JobSystem(unsigned workerCount = 0);

    /// Runs every job already submitted, then joins every worker. No job body
    /// runs after this returns (INV-12).
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// Never blocks and never drops work. The queue is unbounded by design,
    /// so a caller submitting without ever waiting can grow it without limit
    /// -- which is a bug in that caller.
    [[nodiscard]] JobHandle submit(std::function<void()> job);

    /// Submit `count` jobs, one per index, and wait for all of them.
    void parallelFor(std::size_t count, const std::function<void(std::size_t)>& body);

    /// Block until `handle`'s job has run.
    ///
    /// Called from a worker thread, this runs pending jobs while it waits
    /// instead of blocking, so a job that waits on a job cannot deadlock the
    /// pool -- including on a pool of one worker (INV-10).
    void wait(const JobHandle& handle);

    [[nodiscard]] unsigned workerCount() const noexcept {
        return static_cast<unsigned>(workers_.size());
    }

private:
    struct Job {
        std::function<void()> body;
        std::shared_ptr<JobHandle::State> state;
    };

    void workerLoop();

    /// Runs one job and marks it complete. An exception thrown by the body is
    /// caught here and logged: the worker loop is a thread boundary, which is
    /// where languages/cpp.md permits `catch (...)`, and letting one escape
    /// would call std::terminate (INV-11).
    void runJob(Job& job) noexcept;

    /// Which JobSystem, if any, owns the calling thread. This is what makes
    /// wait() help rather than block.
    static thread_local JobSystem* currentSystem_;

    std::mutex mutex_;

    /// One condition variable for both "a job arrived" and "a job finished".
    /// Two would be faster and would need every notify site to pick correctly;
    /// this cannot lose a wakeup, and nothing has measured the difference.
    std::condition_variable cv_;

    std::deque<Job> queue_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace uta
