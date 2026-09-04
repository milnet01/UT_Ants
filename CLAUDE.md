# UT_Ants — instructions for Claude Code

## Where this project is

**State:** 4 — between items. Discovery and design are agreed and gated.
**Next:** `UTA-0003` — `upkg`: the package container (spec drafted, ungated).
**In flight:** nothing.

> Keep the three lines above true, and keep them to three lines. **All
> three move together**: picking an item sets `In flight:` and `State:` to
> 5, finishing it clears `In flight:` and returns `State:` to 4. `State:`
> and `In flight:` cannot disagree — `workflow.md` § 1 defines state 4 as
> nothing in flight. Everything else about where work stands is read off
> things that
> are harder to falsify — whether a spec exists, what `git status` says,
> whether the tests pass **on the matrix**. A recorded step number starts
> lying the first time a session forgets to update it, and still reads as
> authoritative.
>
> The roadmap bullet is a record, not a signal: it says 🚧 or ✅ because
> somebody set it, and § Build and test below records a session setting it
> to ✅ while Windows was red. Confirm it against the thing it claims.
> The one other position kept by hand is § Which item comes next's
> deferral note, which that section owns.

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
`$ANTS_GLOBAL_HOOKS` if that names a hooks *directory*, otherwise
`~/.claude/githooks/`. That machine-wide hook picks the gate, decides
documentation-only, and runs it over the pushed commits in a detached
worktree — not over what happens to be on disk.

**Three `git config` keys drive it, they live in `.git/config`, and none
survives a clone:**

```sh
git config ants.gate.command  ./scripts/ci.sh          # without this, NO gate
git config ants.gate.docsMode --docs
git config ants.gate.docsGlob 'docs/*|*.md|LICENSE'
```

`ants.gate.command` is the load-bearing one. Unset, the hook falls back to
a fixed discovery list — `scripts/local-ci.sh`, `ci-local.sh` and similar
— and `scripts/ci.sh` **is not in it**, so a fresh clone takes the no-gate
branch and exits 0. **A green push is not evidence the gate ran**: look
for `NOTHING WAS CHECKED` (no machine-wide hook) or a line saying the repo
has a pipeline but no local gate (keys unset).

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

**Rule 1's set is the open items whose `Source:` carries one of the
review tokens in `roadmap-format.md` § 3.5.3** —
`audit-<date>` (`check-code`), `code-quality-review-<date>`
(`review-code`), `debt-sweep-<date>`. Match the tokens, not the word
"review": two of the three do not contain it, and they are the debt and
codebase findings rule 1 puts first. A finding filed under none of them
is invisible to this order and gets worked last.

**This project has also written `review-code-<date>`, which § 3.5.3 does
not list.** Match it too, and file new findings under the standard's
spelling.

**`Next:` names the next roadmap item.** A rule-1 finding taken ahead of
it moves `In flight:` and `State:`, and leaves `Next:` alone. Record the deferral on the deferred item, in the roadmap
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
