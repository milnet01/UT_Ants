# UTA-0006 — `unav`: the navigation graph and the event-wiring graph

**Status:** spec draft (2026-09-06).
**Kind:** implement.
**Source:** ROADMAP UTA-0006 (design-2026-09-03).
**Blocked by:** UTA-0057 for the reach-spec array, which shipped 2026-09-06.
UTA-0005 for class ancestry, which § 4.3's node filter needs.
**Blocker for:** UTA-0008 — `ubundle` states it depends on `unav` for its
model types, never the reverse. UTA-0012's third query. UTA-0025 and UTA-0028
consume the two graphs at runtime.

<!-- Layman -->
**Layman:** Two invisible maps every UT99 level already contains — where a
player can walk, and which switch opens which door — pulled out as data so the
game can use them without running the original engine.

## 1. Goal

Given a `Package`, `unav` returns two directed graphs over a level's actors:
the **navigation graph**, whose edges are the reach specs UTA-0057 reads, and
the **wiring graph**, whose edges are an actor's `Event` naming another
actor's `Tag`. It owns the graph types and the queries over them. It resolves,
joins and validates; it reads no bytes of its own.

## 2. Problem

`upkg` returns a level's parts and deliberately joins none of them.
`readLevel` in `src/upkg/Level.h` returns `reachSpecs` whose `start` and `end`
are `ObjectReference`s to actors, and UTA-0057 INV-5 keeps a resolved graph
out of that reader on purpose. `readProperties` in `src/upkg/Properties.h`
returns an actor's tagged properties, `Tag` and `Event` among them, with no
notion that one names the other. So both graphs are present in what `upkg`
already returns and neither is expressed.

Nothing downstream can proceed without them. `ubundle` has no model type to
serialise, `uai` has no graph to route on, and UTA-0028's door-puzzle planner
has no way to know which switch opens which door.

### 2.1 What is measured, and what is not

Every figure below was produced on 2026-09-06 by scratchpad probes built
against `libuta_upkg`, over the reference install's `Maps` directory. The
probes are not repo source; § 7 tier 3 is where these measurements become
something the suite re-derives rather than something this document asserts.

**The navigation half is settled and UTA-0057 § 4.6a owns it.** A node's
`Paths` entries index the level's reach-spec array, and the spec at that index
has the listing node as its start. It holds for the overwhelming majority of
entries, and the residue is a small number of maps whose path network
disagrees with their own navigation points.

**The wiring half was believed when this item was filed and is measured
here.** The roadmap bullet asserted that "a button stores the tag of the door
it fires" without anything having read one. It is true, and three properties
of it change the design:

- **It resolves, at 94.9% of events.** **Command:** the wiring probe over the
  install's `Maps`, counting `Event` values matching at least one `Tag` in the
  same map; measured 2026-09-06. The rate is stated because § 7's assertion is
  built on it; the raw counts are not, because § 7 asserts no count.
- **It is many-to-many in both directions.** A tag is shared by several actors
  far more often than not — one switch opening a bank of movers — and a large
  number of events therefore reach more than one target. A design returning a
  single target per event is wrong about most of the library.
- **The remaining 5.1% dangle**, naming a tag no actor in that level carries.
  Same command and date. § 4.5 is what this spec does about them.

**One hypothesis was tested and is dead, and it removes a dependency.** UE1
gives `Actor.Tag` a class-default, so a dangling event might have been an
actor whose `Tag` is inherited rather than stored. Measured over the same
corpus: of the dangling events, only a handful name any actor's class name and
a handful more any actor's object name — the overwhelming majority match
neither. **So the dangle is not explained by inherited tags, and the wiring
graph does not need UTA-0005's `effectiveDefaults`.** It reads an actor's own
property list and nothing else. Had this gone the other way, every wiring
query would have had to walk ancestry per actor.

**What the corpus shows about shape, and it is not a claim about semantics.**
The classes that fire events most are `Trigger` and the stock monsters; the
class fired at most is `Counter`. That is the Monster Hunt kill-gate pattern
and it is why UTA-0061 and UTA-0028 want this graph. This spec reads it as
evidence that the wiring graph is worth extracting, not as a claim about what
any class *does* — resolving a custom class onto behaviour is UTA-0023's.

