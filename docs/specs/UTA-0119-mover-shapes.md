# UTA-0119 — `ubake`: bake each mover's own shape into the bundle

**Status:** accepted (2026-09-11), at the review's cap.
**Kind:** implement.
**Source:** ROADMAP UTA-0119 (user-request-2026-09-11, split from UTA-0110).

**Blocked by:** UTA-0109 and UTA-0110, both shipped.
**Blocker for:** UTA-0014, which draws each shape; UTA-0016, whose walk
through a level needs its doors and lifts.
**Pairs with:** UTA-0104 (the map standard).

**Layman:** Doors, lifts and other moving parts get their shapes baked, so
they appear in the level instead of being invisible.

## 1. Goal

A bake writes each mover's shape — a door, a lift — into a new section,
`MOVR`: its triangles in the mover's own space, and the numbers that place it
in the level. The renderer draws each mover where the level puts it, and a
later item can move one by changing those numbers, without a re-bake.

## 2. Problem

1. **A mover's shape is not in `GEOM`.** UTA-0109 bakes the level's `Model`
   only. A mover carries its own, through `Actor.uc`'s
   `var const export model Brush; // Brush if DrawType=DT_Brush.`, so a bake
   today has no doors and no lifts.
2. **That `Model` is in the brush's own space.** Its points sit around the
   origin rather than around the actor's `Location`, and its `Polys` corners
   lie on those points. The engine places it with `ABrush::ToWorld`:
   `GMath.UnitCoords * Location * PostScale * Rotation * MainScale * -PrePivot`
   (Surreal's `Engine/Inc/ABrush.h`, the 432 headers).
3. **A mover's textures are often ones the level never wears.** Step 5 of the
   bake (UTA-0011 § 4.5) makes materials from the level `Model`'s surfaces
   alone, so a mover would have no material for them.
4. **A class name does not say what is a mover.** The engine's test is
   `AActor::IsMovingBrush`:
   `Brush!=NULL && IsA(ABrush::StaticClass()) && !bStatic` (Surreal's
   `Engine/Inc/UnActor.h`). A class named like a mover need not descend from
   `Brush`.

The measurements behind items 2 and 3, and behind § 3 decisions 3 and 5, are
printed by the real-asset case (§ 7), `tests/real/RealMoversTest.cpp`.

## 3. Scope decisions (agreed with the user)

The user decided that movers get this item of their own, before 0.1.0 (ROADMAP
UTA-0119). The choices below are mine.

1. **A mover is what the engine calls a moving brush** (§ 2 item 4): a class
   descending from `Engine.Brush`, a resolved `bStatic` of false, and a
   `Brush` set. Mine. A static brush is left out because the level's `Model`
   already holds it: the editor merged it in.
2. **Triangles are baked in pivot space; the renderer does the rest.** Pivot
   space is the brush's points with `PrePivot` subtracted and `MainScale`
   applied. `Location`, `Rotation` and `PostScale` are stored beside them.
   Mine, for two reasons. The bake then computes no sine or cosine, so its
   bytes do not depend on a platform's maths library, which UTA-0011 INV-5's
   golden bake checks on every CI leg. And a mover that later moves needs only
   those three numbers changed.
3. **Shear is not applied.** Mine, on measurement. With the engine's shear
   applied as ported (§ 4.5), the corners of sheared static brushes land on
   none of the level's points, where ignoring it lands some. With shear, the
   transform does not split into the two parts decision 2 needs. The
   real-asset case prints how many movers carry one.
4. **`bHidden` does not decide.** A hidden mover is baked. `PLAC` carries
   `bHidden`, and a script can change it while a level runs. Mine.
5. **Each shape is built by UTA-0109's `buildGeometry` over the mover's own
   `Model`, unchanged.** Mine: a mover's `Model` carries the BSP tables that
   function reads, so a second triangulator over `Polys` is not needed.
6. **A texture only a mover wears gets its material made in step 5**, as a
   level texture does. Mine.
7. **The placement formula uses exact sine and cosine, not the engine's
   table.** Mine. `GMath.SinTab` drops each angle's lowest two bits; with the
   table and without it, the static-brush measurement lands corners at the
   same share, and the real-asset case prints both (§ 7).

## 4. Design

### 4.1 The files

| File | Holds |
|---|---|
| `src/ubundle/Bundle.h` | `MoverShape`, `Bundle::movers`, `FORMAT_VERSION` (§ 4.2) |
| `src/ubundle/Sections.h`, `MoverSection.cpp` | the `MOVR` codec and its validation |
| `src/ubake/Movers.h/.cpp` | `findMovers` and `buildMover` (§ 4.3 to § 4.5) |
| `src/ubake/Bake.cpp`, `src/ubake/Name.h` | the bake's steps, materials over movers, `BAKER_REVISION` (§ 4.6) |
| `tests/support/FCoordsPort.h` | a port of the engine's FCoords operators, with `GMath`'s table or exact sine and cosine: INV-7 grades § 4.5's formula against it, the real-asset case places static brushes with it, and UTA-0014 grades its placement against it |

`MoverSection.cpp` joins `uta_ubundle`, and `Movers.cpp` joins `uta_ubake`. No
target or link changes.

### 4.2 The `MOVR` section

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 6;  // 6 since UTA-0119

/// One mover's shape, in its pivot space (UTA-0119 SS 4.5).
struct MoverShape {
    std::uint32_t exportIndex = 0;            ///< its slot in the map's export table, as PLAC's
    std::array<float, 3> location{};          ///< as placed, UT99's own units
    std::array<std::int32_t, 3> rotation{};   ///< pitch, yaw, roll; 65536 to a turn
    std::array<float, 3> postScale{1, 1, 1};
    Geometry geometry;                        ///< GEOM's shape: vertices, indices, batches
};

struct Bundle {
    // ... the existing members, then:
    std::optional<std::vector<MoverShape>> movers;  ///< strictly ascending by exportIndex
};

}  // namespace uta::ubundle
```

**`MOVR`** is the bytes `M`, `O`, `V`, `R`. Its payload is
`vector<MoverShape>`. A `MoverShape` is `exportIndex` as `u32`, `location` as
three `f32`, `rotation` as three `i32`, `postScale` as three `f32`, then
`geometry` in `GEOM`'s own encoding (UTA-0109 § 4.2). **Minimum encoded size:
52 bytes** — the four fixed fields and three empty vectors.

**Validation**, `MalformedData` on `read` and `InvalidArgument` on `write`:
shapes strictly ascending by `exportIndex`; each shape's `geometry` by
UTA-0109 § 4.2's rules. The floats are not validated: a zero `postScale` is
the renderer's to handle, as a light's numbers are (UTA-0110 § 4.4).
`ubundle` does not check that a shape's `exportIndex` has a placement; the
baker guarantees it (INV-3).

**`write` emits it last**: `ROOM`, `NAVG`, `WIRG`, `TEXS`, `MATS`, `GEOM`,
`PLAC`, `LITE`, `MOVR`. **`FORMAT_VERSION` becomes `6`.**

### 4.3 Which actors are movers

An actor of `PLAC` (UTA-0110) is a mover when all three hold:

1. **Its class entry is resolved, and its `path` or one of its `ancestry`
   paths is `engine.brush`.** An unresolved class is not a mover: which
   classes it descends from is not known. Nor is a class whose ancestry
   ends, at a missing package or class, before reaching `engine.brush`.
2. **Its resolved `bStatic` is false.** Resolved as UTA-0110 § 4.6 resolves a
   light field: the actor's own record of that name at array index `0` and of
   kind `Bool`, else its class's default, else `Actor.uc`'s default, false.
   `Brush.uc` sets it true and `Mover.uc` false.
3. **Its own `Brush` property names an object.** It is read from the actor's
   own property list with `upkg::readProperties`, for the reference itself. A
   `Brush` an actor inherits from its class's defaults is not baked (§ 9).

### 4.4 A mover's numbers

Each resolves as § 4.3 item 2 does — the actor's own record, else its class's
default, else the default given:

| Field | Property | Kind | Default |
|---|---|---|---|
| `location` | `Location` | `Vector` | zero |
| `rotation` | `Rotation` | `Rotator` | zero |
| — | `PrePivot` | `Vector` | zero |
| — | `MainScale` | `Raw`, struct `Scale` | one on each axis |
| `postScale` | `PostScale` | `Raw`, struct `Scale` | one on each axis |

**A `Scale` value is seventeen bytes**: `Scale` as three `f32`, then
`SheerRate` as `f32` and `SheerAxis` as `u8`, little-endian — `Core/Object.uc`'s
`struct Scale`. A `Raw` value of another struct name or length is passed over,
as a record of the wrong kind is. `SheerRate` and `SheerAxis` are read and not
applied (§ 3 decision 3).

### 4.5 Building a shape

```cpp
namespace uta::ubake {

/// The movers among `actors`, by SS 4.3, each with its Brush Model's export.
[[nodiscard]] Result<std::vector<MoverSite>> findMovers(const upkg::Package& map,
                                                        const ubundle::Placements& actors);

/// One mover's shape, by SS 4.4 and SS 4.5.
[[nodiscard]] Result<ubundle::MoverShape> buildMover(const upkg::Package& map,
                                                     const MoverSite& mover,
                                                     const ubundle::Placements& actors,
                                                     const MaterialLookup& lookup);

}  // namespace uta::ubake
```

`MoverSite` holds the actor's placement index and its `Model`'s export.

**For each mover, in export order:**

1. **`buildGeometry` over its `Model`**, with the lookup the bake's materials
   step made. Its refusal refuses the bake, naming the actor.
2. **Each vertex's position `p` becomes `MainScale ⊙ (p − PrePivot)`**,
   componentwise, computed in double and stored as float. Its normal `n`
   becomes `n ⊘ MainScale`, normalised. Its `u` and `v` are kept:
   `buildGeometry` computed them in the brush's own space, so a texture
   stretches with its brush.
3. **When the product of `MainScale`'s three components is negative**, each
   triangle's second and third indices swap, so its front face stays the one
   its normal points out of.
4. **`location`, `rotation` and `postScale`** are § 4.4's.

**The renderer places a pivot-space point `q` at**
`location + postScale ⊙ (Y · P · R · q)`, with `c` and `s` the exact cosine
and sine of `2π × angle / 65536`:

```
Y = | c_yaw  -s_yaw  0 |    P = | c_pitch  0  -s_pitch |    R = | 1  0        0      |
    | s_yaw   c_yaw  0 |        | 0        1   0       |        | 0  c_roll   s_roll |
    | 0       0      1 |        | s_pitch  0   c_pitch |        | 0 -s_roll   c_roll |
