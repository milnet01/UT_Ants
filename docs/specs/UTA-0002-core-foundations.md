# UTA-0002 — build `core`: the error type, logging, filesystem and job system

**Status:** accepted (2026-09-04).
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
/// The short, stable name of a code -- for logs and test failure output,
/// never for a player. Never empty.
[[nodiscard]] std::string_view errorCodeName(ErrorCode code) noexcept;

class Error {
public:
    Error(ErrorCode code, std::string message);

    [[nodiscard]] ErrorCode code() const noexcept;

    /// Ref-qualified, with the rvalue overload deleted. The view aliases the
    /// message, and withContext returns a prvalue, so the chained shape below
    /// would otherwise dangle -- and ASan does not catch it, because a short
    /// message lives inside the object.
    [[nodiscard]] std::string_view message() const& noexcept;
    std::string_view message() const&& = delete;

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

Both expand to a uniquely named temporary, a `has_value()` test returning
`std::unexpected` on failure, and — for `UTA_TRY` — a move out of it. No
statement expressions, because MSVC has none.

The uniquifier is `__COUNTER__`, not `__LINE__`. `UTA_TRY` declares in the
enclosing scope, so two on one physical line would redefine one name, and
MSVC's edit-and-continue build does not expand `__LINE__` to a pasteable
token — which matters precisely because MSVC is why this shape exists.

### 4.3 Logging — `src/core/Log.h`

```cpp
namespace uta {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warning, Error, Off };

/// One per part, declared once at that part's own scope.
class LogCategory {
public:
    /// const char*, not string_view: the name is borrowed and read on every
    /// line, and a string_view parameter would silently accept a std::string
    /// temporary and then print freed memory.
    constexpr explicit LogCategory(const char* name, LogLevel minimum = LogLevel::Info);

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

    /// Drops every sink. For tests, and for a program tearing down its
    /// own logging deliberately.
    void clearSinks();

    /// noexcept, and it reports no failure: a logger that could fail
    /// would turn every log line into a branch. A throwing sink is
    /// caught here and the sinks after it still run.
    void write(const LogCategory& category, LogLevel level,
               std::string_view text) noexcept;
};

[[nodiscard]] LogSink consoleSink();

/// Opens `path` for append, once, and says why when it cannot.
[[nodiscard]] Result<LogSink> fileSink(const std::filesystem::path& path);

}  // namespace uta

/// The only way a part logs. The level test happens before the format
/// call, so a suppressed message costs a load and a branch.
#define UTA_LOG(category, level, ...) ...
```

`core`'s own category is `uta::logCore`, named `"core"`, and is `constinit`
so it takes no part in static-initialisation order.

**`UTA_LOG` catches its own throw.** `std::format` runs in the caller's
expression, outside `write`'s `noexcept`, so logging from any `noexcept`
function would otherwise be a `std::terminate` waiting for an allocation
failure — and every such caller would have to rediscover it.

**`fileSink` reports its failure; `write` does not, and the difference is
not an inconsistency.** `write` is called from everywhere, including
`noexcept` functions, and has no channel of its own — a logger that
reported failure would put a branch on every log line. `fileSink` is a
factory called once at startup by a caller that *does* have a channel,
and it sits on a module boundary, where `docs/design.md` requires
`std::expected`. Its failure path is ordinary rather than exotic: on a
first run the log directory does not exist. Returning a sink that
silently does nothing leaves a program running with logging off and no
way to find out.

**`Logger::instance()` leaks deliberately.** A function-local static is
destroyed before every static constructed earlier than the first call, so a
static whose destructor logs would touch a destroyed mutex.

**A sink is invoked under the lock, and a log call made from inside a sink
is discarded — `Logger::write` is not re-entrant.** A thread-local guard is
what discards it. Without one the mutex, which is not recursive, would
deadlock the process, and nothing would detect that: ThreadSanitizer does
not do deadlock detection. So a sink author may log without hanging the
program, and their line is dropped rather than written.

**`fileSink` maps `errno` by §4.4's rule**, the same one the other `fopen`
in the project uses — so a missing log directory is `NotFound` rather than
a general failure. The startup routine that calls it branches on the code
to decide whether to create the directory and retry or to give up, so a
code left to the implementation is a contract that caller binds to
blind.

**Both shipped sinks escape control bytes** (CWE-117). Message text carries
filesystem paths now and package bytes once `upkg` lands.

A program's startup installs the sinks; nothing else does.

### 4.4 Filesystem — `src/core/FileSystem.h`

```cpp
namespace uta::fs {

/// Where the platform puts per-user files (design rule 15). Linux
/// follows the XDG base-directory variables, falling back to
/// ~/.config, ~/.cache and ~/.local/state -- and appends "ut-ants" to
/// whichever base it chose, the variable's and the fallback's alike.
/// Windows uses %APPDATA%\UT_Ants and %LOCALAPPDATA%\UT_Ants\{cache,logs}.
///
/// A variable is honoured as the user set it, provided it is ABSOLUTE;
/// core does not otherwise police the platform's own configuration. One
/// that is set but relative is treated as unset, so the platform's
/// fallback applies -- or NotFound where it has none. What core does
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

/// Join `relative` under `root` and refuse anything that escapes it,
/// and anything whose NAME is unsafe on either platform.
/// This is the trust boundary: `unet` will name files from a remote
/// server, and `ubake` writes under content/. Absolute inputs, empty
/// inputs and paths that resolve outside `root` — through `..`, a
/// symlink, or both — are InvalidArgument, as is any component barred
/// by the lexical pass below.
[[nodiscard]] Result<std::filesystem::path> resolveUnder(
    const std::filesystem::path& root, const std::filesystem::path& relative);

}  // namespace uta::fs
```

`resolveUnder` compares `std::filesystem::weakly_canonical` of the joined
path against `weakly_canonical(root)`, **element-wise rather than by string
prefix**, so `/data-other` is not accepted as being under `/data`.

Three things implementation proved that the draft did not say:

- **A dangling symlink needs its own check.** `weakly_canonical` resolves
  only the leading elements that exist, and `status` follows links — so a
  link to a missing target is appended unresolved and passes containment
  while still pointing outside `root`. Rejected explicitly.
- **`readFile` caps the size it will allocate.** The size is
  attacker-influenced wherever the path came through `resolveUnder`, and an
  unguarded allocation throws `bad_alloc` straight out of `uta::fs` against
  INV-3. **The cap is 4 GiB**, a named constant in `FileSystem.cpp`; over
  it is `InvalidArgument`, and a failed allocation is `OutOfMemory`. The
  figure is stated rather than left to the implementation because `upkg`
  (UTA-0003) reads every package through `readFile`, and its author has
  to know which sizes are supported. `scripts/package-census.py` reports
  the largest package in any given install; across the content this
  project is developed against, that stays well under a gigabyte.
- **Files are opened by their native path**, never `path::string()`. That is
  UTF-8 on Windows while `fopen` decodes the ANSI code page, so every
  non-ASCII path — `%APPDATA%` for a non-ASCII user name among them — would
  fail to open.

**A lexical pass over `relative`'s components runs first, before any
filesystem call, and it is enforced on every platform rather than only on
Windows.** Three shapes are safe on Linux and are not names at all on
Windows, and `resolveUnder` is where a remote server's chosen filename
arrives:

- **Reserved device names** — `CON`, `NUL`, `AUX`, `PRN`, `COM1`–`COM9`,
  `LPT1`–`LPT9` — resolve to devices in *any* directory on Win32, and do
  so with an extension too, so `COM1.txt` is the same device. A remote
  file named `COM1` passes containment and then reads from a serial
  port, which can block with no timeout. Matched case-insensitively, on
  the component's text up to its first `.`.
- **A trailing dot or space on any component.** Win32 strips them, so
  `a.` and `a` are one file after the check has passed on two names.
- **A colon anywhere in a component.** On Win32 `a.txt:s` writes an
  alternate data stream, which the quarantine guard's extension check
  cannot see, and `C:foo` is drive-relative rather than absolute — so
  `is_absolute()` does not catch it.

**Enforcing all three everywhere is a deliberate choice, and it is
stricter than the platform requires.** Two reasons. A rule that holds on
one platform and not the other means a Linux server and a Windows client
disagree about which content is safe, and `unet` moves content between
exactly those. And a lexical rule is testable on every leg of the matrix,
where a Windows-only rule is checked by one leg and by no local run at
all. The cost is that a legitimate Linux filename containing a colon or a
trailing dot is refused; no Unreal Tournament content uses one.

**The containment comparison must not depend on whether a `root` spelled
with a trailing separator yields an empty final element.** Measured on
libstdc++ 16.2: `weakly_canonical` strips it, so containment already
holds there — it is `lexically_normal` that preserves it, and this
function does not call that. **MSVC's standard library was then measured
by the matrix and behaves the same**, once INV-6's case existed to ask
it. libc++ remains unmeasured: both Linux legs use libstdc++. So the
property is locked by INV-6's test case on every leg rather than argued
from whichever implementation happens to be at hand. Refusing such a root fails closed, so it is availability
rather than a hole — but refusing it is **not** an acceptable outcome on
any leg, which is what INV-6 requires. Where a standard library preserves
the empty element, `resolveUnder` strips it from the canonical root
before comparing.

An `fopen` failure maps `errno` to a code chosen for it, rather than one code
standing for every cause.

`writeFileAtomically`'s temporary carries the process id and is created
exclusively, because two processes writing one destination is a designed-in
shape (design rule 16) and a shared name would let them interleave into one
file and each rename it into place. The bytes are flushed to the device
before the rename, so a power loss cannot commit the rename ahead of them.

An environment variable that is **relative** is ignored, which the XDG
specification requires and INV-8's absolute-fallback promise needs.

### 4.5 The job system — `src/core/Jobs.h`

```cpp
namespace uta {

/// Handle to submitted work. Copyable, and safe to outlive its
/// JobSystem: the completion state is shared-owned by the handle, and
/// the JobSystem pointer it carries is compared, never dereferenced.
class JobHandle {
public:
    [[nodiscard]] bool done() const noexcept;

    /// Whether the body threw. Meaningful once done() is true; false
    /// for a default-constructed handle, which ran nothing.
    [[nodiscard]] bool failed() const noexcept;
};

class JobSystem {
public:
    /// 0 means one fewer than hardware_concurrency(), because the
    /// submitting thread is usually working too -- and one worker where
    /// that reports 0 or 1. NOT `hardware_concurrency() - 1` clamped to
    /// 1: the return is unsigned, so 0 - 1 is UINT_MAX and a std::max
    /// against it changes nothing. Subtract only after the 0-or-1 test.
    explicit JobSystem(unsigned workerCount = 0);
    ~JobSystem();                                  // drains, then joins
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] JobHandle submit(std::function<void()> job);

    /// Submit `count` jobs, wait for all of them, and return how many
    /// bodies threw -- 0 when every one succeeded. nodiscard, because a
    /// count nobody reads is the defect this return exists to close.
    [[nodiscard]] std::size_t parallelFor(std::size_t count,
                                          const std::function<void(std::size_t)>& body);

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

**Containing a failure is not the same as hiding it, and the handle
records which happened.** Catching keeps one bad job from calling
`std::terminate`; it must not make a failed job indistinguishable from a
successful one. `JobHandle::failed()` is set before `done()`, under the
same lock, so a waiter that sees completion sees the outcome with it.
`parallelFor` returns how many bodies threw. The reason this matters
here rather than in general is `ubake`: `docs/design.md` requires baking
to use jobs, and ADR-0002 requires one map, recipe and baker version to
hash to one bundle on any machine — so a batch in which every body threw,
reported as complete, is a wrong bundle presented as a good one. A log
line is not a reporting channel: nothing downstream reads it.

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
set(UTA_SANITIZE "" CACHE STRING "Sanitizer to build with: empty or thread")
```

`thread` and nothing else. AddressSanitizer is not offered here because
MSVC spells it `/fsanitize=address` and takes no link flag, so a second
value would need a second spelling for a build nothing in this item
runs; an item that wants it can add it with its own contract.

The option applies `-fsanitize=thread` as a compile and link option **at
the top level**, so `uta_core`, the test executable and the Catch2 the
build fetches are all instrumented. Instrumenting `uta_core` alone would
produce a leg that reports nothing about the job tests, which is what
INV-13 is actually about. MSVC has no ThreadSanitizer, so the option
refuses `thread` there with a configure error rather than building
something that does not check what it claims. `scripts/ci.sh` gains one
step after `test`:
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
  escape. The two report differently and the difference is deliberate: a
  `uta::fs` failure comes back as an `Error`, while `Logger`'s entry
  points return `void`, so a throwing sink is caught and dropped rather
  than reported — §6 owns why.
  *Test:* two files. `tests/unit/CoreFileSystemTest.cpp` drives each
  documented `uta::fs` failure path and asserts a returned error rather
  than a throw; every case is permission-independent — a missing file, a
  directory passed where a file is expected, an absent parent directory
  — because a run as root defeats a permission fixture, which is the
  ground INV-7 gives for rejecting one.
  `tests/unit/CoreLogTest.cpp` installs a throwing sink followed by a
  recording one, and asserts `write` returns normally and the second
  sink still received the record.
  *Breaks when:* a `std::filesystem` call is made without the
  `error_code` overload, so it throws `filesystem_error` at the caller;
  or `Logger::write` calls its sinks outside a `try`, so one bad sink
  takes down every call site and stops the sinks after it.

- **INV-4** — A message below its category's minimum level is not
  formatted: arguments to `UTA_LOG` are not evaluated.
  *Test:* `tests/unit/CoreLogTest.cpp` installs a sink, then logs at
  `Trace` to a category set to `Warning`, passing a function that records
  having been called. The installed sink is what isolates the rule: with
  no sink, an implementation could skip formatting for a different
  reason and pass.
  *Breaks when:* the macro formats first and tests the level afterwards.

- **INV-5** — `Logger::write` invokes sinks serially: no two sink calls
  overlap, whatever the calling threads do. Stated as overlap rather
  than as "no line is spliced", because a sink is handed one whole
  `LogRecord` per call and so sees intact text either way — a test
  asserting intact text passes against a `write` that never locks, and
  would be a green check on an unimplemented property.
  *Test:* `tests/unit/CoreLogTest.cpp`. Many threads log at once; the
  sink increments a counter on entry and decrements on exit, and asserts
  the counter is never above one.
  *Breaks when:* the sink is called outside the lock — which is exactly
  what the counter sees and intact text does not.

- **INV-6** — `resolveUnder` returns `InvalidArgument` for every input
  resolving outside `root`, and for every absolute or empty input; it
  returns a path under `root` for every relative, non-empty input that
  resolves inside it, passes INV-15's lexical pass, and is not an
  unresolved symlink. A `root` spelled with a trailing separator is
  accepted, on every standard library.
  *Test:* `tests/unit/CoreFileSystemTest.cpp`, including `..` escapes,
  absolute paths, an empty path, a symlink inside `root` pointing
  outside it, and a `root` written with a trailing separator.
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
  `%APPDATA%` and `%LOCALAPPDATA%` on Windows. A variable that is set but
  **relative** is treated as unset. Where a variable is unset
  and the platform has a fallback, that fallback is an absolute path;
  where it has none, the function returns `NotFound`.
  *Test:* `tests/unit/CoreFileSystemTest.cpp`, three cases. (a) Both
  platforms: set whichever variables that platform reads and assert the
  result — compiled for the platform under test rather than asserting
  XDG behaviour on Windows, where `ci.sh` runs `ctest` too. (b) Linux
  only, because it is the only platform with a fallback: clear the XDG
  variables with `HOME` set, and assert each result `is_absolute()`.
  (c) Both platforms: clear everything the function reads — on Linux
  `HOME` as well — and assert `NotFound`, which is §6's rule and would
  otherwise be locked by nothing. (d) Both platforms: set the platform's
  own variable to a **relative** path and assert the result is the
  absolute fallback, or `NotFound` where the platform has none — without
  it the relative rule above is asserted by nothing and deleting it
  leaves the suite green.
  *Breaks when:* a fallback is written relative to the current working
  directory, which for a test run is the build tree and for a launch
  from a clone would be the repository; or a relative variable is
  honoured rather than treated as unset, which puts a user's config or
  logs wherever the process happened to start; or a Windows
  implementation invents a hardcoded absolute path to satisfy (b), which
  §6 forbids.

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
  blocking `wait` passes. The `TIMEOUT` §7 adds to
  `catch_discover_tests` is what turns the deadlock into a failure
  rather than a hung run; without it this invariant cannot be falsified,
  only waited on.
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

- **INV-15** — `resolveUnder` returns `InvalidArgument` for any relative
  path with a component that is a Win32 reserved device name (with or
  without an extension, any case), ends in a dot or a space, or contains
  a colon — on every platform, and before any filesystem call.
  *Test:* `tests/unit/CoreFileSystemTest.cpp`, one case per shape,
  including `COM1.txt` and a name differing from a safe one only by a
  trailing dot — plus one case that distinguishes the ORDER:
  `COM1/../safe.unr`, which must return `InvalidArgument`.
  Canonicalisation erases the barred component before a late check could
  see it — measured on libstdc++ 16.2, it resolves to `<root>/safe.unr`
  — so a filesystem-first implementation accepts it and a lexical-first
  one refuses. Without that case every other input fails either way and
  the ordering half is unfalsifiable. It needs no error condition and no
  special filesystem, so it runs on all three legs, which is what the
  rule being platform-independent buys.
  *Breaks when:* the check is guarded by `#ifdef _WIN32`, so the Linux
  and Clang legs assert nothing and the rule is proven by one leg; or it
  is applied to the joined path after `weakly_canonical`, which has
  already resolved the name it was supposed to inspect.

- **INV-16** — A job whose body throws is reported as failed:
  `JobHandle::failed()` is true once `done()` is, and `parallelFor`
  returns the number of bodies that threw.
  *Test:* `tests/unit/CoreJobsTest.cpp` waits on a handle whose body
  throws and asserts `done()` and `failed()` are both true; and runs a
  `parallelFor` in which a known subset of bodies throw, asserting the
  returned count. Neither distinguishes the publication ORDER, so a
  third case spins on `done()` from another thread and reads `failed()`
  the moment it turns true, over many iterations. That case is
  probabilistic; INV-13's sanitizer leg is what makes the order visible
  rather than merely likely, and §10 marks INV-16 partial for it.
  *Breaks when:* the flag is stored after `done` is published, so a
  waiter woken by `done` can read the outcome before it is written — the
  failure is then reported correctly under a debugger and not under
  load.

- **INV-17** — `fileSink` returns an `Error` naming the reason when it
  cannot open its file, with the code chosen for that `errno` by §4.4's
  mapping, and a working sink when it can.
  *Test:* `tests/unit/CoreLogTest.cpp` asks for a sink under a directory
  that does not exist and asserts `NotFound` and a non-empty message,
  then asks for one under a temporary directory and asserts a line
  written through it arrives.
  *Breaks when:* the failure is folded back into a do-nothing sink — the
  call then succeeds, the program runs with logging off, and the only
  evidence is the absence of output.

## 6. Failure modes

- **The variables one directory function reads give nothing to fall back
  to.** On Linux that is its XDG variable and `HOME` both unset; on
  Windows it is `%APPDATA%` for `configDirectory`, or `%LOCALAPPDATA%`
  for `cacheDirectory` and `logDirectory` — and Windows has no fallback
  beyond those. **That function** returns `NotFound` rather than guessing
  at the current directory; the other two are unaffected, since they read
  different variables. A program that cannot find where to write says so.
- **A platform whose `rename` refuses to replace an existing
  destination.**
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
alongside `CompactIndexTest.cpp` and `PackageBuilderTest.cpp`. That call
gains `TIMEOUT 30`: INV-10's breaking case is a deadlock, and without a
bound it hangs the run instead of failing it, so the invariant could
never be falsified. The executable also gains `uta_core` as a link
library.

| File | Locks |
|---|---|
| `tests/unit/CoreErrorTest.cpp` | INV-1, INV-2 |
| `tests/unit/CoreLogTest.cpp` | INV-3 (its `Logger` half), INV-4, INV-5, INV-17 |
| `tests/unit/CoreFileSystemTest.cpp` | INV-3 (its `uta::fs` half), INV-6, INV-7, INV-8, INV-15 |
| `tests/unit/CoreJobsTest.cpp` | INV-9, INV-10, INV-11, INV-12, INV-13, INV-16 |

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
| INV-3 | **Partial:** `tests/unit/CoreFileSystemTest.cpp` and `tests/unit/CoreLogTest.cpp`, Catch2 unit tests, cover the `uta::fs` and `Logger` halves respectively; an undocumented failure path added later is caught by nothing |
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
| INV-15 | `tests/unit/CoreFileSystemTest.cpp`, a Catch2 unit test, on all three legs |
| INV-16 | **Partial:** `tests/unit/CoreJobsTest.cpp` locks the outcome and the `parallelFor` count; the publication ORDER is only probabilistically covered there, and is caught properly by INV-13's ThreadSanitizer leg, which is Linux-only |
| INV-17 | `tests/unit/CoreLogTest.cpp`, a Catch2 unit test |
| §4.3 "a log call made from inside a sink is discarded" | `tests/unit/CoreLogTest.cpp`, "a sink that logs does not deadlock the logger", which asserts the sink is entered once; the `TIMEOUT 30` is what turns the guard's removal into a failure rather than a hang |
| §4.4 "the `readFile` cap is 4 GiB" | **nothing** — no fixture asserts the boundary, because a file over the cap is impractical to create in the suite; the constant can be changed without breaking a test, and `upkg` binds to it |
| §4.4 "a name barred on Win32 but legal here" | **Partial:** INV-15 covers the three shapes named; a Win32 naming rule nobody has thought of is caught by nothing, and no Windows-native path test exists — the rule is lexical precisely so this gap does not depend on the platform |
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
| 2 | 2026-09-04 | 3, cold — identical brief, packet rebuilt from disk | 0 | 5 | 2 | 2 | **Nine verified, nine fixed, none dismissed. Cap reached (2 for a spec); the run files its tail — which is empty — and exits. Not one Q1.** **A calm cap, not a violent one:** three of the nine landed on text loop 1 wrote, so the majority were defects the draft always held and the cap simply ran out of loops for. (The second share is degenerate here and worth saying so: the gate was armed by the document being NEW, so every finding falls inside the armed span by construction.) **The finding of the run came from one lane and was proven by running rather than argued:** §4.5 prescribed `hardware_concurrency() - 1` floored at 1, and the return is `unsigned` — compiled and run, a report of 0 gives 4,294,967,295 workers, and `std::max` against `UINT_MAX` changes nothing, so §6's promise of one worker was false and the constructor would have tried to spawn four billion threads. The subtraction now comes after the 0-or-1 test. **All three lanes independently found two others.** §6's rename bullet was headed with the negative stated as fact — an implementer taking the heading at face value writes `remove()` before `rename()`, which destroys the crash-safety property §4.4 sells and INV-7 locks. And INV-8's second half was called platform-independent while Windows has no fallback at all, so on the Windows leg — which runs `ctest`, and went green for the first time today — a correct implementation fails, and the likelier repair is to invent a hardcoded path §6 forbids. **Two Q4s, both clauses that could not fail:** INV-5 asserted no line is spliced, but a sink is handed one whole `LogRecord` per call and sees intact text whether or not `write` locks — it now asserts sink calls do not overlap, which is what the lock actually buys; and INV-10 cited a CTest timeout nothing in the item set, so its deadlock would have hung the run rather than failed it. **Both Q3s were the orchestrator's, from lane open questions:** the sanitizer offered `address` while describing only the GCC/Clang spelling (dropped rather than specified — nothing here runs it), and never said which targets it instruments, which decides whether the leg checks the job tests at all or nothing. **Packet defect, mine:** its header still read 574 lines against the brief's 623; all three lanes raised it and none was misled, since the placeholder sits below every citation. **Resolved clean, not in the tally:** `ci.sh`'s green step does print the skipped list, so a skipped sanitizer leg is visible in the run output. |
| impl | 2026-09-04 | none dispatched — implementation, not a review loop | n/a | n/a | n/a | n/a | **Fold-back from building it, and from the `review-code` sweep that followed. NO reviewer was dispatched for this row; it records what the code proved.** The gate had converged at its cap, and every correction below is a clause implementation falsified rather than a change of direction — so under `CLAUDE.md` rule 14 this records what was built and does not re-arm the gate. **What the contract got wrong. § 4.2's `message()` returned a view into the object while `withContext` returns a prvalue, so the chained shape the spec's own example teaches read freed memory** — measured: it printed nothing, and ASan missed it because a short message lives inside the object rather than on the heap. Ref-qualified now, with the rvalue overload deleted. The macros' `__LINE__` uniquifier could not survive two `UTA_TRY` on one line, nor MSVC's edit-and-continue build — which is the compiler the whole macro shape exists for. § 4.3's `UTA_LOG` ran `std::format` in the caller's expression, outside `write`'s `noexcept`, so logging from any `noexcept` function was a `std::terminate` waiting for an allocation failure; `Jobs.cpp` had already had to hand-wrap it once, which is the evidence the fix belonged in the macro. `LogCategory` took a `string_view` name it borrows forever. § 4.4's `resolveUnder` — the project's trust boundary — accepted a DANGLING symlink, because `weakly_canonical` resolves only what exists and `status` follows links; `readFile` allocated from an attacker-influenced size with no cap; files were opened by `path::string()`, which is UTF-8 on Windows while `fopen` decodes the ANSI code page, so `%APPDATA%` for a non-ASCII user name would never have opened; and the atomic write's temporary carried no process id, so two processes — which design rule 16 makes a designed-in shape — could interleave into one file and each rename it into place. **Three findings were queued rather than folded in**, because each needs a contract decision or a platform this machine is not: UTA-0046 (Windows reserved device names and trailing-separator roots), UTA-0047 (a thrown job is indistinguishable from a successful one, which for `ubake` means a wrong bundle reported as good), UTA-0048 (`fileSink` cannot report why it failed to open, against the design's `std::expected` rule at a module boundary). **Not folded in and deliberately so:** the spec's INV wording is unchanged — every fix above satisfies the invariants as written, and none of them needed the contract loosened. |
| 3 | 2026-09-04 | 3, cold — genre pinned `spec`; gate armed by the UTA-0046/0047/0048 amendment | 2 | 2 | 2 | 1 | **Seven verified, seven fixed, three dismissed as immaterial.** The gate was armed by an amendment changing three contracts for work not yet built, so the packet named those three regions and told lanes a spec-vs-code difference there was expected. **All three lanes independently found the same two Q2s**, which is the strongest agreement this document has produced. **The most consequential is a contradiction the amendment itself created:** INV-6 promised `resolveUnder` returns a path for *every* relative, non-empty input resolving inside `root`, and new INV-15 refuses `COM1`, `a.` and `a:b`, which are all three of those things. §7 gives both invariants one test file, so a builder writes one assertion that the other's implementation must fail — and the security rule is the likelier one to be dropped as the older statement. The same absolute wording had been swallowing the dangling-symlink rejection since the previous run. **The second is older and not the amendment's:** §4.4's declaration comment says a variable is honoured *wherever it points* while §4.4's prose and the shipped code both ignore a relative one, and INV-8 sided with the comment — so `XDG_CONFIG_HOME=cfg` yields the build tree on one reading and `~/.config` on the other, with INV-8's own test passing either way. **Two lanes found the `readFile` cap unpinned:** the prose describes a cap and names no figure, no invariant and no §10 row, while the code had already chosen 4 GiB. That is a contract UTA-0003 binds to — `upkg` reads every package through `readFile` — so the figure is now stated, with an honest `nothing` row, since a fixture over the cap is impractical. **One lane found a false claim in my own amendment**, and running it then refuted the lane's diagnosis too: I had written *the containment comparison ignores an empty trailing element* in the present tense for unbuilt work, and named `lexically_normal`, which the function never calls. Measured on libstdc++ 16.2, `weakly_canonical` strips the element and containment already holds, so nothing is broken on that leg; libc++ and MSVC cannot be run here. The property is now locked by an INV-6 test case on every leg rather than argued from the one implementation available — an unrunnable region stated rather than looped on. **One lane found the §4.5 sketch contradicting the header it describes**, calling a handle outliving its `JobSystem` a programming error where `Jobs.h` says the shared state makes it safe and the owner pointer is compared, never dereferenced — material because UTA-0047's implementer is exactly who reads that line before adding `failed()`. **The Q4:** INV-15's own test could not falsify its *before any filesystem call* half, since every listed input fails whether the check runs before or after canonicalisation; a case under an uncanonicalisable root now distinguishes them. **Dismissed as true but immaterial:** §1's *no `src/` sources at all*, §11's *(Filled once the stack exists.)*, and the `LogRecord` sketch omitting a default the shipped struct sets. **Four lane open questions resolved clean and are not in the tally:** `logCore` is `constinit`, the re-entrancy guard exists and is covered by a test asserting the sink is entered once, `xdgDirectory` does apply `ut-ants` to all three Linux paths, and the code's cap is real. |
| 4 | 2026-09-04 | 3, cold — identical brief, packet rebuilt from disk | 1 | 1 | 1 | 3 | **Six verified, six fixed, none dismissed. Cap reached (2 for a spec); the run files its tail — which is empty — and exits.** **A violent cap by the measurement, and the number is the number: five of this loop's six findings landed on text THIS RUN wrote.** The qualitative read is gentler — loop 3 fixed contradictions and loop 4 found weak *test clauses* in the invariants loop 3 wrote, so the loops progressed rather than circling — but that is a reading, and the share is what the rule asks to be reported. **Second share:** eight of the run's thirteen verified findings anchor inside the amendment's own span; the other five were pre-existing defects the whole-document read surfaced, which is the audit half of the gate doing its work. **Three of the six were Q4 — invariants whose own tests could not falsify them**, which is the shape a loop after a loop of new invariants produces. **Two lanes independently found the sharpest:** INV-16 requires `failed()` to be published before `done()`, and its test waited and then read both flags — which passes whichever order they were published in. §10 compounded it by marking the row covered where the structurally identical races in INV-5 and INV-13 both carry **Partial**. An implementer would have written the stated test, watched it green against a `failed` stored after `done`, and shipped the race that makes `ubake` read *0 threw* from a batch that threw. **INV-8's relative-variable rule, added in loop 3, was asserted by none of its three test cases** — so deleting the branch would have left the suite green and a user's config under the build tree. **INV-15's ordering fixture was not constructible:** it asked for a `root` that cannot be canonicalised, while INV-3 and INV-7 both reject permission-dependent fixtures and a symlink loop is not portable to the MSVC leg. Measured a replacement instead: `weakly_canonical` resolves `<root>/COM1/../safe.unr` to `<root>/safe.unr`, so that one input separates a filesystem-first implementation from a lexical-first one with no error condition and no special filesystem, and runs on every leg. **The Q2 was loop 3's own collateral:** INV-6 required a trailing-separator root *accepted on every standard library* while §4.4, two hundred lines away, called refusing it *fails closed* — licence to ship the refusal on whichever leg preserves the empty element. §4.4 now prescribes stripping it. **The Q3:** the amendment made `fileSink` a failure-returning surface and pinned no `ErrorCode`, while §4.4 pins exactly that for the project's other `fopen`; the startup routine branches on the code to decide whether to create the log directory and retry, so two builders settle it differently. Verified against `codeForErrno`, which maps `ENOENT` to `NotFound`. **The Q1 was pre-existing:** §4.4 attached *each under "ut-ants"* to the fallback list alone, where `xdgDirectory` appends it to the variable branch too — a builder writing INV-8 case (a) would have gone red against shipped code and "fixed" it by moving every user's config, cache and logs. **All three lanes declined to file §1's *no `src/` sources at all* and §11's *(Filled once the stack exists.)* as immaterial, and flagged the judgement rather than the sentences** — the same call loop 3 made. **Routing at the cap:** a spec's cap is where implementation takes over, and implementation is the better third reviewer. The document goes to the three fixes it was amended for; this gate does not re-run on it as it stands. |
| impl-2 | 2026-09-04 | none dispatched — implementation, not a review loop | n/a | n/a | n/a | n/a | **Fold-back from building UTA-0046, UTA-0047 and UTA-0048. NO reviewer was dispatched for this row; it records what the matrix proved.** Under `CLAUDE.md` rule 14 this records what was built and does not re-arm the gate. **The contract needed one correction, and it was a fact the document itself flagged as unknown.** §4.4 said MSVC's standard library *was not measured, because this machine cannot run them*, which was true when written and stopped being true the moment INV-6's trailing-separator case ran on the matrix: MSVC strips the empty element too. libc++ is still unmeasured, and now named as the one that is — both Linux legs use libstdc++. **The invariants needed no change at all**, which is the useful half of this row: every fix satisfied them as written. **What the matrix caught was a TEST claiming more than its invariant**, not a defect in the code. INV-6's new case asserted the resolved path was a string prefix of the raw root, and the MSVC leg went red on it while `resolveUnder` had accepted the root correctly — the CI runner's temp directory is a short 8.3 path that `weakly_canonical` expands, so the prefix test failed for a reason with nothing to do with trailing separators. That is `spec-format.md` §0's third question, *does any test claim to prove more than it exercises*, answered by a compiler rather than by a reader. The case now compares the two spellings of the root against each other, which depends on no platform's idea of how a path is written. **Worth carrying forward:** the two Linux legs and ThreadSanitizer were all green on this, and only the leg that could not be run locally disagreed — which is the argument for INV-15's rules being lexical and platform-independent rather than `#ifdef _WIN32`. |

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
- **Worker threads:** one fewer than `hardware_concurrency()`, and one
  where that reports 0 or 1 — §4.5 says why the subtraction comes second.
- **No new external dependency.** Design rule 1 forbids one, and INV-14
  checks it.
