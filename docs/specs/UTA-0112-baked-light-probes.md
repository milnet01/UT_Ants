# UTA-0112 — `ubake`: bake the level's bounced light into light probes

**Status:** spec draft (2026-09-11).
**Kind:** implement.
**Source:** ROADMAP UTA-0112 (split out of UTA-0011 by the user, 2026-09-10).

**Blocked by:** UTA-0011, shipped.  **Pairs with:** UTA-0014, which draws with
§ 4.3's light model and § 4.9's formula.

**Layman:** Work out ahead of time how light bounces off each level's walls and
floors, and store it at points spread through the level, so rooms and players
are lit softly and not just by their lamps.

## 1. Goal

A bake writes a new section, `LPRB`. It holds light probes: points on a lattice
near the level's surfaces. Each stores the light reaching it after one bounce
off the level, as six colours, one per axis direction — an ambient cube. The
renderer lights walls and moving bodies from them, with no second texture
coordinate set. This spec also fixes the light model the bake and the renderer
share: how a light's UT99 numbers become light at a point.

## 2. Problem

1. **The bundle carries no baked light.** `ubundle::Bundle` holds rooms, two
   graphs, textures, materials, geometry, placements, lights, movers and
   collision. ADR-0002 lists baked indirect light among what a bundle carries.
   `docs/design.md` § The stack, and what it rules out, says the visual target
   is reached with shadow maps, baked indirect light and volumetrics, with ray
   tracing neither required nor planned. The rays § 4.7 casts are the baker's
   own arithmetic on the CPU, at bake time; no graphics card traces them.
2. **UTA-0014 draws direct light only.** Its body: the level's light actors
   *"become real dynamic lights with shadow maps"*. Light bouncing off a wall
   reaches nothing unless it is baked.
3. **No light model exists.** UTA-0110 § 3 decision 1 keeps each light's UT99
   numbers in `LITE` and leaves turning them into colour to UTA-0014. A bake of
   bounced light needs that conversion now. The bake and the renderer must use
   the same one, or bounced light will not match direct light.
4. **UT99's own model is in no public source reached.** The public UT 432
   source, <https://github.com/FaultyRAM/Ut99PubSrc>, has no `Render`
   directory, and its `Engine/Src` holds no files:
   `gh api "repos/FaultyRAM/Ut99PubSrc/git/trees/HEAD?recursive=1" --jq '.tree[].path' | grep -c '^Engine/Src/'`
   → `0`. Surreal's headers (<https://github.com/stephank/surreal>) declare
   `FGetHSV` in `Engine/Inc/UnTex.h` without its body. They do define
   `AActor::WorldLightRadius` in `Engine/Inc/AActor.h`: `25 * (LightRadius + 1)`.
5. **UTA-0109 left this item a coordinate set to decide.** Its § 8: *"UTA-0112
   has not chosen how it bakes"*.
6. **A lattice over the level's box does not scale.** UTA-0098 measured
   CTF-Face's box as mostly sky, and umap's lattice over it was refused.

## 3. Scope decisions (agreed with the user)

The user asked me to use my own judgement while they were away (2026-09-11).
Every decision below is therefore mine and open to their review; § 15 names the
ones most worth it.

1. **Probes storing an ambient cube, not light maps.** A probe lights a wall
   and a moving body alike, and needs no second coordinate set. Valve's Source
   engine lit moving models this way: Mitchell, McTaggart and Green, *Shading in
   Valve's Source Engine*, SIGGRAPH 2006,
   <https://advances.realtimerendering.com/s2006/Mitchell-ShadingInValvesSourceEngine.pdf>.
   The cost is detail: baked light varies no faster than the probe spacing.
2. **One bounce, from static lights.** A probe stores the direct light of the
   level's static lights, reflected once by the surfaces it sees. Direct light
   stays UTA-0014's, drawn every frame with shadow maps.
3. **The light model is this spec's own** (§ 4.3), because UT99's is in no
   public source reached (§ 2 item 4). UTA-0014 draws direct light with it.
   `LITE` keeps UT99's numbers, as UTA-0110 decided. What changes is that a
   change to the model re-bakes every map, since the bounce was computed with
   it. `BAKER_REVISION` records that.
