<!-- ants-spec-format: 1 -->
# UTA-0457 — a round body that slides through the level's collision

**Status:** accepted (2026-11-10), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0457 (user-decision-2026-11-10, split from UTA-0017).
**Blocker for:** UTA-0017, UTA-0458.

**Layman:** A player-shaped body moves through a map, stopping at walls and floors and sliding along them, with its edges where the model's are rather than a box around it.

## 1. Goal

`uworld` gains a body-collision world: the level's and each mover's
surfaces as one collision mesh, and a round body that can be swept
through it, slid along what it hits, and asked whether it fits.
UTA-0017's movement rules call it every tick. It decides where a body
stops; it never decides how a body wants to move.

## 2. Problem

`CollisionQuery.h` tests points and segments only. A player is a body
with a size, so movement needs a swept-volume query against the same
surfaces (UTA-0111's `COLL` section).

UT99 sweeps an upright square box (measured below). The user asked
that collision match the model instead of being bigger than it needs
to be (2026-11-10). This spec is where that becomes a contract.

**What the original does, measured** (2026-11-10, a throwaway probe
over `DM-Deck16][` baked at baker revision 78 and UTA-0456's
`20261110-130929` recordings at 60 fps; § 7's real-asset case prints
the same figures):

- **The body is a box.** In `wall-angle`, ticks 96 to 99, the player is
  stopped at a corner where the widest box that fits is 18.0 to 19.1
  units in half-width and the widest cylinder 19.4 to 23.9. Neither
  touches: UT99 stops a body 1 to 7 units short of a corner.
- **A walking player hovers.** Its centre stands 43.8 above a flat
  floor in `stand`, `stair`, `ledge` and `wall-angle`: 4.8 above its
  39-unit half-height. A falling one sinks to 41.2 (`ledge`, tick 135)
  and is lifted to 43.3 on landing, so the hover is walking's rule,
  not the sweep's.
- **A sliding player stays off a wall by a gap that shrinks as the frame
  rate rises** (`wall-angle`, a wall square to the axes): about 0.8 at
  30 fps once settled, 0.35 at 60 (a run `summary.tsv` marks missed), and
  0.24 at 85.

## 3. Scope decisions (agreed with the user)

1. **The body is an upright round cylinder**, not UT99's box. The
   user, 2026-11-10: collision matches the model. The shape was left
   to the session, which chose round over a body fitted to the mesh:
   the round body fits the model's outline while keeping the sizes
   maps were built around, so every doorway and ledge still fits.
2. **A body is sized per character.** The player's is UT99's TMale1,
   radius 17 and half-height 39 (UTA-0121). Other characters bring
   their own sizes (the user, 2026-11-10: characters differ in size).
3. **A body touches what it hits**, with a skin gap far below what an
   eye can see (§ 4.4). The user chose this over UT99's hover and wall
   gap (2026-11-10). The hover is walking's, so UTA-0017 owns removing
   it; this spec only makes touching possible.
4. **Jolt Physics does the sweeps.** The user left the choice to the
   session "as long as it is good and high quality" (2026-11-10). A
   round body's exact contact with edges and corners is where
   hand-written collision goes wrong; Jolt's cylinder cast is mature
   and tested, and UTA-0114 adopts it for 0.3.0's physics world
   anyway. The movement rules stay this project's (UTA-0114's body:
   "Player and bot movement stays UT99's").
5. **Judged by eye through UTA-0458's third-person view**, when it
   lands; the user, 2026-11-10: "If it looks right for the 3rd person
   view, then that is what should work for the first person view."

## 4. Design

### 4.1 Files

| File | Holds |
|---|---|
| `src/uworld/BodyCollision.h`, `.cpp` | § 4.3's world and § 4.4's queries |
| `src/uworld/CollisionMesh.h`, `.cpp` | § 4.2: a tree's surfaces as triangles |
| `CMakeLists.txt` | Jolt, fetched (§ 4.6) |
| `tests/unit/BodyCollisionTest.cpp` | § 7's unit cases |
| `tests/unit/CollisionMeshTest.cpp` | § 7's mesh cases |
| `tests/real/RealBodyCollisionTest.cpp` | § 7's recording cases |

### 4.2 The collision mesh

`collisionMesh(const ubundle::CollisionTree&)` returns triangles in the
tree's own space. For each node a walk from node 0 reaches (UTA-0111
§ 4.5) with an outline of three or more points:

1. **Probe both sides at several samples**: the outline's centroid `c`,
   and the midpoint from `c` to each of its points. At each sample `s`,
   with the node's plane normal `n`, ask `isEmpty` at `s + n·PROBE` and
   `s − n·PROBE`, with `PROBE` 0.5 units.
2. **Keep the face when any sample has exactly one side empty.** A face
   partly covered still bounds space where it is open; dropping it would
   leave a hole (found building it: a floor whose centroid lies under a
   pillar). A face empty on both sides at every sample is a sheet nothing
   stands on: water, a zone portal, a non-solid brush. Any other face is
   buried.
3. **Wind it to face the side most of those samples find empty**, and
   fan it into triangles from
   its first point. An outline's own order says nothing about which
   way it faces (UTA-0111 § 4.5).

