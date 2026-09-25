# UTA-0012 — `ut-dump`: the output-shape contract

**Status:** accepted (2026-09-25). **No `review-contract` gate ran**, by the
user's decision of 2026-09-25: the consumer checked every key its code reads
against § 4, and INV-4 fails on any key change the list does not carry.
Recorded here so an ungated document is never mistaken for a converged one.
**Kind:** implement.
**Source:** ROADMAP UTA-0012 (design-2026-09-03), scoped by the user
2026-09-21.

**Pairs with:** UT_MonsterHunt's `docs/ut-dump-output-shape.md`, the draft
this document takes as input (scope decision 1), and their GAME-0124, the
second reader § 4.10 names.
**Related:** UTA-0172 owns `wiring.actors`. UTA-0203 (`packages[]` order) is
settled by § 4.3. UTA-0189 and UTA-0198 add keys under § 4.9's rule.

**Layman:** Write down exactly what the map-inspection tool prints and what
each part means, so the programs that read it keep working when the tool
changes.

---

## 1. Goal

A consumer can read `ut-dump`'s output from this document alone. It knows
which keys it may rely on, what each means, how a package is identified, and
when a change to the tool can break it.

## 2. Problem

`ut-dump` is mentioned across `docs/specs/` and is the subject of none of
them. So its output has no contract, though UT_MonsterHunt builds its map
checks on it. The only written description is their draft. It records what
they observed, not what we guarantee.

Three gaps follow, each measured or reported:

- **Order.** `packages[]` comes back sorted by path, not in argument order.
  Nothing tells a consumer (UTA-0203).
- **Silent drops.** An actor whose class chain cannot be walked to the root
  is not a navigation node. `unav`'s `descendsFromNavigationPoint` returns
  false for it. Nothing in the default output counts such actors, so an
  undercounted graph reads as a small one.
- **Versioning.** `schema` has stayed 1 while `surfaces`, `monsters`,
  `levelInfo`, `levelSummary` and `wiring.actors` were added. No rule says
  whether that was right.

## 3. Scope decisions (agreed with the user)

**Provenance.** Decision 1 and the requirements in decisions 3, 5 and 6 are
the user's, from 2026-09-21, recorded on ROADMAP UTA-0012. Decisions 2, 4 and
7 are the session's calls, made against the evidence named beside each, and
are put to the consumer in this draft.

1. **This project owns the contract; the consumer's draft is input.** Their
   document says what they saw. This one says what we promise. Where the two
   differ, this one is right and the difference is named (§ 8).

2. **Every key the tool emits is contracted.** Their draft pinned a narrow
   set and left `file`, `ok`, `bytes`, `exports` and `imports` out. But
   `file` is the only identity a package has, and an uncontracted key that a
   consumer reads is the problem this item exists to fix. So nothing emitted
   is incidental. A key is either in § 4 or not emitted.

3. **Identity, never position.** Every element that a consumer must match to
   something else carries its own identity. A package carries `file`. A
   navigation node carries `export`. A consumer that zips an array against its
   own list is outside this contract.

4. **Adding a key does not bump `schema`.** Removing, renaming or retyping a
   key, or changing what a key means, does. A consumer ignores keys it does
   not know. This makes the history right: every key added so far was
   additive (§ 4.9).

5. **Anything dropped is counted.** Where the tool reads something and drops
   part of it, the same object carries a count of what was dropped, whatever
   the reason. This is UT_MonsterHunt's GAME-0160 lesson made a house rule,
   not one key's habit. § 4.8 lists the counts, and adds the one missing.

6. **A second reader is named as a consumer.** UT_MonsterHunt's GAME-0124
   reads `--nav-graph` so that something other than the engine reads the
   file. § 4.10 lists what it binds to. Their reason: in a year, whoever
   maintains `ut-dump` will have forgotten a second reader exists.

