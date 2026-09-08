# UTA-0007 — `umap`: partition a level into rooms, and answer which room a point is in

**Status:** accepted (2026-09-08).
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
derived from the level's own BSP zones, each with a traced 2D footprint and a
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

/// A point on the map plane, and a point in the level. `umap`'s own, because
/// `upkg`'s Vector3 is not linkable from here (design rule 2).
struct Point2 { float x = 0, y = 0; };
struct Point3 { float x = 0, y = 0, z = 0; };

/// One connected piece of a room's floor plan: an outer ring and its holes.
/// Every ring is closed with its first vertex NOT repeated at the end.
struct Footprint {
    std::vector<Point2> outer;
    std::vector<std::vector<Point2>> holes;
};

/// One room: a zone of the level, its footprint, and where it sits.
struct Room {
    /// The zone index this room was built from, in the source Model's own
    /// numbering. Never 0 -- SS 4.3.
    std::uint32_t zoneIndex = 0;

    /// One entry per CONNECTED component of the room -- SS 4.4. A zone used
    /// twice in a level (two pools of one water zone) is one room with two
    /// parts, not two rooms and not one self-intersecting ring.
    std::vector<Footprint> parts;

    /// Vertical extent, from the room's own samples -- SS 4.4.
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
    /// always NO_ROOM, as is every zone no leaf names -- SS 4.2.
    std::vector<std::uint32_t> roomForZone;
};

inline constexpr std::uint32_t NO_ROOM = 0xFFFFFFFFu;
/// Stored in leafZone for a leaf whose zone the build refused. Resolves to
/// NO_ROOM, never to a room -- SS 4.6, SS 6.
inline constexpr std::uint8_t ZONE_REFUSED = 0xFFu;

/// What the bake may vary. Every default is ABSOLUTE: none is computed from
/// another field, so two conforming builders given the same options produce
/// the same map. SS 4.4 and SS 4.5 say what each one does.
struct RoomBuildOptions {
    /// Half UT99's nominal 64-unit player width, so a doorway is several
    /// cells across.
    float sampleSpacing     = 32.0f;
    /// Half the default spacing -- but a LITERAL, not a computation. A
    /// caller raising sampleSpacing does NOT move this, because a tolerance
    /// that tracked another field would differ between two builders that
    /// both read this document (SS 14 asks whether it should).
    float simplifyTolerance = 16.0f;
    /// Twice UT99's nominal player height, so a room and the gallery above
    /// it separate while a stepped floor does not.
    float floorSeparation   = 128.0f;
};

/// What the build could not do, on the success path. NOT part of what
/// `ubundle` writes -- it is bake diagnostics, and design rule 17 governs the
/// room model rather than this.
struct RoomBuildReport {
    /// Zone indices of rooms that caught NO sample, so have no footprint
    /// -- SS 4.4. A room that caught samples always traces (INV-6), so this
    /// is the unsampled population and not a trace-failure population.
    std::vector<std::uint32_t> roomsWithoutFootprint;
    /// Zone indices named by a leaf but outside the zone table -- SS 6.
    std::vector<std::uint32_t> refusedZones;
};
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
struct RoomBuildResult {
    RoomMap map;
    RoomBuildReport report;
};

[[nodiscard]] Result<RoomBuildResult> buildRoomMap(
    const uta::upkg::Model& model, const RoomBuildOptions& options = {});