## 3. Scope decisions (agreed with the user)

### 3.1 Both graphs are one item — user, roadmap bullet 2026-09-03

The bullet names them together. They are one item because they share a node
identity (§ 4.2) and because a consumer wants to ask both questions of one
actor: *where can a bot walk from here, and what does standing here fire?*
Splitting them would put that join in whichever consumer needed it first.

### 3.2 The remaining calls are mine, with reasons

- **Node identity is the export index, as a plain integer** (§ 4.2), because
  that is the only currency both graphs already speak and because § 4.1's link
  split forbids an `upkg` type in the runtime library.
- **The subsystem ships two libraries, not one** (§ 4.1). This is not a
  preference: `docs/design.md` rule 2 forbids the runtime linking `upkg`, and a
  single library would breach it through this item.
- **Unresolvable edges are returned and marked, never dropped** (§ 4.5),
  following UTA-0057 INV-4's reasoning: a level with a broken edge must not be
  indistinguishable from a level that never had one.
- **The graphs own their strings** (§ 4.6), because `ubundle` serialises them
  after the `Package` they were read from may be gone.

## 4. Design

### 4.1 Layout and the build

**`src/unav/` builds TWO libraries, and the split is forced by
`docs/design.md` rather than chosen here.** Design rule 2 says neither runtime
target links `upkg` — *"the game cannot read a `.unr` file even by accident,
because the code to do so is not in it"* — while rule 6 makes `uai` depend on
`unav` and rule 3 names `unav` a vocabulary shared across the bake/runtime
seam. A single `uta_unav` linking `uta_upkg` would therefore pull the package
reader into `ut-ants`, breaching rule 2 through this item.

Rule 17 is what the split follows: `unav` owns the graph **types**, and
design.md already routes the builders through the baker — its subsystem table
has `ubake` *"drives `upkg`, `umat` and the graph builders"*.

- **`uta_unav`** — the types of § 4.1 and the queries of § 4.7. Links
  `uta_core` and nothing else. This is what the runtime links.
- **`uta_unav_build`** — `buildNavGraph` and `buildWiringGraph`. Links
  `uta_unav` and `uta_upkg`. Bake-side only; no runtime target may link it.

Each `CMakeLists.txt` asserts its own closure at configure time, as
`src/upkg/CMakeLists.txt` already does for UTA-0003's INV-13. The
runtime-closure test design rule 2 names does not exist yet — no runtime
target does — and building it stays with whichever item first ships one.

```cpp
// src/unav/Graphs.h -- links uta_core only
struct NavNode {
    std::uint32_t exportIndex = 0;  // the actor's slot in the package's
                                    // export table -- SS 4.2
    std::string className;          // owned -- see SS 4.6
};

struct NavEdge {
    std::uint32_t from = 0;     // index into NavGraph::nodes
    std::uint32_t to = 0;
    std::int32_t distance = 0;
    std::int32_t collisionRadius = 0;
    std::int32_t collisionHeight = 0;
    std::int32_t reachFlags = 0;
    bool pruned = false;
};

struct WiringEdge {
    std::uint32_t from = 0;     // index into WiringGraph::nodes
    std::uint32_t to = 0;
    std::string event;          // the Tag this edge was matched on
};

struct DanglingEvent {
    std::uint32_t from = 0;
    std::string event;          // named a Tag no actor in this level carries
};
```

**No member of any type above is an `upkg` type**, which is what lets
`uta_unav` link `uta_core` alone. An actor is named by its export index rather
than by `ObjectReference`, which lives in `src/upkg/Package.h`; § 4.2 is why
that index is the right identity anyway, and § 4.6 is why the strings are
owned. `ubundle` serialising a plain integer and a string is also strictly
easier than serialising a package-reader type.

The builders live in the other library and take what `upkg` already returns
rather than re-reading it:

```cpp
// src/unav/Build.h -- links uta_unav and uta_upkg, bake-side only
[[nodiscard]] Result<NavGraph>    buildNavGraph(const Package&, const Level&,
                                                const PackageResolver&);
[[nodiscard]] Result<WiringGraph> buildWiringGraph(const Package&);
```