7. **`classCounts` counts every export, not every actor.** Their open
   question 4. It answers "what is in this file", which a package with no
   Level can also answer. An actor-by-actor list is `wiring.actors`
   (UTA-0172), behind `--wiring-graph`.

## 4. Design

### 4.1 Invocation

```
ut-dump (--install <root> | --system <dir>) [--nav-graph] [--wiring-graph] [--ndjson] <package|directory>...
```

- `--install` names the install's root. Added by `UTA-0206`. It is searched
  through `ubake`'s `Install`, the code `ut-bake` uses: `System`, `Maps`,
  `Textures`, `Sounds` and `Music`, in the game's `Paths` order. Some maps'
  classes live in a texture package, which only this finds. With no `Core`
  package found, the tool writes the warning `--system` writes.
- `--system` names a directory scanned, not recursively, for `.u` files, by
  extension case-folded. They resolve class ancestry across packages. With
  none found, the tool writes a warning to stderr and continues.
- Giving both is a usage error.
- Either way, a class an import names under `UnrealI` that `UnrealI` lacks is
  found in `UnrealShare` (`UTA-0005` § 4.6).
- A directory argument contributes each regular file directly inside it. It
  is not recursed. Any regular file is dumped, whatever its extension.
- `--nav-graph` adds `nav.nodeList` and `nav.edgeList` (§ 4.6).
- `--wiring-graph` adds `wiring.chainsUnresolved` and `wiring.actors`
  (UTA-0172).
- The exit code is 0 when every package was attempted, including packages
  that did not open. 2 is a usage error, with no output on stdout.

### 4.2 The envelope

The document form:

```json
{"schema": 1, "packages": [ <package>, ... ]}
```

`--ndjson` writes the same objects one per line: first `{"schema":1}`, then
one package per line, each byte-equal to the document form's element with
layout removed. There is no end marker. Completeness is the exit code.

Whitespace and key order are layout, not contract.

### 4.3 Identity and order

- **A package is identified by `file`**: the path as walked, a directory
  argument joined with the entry's name. It is not canonicalised.
- **`packages[]` is in ascending path order**, as `std::filesystem::path`
  compares, over every file from every argument together. It is not argument
  order. This settles UTA-0203: the sort stays, because two runs over one set
  of files then produce the same document whatever order the files were named
  in.
- **A navigation node is identified by `export`**, its index in the
  package's export table. Its position in `nodeList` is meaningful only
  inside that one object, where `edgeList` refers to it.

### 4.4 One package

A package that did not open:

| Key | Type | Meaning |
|---|---|---|
| `file` | string | § 4.3 |
| `ok` | `false` | |
| `error` | string | `unreadable or empty` when no bytes were read, else `package did not open` |

No other key is present.

A package that opened carries these, in this order:

| Key | Type | Meaning |
|---|---|---|
| `file` | string | § 4.3 |
| `ok` | `true` | |
| `bytes` | integer | the file's size |
| `exports`, `imports` | integer | the lengths of the two tables |
| `importedPackages` | array of string | the distinct outermost package names in the import table, ascending bytewise, spelled as stored. A texture's group is not a package (UTA-0070) |
| `classCounts` | object, string → integer | for every export, the name of its class, counted. An export whose class reference is null, such as a class object, counts under `None`, and one whose class cannot be named counts under `?`. The counts sum to `exports` |
| `level` | object or `null` | § 4.5. `null` when the package has no `Level` export, and then no key below is present |

A package whose `Level` export does not read carries `level: null` and
`levelError`, a string, and no key below it.

Otherwise, after `level`: `surfaces` (and `surfacesError` when it is null),
`levelInfo`, `levelSummary`, `monsters`, `nav`, `wiring`, as § 4.5 to § 4.7
give them.

### 4.5 Level, surfaces, credits and monsters