```

**The report rides on the success path, not on `Result`'s error channel.**
`Result`'s error arm means the build REFUSED; a room with no footprint is a
built map with a note attached, and the two must not share a channel or
`ubake` cannot tell a degraded map from a rejected one.

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
| Maps whose zone table is **empty** | 2 |
| Largest zone table | 64 |
| Leaves examined | 2 759 160 |
| Leaves naming zone 0 | 0 |
| Leaves whose `iZone` is outside its own zone table | 0 |
| Maps whose leaves name only ONE distinct zone | 26 |

The command is § 7's tier-3 case, which prints these figures rather than
having them transcribed here.

Four things follow, and each is used below:

1. **The partition is useful.** No map has a one-entry zone table, and
   834 of 836 have more than one. **Two have an empty one**, which is
   836 = 834 + 0 + 2; § 6 carries that case.
2. **Zone 0 is reserved.** Not one leaf of 2 759 160 names it. This matches
   the engine's own `FBspNode::iZone` documentation, where the array is
   *"Visibility zone in 1=front, 0=back"* and index 0 is the null zone.
   Source: <https://beyondunrealwiki.github.io/pages/standard-unreal-object-defi.html>
3. **Leaf zone indices need no clamping**, only a refusal — nothing in the
   install is out of range, so a reader that refuses one costs nothing.
4. **26 maps partition to a single room, and the reason is the room-creation
   rule below rather than their zone tables** — every one of them has a
   multi-entry table and leaves naming only one zone.

**The room-creation rule, stated because the census does not imply it.** A
zone gets a `Room` **only where at least one leaf of the source `Model` names
it**. Every other entry of `roomForZone` — index 0, and any zone the level
declares but no leaf occupies — is `NO_ROOM`.

The alternative is one room per zone-table entry, and it is wrong here: on
those 26 maps it emits up to 63 rooms that no point can ever resolve to, which
`ubundle` then serialises, `uui` draws as empty footprints, and the server keeps
exploration bits against for rooms nobody can enter. The count of rooms is the
bundle-format shape (§ 2 consequence 3), so this is not a detail the
implementer may settle locally.

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
   through `roomForZone`, **and a zone at or past `roomForZone.size()` is
   `NO_ROOM`** rather than an index into it.

**Every index this descent takes from the file is bounded before it is used.**
A child index outside `nodes`, a leaf index outside `leafZone`, and a zone
outside `roomForZone` each yield `NO_ROOM`. The iteration counter of the next
paragraph bounds a *cycle*; it does not bound a *range*, and the two failures
are different — a cyclic graph loops forever, an out-of-range index reads
memory that is not ours. Both arrive from a file this project did not write.
INV-3 covers both.

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

### 4.4 Tracing a room's footprint

Per § 3.1 the footprint follows the level's geometry. It is derived by **one
sampling pass over the level, bucketed by room**, then a boundary trace — not
by an exact polygon union of projected surfaces (§ 8), and **not from per-leaf
geometry, because there is none**. `Leaf` carries `iZone`, `iPermeating`,
`iVolumetric` and `visibleZones` and no extent of any kind. **The tables do
carry per-NODE boxes** — `Model::bounds`, indexed by `BspNode::iCollisionBound`
and `iRenderBound` — and they are deliberately not used here: they are keyed by
node, the buckets wanted are keyed by zone, and a zone's nodes are exactly what
a descent would have to walk to find. So this samples the model's own
`boundsMin` / `boundsMax` instead. Bounding the grid per subtree from
`Model::bounds` is a real optimisation and § 14 leaves it open.

1. **One grid over the whole level.** Take `model.boundsMin` and
   `model.boundsMax` and sample a regular lattice at `options.sampleSpacing`
   in all three axes.
2. **Resolve every sample once** through § 4.3, and bucket it by the room
   returned. `NO_ROOM` samples are discarded. This is the only pass: no room
   is sampled separately, and **no room needs an extent of its own
   beforehand**, which is what removes the dependency on geometry the leaves
   do not carry.
3. **`minZ` and `maxZ` are the lowest and highest sample Z in the room's own
   bucket**, so a room's vertical extent is an output of this pass rather
   than an input to it. § 4.5 clusters on those values. **A room whose bucket
   is empty has no extent to report** — its `minZ` and `maxZ` keep their
   struct defaults, which are not a measurement, and § 4.5 step 1 excludes it
   from clustering for exactly that reason. Without that exclusion a room
   with no samples clusters at zero and invents a band below the level.
4. **Project each bucket to XY.** A cell is IN for a room when at least one
   sample in that column, at any Z, resolved to it. **A cell is the square of
   `sampleSpacing` CENTRED on its column's sample** — recorded 2026-09-08,
   because this step did not say where a cell's edges fall and the two
   readings place every footprint half a cell apart. `parts` is serialised,
   so two conforming builders must not differ here.
5. **Trace each connected component separately.** Marching squares over the
   IN set yields closed rings. Group them by connected component: each
   component becomes one `Footprint`, its outer ring `outer` and any ring
   enclosed by it one of its `holes`. **A zone occupying two disjoint
   volumes** — two pools of one water zone, a zone reused at both ends of a
   level — is therefore one room with two `parts`, never one
   self-intersecting ring and never two rooms.
6. **Simplify every ring** with Ramer–Douglas–Peucker at
   `options.simplifyTolerance`, removing the grid staircase without moving a
   vertex further than that tolerance. **The simplifier never reduces a ring
   below three vertices**: where the tolerance would, the ring keeps its
   three most extreme vertices. Without that rule INV-6 would turn on an
   implementer's `>` versus `>=` at a one-cell room.

**Sampling reuses § 4.3's lookup as its primitive**, so the footprint cannot
disagree with the lookup about which room a spot belongs to — the two would
otherwise be independent answers to one question.

**Cost is bounded by the level box and not by the room count** — one pass,
and adding a room adds no pass. § 13 carries the budget.

**Resolution is a stated approximation, not an accident.** A volume thinner
than `sampleSpacing` in all three axes catches no sample, so it gets no
bucket and no `parts`. It is still a room: § 6 says what happens,
`RoomBuildReport::roomsWithoutFootprint` names it, and INV-6 makes the case
observable rather than silent.

### 4.5 Floor bands

Per § 3.2 the map screen shows one floor at a time, so rooms are assigned to
bands.

Bands are derived from the rooms, not from the geometry, because a "floor" is
a property of how rooms stack rather than of any surface:

1. Collect the vertical midpoint of every room **whose sample bucket is
   non-empty**. A room with no bucket has no measured extent (§ 4.4 step 3),
   so it does not vote on where the bands fall; step 5 says where it lands.
2. Cluster them on Z with single-linkage, splitting wherever consecutive
   sorted midpoints differ by more than `options.floorSeparation`, whose
   default and its reasoning are stated with the option in § 4.1.
3. Each cluster is a band. **`bands[i]` is the LOWEST midpoint in cluster
   i**, and the bands are ascending. That rule is stated because "the lower
   edge" has two readings — the cluster's own minimum, or the halfway point of
   the gap beneath it — and they place a room differently at every boundary.
   `bands` and `floors` are both serialised, so two conforming builders must
   not be able to disagree here. **The topmost band is unbounded above**, so
   the last band covers everything from its own edge upward.
4. A room joins **every** band its `[minZ, maxZ]` overlaps, so a stairwell or
   a lift shaft appears on each floor it connects rather than vanishing
   between them. INV-8 locks the never-empty half.
5. **A room with an empty bucket joins band 0** — it has no measured extent to
   overlap with, and INV-8 requires every room to sit on some band. **Where a
   level's rooms ALL went unsampled there is no band 0 to join, so the build
   opens one at z = 0** — recorded 2026-09-08; without it INV-8 is unmeetable
   on that level. A level with no rooms at all still gets no bands, which is
   what § 6 calls "no rooms and no bands". It draws
   nothing, having no footprint, so the band it nominally occupies costs the
   map screen nothing.

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
    /// leafZone[i] is leaves[i].iZone in the source Model, narrowed.
    /// Leaf::iZone is i32 in upkg. TWO refusals stand between it and this
    /// byte, and BOTH are required (SS 6): the build refuses a Model whose
    /// zone table exceeds the engine's 64-zone ceiling, and it refuses a leaf
    /// naming a zone outside that table. A leaf whose zone was refused stores
    /// ZONE_REFUSED here, which resolves to NO_ROOM.
    /// Membership alone does NOT make the narrowing safe: a file declaring
    /// 300 zones passes a membership test and aliases zone 300 onto 44.
    std::vector<std::uint8_t> leafZone;
```

