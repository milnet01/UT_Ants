# UTA-0172 — `ut-dump`: emit each actor's event wiring

**Status:** accepted (2026-09-21). **No `review-contract` gate ran**, by the
user's explicit decision of 2026-09-21: the schema's real check is empirical,
against UT_MonsterHunt's measured verdicts for four maps, rather than a cold
read of prose. Recorded here because a reader must be able to tell an ungated
document from a converged one.
**Kind:** feature.
**Source:** ROADMAP UTA-0172 (ut-monsterhunt-2026-09-17 GAME-0145).

**Pairs with:** UT_MonsterHunt's GAME-0145, which is the consumer and supplied
this document's ground truth.
**Related:** UTA-0203 (`packages[]` ordering), UTA-0012 (`ut-dump` has no
output-shape contract at all — § 11).

**Layman:** Write down which switches and triggers in a map can turn on which
others, so a map whose exit can never open is found by reading the map file
instead of opening the old editor.

---

## 1. Goal

A consumer reading `ut-dump`'s output alone can decide, for any map, whether
anything in that map is capable of switching a given actor on.

## 2. Problem

`ut-dump` already emits a `wiring` key, and it carries only
`{nodes, edges, dangling}` — three counts and a list of unmatched event names.
Counts cannot answer a reachability question about a specific actor, because
the identity of the actors is exactly what they discard.

So UT_MonsterHunt's exit survey reads T3D exports instead. Two costs, both
theirs and both real: a T3D export is a separate artifact that goes stale when
the map changes, and five of the maps in their survey have none at all, so
those maps cannot be checked.

The information is already in the package. `buildWiringGraph` reads it and
then throws the identities away.

## 3. Scope decisions (agreed with the user)

**Provenance, because the heading is this project's fixed section name and
claims more than happened here.** Decision 6 is the user's, taken 2026-09-21.
Decisions 2 and 4 follow from evidence UT_MonsterHunt supplied as this
document's consumer. Decisions 1, 3 and 5 are the session's calls, made
against the standards named beside each, and none has been put to the user.

1. **This document covers the wiring key only.** `ut-dump`'s output shape as a
   whole has no contract — it is mentioned in eight specs in `docs/specs/` and
   is the subject of none. That is UTA-0012's, and § 11 records it. Folding it
   in here would breach one-spec-per-id (`spec-format.md` § 2).

2. **No MonsterHunt concept enters `ut-dump`.** The tool is a general package
   inspector. It does not know what an exit is, does not special-case a class
   name, and applies no class filter when deciding whose events to emit. The
   consumer's rule stays in the consumer. This is why § 4.4 emits an ancestry
   chain rather than a boolean.

3. **Behind a flag.** The per-actor array is opt-in via `--wiring-graph`,
   mirroring the existing `--nav-graph`. § 4.6 gives the measured reason.

4. **Names verbatim, in the case stored.** No folding, no normalisation.
   Emitting verbatim is the only choice that lets one consumer match
   case-insensitively while another matches exactly; normalising on emit
   destroys information no consumer can recover. § 4.5 carries the measurement
   that makes this concrete.

5. **The oracle is the file at level load, not the running game.** Everything
   here is read from the package. A map whose exit is wired at runtime by
   something the file does not show will read as unwired. UT_MonsterHunt's own
   rule decides from the live level at first Tick, and the two can disagree;
   grading that disagreement is out of scope (§ 9) and is recorded on their
   GAME-0145.

6. **No cold-read gate.** The user decided on 2026-09-21 that this document is
   written but not put through `review-contract`, because its schema is graded
   empirically against UT_MonsterHunt's measured per-exit verdicts rather than
   by a cold read of prose. The Status line and § 12 record it so an ungated
   document is never mistaken for a converged one.

## 4. Design

### 4.1 The flag

`--wiring-graph` adds `actors` to the existing `wiring` object. Absent, the
output is byte-identical to today's.

It composes with `--ndjson` exactly as `--nav-graph` does, and is independent
of it: either, both or neither.

### 4.2 Where it goes

Inside the existing `wiring` object, beside the current keys:

```json
"wiring": {
  "nodes": 12,
  "edges": 9,
  "dangling": [{"class": "Trigger", "event": "missiondone"}],
  "chainsUnresolved": 0,
  "actors": [ ... ]
}
```