- **`level`**: `actors`, the Level actor list's populated entries; `rawSlots`,
  its raw length; `reachSpecs`, the length of its ReachSpec array; and
  `chainsUnresolved`, § 4.8's new count. All integers.
- **`surfaces`**: `{total, byTextureAndFlags}`. `total` is the Model's
  surface count. `byTextureAndFlags` is one row per distinct (texture name,
  `polyFlags`) pair, ascending by that pair: `{texture, polyFlags, surfaces,
  drawnNodes}`. `texture` is the object name alone, not package-qualified.
  `drawnNodes` counts BSP nodes of three or more vertices that use the
  surfaces. `null`, with `surfacesError`, when the level names no Model
  export or the Model does not read (UTA-0201).
- **`levelInfo`, `levelSummary`**: `{title, author}`, each a string or
  `null`. The values are the map's own stored ones: `null` means the map sets
  none. `levelInfo` reads the LevelInfo named in the level's actor list;
  `levelSummary`, the first `LevelSummary` export. Either object is `null`
  when there is no such export or its properties do not read (UTA-0101).
- **`monsters`**: `{factories, capacity, unlimitedFactories,
  unknownCapacityFactories, placedPawns, unresolvedActors}`, all integers.
  The monster rule and the counting rule are UTA-0173's, in `writeMonsters`'
  own comment. This document pins the key names and the types.

### 4.6 `nav`

`null` when the navigation graph did not build. Otherwise `{nodes, edges,
discardedEndpoints, nodesWithNoExit}`, all integers:

- `nodes`, `edges`: `unav::buildNavGraph`'s node and edge counts.
- `discardedEndpoints`: reach-spec endpoints that resolved to no node. It
  counts endpoints, not specs.
- `nodesWithNoExit`: nodes with no outgoing edge.

With `--nav-graph`, also:

- **`nodeList`**: `[{export, name, class}]`, in ascending `export` order.
  `name` is the actor's object name. `class` is its class name, as stored.
- **`edgeList`**: `[{from, to, distance, collisionRadius, collisionHeight,
  reachFlags, pruned}]`, all integers. `from` and `to` are positions in this
  object's `nodeList`. The rest are the reach spec's own fields, undecoded and
  unfiltered. `pruned` is the file's byte, not a boolean.

An `edgeList` position is not the file's reach-spec index. The graph drops a
spec with an unresolved endpoint and groups the rest by `from` (UTA-0198).

### 4.7 `wiring`

`null` when the wiring graph did not build. Otherwise `{nodes, edges,
dangling}`. `nodes` and `edges` are integers. `dangling` is `[{class,
event}]`: `event` names a tag no actor carries, and `class` is the class of
the actor firing it. `?` when the class cannot be named.

With `--wiring-graph`, `chainsUnresolved` and `actors` follow. UTA-0172 owns
both. This document restates neither.

### 4.8 Counting what was dropped

Scope decision 5's rule, applied:

| What is dropped | Where it is counted |
|---|---|
| A package that does not open | its own element, `ok: false` |
| A reach-spec endpoint that resolves to no node | `nav.discardedEndpoints` |
| An actor or prototype whose class family cannot be sorted into the monster rule | `monsters.unresolvedActors` |
| An actor whose class chain does not reach the root | **`level.chainsUnresolved`** (new) |

**`level.chainsUnresolved`** counts the level's actors, each export once,
whose class chain does not end at the root. An actor with no class does not
count: it has no chain to cut short, and `wiring.actors` reports it with an
empty chain ending `root` (UTA-0172).
It is present without any flag, because the actors it counts are exactly the
ones the navigation graph silently leaves out. It equals
`wiring.chainsUnresolved` wherever both are emitted: same population, same
test.

**Not counted, and why:**

- A directory entry that is not a regular file. It is not a package, and a
  subdirectory is not walked (§ 4.1).
- A `--system` package that does not open. The resolver then answers "absent"
  for it, so every actor whose chain runs through it lands in
  `chainsUnresolved`. The count is per actor, not per package. That is the
  unit a consumer can act on.

