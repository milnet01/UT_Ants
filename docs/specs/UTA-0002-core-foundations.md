# UTA-0002 — build `core`: the error type, logging, filesystem and job system

**Status:** spec draft (2026-09-04).
**Kind:** implement.
**Source:** ROADMAP UTA-0002 (design-2026-09-03).
**Blocker for:** every later item — `core` is what they are built on.

Nothing here is visible to a player. `core` is the shared foundation every
other part of the engine sits on: how failures are reported, how the game
writes to its log, where its files live on disk, and how work is spread
across processor cores.

## 1. Goal

`src/core/` exists and builds as `uta_core`, a library linking nothing
beyond the C++ standard library. Every later part reports failure the same
way, logs the same way, finds its files the same way, and spreads work the
same way — because `core` decides all four once. Today the repository has
no `src/` sources at all; `CMakeLists.txt` says so in the comment where
`add_subdirectory(src)` would go.

## 2. Problem

`docs/design.md` § *What every part does the same way* fixes four
behaviours no code implements yet. Each is a contract other parts
bind to, so each one invented twice is two of them.

1. **Errors.** The design requires `std::expected<T, Error>` across every
   module boundary, with exceptions confined inside a part. There is no
   `Error`. An implementer starting `upkg` (UTA-0003) would define one,
   and `umat` would define another.
2. **Logging.** The design requires one logger with a category per part,
   and forbids `printf` and `std::cout` outside a program's own startup.
   With no logger to reach for, the forbidden thing is the only thing
   available.
3. **Filesystem.** Design rule 15 puts configuration and logs "where the
   platform puts them" and quarantines everything derived from the
   player's install under `content/`. Both are path decisions, and a
   path decision made per call site is made differently each time.
   `scripts/quarantine-guard.sh` already enforces the quarantine over
   the git index; nothing helps code stay on the right side of it.
4. **Threading.** The design requires one job system in `core`, with
   rendering, asset loading and baking using it and the simulation
   staying single-threaded. Where a part starts its own threads instead,
   "the simulation is single-threaded" stops being checkable.

The build is ready for this: `CMakeLists.txt` pins C++23 for
`std::expected` by name, and rejects GCC below 14, Clang below 18 and
MSVC below 19.40.

## 3. Scope decisions (agreed with the user)

Preference calls, all made by the user on 2026-09-04.

1. **The job queue is one shared, lock-guarded queue** — not per-worker
   work-stealing deques. Nothing measures contention yet, the API hides
   the queue, and the faster design can replace it without touching a
   caller. §8 records what was rejected.
2. **A ThreadSanitizer leg joins the pipeline in this item.** The job
   system is the project's first threaded code, and ordinary test runs
   do not see data races. This widens the item into `CMakeLists.txt` and
   `scripts/ci.sh`, which UTA-0041 shipped; §4.5 keeps the change to a
   build option and one step.
3. **An `Error` carries a reason code and a message, and nothing else** —
   no source location, no chain of causes. §8 records both.
4. **Log lines are written by the thread that logged them**, under a
   lock, rather than handed to a background writer. The lines before a
   crash are the ones worth having.

## 4. Design

Casing follows `languages/cpp.md`: PascalCase types, camelCase
functions, SCREAMING_SNAKE macros, trailing underscore on private
members — the convention `tests/support/UnrealPackageBuilder.h` already
uses for `UnrealPackageBuilder` and `encodeCompactIndex`.

### 4.1 Layout and the build

```
src/CMakeLists.txt              add_subdirectory(core)
src/core/CMakeLists.txt         the uta_core target
src/core/Error.h  Error.cpp
src/core/Log.h    Log.cpp
src/core/FileSystem.h  FileSystem.cpp
src/core/Jobs.h   Jobs.cpp
```

`uta_core` is a static library. Its only link entry is
`Threads::Threads`, which is how CMake spells the standard library's own
threading support and is what `std::thread` needs on GCC and Clang; any
other entry would breach design rule 1, and INV-14 is that rule
expressed where the build can see it. The root `CMakeLists.txt` gains
`add_subdirectory(src)`, replacing the comment that stands in for it.

### 4.2 The error type — `src/core/Error.h`