`wiring` is already `null` for a package whose wiring graph did not build.
That is unchanged: `actors` and `chainsUnresolved` exist only where `wiring`
is an object, and only under `--wiring-graph`.

`chainsUnresolved` is the number of emitted actors whose `chainEnd` is not
`"root"` — the count of actors this map could not fully classify. A consumer
can derive it from `actors`, and it is stated separately so that a report can
say how much of the map it could not resolve instead of implying full
coverage. UT_MonsterHunt asked for it after their GAME-0160 taught them the
cost of a silently partial population.

### 4.3 One element

One element per emitted actor, in export-table order:

```json
{
  "index": 417,
  "name": "MonsterEnd0",
  "class": "MonsterEnd",
  "classChain": ["MonsterEnd", "Trigger", "Triggers", "Actor", "Object"],
  "chainEnd": "root",
  "tag": "MonsterEnd",
  "events": {
    "Event": "opensesame",
    "OutEvents": {"1": "MissionDone"},
    "BumpEvent": "",
    "PlayerBumpEvent": "",
    "FirstHatePlayerEvent": "",
    "MonsterEndTag": ""
  },
  "bInitiallyActive": false,
  "initialState": ""
}
```

- `index` — the actor's position in the package's export table. **This is the
  identity**; see the warning below.
- `name` — the export's own object name, as stored. Needed so a consumer can
  exclude an actor that names its own tag; without it, an exit whose own
  `Event` equals its own `Tag` reads as switched on by something.

  **`name` is NOT unique within a map, and a consumer must not key on it.**
  Measured 2026-09-21 by UT_MonsterHunt: MH-MJD_FIX3 holds two `MonsterEnd0`
  actors, three SkyWars maps hold two `Counter7` each, and MH-ZombieCorridor-BP
  holds two `Dispatcher8`. A reader keying by name silently merges them, and a
  self-naming exclusion written against `name` excludes the wrong actor. Use
  `index`.
- `class` — the leaf class name, as stored.
- `classChain` — § 4.4.
- `chainEnd` — § 4.4.
- `tag` — the actor's `Tag`, after the defaults merge of § 4.5. This is the
  join key: every other actor's event properties are matched against it.
- `events` — the six naming properties. A property with no value is `""`
  rather than absent, so a consumer never has to distinguish "unset" from
  "key missing".
- `OutEvents` — an **object keyed by decimal index as a string**, not an
  array, and holding only the indices that have a value. § 4.5 says why the
  index is load-bearing and why this is not a flat value.
- `bInitiallyActive` — after the same defaults merge, and **`null` where the
  actor's class family has no such property at all**. It is not `false` there.
  The `""`-not-absent convention above is about the string fields; collapsing
  "no such property" into `false` would make it identical to "switched off",
  and switched-off-ness is half of the consumer's never test. A consumer must
  treat `null` as unknown.
- `initialState` — after the same merge, `""` when unset. Emitted because it
  is free from the merge; no consumer reads it today.

### 4.4 The class chain, and its honesty field

`classChain` is the actor's class and its ancestors, leaf first, from
`uta::upkg::readAncestry` (`src/upkg/Class.h`). The § 4.3 example is the
measured chain for a real `MonsterEnd`, taken from the implementation's own
output on MH-UM-TeamFight rather than written from memory — an earlier draft
of this document guessed `NavigationPoint` and was wrong: a MonsterEnd
descends from `Trigger`. It is emitted rather than any
name test because the class-name population is neither closed nor
case-consistent — § 4.5's census found six spellings, one of them differing
only in case.

`readAncestry` can stop for three reasons, and `Ancestry::end` says which.
`chainEnd` reports it as `"root"`, `"packageMissing"` or `"classMissing"`.

**This field is not bookkeeping.** `Class.h` states that an ancestry ending
`PackageMissing` or `ClassMissing` still merges over the part that resolved —
"honest but incomplete". So a chain that does not reach a given base class
means either that the actor does not descend from it, or that the walk ran out
of packages before finding out. A consumer that cannot tell those apart will
read a truncated chain as a negative answer. `chainEnd` is what makes the two
distinguishable, and a consumer must treat anything other than `"root"` as
"unknown", not "no".

### 4.5 Reading a property that was never stored

