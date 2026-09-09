# UT_Ants — instructions for Claude Code

## Where this project is

**State:** 4 if nothing is 🚧, or if every 🚧 is parked on `Waiting-on:`
— `workflow.md` § 1 puts a project whose every 🚧 is parked between
items. Else 5.
**Next:** `UTA-0009` — `umat`: generate a PBR material from a 1999
texture. `UTA-0008` shipped on 2026-09-08, so `UTA-0011` (`ubake`) now
has a container to write a bake into and is blocked only by `umat`,
which is `UTA-0009` and `UTA-0010`. Priority rule 1 is still clear:
`UTA-0059` remains the only open review-sourced item and its own body
defers it until the renderer lands. `UTA-0079` and `UTA-0081` are
`Source: in-session-`, so rule 1 does not reach them. `UTA-0013`'s
quarantine guard was unblocked by the same item and is the alternative
if the guard is wanted before the baker.
**In flight:** whatever the roadmap marks 🚧.

> **`In flight:` is not kept by hand.** Ask the roadmap:
> `roadmap_query status:"in-progress"`. It was a hand-kept line until
> 2026-09-05, when the project began running two sessions at once and one
> line could no longer name what two sessions held. A line that must be
> right in two places at once is a line that will be wrong in one of them.
>
> **`State:` is written as the formula, not as its answer** — so no
> session owes it an edit, and it cannot disagree with the roadmap
> (`workflow.md` § 1). `Next:` is still kept by hand: it
> is a decision rather than an observation. It advances when the item it
> names is picked up, and stays put when a rule-1 finding is taken ahead
> of it (§ Which item comes next, which also owns the deferral note).
>
> Everything else is read off things harder to falsify: whether a spec
> exists, what `git status` says, whether the tests pass **on the matrix**.
> The roadmap's ✅ is not one of them — it says ✅ because somebody set it,
> and § Build and test records a session setting it so while Windows was
> red. **Its 🚧 is different**: a session sets it when it picks work up,
> minutes before doing the work, so it is the freshest thing available and
> the only one that can name two items at once.

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
`$ANTS_GLOBAL_HOOKS/pre-push` whenever that variable is set to a
**non-empty** value — the hook uses `${ANTS_GLOBAL_HOOKS:-...}`, so an
empty one falls back exactly as an unset one does — else
`~/.claude/githooks/pre-push`, and the resolved path must be
executable. A set-but-wrong value is not corrected, it just disables the
gate. The machine-wide hook then picks the
gate script, decides documentation-only, and runs it over the pushed
commits in a detached worktree — not over what is on disk.

**Four settings drive that. Three are `ants.gate.*` and live only in
`.git/config`, so a clone has none of them. `core.hooksPath` is the
exception** — ~/.gitconfig sets it machine-wide to ~/.claude/githooks, and the
repository value below overrides it. So unsetting the repository value
does **not** disable the gate: it falls back to the machine-wide hook,
losing this repository's own hooks rather than the push gate. Corrected
2026-09-05, measured with
`git config --show-origin --get-all core.hooksPath`.

```sh
git config core.hooksPath      .githooks         # see the note below
git config ants.gate.command   ./scripts/ci.sh   # unset: no gate runs
git config ants.gate.docsMode  --docs
git config ants.gate.docsGlob 'docs/*|*.md|LICENSE'
```

**A green push is not evidence the gate ran.** Three ways it passes having
checked nothing, and only two announce themselves: `NOTHING WAS CHECKED`
(the resolved hook is missing), a line naming a pipeline but no local gate
(`ants.gate.command` unset — the hook's fallback list does not contain
`scripts/ci.sh`), and **no hook output at all**, which on this machine
means `core.hooksPath` naming a directory with no `pre-push` rather than
being unset.
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

**Write a spec's invariants in the bullet form, or no tool can see them.**
`spec-format.md` § 3.7 defines `- **INV-1** — <claim>. *Test:* … *Breaks
when:* …` and a GFM table, and nothing else. A paragraph form
(`**INV-1.** <claim>`) parses to ZERO invariants -- and `spec_lint` then
returns `findings: []`, `sections_checked: true` and
`test_coverage_checked: true`, which is indistinguishable from a clean
document. `invariant_no_test` "always runs" and ran over an empty set.
Measured 2026-09-06: UTA-0005 shipped as accepted, through two full
`review-contract` loops, with thirteen invariants invisible to
`spec_query`, `invariant_check` and `spec_lint` alike. Check
`spec_query` returns a non-zero `invariants_count` before trusting a
clean lint.

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