```cpp
namespace uta {

enum class ErrorCode : std::uint16_t {
    Unknown = 0,
    NotFound,
    PermissionDenied,
    AlreadyExists,
    InvalidArgument,
    MalformedData,
    UnsupportedVersion,
    IoFailure,
    OutOfMemory,
    Cancelled,
};

/// A failure crossing a module boundary: why it failed, and a sentence
/// saying so. Both are always present.
class Error {
public:
    Error(ErrorCode code, std::string message);

    [[nodiscard]] ErrorCode code() const noexcept;
    [[nodiscard]] std::string_view message() const noexcept;

    /// Prepend context, keeping the code:
    ///   readFile -> Error{NotFound, "no such file: dm-deck16.unr"}
    ///   .withContext("loading DM-Deck16")
    ///   -> "loading DM-Deck16: no such file: dm-deck16.unr"
    [[nodiscard]] Error withContext(std::string_view context) const;

private:
    ErrorCode code_;
    std::string message_;
};

template <class T>
using Result = std::expected<T, Error>;

/// Shorthand for a failure. `Result<void>` is how a function that
/// returns nothing reports one; there is no second spelling.
[[nodiscard]] std::unexpected<Error> fail(ErrorCode code, std::string message);

}  // namespace uta
```

Propagation. `std::expected`'s `and_then` and `transform` cover chained
transformations; imperative sequences get two macros, and `core` defines
no others:

```cpp
/// Bind, or return the error to the caller.
///   UTA_TRY(auto bytes, uta::fs::readFile(path));
#define UTA_TRY(declaration, expression) ...

/// Run for effect, or return the error to the caller.
///   UTA_CHECK(uta::fs::writeFileAtomically(path, bytes));
#define UTA_CHECK(expression) ...
```

Both expand to a uniquely named temporary (`__LINE__`, two-level
concatenation), a `has_value()` test returning `std::unexpected` on
failure, and — for `UTA_TRY` — a move out of the temporary. No statement
expressions, because MSVC has none.

### 4.3 Logging — `src/core/Log.h`

```cpp
namespace uta {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warning, Error, Off };

/// One per part, declared once at that part's own scope.
class LogCategory {
public:
    explicit LogCategory(std::string_view name, LogLevel minimum = LogLevel::Info);

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] LogLevel minimum() const noexcept;
    void setMinimum(LogLevel level) noexcept;   // atomic; settable at runtime
    [[nodiscard]] bool enabled(LogLevel level) const noexcept;
};

struct LogRecord {
    std::string_view category;
    LogLevel level;
    std::string_view text;
    std::chrono::system_clock::time_point time;
    std::thread::id thread;
};

using LogSink = std::function<void(const LogRecord&)>;

/// The one global the design sanctions ("no global mutable state except
/// the logger"). The macro formats the message; this stamps, locks and
/// dispatches, so two threads cannot interleave one line.
///
/// A sink is called synchronously, under that lock. The views in a
/// LogRecord are valid for that call only — a sink that keeps anything
/// copies it.
class Logger {
public:
    static Logger& instance() noexcept;
    void addSink(LogSink sink);
    void write(const LogCategory& category, LogLevel level, std::string_view text);
};

[[nodiscard]] LogSink consoleSink();
[[nodiscard]] LogSink fileSink(const std::filesystem::path& path);

}  // namespace uta

/// The only way a part logs. The level test happens before the format
/// call, so a suppressed message costs a load and a branch.
#define UTA_LOG(category, level, ...) ...
```

`core`'s own category is `uta::logCore`, named `"core"`. A program's
startup installs the sinks; nothing else does.

### 4.4 Filesystem — `src/core/FileSystem.h`