A UE1 actor stores only the properties whose values differ from its class's
defaults, and a class's defaults are themselves a difference against its
parent's. So the value an actor actually has comes from merging the chain,
which is `uta::upkg::effectiveDefaults` — the same route UTA-0101 took for
monster capacity.

**This is not an optimisation; the common case depends on it.** The exits in
all four of UT_MonsterHunt's never-maps carry `Tag` = `MonsterEnd`, which is
the class default and is stored on none of them. An implementation reading
stored properties only would emit `""` for every one of those tags and the
consumer's rule would collapse.

Three measurements shape this section. Each is UT_MonsterHunt's, from their
T3D survey, except the census, which is ours.

**The census.** Over the install's Maps directory:

```sh
for m in "$MAPS"/*.unr; do ut-dump --system "$SYS" --ndjson "$m"; done \
  | python3 -c '...count classCounts keys matching /monsterend/i...'
```

Population: **1441 installed map packages**, `ut-dump` as the reader.
`MonsterEnd` 2089, `AdvancedMonsterEnd` 8, `MonsterEndTrigger` 6,
`SBMonsterEndTrigger` 3, `monsterend` 2, `MovingMonsterEnd` 2.

That population was settled on 2026-09-21 rather than assumed. UT_MonsterHunt
first reported different figures from a sweep of their T3D export directory;
on comparing, they found it unfiltered — it included 79 exports for maps that
are not installed, and missed 11 installed maps that have no export at all.
They asked for this census to be the one quoted. Their `MonsterEndTrigger`
count was low by the two `-LIFT` variants for exactly that reason.

Two of the six spellings are not exits despite containing the word. One,
`monsterend`, differs from another only in case, and that one is scope
decision 2's sharpest evidence:

**The lowercase spelling is a migration trap, not a present defect.** It
appears in MH-MayhemCastleV2 and MH-MayhemCastleV2-BP. UT_MonsterHunt's survey
reads both correctly today, because their T3D export normalises the class name
while `ut-dump` reports it as stored — the two readers answer slightly
different questions and neither is wrong. But a consumer migrating from the
export to this output with a case-sensitive suffix test starts returning "no
exit" for those two maps, while every other map keeps working. A regression
that looks like a clean migration is the worst shape available, and emitting
the ancestry chain (§ 4.4) is what removes it.

**The index is not zero.** In MH-3072-FloorWaysSBMod a Dispatcher names the
exit through `OutEvents(1)`, not `OutEvents(0)`. An implementation emitting
only the first element, or flattening `OutEvents` to a single value, reads that
map as unwired when it is wired. Hence the keyed object in § 4.3.

**The case differs across the join.** In the same map, `OutEvents(1)` holds
`MissionDone` and the exit's `Tag` is `missiondone`. Both sides are emitted
verbatim (scope decision 4) and the consumer folds case.

### 4.6 Which actors are emitted

**Every actor in the export table is emitted. There is no filter.**

The array is large — MH-GolgothaAL_fix has 2905 actors — and that is what
scope decision 3's flag is for, mirroring `--nav-graph`.

A filter was specified here until 2026-09-21: emit an actor only when `Tag` or
one of the six events is non-empty after the merge. It was dropped on the
consumer's objection, and the reasoning is worth keeping because it is not
obvious.

UT_MonsterHunt's never test is `never == len(exits)` — the exit count is a
**denominator**. Any filter that can drop an exit shrinks that denominator, so
a map with one never exit and one dropped live exit reads as never: a false
never, which is their dangerous direction, arriving silently. They measured
that it cannot happen today — across 1324 exported maps carrying exits, zero
exits have an empty `Tag`, and after the merge `Tag` defaults to the class
name — so the filter was safe in fact.

But their correctness would have depended on a property of our filter that
neither document stated. They asked for an exemption for exit classes; that
is refused under scope decision 2, because it puts a MonsterHunt concept in a
general tool. Removing the filter altogether answers the objection without
the concept, and costs almost nothing precisely because their measurement
shows the filter was dropping almost nothing.

## 5. Invariants

- **INV-1** — Without `--wiring-graph`, output is byte-identical to the same
  invocation before this change.
  *Test:* `tests/unit/DumpCliTest.cpp`: run the CLI over a fixture with and
  without the flag; assert the no-flag output contains no `"actors"` key and
  that `wiring` still carries `nodes`, `edges` and `dangling`.
  *Breaks when:* the array is emitted unconditionally, or the flag changes any
  other key.