**This is a copy, and the copy is the point.** The alternative is for the
runtime to hold a `upkg::Model`, which design rule 2 forbids. It is also
narrower than the source: the renderer's vectors, surfaces, lightmaps and
light references are not carried, because the lookup does not read them.

`Point3` is `umap`'s, declared beside `Point2` for the same reason.

## 5. Invariants

- **INV-1** — Every room names a distinct zone index in
  `[1, model.zones.size())` that **at least one leaf of the source `Model`
  names**; no room names zone 0; and `roomForZone` is `NO_ROOM` at every index
  no room was created for, index 0 included.
  *Test:* `tests/unit/RoomMapTest.cpp`, "a room exists for each zone a leaf
  names, and for no other".
  *Breaks when:* the builder walks the zone table rather than the leaves — it
  then emits a room for every declared zone, including ones no point can
  resolve to. On the 26 single-zone maps of § 4.2 that is up to 63 phantom
  rooms, each serialised, drawn, and tracked for exploration.

- **INV-2** — For every node of the source `Model` carrying a vertex pool,
  and for each side of its plane, a probe point just off the plane on that
  side resolves through `roomAt` to the room for the zone **that node's own
  record gives for that side** — `leaves[node.iLeaf[side]].iZone` where that
  side is a leaf, otherwise `node.iZone[side]`.
  *Test:* `tests/real/RealInstallTest.cpp`, "every node's own zone record
  agrees with the descent", over the install.
  *Breaks when:* the descent takes `iFront` on the back side — the front/back
  convention of § 4.3 is an array index, so swapping it compiles, returns a
  plausible room for most points, and is wrong for all of them.

  **The ground truth is read from the node record, never produced by a
  descent, and that is the whole point of this invariant's shape.** The
  obvious phrasing — *a point inside each leaf resolves to that leaf's zone* —
  cannot be written: `Leaf` carries no geometry (§ 4.4), so the only way to
  obtain a point inside a given leaf is to descend to it, and a test that
  descends and then checks `roomAt`'s descent asserts the convention against
  itself. **A consistently swapped convention passes such a test**, and § 10
  makes this the only check of that defect, so a circular form would leave it
  caught by nothing.

  **The probe point** is the centroid of the node's polygon — its `numVertices`
  entries from `verts` starting at `iVertPool`, each naming a `points` entry —
  displaced along `plane.normal` by a small multiple of the level's own scale.
  Both tables are members of `Model`. A node with no vertex pool is skipped
  rather than probed.

  **Three restrictions on the probe set, not one, and the last two were
  derived by building this.** Recorded 2026-09-08 from the measurement in
  UTA-0078 and UTA-0079; each is the same argument the first one makes.

  - **A node the descent cannot REACH is not probed.** Coplanar detail hung
    off the tree by `BspNode::iPlane` is never visited by a front/back walk,
    and neither is a subtree whose own root is one of those — measured, about
    36% of a map's nodes. So the test is a walk from node 0, not a test of
    `iPlane`.
  - **A probe not strictly inside its own node's cell is not probed.** The
    cell is what the node's ancestors' planes cut out; a probe within one
    nudge of an ancestor plane is placed by the `>= 0` tie-break rather than
    by geometry, and one on the wrong side of an ancestor is in another
    node's cell. Walking the probe down and measuring what it passes is the
    WRONG test: that measures the path it took, which differs exactly when it
    went somewhere else.

  **Only a side whose child is `INDEX_NONE` is probed, and that restriction is
  what makes the invariant true rather than merely strict.** § 4.3 step 3
  consults a node's own `iLeaf`/`iZone` record ONLY where the descent stops
  there; on a side with a child it descends, and the zone it returns belongs
  to some node further down. Probing a side with a child would therefore
  compare the descent's answer against a record the descent never reads, and
  the hard-form assertion of § 7 would go red on correct code. **A swapped
  front/back convention is still caught**, because a stopping side is exactly
  where the swap sends the probe down the wrong branch.

  **A record naming zone 0, or a zone `roomForZone` maps to `NO_ROOM`, must
  return `NO_ROOM`, and that counts as agreement.** § 4.2 creates a room only
  for a zone some leaf names, so a node side may legitimately record a zone
  that has none; demanding "the room for" it would fail an implementation
  that is behaving exactly as § 4.2 requires.

