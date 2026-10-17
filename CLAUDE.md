# UT_Ants — instructions for Claude Code

## Where this project is

**State:** 4 if nothing is 🚧, or every 🚧 is parked on `Waiting-on:`
(`workflow.md` § 1). Else 5.
**Next:** the user's order of 2026-10-05: help other sessions; then every
open fix-kind or review-sourced item, oldest first; then 0.1.0. Parked fix
items: `UTA-0186` and `UTA-0249` (reconfirmed 2026-10-05). `UTA-0098` and
`UTA-0100` defer themselves. Then the rest of 0.1.0 (`UTA-0204`):
`UTA-0343`, `UTA-0344`, `UTA-0353`, `UTA-0359`, `UTA-0380`, `UTA-0382`,
`UTA-0383`.
`UTA-0283` waits on UT_MonsterHunt.
After 0.1.0: glass (`UTA-0272`), see-through water (`UTA-0273`),
vegetation (`UTA-0293`).
**In flight:** `roadmap_query status:"in-progress"`. Not kept by hand.

> `State:` is a formula, so no session owes it an edit and it cannot
> disagree with the roadmap. `Next:` is a decision, kept by hand. It
> advances when its item is picked up, and stays put when a rule-1 finding
> is taken first (§ Which item comes next).
>
> Trust what is hard to falsify: whether a spec exists, `git status`, tests
> passing **on the matrix**. The roadmap's ✅ is not evidence; somebody set
> it. **Its 🚧 is**: a session sets it minutes before the work.

What shipped and what it left behind:
[`docs/project-state-history.md`](docs/project-state-history.md).
`ROADMAP.md` is the authority. Below is only what changes what a session
does *now*.

### Picking work

Rule 1's set: ask the roadmap (§ Which item comes next). Standing deferral:
`UTA-0059` until the renderer lands. `UTA-0079` and `UTA-0081` are
`Source: in-session-`, outside rule 1.

**Read the open `0.1.0` bodies before picking.** `UTA-0082` records items
deferred out of that cut.

### Standing facts

**A bake goes stale when the baker or the format moves.** Bundles are
format 29 (`UTA-0055`); the baker is revision 60 (`UTA-0254`). Re-bake any
map baked before either.

**The renderer's first iteration uses the cheapest methods that still look
like a modern game**; fully modern features come after (user, 2026-09-14).

**Light behaves realistically, emulated with cheap tricks.** The
original's brightness is a guide, not a target (user, 2026-10-06). Whether
a dark room gets an added light with its fitting is decided per room, only
after the map is re-baked and its lighting checked (`UTA-0256`, user
2026-10-05). Never a blanket rule, never a brightness gain.

**Bounce light stays**, by whatever mechanism (user, 2026-09-20). Today it
is `UTA-0112`'s baked probes. Removing or disabling it is the user's call,
not a measurement's: its frame share is small enough that a performance
pass could drop it and show almost nothing.

### Routing

**Before touching routing, read [`.claude/rules/routing.md`](.claude/rules/routing.md).**
It loads by itself when a session reads `tools/ut-paths/`, `src/unav/` or
their tests.

## How work is done here

`~/.claude/workflow.md` (states, gates, "done") and `~/.claude/standards/`
(code, tests, commits, documents, releases) are read in place. This project
has no copies and summarises neither: a rule in two places becomes two rules
that disagree.

### Contract reviews

**`~/.claude/CLAUDE.md` rule 14's gate is cancelled here** (user,
2026-09-26). Where it would ask, the commit body records **no gate**.

**Practice:** building the code is the default reviewer. A review runs only
where the session judges it will catch what building cannot, cheaply enough
to be worth its tokens. That call is the session's, without asking (user,
2026-10-02); say in one line why it was worth it. A small edit to a
contract document needs no review and no ask. The user: "Reviews are very
token heavy and should only be used where the pay off is worthwhile" and
"Often times I find that coding it is the best reviewer."

## This project's own facts

### Stack

C++23, CMake, Catch2 v3 fetched by the build. Linux and Windows are both
first-class; GitHub's matrix builds GCC, Clang and MSVC. Reasoning and
version floors: `docs/design.md` § The stack.

### Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -L unit
```

`./scripts/ci.sh` is every step, run once per compiler.
`./scripts/ci-matrix.sh` is the push gate (`UTA-0207`): `ci.sh` under GCC 14
and Clang 19 here, and MSVC on the Windows test machine over SSH. A
docs-only push runs `./scripts/ci.sh --docs` once, with no compiler leg.

**Device tests draw on a real Vulkan device**, label `device`; INV-5's
refusal test is `device-absent`. `ci.sh` runs `device` on Linux only, and
says so on the MSVC leg. Run them on Mesa's CPU driver, as CI's Linux legs
do:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json VK_VALIDATION_VALIDATE_SYNC=true \
  ctest --test-dir build -L '^device$'
```

Without `VK_DRIVER_FILES` they run on this machine's GPU. **A device test
with no device fails; it never skips**
(`docs/specs/UTA-0014-vulkan-draw-path.md` § 3 decision 6). **It runs under
the Khronos validation layer, and fails on any layer error or a missing
layer** (`UTA-0138`). `ci.sh` sets `VK_VALIDATION_VALIDATE_SYNC=true`
(`UTA-0227`); set it by hand too, or a local run misses what the gate
catches.

**No file edits while a push runs**: `ci-matrix.sh` fails if the tree
changes mid-run. **Flip a roadmap item on GitHub's matrix, never on a local
run.** **A green push is not evidence the gate ran**; run
`./scripts/setup-hooks.sh` once in every clone (`UTA-0231`).

**Read [`docs/build-and-test.md`](docs/build-and-test.md) before** trusting
a green push, debugging the gate, reading a CI run, or using ccache,
`ut-bench`, `ut-compare`, ThreadSanitizer, the address sanitizer or the
real-asset tier.

**Before trusting a green test, editing a test or writing a spec's
invariants, read [`.claude/rules/testing.md`](.claude/rules/testing.md).**
It loads by itself when a session reads a test or a spec.

### Which item comes next

The user's standing order:

1. Outstanding fixes from any review — test, debt, codebase or document,
   backlogged ones included.
2. Open items that reach v0.1.0.
3. Open items for the version after.

**Rule 1's set is every open item whose `Source:` records the review that
produced it.** That sentence is the test; the tokens below are examples.
Match on the `Source:` recording a review, not on the word: `audit-` and
`debt-sweep-` lack it.

`roadmap-format.md` § 3.5.3 defines `audit-`, `code-quality-review-`,
`debt-sweep-` and `doc-review-` (each `-<date>`). Its `user-<date>`, and
this project's `user-request-` and `in-session-`, record no review and are
outside rule 1. **This project also writes `review-code-<date>` and
`review-contract-<date>`** (the second filed by a rule 14 gate). Never
substitute a § 3.5.3 value for either: `doc-review-` looks like a contract
gate's and is not.

```
roadmap_query status:"active"
  source:["review-", "audit-", "debt-sweep-", "code-quality-review-",
          "doc-review-", "indie-review-"]
```

The query matches prefixes literally, so it misses any prefix not passed.
**An empty or thin answer does not mean the set is empty**: list every open
item and apply the test to each `Source:`.

A review with no adopted token: file with the nearest one and name the
review in the body. A `Source:` that records nothing puts the item outside
this order.

**A rule-1 finding taken ahead of `Next:` leaves `Next:` alone.** Record
the deferral on the deferred item in the roadmap store, not in
`ROADMAP.md`: it is generated, and drops a hand edit silently. Clear the
note when that item is picked up.

### Roadmap IDs

`UTA-NNNN` (`roadmap-format.md` § 3.5.1). Commit subjects are
`<ID>: <description>` (`commits.md`).

### Overrides

Deliberate departures from a global standard **or `~/.claude/workflow.md`**
go in `docs/standards/`, with the reason. Its `README.md` owns what else
may live there. **Only that `README.md` there means no overrides.**

### History

| File | Holds |
|---|---|
| [`docs/project-state-history.md`](docs/project-state-history.md) | What shipped and what it left behind, investigations, and this file's corrections to its own record |
| [`docs/build-and-test-lessons.md`](docs/build-and-test-lessons.md) | What each § Build and test rule cost |
| [`docs/session-coordination-history.md`](docs/session-coordination-history.md) | Measurements of worktrees, the roadmap store and the push gate |
| [`docs/claude-md-history.md`](docs/claude-md-history.md) | How this file changed, and what it used to say |
| `docs/claude-md-review-<date>.md` | Contract-gate loop logs, one per run, rows numbered across them |

**Rules live here; their evidence lives there.** Follow the link before
deciding a rule no longer earns its place.