### 4.3 The world

```cpp
namespace uta::uworld {

struct BodySize { double radius = 17, halfHeight = 39; };

class BodyWorld {
public:
    /// The level's mesh and each mover's, every mover at its MOVR pose.
    static Result<BodyWorld> build(const ubundle::Collision& collision,
                                   std::span<const ubundle::MoverShape> movers);
    /// Moves mover `i` (its index in `collision.movers`) to a new pose;
    /// refuses an `i` past the last, changing nothing.
    Result<void> placeMover(std::size_t i, const Vec3& location,
                            const std::array<std::int32_t, 3>& rotation);  // MOVR's pitch, yaw, roll
    ...
};

}
```

A mover's pose places its tree by UTA-0119 § 4.5's formula, with its
`MOVR` shape's `postScale`. A postScale after the rotation is not a
rotation and a scale Jolt can place a shape by, so a turn rebuilds the
mover's mesh; a move only re-places it. A mover whose `MOVR` shape is missing, or
whose mesh is empty, refuses the build, naming its export index.

### 4.4 The queries

```cpp
struct BodyHit {
    double fraction = 1;   // of the move travelled; 1 when nothing was hit
    Vec3 normal{};         // the hit surface's, facing the body
    bool startsInside = false;
};

struct BodyMove {
    Vec3 end;                    // where the body came to rest
    std::vector<BodyHit> hits;   // in order, at most MAX_SLIDES + 1
};

BodyHit  BodyWorld::sweep(const BodySize&, const Vec3& from, const Vec3& to) const;
BodyMove BodyWorld::slide(const BodySize&, const Vec3& from, const Vec3& delta) const;
bool     BodyWorld::fits(const BodySize&, const Vec3& at) const;
```

- **Positions are the body's centre, in UT99 units and axes.** The
  cylinder stands along Z.
- **`sweep` stops a skin short of contact.** With `d` the unit
  direction and `n` the hit normal, the body travels
  `max(0, contact − SKIN / max(|d·n|, 1/32))`, which leaves it `SKIN`
  off the hit surface; a body gliding nearly along a surface stops
  closer. `SKIN` is 0.125 units; the user chose touching (§ 3 item 3).
- **A body starting inside** returns `fraction 0`, `startsInside`, and
  as `normal` the way out of its deepest overlap. Where its centre is in
  solid, by the trees' `isEmpty`, `normal` is zero: a face knows its
  plane, not which side of it is solid. A body deep in solid touches no
  face, so the trees, not the mesh, are what find it.
- **`slide`** sweeps `delta`; on a hit it removes the remainder's
  component into the surface and sweeps again; on a second hit it moves
  along the crease the two surfaces make; it stops after `MAX_SLIDES`
  (2) slides. Stepping up, the floor and the hover are UTA-0017's.
- **`fits`** says whether the body at `at` overlaps nothing: no face by
  more than 0.01 units, and its centre not in solid.
- **Same results on every machine.** Jolt is built cross-platform
  deterministic (§ 4.6), so GCC, Clang and MSVC agree bit for bit.

### 4.5 Units handed to Jolt

Jolt's tolerances are tuned for metres. A position goes to Jolt as
UT99 units times `1/64`, a power of two, so the scaling is exact and a
map's ±32768 becomes ±512. The cylinder's rim is rounded by Jolt's
convex radius, 0.5 units, inside the body's size: the body is never
larger than `BodySize` says.

### 4.6 The dependency

Jolt Physics, route 1 by `docs/standards/dependency-acquisition.md`
§ 2: fetched at release tag `v5.6.0` (current, resolved 2026-11-10),
declared in the root `CMakeLists.txt`, its headers included as
`SYSTEM`. Built with `CROSS_PLATFORM_DETERMINISTIC` on and with its
options that would change this project's flags or runtime off. A row
joins `docs/design.md` § The stack.

## 5. Invariants

- **INV-1** — The mesh holds exactly the faces between empty and solid. A
  sheet with empty space on both sides is dropped, a buried face is dropped,
  a face partly covered is kept, and every kept face's normal points into
  empty space.
  *Test:* `CollisionMeshTest.cpp`, a hand tree of a room with a sheet across it.
  *Breaks when:* every outline is kept (water becomes a floor), or a face is
  wound by its outline's order.

- **INV-2** — A swept body stops `SKIN` off a wall it hits, at any angle.
  Swept at walls of 0°, 30° and 45° to the axes, the body's side ends between
  `SKIN − 0.01` and `SKIN + 0.01` off the wall.
  *Test:* `BodyCollisionTest.cpp`.
  *Breaks when:* the body is bigger than its size, or the skin is missing.

- **INV-3** — At a convex corner, the body's edge reaches the corner. Swept
  diagonally past a convex vertical corner, the body's centre comes within
  `radius + SKIN + 0.01` of the corner's edge.
  *Test:* `BodyCollisionTest.cpp`.
  *Breaks when:* the body is a box, or planes are pushed out by the body's
  size (UT99 stops 1 to 7 units short, § 2).