```cpp
namespace uta::fs {

/// Where the platform puts per-user files (design rule 15). Linux
/// follows the XDG base-directory variables, falling back to
/// ~/.config, ~/.cache and ~/.local/state, each under "ut-ants".
/// Windows uses %APPDATA%\UT_Ants and %LOCALAPPDATA%\UT_Ants\{cache,logs}.
///
/// A variable is honoured as the user set it, wherever it points; core
/// does not police the platform's own configuration. What core does
/// guarantee is that every FALLBACK is absolute, so an unset variable
/// can never put configuration or logs somewhere relative to the
/// current working directory -- which for a test run is the build tree,
/// and for a launch from a clone would be the repository.
[[nodiscard]] Result<std::filesystem::path> configDirectory();
[[nodiscard]] Result<std::filesystem::path> cacheDirectory();
[[nodiscard]] Result<std::filesystem::path> logDirectory();

[[nodiscard]] Result<std::vector<std::byte>> readFile(const std::filesystem::path& path);

/// Write to a temporary in the destination's own directory, then rename
/// over the destination. The destination is never opened for writing, so
/// a crash leaves the old bytes rather than half the new ones.
[[nodiscard]] Result<void> writeFileAtomically(const std::filesystem::path& path,
                                               std::span<const std::byte> bytes);

/// Join `relative` under `root` and refuse anything that escapes it.
/// This is the trust boundary: `unet` will name files from a remote
/// server, and `ubake` writes under content/. Absolute inputs, empty
/// inputs and paths that resolve outside `root` — through `..`, a
/// symlink, or both — are InvalidArgument.
[[nodiscard]] Result<std::filesystem::path> resolveUnder(
    const std::filesystem::path& root, const std::filesystem::path& relative);

}  // namespace uta::fs
```

`resolveUnder` compares `std::filesystem::weakly_canonical` of the joined
path against `weakly_canonical(root)`, so symlink escapes are caught
along with lexical ones.

### 4.5 The job system — `src/core/Jobs.h`

```cpp
namespace uta {

/// Handle to submitted work. Copyable; outliving its JobSystem is a
/// programming error the destructor's join makes impossible in practice.
class JobHandle { ... };

class JobSystem {
public:
    /// 0 workers means hardware_concurrency() - 1, floored at 1: the
    /// submitting thread is usually doing work too.
    explicit JobSystem(unsigned workerCount = 0);
    ~JobSystem();                                  // drains, then joins
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] JobHandle submit(std::function<void()> job);

    /// Submit `count` jobs and wait for all of them.
    void parallelFor(std::size_t count, const std::function<void(std::size_t)>& body);

    /// Block until `handle`'s job has run. Called from a worker thread,
    /// this runs pending jobs while it waits instead of blocking, so a
    /// job that waits on a job cannot deadlock the pool.
    void wait(const JobHandle& handle);

    [[nodiscard]] unsigned workerCount() const noexcept;
};

}  // namespace uta
```

One `std::deque` of jobs behind one `std::mutex` and one
`std::condition_variable`. A worker loop catches every exception a job
body throws, logs it against `logCore` at `Error`, and carries on — the
worker loop is a thread boundary, which is where `languages/cpp.md`
permits `catch (...)`.

**Completion order is unspecified**, and jobs run on unspecified
threads. So no *result* may depend on the order jobs finish in. That is
a constraint on callers, not a ban on reproducible work: `docs/design.md`
requires baking to use jobs and also requires one map, recipe and baker
version to hash to one bundle on any machine, so `ubake` combines its
job results in submission order and both hold. The simulation is
single-threaded because the design says so, not because this forbids it.

**Who owns the instance.** A program constructs the `JobSystem` and
passes it by reference to the parts that need it. `core` offers no
global accessor: the logger is the one global the design sanctions, and
a second would need its own argument.

**The sanitizer leg.** `CMakeLists.txt` gains a cache variable:

```cmake
set(UTA_SANITIZE "" CACHE STRING "Sanitizer to build with: empty, thread, or address")
```

It applies the matching `-fsanitize=` compile and link options on GCC
and Clang. MSVC has no ThreadSanitizer, so the option refuses `thread`
there with a configure error rather than building something that does
not check what it claims. `scripts/ci.sh` gains one step after `test`:
a second configure-build-test with `-DUTA_SANITIZE=thread`, skipped
with a printed reason on Windows, through the existing `skip` helper.

## 5. Invariants

- **INV-1** — Every failure `core` returns carries a non-empty message
  and a code chosen for that failure — never `Unknown` as a shortcut.
  *Test:* `tests/unit/CoreErrorTest.cpp` drives each documented failure
  path in `uta::fs` and asserts both of the returned error's fields.
  *Breaks when:* a call site reaches for `fail(ErrorCode::Unknown, ...)`
  rather than naming the reason, or passes an empty message. `Error` has
  no default constructor, so those are the two ways in.

