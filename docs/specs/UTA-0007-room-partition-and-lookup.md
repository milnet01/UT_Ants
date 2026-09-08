# UTA-0007 — `umap`: partition a level into rooms, and answer which room a point is in

**Status:** spec draft (2026-09-08).
**Kind:** implement.
**Source:** ROADMAP UTA-0007 (design-2026-09-03).

**Blocked by:** UTA-0069 — shipped 2026-09-08; it derived the `Model` tables
this reads.  **Blocker for:** UTA-0011 (`ubake` drives this), and the
level-map half of UTA-0016.

**Layman:** Chop a level into rooms so the in-game map has something to draw,
using the room divisions the original level already has — and answer, for any
spot in the level, which room you are standing in.

## 1. Goal

After this ships, a baked level carries a **room map**: a list of rooms
derived from the level's own BSP zones, each with a traced 2D outline and a
floor band, plus a lookup that answers which room any point in the level falls
in. `uui` has something to draw and `uworld` has something to ask; neither
needs the package reader, and no map is hand-authored.

## 2. Problem

`docs/design.md`'s subsystem table gives `umap` the level map — *"the
simplified, room-partitioned model of a level that the in-game map screen
draws, and the rule for which room a position falls in"* — and its
*Exploration is per-room* rule makes that partition the unit the server
records exploration against. **S12** is written against the map screen, so
nothing that rule describes can be built until rooms exist.

Nothing produces them today. `src/umap/` does not exist. The inputs do:
`readModel` in `src/upkg/Geometry.h` returns `Model::zones`,
`Model::nodes` and `Model::leaves`, and UTA-0069 shipped the last of those on
2026-09-08 — `Leaf::iZone`, whose doc comment records that it *"indexes the
Model's own zones"* on every leaf in the reference install.

Three consequences, in the order they bite:

1. **No map screen.** UTA-0016 walks a baked level; the map is part of what
   0.1.0 delivers.
2. **No exploration unit.** The server records a bit per player per room
   (design rule 18). Without rooms there is nothing to set a bit against.
3. **`ubake` has nothing to write.** UTA-0011 drives `umap` and writes its
   output into the bundle; design rule 17 makes the room model a
   bundle-format contract, so getting its shape wrong is a format version bump
   later.

## 3. Scope decisions (agreed with the user)

### 3.1 A room outline is traced from the level's own geometry — user, 2026-09-08

Asked with three options and a sketch of each. The user chose a traced
outline over axis-aligned rectangles and over a floor-only footprint, on the
ground that the map should read as the level rather than as a diagram.

This is the expensive choice and it was made deliberately: § 4.4 needs a
boundary trace, where rectangles would have been two lines of code.

### 3.2 Rooms are split into floor bands now — user, 2026-09-08

Same question, three options: flatten everything into one view, store the
height now and decide later, or split into floors now. The user chose to
split now, for towers and stacked bases where a flattened view is unreadable.

**This is the larger of the two, and § 4.5 is the section it creates.** The
cheaper middle option was offered and declined, so § 4.5 is not over-building.

### 3.3 The remaining calls are mine, with reasons

- **A room is exactly one zone** (§ 4.2). The design document already says
  the partition comes from the level's own zones; the measurement in § 4.2 is
  what shows that this yields a useful partition rather than a trivial one.
- **Zone 0 is not a room** (§ 4.3), on the measurement in § 4.2.
- **The outline is traced from sampled occupancy rather than from an exact
  polygon union** (§ 4.4, § 8).
- **`umap` declares its own point type** rather than reusing
  `uta::upkg::Vector3` (§ 4.1), which is forced by design rule 2.

## 4. Design

### 4.1 Layout and the build

**`src/umap/` builds TWO libraries, and the split is forced by
`docs/design.md` rather than chosen here** — the same argument
`docs/specs/UTA-0006-navigation-and-wiring-graphs.md` § 4.1 makes for `unav`,
and for the same two rules. Design rule 2 says neither runtime target links
`upkg`; design rule 17 says `umap` owns its model types while `ubundle` owns
their bytes. A single library linking `uta_upkg` would pull the package reader
into `ut-ants` through this item.

- **`uta_umap`** — the types below and the lookup of § 4.3. Links `uta_core`
  and nothing else. This is what the runtime links.
- **`uta_umap_build`** — `buildRoomMap`. Links `uta_umap` and `uta_upkg`.
  Bake-side only; no runtime target may link it.