```

That is `ABrush::ToWorld` applied with `FVector::TransformPointBy`, with its
shear left out, split after `MainScale`, and with exact sine and cosine where
the engine reads `GMath`'s table (§ 3 decision 7). The FCoords operators it
rests on are Surreal's `Core/Inc/UnMath.h`: `operator*=` taking a `FVector`, a
`FRotator`, a `FScale` and a `FCoords`, and `TransformPointBy`. INV-7 checks
the formula against a port of them.

### 4.6 The bake's steps

`detail::bake`'s steps become, with each refusal still naming the map:

1. to 4. The level, its world, `ROOM`, `NAVG` and `WIRG`, unchanged.
5. **`PLAC` and `LITE`**, UTA-0110's, moved here from after `GEOM`: the
   movers are found from them.
6. **The movers**, by `findMovers`, and each one's `Model` read.
7. **`TEXS` and `MATS`**, over the level's `Model` and every mover's `Model`
   together. A surface of any of them contributes its texture and variant as
   a level surface does today (UTA-0011 § 4.6).
8. **`GEOM`**, unchanged.
9. **`MOVR`**, `buildMover` over each mover, in export order.
10. **The budget**, unchanged.

`MOVR` is written, and empty where the level has no mover. **`BAKER_REVISION`
becomes `4`**, and UTA-0011 INV-5's golden value is recorded again under it.

### 4.7 As built (2026-09-11)

- **`buildMover` takes the mover's `Model`, already read, rather than the
  map.** § 4.6 reads each mover's `Model` once, in step 6, for the materials,
  and step 9 builds from the same one.
- **The rule "the actor's own record, else its class's default" is one
  function**, `ubake::detail::resolvedRecord` in `src/ubake/Actors.h`. The
  light fields and every number in § 4.4 go through it.
- **A mover `Model` that does not read keeps its refusal's code**, as INV-9
  says, and no test can show the code being kept. Every `Model` of a map is
  at the map's own package version, so once the level's `Model` has read, a
  mover's cannot be refused as `UnsupportedVersion`. INV-9's case for it uses
  bytes that do not decode, which is `MalformedData` either way.
- **`tests/unit/BakeTest.cpp`'s check that the bake takes the level's own
  `Model`** now picks the level's and the decoy's by name. The standard
  fixture's mover brings a third `Model`, its brush, so the check sees three.
- **The standard fixture's mover** is a door of `Engine.Mover` wearing
  `TexPkg.Metal.Door`, which no level surface wears. The golden bake's case
  asserts it shapes that one mover.
- **Mutation, by hand:** each mutation in § 7's list was killed by the
  invariant naming it, and one mutation for each rule this item adds by that
  rule's case. "Apply the shear" was mutated as reading `SheerRate` into the
  scale, since the code never reads the shear to apply it.
- **The real-asset case compiles and has not been run.** Its figures are for
  whoever runs it against an install.

## 5. Invariants

- **INV-1** — `MOVR` round-trips through `ubundle::write` and `ubundle::read`,
  every float bit included, and `write` emits it after `LITE`.
  *Test:* `tests/unit/BundleMoversTest.cpp`: a payload authored from § 4.2
  field by field, never by `write`, holding two shapes — one with a `-0.0`
  location and a negative `postScale`, each with a two-batch geometry — in a
  bundle that also carries `LITE`. `read` decodes it and `write` reproduces
  it. One shape with empty geometry encodes to exactly 52 bytes.
  *Breaks when:* a field is encoded at another's width, or `MOVR` is emitted
  before `LITE`.

- **INV-2** — `read` refuses with `MalformedData`, and `write` with
  `InvalidArgument`: two shapes of one slot; shapes out of order; a shape
  whose geometry has an index past its vertices.
  *Test:* `tests/unit/BundleMoversTest.cpp`, one case per rule, each fixture
  breaking that rule alone.
  *Breaks when:* a rule is checked on one path only, or a shape's geometry is
  not validated.

- **INV-3** — An actor gets a shape exactly when § 4.3 makes it a mover, and
  each shape's `exportIndex` is that actor's placement's.
  *Test:* `tests/unit/BakeMoversTest.cpp`: a map holding an actor of
  `Engine.Mover`; one of `Engine.Brush` that sets `bStatic` false itself; one
  of `Engine.Mover` that sets `bStatic` true; one of `Engine.Brush`; one of a
  class named `WeaponRemover` that does not descend from `Brush`; one of a
  mover class whose package is missing; and one of `Engine.Mover` with a
  null `Brush`. Every actor but the last carries a `Brush` naming a `Model`
  export, so each one left out is left out by the rule it isolates. The
  first two get shapes and no other does; the second is what fails a test
  on the class's name.
  *Breaks when:* a class name decides, `bStatic` is read from only one of the
  actor and its class, or an unresolved class is guessed at.

- **INV-4** — A shape's vertices are `MainScale ⊙ (p − PrePivot)` of
  `buildGeometry`'s vertices over the mover's `Model`, its normals are
  `n ⊘ MainScale` normalised, its `u` and `v` are `buildGeometry`'s, and its
  triangles are reversed when `MainScale`'s product is negative.
  *Test:* `tests/unit/BakeMoversTest.cpp`: a mover whose `Model` is one
  square tilted 45° about Y, with `PrePivot` `(8, 0, 0)` and `MainScale`
  `(2, -1, 1)`. Its positions, normal, `u`, `v` and index order are asserted.
  The tilt is what lets the normal fail: an axis-aligned normal normalises to
  itself whether it is scaled or divided.
  *Breaks when:* `PrePivot` is subtracted after scaling, a normal is scaled
  rather than divided, `u` and `v` are recomputed after the transform, or the
  winding is kept under a mirror.

- **INV-5** — `location`, `rotation` and `postScale`, and the `PrePivot` and
  `MainScale` a shape is built with, are the actor's own values, else its
  class's defaults, else § 4.4's.
  *Test:* `tests/unit/BakeMoversTest.cpp`: a mover whose class default
  `PostScale` is `(1, 1, 2)` and whose actor sets `Location`, `Rotation` and
  `MainScale`; and one setting nothing.
  *Breaks when:* only the actor's own list is read, or only the class's
  defaults are.

- **INV-6** — Shear is not applied: a `SheerAxis` and `SheerRate` on
  `MainScale` leave a shape's vertices as they are without them.
  *Test:* `tests/unit/BakeMoversTest.cpp`: two movers of one `Model` — INV-4's
  tilted square, whose corners vary in both x and z — and one `MainScale`,
  one of them carrying `SheerAxis` `SHEER_ZX` and `SheerRate` `0.8`. The
  engine's `FSheerSnap` keeps that rate at `0.65`; a rate within `0.05`
  snaps to none, and an axis the square does not vary along moves nothing,
  so either would hide an applied shear.
  *Breaks when:* the shear is applied.

- **INV-7** — § 4.5's placement formula equals `ABrush::ToWorld` applied with
  `FVector::TransformPointBy`, computed with exact sine and cosine, for any
  `Location`, `Rotation`, `PrePivot`, `MainScale` and `PostScale` without
  shear. This checks the document: no code of this item computes the
  formula. UTA-0014's renderer will, and its test grades against the same
  port.
  *Test:* `tests/unit/BakeMoversTest.cpp`: a port of the FCoords operators
  § 4.5 cites, kept in `tests/support/FCoordsPort.h`, with exact sine and
  cosine, against the formula, both in double. The inputs turn each rotation axis alone and together, by angles
  that are not multiples of 4, and carry a negative scale and a non-zero
  `PrePivot`. Every coordinate lies within 4096 of the origin and every scale
  component between 1/16 and 16 in magnitude. The two agree within `0.001`,
  absolute.
  *Breaks when:* the formula's rotation order, a sign, or where `PostScale`
  sits differs from the engine's.

- **INV-8** — Every batch of every shape names a `MATS` id or none, and a
  texture only a mover wears has its variant made.
  *Test:* `tests/unit/BakeMoversTest.cpp`, through `detail::bake`: a map whose
  mover wears a texture no level surface wears.
  *Breaks when:* the materials come from the level's `Model` alone.

- **INV-9** — A mover whose `Brush` names no `Model` export of the map, or
  whose `MainScale` has a zero component, refuses the bake with
  `MalformedData` naming the actor. One whose `Model` or geometry does not
  read refuses it with that refusal's own code, naming the actor, as the
  level's `Model` does (UTA-0011 § 4.5): `readModel` refuses a version-61
  package as `UnsupportedVersion`.
  *Test:* `tests/unit/BakeMoversTest.cpp`, one case for each.
  *Breaks when:* such a mover is baked, dropped without a word, or its
  refusal's code is replaced.

## 6. Failure modes

| When | What happens |
|---|---|
| A mover's `Brush` names an import, or an export that is not a `Model` | The bake is refused, naming the actor |
| A mover's `Model`, or its geometry, does not read | The bake is refused with that refusal's own code, naming the actor |
| A mover's `MainScale` has a zero component | The bake is refused, naming the actor |
| An actor's class does not resolve | It is not a mover; `PLAC` records the class as it does today |
| A `Scale` value is not seventeen bytes of struct `Scale` | It is passed over, and the next source is used |
| A mover carries a shear | It is baked without it (§ 3 decision 3) |
| The level has no mover | `MOVR` is written empty |

## 7. Tests

**Unit, on every CI leg:** `tests/unit/BundleMoversTest.cpp` for INV-1 and
INV-2; `tests/unit/BakeMoversTest.cpp` for INV-3, INV-4, INV-5, INV-6,
INV-7, INV-8 and INV-9. Each but INV-7 is seen failing before the code it
locks exists: INV-7 checks § 4.5's formula, which no code of this item
computes.

**The fixtures grow.** The standard fixture's `Engine` package holds `Brush`
(`bStatic` true) and `Mover` under it (`bStatic` false). `MapBuilder` in
`tests/unit/BakeFixture.h` gains a brush `Model` export and an actor naming it
through `Brush`, and the standard fixture gains a mover wearing a texture no
level surface wears. UTA-0011 INV-5's golden bake then covers `MOVR`.

**Real-asset tier, local only:** `tests/real/RealMoversTest.cpp` runs
`buildActors`, `findMovers` and `buildMover` over every map, with a lookup
that makes no material, as `tests/real/RealGeometryTest.cpp` does. It prints
movers baked; maps refused, by reason; movers carrying a shear; mover
`Model`s with no BSP nodes, whose shape comes out empty; mover `Model`s whose
points sit nearer the origin than the actor; and textures only a mover
wears. It also places every static brush's `Polys` corners with
`tests/support/FCoordsPort.h`, the port INV-7 grades against, and prints the
share landing on a level point, by transform property, with `GMath`'s table
and with exact sine and cosine. That is what shows the port is the engine's.

**Mutation, by hand** (`CLAUDE.md` § Build and test): take a class name for
the mover test; read `bStatic` from the actor alone; subtract `PrePivot` after
scaling; scale a normal instead of dividing it; keep the winding under a
mirror; apply the shear; make materials from the level's `Model` alone; swap
two rotations in INV-7's formula. Each must be killed by the invariant that
names it.

## 8. Alternatives considered (and rejected)

- **Triangles placed in the world at bake time.** The bake would compute sine
  and cosine, whose last bits vary by platform, and a mover that moves would
  need its brush space back.
- **Triangulating `Polys` rather than the BSP.** `buildGeometry` reads the
  BSP already, and every mover's `Model` carries one; `Polys` would need a
  second triangulator.
- **Applying the engine's shear.** § 3 decision 3.
- **Mover triangles inside `GEOM`.** A mover must stay separable, to be moved
  and to be joined to its placement.
- **`PrePivot` and `MainScale` left to the renderer.** No stock script
  changes a mover's `PrePivot` or `MainScale` while a level runs, so baking
  them once keeps UE1's pivot and scale rules out of the renderer. A map's own
  script could; that is the movement item's to handle (§ 9).

## 9. Out of scope

- Moving a mover — its keyframes, `BasePos` and states — not yet queued.
- Drawing a shape — tracked by UTA-0014.
- A `Brush` an actor inherits from its class's defaults — not yet queued.
- A mover's shear — not yet queued (§ 3 decision 3).
- A mover's collision — UTA-0111.

## 10. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1, INV-2 | `tests/unit/BundleMoversTest.cpp`, a unit test |
| INV-3, INV-4, INV-5, INV-6, INV-7, INV-8, INV-9 | `tests/unit/BakeMoversTest.cpp`, a unit test |
| `BAKER_REVISION` covering the movers | **Partial:** `tests/unit/BakeGoldenTest.cpp`, a golden-hash test, catches what its fixture places |
| The FCoords port being the engine's | **Partial:** `tests/real/RealMoversTest.cpp`, a real-asset test, prints the static-brush share; no CI leg runs it |
| Shear being right to leave out | **nothing** — the measurement in § 3 decision 3 is printed, not asserted |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md`, in the same change as
  the code: § 4.2's minimum-size table gains `MoverShape`; § 4.3 and § 4.4
  gain version `6` and `MOVR`; § 4.10's API and order clause gain it; INV-4
  is annotated with version `6`.
- `docs/specs/UTA-0011-map-baker.md` — § 4.5's steps take § 4.6's order,
  § 4.6's materials take the movers' `Model`s, and § 4.3's `BAKER_REVISION`
  moves to `4`. Recorded when built.
- `docs/specs/UTA-0110-lights-and-placements.md` — § 4.7's step position gains
  a pointer to § 4.6 here.
- `docs/specs/UTA-0109-map-geometry.md` — § 9's movers line points here.
- `CHANGELOG.md` — an `### Added` entry, and a `### Changed` entry for format
  version `6`.
- ROADMAP UTA-0014 — a note that its placement of a mover reproduces § 4.5's
  formula, graded against `tests/support/FCoordsPort.h`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0119-mover-shapes-loop-log.md`.

## 13. Resource cost

- No new target and no new dependency.
- One `buildGeometry` per mover, over a `Model` far smaller than the level's.
- Materials for textures only movers wear: TEXS grows by what those
  textures take.

## 14. Migration / compatibility

**No `.utab` exists that version `6` orphans**: `0.1.0` has not been cut. A
version-`5` file is refused and baked over (UTA-0011 § 4.7).