- **INV-2** — `withContext` keeps the code and prefixes the message; the
  original message survives in full.
  *Test:* `tests/unit/CoreErrorTest.cpp`.
  *Breaks when:* an implementation replaces the message instead of
  prefixing it, so the innermost reason is lost by the time a caller
  prints it.

- **INV-3** — No `uta::fs` or `uta::Logger` entry point lets an exception
  escape; a documented failure comes back as an `Error`.
  *Test:* `tests/unit/CoreFileSystemTest.cpp` drives each documented
  failure path and asserts a returned error rather than a throw. Every
  case is permission-independent — a missing file, a directory passed
  where a file is expected, an absent parent directory — because a run
  as root defeats a permission fixture, which is the ground INV-7 gives
  for rejecting one.
  *Breaks when:* a `std::filesystem` call is made without the
  `error_code` overload, so it throws `filesystem_error` at the caller.

- **INV-4** — A message below its category's minimum level is not
  formatted: arguments to `UTA_LOG` are not evaluated.
  *Test:* `tests/unit/CoreLogTest.cpp` installs a sink, then logs at
  `Trace` to a category set to `Warning`, passing a function that records
  having been called. The installed sink is what isolates the rule: with
  no sink, an implementation could skip formatting for a different
  reason and pass.
  *Breaks when:* the macro formats first and tests the level afterwards.

- **INV-5** — Concurrent logging produces whole lines: with many threads
  logging at once, every line a sink receives is one complete message
  and no two are spliced.
  *Test:* `tests/unit/CoreLogTest.cpp`.
  *Breaks when:* the sink is called outside the lock, or formatting is
  moved inside a sink that appends to a shared buffer.

- **INV-6** — `resolveUnder` returns `InvalidArgument` for every input
  resolving outside `root`, and for every absolute or empty input; it
  returns a path under `root` for every relative, non-empty input that
  resolves inside it.
  *Test:* `tests/unit/CoreFileSystemTest.cpp`, including `..` escapes,
  absolute paths, an empty path, and a symlink inside `root` pointing
  outside it.
  *Breaks when:* the check is lexical only — `root/link/../..` escapes
  through a symlink that `lexically_normal` resolves as text.

- **INV-7** — A failed `writeFileAtomically` leaves no temporary behind
  and leaves the destination as it was.
  *Test:* `tests/unit/CoreFileSystemTest.cpp`, two cases, both
  permission-independent — a run as root defeats an unwritable-directory
  fixture, and the test then fails to fail. (a) The destination's parent
  is a regular file, so no temporary can be created: assert the error.
  (b) The destination is an existing directory, so the rename fails
  *after* the temporary is written: assert the error, that the parent's
  listing is what it was, and that a pre-existing sibling file still
  holds its original bytes.
  *Breaks when:* the implementation truncates the destination before
  writing, or returns early on a write error without unlinking its
  temporary — case (b) is the one that catches the second.

- **INV-8** — `configDirectory`, `cacheDirectory` and `logDirectory`
  honour the platform's own variables where set — XDG on Linux,
  `%APPDATA%` and `%LOCALAPPDATA%` on Windows — and every fallback they
  return when a variable is unset is an absolute path.
  *Test:* `tests/unit/CoreFileSystemTest.cpp`. The first half is
  per-platform: it sets whichever variables that platform reads and
  asserts the result, so the case is compiled for the platform under
  test rather than asserting XDG behaviour on Windows, where `ci.sh`
  runs `ctest` too. The second half is platform-independent — clear the
  variables and assert each result `is_absolute()`.
  *Breaks when:* a fallback is written relative to the current working
  directory, which for a test run is the build tree and for a launch
  from a clone would be the repository.

- **INV-9** — Every submitted job runs exactly once.
  *Test:* `tests/unit/CoreJobsTest.cpp` submits many jobs each
  incrementing its own counter, waits, and asserts every counter is one.
  *Breaks when:* a worker takes a job without erasing it under the same
  lock, so two workers take the same one.

- **INV-10** — `wait` called from inside a running job returns; it does
  not deadlock.
  *Test:* `tests/unit/CoreJobsTest.cpp` submits a job that itself
  submits a job and waits on it, on a pool of one worker — the case a
  blocking `wait` cannot survive. The pool size is what isolates the
  rule: with spare workers, another worker takes the inner job and a
  blocking `wait` passes. A CTest timeout is what turns the deadlock
  into a failure rather than a hung run.
  *Breaks when:* `wait` blocks on the condition variable unconditionally
  instead of helping to run pending jobs.

