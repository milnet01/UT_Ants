# UTA-0121 — `ut-paths`: propose bot path nodes for UT99's maps

**Status:** accepted (2026-09-11), at the review's cap.
**Kind:** feature.
**Source:** ROADMAP UTA-0121 (user-request-2026-09-11).

**Blocked by:** UTA-0111, shipped.
**Pairs with:** UT_MonsterHunt's GAME-0095, which applies the output.

**Layman:** A tool that works out where extra breadcrumbs belong in old maps,
so UT99's own bots can find their way to the exit.

## 1. Goal

A command, `ut-paths`, reads a map from the install and writes one JSON file
proposing PathNode positions. Each position is where a player can stand. The
nodes run from the part of the map's path network its start can reach toward
each exit: to the exit itself, or on a `PARTITIONED` map to the part of the
network that reaches it. UT_MonsterHunt adds them to its path-building
recipe, and UT's editor links them.

## 2. Problem

1. **UT_MonsterHunt's route census finds maps whose exit no bot reaches.**
   Two of its groups are path problems (their `docs/route-census-split.md`).
   `EXIT_OFF_NET`: the exit is far from every navigation point.
   `PARTITIONED`: a navigation point is near the exit, but the start's part
   of the network never reaches it. Their split script,
   `analysis/routecensus_split.py`, separates the two at the furthest
   exit-to-node distance seen on any map that routed.
2. **Their recipe places seeds without checking them.**
   `analysis/seedpaths.py` writes each seed's `Location` verbatim, with no
   floor snap and no collision check. So a proposed position must already be
   one a player can stand at.
3. **Nothing here knows where a player can stand.** `unav::NavGraph` holds
   nodes and reach specs but no positions (`src/unav/Graphs.h`).
   `ubundle::CollisionTree` answers whether a point is solid (UTA-0111 § 4.5),
   and nothing yet traces a segment, finds a floor or tests a body.
4. **The agreed hand-off needs an MD5**, and only SHA-256 exists
   (`src/core/Sha256.h`).
5. **This is the third tool writing JSON by hand.** `tools/ut-bake/Cli.cpp`'s
   string escaper is a copy of `tools/ut-dump/main.cpp`'s, and says a third
   is the point to share it.

## 3. Scope decisions (agreed with the user)

The user decided three things (ROADMAP UTA-0121, 2026-09-11):

1. **This project writes no UT99 map file.** It proposes positions, and
   UT_MonsterHunt applies them.
2. **The format is agreed with UT_MonsterHunt (their GAME-0095):** one JSON
   file per map holding `map`, `md5`, `group`, `exits` and `nodes`, every
   MonsterEnd in `exits`, positions in world units on UT's axes, and Z where
   UT stores a standing actor. A map whose only walkable connection crosses a
   mover's brush is flagged, not counted as a failure.
3. **Lifts, teleporters, doors opened by shooting, and their
   `PAWN_ANCHORED_NO_ROUTE` group are out of reach.**

The choices below are mine, the user being away and having left them to me.

4. **The body is the census surveyor's.** `Botpack.TMale1`, which their probe
   spawns, resolves to CollisionRadius 17, CollisionHeight 39 and
   MaxStepHeight 25 (§ 13's probe, over the install's own packages). A node
   stands with its Location 39 above the floor, the middle of that cylinder.
   What size UT's editor tests a seed at is § 14's open question.
5. **Walking is modelled conservatively.** A player steps up or down at most
   25 between neighbouring spots, or walks one ramp. Jumps and drops are not
   modelled, so a route needing one is not found. Anything this finds, a bot
   can walk.
6. **A floor is a surface whose normal's Z is at least 0.7.** That is not UT's
   measured limit, which no source here states; it is the value
   UT_MonsterHunt's own wall heuristic uses (their `analysis/wallcheck.py`).
   The real-asset case prints the floor under every shipped PlayerStart and
   PathNode, which is what would show it too strict.
7. **Movers block first.** A route is searched with every mover's box solid.
   Only where none is found is it searched again with them open; a route found
   then is the mover flag.