4. **Probes are seeded from the surfaces, not from the level's box**, so a box
   that is mostly sky costs nothing (§ 2 item 6). Sky surfaces seed nothing.
5. **Empty space is the level's collision tree**, UTA-0111 § 4.5's rule.
   `isEmpty` already implements it in `tools/ut-paths/Trace.cpp`, so that file
   moves into `ubake` rather than being written twice (§ 4.10).
6. **Rays meet the surfaces the renderer draws — `GEOM` — not the collision
   tree.** `GEOM` names each surface's material, which gives bounced light its
   colour. It is also what UTA-0014's shadow maps will be drawn from.
   Translucent and modulated surfaces let light through.
7. **A probe stores linear light as 32-bit floats**, in § 4.3's units. A
   tighter encoding is the renderer's to choose when it loads them.
8. **A surface's colour for the bounce is the linear mean of its material's
   base level.**
9. **The lattice spacing is 128 units and a probe casts 162 rays.** Neither is
   measured; § 15.
10. **The bake computes its own sine**, as a fixed polynomial (§ 4.3). A
    spotlight's direction needs one. `docs/design.md` § What every part does
    the same way keeps the platform maths library out of the baker. UTA-0052
    § 15 records the reading this spec follows: `sqrt` is correctly rounded by
    IEEE 754 and is used, while `sin` is in the family that differs between
    libraries and is not.

## 4. Design

### 4.1 The files

| File | What |
|---|---|
| `src/ubundle/Bundle.h`, `Sections.h`, new `LightProbeSection.cpp` | § 4.2's types, `ID_LPRB`, its reader and writer |
| new `src/ubake/CollisionQuery.h`, `.cpp` | moved from `tools/ut-paths/Trace.h` and `.cpp` — § 4.10 |
| new `src/ubake/LightModel.h`, `.cpp` | § 4.3 |
| new `src/ubake/SurfaceRays.h`, `.cpp` | § 4.7's rays against `GEOM` |
| new `src/ubake/LightProbes.h`, `.cpp` | § 4.4 to § 4.7 |
| `src/ubake/Bake.cpp` | § 4.5's colour, § 4.8's step |
| `src/ubake/Name.h` | `BAKER_REVISION` becomes `6` |
| `tools/ut-paths/Trace.h`, `.cpp` | reduced to § 4.10's forwarding |

### 4.2 The `LPRB` section

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 8;  // 8 since UTA-0112

/// One probe: a lattice point, and the light reaching it from each axis.
struct LightProbe {
    std::array<std::int32_t, 3> cell{};          ///< x, y, z; the probe is at cell * spacing
    std::array<std::array<float, 3>, 6> cube{};  ///< linear RGB, faces +X, -X, +Y, -Y, +Z, -Z
};

struct LightProbes {
    std::uint32_t spacing = 0;                    ///< UT units between lattice points
    std::vector<LightProbe> probes;               ///< strictly ascending by z, then y, then x
};