### 4.9 Versioning

`schema` is an integer. It changes when a key is removed, renamed or retyped,
or when a key's meaning changes. Adding a key does not change it. A consumer
must ignore keys it does not know.

**Every key addition updates § 4 in the same change.** The key-set test
(INV-4) fails until it does. That test is what stops scope decision 2
decaying.

### 4.10 Consumers

Every UT_MonsterHunt reader also reads `schema`, `file` and `ok`. Listed by
them on 2026-09-25, checked by them against § 4 and UTA-0172 § 4.3 to § 4.4.

| Consumer | Reads | Why it matters |
|---|---|---|
| `analysis/exitsurvey.py` (GAME-0032) | `wiring.chainsUnresolved`; `wiring.actors`' `index`, `class`, `classChain`, `chainEnd`, `tag`, `events` (`OutEvents` as an index-to-name object) and `bInitiallyActive`, with `null` read as unknown | The exit survey. UTA-0172 contracts these fields |
| GAME-0124, the second reader (`analysis/mapcheck/facts.py`, `walking_adjacency`) | `edgeList`'s `from`, `to`, `reachFlags`, `collisionRadius`, `collisionHeight`, `distance`, `pruned`; `nodeList`'s `name`, `class` | It reads the file by a route other than the engine's. A change here breaks the only independent check on a path build |
| `analysis/mapcheck` checks | `nodeList`'s `name`, `class`; `edgeList`'s `from`, `to`, `reachFlags`, `collisionRadius`, `collisionHeight` (the route check); `classCounts` (paths, starts); `importedPackages` (load) | Per-map checks |
| `analysis/pathtriage.py` | `nav.nodes`, `nav.edges`, `nav.nodesWithNoExit`; `classCounts`; `wiring.dangling` | Path triage |
| `analysis/pathverify.py` | `classCounts`; `level.actors`; `nav.nodes`, `nav.edges`, `nav.nodesWithNoExit` | Path verification |
| `analysis/build_votedata.py` | `monsters.placedPawns`, `monsters.capacity`; `classCounts` | Map-vote data, monster totals (UTA-0173) |

A consumer is added to this table when it tells us what it reads.

## 5. Invariants

- **INV-1** — The document form is `{"schema": 1, "packages": [...]}`. The
  `--ndjson` form is the line `{"schema":1}`, then one line per package, each
  equal to the document form's element with layout removed.
  *Test:* `tests/unit/DumpCliTest.cpp`, the existing UTA-0145 case. It runs
  both forms over a directory holding a map, an empty file and a garbage
  file.
  *Breaks when:* the header is dropped or moved, a package spans two lines,
  or a line differs from its document element.

- **INV-2** — `packages[]` is in ascending path order whatever the argument
  order, and each element's `file` is the path it was read from.
  *Test:* `tests/unit/DumpCliTest.cpp`: name `B.unr` and `A.unr` as separate
  arguments, in that order. `packages[0].file` ends `A.unr` and
  `packages[1].file` ends `B.unr`.
  *Breaks when:* argument order is emitted, or `file` is dropped or
  canonicalised.

- **INV-3** — A package that does not open carries exactly `file`, `ok:
  false` and `error`. The run goes on to the next package and exits 0.
  *Test:* `tests/unit/DumpCliTest.cpp`: over an empty file, a garbage file and
  a map, the first two carry exactly those three keys, with the two `error`
  strings § 4.4 gives, and the map is still dumped.
  *Breaks when:* a failure aborts the run, exits non-zero, or leaks any other
  key.