**One directory with one `CMakeLists.txt` defining both targets**, asserting
both closures at configure time in the form `src/unav/CMakeLists.txt` already
uses. One directory rather than two, because two would make the split a
build-layout fact every test target binds to, where it is a link-closure fact.

**No member of any type below is a `upkg` type**, which is what lets
`uta_umap` link `uta_core` alone. `Vector3` is declared in
`src/upkg/Properties.h`, so it is unavailable here; `umap` declares `Point2`
instead. This mirrors UTA-0006 § 4.6, where the graphs own their strings for
the same reason, and it makes `ubundle`'s job easier rather than harder — a
pair of floats serialises more simply than a package-reader type.

```cpp
// src/umap/Rooms.h -- links uta_core only

/// A point on the map plane. `umap`'s own, because `upkg`'s Vector3 is not
/// linkable from here (design rule 2).
struct Point2 {
    float x = 0;
    float y = 0;
};

/// One room: a zone of the level, its traced outline, and where it sits.
struct Room {
    /// The zone index this room was built from, in the source Model's own
    /// numbering. Never 0 -- SS 4.3.
    std::uint32_t zoneIndex = 0;

    /// The traced boundary, in order, first vertex NOT repeated at the end.
    /// A room may also carry holes -- SS 4.4.
    std::vector<Point2> outline;
    /// Each hole is a closed ring inside `outline`.
    std::vector<std::vector<Point2>> holes;

    /// Vertical extent, in the level's own units.
    float minZ = 0;
    float maxZ = 0;

    /// Floor bands this room appears on -- SS 4.5. Never empty. More than
    /// one marks a room that connects bands, such as a stairwell.
    std::vector<std::uint16_t> floors;
};

/// A level's rooms, and the data the lookup needs.
struct RoomMap {
    std::vector<Room> rooms;
    /// Floor band boundaries, ascending. `bands[i]` is the lower edge of
    /// band i; band i covers [bands[i], bands[i+1]).
    std::vector<float> bands;
    /// Maps a source zone index to a position in `rooms`, or NO_ROOM.
    /// Sized to the source Model's zone table, so index 0 is present and
    /// always NO_ROOM.
    std::vector<std::uint32_t> roomForZone;
};

inline constexpr std::uint32_t NO_ROOM = 0xFFFFFFFFu;
```

**`RoomMap` carries no per-player and no exploration state, and that is a
security property rather than an omission** — see INV-7. Exploration is
`ugame`'s, recorded server-side per player per room; design rule 18 makes a
client that holds another team's exploration a wallhack. A `visited` flag on
`Room` would be serialised into the bundle every client holds, which is what
makes that rule unenforceable elsewhere.

The builder lives in the other library and takes what `upkg` already returns:

```cpp
// src/umap/Build.h -- links uta_umap and uta_upkg, bake-side only
[[nodiscard]] Result<RoomMap> buildRoomMap(const uta::upkg::Model& model,
                                           const RoomBuildOptions& options = {});
```

It takes the `Model` UTA-0069 returns rather than re-reading the package,
because re-reading it here would be a second decoder of a layout that spec
owns.

### 4.2 A room is a zone, and the measurement that says so is worth having

`docs/design.md` already fixes the partition — *"`umap` partitions a level
into rooms from the level's own zones"*. What it does not say is whether that
yields a useful partition on the actual map library, and a partition that
collapses to one room per level would satisfy the sentence and deliver
nothing.

Measured 2026-09-08 over the reference install's `Maps` directory, reading the
largest parsing `Model` in each `.unr` — which is where the level geometry
lives, as `tests/real/RealInstallTest.cpp` already records in the case that
walks every modelled export:

| Question | Answer |
|---|---|
| Maps walked / with a parsing `Model` | 837 / 836 |
| Maps whose zone table has more than one entry | 834 |
| Maps whose zone table has **exactly** one entry | 0 |
| Largest zone table | 64 |
| Leaves examined | 2 759 160 |
| Leaves naming zone 0 | 0 |
| Leaves whose `iZone` is outside its own zone table | 0 |
| Maps whose leaves name only ONE distinct zone | 26 |

The command is § 7's tier-3 case, which prints these figures rather than
having them transcribed here.

Four things follow, and each is used below:

1. **The partition is useful.** No map has a one-entry zone table.
2. **Zone 0 is reserved.** Not one leaf of 2 759 160 names it. This matches
   the engine's own `FBspNode::iZone` documentation, where the array is
   *"Visibility zone in 1=front, 0=back"* and index 0 is the null zone.
   Source: <https://beyondunrealwiki.github.io/pages/standard-unreal-object-defi.html>
3. **Leaf zone indices need no clamping**, only a refusal — nothing in the
   install is out of range, so a reader that refuses one costs nothing.
4. **26 maps partition to a single room.** That is a real output, not a
   failure; § 6 says what the map screen does with it.

The 64-zone ceiling is the engine's, documented on the Unreal wiki as *"A
level can only have 64 zones"*, and the measurement reaches it.
Source: <https://unrealarchive.org/wikis/unreal-wiki/Legacy:Zoning.html>

### 4.3 Which room a point is in

A descent of the BSP, which is what the tables are for. The conventions below
are the engine's own and are not inferred from the field names:
`FBspNode::iZone[2]` and `iLeaf[2]` are both indexed **1 = front, 0 = back**,
`iLeaf` uses `INDEX_NONE` for *not a leaf*, and `iFront`/`iBack` use
`INDEX_NONE` for *no child*.
Source: <https://beyondunrealwiki.github.io/pages/standard-unreal-object-defi.html>

```cpp
// src/umap/Rooms.h -- links uta_core only
/// The room containing `point`, or NO_ROOM.
[[nodiscard]] std::uint32_t roomAt(const RoomMap& map, Point3 point);
```

The descent, stated once so the implementation and § 5 agree:

1. Start at node 0. An empty node table is `NO_ROOM`.
2. At node `n`, the point is in FRONT when
   `dot(n.plane.normal, point) - n.plane.w >= 0`, and BACK otherwise. Let
   `side` be 1 for front and 0 for back.
3. Take `n.iFront` when `side` is 1, else `n.iBack`. If it is not
   `INDEX_NONE`, descend into it and repeat from 2.
4. Otherwise, if `n.iLeaf[side]` is not `INDEX_NONE`, the answer is the zone
   of `leaves[n.iLeaf[side]]`.
5. Otherwise the answer is `n.iZone[side]`.
6. Zone 0 is **not** a room: it resolves to `NO_ROOM`. Any other zone maps
   through `roomForZone`.

**The descent is bounded by the node count.** A `Model` whose child indices
form a cycle is malformed input from a file this project did not write, and an
unbounded descent on it hangs the bake rather than refusing it. Step 3 counts
its iterations and yields `NO_ROOM` past `nodes.size()`. INV-3 locks this.

**Points precisely on a plane take the front side**, by the `>= 0` above. The
choice is arbitrary and only has to be *stated*, so that a point on a shared
wall resolves to one room rather than to whichever the compiler's rounding
picked.

**The lookup is on `RoomMap` and not on `Model`**, which is what keeps it in
the runtime library. `RoomMap` therefore carries the descent tables it needs;
§ 4.6 says which, and that is the one place this design pays for design rule 2
in bytes rather than in structure.

### 4.4 Tracing a room's outline

Per § 3.1 the outline follows the level's geometry. It is derived by
**sampling occupancy and tracing the boundary**, not by an exact polygon
union of projected surfaces — § 8 records why.

For each room:

1. Take the room's zone extent in X and Y from the leaves that name it.
2. Sample a grid over that extent at `options.sampleSpacing` (default 32
   level units, one quarter of UT99's 64-unit nominal player width, so a
   doorway is several cells across). A cell is IN when a point at its centre,
   swept over the room's Z extent at the same spacing, resolves to this room
   by § 4.3.
3. Trace the boundary of the IN set with marching squares, producing one
   closed ring per connected boundary. The outermost ring is `outline`; any
   ring enclosed by it is a hole.
4. Simplify each ring with Ramer–Douglas–Peucker at
   `options.simplifyTolerance` (default half the sample spacing), which
   removes the staircase the grid introduces without moving a vertex more
   than that tolerance.

**Sampling reuses § 4.3's lookup as its primitive**, so the outline cannot
disagree with the lookup about which room a spot belongs to — the two would
otherwise be two independent answers to one question.

**Resolution is a stated approximation, not an accident.** A room narrower
than the sample spacing in both axes produces no IN cells and therefore no
outline; § 6 says what happens to it, and INV-6 makes that case observable
rather than silent.

### 4.5 Floor bands

Per § 3.2 the map screen shows one floor at a time, so rooms are assigned to
bands.