8. **Nodes are spaced for UT's linker.** Each straight hop between proposed
   nodes is clear and at most 350 long, and none is within 50 of another
   navigation point unless the path leaves no other choice. UnrealWiki's Basic Bot Pathing: "A distance of 300 to
   700 UUs seems to work well", "No farther then 300 to 350 on steps and
   ramps", and closer than 50 "UnrealEd will notify you that your paths are
   too close together". One cap of 350 meets both distances without telling
   a ramp from a floor. UT's own link limit is not stated in any source
   reached here.
   Source: <https://beyondunrealwiki.github.io/pages/basic-bot-pathing.html>.
9. **The code lives in the tool**, not in a library. `uworld` does not exist,
   and UTA-0017 owns real body-against-level collision. A second user moves
   this code (§ 8).

## 4. Design

### 4.1 The files

| File | Holds |
|---|---|
| `tools/ut-paths/main.cpp`, `Cli.h`, `Cli.cpp` | the command line (§ 4.2) |
| `tools/ut-paths/Trace.h`, `Trace.cpp` | point and segment checks against a tree (§ 4.4) |
| `tools/ut-paths/Walkable.h`, `Walkable.cpp` | standing spots and the walk graph (§ 4.5) |
| `tools/ut-paths/Seeds.h`, `Seeds.cpp` | the start, exits, network part, routes and nodes (§ 4.6, § 4.7) |
| `tools/ut-paths/CMakeLists.txt` | `ut-paths`, linking `uta_ubake` |
| `tools/common/Json.h` | the shared string escaper (§ 4.8) |
| `src/core/Md5.h`, `Md5.cpp` | RFC 1321 MD5 (§ 4.8) |

`tools/CMakeLists.txt` adds `ut-paths`, and `uta_tools_common`, the
interface library carrying `tools/common/`. `tests/CMakeLists.txt` compiles
`Cli.cpp`, `Trace.cpp`, `Walkable.cpp` and `Seeds.cpp` into
`uta_unit_tests`, as it does ut-bake's `Cli.cpp`. `src/ubake/Bake.h`
declares the bake's `findLevel` and `findModel` in `detail`, so `sceneOf`
finds a map's Level and Model as a bake does.

### 4.2 The command line

```text
ut-paths --install <dir> --census <tsv> --out <dir> [<map> ...]
```

- `--census` is UT_MonsterHunt's split TSV. Its header names a `map` and a
  `group` column. Rows whose group is `EXIT_OFF_NET` or `PARTITIONED` are the
  work; every other row is ignored.
- Named maps narrow the work. A named map that is not such a row is refused.
- Each map is read from `<install>/Maps/<map>.unr`, the name as the TSV
  spells it, a `-BP` name included, and writes `<out>/<map>.json` through a
  temporary file and a rename. A map with no file there is skipped: named in
  the output, and not a refusal. UT_MonsterHunt's GAME-0092 moved some census
  maps out of `Maps/`.
- Standard output is one JSON object, as ut-bake's is, and the same object is
  written to `<out>/ut-paths-summary.json`, so a reader of the directory sees
  every map the run took, skipped ones included. Standard error is for
  people.

  ```json
  {"schema": 1, "maps": [
    {"map": "MH-A", "status": "written", "exits": 1, "nodes": 7},
    {"map": "MH-B", "status": "skipped", "why": "no file in Maps/"},
    {"map": "MH-C", "status": "refused", "why": "<the refusal's message>"}
  ]}
  ```
- Exit status: 0 when every map was written or skipped, 1 when any was
  refused, 2 for bad arguments.

### 4.3 The per-map file

```json
{
  "schema": 1,
  "map": "MH-Example",
  "md5": "0123456789abcdef0123456789abcdef",
  "group": "EXIT_OFF_NET",
  "moverOnly": false,
  "exits": [
    {"x": 1024, "y": -512, "z": 96, "route": "found"}
  ],
  "nodes": [
    {"x": 400, "y": -300, "z": 39}
  ]
}
```

