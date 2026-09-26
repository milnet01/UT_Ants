# UT_Ants — instructions for Claude Code

## Where this project is

**State:** 4 if nothing is 🚧, or if every 🚧 is parked on `Waiting-on:`
— `workflow.md` § 1 puts a project whose every 🚧 is parked between
items. Else 5.
**Next:** `UTA-0177`.
**In flight:** whatever the roadmap marks 🚧.

> **`In flight:` is not kept by hand.** Ask the roadmap:
> `roadmap_query status:"in-progress"`. A line that must be right in two
> places at once is a line that will be wrong in one of them.
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
> and [`docs/build-and-test-lessons.md`](docs/build-and-test-lessons.md)
> records a session setting it so while Windows was red. **Its 🚧 is
> different**: a session sets it when it picks work up,
> minutes before doing the work, so it is the freshest thing available.

**What shipped, and what each item left behind, is
[`docs/project-state-history.md`](docs/project-state-history.md).** It was
moved out of this section on 2026-09-20: it had grown to about two fifths of
this file, which is read on every prompt, and a session needs it only when it
goes looking. `ROADMAP.md` is the authority for any of it. What stays below is
what changes what a session does *now*.

### Picking work

**Rule 1's set is not listed here — ask the roadmap**, which § Which item
comes next gives the call for. A hand-kept list of it goes stale the
moment an item is filed or closed, which is the same reason `In flight:`
is not kept by hand. What is worth recording is the standing deferral:
`UTA-0059` defers itself until the renderer lands. `UTA-0079` and
`UTA-0081` are `Source: in-session-`, so rule 1 does not reach them.

**Read the open `0.1.0` bodies before picking** — `UTA-0082` records items
deferred out of that release's cut.

### Standing facts

**A bake goes stale when the baker or the format moves.** A bundle is
format 14 (`UTA-0164`) and the baker is at revision 23 (`UTA-0212`), so a
map baked before either must be baked again.

**The renderer's first iteration uses the cheapest methods that still look
like a modern game**; fully modern features come after (user, 2026-09-14).

**Bounce light stays**, by whatever mechanism (user, 2026-09-20). It ships
today as `UTA-0112`'s baked probes. Removing or disabling it is the user's
call, not a measurement's — its share of a frame is small enough that a
performance pass could drop it and show almost nothing.

### Routing: read these before touching it

**No exit in the reference library is shot** (`UTA-0130`, censused
2026-09-25). `MonsterEndSB` wins by `TakeDamage` only with `TriggerType ==
TT_Shoot`, and no map uses it: every exit is player- or pawn-proximity. So
reaching the exit's cylinder is the right routing test here. The counts are on
`UTA-0130`. `ut-dump`'s `exits` carries `triggerType`,
`damageThreshold` and `bInitiallyActive`; census again before trusting this on
another library.

**Read `offWorld` as "not walkable to", never as "cannot be finished".**
Maps carrying an Assault-to-MH conversion kit put an `MHEnd` actor on the
MonsterEnd, and it touches the MonsterEnd when the final objective fires. All
58 off-world maps are this shape; `UTA-0131` has the evidence.

**Triggering the MonsterEnd is the WHOLE win condition**, with no monster
count anywhere in it.

**Read `UTA-0196` before any chain work**: an earlier three-node chain
reached the exit's node but hung off a component the start cannot reach.

**Do not read UT_MonsterHunt's stored route verdicts without checking
firmness.** The caution stands on its own. The case this file used to cite for
it was theirs, and they corrected it on 2026-09-20; the correction is in the
history file.

**A teleporter that starts switched off still routes, unless nothing in the
map switches it on** (`UTA-0142`, UTA-0121 § 3 decision 10). The game's own
planner routes through one at round start, so do not cut its links on
`bEnabled` alone.

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
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
  ctest --test-dir build -L '^device$'