- **INV-3** — `roomAt` returns `NO_ROOM` rather than looping or reading out of
  range, for every input: a `RoomMap` whose child indices form a cycle, one
  whose child index is outside `nodes`, one whose leaf index is outside
  `leafZone`, and one whose zone is outside `roomForZone`.
  *Test:* `tests/unit/RoomMapTest.cpp`, "a cyclic node graph terminates" and
  "an out-of-range index resolves to no room".
  *Breaks when:* the descent counts iterations but never bounds an index — the
  cycle case then passes while a single out-of-range child reads memory that
  is not ours. The two failures are different and a counter catches only one.

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

- **INV-6** — Every room whose sample bucket is non-empty has at least one
  `Footprint`; every `outer` ring and every hole ring has at least three
  vertices and is closed without repeating its first vertex; and every room
  with an empty bucket appears in `RoomBuildReport::roomsWithoutFootprint`.
  *Test:* `tests/unit/RoomMapTest.cpp`, "every sampled room has a closed
  footprint, and every unsampled one is reported".
  *Breaks when:* a room thinner than `sampleSpacing` in all three axes is
  dropped silently instead of being kept and reported — or the simplifier
  reduces a one-cell ring below three vertices, which § 4.4 step 6 forbids
  precisely so this invariant does not turn on a comparison operator.

