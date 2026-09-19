# UTA-0164 — baked ambient occlusion

**Status:** spec draft (2026-09-19). No review: the user chose a short spec
with no cold read, 2026-09-19.
**Kind:** feature.
**Source:** ROADMAP UTA-0164 (user-request-2026-09-15, split from UTA-0015).

**Layman:** corners and creases get a soft shadow, so rooms look solid
instead of evenly lit.

## 1. Goal

A bake measures, for every lit surface of the level, how much of the space
just above it is enclosed by other surfaces. It stores that as a greyscale
image in a new `AOCC` section, with a second texture coordinate per `GEOM`
vertex that points into it. The renderer darkens a surface's indirect and
ambient light by that value, at every tier.

## 2. Problem

1. `scene.frag` shades a lit surface as `base * (direct + indirect +
   ambient)`. Nothing in that sum knows about nearby geometry.
2. The probes cannot stand in. `ubake::PROBE_SPACING` is 128 units, so a
   probe's occlusion is spread over a whole cell, and `probePoint` reads it
   half a spacing off the surface (UTA-0185).
3. Zone ambient is flat by design (UTA-0156 § 3), so it lights a room's
   corners exactly as brightly as its middle.
4. `GEOM` gives each BSP polygon vertices of its own. `ubake::buildGeometry`
   step 8 fans each polygon from its first vertex, and no vertex is shared
   between polygons. So a polygon can have its own region of an image with
   no vertex split.

## 3. Scope decisions

- **Baked, not screen-space** — the session's call, 2026-09-19, under the
  user's cheapest-first direction and their wording "in the baked maps". It
  costs one texture read a pixel, so every tier draws it.
- **Indirect and ambient only.** Direct light already has shadow maps.
  Occlusion on it would darken a lamp-lit corner twice.
- **The level only.** Movers and actors are not occluded and occlude
  nothing new: a mover's shape moves, and a baked value would be wrong once
  it did.
- **How it looks is set by research, not by asking the user** — the user's
  standing instruction. The original game has no occlusion to measure
  against, so § 4.3's constants cite shipping engines instead.

## 4. Design

### 4.1 The `AOCC` section — `ubundle`

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 14; // 14 since UTA-0164
inline constexpr std::uint32_t OCCLUSION_ATLAS_LIMIT = 4096;