`buildNavGraph` takes the `Level` UTA-0057 returns because re-reading it here
would be a second decoder of a layout that spec owns.

### 4.2 One node identity, and why it is the export index

Both graphs identify an actor by its position in the package's export table,
carried as a plain `std::uint32_t`. A reach spec already names its endpoints
that way — UTA-0057 § 4.5 derived `start` and `end` as object references to
actors, and `ObjectReference::index()` is that position — and an actor
carrying a `Tag` is an export. Nothing else is common to both.

**The builder converts, and the graph stores the bare index**, because § 4.1's
link split forbids an `upkg` type in the runtime library. That conversion is
the one place the off-by-one in `ObjectReference` is handled, which is the
reason UTA-0003 made it a type rather than a bare integer.

**It is deliberately not a position in `Level::actors`.** That array drops
null slots (UTA-0004 INV-9), so its indices do not survive the drop and cannot
be joined against a reach spec's endpoints. This is the hazard UTA-0057 § 4.1
recorded when it decided the returned type must be able to express what the
file stores.

### 4.3 Which actors are nodes

**Navigation graph:** every export whose class descends from
`NavigationPoint`. UTA-0057 § 4.6 measured why this must walk ancestry — no
export of the literal class carries a `Paths` entry, and the set of
subclasses does not close, so a name list cannot be complete. That is
UTA-0005's walk and it is why this item is blocked by it.

**Wiring graph:** every export carrying an explicit `Tag` or `Event`. No
ancestry walk, per § 2.1's dead hypothesis. The two node sets overlap and are
not nested: a `PathNode` may carry a `Tag`, and a `Mover` is a wiring node
and never a navigation one.

### 4.4 Where the edges come from

**Navigation edges are the reach specs, not the `Paths` properties.** The
array is the level's own edge list; `Paths` is a per-node index into it.
Building from the array visits each edge once, and UTA-0057 § 4.6a measured
`Paths` as agreeing with it rather than as a second source. Reading both and
reconciling them would make this item own a disagreement that belongs to the
content.

**Wiring edges are `Event` matched against `Tag`, within one level.** A match
is exact on the name. Because a tag is shared (§ 2.1), one `Event` yields one
edge per actor carrying that tag.

### 4.5 Edges that do not resolve

Both graphs meet edges that name something absent, and neither is a refusal.

A reach spec whose `start` or `end` is not an export this graph holds as a
node is **dropped from the edge list and counted**. A dangling `Event` is
returned in `WiringGraph::dangling` rather than as an edge, because it is the
only record that an author wired something and the target went away.

**Neither is a `MalformedData`.** UTA-0057 § 6 already settled that a level
whose path network disagrees with its own nodes is content rather than a
layout defect, and this item inherits that rather than re-deciding it. A
builder that refused those levels would refuse maps the rest of the library
agrees are sound.

**But a count of zero is not the same as an absence**, which is why both are
counted rather than silently skipped: a consumer that gets an empty graph
needs to tell "this level was never pathed" from "every edge was discarded".

### 4.6 The graphs own their strings

A class name or a tag is a `std::string` held by the graph, not a name-table
index and not a `std::string_view`.

`Package::name` returns a `std::string_view` whose lifetime is the
`Package`'s, and `ubundle` serialises these types after the package they came
from may be closed. § 4.1's link split is the second reason and it is the
harder one: the runtime library may not name an `upkg` type at all, so a
name-table index is not available to it even in principle. `upkg` returns references unresolved for exactly that reason
(UTA-0004 § 4.1); a graph is where the resolving happens, so it is also where
the ownership has to change. The cost is one copy per node, paid once at build
time, and § 13 bounds it.

### 4.7 The queries

The bullet gives this item "the queries over them". Three, because three are
what the blocked consumers ask:

```cpp
std::span<const NavEdge>    edgesFrom(const NavGraph&, std::uint32_t node);
std::span<const WiringEdge> firedBy  (const WiringGraph&, std::uint32_t node);
std::span<const WiringEdge> firing   (const WiringGraph&, std::uint32_t node);
```

