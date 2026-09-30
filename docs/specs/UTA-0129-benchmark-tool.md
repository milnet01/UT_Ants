# UTA-0129 — a benchmark tool that says where a bake's time goes

**Status:** spec draft (2026-09-30). No review: the user chose a spec with no
cold read, 2026-09-30. The bake half is built; § 4 records it as built.
**Kind:** implement.
**Source:** ROADMAP UTA-0129 (user-request-2026-09-12).

**Blocker for:** UTA-0098, UTA-0100.

**Layman:** a tool that bakes a map several times and says which step of the
bake took the time, so speed-up work goes where it helps.

## 1. Goal

`ut-bench bake` bakes each map it is given several times and prints, for
each named step of the bake, how long it took and what share of the whole
that is. Every figure is printed beside the machine, the compiler, the build
type and the commit it was measured on. The tool also checks that every run
of a map produced the same bundle.

## 2. Problem

1. Nothing in `src` measures a bake. `std::chrono::steady_clock` appears in
   `src/urender/Frame.cpp` and in four real-asset tests, and nowhere in
   `src/ubake`.
2. `ubake::bakeToDirectory` and `ubake::detail::bake` run their steps one
   after another on the calling thread, with numbered comments naming each
   step. A total bake time says nothing about which step it went to.
3. UTA-0098 and UTA-0100 each wait on a per-step figure. Both were found by
   reading source by hand, which is not repeatable.
4. A timing with no record of the machine and build cannot be compared with
   a later one. `CLAUDE.md` § Build and test records that a bare `ci.sh` uses
   whatever `CXX` resolves to, so two local figures need not share a compiler.

## 3. Scope decisions (agreed with the user)

- **A spec, with no review** — user, 2026-09-30.
- **The bake half first.** Frame time follows as this item's second step and
  extends this spec — user, 2026-09-30.
- **The reporting shape follows Vestige's**, as the roadmap item requires:
  a named scope that nests, one row per phase with its depth, the minimum as
  the figure to compare on, and no timing gate in CI. What was taken and what
  was not is recorded on UTA-0129 in the roadmap.
- **Timing is always on.** A bake reads the clock a few dozen times. An
  option to turn that off would be one more path to test. Session's call.
- **`ut-bake`'s output does not change.** `ut-bench` links `uta_ubake` and
  reads the phases from `BakeOutcome`. `UTA-0011` § 4.8's output shape is a
  breaking surface (`docs/standards/versioning-overrides.md` § Breaking
  surfaces) and gains nothing from carrying timings. Session's call.
- **`ut-bench` is not a breaking surface.** It is a developer's tool and
  ships to no player or server operator. Its output carries a `schema`
  number so a reader can refuse a shape it does not know. Session's call.
- **Phases are timed on the calling thread only**, as wall-clock time. Work
  a step hands to the job system counts toward the step that waited for it.
  Per-thread CPU time is not reported. Session's call.

## 4. Design

### 4.1 Phase times — `core`

`src/core/Timing.h`, `src/core/Timing.cpp`, in `uta_core`.

```cpp
namespace uta {

/// One named phase, as PhaseTimes recorded it.
struct Phase {
    std::string name;        ///< lower-case words joined by '-'
    unsigned depth = 0;      ///< 0 for a phase opened with none open
    double seconds = 0;      ///< every call's elapsed time, summed
    std::uint64_t calls = 0;
};

/// Named, nested phases and how long each took. Owned by the thread that
/// made it: a scope opened on any other thread records nothing.
class PhaseTimes {
public:
    using Clock = std::function<double()>;   ///< seconds, never decreasing

    PhaseTimes();                    ///< reads std::chrono::steady_clock
    explicit PhaseTimes(Clock clock);

    /// Every phase closed so far, in the order each was first opened.
    [[nodiscard]] std::vector<Phase> phases() const;

    /// Add `child`'s phases under the phase now open, each one deeper by
    /// this one's open depth, after the phases already recorded.
    void adopt(const std::vector<Phase>& child);
};

/// Opens `name` in `times` and closes it when destroyed. `times` may be null.
class PhaseScope {
public:
    PhaseScope(PhaseTimes* times, std::string_view name);
    ~PhaseScope();
    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;
};

}  // namespace uta
```

- A phase is identified by its name and the phases open around it. Opening
  the same one again adds to `seconds` and `calls`; it makes no second row.
- A parent's `seconds` includes its children's.
- A row appears in `phases()` when its scope first closes, at the position
  it was first opened.

### 4.2 The bake records its phases — `ubake`

```cpp
struct BakeResult {
    // ... the existing members, then:
    std::vector<Phase> phases;   ///< detail::bake's steps, depth 0
};

struct BakeOutcome {
    // ... the existing members, then:
    std::vector<Phase> phases;   ///< every step that ran, whatever the verdict
};
```