Bands are derived from the rooms, not from the geometry, because a "floor" is
a property of how rooms stack rather than of any surface:

1. Collect every room's vertical midpoint.
2. Cluster them on Z with single-linkage, splitting wherever consecutive
   sorted midpoints differ by more than `options.floorSeparation` (default
   128 level units, twice UT99's nominal player height, so a room and the
   gallery above it separate while a stepped floor does not).
3. Each cluster is a band. `bands` holds the lower edge of each, ascending.
4. A room joins **every** band its `[minZ, maxZ]` overlaps, so a stairwell or
   a lift shaft appears on each floor it connects rather than vanishing
   between them. INV-8 locks the never-empty half.

**A level with one cluster has one band**, which is the flattened view — so
the 26 single-room maps of § 4.2 and any single-storey level degrade to
exactly the cheaper option the user declined, without a second code path.

### 4.6 What `RoomMap` carries for the lookup

§ 4.3 runs against `RoomMap` in the runtime library, so the descent tables
travel with it. `RoomMap` gains:

```cpp
    /// The BSP planes and child links the descent needs, copied from the
    /// source Model at bake time. `umap`'s own types -- SS 4.1.
    struct Node {
        Point3 normal;
        float w = 0;
        std::int32_t iFront = 0, iBack = 0;   // INDEX_NONE = no child
        std::int32_t iLeaf[2] = {0, 0};       // 1=front, 0=back
        std::uint8_t iZone[2] = {0, 0};
    };
    std::vector<Node> nodes;
    /// leafZone[i] is leaves[i].iZone in the source Model.
    std::vector<std::uint8_t> leafZone;
```

**This is a copy, and the copy is the point.** The alternative is for the
runtime to hold a `upkg::Model`, which design rule 2 forbids. It is also
narrower than the source: the renderer's vectors, surfaces, lightmaps and
light references are not carried, because the lookup does not read them.

`Point3` is `umap`'s, declared beside `Point2` for the same reason.

## 5. Invariants

- **INV-1** — Every room in a `RoomMap` names a distinct zone index in
  `[1, model.zones.size())`, and no room names zone 0.
  *Test:* `tests/unit/RoomMapTest.cpp`, "a built room map names every zone but
  zero".
  *Breaks when:* the builder walks the zone table from 0, which yields a room
  no leaf can ever resolve to and shifts every subsequent `roomForZone` entry.

- **INV-2** — For every leaf in the source `Model`, a point inside that leaf
  resolves through `roomAt` to the room built from that leaf's `iZone`.
  *Test:* `tests/real/RealInstallTest.cpp`, "every leaf resolves to its own
  zone's room", over the install.
  *Breaks when:* the descent takes `iFront` on the back side — the front/back
  convention of § 4.3 is an array index, so swapping it compiles, returns a
  plausible room for most points, and is wrong for all of them.

- **INV-3** — `roomAt` returns within `nodes.size()` plane tests for every
  input, including a `RoomMap` whose child indices form a cycle.
  *Test:* `tests/unit/RoomMapTest.cpp`, "a cyclic node graph terminates".
  *Breaks when:* the descent trusts `iFront`/`iBack` without counting its
  iterations, and a malformed map hangs the bake instead of refusing.

- **INV-4** — A point resolving to zone 0, and a point in a `RoomMap` with no
  nodes, both return `NO_ROOM` rather than a room index.
  *Test:* `tests/unit/RoomMapTest.cpp`, "zone zero and an empty map are not
  rooms".
  *Breaks when:* `roomForZone[0]` is populated, which turns "outside the
  level" into a room the map screen draws and the server records exploration
  against.

- **INV-5** — `uta_umap`'s link closure is `uta_core` alone; `uta_umap_build`'s
  adds `uta_umap` and `uta_upkg`.
  *Test:* the configure-time assertions in `src/umap/CMakeLists.txt`, in the
  form `src/unav/CMakeLists.txt` uses.
  *Breaks when:* a convenience dependency is added to `uta_umap`, which
  breaches design rule 2 silently — nothing else in the build would fail.

- **INV-6** — Every room whose sampled occupancy is non-empty has an
  `outline` of at least three vertices, and every ring in `outline` and
  `holes` is closed without repeating its first vertex.
  *Test:* `tests/unit/RoomMapTest.cpp`, "every traced room has a closed
  outline", and the install case of INV-2 asserts it over real maps.
  *Breaks when:* a room is narrower than `sampleSpacing` in both axes, so the
  trace has nothing to walk. § 6 states the handling; this makes it visible.