struct Bundle {
    // ... the existing members, then:
    std::optional<LightProbes> lightProbes;
};

}  // namespace uta::ubundle
```

**Face `k` is gathered from rays cast toward axis `a_k`, and lights a surface
whose normal is `a_k`.** So `cube[5]`, −Z, is the light rays cast downward
find, and it lights a ceiling.

**`LPRB`** is the bytes `L`, `P`, `R`, `B`. Its payload is `spacing` as `u32`,
then `vector<LightProbe>`. A `LightProbe` is `cell` as three `i32`, then `cube`
as eighteen `f32`, face by face, red, green and blue within each face: 84
bytes, fixed.

**Validation**, `MalformedData` on `read` and `InvalidArgument` on `write`:

- `spacing` is not zero;
- the probes are strictly ascending by `cell[2]`, then `cell[1]`, then
  `cell[0]`;
- every `cube` value is finite and not below zero.

**`write` emits it last**: `ROOM`, `NAVG`, `WIRG`, `TEXS`, `MATS`, `GEOM`,
`PLAC`, `LITE`, `MOVR`, `COLL`, `LPRB`. **`FORMAT_VERSION` becomes `8`.**

### 4.3 The light model

```cpp
namespace uta::ubake {

struct Rgb {
    double r = 0, g = 0, b = 0;
};

[[nodiscard]] Rgb lightColour(std::uint8_t hue, std::uint8_t saturation) noexcept;
[[nodiscard]] double lightRadius(std::uint8_t radius) noexcept;
[[nodiscard]] double falloff(double distance, double radius) noexcept;
/// `angle` in UT units, 65536 to a turn.
[[nodiscard]] double sineOf(std::int32_t angle) noexcept;
[[nodiscard]] double cosineOf(std::int32_t angle) noexcept;
/// A light's pointing direction from its pitch, yaw and roll.
[[nodiscard]] Vec3 directionOf(const std::array<std::int32_t, 3>& rotation) noexcept;
/// The light `light` puts on a surface at `x` with unit normal `n`, with no
/// shadow test.
[[nodiscard]] Rgb lightAt(const ubundle::Light& light, const Vec3& x, const Vec3& n) noexcept;
/// An 8-bit sRGB value, decoded to linear.
[[nodiscard]] double linearOf(std::uint8_t srgb) noexcept;

}  // namespace uta::ubake
```

`Vec3` is § 4.10's.

- **Colour.** Let `h = 6 × hue / 256` and `f = h − floor(h)`. The pure colour
  by sector `floor(h)` is: 0 → (1, f, 0); 1 → (1 − f, 1, 0); 2 → (0, 1, f);
  3 → (0, 1 − f, 1); 4 → (f, 0, 1); 5 → (1, 0, 1 − f). With
  `w = saturation / 255`, each channel of the light's colour is
  `pure × (1 − w) + w`. So saturation 255 is white, which makes `Light.uc`'s
  default `LightSaturation=255` a white light.
- **Intensity** is `brightness / 255`.
- **Radius.** `R = 25 × (radius + 1)`, `AActor::WorldLightRadius`.
- **Falloff.** `(1 − (d / R)²)²` for a distance `d` below `R`; `0` at `R` and
  beyond.
- **Incidence.** `max(0, n · l)`, with `l` the unit vector from `x` to the
  light. `1` when `effect` is `LE_NonIncidence` (13).
- **Spot**, when `effect` is `LE_Spotlight` (12) or `LE_StaticSpot` (8). With
  `c = 1 − cone / 256`, the factor is `clamp((dir · (−l) − c) / (1 − c), 0,
  1)`, and `0` when `cone` is `0`. `dir` is `directionOf(rotation)`:
  `(cos p × cos y, cos p × sin y, sin p)` for pitch `p` and yaw `y`. That is
  UTA-0119 § 4.5's `Y · P · R` applied to +X, so roll does not move it. The
  engine's own is `FRotator::Vector` in the public source's `Core/Inc/UnMath.h`.
  **Every other effect is baked as `LE_None`.**
- **A light at the point.** When `d` is `0`, the incidence and spot factors are
  `1`.
- **`lightAt`** is colour × intensity × falloff × incidence × spot, channel by
  channel.
- **Sine and cosine.** `sineOf` reduces its angle with integer arithmetic to
  the first eighth of a turn. There it evaluates fixed polynomials by Horner's
  rule, using only addition and multiplication. `cosineOf(a)` is
  `sineOf(a + 16384)`.
- **sRGB.** `linearOf` is a table of 256 literals generated offline from IEC
  61966-2-1's decoding: with `c = srgb / 255`, `c / 12.92` at or below
  `0.04045`, else `((c + 0.055) / 1.055)^2.4`.

**Units.** A surface of reflectance 1 facing a white light of brightness 255,
at the light, shows `1.0`. Exposure and tone mapping are UTA-0014's.

### 4.4 Which lights bake

```cpp
namespace uta::ubake {

/// The lights of `lights` that bake, in the order given.
[[nodiscard]] std::vector<ubundle::Light> bakedLights(const std::vector<ubundle::Light>& lights,
                                                      const ubundle::Placements& placements);

}  // namespace uta::ubake
```

A light bakes when all of these hold:

- its `type` is neither `LT_None` (0) nor `LT_BackdropLight` (6);
- `specialLit` is false. Such a light lights only `PF_SpecialLit` surfaces;
- **its resolved `bStatic` is true.** It is read with
  `ubake::detail::resolvedRecord`, over its placement's properties and then
  its class's defaults, as UTA-0110 § 4.6 resolves a light's fields. It is
  false when neither sets it. `Light.uc` sets `bStatic=True`, and
  `TriggerLight.uc` sets `bStatic=False`, so a light a script switches has no
  bounce. Both scripts: <https://github.com/Slipyx/UT99/tree/master/Engine>.

A light whose `exportIndex` has no placement does not bake. A pulsing,
flickering or strobing type bakes at its stored brightness.

### 4.5 A surface's colour

```cpp
namespace uta::ubake {

inline constexpr double DEFAULT_ALBEDO = 0.5;

/// The linear mean of an RGBA image's pixels whose alpha is not 0.
[[nodiscard]] std::optional<Rgb> meanAlbedo(const umat::Image& rgba) noexcept;

using AlbedoLookup = std::function<Rgb(std::string_view materialId)>;

}  // namespace uta::ubake
```

**`meanAlbedo`** sums `linearOf` of each channel over every pixel whose alpha is
not `0`, in row-major order, in double, and divides by their number. It is
empty when no pixel qualifies.

**`makeVariant` computes it** from the RGBA base level it hands to
`umat::generate`, and `bakeMaterials` keeps it by material id. A batch whose
`material` is empty, and a material whose `meanAlbedo` was empty, take
`DEFAULT_ALBEDO` on each channel.

### 4.6 Placing the probes

The spacing `S` is `128`.

1. **Each triangle of `GEOM` whose batch's `polyFlags` lack `PF_FakeBackdrop`
   (`0x80`)** gives a box: the minimum and maximum of its three positions, in
   double, grown by `S` on every side.
2. **Every lattice point `(i × S, j × S, k × S)` inside a box**, bounds
   included, is a candidate.
3. **A candidate is a probe when `isEmpty` accepts it** against `COLL`'s level
   tree.
4. **The probes are ordered by `k`, then `j`, then `i`**, each once.

### 4.7 Gathering a probe's light

```cpp
namespace uta::ubake {

class SurfaceRays {
public:
    explicit SurfaceRays(const ubundle::Geometry& geometry);