`bakeToDirectory` records these at depth 0, in this order. Each is the step
the comment beside it in `src/ubake/Bake.cpp` names.

| Phase | What it covers |
|---|---|
| `open-install` | `Install::open` |
| `read-map` | reading the map file |
| `name` | `detail::bakeName` |
| `closure` | opening the map, `detail::closure`, and resolving each import |
| `clashes` | `clashesOf` |
| `bake` | `detail::bake`; its steps follow at depth 1 |
| `fit-budget` | `umat::fitToBudget` and the measure after it, when they run |
| `encode` | `ubundle::write` |
| `write-file` | `fs::writeFileAtomically` |

`detail::bake` records these, which `bakeToDirectory` adopts under `bake`:

| Phase | `detail::bake`'s step |
|---|---|
| `level` | 1 and 2, the level and its world |
| `rooms` | 3, `umap::buildRoomMap` |
| `nav` | 4, `unav::buildNavGraph` |
| `wiring` | 4, `unav::buildWiringGraph` |
| `actors` | 5, `buildActors` |
| `movers` | 6, the movers and their Models |
| `materials` | 7, `bakeMaterials` |
| `geometry` | 8, `buildGeometry` |
| `strips` | `markStrips`, which `UTA-0255` moved to after step 8 |
| `mover-shapes` | 9 |
| `collision` | 10, `buildCollision` for the level and each mover |
| `light-probes` | 11 |
| `occlusion` | 11b, `bakeOcclusion` |
| `budget` | 12 |

A bake that stops early carries the phases that closed before it stopped. A
`Cached` outcome therefore ends at `clashes`. A step that does not run for a
map has no row.

A new step gets a phase. Adding one is not a schema change (§ 4.3).

No phase is opened inside a job. The bundle's bytes do not depend on any
phase (INV-4).

### 4.3 The command line — `tools/ut-bench`

```text
ut-bench bake --install <install> --scratch <dir> [--runs <n>] [--workers <n>]
              [--texture-cache <dir>] <map>...
ut-bench --help
```

- `--scratch` is where each run's bundle is written. The tool hashes the
  bundle and removes it before the next run. It is required: a bundle is
  large, and the system temporary directory may be memory.
- `--runs` defaults to 3. It is at least 1.
- `--workers` is the `JobSystem` worker count. It defaults to `JobSystem`'s
  own default.
- `--texture-cache` is passed to the bake, as `ut-bake`'s is.
- Each run is `bakeToDirectory` with `force` set, so no run is served from a
  cache. Runs are sequential, and maps are baked one after another.
- The budget is `ut-bake`'s default. An over-budget bake is still timed.

Standard output is exactly one JSON object, with `ut-dump`'s string escapes.
Standard error carries a table for a person: each map's phases by share,
largest first, and any warning.

```json
{"schema": 1, "workload": "bake",
 "machine": {"cpu": "<model name>", "logicalCores": 0, "os": "<name>",
             "load1": 0.0},
 "build": {"compiler": "<id> <version>", "buildType": "<type>",
           "sanitizer": "none | thread", "commit": "<short hash>"},
 "bakerVersion": "<UTA-0011 § 4.3>", "workers": 0, "runs": 3,
 "textureCache": false,
 "maps": [
  {"map": "<as given>", "verdict": "written | over-budget | refused",
   "error": "<a sentence>",
   "bundleSha256": "<64 hex digits>", "identical": true,
   "total": {"min": 0.0, "median": 0.0, "max": 0.0},
   "unattributed": {"min": 0.0, "median": 0.0, "max": 0.0},
   "phases": [
    {"name": "materials", "depth": 1, "calls": 1,
     "seconds": {"min": 0.0, "median": 0.0, "max": 0.0}, "share": 0.0}]}]}
```

- Every `machine` and `build` member is always present. A value the tool
  cannot learn is the string `"unknown"`; `load1` is `null` where the
  platform has no load average. `cpu` is read from `/proc/cpuinfo` and is
  `"unknown"` off Linux. `sanitizer` is `UTA_SANITIZE`'s value, or `"none"`.
  `commit` is `apps/ut-ants/WriteBuildCommit.cmake`'s value, `-dirty` suffix
  included.
- `load1` is the one-minute load average read before the first run.
- `total` is the time of one `bakeToDirectory` call, over the runs.
- `unattributed` is `total` less the depth-0 phases, per run, over the runs.
- A phase's `seconds` is taken over the runs. `calls` is the first run's.
- `share` is the phase's minimum divided by `total`'s minimum.
- The median of an even number of values is the mean of the middle two.
- `bundleSha256` is the first run's bundle. `identical` is whether every
  run's bundle has that hash. Neither appears on `refused`, and on
  `over-budget`, where nothing is written, neither appears.