Edges are stored grouped by source so `edgesFrom` and `firedBy` are a span
into storage rather than a search. `firing` — who fires *at* this actor —
needs the reverse grouping, so the graph carries both orders. **Whether that
second order earns its storage is § 14's open question**, and it is asked
there rather than settled here because `ubundle` fixing the bytes is what
makes it expensive to change.

**No pathfinding here.** Routing across a level is UTA-0060's and a planner is
UTA-0028's. This item returns the graph they route on; a search algorithm in
this file is a second place for a routing decision to live.

## 5. Invariants

- **INV-1** — Every edge in a returned graph has `from` and `to` in range for
  that graph's own node list.
  *Test:* `tests/unit/NavGraphTest.cpp` builds a level whose reach specs name
  an actor that is not a navigation node, and asserts the returned edge list
  holds no edge referencing it while the discard count reports it.
  *Breaks when:* the builder maps a spec's endpoint through the wrong index
  space — `Level::actors` rather than the export table (§ 4.2) — at which
  point every edge on a sparse map points at the wrong actor. This fixture
  isolates that rule: the level's actor array is built with null slots so the
  two index spaces disagree, and no other rule rejects it.

- **INV-2** — A reach spec whose endpoint is not a node is counted, and the
  count is non-zero only when such a spec was seen.
  *Test:* `tests/unit/NavGraphTest.cpp`, two fixtures — one whose specs all
  resolve, asserting the count is zero, and one with a single unresolvable
  endpoint, asserting it is one.
  *Breaks when:* the builder drops an edge without counting, which makes a
  level whose specs were all discarded indistinguishable from one that stated
  none — the shape UTA-0057 INV-4 exists to prevent, arriving one layer up.

- **INV-3** — A `WiringEdge` exists for every actor carrying the tag an
  `Event` names, not merely the first.
  *Test:* `tests/unit/WiringGraphTest.cpp` builds one actor whose `Event`
  names a tag three actors carry, and asserts three edges.
  *Breaks when:* the builder resolves a tag to a single target. § 2.1
  measured that a large share of events reach more than one actor, so this
  breach loses real edges on most maps while every fixture with a unique tag
  still passes. This fixture isolates that rule: the three targets differ only
  in sharing the tag, so nothing else can reject it.

- **INV-4** — An `Event` naming a tag no actor carries appears in `dangling`
  and produces no edge.
  *Test:* `tests/unit/WiringGraphTest.cpp` builds an actor whose `Event` names
  an absent tag, and asserts one `dangling` entry and no edge.
  *Breaks when:* the builder either invents an edge to nothing or discards the
  event silently. § 2.1 measured that a minority of real events dangle, so
  silent discard loses the only record that an author wired something.

- **INV-5** — The graphs hold no view into the `Package` they were built from,
  and no member of any graph type is an `upkg` type.
  *Test:* declared reading check — `src/unav/Graphs.h` is read against this
  clause; no member is a `std::string_view`, a `std::span`, a pointer into
  package storage, or a type declared in `src/upkg/`.
  *Breaks when:* a member is changed to a view to save a copy, at which point
  `ubundle` serialises freed memory; or an `ObjectReference` is stored for
  convenience, which makes `uta_unav` need `uta_upkg` and breaches
  `docs/design.md` rule 2 the moment a runtime target links it. No fixture can
  demonstrate the absence of a member that was never added, which is UTA-0057
  INV-5's reasoning and UTA-0005 INV-3a's precedent.

- **INV-6** — `uta_unav` links `uta_core` and nothing else, and `unav` reads
  no package bytes of its own.
  *Test:* a configure-time assertion in `src/unav/CMakeLists.txt` on the first
  half, in the form `src/upkg/CMakeLists.txt` already uses for UTA-0003's
  INV-13; a declared reading check on the second — `src/unav/` is read against
  this clause and constructs no `ByteReader`.
  *Breaks when:* a query needs a property nobody passed in and the easy fix is
  either to link `uta_upkg` from the type library — which the configure-time
  assertion catches — or to re-read the export inside the builder, which it
  does not, and which gives one format two decoders.