- **INV-2** — A property never stored on an actor is emitted with its class
  family's default value, not empty.
  *Test:* `tests/unit/DumpCliTest.cpp`: a synthetic package whose actor class
  defaults `Tag` to a known string and whose actor stores no `Tag`; assert the
  element's `tag` is that string.
  *Breaks when:* the implementation reads stored properties only, or merges by
  name index rather than by name text — which `Class.h` INV-8 records as
  silently failing across a package boundary.

- **INV-3** — `OutEvents` preserves each value's index, and an index above 0
  survives.
  *Test:* `tests/unit/DumpCliTest.cpp`: an actor storing `OutEvents(1)` and no
  `OutEvents(0)`; assert `events.OutEvents` is `{"1": "..."}` with no `"0"`
  key.
  *Breaks when:* `OutEvents` is flattened to one value, emitted as a dense
  array that renumbers, or truncated at a fixed width.

- **INV-4** — Names are emitted in the case stored, unfolded, for `name`,
  `class`, `tag` and every event value.
  *Test:* `tests/unit/DumpCliTest.cpp`: an actor whose `Tag` is `missiondone`
  and whose `Event` is `MissionDone`; assert both spellings survive and differ.
  *Breaks when:* either end is lower-cased on emit, which would make the two
  compare equal here and destroy a distinction a consumer may need.

- **INV-5** — `chainEnd` reports the walk's true terminal state, and a
  truncated chain is distinguishable from a complete one.
  *Test:* `tests/unit/DumpCliTest.cpp`: an actor whose parent class lives in a
  package the resolver does not supply; assert `chainEnd` is `"packageMissing"`
  and that `classChain` holds the part that did resolve.
  *Breaks when:* `chainEnd` is always `"root"`, or a truncated chain is emitted
  as though complete — the defect § 4.4 exists to prevent.

- **INV-6** — Every emitted element's JSON is valid UTF-8 whatever bytes the
  package holds.
  *Test:* covered by `tests/unit/ToolsJsonTest.cpp` (UTA-0202), since every
  string here goes through `writeJsonString`; plus a `DumpCliTest.cpp` case
  with a Latin-1 byte in an actor's `Tag`.
  *Breaks when:* a name is written to the stream by any route other than
  `writeJsonString`.

- **INV-8** — Every actor in the export table is emitted; no actor is
  filtered out.
  *Test:* `tests/unit/DumpCliTest.cpp`: a synthetic package holding an actor
  with no `Tag` and no event of any kind; assert it still appears in `actors`,
  and that the array's length equals the package's actor count.
  *Breaks when:* any emission filter is reintroduced — which would make the
  consumer's exit-count denominator depend on it, per § 4.6.

- **INV-9** — `bInitiallyActive` is `null`, not `false`, where the actor's
  class family has no such property.
  *Test:* `tests/unit/DumpCliTest.cpp`: an actor whose class chain never
  declares `bInitiallyActive`; assert the field is `null`. A second actor that
  declares it `False` asserts `false`, so the two are distinguishable.
  *Breaks when:* the absent case is defaulted to `false`, making "no such
  property" and "switched off" the same value — half of the consumer's never
  test.

- **INV-7** — The four maps UT_MonsterHunt's T3D survey calls never-maps are
  the maps for which their rule, applied to this output, returns never.
  *Test:* not a unit test — a grading run over the 1430-map intersection,
  described in § 7.
  *Breaks when:* any of `tag`, `index`, `class` or the `OutEvents` index is
  wrong or missing, since their rule is a join over exactly those.

## 6. Failure modes

- **A package with no Level.** `wiring` is already `null` there and stays so.
- **An ancestry the resolver cannot complete.** Not an error; `chainEnd` says
  so (INV-5).
- **A malformed property block.** `readProperties` already reports it; the
  actor is skipped and the rest of the array is emitted, matching how the
  existing keys behave. It is not a whole-file failure.
- **An `OutEvents` index beyond any expected width.** No cap is applied. The
  roadmap item said `OutEvents[0..7]`; UT_MonsterHunt match `OutEvents(\d+)`
  with no bound and have not checked whether the library exceeds 7. A cap
  belongs to the property format, not to a consumer's habit.