- **`md5`** is of the bytes of the `.unr` read, as 32 lower-case hex digits.
- **`group`** is the TSV's.
- **`exits`** holds every actor whose class descends from `MonsterEnd`, in the
  level's actor order, at its `Location` as stored. `route` is `found`,
  `mover` or `none` (§ 4.7).
- **`moverOnly`** is true exactly when no exit's route is `found` and at least
  one is `mover`.
- **`nodes`** are the proposed positions, exit by exit, each exit's in route
  order from the network toward the exit. It is empty when no route is
  `found`.
- **Numbers.** A node's position is computed in double, rounded once to
  `float`, UT's own storage type, and written as that float's shortest
  round-trip decimal (`std::to_chars`). An exit's is its `Location`, already
  a float.

### 4.4 Tracing the tree

```cpp
namespace uta::paths {

/// SS 4.5 of UTA-0111: whether `p` is in empty space.
[[nodiscard]] bool isEmpty(const ubundle::CollisionTree& tree, const Vec3& p);

struct Hit {
    double fraction = 1;  ///< along a to b; 1 when nothing is hit
    Vec3 normal{};        ///< the crossed plane's normal, facing a
};

/// The first point from a to b where empty space turns solid.
[[nodiscard]] Hit trace(const ubundle::CollisionTree& tree, const Vec3& a, const Vec3& b);

/// The first point from a to b where solid turns empty: SS 4.5's floors.
[[nodiscard]] Hit traceOut(const ubundle::CollisionTree& tree, const Vec3& a, const Vec3& b);

}  // namespace uta::paths
```

`Vec3` is three doubles. Both functions follow UTA-0111 § 4.5: the walk starts
at node 0 with the tree's `outside`, updates it by `FBspNode::ChildOutside`
with no extra flags, and a point with `normal · p > distance` takes `front`.

`trace` splits the segment at each node's plane it straddles and descends
the near part first, carrying `outside` down each side. A part touching a
plane at one end only goes with its other end: a split's far part starts on
the plane, and where a later node shares it, a coplanar node or two rooms
sharing a face, a split there would be a solid part of no length. The first leaf reached
that is solid ends it. The crossing is on the plane of the node where the
segment was split into empty and solid parts; `normal` is that plane's,
reversed if needed to face `a`. A segment starting in solid returns fraction
0. The walk ends because § 4.2 item 2 of UTA-0111 holds for any tree a bake
wrote.

### 4.5 Where a player can stand

The body is § 3 decision 4's: radius `R` 17, half-height `H` 39, step `S` 25.
A floor needs a normal with Z at least `F`, 0.7 (§ 3 decision 6).

- **Columns.** Over the level tree's points' bounding box, a grid of columns
  32 apart in X and Y.
- **Floors.** In each column, from the box's top down: trace to the first
  solid, keep the hit if its normal's Z is at least `F`, then step 1 below it
  and trace down to where space is empty again, and repeat. Each kept hit at
  height `z` is a candidate spot at `z + H`.
- **Fit.** A spot is kept only if every probe point is empty: the centre, the
  centre raised and lowered by `H - 1`, and eight points on the radius `R`
  circle at each of the heights `-H + S + 1`, `0` and `H - 1` from the centre.
  This is a sampled test, not a swept cylinder (§ 9).
- **Walk graph.** Spots in the eight neighbouring columns join when the three
  segments between them, at the same three heights from each centre, trace
  clear, and either their heights differ by at most `S` or their floors lie
  on one plane: the two floor hits' normals match, and each hit lies within 1
  of the other's plane. That second case is a ramp, since a floor as steep as
  `F` rises about 33 between columns 32 apart.

### 4.6 The start, the exits and the network

- **The start** is the first actor in the level's actor list
  (`upkg::Level::actors`) whose class descends from `Engine.PlayerStart`, as
  UT_MonsterHunt's probe takes it.
- **The exits** are the actors whose class descends from `MonsterEnd`.
  Ancestry comes from `ubake::buildActors`, so an exit is found whatever
  package declares its class.
- **Positions** are each actor's resolved `Location`: its own property, else
  its class default (`ubake::detail::resolvedRecord`).