    struct Hit {
        std::size_t triangle = 0;  ///< its first index's position in `indices`, over 3
        double t = 0;              ///< along the direction
    };

    /// The nearest occluder a ray from `origin` along `direction` meets at t > 0.
    [[nodiscard]] std::optional<Hit> first(const Vec3& origin, const Vec3& direction) const;
    /// Whether an occluder crosses the segment from `a` to `b` strictly between them.
    [[nodiscard]] bool blocked(const Vec3& a, const Vec3& b) const;
};

/// The ray directions, in the order below.
[[nodiscard]] const std::vector<Vec3>& directions();

/// The six faces from one radiance per direction, in `directions()`' order.
[[nodiscard]] std::array<Rgb, 6> cubeOf(std::span<const Rgb> radiance);

/// The six faces of one probe at `p`, by the rules below.
[[nodiscard]] std::array<Rgb, 6> gatherProbe(const Vec3& p, const SurfaceRays& rays,
                                             const ubundle::Geometry& geometry,
                                             const std::vector<ubundle::Light>& lights,
                                             const AlbedoLookup& albedo);

/// Every probe of the level, with § 4.6's spacing.
[[nodiscard]] Result<ubundle::LightProbes> bakeLightProbes(
    const ubundle::Geometry& geometry, const ubundle::CollisionTree& level,
    const std::vector<ubundle::Light>& lights, const AlbedoLookup& albedo, JobSystem& jobs);

}  // namespace uta::ubake
```

**An occluder** is a triangle whose batch's `polyFlags` lack `PF_Translucent`
(`0x04`) and `PF_Modulated` (`0x40`). A ray meets a triangle as Möller and
Trumbore compute it, in double, from either side. Two hits at the same `t` go
to the lower triangle number, so the answer does not depend on how
`SurfaceRays` indexes the triangles.

**The directions** are the vertices of an icosahedron with each edge split at
its midpoint twice. The icosahedron's vertices are the cyclic permutations of
`(0, ±1, ±φ)`, `φ = (1 + √5) / 2`. Every vertex is normalised as `v / √(v · v)`,
and a new vertex is the normalised sum of its edge's two ends. That gives 162
directions, taken in ascending order of `z`, then `y`, then `x`.

**For each direction `ω`**, the radiance `L`:

1. No `first` hit: `L = 0`.
2. `n` is the hit triangle's first vertex's normal, normalised.
3. If `ω · n ≥ 0`, the ray met the surface from behind: `n` becomes `−n` when
   the batch has `PF_TwoSided` (`0x100`), and otherwise `L = 0`.
4. A batch with `PF_FakeBackdrop`: `L = 0`.
5. With `x = p + t × ω`, `E` is the sum over the baked lights, in their order,
   of `lightAt(light, x, n)`. A light counts `0` when
   `blocked(x + 0.5 × n, light.location)`.
6. `L` is the batch's albedo times `E`, channel by channel.

**Face `k`**, with axis `a_k`, is `Σ L × max(0, ω · a_k) / Σ max(0, ω · a_k)`,
both sums taken in the directions' order. It is stored as the nearest float.

**`bakeLightProbes`** places the probes by § 4.6, then runs `gatherProbe` for
each one through `jobs.parallelFor`. Each result goes to its probe's slot, and
nothing a probe computes depends on another, so the bytes do not depend on the
worker count. A job that throws refuses the bake.

### 4.8 The bake's step

`detail::bake` gains a step after `COLL`:

> **11. `LPRB`** is `bakeLightProbes` over `GEOM`, `COLL`'s level tree,
> `bakedLights` of `LITE` against `PLAC`, and step 7's albedo. Its refusal
> refuses the bake, naming the map.

The budget becomes step 12. The section is always written, with a spacing of
`128`, and holds no probes where none are placed. **`BAKER_REVISION` becomes
`6`**, and UTA-0011 INV-5's golden value is recorded again under it.

### 4.9 What the renderer does with a probe — UTA-0014's contract

For a unit normal `n` and one probe's `cube`:

```
indirect(n) = n.x² × cube[n.x ≥ 0 ? +X : −X]
            + n.y² × cube[n.y ≥ 0 ? +Y : −Y]
            + n.z² × cube[n.z ≥ 0 ? +Z : −Z]