**There is no `UTA_SANITIZE=address`.** That option takes `''` or
`'thread'` and refuses anything else with a `FATAL_ERROR`, so reach for
the flags directly in a build directory of their own:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

Worth knowing because a bounds check is the shape a plain test cannot
grade: remove one and the case is undefined behaviour rather than a wrong
answer, so it passes. UTA-0006 § 4.5's check was proved load-bearing this
way — without it that fixture is a heap-buffer-overflow.

**The sanitizer is half the answer; the FIXTURE has to reach the code.**
Measured 2026-09-08 on UTA-0007's `roomAt`: removing its child-index range
check survived the tests under AddressSanitizer, because the descent is
bounded by `nodes.size()` and a one-node fixture exits that loop before it
ever dereferences the bad index. The loop bound was rejecting the fixture
before the rule under test was reached, so ASAN had nothing to see. Padding
the fixture to three nodes turned the same mutation into a reported
heap-buffer-overflow. **So when a mutation survives under a sanitizer, suspect
the fixture before concluding the check is unnecessary** — ask which rule makes
this fixture fail, and whether it is the rule you meant to test.

**And mutate before trusting a green test at all.** Three of that item's four
tier-1 cases were vacuous on first writing: each passed, each read correctly,
and each was decided by something other than the rule it named — a table entry
that already returned the refusal, a range check that subsumed the guard, and
the loop bound above. None of that is visible from reading the test.

Address and thread cannot share a binary, which is why this is a separate
directory rather than a flag on the gate.

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

### Running two sessions at once

Two Claude Code sessions may work this project simultaneously, and **two
is the cap** — two sessions, therefore at most two items in flight.
**`workflow.md` § 1 already allows several sessions to work one project
from separate worktrees, each holding one item, and sets no ceiling on
how many. What this project adds is the ceiling of two**, and this
section is where it is stated — the ceiling is stricter than § 1 rather
than a departure from it, so it needs no override file. **This is the
only home for the rules below; do not restate them elsewhere.** A
`docs/standards/workflow-overrides.md` carried them until 2026-09-08 and
was retired at its own review gate, which found the second copy
diverging from this one on every loop —
`docs/reviews/workflow-overrides-loop-log.md` is the record.
Corrected 2026-09-08: this read *"departs from `workflow.md` § 1,
which allows exactly one item in flight"*, which was true when written
and stopped being so when § 1's own gate changed the rule to *per
session* on 2026-09-07.

There is no orchestrator: nothing schedules the sessions, and neither can
block the other.

**The roadmap store is what keeps them apart, and reaching it takes one
deliberate step.** The store is keyed to the MAIN checkout. A worktree is
a different path, the store has no row for it, and `roadmap_log` there
silently falls back to patching that worktree's own `ROADMAP.md` — which
the next render from the main checkout overwrites. Measured 2026-09-05: a
query from the worktree answered `source: "markdown"` where the main
checkout answers `source: "store"`, and a dry-run flip reported
`write_path: "patch"`.

> **So every roadmap verb passes the MAIN checkout as `caller_cwd`,
> whatever worktree you are working in**, and **only the main checkout
> ever commits `ROADMAP.md`.** A worktree session that commits it commits
> a stale render.

**Seven rules, and the first is what makes the rest work.**

1. **Flip the item to 🚧 BEFORE starting work, and name yourself in the
   note.** That flip is the claim, and it is how the other session learns
   the item is taken. **The marker records that an item is held and not by
   whom**, so put your session name (`ListAgents` reports it) in the
   progress note — without it a 🚧 left by a dead session is
   indistinguishable from a live claim, and rule 5 makes 🚧 outlive a
   session deliberately.
2. **One item per session, two in total.** `roadmap_query
   status:"in-progress"` — an item another session holds is not
   available, whatever the priority order says about it; a session
   already holding one does not take a second; and **if two are already
   🚧, nothing is available to anyone**. A 🚧 whose named holder is not in
   `ListAgents` is abandoned and may be resumed. **A 🚧 parked on
   `Waiting-on:` counts against neither limit** — `workflow.md` § 1 makes
   it the exception and `roadmap-format.md` § 3.5.4 excludes it from
   selection, so skip it and take the next workable item.