- **The network** is `unav::buildNavGraph`'s graph. Its start part is every
  node reachable, over its edges in their stated direction, from the node
  nearest the start's Location.
- **Placing an actor on the walk graph.** Its spot is the one nearest its
  Location among spots in columns within 64 of it horizontally, whose centre
  is within `H + S` of it vertically. An actor with none is off the graph.
- **Touching an exit.** A spot touches an exit when it lies inside the exit's
  collision cylinder grown by the body: horizontally within the exit's
  resolved `CollisionRadius` plus `R`, vertically within its
  `CollisionHeight` plus `H`. MonsterEnd defaults to 40 and 40.

### 4.7 Routes and nodes

```cpp
namespace uta::paths {

/// UT's CollisionRadius and CollisionHeight, the height a half-height.
struct Cylinder { Vec3 centre{}; double radius = 0, height = 0; };
struct Box { Vec3 min{}, max{}; };

/// One map, read and placed: what SS 4.6 and the movers' boxes produce.
struct Scene {
    ubundle::CollisionTree tree;                             ///< the level's
    std::vector<Vec3> network;                               ///< each navigation point's Location
    std::vector<std::pair<std::size_t, std::size_t>> edges;  ///< into `network`, from then to
    Vec3 start{};
    std::vector<Cylinder> exits;                             ///< Location and collision size
    std::vector<Box> movers;                                 ///< world boxes
};

enum class Route { Found, Mover, None };

struct Proposal {
    std::vector<Route> routes;  ///< one per exit, in `Scene::exits` order
    std::vector<Vec3> nodes;
};

[[nodiscard]] Result<Scene> sceneOf(const upkg::Package& map, std::string_view mapName,
                                    const upkg::PackageResolver& resolver);
/// `partitioned` is whether the map's census group is PARTITIONED (SS 4.7).
[[nodiscard]] Proposal propose(const Scene& scene, bool partitioned);
[[nodiscard]] std::string toJson(std::string_view map, std::string_view md5, std::string_view group,
                                 const Scene& scene, const Proposal& proposal);

}  // namespace uta::paths
```

- **Movers.** Each mover's tree, from `ubake::buildMoverCollision`, is placed
  by UTA-0119 § 4.5's formula with its `MOVR` shape's `location`, `rotation`
  and `postScale`. Its box is the world-axis box around its placed points. A
  spot whose body box overlaps a mover's box is a mover spot.
- **Search.** For each exit: the shortest path, by distance, from the start
  part's placed spots, and the start's own, to a goal spot. A spot touching
  the exit is a goal. On a `PARTITIONED` map, so is a spot placed for a
  navigation point outside the start part from which the network reaches the
  navigation point nearest the exit: the chain bridges the gap and stops. First
  with mover spots removed; a path found is `found`. Else with them kept; a
  path found is `mover`. Else `none`.
- **Hops.** A hop from one position to another is allowed when it is at
  most 350 long; its three segments at § 4.5's heights trace clear; its
  centre segment, grown by `R` across and `H` up and down, meets no mover's
  box; and at every 32 along it, a spot of the walk graph that is not a
  mover spot lies within 32 horizontally and within `S` of the hop's own
  height there. So a hop never spans a pit or a door the path went round.
- **Nodes.** Along a `found` path, from its first spot: the next point is the
  furthest spot along the path with an allowed hop from the chain's last
  point. Where that spot lies within 50 of an existing navigation point, or
  of a node already proposed, that point takes its place if the hop to it is
  allowed; if not, the furthest spot with an allowed hop and no such point
  within 50 is taken, and failing that the spot itself. A spot taken is
  proposed as a node, and the next hop is measured from whatever was taken.
  The path's last spot ends the chain, by the same rule.

### 4.8 MD5 and the JSON escaper

```cpp
namespace uta {
/// RFC 1321 MD5, fed in any number of pieces, as Sha256 is.
class Md5 { /* update(std::span<const std::byte>), finish() -> std::array<std::byte, 16> */ };
[[nodiscard]] std::array<std::byte, 16> md5(std::span<const std::byte> bytes);
}
```

