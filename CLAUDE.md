# UT_Ants — instructions for Claude Code

## Where this project is

**State:** 4 if nothing is 🚧, or if every 🚧 is parked on `Waiting-on:`
— `workflow.md` § 1 puts a project whose every 🚧 is parked between
items. Else 5.
**Next:** `UTA-0059`, splitting ADR-0007's procedure from its decision — a
rule-1 item whose trigger, the renderer landing, has fired. It waits on a
user decision its roadmap note sets out: supersede ADR-0007 or edit it.
`UTA-0013`'s quarantine guard is the next fresh pick that needs no decision.

`UTA-0014` shipped 2026-09-12, green on the matrix; its spec is
`docs/specs/UTA-0014-vulkan-draw-path.md`. Two things from it that reach
other work. The draw path is graded headlessly on Mesa's software driver,
so `scripts/ci.sh` selects test labels per platform and both CI legs install
Vulkan packages; the presenting path is run by hand, with the probe at
`/mnt/Games/Scripts/Linux/ut-ants-present-probe-uta0014/`.
And `ZoneInfo`'s ambient values, which `UTA-0112` hands the renderer, are
in **no** bundle section (`rg -c Ambient src/` exits 1), so applying them
needs `ubake` to write them and a format version bump first.

`UTA-0133` shipped 2026-09-12 and is ✅. Its gate is worth reading before
the next amendment to any spec: `review-contract` ran before the code, per
rule 14, and found the amendment ITSELF wrong — keyed on two different
navigation points at once. Writing the code first would have built the
defect it was removing.

`UTA-0126` — why eight maps still do not route — is parked on
`Waiting-on:` UT_MonsterHunt and counts against neither limit. All eight
now have a named cause; `UTA-0133` is the repair for the part that is
ours.

What that run settled: the bridge node DOES get its spec and does NOT
open the route — the gap closes by 68 of 2852 units, and the partition
is structural rather than a reach-flag artefact, since "all" mode
reaches the exit no better. So what is needed is a CHAIN across the
remaining 2784 units ANCHORED AT PathNodeSeed8's end, heading toward the
exit. An earlier three-node chain did reach the exit's node but hung off
a component the start cannot reach, which is the mistake that run rules
out.

**All eight now have a named cause** (2026-09-12). Two are ours and are
`UTA-0133`, now shipped: § 4.7 offered its fallback goal wherever the
network reached the node NEAREST the exit, never checking that node is AT
the exit, so the chain bridged to a substitute and never approached the
exit. It keys on a navigation point that TOUCHES the exit now. Over the
partitioned corpus 17 maps moved: five gained something real, twelve
stopped claiming a route they never had. Three need nothing from us
that we can see, and the question goes back to UT_MonsterHunt. Two have
no spot touching their exit. One is MH-NivenSB above, whose fallback
premise HOLDS — an earlier note of ours recorded it as failing, by
comparing a 3D distance against a horizontal window. Read `UTA-0126`'s
body before the chain work: `UTA-0133` is the same mistake as that
three-node chain, and fixing it comes first.

`UTA-0014` — urender's Vulkan device bring-up and the bundle draw path —
is the keystone of 0.1.0 and the alternative to take: `UTA-0059` defers
itself until it lands, and the render items behind it all wait on the
draw path existing. `UTA-0013`'s quarantine guard is a third, and
neither shares a directory with ut-paths, so a second session can take
either alongside `UTA-0126`.

**Shipped 2026-09-12**, all on the matrix: `UTA-0133`'s fallback keying
and `UTA-0134` (answered in-engine by UT_MonsterHunt, nothing to change),
`UTA-0127`'s off-world mark, `UTA-0128`'s control group, `UTA-0087`,
`UTA-0081`, `UTA-0071` and `UTA-0077`.

**Filed the same day and all open:** `UTA-0129` the benchmark tool,
`UTA-0130` exits that are SHOT rather than walked into, `UTA-0131` the
hunt for whatever tool parked 58 MonsterEnds outside the world, and
`UTA-0132` the real-asset tier assuming the reference install.

Filed 2026-09-12 from UT_MonsterHunt's answer to `UTA-0134`: `UTA-0135`, the
exit's VERTICAL window, which is unmeasured and which MH-NivenSB's
correction rests on; `UTA-0136`, serialising the nav graph's reach-spec
flags from ut-dump, **which they have now asked for four times and want a
yes or no on** — they are flag-blind without it, and we already filter on
those same fields in `sceneOf`; and `UTA-0137`, a skipped census row not
saying whether the map was renamed or de-duplicated away — the two look
identical from the name, and resolving one by punctuation reads a DIFFERENT
map. Corrected by UT_MonsterHunt before anything was built on it.