```

Without that variable the tier runs on this machine's GPU. **A device test that
finds no device fails; it never skips** —
`docs/specs/UTA-0014-vulkan-draw-path.md` § 3 decision 6. **It also runs under
the Khronos validation layer, and fails on any layer error or when the layer
is not installed** (`UTA-0138`).

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
losing this repository's own hooks rather than the push gate.

```sh
git config core.hooksPath      .githooks         # see the note below
git config ants.gate.command   ./scripts/ci-matrix.sh   # unset: no gate runs
git config ants.gate.docsMode  --docs
git config ants.gate.docsGlob 'docs/*|*.md|LICENSE'
```

**A green push is not evidence the gate ran.** Four ways it passes having
checked nothing, and only two announce themselves: `NOTHING WAS CHECKED`
(the resolved hook is missing), a line naming a pipeline but no local gate
(`ants.gate.command` unset — the hook's fallback list does not contain
`scripts/ci-matrix.sh`), **no hook output at all**, which on this machine
means `core.hooksPath` naming a directory with no `pre-push` rather than
being unset, and **`.githooks/pre-push` not being executable** — git skips
a non-executable hook in silence.
An unset `docsGlob` is silent too, and widens what counts as
documentation.

**Config alone cannot answer this, because the mode is not config.** Check
both:

```sh
git config --get-regexp 'hooksPath|^ants\.gate\.'
test -x .githooks/pre-push && echo "hook executable" || echo "HOOK NOT EXECUTABLE"
```

**The push gate runs GitHub's three legs; a bare `ci.sh` runs one.** A
bare run uses whatever `CXX` resolves to, and says which at the start and
the end. The gate needs `gcc14-c++` and `clang19` installed, and the SSH
alias `wintest-gate`, which logs in to the Windows machine as a NON-ADMIN
account. Elevated, the Vulkan loader ignores `VK_DRIVER_FILES`, and
INV-5's no-driver tests then find that machine's GPU and fail. **When the
machine is unreachable the gate prints `WINDOWS LEG NOT RUN` and lets the
push go** (user decision, 2026-09-25), so a green push can still lack the
MSVC leg. **Flip a roadmap item on GitHub's matrix, never on a local
run.**

**A `cancelled` CI run is not a failure.** `.github/workflows/ci.yml`
sets `cancel-in-progress`, so each push cancels the run still in flight
and its jobs render as ✗. Check the run whose `headSha` is HEAD:

```sh
gh run list --limit 5 --json headSha,conclusion
```

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
that separation is what **S7** is measured on. On this machine add
`-DUTA_REFERENCE_INSTALL=ON`: some of the tier's figures were measured on
the reference install and are asserted only there (`UTA-0132`). Off, they
are printed and not asserted.

**There is no `UTA_SANITIZE=address`.** That option takes `''` or
`'thread'` and refuses anything else with a `FATAL_ERROR`, so reach for
the flags directly in a build directory of their own:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

Address and thread cannot share a binary, which is why this is a separate
directory rather than a flag on the gate.

**Five rules this project paid for.
[`docs/build-and-test-lessons.md`](docs/build-and-test-lessons.md) holds
what each one cost — read it before deciding one no longer earns its
place.**

- **Mutate before trusting a green test.** A test that passes and reads
  correctly may still be graded by something other than the rule it names.
  `./scripts/mutation-probe.py ubundle` asks mechanically, and a new
  survivor exits non-zero. **`ubundle` is its only subject** — the
  mutations are written out by hand, so **every other lane is mutated by
  hand too**, and the rule still applies there. `UTA-0083` generalises it.
  Add `--asan` for a rule the Release leg cannot see, such as a bounds
  check.
- **When a mutation survives under a sanitizer, suspect the FIXTURE before
  concluding the check is unnecessary.** Ask which rule makes this fixture
  fail, and whether it is the rule you meant to test. A bounds check is the
  shape a plain test cannot grade — remove one and the case is undefined
  behaviour rather than a wrong answer, so it passes.
- **A path test must not compare against a raw temp path.** The Windows
  runner's temp directory is an 8.3 name that `weakly_canonical` expands.
  Assert the property instead: resolve under both spellings, compare the
  results.
- **Write a spec's invariants in `spec-format.md` § 3.7's bullet form.** A
  paragraph form parses to ZERO invariants, and `spec_lint` then returns
  `findings: []` with its section and coverage flags true — indistinguishable
  from a clean document. Check `spec_query` reports a non-zero
  `invariants_count` before trusting a clean lint.
- **`spec_lint` reports `surfaces_checked: false` on this project, always.**
  It resolves test surfaces only in a `tests/features/<name>/` layout and
  this project uses `tests/unit/`, so `findings: []` is SILENT about test
  surfaces rather than a pass. Read the flag before the count and check the
  `*Test:*` clauses by hand. Upstream ANTS-4393 / ANTS-4679.

### Which item comes next

The user's standing priority order, given 2026-09-04 and revised
2026-09-14 so the work reaches each release in turn:

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

Kept outside this file, because every session pays for every line here on
every turn and reads this history almost never. Split by kind:

| File | What it holds |
|---|---|
| [`docs/project-state-history.md`](docs/project-state-history.md) | What shipped and what it left behind, the investigations, and the corrections this file made to its own record. Moved out of § Where this project is on 2026-09-20 |
| [`docs/build-and-test-lessons.md`](docs/build-and-test-lessons.md) | What each § Build and test rule cost — the vacuous tests, the ASan fixture, the red MSVC leg, the invisible invariants |
| [`docs/session-coordination-history.md`](docs/session-coordination-history.md) | What was measured about worktrees, the roadmap store and the push gate |
| [`docs/claude-md-history.md`](docs/claude-md-history.md) | How this document changed, and what it used to say |
| `docs/claude-md-review-<date>.md` | The contract-gate loop logs, one record per run, rows numbered continuously across them |

**Rules live here; their evidence lives there.** A rule in this file is
meant to be followed without reading its history. Follow the link before
deciding a rule no longer earns its place — each was paid for once
already.
