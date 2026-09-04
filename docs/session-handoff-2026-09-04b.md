# Where this project stands — 2026-09-04, later

Supersedes `session-handoff-2026-09-04.md`. **Everything here is checkable
from the repository, the roadmap store, git and GitHub Actions; nothing is
the only copy of anything.**

## State

`~/.claude/workflow.md` state **5 — on an item**. Nothing is in flight.
Next is `UTA-0003`, `upkg`'s package container.

## What this session did

**`UTA-0002` shipped — `core`.** The error type, the logger, the
filesystem layer and the job system, building as `uta_core` and linking
nothing but `Threads::Threads`, which a configure-time assertion checks.
The suite is green under GCC, Clang and MSVC, and clean under
ThreadSanitizer.

**The repository is public**, at `github.com/milnet01/UT_Ants`, at the
user's direction. Before the first push: the quarantine guard was run, and
every commit in history — not only the tip — was checked for Unreal
Tournament content. There is none.

**All three CI legs are green for the first time.** The Windows leg had
never executed before today; it now has, repeatedly.

## The three defects only the pipeline could find

Each of these was invisible to a local run, and each is worth knowing
about because the same class will recur.

**The project's Clang floor was wrong.** Clang 18 reports `__cpp_concepts`
as the pre-final C++20 value, and libstdc++ gates `std::expected` on the
final one — so it compiles `<expected>` away entirely, and the type this
project pinned C++23 *for* does not exist. Reproduced both directions in
an `ubuntu:24.04` container. The floor is now 19 in `docs/design.md`,
`CMakeLists.txt` and the CI matrix.

**`.gitignore` was swallowing `src/core`.** A bare `core` pattern, meant
for core dumps, matches a directory of that name at any depth. Three
commits describing the library contained none of it, and everything built
locally because the files were on disk. Only the push gate — which
configures a clean worktree of the commits being pushed — could see it.
The pattern is anchored now.

**The Clang leg had no ThreadSanitizer runtime.** `libclang-rt-19-dev` is
a *recommended* dependency, and the workflow installs with
`--no-install-recommends`.

## Local runs and GitHub

`scripts/ci.sh` **is** the pipeline and GitHub calls that same file, so the
steps cannot drift. What drifts is the **matrix**: GitHub calls it once per
compiler; a developer calls it once with whatever `CXX` resolves to.

So the gate now names its compiler, and a local run ends by saying it
covered one leg of three. To run another leg locally:

```sh
CC=clang CXX=clang++ ./scripts/ci.sh
```

That is not one machine equalling three — MSVC needs Windows — but the gap
is visible and crossable rather than silent.

## What reviewed what

- The spec was gated by `review-contract` to its cap **before any code was
  written**: two loops, six cold lanes, twenty-one findings fixed.
- `check-code` ran over the result. Its findings are closed in the history;
  the ones dismissed carry their reasons.
- `review-code` then judged the code with four cold lanes, one per module,
  each told which files this session had authored. That disclosure is what
  the skill names as the remedy for an author's blind spot, and it worked:
  the lanes went at the exact beliefs they were told to doubt.

**Three of the review findings were verified by running rather than
reading**, and are worth carrying forward as a habit: `Error::message()`
returned a view into an object that `withContext` destroys (ASan missed
it — a short message lives inside the object); `JobSystem`'s constructor
called `std::terminate` when the OS refused a thread; and removing the
logger's re-entrancy guard *hangs* the suite.

## Open, and deliberately

- **`UTA-0043`** — how SDL3, glm, shaderc, Assimp and the Vulkan SDK are
  acquired on both platforms. Wanted before the first third-party
  dependency, so before `UTA-0009`.
- **`UTA-0044`, `UTA-0045`** — subsurface scattering, and roughness-weighted
  screen-space reflections. Both blocked on one design edit: `docs/design.md`
  enumerates `urender`'s responsibilities and names neither, so one rule 14
  gate pass can carry both.
- **`UTA-0046`, `UTA-0047`, `UTA-0048`** — the review findings not fixed in
  the item. Windows-only path hazards needing the Windows box; a job's
  failure being invisible to whoever waited on it; `fileSink` unable to
  report why it could not open.
- **No roadmap item carries the numeric contract.** `docs/design.md`
  requires floating-point contraction and fast-math off across three
  compilers, and ADR-0002 requires one map, recipe and baker version to hash
  to one bundle *on any machine*. Nothing tests that, and a cross-platform
  float-agreement check is cheap now that the matrix is green. It gets
  expensive at `UTA-0011`, with the whole bake pipeline built on top.
- **The map count question is settled, not open.** The design gate's loop-5
  row records it: `docs/discovery.md` carries the live figure, and the three
  ADRs keep theirs because an ADR records what was believed at the time. The
  previous handoff read as though this were outstanding.

## Verifying this file rather than trusting it

```sh
cd /mnt/Games/Scripts/Linux/UT_Ants
./scripts/ci.sh                      # the whole gate, one compiler leg
CC=clang CXX=clang++ ./scripts/ci.sh # another leg
gh run list --limit 3                # what GitHub made of it
```

The suite still passes with no Unreal Tournament present. That is **S7**,
and it remains the claim worth re-checking first.
