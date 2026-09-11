# UTA-0111 — `ubake`: write the level's collision into the bundle

**Status:** accepted (2026-09-11), at the review's cap.
**Kind:** implement.
**Source:** ROADMAP UTA-0111 (user-request-2026-09-10, split from UTA-0011).

**Blocked by:** UTA-0011, shipped; UTA-0119, shipped, for the movers.
**Blocker for:** UTA-0017, the movement model; UTA-0114, the physics world;
UTA-0121, the UT99 path tool.

**Layman:** Record what in each level is solid, so players, bots and flying
debris stop at walls.

## 1. Goal

A bake writes the level's collision into a new section, `COLL`: UT99's own
collision tree from the level's `Model`, and each mover's from its own. The
movement model tests walls against the tree as UT99 does, and the physics
world builds its one triangle mesh from the same data when a map loads.

## 2. Problem

1. **No section holds what is solid.** `GEOM` holds what UT99 draws
   (UTA-0109), and that is not what it collides with. Its headers say so of
   two node flags: `NF_NotVisBlocking = 0x04, // Node does not block
   visibility, i.e. is an invisible collision hull.` and `NF_NotCsg = 0x01,
   // Node is not a Csg splitter, i.e. is a transparent poly.` (the 432
   headers' `Engine/Inc/UnObj.h`).
2. **UT99 tests collision against the tree, not against triangles.**
   `UModel::PointCheck` and `UModel::LineCheck` take an extent and extra node
   flags (`Engine/Inc/UnModel.h`), and a node's solidity is
   `FBspNode::IsCsg`: `(NumVertices>0) && !(NodeFlags & (NF_IsNew | NF_NotCsg
   | ExtraFlags))` (`Engine/Inc/UnObj.h`). A triangle list cannot answer that.
3. **The hull table packs two kinds of value into one integer array.**
   `upkg::Model::leafHulls` (UTA-0069) holds, for each node naming one through
   `iCollisionBound`, a run of node indices ended by −1 and followed by the six
   floats of a box, stored as their bits. Bit 30 of an index is a flag.
4. **A mover's tree is in its brush's space**, as its shape was (UTA-0119 § 2
   item 2).
5. **The level's tree does not store every child after its parent, and it
   holds nodes no walk from the root reaches.** So a reader cannot bound a
   walk by index order, and a file whose links form a cycle would hang it.

The measurements behind items 3 and 5, behind § 4.5's reading of the flag,
and behind § 4.3's refusals holding on every map, are printed by the
real-asset case (§ 7),
`tests/real/RealCollisionTest.cpp`.

## 3. Scope decisions (agreed with the user)

The user decided two things (ROADMAP UTA-0111, 2026-09-11):

1. **The bundle stores UT99's own collision tree, with each node's outline.**
   The movement model reads the tree; the physics world builds its mesh from
   the outlines when a map loads. One copy, so the two cannot disagree.
   Rejected by the user: the tree plus a ready-made mesh, and a mesh only.
2. **Each mover's tree is baked in this item**, beside its `MOVR` shape.

The choices below are mine.

3. **Transcribe, do not interpret.** Every node field is UT99's own value.
   Which nodes are solid, how an extent is tested and which extra flags a
   caller passes are the readers' (§ 4.5), because they are UT99's rules and
   its headers state them.
4. **The hull table is decoded, not copied.** Each run becomes a hull: its
   planes, each a node index and the flag, and its box as floats. A reader
   then never reads an integer's bits as a float, and no reader decodes the
   runs again.
5. **A mover's tree is in its `MOVR` shape's pivot space**, built with the
   same `PrePivot` and `MainScale`. One placement then serves the shape and
   the tree. Planes transform exactly (§ 4.4), so front and back keep their
   meaning under a mirror.
6. **Validation proves a walk ends**, by the rule in § 4.2 item 2, rather
   than by index order, which § 2 item 5 rules out.
7. **`COLL` is a section of its own**, not members of `GEOM` and `MOVR`.
   Those change for the renderer, and this for movement, and `Sections.h`
   gives each section its own file so that work on one does not touch
   another (UTA-0091).

## 4. Design

### 4.1 The files

| File | Holds |
|---|---|
| `src/ubundle/Bundle.h` | the types below, `Bundle::collision`, `FORMAT_VERSION` (§ 4.2) |
| `src/ubundle/Sections.h`, `CollisionSection.cpp` | the `COLL` codec and its validation |
| `src/ubake/Collision.h/.cpp` | `buildCollision` and `buildMoverCollision` (§ 4.3, § 4.4) |
| `src/ubake/Movers.h/.cpp` | the pivot-space transform of a point, shared by `buildMover` and `buildMoverCollision` |
| `src/ubake/Bake.cpp`, `src/ubake/Name.h` | the bake's step and `BAKER_REVISION` (§ 4.6) |

`CollisionSection.cpp` joins `uta_ubundle`, and `Collision.cpp` joins
`uta_ubake`. No target or link changes.

### 4.2 The `COLL` section

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 7;  // 7 since UTA-0111

/// One node of UT99's collision tree, its fields UT99's own (UTA-0111 SS 4.3).
struct CollisionNode {
    std::array<float, 3> normal{};  ///< the plane's normal
    float distance = 0;             ///< the plane's W: normal . p == distance on it
    std::int32_t back = -1;         ///< iBack, UT99's child 0; -1 for none
    std::int32_t front = -1;        ///< iFront, UT99's child 1; -1 for none
    std::int32_t coplanar = -1;     ///< iPlane, the next node on this plane; -1 for none
    std::int32_t hull = -1;         ///< into CollisionTree::hulls; -1 for none
    std::uint8_t nodeFlags = 0;     ///< UT99's NodeFlags, verbatim
    std::uint32_t polyFlags = 0;    ///< its surface's PolyFlags, verbatim; 0 with no outline
    std::uint32_t firstOutline = 0; ///< into CollisionTree::outline
    std::uint32_t outlineCount = 0; ///< 0, or 3 and more
};

/// One plane bounding a hull: a node's plane, flipped where `flipped` is set.
struct HullPlane {
    std::uint32_t node = 0;
    bool flipped = false;           ///< bit 30 of UT99's stored index
};

struct CollisionHull {
    std::vector<HullPlane> planes;
    std::array<float, 3> min{}, max{};  ///< the box UT99 stores after the run
};

struct CollisionTree {
    std::vector<CollisionNode> nodes;          ///< node 0 is the root
    std::vector<std::array<float, 3>> points;  ///< what outlines index
    std::vector<std::uint32_t> outline;        ///< point indices, a run per node
    std::vector<CollisionHull> hulls;
    bool outside = false;                      ///< UT99's RootOutside
};

/// One mover's tree, in its MOVR shape's pivot space (UTA-0111 SS 4.4).
struct MoverCollision {
    std::uint32_t exportIndex = 0;  ///< its slot in the map's export table, as MOVR's
    CollisionTree tree;
};

struct Collision {
    CollisionTree level;
    std::vector<MoverCollision> movers;  ///< strictly ascending by exportIndex
};

struct Bundle {
    // ... the existing members, then:
    std::optional<Collision> collision;
};

}  // namespace uta::ubundle
```

**`COLL`** is the bytes `C`, `O`, `L`, `L`. Its payload is a `Collision`:
`level`, then `movers` as a `vector<MoverCollision>`. Each type encodes its
fields in the order declared above, with `bool` as `u8`:

| Element | Encoding | Minimum bytes |
|---|---|---|
| `CollisionNode` | four `f32`, four `i32`, `u8`, three `u32` | 45 (fixed) |
| `HullPlane` | `u32`, `u8` | 5 (fixed) |
| `CollisionHull` | `vector<HullPlane>`, six `f32` | 28 |
| `CollisionTree` | four vectors, `u8` | 17 |
| `MoverCollision` | `u32`, `CollisionTree` | 21 |

A `Collision` whose level tree is empty and which has no movers encodes to
21 bytes.

**Validation**, `MalformedData` on `read` and `InvalidArgument` on `write`,
for each tree:

1. `back`, `front` and `coplanar` are each −1 or a node of the tree.
2. **Walking from node 0 over `back`, `front` and `coplanar` reaches no node
   twice.** So no walk from the root loops. A node no walk reaches is kept,
   as UT99 keeps it (§ 2 item 5). A tree with no nodes passes.
3. `hull` is −1 or a hull of the tree.
4. `outlineCount` is 0 or at least 3, and `firstOutline + outlineCount`,
   computed without overflow, is at most the size of `outline`.
5. Every entry of `outline` names a point.
6. Every `HullPlane::node` names a node.

And `movers` are strictly ascending by `exportIndex`. `read` alone refuses a
`bool` byte other than 0 or 1, in `outside` and in `flipped`. The floats are
not validated, as `GEOM`'s are not. `ubundle` does not check that a tree's
`exportIndex` has a `MOVR` shape; the baker guarantees it (INV-6).

**`write` emits it last**: `ROOM`, `NAVG`, `WIRG`, `TEXS`, `MATS`, `GEOM`,
`PLAC`, `LITE`, `MOVR`, `COLL`. **`FORMAT_VERSION` becomes `7`.**

### 4.3 The level's tree

```cpp
namespace uta::ubake {

/// A Model's collision tree, by SS 4.3.
[[nodiscard]] Result<ubundle::CollisionTree> buildCollision(const upkg::Model& model);

/// One mover's tree, by SS 4.4, from its Model already read.
[[nodiscard]] Result<ubundle::MoverCollision> buildMoverCollision(const MoverSite& mover,
                                                                  const upkg::Model& model,
                                                                  const ubundle::Placements& actors);

}  // namespace uta::ubake
```

`buildCollision` transcribes the `Model`:

- **`points`** are the `Model`'s `points`, in order.
- **Node `i`** is `nodes[i]`: its `plane`'s normal and `w`; `iBack`, `iFront`
  and `iPlane` as `back`, `front` and `coplanar`; `nodeFlags`; for a node
  with vertices, the `polyFlags` of `surfs[iSurf]`, and 0 for a node without,
  whose `iSurf` is not read, as `buildGeometry` does not read it; and as its
  outline the `pVertex` of
  `verts[iVertPool]` onward, `numVertices` of them, appended to `outline` in
  node order, with `firstOutline` where they begin.
- **`hulls`** are one for each distinct `iCollisionBound` other than −1, in
  ascending order of that value. Each entry of its run, up to the first −1,
  becomes a plane: bit 30 is `flipped`, and the entry with bit 30 cleared is
  `node`. The six entries after the −1 are `min` then `max`, x before y
  before z, each the bits of an `f32`. A node's `hull` is the index of its
  `iCollisionBound`'s hull, or −1.
- **`outside`** is `rootOutside`.

**Refusals**, each `MalformedData` naming the node:

- a link neither −1 nor a node, or § 4.2 item 2 broken;
- `iSurf` naming no surface, on a node with vertices;
- `numVertices` of 1 or 2; a run from `iVertPool` past `verts`; a `pVertex`
  naming no point;
- `iCollisionBound` below −1 or past `leafHulls`; a run with no −1, or fewer
  than six entries after it; an entry that, with bit 30 cleared, names no
  node;
- and, naming the `Model`, a `rootOutside` other than 0 or 1.

They reach the nodes `buildGeometry` skips too, since an invisible node can
still be solid (§ 2 items 1 and 2). A tree that silently loses a node can let a
player through a wall, and nothing downstream can tell. Every one of these
held on every `Model` of the reference install (§ 7).

### 4.4 A mover's tree

`buildMoverCollision` runs `buildCollision` over the mover's `Model`, its
refusal naming the actor, then moves the tree into the pivot space of the
mover's `MOVR` shape. `PrePivot` and `MainScale` resolve as UTA-0119 § 4.4
resolves them, and a `MainScale` with a zero component is refused as
`buildMover` refuses it. With `S` for `MainScale` and `P` for `PrePivot`,
in double and stored as float:

- **A point `p` becomes `S ⊙ (p − P)`**, computed by the function
  `buildMover` uses for a vertex, so a corner the shape and the tree both
  hold is the same float.
- **A plane `(n, d)` becomes `(n ⊘ S, d − n · P)`**, both then divided by
  the length of `n ⊘ S`. For `q = S ⊙ (p − P)`, `(n ⊘ S) · q = n · p − n · P`,
  so a point on the plane stays on it and a point in front stays in front,
  under a mirror too. No link is swapped, and no outline is reversed
  (§ 4.5).
- **A hull's box** takes `S ⊙ (min − P)` and `S ⊙ (max − P)`, swapped on each
  axis where `S` is negative.

`exportIndex` is the mover's placement's.

### 4.5 What a reader may rely on

UTA-0017 and UTA-0114 read this section; what follows is what they bind to.

- **The fields mean what UT99's headers say they mean.** A node a walk
  reaches is solid by `FBspNode::IsCsg`, above. A walk descends by
  `FBspNode::ChildOutside`:
  `iChild ? (Outside || IsCsg(ExtraFlags)) : (Outside && !IsCsg(ExtraFlags))`,
  starting from the tree's `outside`, where child 1 is `front` and child 0 is
  `back` (`Engine/Inc/UnObj.h`). The extra flags are the caller's.
- **A point with `normal · p > distance` descends into `front`, child 1;
  any other point, on the plane included, into `back`, child 0.** The
  header's comments on `iBack` and `iFront` say the reverse, and are not the
  authority. Walked this way, nearly every `PlayerStart` of the reference
  install lies outside; walked the other way, nearly none does (§ 7).
- **A walk from node 0 over the three links ends**, reaching no node twice
  (§ 4.2 item 2). Every index a reader follows is in range. A node no walk
  reaches is not solid, so a reader building a mesh skips its outline, as
  the movement model's walk never meets it. On the reference install none
  carries an outline or a hull (§ 7).
- **A hull is the region behind its planes, each reversed where `flipped`
  is set.** That is measured, not read from the headers: over the reference
  install, no hull's box lies wholly in front of an unflipped plane, or
  wholly behind a flipped one, by more than half a unit. The real-asset case
  prints both (§ 7).
- **An outline's order is its vertex pool's, and says nothing about which
  way the face points.** Some nodes wind against their surface's normal
  (UTA-0109 § 2 item 4), and § 4.4 never reverses one. A reader orients
  each outline by its node's plane.
- **A mover's tree is placed by UTA-0119 § 4.5's formula**, with its `MOVR`
  shape's `location`, `rotation` and `postScale`.

### 4.6 The bake's steps

`detail::bake`'s steps 1 to 9 are UTA-0119 § 4.6's, unchanged. Then:

10. **`COLL`**: `buildCollision` over the level's `Model`, then
    `buildMoverCollision` over each mover, in export order, from the `Model`
    step 6 read.
11. **The budget**, unchanged.

`COLL` is written, its `movers` empty where the level has no mover.
**`BAKER_REVISION` becomes `5`**, and UTA-0011 INV-5's golden value is
recorded again under it.

## 5. Invariants

- **INV-1** — `COLL` round-trips through `ubundle::write` and `ubundle::read`,
  every float bit included, and `write` emits it after `MOVR`.
  *Test:* `tests/unit/BundleCollisionTest.cpp`: a payload authored from § 4.2
  field by field, never by `write`, in a bundle that also carries `MOVR`. Its
  level tree holds a node whose `front` child is stored before it, a node
  no walk from node 0 reaches, an outline, a hull of two planes one flipped,
  and `outside` false; one mover's tree holds a `-0.0` point. `read` decodes
  it and `write` reproduces it. An empty level tree with no movers encodes
  to exactly 21 bytes.
  *Breaks when:* a field is encoded at another's width, a child stored
  before its parent or an unreached node is refused, or `COLL` is emitted
  before `MOVR`.

- **INV-2** — `read` refuses with `MalformedData`, and `write` with
  `InvalidArgument`: a link naming no node; a node a walk from node 0
  reaches twice; a `hull` naming no hull; an outline of
  two points; an outline reaching past `outline`; an outline entry naming
  no point; a hull plane naming no node; two trees of one slot; trees out of
  order. `read` alone refuses a `bool` byte of 2 in `outside` and in
  `flipped`.
  *Test:* `tests/unit/BundleCollisionTest.cpp`, one case per rule, each
  fixture breaking that rule alone. "Reached twice" is a link back to node 0
  in one case and a node two parents name in another.
  *Breaks when:* a rule is checked on one path only.

- **INV-3** — The level's tree is its `Model`'s: its points in order; each
  node's plane, three links and `nodeFlags` verbatim; its `polyFlags` its
  surface's where it has vertices, else 0; its outline its vertex run's
  `pVertex`, in order; and
  `outside` its `rootOutside`.
  *Test:* `tests/unit/BakeCollisionTest.cpp`: `buildCollision` over a `Model`
  built in memory whose nodes' vertex runs are not in node order, whose
  surfaces carry distinct `polyFlags` unlike any node's `nodeFlags`, whose
  nodes' `back` and `front` differ, and which holds a node of no vertices
  whose `iSurf` names no surface.
  *Breaks when:* `back` and `front` swap, `polyFlags` come from the node, an
  outline is read from the wrong place, or a node of no vertices has its
  `iSurf` read.

- **INV-4** — The hulls are the distinct `iCollisionBound` values in
  ascending order; each run decodes to its planes, with bit 30 as `flipped`,
  and its box; and a node names its own run's hull.
  *Test:* `tests/unit/BakeCollisionTest.cpp`: a `Model`, built in memory,
  whose `leafHulls`
  holds two runs, the second named by node 0 and the first by node 2, so
  ascending order differs from node order, with one entry carrying bit 30.
  *Breaks when:* the hulls are ordered by node, the flag is left in the
  index, or the box is read as integers.

- **INV-5** — A mover's tree is its `Model`'s in pivot space: its points are
  its `MOVR` shape's positions, bit for bit; its planes are § 4.4's; its
  boxes are § 4.4's, swapped on a negative axis; its links and outlines are
  its `Model`'s, under a mirror too.
  *Test:* `tests/unit/BakeCollisionTest.cpp`: `buildMoverCollision` and
  `buildMover` over one mover, with `PrePivot` `(8, 0, 0)` and `MainScale`
  `(2, -1, 1)`. Its `Model`, built in memory, holds UTA-0119 INV-4's tilted
  square as node 0, with a hull, and a node of no vertices as node 0's
  `front`. Every point equals a shape position bit for bit; node 0's links
  and outline are unchanged; a corner stays on the transformed plane within
  `1e-4`; a point in front of the plane stays in front; the hull's box is
  swapped on y.
  *Breaks when:* a normal is scaled rather than divided, a distance ignores
  `PrePivot`, a mirror moves a point to the other side of its plane, swaps
  links or reverses an outline, or a box is not swapped.

- **INV-6** — A mover gets a tree exactly when it gets a `MOVR` shape, with
  that shape's `exportIndex`.
  *Test:* `tests/unit/BakeCollisionTest.cpp`, through `detail::bake`: the map
  of UTA-0119 INV-3's case, whose actors isolate each rule of which actors
  are movers. The trees' `exportIndex` values equal the shapes'. A map with
  no mover writes an empty `movers`.
  *Breaks when:* trees are keyed by placement position, or built for a brush
  that is not a mover.

- **INV-7** — A `Model` breaking a § 4.3 refusal is refused with
  `MalformedData`, naming the node, or the `Model` for `rootOutside`. In a
  bake, a mover's refusal names the actor as well.
  *Test:* `tests/unit/BakeCollisionTest.cpp`: one case per refusal, calling
  `buildCollision` on a `Model` built in memory that breaks that rule alone.
  Then two bakes through `detail::bake`: one whose level `Model`, and one
  whose mover's `Model`, breaks only a `coplanar` link naming no node. No
  earlier step of the bake reads `iPlane`, so only `COLL` can refuse them.
  *Breaks when:* such a node is dropped, clamped or baked, or a refusal
  names the wrong thing.

## 6. Failure modes

| When | What happens |
|---|---|
| A `Model` breaks a § 4.3 refusal | The bake is refused, naming the node, or the `Model` for `rootOutside`, and the actor for a mover |
| A mover's `MainScale` has a zero component | The bake is refused, as UTA-0119 INV-9 refuses it |
| The level has no mover | `movers` is written empty |
| A `Model` has no nodes | Its tree has no nodes and no hulls; its points and `outside` are transcribed |
| A plane or a box holds a non-finite float | It is baked as is; floats are not validated |

## 7. Tests

**Unit, on every CI leg:** `tests/unit/BundleCollisionTest.cpp` for INV-1
and INV-2; `tests/unit/BakeCollisionTest.cpp` for INV-3, INV-4, INV-5,
INV-6 and INV-7. Each is
seen failing before the code it locks exists.

**The fixtures change.** `ModelExportWriter`, in
`tests/support/UnrealPackageBuilder.h`, writes every node's `iPlane` and
`iCollisionBound` as 0 and its `LeafHulls` empty. § 4.3 refuses both: node 0
would name itself as its coplanar, and a hull index would pass an empty table.
So its `Node` gains `iPlane`, `iCollisionBound` and `nodeFlags`, defaulting to
−1, −1 and 0, and the writer gains a `LeafHulls` table and `RootOutside`.
Its existing `iFront` and `iBack` were written in the opposite order to the
one `upkg::readModel` reads, so a fixture's `iFront` read back as `iBack`,
until UTA-0122 corrected it. That is why the cases of INV-3, INV-4 and INV-5,
and INV-7's per-refusal cases, build their `Model` in memory.
Every bake test then builds a `Model` § 4.3 accepts. UTA-0011 INV-5's golden
bake covers `COLL`, and its case asserts the level tree has the fixture
`Model`'s nodes.

**Real-asset tier, local only:** `tests/real/RealCollisionTest.cpp` runs
`buildCollision` over every map's level `Model`, and `buildMoverCollision`
over every mover. It prints trees built, and maps refused by reason; hull
entries with bit 30 set and clear, by which side of the entry's plane its
run's box lies; links naming a node stored before their own; nodes no walk
from node 0 reaches, and those of them carrying an outline or a hull; and
the share
of `PlayerStart` locations the tree classifies as outside, walking as § 4.5
describes and walking with the two children the other way round. Those two
shares are what show which child is front, and that the transcription reads
as UT99 reads it.

**Mutation, by hand** (`CLAUDE.md` § Build and test): swap `back` and
`front`; take `polyFlags` from `nodeFlags`; order hulls by node; leave the
flag in the index; read the box as integers; scale a normal instead of
dividing it; drop `PrePivot` from a distance; leave a box unswapped; swap
links or reverse an outline under a mirror; drop
§ 4.2 item 2; delete any one § 4.3 refusal; key trees by placement
position. Each must be killed by the
invariant that names it.

## 8. Alternatives considered (and rejected)

- **The tree plus a ready-made mesh**, and **a mesh only** — rejected by the
  user (§ 3 decision 1).
- **`leafHulls` copied as its integer array.** Each reader would read bits as
  floats and decode the runs again.
- **A mover's tree in brush space, with `PrePivot` and `MainScale` stored.**
  One mover would have two spaces, and two placements to keep in step.
- **The tree inside `GEOM` and `MOVR`** — § 3 decision 7.
- **Every child stored after its parent, as the walk rule** — false for the
  level's trees (§ 2 item 5).

## 9. Out of scope

- Testing movement against the tree — UTA-0017.
- Building the physics mesh — UTA-0114.
- Moving a mover's tree — not yet queued, as moving its shape is not.
- A mover's shear — not applied, as UTA-0119 § 3 decision 3 leaves it.
- The `Model`'s zones and leaves — UTA-0007 reads the zones for rooms.

## 10. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1, INV-2 | `tests/unit/BundleCollisionTest.cpp`, a unit test |
| INV-3, INV-4, INV-5, INV-6, INV-7 | `tests/unit/BakeCollisionTest.cpp`, a unit test |
| `BAKER_REVISION` covering collision | **Partial:** `tests/unit/BakeGoldenTest.cpp`, a golden-hash test, catches what its fixture holds |
| The flag's meaning, and the walk reading as UT99's | **Partial:** `tests/real/RealCollisionTest.cpp`, a real-asset test, prints both; no CI leg runs it |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md`, in the same change as
  the code: § 4.2's minimum-size table gains the five elements of § 4.2 here;
  § 4.3 and § 4.4 gain version `7` and `COLL`; § 4.10's API and order clause
  gain it; INV-4 is annotated with version `7`.
- `docs/specs/UTA-0011-map-baker.md` — § 4.5's steps gain § 4.6's step 10,
  and § 4.3's `BAKER_REVISION` moves to `5`. Recorded when built.
- `docs/specs/UTA-0109-map-geometry.md` — § 4.3's closing sentence says a
  node it skips cannot refuse `GEOM`, rather than a bake, since § 4.3 here
  refuses one. Recorded when built.
- `docs/specs/UTA-0119-mover-shapes.md` — § 9's collision line points here.
- `CHANGELOG.md` — an `### Added` entry, and a `### Changed` entry for format
  version `7`.
- ROADMAP UTA-0017 and UTA-0114 — a note naming § 4.5 as what each reads.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0111-level-collision-loop-log.md`.

## 13. Resource cost

- No new target and no new dependency.
- `COLL` holds one record per BSP node, the `Model`'s points, the outlines'
  indices and the hull runs, for the level and for each mover.

## 14. Migration / compatibility

**No `.utab` exists that version `7` orphans**: `0.1.0` has not been cut. A
version-`6` file is refused and baked over (UTA-0011 § 4.7).