- **INV-11** — An exception thrown by a job body does not escape the
  worker: the pool keeps running and later jobs still run.
  *Test:* `tests/unit/CoreJobsTest.cpp` submits a throwing job followed
  by a counting one, and asserts the counter moved.
  *Breaks when:* the worker loop catches by a specific type, or does not
  catch at all, so a job throwing anything else calls `std::terminate`.

- **INV-12** — `~JobSystem` returns only after every submitted job has
  run and every worker has been joined.
  *Test:* `tests/unit/CoreJobsTest.cpp` preallocates a vector outside the
  pool and has each job write its own index, lets the pool leave scope
  without waiting, and asserts afterwards that every slot was written.
  Its own slot, not a shared `push_back`: concurrent appends would be a
  race in the fixture, and INV-13 would then report the test rather than
  the pool.
  *Breaks when:* workers are detached, or the stop flag is set without
  waking threads blocked on the condition variable — either way the
  vector comes back short, or the process crashes writing to it.

- **INV-13** — The job tests are clean under ThreadSanitizer.
  *Test:* `scripts/ci.sh`'s sanitizer step; locally,
  `cmake -DUTA_SANITIZE=thread` then `ctest -L unit`.
  *Breaks when:* a counter is read outside the lock, or `JobHandle`'s
  completion flag is a plain `bool`.

- **INV-14** — `uta_core` links nothing beyond the standard library
  (design rule 1).
  *Test:* `src/core/CMakeLists.txt` asserts at configure time that the
  target's `LINK_LIBRARIES` property holds nothing but
  `Threads::Threads`. That one entry is the standard library's own
  threading support rather than an exception to the rule; an empty
  assertion would fail the moment `std::thread` is linked on GCC.
  *Breaks when:* somebody adds a convenient third-party dependency to
  `core`, which is the rule every later part inherits its independence
  from.

## 6. Failure modes

- **The platform's variables give nothing to fall back to** — the XDG
  variables and `HOME` both unset on Linux, `%APPDATA%` or
  `%LOCALAPPDATA%` unset on Windows. All three directory functions
  return `NotFound` rather than guessing at the current directory. A
  program that cannot find where to write says so.
- **`std::filesystem::rename` does not replace an existing destination.**
  The standard requires replacement for a non-directory destination, and
  the MSVC standard library implements it; if a platform were to differ,
  `writeFileAtomically` unlinks its temporary and returns the error,
  leaving the destination as it was rather than reporting a success it
  did not achieve. Unlinking is INV-7's contract on every failure path
  the function can reach; the bullet below is the one it cannot.
- **A crash between the temporary write and the rename.** The
  destination keeps its old bytes; a temporary is left in its directory.
  This is the trade the design accepts — no test can reach it, and §10
  records that.
- **`resolveUnder` is checked, then the filesystem changes.** A symlink
  swapped between the check and the open defeats it. Callers that need
  more open the file and verify afterwards; `core` does not pretend to
  close this.
- **Every worker is busy and `wait` is called from a non-worker thread.**
  It blocks on the condition variable, which is correct — the calling
  thread has no queue to help with, and the pool will get there.
- **`hardware_concurrency()` returns 0.** The floor of one worker
  applies, so the pool still runs jobs rather than dropping them.
- **A sink throws.** `Logger::write` catches and drops it. A logger that
  can propagate a failure turns every log line into a branch.

## 7. Tests

New test files join the existing `uta_unit_tests` executable in
`tests/CMakeLists.txt`, which already discovers with
`catch_discover_tests(uta_unit_tests PROPERTIES LABELS "unit;fast")`
alongside `CompactIndexTest.cpp` and `PackageBuilderTest.cpp`:

| File | Locks |
|---|---|
| `tests/unit/CoreErrorTest.cpp` | INV-1, INV-2 |
| `tests/unit/CoreLogTest.cpp` | INV-4, INV-5 |
| `tests/unit/CoreFileSystemTest.cpp` | INV-3, INV-6, INV-7, INV-8 |
| `tests/unit/CoreJobsTest.cpp` | INV-9, INV-10, INV-11, INV-12, INV-13 |