- **INV-7** — No type in `src/umap/Rooms.h` carries per-player or exploration
  state.
  *Test:* `grep -nE '\b(visited|explored|seen|player|team)\b' src/umap/Rooms.h | grep -vE ':\s*(//|\*)'`
  returns nothing. **The comment filter is load-bearing, not decoration.**
  Any header documenting why this state is absent must use the very words the
  grep hunts, so a clause matching every line would be falsified by the
  sentence documenting the rule. UTA-0004's INV-3 failed exactly this way and
  was repaired on 2026-09-08 under UTA-0074; this clause is written already
  knowing it. (§ 4.1's explanation is spec prose, not a header comment — the
  filter is for the header the implementer writes.)
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
| The `Model` parses | A version-61 package (UTA-0072) | `readModel` fails **before** `buildRoomMap` is called — this builder takes a `Model`, never a package, so it has no reader error of its own to return. `ubake` refuses that map and says which |
| The zone table is within the engine's ceiling | A malformed or hostile file declaring more zones than the engine allows | The build refuses the `Model`. **Required, and membership testing does not cover it**: a table of 300 zones passes every per-leaf membership check and then aliases zone 300 onto 44 through § 4.6's narrowing |
| The zone table is non-empty | 2 maps in the install have an empty zone table (§ 4.2) | No leaf can name a zone, so no room is created. A valid `RoomMap` with no rooms and no bands; `roomForZone` is empty, so every zone is at or past its size and `roomAt` returns `NO_ROOM` everywhere — by INV-3's range bound, which is what makes an empty table safe, with INV-4 covering zone 0 and the empty node table. `ubake` bakes a level with no map screen rather than refusing |
| Leaves name more than one zone | 26 maps name exactly one (§ 4.2) | One room covering the level. Drawn, and legitimately so — those levels really are one zone. **Not the same measurement as the row above**: their zone tables are multi-entry, and the room-creation rule of § 4.2 is what reduces them to one room |
| Every room catches a sample | A crawlspace or trim volume thinner than `sampleSpacing` in all three axes | No `parts`. The room is kept, listed in `RoomBuildReport::roomsWithoutFootprint`, and `uui` skips a room with no footprint rather than drawing a degenerate one (INV-6) |
| A leaf's `iZone` is inside its own zone table | Malformed or hostile file; § 4.2 measured none in the install | The build refuses that zone, records it in `RoomBuildReport::refusedZones`, creates no room for it, and stores `ZONE_REFUSED` in `leafZone` for every leaf that named it. It does not clamp — a clamped index silently moves a room, which is the harm, and storing the raw value would do the same through the narrowing |
| Child indices are in range | Malformed or hostile file | The descent's iteration bound returns `NO_ROOM` rather than looping (INV-3), so a malformed map degrades instead of hanging the bake |
| Rooms stack into distinguishable bands | A ramped level with no flat floors | One band. The map degrades to the flattened view, which is § 4.5's stated behaviour rather than a defect |

## 7. Tests

Three tiers, matching the project's existing split.

1. **`tests/unit/RoomMapTest.cpp`** — fixtures built by hand, locking INV-1,
   INV-3, INV-4, INV-6 and INV-8. The fixtures are small `RoomMap` and
   `Model` values written in the test, not real packages, so each case
   isolates one rule.
2. **`src/umap/CMakeLists.txt`** — the configure-time link-closure
   assertions of INV-5. **INV-7's `grep` is a new step this item adds to
   `scripts/ci.sh`**, alongside the ones already there; it is not wired
   today, and wiring it is part of this work rather than an existing
   check being relied on.
