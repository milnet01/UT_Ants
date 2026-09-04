# UT_Ants — instructions for Claude Code

## Where this project is

**State:** 5 — on an item. Discovery and design are agreed and gated.
**Next:** `UTA-0003` — `upkg`: the package container (spec drafted, ungated).
**In flight:** nothing.

> Keep the three lines above true, and keep them to three lines. They are
> the only position this project records. Everything else about where
> work stands is read off things that cannot lie — whether a spec exists,
> whether tests fail, what `git status` says, whether the roadmap bullet
> is 🚧. A recorded step number starts lying the first time a session
> forgets to update it, and still reads as authoritative.

## How work is done here

- **`~/.claude/workflow.md`** — the states, the gates, and what "done"
  means. Read in place. This project does not have its own copy.
- **`~/.claude/standards/`** — how to write code, tests, commits,
  documents, releases. Also read in place.

Neither is summarised here. A rule restated in two places is two rules
that will disagree.

## This project's own facts

Everything below is specific to this project, which is why it lives here
rather than in a standard.

### Stack

C++23, CMake, Catch2 v3 fetched by the build. Linux and Windows are both
first-class; GitHub's matrix builds GCC, Clang and MSVC. `docs/design.md`
§ The stack owns the reasoning and the version floors — read it there.

### Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -L unit
```

`./scripts/ci.sh` is the whole gate. `.githooks/pre-push` does not run it:
it delegates to `~/.claude/githooks/pre-push` (or `$ANTS_GLOBAL_HOOKS`),
which decides from `git config ants.gate.docsGlob` whether the push is
documentation-only, then runs the gate over the pushed commits in a
detached worktree — not over what happens to be on disk.

**With no machine-wide hook the delegator prints `NOTHING WAS CHECKED`
and exits 0**, so a green push is not evidence the gate ran. `docsGlob`
lives in `.git/config` and does not survive a clone; here it is
`docs/*|*.md|LICENSE`, and unset it falls back to a wider default — so a
fresh clone gates differently without saying so.

**A local green is one leg of three.** GitHub runs GCC, Clang and MSVC;
a local run uses whatever `CXX` resolves to, and the gate says which at
the start and the end. `CC=clang CXX=clang++ ./scripts/ci.sh` runs
another leg. **Flip a roadmap item on the matrix, not on the local
leg** — done once the other way round on 2026-09-04, and the item read
shipped while Windows was red.

**A `cancelled` CI run is not a failure.** `.github/workflows/ci.yml`
sets `cancel-in-progress`, so each push cancels the run still in flight
and its jobs render as ✗. Check the run whose `headSha` is HEAD:

```sh
gh run list --limit 5 --json headSha,conclusion
```

**A path test must not compare against a raw temp path.** The Windows
runner's temp directory is a short 8.3 name that `weakly_canonical`
expands, so `result->string().starts_with(dir.path().string())` compares
two spellings of one directory and fails on MSVC alone. Assert the
property instead — resolve under both spellings and compare the results.
Cost one red MSVC leg on 2026-09-04; both Linux legs were green.

**`spec_lint` reports `surfaces_checked: false` on this project, always.**
It resolves test surfaces only in a `tests/features/<name>/` layout and
this project uses `tests/unit/`, so `findings: []` is SILENT about test
surfaces rather than a pass — read the flag before the count, and check
the `*Test:*` clauses by hand. Upstream ANTS-4393 / ANTS-4679.

Two options worth knowing. `-DUTA_SANITIZE=thread` builds under
ThreadSanitizer, which is how the job system's thread-safety is
measured; the gate runs it as its own step on Linux, and refuses on
MSVC, which has no ThreadSanitizer. `-DUTA_REAL_ASSET_TESTS=ON` with
`-DUTA_UT_INSTALL_DIR=<path>` adds the second test tier, off by default
so a clone with no Unreal Tournament still builds and tests clean —
that separation is what **S7** is measured on.

### Which item comes next

The user's standing priority order, given 2026-09-04:

1. Outstanding fixes from any review — test, debt, codebase or document,
   including backlogged ones.
2. Open roadmap items that reach v1.0.0.
3. Open roadmap items for the version after.

**Rule 1's set is the open items whose `Source:` names a review** — that
is the only handle a session has, so a finding filed without it is
invisible to this order and gets worked last. File it that way.

**`Next:` names the next roadmap item.** A rule-1 finding taken ahead of
it moves `In flight:` and leaves `Next:` alone, so the two lines stay
true together. Record the deferral on the deferred item, in the roadmap
store — not by editing `ROADMAP.md`, which is generated from the store
and drops a hand edit without saying so. Clear that note when the item is
picked up: a note nobody clears is the lying record the block at the top
of this file warns about.

### Roadmap IDs

`UTA-NNNN`, per `roadmap-format.md` § 3.5.1. Commit subjects
are `<ID>: <description>`, per `commits.md`.

### Overrides

Any place this project deliberately departs from a global standard goes
in `docs/standards/`, with the reason. If that directory is empty, there
are none.

### This file's own review history

Kept outside this file, so every session does not pay for it:
`docs/claude-md-review-2026-09-04.md`.