3. **The session already in the main checkout keeps it; a second session
   gets its own git worktree** (`claude -w <name>`). Never two sessions in
   one checkout. A session started the ordinary way in the main checkout
   while another already holds it has breached before it read this line.

   **Check at start-up, because nothing else will — and the check is the
   roadmap, not the process list.** `roadmap_query status:"in-progress"`
   lists the held items; rule 1 put the holder's session name in each
   one's progress note. `ListAgents` then says which of those names is
   live. A named holder that is absent has abandoned its item, and rule 2
   lets you resume it.

   **Neither command counts this project's sessions, which is why the
   test is written this way.** `ListAgents` is machine-wide: measured
   2026-09-09, it returned `ants-terminal-ff` and `ut-monsterhunt-b9`,
   neither working this project. `git worktree list` is project-scoped,
   but a worktree outlives the session that made it, so its presence
   proves nothing about a live holder — run it to see whether the main
   checkout is free, never as a session count.

   **What this route cannot do is detect a breach.** It finds a holder
   that named itself. A session that takes an item without flipping it,
   or flips it without naming itself, is invisible to it. So the cap
   rests on rule 1 being followed, and rule 1 is what makes this rule
   work at all. Corrected 2026-09-09 (UTA-0084): the test was
   `git worktree list` and `ListAgents`, and a session following it
   learned nothing about who held what.

   `.git/config` is shared across worktrees, so `core.hooksPath` and the
   three `ants.gate.*` settings apply in a new one without being set
   again — verified 2026-09-05, and it is the thing most likely to be
   assumed rather than checked, because a missing gate is silent.

   **A worktree brings its own branch, because git refuses to check one
   branch out twice.** That also rules out merging *into* `main` from the
   worktree: `main` is checked out elsewhere, so a local push at it is
   refused by git's checked-out-branch protection. So the worktree
   session **pushes its own branch to `origin`** and the session holding
   `main` merges it. The `pre-push` gate sits on that push — measured
   from the second worktree on 2026-09-05, where it ran `ci.sh --docs`
   and named the commit it was gating, so this is not inferred from the
   settings being present. Its build directory is its own too, which is
   why the two do not fight over `build/`.
4. **Take items that do not share a directory** — and this outranks the
   priority order, as rule 2 does. Where the next item § Which item comes
   next would give you shares a directory with one in flight, take the
   next one that does not, and say why in the flip note. The roadmap
   stops two sessions taking the same item; it does not stop them editing
   one file from two items. `Lanes:` is the cheap signal.
5. **Clear 🚧 when the item is settled** — ✅ when it is done on the
   matrix, back to 📋 when it is abandoned. **A session boundary is not
   one of those**: work still under way stays 🚧 across it, which is
   `workflow.md` § 1's state 5 persisting, and 📋 would tell the other
   session by rule 2 that a half-built item is free.
6. **Only the session in the main checkout advances `Next:`.** It is not
   one shared line: each worktree has its own checkout of this file on
   its own branch, so two sessions editing it produce two copies that
   diverge silently until they merge. A worktree session that thinks
   `Next:` should move says so and leaves the line alone.
7. **A worktree session does not commit `ROADMAP.md`.** Its copy is a
   render of a store it cannot reach; the main checkout owns that file.

**Sessions can message each other** (`ListAgents`, then `SendMessage` by
name), which is worth knowing and is not a coordination mechanism: a
message is read when the other session next looks, and nothing makes it
look.

### Roadmap IDs

`UTA-NNNN`, per `roadmap-format.md` § 3.5.1. Commit subjects
are `<ID>: <description>`, per `commits.md`.

### Overrides

Any place this project deliberately departs from a global standard **or
from `~/.claude/workflow.md`** goes in `docs/standards/`, with the
reason. `docs/standards/README.md` owns what else may live there. **If
that directory holds nothing but its own `README.md`, there are no
overrides** — it is never empty, so emptiness is not the test.

### This file's own review history

Kept outside this file, so every session does not pay for it:
`docs/claude-md-review-2026-09-04.md`.
