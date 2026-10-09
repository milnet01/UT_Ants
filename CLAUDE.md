# UT_Ants — instructions for Claude Code

## Where this project is

**State:** 4 if nothing is 🚧, or if every 🚧 is parked on `Waiting-on:`
— `workflow.md` § 1 puts a project whose every 🚧 is parked between
items. Else 5.
**Next:** the user's priority list of 2026-10-05: help other sessions, then every open fix-kind or review-sourced item oldest first, then 0.1.0. Of the open fix-kind items, `UTA-0186` and `UTA-0249` are parked (`UTA-0249` reconfirmed by the user 2026-10-05); `UTA-0098` and `UTA-0100` defer themselves. Then 0.1.0, trimmed to what the release needs on 2026-10-06 (`UTA-0204`): `UTA-0334`, baking the maps UT_MonsterHunt added on 2026-10-07, after the lighting pass and the sun so nothing is baked twice (user, 2026-10-07; the sun, `UTA-0338`, shipped 2026-10-09). `UTA-0283` waits on UT_MonsterHunt. Glass (`UTA-0272`), see-through water (`UTA-0273`) and vegetation (`UTA-0293`) come after 0.1.0. Light behaves realistically, emulated with cheap tricks; matching the original's brightness is a guide, not a target (user, 2026-10-06), and whether a dark room gets an added light with its fitting is decided per room, only after the map is re-baked and its lighting checked (`UTA-0256`, user 2026-10-05) — never a blanket rule, never a brightness gain.
**In flight:** whatever the roadmap marks 🚧.

> **`In flight:` is not kept by hand.** Ask the roadmap:
> `roadmap_query status:"in-progress"`.
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
> The roadmap's ✅ is not one of them — it says ✅ because somebody set it.
> **Its 🚧 is different**: a session sets it when it picks work up,
> minutes before doing the work, so it is the freshest thing available.

**What shipped, and what each item left behind, is
[`docs/project-state-history.md`](docs/project-state-history.md).**
`ROADMAP.md` is the authority for any of it. What stays below is
what changes what a session does *now*.

### Picking work

**Rule 1's set is not listed here — ask the roadmap**, which § Which item
comes next gives the call for. What is worth recording is the standing deferral:
`UTA-0059` defers itself until the renderer lands. `UTA-0079` and
`UTA-0081` are `Source: in-session-`, so rule 1 does not reach them.

**Read the open `0.1.0` bodies before picking** — `UTA-0082` records items
deferred out of that release's cut.

### Standing facts

**A bake goes stale when the baker or the format moves.** A bundle is
format 25 and the baker is at revision 48 (both `UTA-0338`), so a
map baked before either must be baked again.

**The renderer's first iteration uses the cheapest methods that still look
like a modern game**; fully modern features come after (user, 2026-09-14).

**Bounce light stays**, by whatever mechanism (user, 2026-09-20). It ships
today as `UTA-0112`'s baked probes. Removing or disabling it is the user's
call, not a measurement's — its share of a frame is small enough that a
performance pass could drop it and show almost nothing.

### Routing

**Before touching routing, read [`.claude/rules/routing.md`](.claude/rules/routing.md).**
It loads by itself when a session reads `tools/ut-paths/`, `src/unav/` or
their tests.

## How work is done here

- **`~/.claude/workflow.md`** — the states, the gates, and what "done"
  means. Read in place. This project does not have its own copy.
- **`~/.claude/standards/`** — how to write code, tests, commits,
  documents, releases. Also read in place.

Neither is summarised here. A rule restated in two places is two rules
that will disagree.

### Contract reviews

**`~/.claude/CLAUDE.md` rule 14's gate is cancelled for this project**
(user, 2026-09-26). Where that rule would ask, the commit body records
**no gate**.

**Practice, stated separately:** building the code is the default
reviewer. A review runs only where the session judges it will catch
what building cannot, and cheaply enough to be worth its tokens — the
user leaves that call to the session (2026-10-02) and need not be
asked. Say in one line why it was worth it. A small edit to a
contract document is made without a review and without asking. The
user's words: "Reviews are very token heavy and should only be used
where the pay off is worthwhile" and "Often times I find that coding
it is the best reviewer."

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

`./scripts/ci.sh` is the whole list of steps, run once per compiler.
`./scripts/ci-matrix.sh` is the push gate (`UTA-0207`): it runs `ci.sh`
under GCC 14 and Clang 19 here, and under MSVC on the Windows test machine
over SSH. A documentation-only push runs `./scripts/ci.sh --docs` once — a
reduced run in which no compiler leg fires.

**The renderer's device tier draws on a real Vulkan device.** Its tests carry
the label `device`; INV-5's refusal test carries `device-absent`.
`scripts/ci.sh` runs `device` on Linux only, and on the MSVC leg says it did
not. Run it headlessly on Mesa's CPU driver, which is what CI's Linux legs have:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json VK_VALIDATION_VALIDATE_SYNC=true \
  ctest --test-dir build -L '^device$'