## 6. Failure modes

| Condition | Result |
|---|---|
| A reach spec's endpoint is not a navigation node | Edge discarded and counted — § 4.5, INV-2. Not a refusal |
| An `Event` names a tag no actor carries | Recorded in `dangling` — § 4.5, INV-4. Not a refusal |
| A level with no reach specs at all | An empty navigation graph, which is what the file states. UTA-0057 already refuses a level whose tail it cannot read, so an empty array here is content |
| An actor's property list does not parse | Inherited from `readProperties`; that layer is UTA-0003's |
| The ancestry walk cannot reach `NavigationPoint` because the install lacks a package | `AncestryEnd::PackageMissing`, which UTA-0005 INV-7 makes a successful end. The actor is not a navigation node and the reason is legible |
| `readLevel` refuses the level | Propagated. This item adds no refusal of its own to a layout UTA-0057 owns |

## 7. Tests

**Tier 1 — unit, always on.** `tests/unit/NavGraphTest.cpp` and
`tests/unit/WiringGraphTest.cpp`, against `UnrealPackageBuilder` and the
`LevelExportWriter` UTA-0057 added. Covers INV-1, INV-2, INV-3 and INV-4,
each of which needs content constructed to be wrong in one named way — an endpoint that resolves
to nothing, a tag carried by exactly three actors, an event naming an absent
tag. Real content supplies none of those on demand.

**Tier 2 — declared reading checks.** INV-5 and INV-6, for the reason
UTA-0057 § 7 gives: no fixture demonstrates the absence of a member or of a
call that was never added.

**Tier 3 — real assets, off by default.** `tests/real/RealInstallTest.cpp`
under `-DUTA_REAL_ASSET_TESTS=ON`. It builds both graphs for every map and
**prints the figures § 2.1 asserts** — nodes, edges, discarded endpoints,
events, resolved, dangling. That is the point of the tier here: § 2.1's
numbers become an output of the suite rather than a transcription in this
document, so nobody re-derives them by hand and a drift shows up as a changed
line rather than as a stale sentence.

**What it asserts is a population and two rates**, following UTA-0057 § 4.6a
rather than inventing a second convention: the run fails if it built no graph
or resolved no event, and if the share of events that resolve falls below 90%
— a floor under § 2.1's measured 94.9%, wide enough that ordinary growth in
the library does not reach it. The same shape guards the share of reach specs
whose endpoints resolve to nodes. A
magnitude floor would need re-tuning as the library grows; a rate does not,
and a builder using the wrong index space does not lose a few percent but
nearly all of them.

## 8. Alternatives considered (and rejected)

- **Build navigation edges from `Paths` rather than from the array.** Rejected:
  it makes this item own the disagreement UTA-0057 § 4.6a measured between the
  two, which is content rather than something a builder can resolve.
- **One graph type with an edge kind.** Rejected: the two edge payloads share
  no field, so the merged type is a union with a tag and every consumer
  switches on it to reach anything.
- **Resolve tags through class defaults.** Rejected on measurement, not on
  taste — § 2.1's hypothesis test showed inherited tags explain almost none of
  the dangle, so the walk would cost an ancestry resolution per actor and
  recover almost no edges.
- **Return name-table indices instead of owned strings.** Rejected twice over:
  `ubundle` outlives the `Package`, so every consumer would have to keep one
  alive to read a tag — and § 4.1's link split means the runtime library cannot
  name an `upkg` type at all, so the index has nothing to be resolved against
  on that side of the seam.
- **Refuse a level with a dangling event.** Rejected: it refuses a real share
  of the library over content the rest of the library agrees is sound.

## 9. Out of scope