- `error` appears only on `refused`, and then `total`, `unattributed` and
  `phases` do not.
- `phases` is in `BakeOutcome::phases` order. A phase some runs lack is
  summarised over the runs that have it.

The exit code:

| Code | Means |
|---|---|
| `0` | every map was written or over budget, and every `identical` is true |
| `1` | a map was refused, or a map's runs gave different bundles |
| `2` | the arguments were wrong |

A warning is printed on standard error, and changes nothing else, when
`buildType` is not `Release`, when `sanitizer` is not `none`, or when `load1`
is above `logicalCores`.

```cpp
namespace uta::bench {

/// What compiled the program. `main.cpp` fills it; the default is what a
/// caller that cannot say gets.
struct BuildInfo {
    std::string compiler = "unknown";
    std::string buildType = "unknown";
    std::string sanitizer = "none";
    std::string commit = "unknown";
};

/// `args` excludes the program name. Returns the exit code.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err, const BuildInfo& build = {});

namespace detail {

/// One run of one map, as summarise reads it.
struct Run {
    double total = 0;
    std::string bundleSha256;        ///< empty when nothing was written
    std::vector<Phase> phases;
};

struct Spread { double min = 0, median = 0, max = 0; };

[[nodiscard]] Spread spreadOf(std::vector<double> values);

struct PhaseSummary {
    std::string name;
    unsigned depth = 0;
    std::uint64_t calls = 0;
    Spread seconds;
    double share = 0;
};

struct MapSummary {
    Spread total, unattributed;
    std::vector<PhaseSummary> phases;
    bool identical = true;
};

/// § 4.3's figures for one map. `runs` is not empty.
[[nodiscard]] MapSummary summarise(const std::vector<Run>& runs);

}  // namespace detail
}  // namespace uta::bench
```

`tools/ut-bench/Cli.cpp` is compiled into the tool and into the unit tests,
as `tools/ut-bake/Cli.cpp` is.

## 5. Invariants

- **INV-1** — with a clock a test steps by hand, a phase's `seconds` is its
  scope's elapsed time; a phase opened twice under the same parent is one
  row with `calls` 2 and both times summed; a child's row follows its
  parent's with `depth` one more, and the parent's `seconds` includes it.
  *Test:* `tests/unit/TimingTest.cpp`, new.
  *Breaks when:* a second opening makes a second row; a child's time is
  taken off its parent; rows are ordered by closing.

- **INV-2** — a scope opened on a thread other than the one that made the
  `PhaseTimes` adds no row and changes no row, and a `PhaseScope` given a
  null `PhaseTimes` does nothing.
  *Test:* `tests/unit/TimingTest.cpp`: scopes opened inside
  `JobSystem::parallelFor` while the owner holds one open. The
  ThreadSanitizer step of `scripts/ci.sh` runs it too.
  *Breaks when:* the owner check is dropped, which adds rows here and is a
  data race under ThreadSanitizer.

- **INV-3** — a `Written` fixture bake's `BakeOutcome::phases` holds § 4.2's
  depth-0 names in § 4.2's order, less `fit-budget`, with `detail::bake`'s
  names at depth 1 straight after `bake`. The same bake asked for again
  without `force` is `Cached` and its phases end at `clashes`.
  *Test:* `tests/unit/BakePhasesTest.cpp`, new, over `tests/unit/BakeFixture.h`.
  It compares names and depths, never a time.
  *Breaks when:* a step's scope is left out or misnamed; `adopt` is not
  called; the cached return skips the phases.

- **INV-4** — recording phases changes no bundle byte.
  *Test:* UTA-0011 INV-5's golden bake, unchanged, and
  `./scripts/mutation-probe.py ubundle`'s baseline.
  *Breaks when:* a phase's value reaches anything `ubundle::write` reads.
  The golden would move and `BAKER_REVISION` has not.

- **INV-5** — `spreadOf` returns the smallest, the median and the largest of
  its values in any order given; the median of `{4, 1, 3, 2}` is 2.5.
  *Test:* `tests/unit/BenchCliTest.cpp`, new.
  *Breaks when:* the input is not sorted first; the even case takes one of
  the middle two.

- **INV-6** — `summarise` over runs whose hashes differ gives `identical`
  false; `unattributed` is each run's `total` less its depth-0 phases, so a
  depth-1 phase never counts toward it; `share` is a phase's minimum over
  `total`'s minimum.
  *Test:* `tests/unit/BenchCliTest.cpp`, over `Run` values written out in
  the test.
  *Breaks when:* only the first two hashes are compared; every depth is
  summed; `share` uses the median.