INV-14 is deliberately absent from that table: its surface is
`src/core/CMakeLists.txt`, not a test file. §10 carries it.

They exercise `uta_core` only, so `S7` holds unchanged: no Unreal
Tournament install is touched and the second, real-asset tier is not
involved.

Each test is written before the code it locks and seen to fail —
against a missing symbol first, then against the failure it names, per
`languages/cpp.md` § Tests, rebuilding on both runs.

INV-13 is the exception in kind: it is a whole-suite property rather
than a case, and it is proven by the sanitizer leg reporting cleanly on
a run that also proves it can report — a deliberately racy throwaway
test is used once to confirm the leg fails, and is not committed.

## 8. Alternatives considered (and rejected)

- **Work-stealing deques (Chase-Lev) for the job queue.** The design
  most engines use, and faster under load. Rejected for now: it is
  lock-free atomic code many times the size of the shared queue, its
  bugs are the hardest kind to reproduce, and nothing has measured
  contention here. `JobSystem`'s public shape hides the queue, so this
  is a replacement rather than a rewrite when a measurement asks for it.
- **A source location on every `Error`.** Rejected: it makes every
  returned value bigger on a path that is already the slow one, and the
  logger records where a failure was reported.
- **A chain of causes.** Rejected: `withContext` gives the same reading
  experience — outermost context first, innermost reason last — without
  a heap node per link.
- **An asynchronous logger.** Rejected: the lines immediately before a
  crash are the ones worth having, and those are the ones a queue loses.
- **A compile-time minimum log level.** Rejected as unmeasured: the
  runtime test already skips formatting, and this would only remove a
  predictable branch. Reconsider if profiling names it.
- **`spdlog` or `fmt` as a dependency.** Rejected by design rule 1:
  `core` depends on nothing beyond the standard library, and C++23 gives
  `std::format`.
- **A `Result` that is this project's own type rather than
  `std::expected`.** Rejected: `CMakeLists.txt` pins C++23 specifically
  for `std::expected`, so writing an equivalent would waste the pin.

## 9. Out of scope

- **`core`'s types, math and memory surfaces**, which
  `docs/design.md` § The parts also lists under `core`. They land when a
  caller needs them. No roadmap item carries them yet.
- **The numeric contract** — floating-point contraction and fast-math
  off, no platform maths library — which `docs/design.md` requires of
  `uworld` and `ubake`. It binds the simulation and the baker, neither of
  which exists. No roadmap item carries it yet; both gaps are named in
  this item's handoff.
- **The forbidden-module-edge and link-closure checks** of design rules
  2 and 4. INV-14 covers `core`'s own edge; the rest need parts that do
  not exist.