`tools/common/Json.h` holds the one string escaper, header-only.
`tools/ut-dump/main.cpp` and `tools/ut-bake/Cli.cpp` drop their copies for it.

## 5. Invariants

- **INV-1** — `md5` returns RFC 1321's digest for every test string in its
  appendix A.5, whole and fed in pieces.
  *Test:* `tests/unit/CoreMd5Test.cpp`: each A.5 string hashed whole, and
  again fed to `Md5` a byte at a time.
  *Breaks when:* a round's shift or constant is wrong, the length is appended
  big-endian, or a piece boundary loses buffered bytes.

- **INV-2** — `trace` returns the first crossing into solid, and its normal
  faces the start.
  *Test:* `tests/unit/PathTraceTest.cpp`: a tree built in memory whose floor
  is `z = 0` and whose slab is solid from `z = 100` to `z = 120`. A segment
  from `z = 200` to `z = -10` hits at the slab's top with normal `+Z`; from
  `z = 50` to `z = -10`, the floor with `+Z`; from `z = 50` upward, the slab's
  underside with `-Z`; one wholly at `z = 50`, nothing.
  *Breaks when:* the far part is descended first, `outside` is not carried
  through a split, or the normal is left facing away.

- **INV-3** — A spot stands `H` above a floor, and only where the body fits
  and the floor is flat enough.
  *Test:* `tests/unit/PathWalkableTest.cpp`: a floor at `z = 0` under a
  ceiling at `z = 100` gives spots at exactly `z = 39`; under a ceiling at
  `z = 70` it gives none; a floor tilted to normal Z 0.6 gives none, and one
  at 0.8 gives spots.
  *Breaks when:* the spot sits on the floor, the fit ignores the ceiling, or
  the slope test is reversed.

- **INV-4** — Spots join only within a step or along one ramp, and with the
  way clear.
  *Test:* `tests/unit/PathWalkableTest.cpp`: floors in neighbouring columns
  20 apart in height join, and 30 apart do not; two spots on one ramp of
  normal Z 0.75, rising 28 between columns, join. Two at one height either
  side of a wall 1 thick, midway between their columns, do not; the test
  first asserts both spots exist, since a thicker wall removes one.
  *Breaks when:* the step is not checked, a ramp's rise is held to the step,
  or the join is not traced.

- **INV-5** — On an `EXIT_OFF_NET` map, a route found is proposed as a chain
  whose every hop is allowed (§ 4.7), from the start part's placed spot to
  the exit, each node standing `H` above its floor.
  *Test:* `tests/unit/PathSeedsTest.cpp`, through `propose` over a `Scene`
  built in memory, not partitioned: an L-shaped corridor, each leg 1500
  long, with the start and its network at one end and a MonsterEnd at the
  other, no navigation point near it. Every hop of the chain is at most 350 and traces clear;
  every node stands `H` above the floor; the last touches the exit. Then
  again with a pit across the middle of one leg, leaving a strip beside it
  that the walk graph follows: no hop crosses the pit.
  *Breaks when:* a hop is not traced, so a node cuts the corner through the
  wall; the 350 cap is not applied along a straight leg; a hop is not
  checked for floor, so it crosses the pit; or the last spot is dropped where
  no navigation point takes its place.

- **INV-6** — A route through a wall is `none`, and one only through a mover
  is `mover`, with no nodes, and `moverOnly` true.
  *Test:* `tests/unit/PathSeedsTest.cpp`: INV-5's corridor, once with a solid
  wall across it, once with a mover's box across it.
  *Breaks when:* movers are never treated as solid, or a `mover` route
  proposes nodes.

- **INV-7** — The start part is what the network reaches from the start, in
  the edges' direction; on a `PARTITIONED` map the chain stops where it meets
  the part that reaches the exit, and a navigation point within 50 takes a
  spot's place only over an allowed hop.
  *Test:* `tests/unit/PathSeedsTest.cpp`, through `propose`, partitioned: a
  network of two parts with one edge from the exit's part to the start's and
  none back, a MonsterEnd beside the exit's part, and a navigation point of
  the exit's part behind a thin wall from a spot on the path, within 50 of
  it. The route starts in the start's part; nodes are proposed across the
  gap and none further along the path than the spot placed for the exit's
  part; the walled-off point takes no spot's place.
  *Breaks when:* edges are followed both ways, which reads the exit's part as
  reachable and proposes nothing; the chain runs on through the exit's part;
  or a point takes a spot's place over a hop through the wall.