## 7. Tests

`tests/unit/DumpCliTest.cpp` carries INV-1, INV-2, INV-3, INV-4, INV-5,
INV-6, INV-8 and INV-9, driving `runCli` over
synthetic packages built by `tests/support/UnrealPackageBuilder.cpp`, in the
pattern that file already uses. No new test binary.

INV-7 is a grading run, not a unit test, because its fixture is the install:

1. `ut-dump --wiring-graph --ndjson` over every map in the install.
2. Apply UT_MonsterHunt's rule to the output: an actor whose `classChain`
   reaches a class named `MonsterEnd` (case-insensitively) is an exit; an exit
   is never when `bInitiallyActive` is false and no **other** actor, by
   `index`, has a `tag` matched by any of its event values, compared
   case-insensitively.
3. **Restrict the assertion to the 1430 maps that also have a T3D export.**
   Assert the never set there is exactly MH-(RTNP)Abyss(SB),
   MH-GolgothaAL_fix, MH-UM-TeamFight and MH-UM-TeamFight-BP.
4. **The remaining 11 maps are a finding, not a grade.** Any never map among
   them is verified by hand and reported; it is not a failure.

**Step 3's restriction is load-bearing and was added on the consumer's
objection.** The four never-maps were computed over 1430 maps, not 1441: the
T3D survey reads only maps that have an export, and 11 installed maps have
none (their GAME-0160). Ten of those 11 carry real MonsterEnd actors —
MH-Slime-UTP has three; Doom][-BP-UTP, Minotaur-BP-UTP and Santa-BP-UTP have
two each — so a run over all 1441 would classify ten maps the reference set
has never seen. If any is a never map, this output is **right** and an
unrestricted assertion fails. A grade that fails on correct output is worse
than no grade, because the obvious repair is to doubt the output.

**Grade the never/not-never classification only, never per-map exit counts.**
UT_MonsterHunt found on 2026-09-21 that their own exit counts are unsound:
MH-MJD_FIX3's T3D export contains one actor twice, both named `MonsterEnd0`,
so their survey reports two exits where `ut-dump`'s `classCounts` reports one.
Ours is the correct count. It changes no verdict on that map, but it means
their counts are not a reference to assert against.

All five maps named in this section were confirmed present in the install on
2026-09-21 (`test -f "$MAPS/<name>.unr"`). This is not pedantry: the lists
UT_MonsterHunt originally supplied for the other classes came from their
unfiltered export sweep and named maps that are not installed, so a grading
set taken on trust could have asserted over files that are not there.

MH-3072-FloorWaysSBMod is the canary: it must **not** be in that set, and it is
the map that fails if `OutEvents` loses its index or the join is
case-sensitive.

## 8. Alternatives considered (and rejected)

- **Emit a boolean `isExit` per actor.** Rejected: it puts a MonsterHunt
  concept in a general package inspector (scope decision 2), and it freezes a
  rule that has already changed once — six class spellings are known and there
  is no reason to think six is final.
- **Filter to actors whose Tag or events are non-empty.** Specified until
  2026-09-21 and removed; § 4.6 carries the reasoning. It made the consumer's
  exit-count denominator depend on an unstated property of our filter.
- **Exempt exit classes from that filter instead of removing it.** The
  consumer's own proposal, refused under scope decision 2: it is a MonsterHunt
  concept inside a general package inspector, and the § 4.5 census shows the
  class list is the part that keeps moving.
- **Key an actor by `name`.** Rejected: names are not unique within a map
  (§ 4.3), so a consumer keying on them silently merges actors.
- **Emit unconditionally rather than behind a flag.** Rejected on § 4.6's
  measurement: most actors carry a default `Tag`, so the array is roughly the
  actor count, and most runs do not want it.
- **Fold case on emit so the join is trivial.** Rejected per scope decision 4:
  it destroys a distinction no consumer can recover, to save the consumer one
  function call.
- **`OutEvents` as a dense array.** Rejected: MH-3072-FloorWaysSBMod stores
  index 1 and not index 0, so a dense array either renumbers — changing which
  index a value had — or carries nulls, which is the keyed object with extra
  steps.

## 9. Out of scope

- The `ut-dump` output-shape contract as a whole —
  `docs/specs/UTA-0012-ut-dump-output-shape.md`, which cites this document
  for `wiring.actors` and `wiring.chainsUnresolved`.
- `packages[]` ordering — UTA-0203.
- Deciding an exit's state in a **running** level. This document's oracle is
  the file at load (scope decision 5). Grading file-level against first-Tick
  verdicts is recorded on UT_MonsterHunt's GAME-0145, unscheduled.
- Any change to `buildWiringGraph`'s existing counts or `dangling` list.

## 10. What checks this

| Invariant | Checked by |
|---|---|
| INV-1 | `tests/unit/DumpCliTest.cpp` |
| INV-2 | `tests/unit/DumpCliTest.cpp` |
| INV-3 | `tests/unit/DumpCliTest.cpp` |
| INV-4 | `tests/unit/DumpCliTest.cpp` |
| INV-5 | `tests/unit/DumpCliTest.cpp` |
| INV-6 | `tests/unit/ToolsJsonTest.cpp`, `tests/unit/DumpCliTest.cpp` |
| INV-8 | `tests/unit/DumpCliTest.cpp` |
| INV-9 | `tests/unit/DumpCliTest.cpp` |
| INV-7 | § 7's grading run over the install — nothing in CI |

INV-7 is checked by nothing automated, deliberately: its fixture is a 1441-map
install that no CI leg has. It is run by hand before the item ships, and the
result recorded on the roadmap item.

## 11. Cross-doc impact

- **Superseded 2026-09-25:** UTA-0012 now has its spec,
  `docs/specs/UTA-0012-ut-dump-output-shape.md`. What follows records the
  state when this document was written.
- **UTA-0012 has no spec, and this document does not create one.** `ut-dump`
  is mentioned in eight documents under `docs/specs/` and is the subject of
  none, so its output shape — `packages`, `classCounts`, `nav`, `surfaces`,
  `levelInfo`, `levelSummary`, `monsters`, `wiring` — is uncontracted. The only
  written description anywhere is `docs/ut-dump-output-shape.md` in
  UT_MonsterHunt's repository, which binds nothing here. This spec adds one
  key to that shape and inherits the gap for everything else.
- **`docs/specs/UTA-0121-bot-path-seeds.md` § 4.8** names `tools/common/Json.h`
  as the one escaper. Unchanged and still true; INV-6 depends on it.
- **UT_MonsterHunt GAME-0145** is the consumer. Their `analysis/exitsurvey.py`
  moves from T3D exports to this output.

## 12. Cold-eyes loop log

`docs/reviews/UTA-0172-actor-event-wiring-loop-log.md`. It is **empty**: no
review loop has run, by the user's decision recorded in the Status line above.

## 13. Resource cost

`--wiring-graph` costs one ancestry walk and one defaults merge **per class**,
not per actor. Measured over the full install (1441 maps), 2026-09-21:

| Run | Wall clock |
|---|---|
| No flag | ~35 s |
| `--wiring-graph`, ancestry walked per actor | 55 m 30 s |
| `--wiring-graph`, ancestry cached per class | 6 m 15 s |

**The per-class cache is not an optimisation to consider later; it is the
difference between a usable tool and an unusable one.** A map's actors share
very few classes — MH-UM-TeamFight has 1758 actors — so walking per actor
repeats the same chain resolution thousands of times. `writeMonsters` has
cached on the same `(package, raw class reference)` key since UTA-0101, and
the first implementation of this section failed to copy it.

The two runs were compared as a verdict diff rather than assumed equivalent:
both return 1332 maps with exits and the same four never maps, so the cache
changes speed and nothing else.

One map, for a sense of the per-map cost: MH-UM-TeamFight, 1758 actors,
0.376 s and 624 KB of JSON.

## 14. Open questions

1. ~~**Does UT_MonsterHunt's 2160 figure include System packages?**~~
   **Resolved 2026-09-21: no.** Their sweep was unfiltered over their export
   directory, which holds 79 exports for maps that are not installed. They
   asked that § 4.5 quote our census instead, which it does.
2. **Is any `OutEvents` index in the library above 7?** Still unchecked. § 6
   declines to cap on the strength of that, which is the safe direction; the
   implementation emits whatever indices it finds, so a measurement over the
   sweep's output would settle it without further tool work.