- **INV-4** — An opened package's top-level key set is exactly § 4.4's, in
  each of its three cases: a non-map, a map, and a map with `--nav-graph` and
  `--wiring-graph`. `level`, `nav` and `wiring` carry exactly § 4.5 to § 4.7's
  keys.
  *Test:* `tests/unit/DumpCliTest.cpp`, with a small key reader local to the
  test that lists an object's keys at one depth. It runs over a fixture System
  package and a fixture map, and compares each key set with a literal list.
  *Breaks when:* a key is added, removed or renamed without the list changing
  — which is the point. The list is § 4 restated as code, so one cannot
  change without the other.

- **INV-5** — `classCounts` counts every export under its class name, a
  null class counting under `None`, and its values sum to `exports`.
  *Test:* `tests/unit/DumpCliTest.cpp`: a fixture map carrying a non-actor
  export beside its actors. Sum the values and compare with `exports`, and
  find the non-actor's class among the keys.
  *Breaks when:* only Level actors are counted, which misses the non-actor,
  or null-class exports are dropped, which breaks the sum.

- **INV-6** — `importedPackages` is the distinct outermost package names,
  ascending.
  *Test:* `tests/unit/DumpCliTest.cpp`: a fixture map importing a class from
  `Engine` and a texture `Pkg.Group.Tex`. The array holds `Engine` and `Pkg`,
  once each, in that order, and not `Group`.
  *Breaks when:* one outer link is walked, which yields `Group`; names
  repeat; or the order follows the import table.

- **INV-7** — Every `edgeList` element's `from` and `to` index this object's
  `nodeList`. Each element carries exactly § 4.6's keys.
  *Test:* `tests/unit/DumpCliTest.cpp`: the existing UTA-0136 cases, plus a
  key-set check on one `nodeList` and one `edgeList` element through INV-4's
  reader.
  *Breaks when:* an edge carries export indices instead of positions, or one
  of GAME-0124's fields is renamed or dropped.

- **INV-8** — `level.chainsUnresolved` counts the level's actors, each export
  once, whose class chain does not reach the root, and equals
  `wiring.chainsUnresolved` under `--wiring-graph`.
  *Test:* `tests/unit/DumpCliTest.cpp`: a fixture map with an actor whose
  class lives in a package the fixture System lacks, named in two Level
  slots, and an actor with no class. Against the same map without those two,
  `level.chainsUnresolved` rises by exactly 1, without any flag. Under
  `--wiring-graph` it equals `wiring.chainsUnresolved`.
  *Breaks when:* the key is emitted only under a flag; a resolved actor is
  counted, which the equality catches; a doubly-slotted actor is counted
  twice; or a classless actor is counted.

## 6. Failure modes

- **A file that is not a package.** INV-3. A directory of maps holding a
  stray text file costs one `ok: false` element, not the run.
- **No `--system` packages.** A stderr warning. Every actor whose chain leaves
  the map is then unresolved, and `level.chainsUnresolved` says how many.
- **A Level export that does not read.** `level: null` plus `levelError`, and
  nothing after it. The package's other keys stand.
- **A navigation or wiring graph that does not build.** That key is `null`.
  The package's other keys stand.

## 7. Tests

`tests/unit/DumpCliTest.cpp` carries INV-1, INV-2, INV-3, INV-4, INV-5,
INV-6, INV-7 and INV-8, driving `runCli` over
synthetic packages built by `tests/support/UnrealPackageBuilder.cpp`, as the
file already does. INV-1 and INV-7 extend existing cases. The rest are new.
No new test binary and no JSON library: INV-4's key reader is local to the
test and reads keys at one depth.

## 8. Alternatives considered (and rejected)

- **Pin only the consumer's narrow set** (their § 1). Rejected under scope
  decision 2: `file` is the identity and was left out, and `surfaces`,
  `monsters` and the credits are read today with no contract.