- **INV-7** — No type in `src/umap/Rooms.h` carries per-player or exploration
  state.
  *Test:* `grep -nE '\b(visited|explored|seen|player|team)\b' src/umap/Rooms.h | grep -vE ':\s*(//|\*)'`
  returns nothing. **The comment filter is load-bearing, not decoration:** the
  doc comment in § 4.1 explaining why this state is absent uses the words
  themselves, so a clause matching every line would be falsified by the
  sentence that documents the rule. UTA-0004's INV-3 failed exactly this way
  and was repaired on 2026-09-08 under UTA-0074; this clause is written
  already knowing that.
  *Breaks when:* a `visited` flag is added to `Room` for convenience. It would
  then be serialised into the bundle every client holds, making design rule
  18's *"a client cannot reveal what the server declined to send"* false, and
  **S12** unmeetable — the wallhack that rule exists to prevent.

- **INV-8** — Every room's `floors` is non-empty, and every value in it
  indexes `bands`.
  *Test:* `tests/unit/RoomMapTest.cpp`, "every room sits on at least one
  floor".
  *Breaks when:* banding assigns by midpoint alone and a room whose midpoint
  falls in a gap between clusters is assigned to none, which drops it from the
  map screen entirely.

## 6. Failure modes

| Assumption | When it breaks | What happens |
|---|---|---|
| The `Model` parses | A version-61 package (UTA-0072) | `buildRoomMap` returns the reader's error unchanged; `ubake` refuses that map and says which |
| The zone table has more than one entry | 26 maps in the install name one distinct zone | One room covering the level. Drawn, and legitimately so — the level really is one zone |
| A room is wider than the sample spacing | A crawlspace or a thin trim volume | No outline. The room is kept, with an empty `outline`, and reported in the build result so `ubake` can log it; `uui` skips a room with no outline rather than drawing a degenerate one |
| Child indices are in range | Malformed or hostile file | The descent's iteration bound returns `NO_ROOM` (INV-3). The build refuses a `Model` whose `iZone` is out of range (§ 4.2 measured none) |
| Rooms stack into distinguishable bands | A ramped level with no flat floors | One band. The map degrades to the flattened view, which is § 4.5's stated behaviour rather than a defect |

## 7. Tests

Three tiers, matching the project's existing split.

1. **`tests/unit/RoomMapTest.cpp`** — fixtures built by hand, locking INV-1,
   INV-3, INV-4, INV-6 and INV-8. The fixtures are small `RoomMap` and
   `Model` values written in the test, not real packages, so each case
   isolates one rule.
2. **`src/umap/CMakeLists.txt`** — the configure-time link-closure
   assertions of INV-5, plus the `grep` of INV-7 which `scripts/ci.sh` runs.
3. **`tests/real/RealInstallTest.cpp`** — one case over the install, locking
   INV-2 and re-asserting INV-6 on real geometry. **It prints § 4.2's table
   rather than asserting transcribed figures**, so those numbers are an output
   of the suite rather than a claim in this document that somebody must
   re-measure by hand. It asserts a population and a rate, never a per-map
   figure: a level whose zoning disagrees with its own leaves is content
   rather than a builder defect.

Each test is seen failing against pre-fix code before it is trusted. INV-2's
case is the one that matters here: written against a descent with the
front/back convention deliberately swapped, it must go red.

## 8. Alternatives considered (and rejected)

- **Axis-aligned rectangles per room.** Two lines of code. Rejected by the
  user (§ 3.1): UT rooms interlock in 3D, so their rectangles overlap in 2D
  and the map reads as stacked boxes.
- **Floor-surface footprint only.** Cleanest result on ordinary rooms.
  Rejected by the user (§ 3.1): rooms that are mostly shaft, ramp or water
  come out thin or empty.
- **Exact 2D union of projected surface polygons.** The correct outline, with
  no sampling error. Rejected here: it needs robust polygon boolean
  arithmetic, which is either a new dependency or a large amount of delicate
  code, and UT brushwork routinely produces degenerate and co-planar polygons
  that make the exact path fragile precisely where it is hardest to debug.
  Sampling trades a stated, bounded error for robustness, and § 4.4's
  tolerance names the error. **Worth revisiting if the traced outline proves
  too coarse at doorways**, which is the first place the sampling would show.