- **INV-4** — A body never ends inside solid. A sweep of 4096 units through
  an 8-unit wall stops before it, and `fits` holds at every sweep's end.
  *Test:* `BodyCollisionTest.cpp`.
  *Breaks when:* only the end point is tested.

- **INV-5** — A slide keeps the move's part along the wall. A move into a
  wall at an angle ends where that part takes it; a move into a 90° inside
  corner stops in the corner; a fall into a corner sharper than square
  slides on down its crease.
  *Test:* `BodyCollisionTest.cpp`.
  *Breaks when:* the body stops dead at a hit, or slides through the second
  wall.

- **INV-6** — A body starting inside says so and does not move: `sweep`
  returns `startsInside` and fraction 0, with the way out where its centre
  is in empty space, and a zero normal where it is in solid. A body wholly
  inside solid, touching no face, is inside.
  *Test:* `BodyCollisionTest.cpp`.
  *Breaks when:* a body overlapping solid is treated as free.

- **INV-7** — A mover blocks where it is placed: at its `MOVR` pose, and
  after `placeMover` at the new pose and not the old.
  *Test:* `BodyCollisionTest.cpp`.
  *Breaks when:* the pose is ignored, or a moved mover keeps its old place.

- **INV-8** — Results match on every compiler. A fixed sequence of sweeps
  and slides hashes to one constant recorded in the test, on GCC, Clang and
  MSVC.
  *Test:* `BodyCollisionTest.cpp`, on GitHub's matrix.
  *Breaks when:* Jolt is built without cross-platform determinism.

- **INV-9** — Our body fits wherever the original's did, and moves where it
  moved. Over UTA-0456's `stand`, `wall-angle`, `stair` and `ledge` runs at
  every rate the recording held, on `DM-Deck16][`: `fits` holds at every
  recorded location; a sweep between two rows the original moved between
  freely (the same Z, or falling without landing) travels the whole way; and
  where the original slid along a wall, our body finds that wall within 2
  units: the original's gap reaches 1.57 on a slide's first ticks at 30 fps,
  and a missing wall would leave 5 units free.
  *Test:* `RealBodyCollisionTest.cpp`, which also prints § 2's hover and
  each rate's wall gap. It reads the level only: a mover's `MOVR` shape
  needs the whole bake's materials, and these runs ride no mover.
  *Breaks when:* the body or mesh is bigger than the original's (the first
  two), or a wall is missing from the mesh (the third).

## 6. Failure modes

- A recording missing, or the install missing: the real case fails
  and names what it lacks; it never skips.
- A mover with no `MOVR` shape: `build` refuses (§ 4.3).
- Jolt's version check failing at start-up: `build` refuses.

## 7. Tests

Unit cases on hand trees: INV-1 in `CollisionMeshTest.cpp`; INV-2, INV-3,
INV-4, INV-5, INV-6, INV-7 and INV-8 in `BodyCollisionTest.cpp`.
Rooms are built as `CollisionTree`s with real outlines, so INV-1's
builder is what the sweeps run on. INV-9's real case, label `real`, bakes
`DM-Deck16][` from the install and reads recordings from the directory
`UTA_MOVEMENT_RECORDINGS` names, through `readMovementRecording`.

## 8. Alternatives considered (and rejected)

- **UT99's box, planes pushed out by its size.** Matches the
  recordings exactly; the user rejected a body bigger than its model.
- **A body fitted to the mesh.** Tightest, but changes which gaps a
  player fits through, and maps were built for 17 by 39.
- **A capsule.** Rounded feet sink at ledges and step edges.
- **Our own cylinder sweep.** No new dependency until 0.3.0, but the
  edge and corner cases are the hard part and would be ours.
- **Jolt's `CharacterVirtual`.** It brings its own stepping and
  floor rules; ours must match UT99's, so we take the sweep only.

## 9. Out of scope

Walking, falling, stepping up, the floor hover and every movement
constant (UTA-0017). Bodies against bodies (UTA-0017 and UTA-0114).
The physics world for debris (UTA-0114). Drawing the body (UTA-0458).

## 10. What checks this

| Invariant | Checked by |
|---|---|
| INV-1 | `CollisionMeshTest` |
| INV-2, INV-3, INV-4, INV-5, INV-6, INV-7 | `BodyCollisionTest` |
| INV-8 | `BodyCollisionTest` on GitHub's matrix |
| INV-9 | `RealBodyCollisionTest`, run by hand: nothing runs the real tier automatically |
| § 3 item 5, looks right | Partial: the user's eye through UTA-0458, after it lands |

## 11. Cross-doc impact

- `src/uworld/CollisionQuery.h`'s header names UTA-0017 as body
  collision's owner; it becomes UTA-0457.
- `docs/design.md` § The stack: Jolt's row.
- UTA-0114: Jolt arrives here, at `v5.6.0`; its spec confirms the
  pin rather than choosing it.
- UTA-0017: reads § 4.4; owns the hover.

## 12. Cold-eyes loop log

No review: the gate is cancelled here (`CLAUDE.md` § Contract
reviews). Building it is the reviewer.

## 13. Resource cost

The mesh's size and build time on `DM-Deck16][` are printed by the
real case; unmeasured until then.