`UTA-0130` is the one to read before touching routing. UT_MonsterHunt
found that `MonsterEndSB` reimplements `TakeDamage` gated on
`TriggerType == TT_Shoot`, so on some maps the exit is SHOT and standing
in its cylinder is not the win condition. `Scene::exits` carries only a
position and a collision size, so every routing test we have treats
reaching the exit as occupying that cylinder — and for such a map that
is wrong in the direction that makes a good map look unroutable. Census
`TriggerType` before widening any rule.

**`UTA-0126`'s eight are not that shape** — censused 2026-09-12, all
eight `monsterhunt.monsterend` with `TriggerType` 0, none `MonsterEndSB`.
This line previously said two of them were and had never been checked.
The corpus is still uncensused, which is what `UTA-0130` is for.

From the same source, and it raises what the ut-paths work is worth:
triggering the MonsterEnd is the WHOLE win condition, with no monster
count anywhere in it. A map that cannot be routed to its exit cannot be
finished at all.

`UTA-0077` ran the real-asset tier on Windows for the first time, on a
STOCK install — 96 maps, zero `MH-`. The readers came back clean on a
corpus they were not derived from: 96 of 96 maps parsed, 640,209 of
640,213 assertions. The three failures are `UTA-0132`, not readers. The
route is written up at `ut-ants-windows-uta0077` and is four commands,
so re-running it on any install is cheap. One Defender exclusion for
`C:\uta-test` was added on the Windows machine; that item carries the
command to undo it.

`UTA-0128` found MHEndPlace's rule SCATTERED against author-placed exits
— a median 7837 units off, within 4000 units on 30% of 471 maps, and
beaten by the node nearest the PlayerStart on a third of them. The
transcription reproduces both of UT_MonsterHunt's reference points
exactly, so that is a finding about the rule and not about our control
group. They accept it and have deployed MHEndPlace as an explicit
stopgap. Probes and per-map output are kept outside the repository, at
`ut-paths-output-uta0128`.