3. **`tests/real/RealInstallTest.cpp`** — one case over the install, locking
   INV-2 and re-asserting INV-6 on real geometry. **It prints § 4.2's table
   rather than asserting transcribed figures**, so those numbers are an output
   of the suite rather than a claim in this document somebody must re-measure
   by hand.

   **INV-2 is asserted at under 1% disagreeing probes, and the HARD form this
   section asked for is not met.** Amended 2026-09-08 from what building it
   measured; UTA-0079 carries the open question.

   The argument for the hard form was that "a swapped front/back convention
   is wrong at *every* probe, so any threshold below 100% passes exactly the
   defect being hunted". The first half is right and the second does not
   follow. Measured, that swap scores **zero** agreements — 0 probes of 11451
   on one map — so it is catastrophic rather than marginal, and a ceiling
   three hundred times under it still catches it. That measurement is
   UTA-0078, which the hard form found.

   What it costs: 30399 probes of 11126404 disagree, and no hypothesis tested
   accounts for them. A rate does not catch a small future regression, which
   is the price of not yet knowing why. The printed CENSUS is a population
   figure; the ASSERTION is per-probe. Those are still different things and
   this tier does both.

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
  The scale is what rules it out — the server's Monster Hunt rotation, which
  ADR-0002 to ADR-0004 put at 610 maps. **That is a different population from
  § 4.2's 837**, which counts `.unr` files in the reference install's `Maps`
  directory; the two figures do not disagree, and the rotation's own count is
  contested by a separate roadmap item. Either way no one is hand-authoring
  hundreds of maps.
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
| INV-1 | `tests/unit/RoomMapTest.cpp`, "a room exists for each zone a leaf names, and for no other" |
| INV-2 | **Partial:** `tests/real/RealInstallTest.cpp`, "every node's own zone record agrees with the descent" — and that tier is **off by default**, so an ordinary gate run proves this invariant not at all. It is the only check reading geometry this project did not write, and the front/back defect it exists to catch is invisible to every check that does run. Unlike UTA-0004's INV-1 there is no fixture half carrying it on the default gate; adding one is worth doing when the builder lands |
| INV-3 | `tests/unit/RoomMapTest.cpp`, "a cyclic node graph terminates" and "an out-of-range index resolves to no room" |
| INV-4 | `tests/unit/RoomMapTest.cpp`, "zone zero and an empty map are not rooms" |
| INV-5 | The configure-time assertions in `src/umap/CMakeLists.txt` |
| INV-6 | **Partial:** `tests/unit/RoomMapTest.cpp`, "every sampled room has a closed footprint, and every unsampled one is reported", plus the install case on real geometry (off by default). Neither proves a footprint is *correct* — that it follows the walls a player sees is a judgement no assertion here makes, and the first real check is looking at the drawn map in UTA-0016 |
| INV-7 | **Partial:** the `grep` in its *Test:* clause, as a step this item adds to `scripts/ci.sh` (§ 7). It catches a field named for what it holds; it cannot catch per-player state smuggled in under a neutral name, which no grep can |
| INV-8 | `tests/unit/RoomMapTest.cpp`, "every room sits on at least one floor" |
| § 4.4's sampling tolerance | **nothing** — no test asserts a traced footprint is within `simplifyTolerance` of the true boundary, because this document defines no independent source of the true boundary to compare against. Revisit if § 8's exact-union alternative is ever built |
| § 4.5's band separation default | **nothing** — the default is reasoned from UT99's player height, not measured against the map library. A level whose floors sit closer merges them silently |
| § 4.4's one-pass sampling cost | **nothing** — no test bounds the bake time or the sample count on a large level. § 13 states the shape of the cost; nothing enforces it, and the first evidence will be `ubake` running on the install |

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

Bake-time sampling (§ 4.4) is ONE pass over the level's own bounding box on a
three-axis lattice at `sampleSpacing` — so its cost is the box's volume over
that spacing cubed, and it does **not** scale with the room count. It runs in
`ubake` and never at runtime. **Halving `sampleSpacing` multiplies the sample
count by eight**, which is the number to know before tuning it, and § 14 is
where that tuning is still open.

**No new external dependency.** Marching squares and Ramer–Douglas–Peucker
are both short and are written here rather than pulled in, which is also what
avoids a geometry library on the runtime side of design rule 2.

## 14. Open questions

- **Is `sampleSpacing`'s default right?** It is reasoned from UT99's player
  width, not measured, and § 13 gives the cubic cost of lowering it. The first
  level drawn in UTA-0016 will answer it, and it is an option rather than a
  constant so that answering it is a configuration change.
- **Should the sample grid be bounded per subtree from `Model::bounds`?**
  § 4.4 samples the whole level box once. Per-node boxes exist and are
  indexed by `BspNode::iCollisionBound` / `iRenderBound`, so a descent could
  bound the grid to the part of the level a zone actually occupies. That is a
  real saving on a large sparse level and it is left out because it buys
  nothing correctness-wise and costs a second traversal to get wrong. Revisit
  when § 13's cost is measured on a real bake rather than reasoned about.
- **Should `simplifyTolerance` track `sampleSpacing` instead of standing
  alone?** § 4.1 fixes it as a literal so two builders cannot disagree, which
  is the safe call and not obviously the right one: a caller who doubles the
  spacing probably wants the tolerance to follow. Left absolute until someone
  tunes the pair on a real level.
- **Should a room spanning many bands be drawn on all of them, or only where
  it opens?** § 4.5 draws it on all, which is the conservative choice. A
  lift shaft through six floors may look like clutter; the map screen is the
  place that finding will surface.