- Pathfinding and route planning — UTA-0060, UTA-0028.
- Serialising either graph — UTA-0008 owns their bytes.
- Interpreting what a class *does* when fired — UTA-0023.
- The collision radius and height a reach spec carries are passed through
  ungraded; UTA-0057 § 10 records that finding an independent source for them
  falls to this item, and § 15 keeps it open.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/NavGraphTest.cpp` — the sparse-actor-array fixture |
| INV-2 | `tests/unit/NavGraphTest.cpp` — both directions |
| INV-3 | `tests/unit/WiringGraphTest.cpp` — the shared-tag fixture |
| INV-4 | `tests/unit/WiringGraphTest.cpp` — the absent-tag fixture |
| INV-5 | A declared reading check. **Nothing** mechanical stops a member becoming a view later |
| INV-6 | Its first half is caught mechanically — the configure-time link assertion in `src/unav/CMakeLists.txt` fails the build if `uta_unav` gains a dependency. Its second half is a declared reading check: **nothing** stops a `ByteReader` inside the builder, where linking `uta_upkg` is legitimate |
| `docs/design.md` rule 2 — that no runtime target links `upkg` through `unav` | **Nothing yet.** The closure test that rule names needs a runtime target and none exists; § 4.1's split is what makes the rule satisfiable, not what enforces it |
| § 2.1's measured figures | `tests/real/RealInstallTest.cpp`, off by default — it prints them, so they are an output rather than a transcription |
| The collision radius and height | **Nothing.** They are passed through from UTA-0057, which grades them nothing either; tracked by § 15 |
| That an edge means what the engine means by it | **Nothing here.** The graph is checked for structure, never against the running game. UTA-0057's roadmap bullet records an in-engine ground-truth offer, and taking it is UTA-0025's |

**Four of the ten rows say `nothing`, and that is this item's honest error
budget.** Graded: the four structural invariants, on every ordinary run, which
is better than UTA-0057 managed because these fixtures need no install.
Ungraded: both declared reading checks, the collision fields passed through
from UTA-0057, and whether an edge means what the engine means by it.

The shape of the gap is worth naming rather than just counting. Everything
about the graphs' **structure** is checked, and nothing about their
**meaning** is. A builder that produces a well-formed graph of the wrong edges
passes every row above, and the only thing that would catch it is the
in-engine comparison § 14 leaves open.

## 11. Cross-doc impact

- **UTA-0057's roadmap bullet** records that this item was blocked on it; the
  block is discharged and the bullet already says so.
- **UTA-0008** states it depends on `unav` for its model types. This spec is
  what that dependency resolves to, and § 4.1's types are the surface.
- **`docs/design.md` needs no change, and checking that is what found § 4.1's
  link split.** It already defines `unav` — *"owns their types and their
  queries; `ubundle` owns how they are written to a file"* — and its rules 2, 3,
  6 and 17 together decide the library structure this spec adopts. This spec
  conforms to that document; it does not amend it.
- **CHANGELOG** on ship.
- **UTA-0060 and UTA-0028** gain a named blocked-by on this item.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0006-navigation-and-wiring-graphs-loop-log.md`.

## 13. Resource cost

Two new static libraries (§ 4.1): a types-and-queries library linking
`uta_core`, and a bake-side builder linking it and `uta_upkg`. No new external
dependency, and no new dependency for any runtime target.

The graphs are held in memory per level and are bounded by the level's own
size: one node per navigation point or tagged actor, one navigation edge per
reach spec, one wiring edge per matching tag pair. § 4.6's owned strings cost
one class name per node and one tag per wiring edge. The many-to-many
measurement of § 2.1 is what makes the wiring edge count worth stating
separately — it exceeds the event count, because one event yields an edge per
target.

**The largest map in the reference install is what bounds the worst case**,
and § 7 tier 3 prints the per-map maxima rather than this document asserting
them.

## 14. Open questions

- **Nothing checks a reach spec's collision radius and height**, here or in
  UTA-0057. They are returned and consumers bind to them. The in-engine
  ground-truth offer UTA-0057's bullet records is the obvious source; whether
  it is worth taking is UTA-0025's call, and this spec does not spend it.
- **Whether `firing` — the reverse wiring query — earns its storage.** It
  doubles the wiring graph's edge storage and no consumer named in § 1 has
  asked for it yet; UTA-0028's planner is the likely first. Building it is
  cheap to defer and expensive to retrofit into a serialised format, which is
  why it is asked here rather than after `ubundle` fixes the bytes.