```

That is the evaluation Valve's paper gives for an ambient cube (§ 3 decision 1).
A surface of reflectance `ρ` shows `ρ × (direct + indirect)`, where `direct` is
§ 4.3's `lightAt` summed over every light drawn, with its shadow map in place of
§ 4.7's `blocked`. How nearby probes are blended, and what a point with no probe
near it gets, are UTA-0014's. Nothing in this item checks this section.

### 4.10 Emptiness moves into `ubake`

`tools/ut-paths/Trace.h` and `.cpp` move to `src/ubake/CollisionQuery.h` and
`.cpp`, in namespace `uta::ubake`, with no change of behaviour. That covers
`Vec3`, `dot`, `length`, `isEmpty`, `Hit`, `trace` and `traceOut`.
**`horizontal` stays in `tools/ut-paths/Trace.h`**: it calls `std::hypot`,
which this spec keeps out of the baker. `tools/ut-paths/Trace.h` then brings the
moved names into `uta::paths` with using-declarations, so ut-paths' code and
tests change no line.

## 5. Invariants

- **INV-1** — `LPRB` round-trips, and `read` and `write` each refuse every
  violation § 4.2 lists: a zero spacing, two probes out of order, and a value
  that is negative or not finite.
  *Test:* `tests/unit/BundleLightProbesTest.cpp`.
  *Breaks when:* a check is dropped, or a field is written out of order.
- **INV-2** — `lightColour` follows § 4.3: hue 0 at saturation 0 is
  `(1, 0, 0)`, hue 64 is `(0.5, 1, 0)`, hue 128 is `(0, 1, 1)`, and saturation
  255 is `(1, 1, 1)` at every hue.
  *Test:* `tests/unit/BakeLightModelTest.cpp`, "light colour".
  *Breaks when:* saturation is inverted, so 255 gives the pure colour, or the
  sectors shift.
- **INV-3** — `lightRadius(0)` is `25` and `lightRadius(64)` is `1625`.
  `falloff` is `1` at distance 0, `0.5625` at half the radius, and `0` at the
  radius and beyond.
  *Test:* `tests/unit/BakeLightModelTest.cpp`, "radius and falloff".
  *Breaks when:* the radius drops its `+ 1`, or the falloff is linear.
- **INV-4** — `lightAt` gives `0` on a surface facing away from a light, and
  under `LE_NonIncidence` the same as on one facing it. A spotlight lights a
  point on its axis and not one behind it. `directionOf` gives +X at zero
  rotation, +Y at yaw 16384 and +Z at pitch 16384, whatever the roll.
  `sineOf` is within `1e-12` of `std::sin` at every angle from 0 to 65535.
  *Test:* `tests/unit/BakeLightModelTest.cpp`, "incidence and spot".
  *Breaks when:* the incidence clamp or the spot test is dropped, pitch's sign
  flips, or a polynomial term is dropped.
- **INV-5** — `linearOf` is within `1e-15` of IEC 61966-2-1's decoding at every
  byte. `meanAlbedo` leaves out every pixel whose alpha is `0`, and is empty
  when all are.
  *Test:* `tests/unit/BakeLightModelTest.cpp`, "sRGB";
  `tests/unit/BakeLightProbesTest.cpp`, "albedo".
  *Breaks when:* a literal is wrong, or a masked pixel is counted.
- **INV-6** — `bakedLights` keeps a static steady light. It drops a backdrop
  light, a special-lit light, a light whose class sets `bStatic` false, and a
  light whose own `bStatic` is false over a class default of true.
  *Test:* `tests/unit/BakeLightProbesTest.cpp`, "which lights bake".
  *Breaks when:* any one of § 4.4's rules is dropped, or the class default is
  read over the actor's own value.
- **INV-7** — In a box room spanning (16, 16, 16) to (400, 400, 400), with a
  sky triangle far outside it, the probes are exactly the lattice points inside
  the room's grown face boxes that `isEmpty` accepts. They are in § 4.6's
  order, and none is near the sky triangle. A level with no triangle writes a
  spacing of 128 and no probes.
  *Test:* `tests/unit/BakeLightProbesTest.cpp`, "placement". No wall or floor
  of that room lies on a lattice plane, so without § 4.6's growth there is no
  candidate at all.
  *Breaks when:* the growth is dropped, a sky triangle seeds, `isEmpty` is
  skipped, or the order differs.
- **INV-8** — What a ray sees. A single-sided lit surface facing away from a
  probe adds nothing, and the same surface with `PF_TwoSided` adds light. An
  opaque triangle between a light and the surface a probe sees removes that
  light, and the same triangle marked translucent does not. A lit surface with
  `PF_FakeBackdrop` adds nothing.
  *Test:* `tests/unit/BakeLightProbesTest.cpp`, "what a ray sees", through
  `gatherProbe`.
  *Breaks when:* the back-face test, the two-sided flip or the shadow test is
  dropped, a translucent triangle occludes, or a backdrop surface is counted.
- **INV-9** — The faces. A probe over a red floor, in a room with white walls
  and ceiling lit from above, has a −Z face whose red-to-green ratio exceeds
  its +Z face's. Doubling every baked light's brightness doubles every stored
  value exactly. `cubeOf` given equal radiance along every direction gives that
  radiance on every face, within `1e-12`.
  *Test:* `tests/unit/BakeLightProbesTest.cpp`, "faces". The doubling is exact
  because scaling one factor by two scales every rounded product, sum and
  quotient by two.
  *Breaks when:* the faces are swapped, the weights are not divided out, or
  intensity is not `brightness / 255`.
- **INV-10** — A bake with one worker and a bake with four write identical
  `LPRB` bytes. UTA-0011 INV-5's golden bake matches on every CI leg under
  `BAKER_REVISION` `6`.
  *Test:* `tests/unit/BakeLightProbesTest.cpp`, "workers";
  `tests/unit/BakeGoldenTest.cpp`.
  *Breaks when:* probes are written in the order jobs finish, a sum's order
  depends on a job, or a library sine enters the bake.
- **INV-11** — The moved emptiness code behaves as it did in place.
  *Test:* `tests/unit/PathTraceTest.cpp`, unchanged, against
  `src/ubake/CollisionQuery.cpp`.
  *Breaks when:* the move changes a line of behaviour.

## 6. Failure modes

- **A light inside solid.** The surfaces around it block its shadow rays, so it
  adds nothing, as its direct light will reach nothing either.
- **A crack between `GEOM` triangles.** A ray through it meets nothing and adds
  `0`, so that probe's face is slightly dark.
- **A probe inside a non-solid brush**, which `isEmpty` accepts. Its rays meet
  that brush's faces from behind, which add nothing.
- **A light with no placement** does not bake (§ 4.4).
- **A very large surface** seeds probes over its whole grown box. The cost
  grows with area, not with the level's box (§ 13).
- **A job that throws** refuses the bake.

## 7. Tests

Each test is seen to fail before the code it grades exists.

| File | Label | Locks |
|---|---|---|
| `tests/unit/BundleLightProbesTest.cpp` | `unit` | INV-1 |
| `tests/unit/BakeLightModelTest.cpp` | `unit` | INV-2, INV-3, INV-4, INV-5 |
| `tests/unit/BakeLightProbesTest.cpp` | `unit` | INV-5, INV-6, INV-7, INV-8, INV-9, INV-10 |
| `tests/unit/BakeGoldenTest.cpp` | `unit` | INV-10, recorded again |
| `tests/unit/PathTraceTest.cpp` | `unit` | INV-11, unchanged |
| `tests/real/RealLightProbesTest.cpp` | real tier | prints each stock map's probe count, baked light count and step time; checks every value is finite and not negative |

**A new fixture, `tests/unit/LightFixture.h` and `.cpp`,** builds `GEOM` for a
box room from its inward faces, one batch per material and flag set. It takes
the matching `COLL` tree from `PathFixture.h`'s `worldOf`.

**Mutations by hand**, each killed by the invariant named:

- INV-1: each validation dropped, on read and on write.
- INV-2: saturation inverted; the sectors shifted by one.
- INV-3: the radius's `+ 1` dropped; a linear falloff.
- INV-4: the incidence clamp dropped; `LE_NonIncidence` ignored; the spot test
  dropped; pitch's sign flipped; one polynomial term dropped.
- INV-5: one sRGB literal changed; alpha-0 pixels counted.
- INV-6: each rule dropped; the class default read first.
- INV-7: the growth dropped; the sky triangle kept as a seed; `isEmpty`
  skipped; the probes left unsorted.
- INV-8: the back-face test dropped; `PF_TwoSided` ignored; the shadow test
  dropped; a translucent triangle occluding; a backdrop surface counted.
- INV-9: +Z and −Z swapped; the weights not divided out.

## 8. Alternatives considered (and rejected)

- **Light maps on a second coordinate set.** Finer detail, but an atlas packer,
  a texel rasteriser and a gather per texel, and moving bodies need probes all
  the same.
- **A lattice over the level's box.** UTA-0098's sky (§ 2 item 6).
- **Rays against the collision tree.** It names no material, so bounced light
  would carry no surface's colour. It also keeps surfaces the renderer does not
  draw.
- **Per-light transfer**, so a light a script switches keeps a true bounce. It
  stores a cube per light per probe.
- **Emission from `PF_Unlit` surfaces.** A map made fullbright for its look
  would flood its neighbours with light.
- **Colours converted only in the renderer**, UTA-0110 § 8's choice. A bake of
  bounced light cannot wait for the renderer.
- **The platform's `sin`, under a recorded reading as UTA-0052 did for `sqrt`.**
  UTA-0052 § 15 names `sin` among the functions that differ between libraries.
- **More than one bounce.** Each costs a pass as large as the first.

## 9. Out of scope

- Drawing direct light, blending probes, and exposure — tracked by UTA-0014.
- ZoneInfo's `AmbientBrightness`, `AmbientHue` and `AmbientSaturation` — tracked
  by UTA-0014, a UT99 number the renderer turns into light like any other.
- Movers in the bake, as surfaces or as shadows — deferred; not yet queued.
- Bounce from lights a script changes — deferred; not yet queued.
- More than one bounce — deferred; not yet queued.
- Light leaking between probes through a thin wall — tracked by UTA-0014, whose
  blending decides it.

## 10. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1 | `tests/unit/BundleLightProbesTest.cpp` |
| INV-2, INV-3, INV-4 | `tests/unit/BakeLightModelTest.cpp` |
| INV-5 | `tests/unit/BakeLightModelTest.cpp` and `tests/unit/BakeLightProbesTest.cpp` |
| INV-6, INV-7, INV-8, INV-9 | `tests/unit/BakeLightProbesTest.cpp` |
| INV-10 | `tests/unit/BakeLightProbesTest.cpp`; **Partial:** `tests/unit/BakeGoldenTest.cpp` grades only the probes its fixture places |
| INV-11 | `tests/unit/PathTraceTest.cpp` |
| § 4.3's model looking like UT99's | **nothing** — UT99's model is in no public source reached; § 15 |
| § 4.9 | **nothing** until the renderer draws; tracked by UTA-0014 |
| The bake's cost on real maps | **Partial:** `tests/real/RealLightProbesTest.cpp` prints it; no CI leg runs it |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md`, in the same change as
  the code: § 4.2's minimum-size table gains `LightProbe`; § 4.3 and § 4.4
  gain version `8` and `LPRB`; § 4.10's API and order clause gain it; INV-4 is
  annotated with version `8`.
