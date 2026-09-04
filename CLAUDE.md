# UT_Ants — instructions for Claude Code

## Where this project is

**State:** 4 — between items.
**Next:** `UTA-0004` — `upkg`: typed level content.
**In flight:** nothing.

> Keep the three lines above true, and keep them to three lines.
> **`State:` and `In flight:` move together**: picking an item sets both,
> finishing clears `In flight:` and returns `State:` to 4 — `workflow.md`
> § 1 defines state 4 as nothing in flight. `Next:` advances when the item
> it names is picked up, and stays put when a rule-1 finding is taken
> ahead of it (§ Which item comes next, which also owns the deferral note
> — the one other position kept by hand).
>
> Everything else is read off things harder to falsify: whether a spec
> exists, what `git status` says, whether the tests pass **on the matrix**.
> The roadmap bullet is not one of them — it says ✅ because somebody set
> it, and § Build and test records a session setting it so while Windows
> was red. A hand-kept record starts lying the first time somebody forgets
> it, and still reads as authoritative.

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

`./scripts/ci.sh` is the whole gate, and a documentation-only push runs
`./scripts/ci.sh --docs` — a reduced run in which no compiler leg fires.

`.githooks/pre-push` runs neither directly. It delegates to
`$ANTS_GLOBAL_HOOKS/pre-push` whenever that variable is set to anything at
all, else `~/.claude/githooks/pre-push`; a set-but-wrong value is not
corrected, it just disables the gate. The machine-wide hook then picks the
gate script, decides documentation-only, and runs it over the pushed
commits in a detached worktree — not over what is on disk.

**Four settings drive that, all in `.git/config`, none surviving a
clone:**

```sh
git config core.hooksPath      .githooks         # unset: no hook runs
git config ants.gate.command   ./scripts/ci.sh   # unset: no gate runs
git config ants.gate.docsMode  --docs
git config ants.gate.docsGlob 'docs/*|*.md|LICENSE'
```

**A green push is not evidence the gate ran.** Three ways it passes having
checked nothing, and only two announce themselves: `NOTHING WAS CHECKED`
(the resolved hook is missing), a line naming a pipeline but no local gate
(`ants.gate.command` unset — the hook's fallback list does not contain
`scripts/ci.sh`), and **no hook output at all** (`core.hooksPath` unset).
An unset `docsGlob` is silent too, and widens what counts as
documentation. Confirm with
`git config --get-regexp 'hooksPath|^ants\.gate\.'`.

**A local green is one leg of three.** GitHub runs GCC, Clang and MSVC;
a local run uses whatever `CXX` resolves to, and the gate says which at
the start and the end. `CC=clang CXX=clang++ ./scripts/ci.sh` runs a
second leg with whatever clang is installed — which is not CI's leg: the
matrix pins `clang-19`, and this machine has no `clang-19` binary at all. **Flip a roadmap item on the matrix, not on the local
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

**`ccache` and `mold` are used if installed and ignored if not**, and change
nothing about the output. ccache needs two settings before it helps across
build directories — untold, it hashes the build path into the key and mostly
misses:

```sh
ccache --set-config base_dir=/
ccache --set-config hash_dir=false
```

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

**Rule 1's set is every open item whose `Source:` records the review that
produced it** — `audit-<date>`, `debt-sweep-<date>`,
`code-quality-review-<date>` (`roadmap-format.md` § 3.5.3), plus this
project's own `review-code-<date>`. Match on the `Source:` recording a
review, not on the word: two of those do not contain it. Where a review
has no token that fits, file the item with the nearest one and name the
review in the body — a `Source:` that records nothing puts the item
outside this order, which is where it is least likely to be found.

**A rule-1 finding taken ahead of `Next:` leaves `Next:` alone.** Record
the deferral on the deferred item, in the roadmap store — not by editing
`ROADMAP.md`, which is generated from it and drops a hand edit without
saying so. Clear the note when that item is picked up.

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