- **The third quarantine-guard check**, which needs the `.utab` origin
  field — tracked by UTA-0013.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/CoreErrorTest.cpp`, a Catch2 unit test |
| INV-2 | `tests/unit/CoreErrorTest.cpp`, a Catch2 unit test |
| INV-3 | **Partial:** `tests/unit/CoreFileSystemTest.cpp` drives the documented failure paths; an undocumented one added later is caught by nothing |
| INV-4 | `tests/unit/CoreLogTest.cpp`, a Catch2 unit test |
| INV-5 | **Partial:** `tests/unit/CoreLogTest.cpp` plus INV-13's ThreadSanitizer leg; a splice needing a rarer interleaving than either produces is caught by nothing |
| INV-6 | `tests/unit/CoreFileSystemTest.cpp`, a Catch2 unit test |
| INV-7 | `tests/unit/CoreFileSystemTest.cpp`, a Catch2 unit test |
| INV-8 | `tests/unit/CoreFileSystemTest.cpp`, a Catch2 unit test |
| INV-9 | `tests/unit/CoreJobsTest.cpp`, a Catch2 unit test |
| INV-10 | `tests/unit/CoreJobsTest.cpp`, a Catch2 unit test |
| INV-11 | `tests/unit/CoreJobsTest.cpp`, a Catch2 unit test |
| INV-12 | `tests/unit/CoreJobsTest.cpp`, a Catch2 unit test |
| INV-13 | **Partial:** `scripts/ci.sh`'s ThreadSanitizer step, on Linux only; MSVC has no ThreadSanitizer, so the Windows leg checks nothing here |
| INV-14 | `src/core/CMakeLists.txt`, a configure-time property assertion |
| §4.3 "no `printf` or `std::cout` outside a program's startup" | **nothing** — no check greps for them; `core` ships no program, so the rule has no in-repo violator to catch yet |
| §4.5 "no result may depend on the order jobs finish in" | **nothing** — the callers that could breach it (`ubake`, `uworld`) do not exist yet, and no check can see an ordering dependence from `core`'s side |
| §4.5 "a program owns the `JobSystem` and passes it by reference" | **nothing** — `core` offering no accessor is what makes a global inconvenient rather than impossible; the first part to want one would have to add it, and nothing stops that |
| §6 "a crash between the temporary write and the rename" | **nothing** — no test can crash a process mid-call; the design accepts the leftover temporary |

## 11. Cross-doc impact

- `CHANGELOG.md` — an `Added` entry for `core` under `[Unreleased]`.
- `CLAUDE.md` — the position lines, and § Build and test, which today
  reads "(Filled once the stack exists.)".
- `README.md` — only if its build instructions change; the CMake
  invocation does not.
- `docs/design.md` — no change. This spec implements it. The one place
  the draft did contradict it — a job-system clause that would have kept
  the baker off jobs, which § *What every part does the same way*
  requires it to use — was a defect in this document and was fixed here
  rather than there.

## 12. Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-04 | 3, cold — genre pinned `spec` | 2 | 9 | 2 | 1 | **Fourteen findings, twelve verified and fixed, two dismissed as immaterial.** Nine of the twelve were Q2 — the draft contradicting itself or the design — which is the shape a first cold read of a four-surface spec produces. **The most consequential:** §4.5 said nothing whose result must be reproducible may use the job system, while `docs/design.md` requires baking to use jobs AND requires one map, recipe and baker version to hash to one bundle on any machine. An implementer of `ubake` would have kept it off jobs. The real invariant is that no *result* may depend on completion order; the baker combines in submission order and both hold. **Two lanes independently found the same broken fixture (Q4):** INV-7's test was to write to a destination whose parent is a regular file, then compare the bytes of an existing destination and list its directory — the fixture excludes its own preconditions. It was collateral from this session's own earlier fix, which swapped the fixture and left the assertions describing the old one. Now two cases, both permission-independent. **INV-14 would have failed the build it was written to protect:** it asserted `LINK_LIBRARIES` empty, which forbids `Threads::Threads` — how CMake spells the standard library's threading support, and exactly what `std::thread` needs on GCC. **INV-8 asserted XDG behaviour with no platform qualifier**, and `scripts/ci.sh` runs `ctest` on Windows, so a correct implementation would have gone red there. Both Q3s were inventions other code binds to: who owns the one `JobSystem`, and whether a `LogRecord`'s views survive the sink call. **Dismissed as true but immaterial:** the casing sentence attributes the trailing-underscore convention to `languages/cpp.md`, which does not state it, and §3 reads as though UTA-0041 shipped the root `CMakeLists.txt`. Neither changes a line anyone builds. **Lane-side facts confirmed, not taken on trust:** all three lanes re-read `CMakeLists.txt`, `tests/CMakeLists.txt`, `scripts/ci.sh` and the cited `docs/design.md` bullets; two opened `cpp.md` to settle citations the packet had not windowed. |

## 13. Resource cost

`uta_core` is a new build target: a static library, with no runtime
state of its own beyond the two below.

- **The logger** holds its sinks and nothing else. A line costs one
  `LogRecord` and one temporary string, both in the calling thread, both
  gone when the call returns; nothing accumulates. A file sink's growth
  is the log file, which is a program's concern rather than the
  logger's, and no program exists yet to set a policy.
- **The job system** holds one `std::deque` of pending jobs. It is
  unbounded by design: `submit` never blocks and never drops work, and
  the cap is the caller's own submission rate. The named consequence is
  that a caller submitting without waiting can grow it without limit —
  which is a bug in that caller, and `parallelFor` (the shape every
  early caller will use) submits a bounded batch and waits.
- **Worker threads:** `hardware_concurrency() - 1`, floored at one.
- **No new external dependency.** Design rule 1 forbids one, and INV-14
  checks it.