- `docs/specs/UTA-0011-map-baker.md` — § 4.5's steps gain § 4.8's step 11, and
  § 4.3's `BAKER_REVISION` moves to `6`. Recorded when built.
- `docs/specs/UTA-0109-map-geometry.md` — § 9's line on coordinates for baked
  light says none are needed, and points here.
- `docs/specs/UTA-0110-lights-and-placements.md` — § 9's line on turning light
  numbers into colour points at § 4.3 here as the model UTA-0014 draws with.
- `docs/specs/UTA-0121-bot-path-seeds.md` — § 4.4 names
  `src/ubake/CollisionQuery.cpp` as where `isEmpty` and `trace` now live.
  Recorded when built.
- `CHANGELOG.md` — an `### Added` entry, and a `### Changed` entry for format
  version `8`.
- ROADMAP UTA-0014 — a note naming § 4.3 and § 4.9 as its contract, and
  ZoneInfo's ambient light as its own.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0112-baked-light-probes-loop-log.md`.

## 13. Resource cost

- No new target and no new dependency.
- A probe is 84 bytes in the file. The count grows with the area of the
  level's non-sky surfaces, and the real-asset case prints it for every stock
  map.
- The bake's time grows with the probes, times 162 rays, times one plus the
  lights reaching each hit. The real-asset case prints it.

## 14. Migration / compatibility

**No `.utab` exists that version `8` orphans**: `0.1.0` has not been cut. A
version-`7` file is refused and baked over (UTA-0011 § 4.7).

## 15. Open questions

- **Whether § 4.3 looks like UT99.** The colour wheel, the falloff and the cone
  are this spec's, since UT99's are in no public source reached. Nothing can
  judge the look until UTA-0014 draws. A change then re-bakes every map, which
  costs nothing before `0.1.0`.
- **Whether 128 units and 162 rays are enough.** Neither was measured. The real
  tier prints the cost; the quality waits for the renderer.
- **Light leaking through thin walls.** If UTA-0014's blending cannot stop it
  alone, a per-probe mask would change `LPRB`.
- **Lights a script switches get no bounce.** UTA-0110's `LITE` keeps enough to
  revisit this with per-light transfer (§ 8).
- **Every decision in § 3 was made without the user**, who asked for that while
  away. They are the first thing to review.