- **Emit `packages[]` in argument order** (UTA-0203's first option).
  Rejected: two runs naming the same files in different orders would produce
  different documents. And with `file` present, order carries no information
  a consumer needs.
- **Bump `schema` on every added key.** Rejected: every consumer's version
  check would then fail on a change that cannot break it. It also rewrites
  history, since `schema` stayed 1 through five additions.
- **Separate actors from other exports in `classCounts`** (their open
  question 4). Rejected under scope decision 7: it changes an existing key's
  meaning, which would bump `schema`, to answer a question `wiring.actors`
  already answers.
- **Count unopened `--system` packages instead of unresolved actors.**
  Rejected in § 4.8: a package count says something failed, not what it cost.
  The per-actor count is what a consumer can act on, and it covers every
  cause at once.

## 9. Out of scope

- `wiring.actors` and `wiring.chainsUnresolved`: UTA-0172.
- New keys already requested: node locations and exits (UTA-0189), and per-node
  `Paths` (UTA-0198). Each lands under § 4.9 and updates § 4.
- A human-readable mode. There is no reader for one.
- The consumer's open questions 1 and 2, about a residue against T3D exports
  and four maps with empty path graphs. They are questions about `upkg`'s
  reading, not about this shape.

## 10. What checks this

| Rule | Checked by |
|---|---|
| INV-1 to INV-8 | `tests/unit/DumpCliTest.cpp`, a unit test on every CI leg |
| § 4.9: a key change updates § 4 | **Partial:** INV-4 fails on an unlisted key, so the test list must change. Nothing checks that § 4's prose changes with it |
| § 4.9: `schema` is bumped when a meaning changes | **nothing**. A changed meaning with an unchanged key set passes every test |
| § 4.10: the consumer table is complete | **nothing**. It is complete only as far as consumers tell us |

## 11. Cross-doc impact

- **UTA-0172 § 11 and § 9** say UTA-0012 has no spec. They become stale when
  this is accepted and gain a pointer here.
- **UTA-0203** closes with this document and INV-2.
- **UT_MonsterHunt's `docs/ut-dump-output-shape.md`** stops being the only
  description. They decide what to do with it. We tell them where this lives.
- **`ut-dump --help`** gains one sentence saying `packages` is in path order
  and to key on `file`.
- **`CHANGELOG.md`**: an entry for `level.chainsUnresolved`.

## 12. Cold-eyes loop log

`docs/reviews/UTA-0012-ut-dump-output-shape-loop-log.md`. Empty: no review
loop has run, by the user's decision recorded in the Status line.

## 13. Resource cost

`level.chainsUnresolved` needs each actor class's chain walked to the root
without any flag. `writeMonsters` already resolves each distinct class once
per map and records whether its chain completed (`ActorKind::resolved`), so
the count can reuse that cache and add no walk.

Measured 2026-09-25: the no-flag `--ndjson` run over the install's `Maps/`,
old binary and new, alternated twice. Old 265 s and 305 s, new 198 s and
290 s, with other work on the machine. So no growth is visible above the
noise. Across every package the two outputs differ only by
`level.chainsUnresolved`.

## 14. Open questions

1. ~~**Does the consumer accept contracting every key?**~~ **Resolved
   2026-09-25: yes.** UT_MonsterHunt raised no objection to scope decisions 1
   to 5, and extended § 4.10.
2. ~~**Do they want `level.chainsUnresolved`?**~~ **Resolved 2026-09-25:
   keep it, always on**, by UT_MonsterHunt, because it names the loss without
   a flag. `monsters.unresolvedActors` is a different count: `writeMonsters`
   skips an unresolved ScriptedPawn and adds a factory's unresolved
   prototype. Measured 2026-09-25 with `ut-dump --wiring-graph --ndjson` over
   the install's `Maps/`, reading `wiring.chainsUnresolved`: four maps are
   non-zero. On MH-SPNaliRescue the unresolved actors are stock items —
   `Barrel`, `Health`, `NaliFruit`, `Clip` — whose classes the resolver
   cannot find. That map is one of the consumer's four empty path graphs, so
   the count finds a real silent loss on its first run (ROADMAP UTA-0206).