- **INV-8** — The file holds the fields of § 4.3, escaped, each number a
  float's shortest round-trip decimal.
  *Test:* `tests/unit/PathSeedsTest.cpp`: `toJson` for a map named with a
  quote, a backslash and an apostrophe, and a node at x one third, compared
  with a file authored by hand from § 4.3, where that x reads `0.33333334`.
  *Breaks when:* a name is written unescaped, a field is renamed, or a
  number is written as the double (`0.3333333333333333`) or with fixed
  digits.

- **INV-9** — Only `EXIT_OFF_NET` and `PARTITIONED` rows are work, a work
  row with no map file is skipped without failing the run, and a named map
  outside the work is refused with exit status 1.
  *Test:* `tests/unit/PathSeedsTest.cpp`, through `runCli` over a census
  written in the test and an install holding none of its maps: one row of
  each group and one of `NO_PATHS_BUILT`. Unnamed, standard output lists the
  two work rows, each skipped for having no file, and not the third, and the
  run exits 0; `<out>/ut-paths-summary.json` holds the same object. Naming the third exits 1, refusing it as outside the work.
  *Breaks when:* the group column is not read, a missing file fails the run,
  the summary is not written into the directory, or a named map outside the
  work is silently skipped.

- **INV-10** — The start is the first PlayerStart in the level's actor list,
  and the exits are every actor whose class descends from MonsterEnd, each at
  its resolved Location and collision size.
  *Test:* `tests/unit/PathSeedsTest.cpp`, through `sceneOf` over a map built
  with `tests/unit/BakeFixture.h` and packages declaring `Engine.PlayerStart`
  and `MonsterHunt.MonsterEnd`: two PlayerStarts; a MonsterEnd whose own
  CollisionRadius is set on the actor; and one of a MonsterEnd subclass the
  map declares. The start is the first PlayerStart; both exits are found, the
  first with its own radius.
  *Breaks when:* a later PlayerStart is taken, a subclass is missed by a
  class-name match, or the class default is read over the actor's own value.

## 6. Failure modes

| When | What happens |
|---|---|
| The map has no file at `<install>/Maps/<map>.unr` | It is skipped and named in the output; the run does not fail |
| The map does not open or read | That map is refused with the reason; no file is written |
| `buildCollision` refuses the level's `Model` | That map is refused, naming the node |
| The map has no PlayerStart, or no MonsterEnd | That map is refused |
| Neither the start nor any node of its part is on the walk graph | Every exit's route is `none` |
| A mover's tree or shape refuses | That map is refused, naming the actor |
| The out directory cannot be written | That map is refused; the others go on |

## 7. Tests

**Unit, on every CI leg:** `tests/unit/CoreMd5Test.cpp` for INV-1;
`tests/unit/PathTraceTest.cpp` for INV-2; `tests/unit/PathWalkableTest.cpp`
for INV-3 and INV-4; `tests/unit/PathSeedsTest.cpp` for INV-5, INV-6, INV-7,
INV-8, INV-9 and INV-10.
Each is seen failing before the code it locks exists. Trees and scenes are
built in memory, with `tests/unit/PathFixture.h`, so only INV-9 and INV-10
need an install or a fixture map.

**Real-asset tier, local only:** `tests/real/RealPathSeedsTest.cpp` runs over
every map in the install holding a MonsterEnd. It prints spots found, the
floor normal Z under every PlayerStart and PathNode (the check on § 3
decision 6), each PlayerStart's Location above its floor (the check on § 3
decision 4), and exits by route. It needs running with `!`: the session's
memory guard stops long runs.