```

Without that variable the tier runs on this machine's GPU. **A device test that
finds no device fails; it never skips** —
`docs/specs/UTA-0014-vulkan-draw-path.md` § 3 decision 6. **It also runs under
the Khronos validation layer, and fails on any layer error or when the layer
is not installed** (`UTA-0138`). `scripts/ci.sh` turns on the layer's
synchronization checks with `VK_VALIDATION_VALIDATE_SYNC=true` (`UTA-0227`);
set it by hand too, or a local run misses what the gate catches.

**Do not edit files while a push runs**: `ci-matrix.sh` fails the gate if
the tree changes mid-run. **Flip a roadmap item on GitHub's matrix, never on a
local run.** **A green push is not evidence the gate ran**, and
`./scripts/setup-hooks.sh` must run once in every clone (`UTA-0231`).

**Read [`docs/build-and-test.md`](docs/build-and-test.md) before** trusting a
green push, debugging the gate, reading a CI run, or using ccache, `ut-bench`,
`ut-compare`, ThreadSanitizer, the address sanitizer or the real-asset test tier.

**Before trusting a green test, editing a test or writing a spec's invariants,
read [`.claude/rules/testing.md`](.claude/rules/testing.md)** — five rules this
project paid for. It loads by itself when a session reads a test or a spec.

### Which item comes next

The user's standing priority order:

1. Outstanding fixes from any review — test, debt, codebase or document,
   including backlogged ones.
2. Open roadmap items that reach v0.1.0.
3. Open roadmap items for the version after.

**Rule 1's set is every open item whose `Source:` records the review that
produced it.** That sentence is the test. The tokens below are examples,
and a session that matches them literally instead of applying the
sentence will miss items.

The review-recording values `roadmap-format.md` § 3.5.3 defines are
`audit-<date>`, `code-quality-review-<date>`, `debt-sweep-<date>` and
`doc-review-<date>`. It defines `user-<date>` as well, which records no
review and is outside rule 1 — as this project's own `user-request-<date>`
and `in-session-<date>` are. This project also writes `review-code-<date>`
and `review-contract-<date>`, the second being what a rule 14 gate files.
Match on the `Source:` recording a review, not on the word — `audit-` and
`debt-sweep-` do not contain it.

**Ask the store — and know what the query cannot do.** `roadmap_query`
takes a `source` array of prefixes and returns the open items matching any
of them. That is still a literal token match, only a cheaper one: it cannot
find an item filed under a prefix you did not pass.

```
roadmap_query status:"active"
  source:["review-", "audit-", "debt-sweep-", "code-quality-review-",
          "doc-review-", "indie-review-"]
```

**So when that comes back empty or thin, do not conclude the set is
empty.** List every open item, read each `Source:` and apply the sentence
above. That is the only complete route, and it is what catches a token
nobody thought to enumerate.

**`review-code-<date>` and `review-contract-<date>` are adopted here**, and
are what those two reviews file. Do not substitute a § 3.5.3 value for
either: `doc-review-` looks like a fit for a contract gate and is a
different row, which a prefix query built from § 3.5.3 alone would find
while missing the real one.

The fallback is for a review with no adopted token at all: file the item
with the nearest one and name the review in the body. A `Source:` that
records nothing puts the item outside this order, which is where it is
least likely to be found.

**A rule-1 finding taken ahead of `Next:` leaves `Next:` alone.** Record
the deferral on the deferred item, in the roadmap store — not by editing
`ROADMAP.md`, which is generated from it and drops a hand edit without
saying so. Clear the note when that item is picked up.

### Roadmap IDs

`UTA-NNNN`, per `roadmap-format.md` § 3.5.1. Commit subjects
are `<ID>: <description>`, per `commits.md`.

### Overrides

Any place this project deliberately departs from a global standard **or
from `~/.claude/workflow.md`** goes in `docs/standards/`, with the
reason. `docs/standards/README.md` owns what else may live there. **If
that directory holds nothing but its own `README.md`, there are no
overrides** — it is never empty, so emptiness is not the test.

### History

Split by kind:

| File | What it holds |
|---|---|
| [`docs/project-state-history.md`](docs/project-state-history.md) | What shipped and what it left behind, the investigations, and the corrections this file made to its own record |
| [`docs/build-and-test-lessons.md`](docs/build-and-test-lessons.md) | What each § Build and test rule cost — the vacuous tests, the ASan fixture, the red MSVC leg, the invisible invariants |
| [`docs/session-coordination-history.md`](docs/session-coordination-history.md) | What was measured about worktrees, the roadmap store and the push gate |
| [`docs/claude-md-history.md`](docs/claude-md-history.md) | How this document changed, and what it used to say |
| `docs/claude-md-review-<date>.md` | The contract-gate loop logs, one record per run, rows numbered continuously across them |

**Rules live here; their evidence lives there.** A rule in this file is
meant to be followed without reading its history. Follow the link before
deciding a rule no longer earns its place — each was paid for once
already.