- **Store height and flatten the view for now.** Offered to the user as the
  middle option and declined (§ 3.2).
- **Rooms from a hand-authored map file.** Rejected by the roadmap bullet:
  *"Built from the level's own BSP zones, so no map needs hand-authoring."*
  610 maps is the scale that rules it out.
- **Merging or splitting zones to even out room sizes.** Rejected for now:
  the level author's zoning is the best available statement of what a room is,
  and § 4.2 shows it is not degenerate. Revisit only with a map where it
  visibly fails.

## 9. Out of scope

- **Drawing the map** — `uui`, design rule 18. This item produces the model.
- **Serialising `RoomMap`** — `ubundle`, design rule 17; UTA-0011 drives the
  bake.
- **Recording exploration** — `ugame`; the server keeps a bit per player per
  room, and INV-7 keeps that state out of this model.
- **Reporting which room a body is in** — `uworld`, per design.md's
  *Exploration is per-room* rule. This item provides the lookup it calls.
- **Version-61 packages** — tracked by UTA-0072.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/RoomMapTest.cpp`, "a built room map names every zone but zero" |
| INV-2 | **Partial:** `tests/real/RealInstallTest.cpp`, "every leaf resolves to its own zone's room" — and that tier is **off by default**, so an ordinary gate run proves this invariant not at all. It is the only check reading geometry this project did not write, and the front/back defect it exists to catch is invisible to every check that does run. Unlike UTA-0004's INV-1, there is no fixture half carrying it on the default gate; adding one is worth doing when the builder lands |
| INV-3 | `tests/unit/RoomMapTest.cpp`, "a cyclic node graph terminates" |
| INV-4 | `tests/unit/RoomMapTest.cpp`, "zero zone and an empty map are not rooms" |
| INV-5 | The configure-time assertions in `src/umap/CMakeLists.txt` |
| INV-6 | **Partial:** the unit case proves a traced room closes its rings; the install case proves it over real geometry, and is off by default. Neither proves the outline is *correct* — that it follows the walls a player sees is a judgement no assertion here makes, and the first real check is looking at the drawn map in UTA-0016 |
| INV-7 | **Partial:** the `grep` in its *Test:* clause, wired into `scripts/ci.sh`. It catches a field named for what it holds; it cannot catch per-player state smuggled in under a neutral name, which no grep can |
| INV-8 | `tests/unit/RoomMapTest.cpp`, "every room sits on at least one floor" |
| § 4.4's sampling tolerance | **nothing** — no test asserts the traced outline is within `simplifyTolerance` of the true boundary, because this document defines no independent source of the true boundary to compare against. Revisit if § 8's exact-union alternative is ever built |
| § 4.5's band separation default | **nothing** — 128 units is reasoned from UT99's player height, not measured against the map library. A level whose floors sit closer merges them silently |

## 11. Cross-doc impact

- `docs/design.md` — no change. This item implements its subsystem table
  entry and rules 17 and 18 rather than altering them.
- `CHANGELOG.md` — an `### Added` entry when this ships.
- `CLAUDE.md` — no change.
- `docs/specs/UTA-0069-model-bsp-tables.md` — no change; this is the consumer
  its § 3.2 and § 11 anticipated, and it binds to the member names that spec
  fixed.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0007-room-partition-and-lookup-loop-log.md`.

## 13. Resource cost

`RoomMap` holds a per-level copy of the BSP planes and child links (§ 4.6),
which is the design's one deliberate duplication. Its size is bounded by the
source `Model`'s node count, and the largest table in the install is what
bounds it in practice; the tier-3 case of § 7 prints that figure alongside
§ 4.2's so the budget is measured rather than asserted.

Bake-time sampling (§ 4.4) is bounded by the level's XY extent divided by
`sampleSpacing`, per room. It runs in `ubake` and never at runtime.

**No new external dependency.** Marching squares and Ramer–Douglas–Peucker
are both short and are written here rather than pulled in, which is also what
avoids a geometry library on the runtime side of design rule 2.

## 14. Open questions

- **Is 32 units the right sample spacing?** It is reasoned from UT99's player
  width, not measured. The first level drawn in UTA-0016 will answer it, and
  it is an option rather than a constant so that answering it is a
  configuration change.
- **Should a room spanning many bands be drawn on all of them, or only where
  it opens?** § 4.5 draws it on all, which is the conservative choice. A
  lift shaft through six floors may look like clutter; the map screen is the
  place that finding will surface.