- **INV-7** — `ut-bench bake` over the fixture install prints one JSON
  object with `schema` 1, every `machine` and `build` member present and not
  empty, one `maps` entry per map in the order given, and exits `0`. A map
  that does not exist is `refused` with an `error`, the other maps are still
  baked, and the exit code is `1`. No `--scratch`, `--runs 0` and no map each
  exit `2` and print nothing on standard output.
  *Test:* `tests/unit/BenchCliTest.cpp`, through `runCli`.
  *Breaks when:* a refusal stops the loop; a member the tool cannot learn is
  left out instead of `"unknown"`.

- **INV-8** — after `ut-bench bake` returns, the scratch directory holds no
  file the tool wrote, whatever the verdicts.
  *Test:* `tests/unit/BenchCliTest.cpp`: the directory is empty before and
  after.
  *Breaks when:* the bundle is removed only on the last run, or only when
  the runs were identical.

## 6. Failure modes

- **The clock is read on a busy machine.** The figures are real and wrong
  for comparison. `load1` is recorded and warned on; the reader decides.
- **The first run reads the install from a cold disk.** `open-install` and
  `name` are then slower in run 1 than after. The minimum hides it and `max`
  shows it.
- **A step is added to the bake with no phase.** Its time lands in `bake`
  and in no child, or in `unattributed`. Both are printed.
- **The scratch directory cannot be written.** `bakeToDirectory` refuses,
  the map is `refused`, and the error says why.
- **A run's bundle cannot be read back or removed.** The map is `refused`
  with that error, and the exit code is `1`.
- **The texture cache is warm in later runs only.** `materials` then differs
  between run 1 and the rest. `textureCache` records that the option was on.

## 7. Tests

| Invariant | Test | Label |
|---|---|---|
| INV-1, INV-2 | `tests/unit/TimingTest.cpp` | `unit` |
| INV-3 | `tests/unit/BakePhasesTest.cpp` | `unit` |
| INV-4 | UTA-0011 INV-5's golden bake | `unit` |
| INV-5, INV-6, INV-7, INV-8 | `tests/unit/BenchCliTest.cpp` | `unit` |

No test asserts a duration. Each new test is seen failing first, by mutating
the rule it names (`CLAUDE.md` § Build and test).

One measurement is run by hand once the tool exists, on the reference
install, and recorded on UTA-0129: `AS-Frigate` and `CTF-Face`, the two maps
UTA-0098 names, at the default worker count.

## 8. Alternatives considered (and rejected)

- **Timings in `ut-bake`'s own report.** It changes a breaking surface, and
  makes that output differ from run to run.
- **A sampling profiler, `perf`, wrapped by a script.** No code changes, and
  it reaches inside a step. It is Linux only, its output is a call tree a
  reader must interpret, and it names functions, not the bake's steps. It
  stays the tool for looking inside one phase once this names the phase.
- **A `PhaseTimes` that locks, so jobs can open phases.** Summed job time
  exceeds wall time and needs its own presentation. Nothing waiting on this
  item asks for it.
- **A baseline file and a comparing tool, as Vestige has.** Useful once
  there are figures worth keeping. Nothing reads one yet.
- **Reporting the mean.** Vestige measured a wider spread on it than on the
  minimum, and an average hides a slow run.

## 9. Out of scope

- Frame time for the renderer — UTA-0129's second step; this spec is
  extended then.
- A baseline file and a comparison between two results — deferred; not yet
  queued.
- Timing `ut-paths` and the package readers on their own — deferred; not
  yet queued.
- CPU time per job — deferred; not yet queued.
- Build time — UTA-0050.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2 | `tests/unit/TimingTest.cpp` |
| INV-3 | `tests/unit/BakePhasesTest.cpp` |
| INV-4 | UTA-0011 INV-5's golden bake |
| INV-5, INV-6, INV-7, INV-8 | `tests/unit/BenchCliTest.cpp` |
| Every bake step is inside a phase | **nothing** — `unattributed` and `bake`'s own row show the gap to a reader |
| A figure was measured on a quiet machine | **nothing** — `load1` is recorded and warned on |
| The `machine` and `build` values are true | **nothing** — INV-7 checks they are present |

## 11. Cross-doc impact

- `docs/specs/UTA-0011-map-baker.md` — `BakeResult` and `BakeOutcome` each
  gain `phases`; recorded in § 4.11's manner once built.
- `docs/design.md` — the tool list, if it names the tools.
- `CLAUDE.md` § Build and test — how to run the measurement.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0129-benchmark-tool-loop-log.md`.

## 13. Resource cost

- One new build target, `ut-bench`, and one new source pair in `uta_core`.
- `PhaseTimes` holds one row per distinct phase. A bake has a fixed set.
- `ut-bench` holds one bundle on disk at a time, in `--scratch`.
- No new dependency.