**Rule 1's set is not listed here — ask the roadmap**, which § Which item
comes next gives the call for. A hand-kept list of it goes stale the
moment an item is filed or closed, which is the same reason `In flight:`
is not kept by hand. What is worth recording is the standing deferral:
`UTA-0059` defers itself until the renderer lands. `UTA-0079` and
`UTA-0081` are `Source: in-session-`, so rule 1 does not reach them.
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
`docs/specs/UTA-0014-vulkan-draw-path.md` § 3 decision 6.

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
git config ants.gate.command   ./scripts/ci.sh   # unset: no gate runs
git config ants.gate.docsMode  --docs
git config ants.gate.docsGlob 'docs/*|*.md|LICENSE'
```

**A green push is not evidence the gate ran.** Four ways it passes having
checked nothing, and only two announce themselves: `NOTHING WAS CHECKED`
(the resolved hook is missing), a line naming a pipeline but no local gate
(`ants.gate.command` unset — the hook's fallback list does not contain
`scripts/ci.sh`), **no hook output at all**, which on this machine
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

**A local green is one leg of three.** GitHub runs GCC, Clang and MSVC;
a local run uses whatever `CXX` resolves to, and the gate says which at
the start and the end. `CC=clang CXX=clang++ ./scripts/ci.sh` runs a
second leg with whatever clang is installed — which is not CI's leg: the
matrix pins `clang-19`, and this machine has no `clang-19` binary at all.
**Flip a roadmap item on the matrix, never on the local leg.**

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
that separation is what **S7** is measured on.

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

The user's standing priority order, given 2026-09-04:

1. Outstanding fixes from any review — test, debt, codebase or document,
   including backlogged ones.
2. Open roadmap items that reach v1.0.0.
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

### Running two sessions at once

Two Claude Code sessions may work this project simultaneously, and **two
is the cap** — therefore at most two items in flight. `workflow.md` § 1
allows several sessions from separate worktrees, each holding one item,
and sets no ceiling. **The ceiling of two is this project's addition.** It
is stricter than § 1 rather than a departure from it, so it needs no
override file.

**This is the only home for these rules; do not restate them elsewhere.**
A second copy was tried and retired — it diverged from this one on every
loop of its own review gate.

There is no orchestrator: nothing schedules the sessions, and neither can
block the other.

**The roadmap store is what keeps them apart, and reaching it takes one
deliberate step.** The store is keyed to the MAIN checkout. A worktree is
a different path, the store has no row for it, and `roadmap_log` there
silently falls back to patching that worktree's own `ROADMAP.md` — which
the next render from the main checkout overwrites, taking the claim with
it and reporting nothing. Measured; see
[`docs/session-coordination-history.md`](docs/session-coordination-history.md).

> **So every roadmap verb passes the MAIN checkout as `caller_cwd`,
> whatever worktree you are working in**, and **only the main checkout
> ever commits `ROADMAP.md`.** A worktree session that commits it commits
> a stale render.

**Seven rules, and the first is what makes the rest work.**

1. **Flip the item to 🚧 BEFORE starting work, and name yourself in the
   note.** That flip is the claim, and it is how the other session learns
   the item is taken. **The marker records that an item is held and not by
   whom**, so put your session name (`ListAgents` reports it on its first
   line, above the peer list) in the progress note — without it a 🚧 left
   by a dead session is indistinguishable from a live claim, and rule 5
   makes 🚧 outlive a session deliberately.

   **Name the checkout you are working in as well.** Nothing on this
   machine reports which checkout a session occupies, so this note is the
   only place that fact exists — and rule 3 needs it to tell a second
   session from a first.
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
   lists the held items; rule 1 put the holder's session name and checkout
   in each one's progress note. `ListAgents` then says which of those names
   is live. A named holder that is absent has abandoned its item, and rule
   2 lets you resume it.

   **Neither command counts this project's sessions, which is why the
   test is written this way.** `ListAgents` is machine-wide, so its count
   is the machine's sessions and not this project's. `git worktree list`
   enumerates the worktrees that EXIST and reports no occupancy at all —
   the main checkout is listed whether or not a session sits in it.

   **So no command answers "is the main checkout free?", which is why rule
   1 has the holder write its checkout down.** A live holder whose note
   names the main checkout means you are the second session: take a
   worktree.

   **An empty in-progress list is NOT evidence that the main checkout is
   free.** Rule 1 records a checkout on a HELD item, and a session between
   items holds none — which is state 4, the ordinary state of this project.
   So the roadmap is silent while somebody is sitting in the main checkout,
   and a session reading that silence as permission breaches this rule
   while following its check exactly.

   **Default to a worktree when you cannot rule out a live peer.** Take the
   main checkout when `ListAgents` shows no live session that could be
   working this project — by name, or by asking one. That is a judgement
   and this rule cannot remove it: nothing on this machine reports
   occupancy, so *I am the first session* is concluded, never measured.

   **Do not take a worktree merely because you are unsure and alone.** A
   worktree session cannot merge into `main` — git refuses a second
   checkout of one branch — cannot commit `ROADMAP.md` (rule 7) and cannot
   advance `Next:` (rule 6). Every one of those needs a session in the main
   checkout. So a sole session that relocates leaves those jobs with
   nobody. If you took a worktree and no peer ever appears, move back.

   **What this route cannot do is detect a breach.** It finds a holder
   that named itself. A session that takes an item without flipping it,
   or flips it without naming itself, is invisible to it. So the cap
   rests on rule 1 being followed, and rule 1 is what makes this rule
   work at all.

   `.git/config` is shared across worktrees, so `core.hooksPath` and the
   three `ants.gate.*` settings apply in a new one without being set
   again. Verified, not assumed — a missing gate is silent, which makes
   this the thing most likely to be taken on trust.

   **A worktree brings its own branch, because git refuses to check one
   branch out twice.** That also rules out merging *into* `main` from the
   worktree: `main` is checked out elsewhere, so a local push at it is
   refused by git's checked-out-branch protection. So the worktree
   session **pushes its own branch to `origin`** and the session holding
   `main` merges it. The `pre-push` gate sits on that push, measured from
   a worktree rather than inferred from the settings being present. Its
   build directory is its own too, which is why the two do not fight over
   `build/`.
4. **Take items that do not share a directory** — and this outranks the
   priority order, as rule 2 does. Where the next item § Which item comes
   next would give you shares a directory with one in flight, take the
   next one that does not, and say why in the flip note. The roadmap
   stops two sessions taking the same item; it does not stop them editing
   one file from two items. `Lanes:` is the cheap signal.
5. **Clear 🚧 when the item is settled** — ✅ when it is done on the
   matrix, back to 📋 when it is abandoned. **A session boundary is not one
   of those**: work still under way stays 🚧 across it, which is
   `workflow.md` § 1's state 5 persisting.

   **That 🚧 does not reserve the item, and rule 2 is what decides.** Once
   your name is gone from `ListAgents` the item is resumable by anyone,
   yourself included. What the marker buys is not a claim but a state: it
   says half-built rather than not started, so whoever picks it up knows
   there is work already there. 📋 would lose that.
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

### History

Kept outside this file, because every session pays for every line here on
every turn and reads this history almost never. Split by kind:

| File | What it holds |
|---|---|
| [`docs/build-and-test-lessons.md`](docs/build-and-test-lessons.md) | What each § Build and test rule cost — the vacuous tests, the ASan fixture, the red MSVC leg, the invisible invariants |
| [`docs/session-coordination-history.md`](docs/session-coordination-history.md) | What was measured about worktrees, the roadmap store and the push gate |
| [`docs/claude-md-history.md`](docs/claude-md-history.md) | How this document changed, and what it used to say |
| `docs/claude-md-review-<date>.md` | The contract-gate loop logs, one record per run, rows numbered continuously across them |

**Rules live here; their evidence lives there.** A rule in this file is
meant to be followed without reading its history. Follow the link before
deciding a rule no longer earns its place — each was paid for once
already.