/// The level's baked ambient occlusion -- UTA-0164 SS 4.1.
struct Occlusion {
    float texelSize = 0;                         ///< UT units a texel spans
    std::uint32_t width = 0, height = 0;         ///< the atlas, in texels
    std::vector<std::array<float, 2>> uv;        ///< one per GEOM vertex, 0 to 1
    std::vector<std::uint8_t> texels;            ///< width * height, row-major
};

} // namespace uta::ubundle
```

`Bundle` gains `std::optional<Occlusion> occlusion`, written after `ZONE`.

On the wire: `texelSize` as f32, `width` and `height` as u32, `uv` as a
vector of two f32 each, `texels` as a vector of u8.

`read` and `write` refuse:

- `width` or `height` of 0, or above `OCCLUSION_ATLAS_LIMIT`;
- `texels` whose length is not `width * height`;
- a `texelSize` that is not finite and positive;
- a `uv` component that is not finite or lies outside `[0, 1]`;
- a `uv` count different from `GEOM`'s vertex count, or `AOCC` present with
  `GEOM` absent. This is a rule across two sections, so it runs once every
  section is decoded, as `validateVertexZones` does.

### 4.2 Charts and the atlas — `ubake`

A **chart** is the triangles of `GEOM` that share their first index. That is
one polygon, by `buildGeometry` step 8. Its vertices are the run from that
index to the largest index its triangles name.

A chart is **lit** when its batch's `polyFlags` has none of `PF_Unlit`,
`PF_FakeBackdrop`, `PF_Invisible` or `PF_Portal`. Only lit charts get
texels.

**The plane basis** comes from the chart's first vertex's `normal`, `n`.
Take the world axis whose component of `n` is smallest in magnitude, lowest
axis on a tie. Then `U = normalize(cross(n, axis))` and `V = cross(n, U)`. A
point `p` has texel coordinates `(dot(p, U), dot(p, V)) / texelSize`.

Two coplanar polygons from one BSP cut share `n` exactly, so they share the
basis and the texel grid. Their texels sample the same world points, which
is what hides the cut.

**A chart's rectangle** is the whole texels covering its vertices' texel
coordinates, grown by one texel on every side.

**The white block** is a 4 by 4 region at the atlas origin, every texel 255.
Every vertex of an unlit chart gets its centre as `uv`.

**Packing.** Charts are placed in order of rectangle height, tallest first,
then by first index. Each goes on the current shelf, left to right; a new
shelf starts when it does not fit the width. The width starts at the
smallest power of two, at least 64, whose square holds the charts' total
area. It doubles while the height passes it, up to
`OCCLUSION_ATLAS_LIMIT`. If the limit is reached and the height still
passes it, `texelSize` doubles and packing restarts. The height is rounded
up to a multiple of 4.

A vertex's `uv` is its texel coordinate relative to its chart's placed
rectangle, plus the rectangle's origin, divided by the atlas size.

### 4.3 The occlusion value — `ubake`

```cpp
namespace uta::ubake {

/// Starting texel size, in UT units.
inline constexpr float OCCLUSION_TEXEL_SIZE = 16;

/// How far an occluder counts, in UT units.
inline constexpr double OCCLUSION_DISTANCE = 64;

/// How far above the surface a ray starts, in UT units.
inline constexpr double OCCLUSION_LIFT = 0.5;

[[nodiscard]] Result<ubundle::Occlusion> bakeOcclusion(
    const ubundle::Geometry& geometry, JobSystem& jobs);

/// The same, starting from `texelSize`, so a test reaches the coarsening rule.
[[nodiscard]] Result<ubundle::Occlusion> bakeOcclusion(
    const ubundle::Geometry& geometry, JobSystem& jobs, float texelSize);

} // namespace uta::ubake
```

For a lit chart's texel at `(i, j)`:

1. Its world point `q` is on the chart's plane, at texel coordinate
   `(i + 0.5, j + 0.5)`.
2. If `q` lies outside the polygon, it moves to the nearest point of the
   polygon, measured in the plane.
3. The ray origin is `q + n * OCCLUSION_LIFT`.
4. For each `w` in `ubake::directions()` with `c = dot(w, n) > 0`, cast
   `rays.first(origin, w)`. A hit at `t <= OCCLUSION_DISTANCE` occludes by
   `1 - t / OCCLUSION_DISTANCE`; a miss or a farther hit by 0.
5. The value is `1 - sum(c * occlusion) / sum(c)`, over the directions in
   the order `directions()` gives. It is stored as `round(255 * value)`.

Texels outside every chart are 255.

`OCCLUSION_DISTANCE` sits between Unity's baked occlusion default of 1 m
and Unreal Engine's `MaxOcclusionDistance` default of 200 cm, taking a
player 78 units tall as about 1.8 m. Those defaults are recalled, not
checked here.

`OCCLUSION_TEXEL_SIZE` puts four texels across `OCCLUSION_DISTANCE`, so a
corner's darkening spans several texels rather than one.

The value depends on its texel alone, and the sums run in a fixed order, so
the section is the same byte for byte at any worker count. Jobs split the
lit charts.

### 4.4 The baker's part — `ubake`

`bake` calls `bakeOcclusion` after the probes. It builds its own
`SurfaceRays` over `GEOM`, as `bakeLightProbes` does. A level with no `GEOM`
vertex has no `AOCC`. The texel size and atlas size are in the section itself.

`BAKER_REVISION` is bumped.

### 4.5 The renderer — `urender`

- The atlas uploads as one `VK_FORMAT_R8_UNORM` image, one mip level, into
  the bindless texture array. `FrameData` gains `occlusionTexture`, `NONE`
  when the bundle has no `AOCC` or the tier is below the feature's.
- `Feature::AmbientOcclusion` joins the tier table with `Tier::Low`.
- A second vertex buffer, binding 1, holds a `vec2` per vertex in the
  concatenated order `SceneGeometry::upload` builds. The level's come from
  `AOCC`. A mover's vertices all get the white block's centre, and so does
  the level when `AOCC` is absent.
- `scene.vert` passes it through. `scene.frag` reads the red channel at
  level 0 through the material sampler, and multiplies `indirect + ambient`
  by it. `direct` and `emitted` are untouched. The one-texel border keeps
  every filtered read inside its own chart, so the sampler's wrap mode never
  matters.
- The renderer's bundle fingerprint (`shapeOf` in `Frame.cpp`) samples
  `AOCC`: its size, and the ends of its uvs and texels. Without it, a bundle
  differing only in `AOCC` keeps the old atlas.
- The shadow pipelines do not bind it.

## 5. Invariants

- **INV-1** — `AOCC` round-trips § 4.1's fields, and `read` and `write` each
  refuse every case § 4.1 lists.
  *Test:* `tests/unit/BundleOcclusionTest.cpp`, new.
  *Breaks when:* a check runs on one path only; the vertex count is checked
  against a mover's geometry instead of `GEOM`'s.

- **INV-2** — in a box room, a texel on the floor more than
  `OCCLUSION_DISTANCE` from every wall stores 255. A texel at the foot of a
  wall stores less than one at the floor's centre. A texel in a corner where
  two walls meet the floor stores less than one at the foot of one wall.
  *Test:* `tests/unit/BakeOcclusionTest.cpp`, new.
  Under a ceiling of the floor's size, the floor stores less at a height of 8
  than at 32, and 255 at 128.
  *Breaks when:* the hemisphere is taken about `-n`; the falloff is
  inverted.

- **INV-3** — two coplanar polygons sharing an edge, one starting 5 units
  further from a wall than the other, store values within one of each other
  in texels either side of that edge at the same distance from the wall.
  *Test:* `tests/unit/BakeOcclusionTest.cpp`.
  *Breaks when:* the basis depends on anything but the normal, or the grid is
  anchored at the chart instead of the world.

- **INV-4** — no two placed chart rectangles overlap, each lies inside the
  atlas, none overlaps the white block, and every lit vertex's `uv` lies
  inside its chart's rectangle shrunk by one texel. Every unlit vertex's `uv`
  is the white block's centre.
  *Test:* `tests/unit/BakeOcclusionTest.cpp`.
  *Breaks when:* the border is dropped; a shelf wraps late.

- **INV-5** — charts whose total area passes `OCCLUSION_ATLAS_LIMIT` squared
  bake with `texelSize` `2 * OCCLUSION_TEXEL_SIZE` or more, and within the
  limit.
  *Test:* `tests/unit/BakeOcclusionTest.cpp`.
  *Breaks when:* packing passes the limit instead of coarsening.

- **INV-6** — `bakeOcclusion` gives the same section, byte for byte, at 1, 2
  and 4 workers.
  *Test:* `tests/unit/BakeOcclusionTest.cpp`.
  *Breaks when:* a sum's order depends on job scheduling.

- **INV-7** — `minimumTier(Feature::AmbientOcclusion)` is `Tier::Low`.
  *Test:* `tests/unit/RenderTiersTest.cpp`, extended.
  *Breaks when:* the row is missing or names another tier.

- **INV-8** — a square lit by zone ambient alone, reading an atlas texel of
  128, draws its ambient times 128/255. Lit by a light alone, it draws the
  same with and without `AOCC`.
  *Test:* `tests/device/RenderOcclusionTest.cpp`, new.
  *Breaks when:* the atlas is not bound; the fingerprint ignores `AOCC`;
  occlusion multiplies `direct`.

## 6. Failure modes

- A map whose charts cannot fit even at a coarse texel size would loop.
  Packing stops doubling at 1024 units a texel and refuses the bake with
  `MalformedData`; no real map is expected near it.
- A polygon so thin its chart has no interior texel still gets its border,
  so its vertices read a clamped neighbour value rather than white.
- A chart whose stored normal has no length gets no texels and reads the
  white block, as an unlit chart does.

## 7. Tests

All carry the `unit` label but INV-8, which carries `device`.

- INV-1 — `tests/unit/BundleOcclusionTest.cpp`, new.
- INV-2 to INV-6 — `tests/unit/BakeOcclusionTest.cpp`, new.
- INV-7 — `tests/unit/RenderTiersTest.cpp`, extended.
- INV-8 — `tests/device/RenderOcclusionTest.cpp`, new.
- `tests/unit/BakeGoldenTest.cpp`, re-recorded.

**Measured by hand after the code lands:**

1. Bake the three reference maps. Record the bake time added, the texel size
   and the atlas size, in UTA-0164's body.
2. Frame a corner of AS-Frigate with `ut-shot`, with and without `AOCC`, and
   confirm the corner darkens and open floor does not.

### 7.1 As built (2026-09-19)

- Every mutation named in § 5's *Breaks when* lines was run by hand and
  failed its test. So did dropping the border, skipping the coarsening,
  skipping either vertex-count check, and not checking the uv range.
- Setting `OCCLUSION_LIFT` to 0 fails no test. The rays leave the plane, so
  a surface does not meet itself in these fixtures. The lift stays as a
  margin against rounding, and no test grades it.
- A ray searches only to `OCCLUSION_DISTANCE` (`SurfaceRays::first`'s
  `limit`), and a texel with no occluder corner in front of its plane within
  that distance skips its rays (`SurfaceRays::anyInFront`). Both are exact:
  the three reference maps baked to the same bytes with and without them.
  The bake times are in UTA-0164's roadmap body.
- INV-8 found a defect before it passed: the fingerprint did not sample
  `AOCC`, so the renderer kept the first bundle's atlas.

## 8. Alternatives considered (and rejected)

- **Screen-space occlusion.** Lost: it costs frame time every frame, is
  usually off on the lowest tier, and halos at depth edges. It stays open
  for movers and actors later.
- **Occlusion per vertex.** Lost: a BSP polygon is large and has few
  corners, so a corner's darkening would smear across the whole wall.
- **Occlusion in the probes.** Lost: § 2 consequence 2.
- **A second texture coordinate inside `GeometryVertex`.** Lost: it changes
  the vertex record every consumer reads, including mover shapes, which carry
  no occlusion.
- **Block compression for the atlas.** Deferred: an 8-bit atlas is at most
  16 MiB, and most maps are far smaller. BC4 can follow if sizes say so.

## 9. Out of scope

- Occlusion on movers and actors — deferred; not yet queued.
- Refitting `AMBIENT_SCALE` and `EXPOSURE` for the darker ambient — tracked
  by UTA-0187, which runs after this item for that reason.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleOcclusionTest.cpp` |
| INV-2 to INV-6 | `tests/unit/BakeOcclusionTest.cpp` |
| INV-7 | `tests/unit/RenderTiersTest.cpp` |
| INV-8 | `tests/device/RenderOcclusionTest.cpp` |
| Bake cost and atlas size on real maps | **nothing** — § 7's measurement is run by hand |
| How it looks in a real corner | **nothing** — § 7's measurement is run by hand |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md` — the section order.
- `docs/specs/UTA-0014-vulkan-draw-path.md` — ambient occlusion is no longer
  deferred; the second vertex binding.
- `CLAUDE.md` § Where this project is — the bundle format version.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

None: no review, by the user's choice.

## 13. Migration / compatibility

`FORMAT_VERSION` becomes `14`. `ubundle::read` refuses any other version, so
every map baked before this item must be baked again.