**Mutation, by hand** (`CLAUDE.md` § Build and test): descend the far part
first; drop `outside` at a split; seat the spot on the floor; drop the
ceiling probes; reverse the slope test; skip the step check; hold a ramp to
the step; skip the join trace; skip the hop trace; lift the 350 cap; skip
the hop's floor check; drop the last node; propose a spot within 50 of a
navigation point; substitute a point without checking its hop; run a
`PARTITIONED` chain on to the exit; never block movers; follow edges both
ways; write a name unescaped; write the double; ignore the group column;
fail the run on a missing file; skip the directory summary; take a later
PlayerStart; read the class default over the actor's own value. Each must
be killed by the invariant that names it.

## 8. Alternatives considered (and rejected)

- **A swept cylinder against the tree.** Exact, and UTA-0017's to build for
  the game. A sampled body is enough here: UT's editor links the nodes, and
  UT_MonsterHunt's census re-run measures the result.
- **Seeds at a grid over every walkable spot.** It would flood the map and
  UT_MonsterHunt's 900-seed cap, and most seeds would repeat the network.
- **Jumps and drops.** More routes, but a route a bot cannot take proposes
  nodes UT's linker will not join. Conservative walking cannot.
- **A library under `src/`.** No runtime target needs this, and its one
  caller is this tool. A second caller, such as UTA-0060, moves it.
- **SHA-256 in place of MD5.** The format is agreed as MD5 (§ 3 decision 2).

## 9. Out of scope

- Lifts, teleporters and shot triggers — UT_MonsterHunt's GAME-0001, GAME-0004
  and GAME-0053.
- Their `PAWN_ANCHORED_NO_ROUTE` group — unexplained, and no node is known to
  help it (ROADMAP UTA-0121).
- Body-against-level collision for the game — UTA-0017.
- Decoding reach-spec flags and sizes — UTA-0085.
- Writing a map file, or linking nodes — UT_MonsterHunt's and UT's editor's.

## 10. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1 | `tests/unit/CoreMd5Test.cpp`, a unit test |
| INV-2 | `tests/unit/PathTraceTest.cpp`, a unit test |
| INV-3, INV-4 | `tests/unit/PathWalkableTest.cpp`, a unit test |
| INV-5, INV-6, INV-7, INV-8, INV-9, INV-10 | `tests/unit/PathSeedsTest.cpp`, a unit test |
| § 3 decisions 4 and 6 hold on real maps | **Partial:** `tests/real/RealPathSeedsTest.cpp` prints them; no CI leg runs it |
| Proposed nodes help a bot reach the exit | **nothing** here — UT_MonsterHunt's census re-run (GAME-0095) is the measure |

## 11. Cross-doc impact

- `CHANGELOG.md` — an `### Added` entry.
- `tools/ut-dump/main.cpp`, `tools/ut-bake/Cli.cpp` — their escaper copies
  are replaced by `tools/common/Json.h`.
- ROADMAP UTA-0121 — a note naming the output directory, sent to
  UT_MonsterHunt when this ships (their GAME-0095).

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0121-bot-path-seeds-loop-log.md`.

## 13. Resource cost

- No new dependency. One new tool target.
- The walk graph holds one record per spot and its edges, for one map at a
  time, and is freed before the next.
- § 3 decision 4's numbers come from a scratch probe reading the install's
  own class defaults with `upkg::readAncestry` and
  `upkg::effectiveDefaults`. It is not in this repository;
  `tests/real/RealPathSeedsTest.cpp` prints them again.

## 14. Open questions

1. **What size UT's editor tests a seed at while it builds paths.** No
   source reached here says. Its `Engine.Scout` defaults to CollisionRadius
   52 and CollisionHeight 50, larger than § 3 decision 4's body, so a node
   that body fits may not fit a Scout. UT_MonsterHunt's census re-run is what
   shows it (their GAME-0095).

UT_MonsterHunt confirmed on 2026-09-11 the details the agreed format left
unsaid: the MD5's file and form (§ 4.3), the map list and `-BP` rows
(§ 4.2), `moverOnly` (§ 4.3), and the start and exits (§ 4.6).
