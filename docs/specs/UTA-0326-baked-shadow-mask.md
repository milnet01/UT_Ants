<!-- ants-spec-format: 1 -->
# UTA-0326 — bake the shadows of lights that never move

**Status:** accepted (2026-10-07), unreviewed. Contract reviews are cancelled for this
project (`CLAUDE.md` § Contract reviews); no review runs unless the user asks.
**Kind:** perf.
**Source:** ROADMAP UTA-0326 (user-request-2026-10-06, from UTA-0323's timings).

**Layman:** most lights and walls never move, so the baker works out once
which parts of each wall each light can reach, and the game reads that
instead of testing shadows every frame.

## 1. Goal

The baker stores, for every lit polygon of the level and every light that
reaches it, how much of that light each patch of the polygon sees past the
level's own geometry. It goes in a new `SMSK` section. The renderer lights a
level surface from its polygon's own list of lights and reads that stored
value instead of the shadow map. Moving geometry still shadows through the
shadow map, and no surface is ever shadowed by both.

## 2. Problem

1. **Shadow reads are most of the frame on the slow maps.** UTA-0323 timed
   `ut-bench frame` at ultra, 3840x2160, on the RX 6600, over baker revision
   42 bundles, each probe alternated with its baseline in one `cc-job`. On
   DM-Bishop the frame took about 80 ms, 27 ms with shadows off, 48 ms with
   one shadow read a light instead of nine, and 12 ms with no direct light.
   Skipping the reads of a light that gives a point nothing brought it to
   65 ms. DM-Closer, first six `closer-fly.txt` cameras, still frames: 25.6
   to 28.1 ms shipped, 16.8 with shadows off, 19.4 with one read, 10.1 with
   no direct light. The figures are in UTA-0323's roadmap body.
2. **The reads repeat work whose answer never changes.** `softShadowOf` in
   `src/urender/shaders/shadows.glsl` makes nine filtered depth compares per
   light per pixel. A UT99 light and the level's walls do not move
   (UTA-0014 § 4.8), so for a level surface the answer is the same every
   frame. The tiles are already drawn once and kept (`ShadowPlanner`); only
   the per-pixel reads remain.
3. **Four channels a texel cannot hold these maps.** Unity's shadowmask packs
   four overlapping lights a texel. A scratch probe counted, for each lit
   polygon, the lights whose reach meets it and whose centre is in front of
   it, before any occlusion. Area-weighted lights per polygon, median /
   90th / largest: DM-Bishop 38 / 55 / 67, DM-Closer 12 / 20 / 39, DM-Crane
   5 / 19 / 32. Giving each light one of four channels, two lights sharing
   a polygon never sharing one, needs 75, 39 and 40 channels by greedy
   colouring, and leaves 172 of Bishop's 183 lights without one.
   *Command:* `~/.cache/uta-scratch/u326/overlap <bundle>`, built from
   `overlap.cpp` beside it against `build/`'s `uta_ubundle`, over the
   UTA-0323 bundles in `~/.cache/uta-scratch/u323/bake/`.
4. **The cluster list over-serves a level surface.** A fragment loops over
   every light whose sphere meets its cluster's box (UTA-0014 § 4.6),
   including lights a wall stands between, and a cluster holds at most 64
   (`CLUSTER_CAPACITY`). Bishop has polygons reached by 67 (consequence 3).

## 3. Scope decisions (agreed with the user)

- **This item before UTA-0325's scalar light loop** — the user, 2026-10-06.
- **Baked, per polygon and per light** — the session's call, from § 2
  consequence 3: a fixed channel budget fails on every measured map.
- **Moving geometry keeps today's shadow maps, and a surface it could
  shadow reads only them** — the session's call. That is the answer to
  shadowing twice: for one light on one polygon, the renderer uses exactly
  one of the two, never a blend.
- **A level surface loops over its polygon's list, not its cluster's** — the
  session's call. The list is baked, so it already leaves out lights a wall
  blocks entirely, and it has no cap.
- **No change to how the shadow atlas is planned or drawn.** The fog
  (UTA-0015) and moving geometry still read it.

## 4. Design

### 4.1 Which lights are baked — `ubundle`

```cpp
namespace uta::ubundle {

/// Whether the renderer draws `light` in its direct term and it can put
/// light on a surface: not LT_BackdropLight, not specialLit, not an absorbed
/// strip light, brightness not 0, and not a spot effect with cone 0.
[[nodiscard]] bool litDirectly(const Light& light) noexcept;

} // namespace uta::ubundle
```

It moves the rule now in `Lights.cpp`'s `drawsDirectly` and the brightness
and cone tests of `shadowFacesOf` into one place. The baker pairs only these
lights. `directLights` keeps its own extra case, a brightness-0 light with a
fog volume, which lights no surface.

### 4.2 The `SMSK` section — `ubundle`

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 23; // 23 since UTA-0326
inline constexpr std::uint32_t SHADOW_MASK_ATLAS_LIMIT = 4096;
inline constexpr std::uint32_t MASK_NO_CHART = 0xFFFFFFFF;
inline constexpr std::uint16_t MASK_ALL_LIT = 0xFFFF;

/// One light on one polygon.
struct MaskPair {
    std::uint32_t light = 0;     ///< an index into LITE
    std::uint16_t x = 0, y = 0;  ///< the rectangle's corner in the atlas, or both MASK_ALL_LIT
    std::uint8_t moverReach = 0; ///< 1 where moving geometry can come between (SS 4.4)
    std::array<std::uint8_t, 3> reserved{}; ///< always zero
};

/// One lit polygon of GEOM.
struct MaskChart {
    std::uint32_t firstPair = 0, pairCount = 0; ///< a run of `pairs`
    std::uint16_t width = 0, height = 0;        ///< its rectangle, border included
};

/// The level's baked shadow mask -- UTA-0326 SS 4.2.
struct ShadowMask {
    float texelSize = 0;                           ///< UT units a texel spans
    std::uint32_t width = 0, height = 0;           ///< the atlas
    std::vector<std::uint32_t> vertexChart;        ///< per GEOM vertex; MASK_NO_CHART where unlit
    std::vector<std::array<float, 2>> vertexTexel; ///< per GEOM vertex, in its rectangle's texels
    std::vector<MaskChart> charts;
    std::vector<MaskPair> pairs;   ///< per chart, ascending by `light`
    std::vector<std::uint8_t> texels; ///< width * height, row-major
};

} // namespace uta::ubundle
```

`Bundle` gains `std::optional<ShadowMask> shadowMask`, written after `FLAM`.
On the wire every field is in the order above, little-endian, a vector as
its count and then its elements.

`read` and `write` refuse:

- `width` or `height` of 0 or above `SHADOW_MASK_ATLAS_LIMIT`, or `texels`
  not `width * height` long;
- a `texelSize` that is not finite and positive;
- `vertexChart` or `vertexTexel` not GEOM's vertex count, or `SMSK` present
  with `GEOM` absent — a rule across sections, run once all are decoded, as
  `validateVertexZones` is;
- a `vertexChart` entry that is neither `MASK_NO_CHART` nor below the chart
  count; a `vertexTexel` component not finite or outside `[0, width]` of its
  chart (`[0, height]` for the second);
- a chart whose pair run passes the end of `pairs`, or runs that overlap;
- a pair whose `light` is not below LITE's count, or `SMSK` present with
  `LITE` absent; pairs of one chart not strictly ascending by `light`;
- a pair whose rectangle, `x` to `x + width` and `y` to `y + height` of its
  chart, passes the atlas, or whose `x` alone is `MASK_ALL_LIT`;
- `moverReach` above 1, or a non-zero `reserved` byte.

### 4.3 The mask value — `ubake`

```cpp
namespace uta::ubake {

/// Starting texel size, in UT units.
inline constexpr float SHADOW_MASK_TEXEL_SIZE = 8;

/// How far above the surface a ray starts, in UT units.
inline constexpr double SHADOW_MASK_LIFT = 0.5;

[[nodiscard]] Result<ubundle::ShadowMask> bakeShadowMask(
    const ubundle::Bundle& bundle, JobSystem& jobs);

/// The same from `texelSize`, so a test reaches the coarsening rule.
[[nodiscard]] Result<ubundle::ShadowMask> bakeShadowMask(
    const ubundle::Bundle& bundle, JobSystem& jobs, float texelSize);

} // namespace uta::ubake
```

**Charts, the plane basis and the texel grid are UTA-0164 § 4.2's**, at this
section's texel size: a chart is one polygon, lit by the same flags, its grid
anchored at the world and its rectangle grown by one texel each side.
`vertexTexel` is a vertex's texel coordinate less its rectangle's corner.

**The occluders are what the shadow pass draws** (`recordFrame`'s shadow
tiles): every GEOM triangle but those whose batch has `PF_Translucent`,
`PF_Modulated`, `PF_NotSolid` or `PF_FakeBackdrop`. A ray that meets a
`PF_Masked` triangle passes through where its base texture's alpha at the
hit, at pan time 0, is below 0.5 (`MASK_THRESHOLD`), as `shadow.frag`
discards. `SurfaceRays` today hits `PF_FakeBackdrop` and ignores alpha, so
the bake needs a variant that matches this rule.

*As built:* nothing decodes the bundle's BC7 textures, so the alpha comes
from the picture the bake decodes for each masked material. `bakeShadowMask`
takes a third argument, `const Cutouts& cutouts = {}`: per MATS id, 1 where
that picture's alpha is at least half, read at the hit's wrapped coordinate.
A `PF_Masked` surface with no cutout is solid.

**A pair is a chart and a `litDirectly` light that can light it**: the
light's sphere — `lightRadius`, grown by half a strip leader's segment —
meets the polygon, and either the light's effect has no incidence term
(`LE_NonIncidence`, `LE_Cylinder`) or the point it is lit from lies in front
of the polygon's plane somewhere.

**For each texel of a pair's rectangle**, border included:

1. Four points at `(i + 0.25 or 0.75, j + 0.25 or 0.75)` each go through
   UTA-0164 § 4.3's steps 1 to 3, with `SHADOW_MASK_LIFT` as the lift.
2. From each origin `o`, a segment runs to the point the light is lit from
   at `o` — its location, or the nearest point of a strip leader's segment,
   as `litFrom` in `light.glsl` takes it. The point is visible when no
   occluder meets the segment short of its end.
3. The texel stores `round(255 * visible / 4)`.

**A pair whose every texel is 255 carries no texels**: its `x` and `y` are
`MASK_ALL_LIT`. **A pair whose every texel is 0 is dropped**: the light
reaches no part of the chart the mask can show. A chart with no pair left
keeps its entry with a run of 0.

**Packing** is UTA-0164 § 4.2's shelf rule over the rectangles of the pairs
that carry texels, with `SHADOW_MASK_ATLAS_LIMIT` in place of its limit and
no white block: tallest first, then by chart, then by light. Past the limit
the texel size doubles and the bake restarts, to a cap of 1024 units, past
which the bake is refused with `MalformedData`.

Each texel depends on its pair alone and jobs split the charts, so the
section is the same byte for byte at any worker count.

### 4.4 Moving geometry — `ubake` and `urender`

The mask's occluders are the level's alone: a mover is not in GEOM. **A pair
is marked `moverReach` when moving geometry could stand between the light
and the polygon**: some mover's reach meets the box that holds the light's
centre and the chart's polygon.

A mover's reach is the box of its shape placed as UTA-0119 § 4.5 places it,
at its own location and at each key its placement carries (`KeyPos`,
`KeyRot`, as offsets from its base). A placement with no keys is at its own
location only.

**A level fragment lighting a marked pair reads the shadow map, never the
mask.** `softShadowOf` is unchanged, and the shadow map holds the level and
every mover, so that pair is shadowed exactly as it is today. An unmarked
pair reads the mask alone. So one pair never has two shadows to combine.

### 4.5 The renderer — `urender`

- `ShadowMask.texels` uploads as one `VK_FORMAT_R8_UNORM` image, one level,
  into the bindless array; `FrameData` gains `shadowMaskTexture`, `NONE` when
  the bundle has no `SMSK`. It is used at every tier.
- Charts and pairs upload as storage buffers. A pair's `light` becomes the
  index of that light in the light buffer the frame uploads; a pair whose
  light the frame does not draw is left out.
- Vertex binding 1 gains `maskChart` (a flat `uint`) and `maskTexel` (a
  `vec2`) per vertex, beside UTA-0164's `occlusionUv`. A mover's vertices,
  and every vertex when there is no `SMSK`, carry `MASK_NO_CHART`.
- **In `scene.frag`, a fragment whose `maskChart` is a chart loops over that
  chart's pairs in place of its cluster's lights.** For each, the direct term
  adds `lightAt * flicker * seen`. `seen` is 1 for a `MASK_ALL_LIT` pair,
  `softShadowOf` for a `moverReach` pair, and otherwise the atlas read at
  `(corner + clamp(maskTexel, 0.5, size - 0.5)) / atlasSize`, level 0,
  filtered. The border texel keeps the read inside its own rectangle.
- A fragment with `MASK_NO_CHART` loops its cluster and reads the shadow map,
  as now. That is every mover and every bundle without `SMSK`.
- `shapeOf` in `Frame.cpp` samples `SMSK`: its sizes, and the ends of its
  vertex arrays, pairs and texels. Without it a bundle differing only in
  `SMSK` keeps the old mask (UTA-0164 § 7.1's lesson).
- The shadow planner, the shadow pass and the fog's reads are unchanged.

*As built:* the charts and pairs are bindings 14 and 15, so the shadow atlas,
the fog volume and the texture array move to 16, 17 and 18. A fragment lit
from its chart's pairs also adds the flashlight, which no pair names:
`FrameData` carries its index beside `shadowMaskTexture`. Without it a masked
surface would stay dark under the flashlight. Tested in
`tests/device/RenderShadowMaskTest.cpp`.

### 4.6 The baker's part — `ubake`

`bake` calls `bakeShadowMask` after `bakeOcclusion`. A level with no GEOM
vertex, or no `litDirectly` light, has no `SMSK`. `BAKER_REVISION` is bumped.

## 5. Invariants

- **INV-1** — `SMSK` round-trips § 4.2's fields, and `read` and `write` each
  refuse every case § 4.2 lists.
  *Test:* `tests/unit/BundleShadowMaskTest.cpp`, new.
  *Breaks when:* a check runs on one path only; the vertex count is checked
  against a mover's geometry instead of GEOM's.

- **INV-2** — in a box room with one point light and a pillar on the floor:
  a floor texel in the pillar's shadow stores 0, one in the open stores 255,
  and one across the shadow's edge stores between. A wall the pillar cannot
  shadow has a `MASK_ALL_LIT` pair. The pillar's face turned from the light
  has no pair for it.
  *Test:* `tests/unit/BakeShadowMaskTest.cpp`, new.
  *Breaks when:* the segment runs from the light's far side; an all-lit pair
  is stored with texels; the front-of-plane test is dropped.

- **INV-3** — a pane between the light and the floor shadows it only where
  it is solid: a `PF_Translucent`, `PF_Modulated`, `PF_NotSolid` or
  `PF_FakeBackdrop` pane casts nothing, and a `PF_Masked` pane whose texture
  is transparent on one half casts only the other half's shadow.
  *Test:* `tests/unit/BakeShadowMaskTest.cpp`.
  *Breaks when:* the bake uses `SurfaceRays`' current occluder set; alpha is
  not read at the hit.

- **INV-4** — a light the renderer does not draw directly, or one of
  brightness 0, or a spot effect with cone 0, has no pair.
  *Test:* `tests/unit/BakeShadowMaskTest.cpp`.
  *Breaks when:* the baker pairs lights by its own rule instead of
  `litDirectly`.

- **INV-5** — a pair is marked `moverReach` exactly when § 4.4's box test
  holds: a mover between the light and the floor marks the floor's pair, the
  same mover beyond the light marks none, and a mover placed beyond the light
  whose key carries it between marks it.
  *Test:* `tests/unit/BakeShadowMaskTest.cpp`.
  *Breaks when:* the keys are ignored; the box is the light's sphere alone.

- **INV-6** — no two placed rectangles overlap, each lies inside the atlas,
  and every lit vertex's `vertexTexel` lies inside its rectangle shrunk by
  one texel. Pairs whose total area passes the limit squared bake at twice
  `SHADOW_MASK_TEXEL_SIZE` or more, within the limit.
  *Test:* `tests/unit/BakeShadowMaskTest.cpp`.
  *Breaks when:* the border is dropped; packing passes the limit instead of
  coarsening.

- **INV-7** — `bakeShadowMask` gives the same section, byte for byte, at 1, 2
  and 4 workers.
  *Test:* `tests/unit/BakeShadowMaskTest.cpp`.
  *Breaks when:* a pair's order or a texel's value depends on scheduling.

The device cases draw a room with no occluder under a hand-written `SMSK`, so
the shadow map alone would light the floor fully, and only a renderer that
reads the mask can produce what each case asserts. They draw one bundle after
another in one renderer, which also exercises the fingerprint.

- **INV-8** — a level surface is shadowed by its pair's texels: with the
  floor's pair at 0 everywhere, the floor draws as with that light switched
  off; at 128, strictly brighter than that and strictly darker than with the
  pair `MASK_ALL_LIT`. "As with" is within 1 of 255.
  *Test:* `tests/device/RenderShadowMaskTest.cpp`, new.
  *Breaks when:* the mask is not bound; the fingerprint ignores `SMSK`; the
  value is inverted; a level fragment still reads the shadow map.

- **INV-9** — a `moverReach` pair reads the shadow map alone: the floor's
  pair at 0 everywhere and marked draws as without `SMSK`. A mover's own
  pixels draw the same with and without `SMSK`. Both within 1 of 255.
  *Test:* `tests/device/RenderShadowMaskTest.cpp`.
  *Breaks when:* a marked pair multiplies the mask into the shadow map; a
  mover's vertices carry a chart.

- **INV-10** — a level fragment lights from its chart's pairs alone: with two
  lights over the floor and an `SMSK` listing only the first, the floor draws
  as with the second switched off, within 1 of 255.
  *Test:* `tests/device/RenderShadowMaskTest.cpp`.
  *Breaks when:* the fragment still loops its cluster.

## 6. Failure modes

- **A mover whose path the placement does not describe** — a key the bake
  cannot read — is taken at its own location only. If it later moves between
  a light and an unmarked pair, the level shows no shadow of it from that
  light. No mover moves in the renderer yet.
- **A map too large at 8 units** coarsens by doubling, so its shadow edges
  soften. § 13's limit holds the memory.
- **A thin polygon** with no interior texel still has its border, so a read
  stays inside its rectangle.
- **A light moved after the bake** would keep its old mask. Nothing moves a
  light: `ubundle::Light` carries its placement alone (UTA-0014 § 4.8).
- **A cutout texture that animates** is read at pan time 0, so its shadow
  does not animate on level surfaces.

## 7. Tests

The device test carries the `device` label; the rest carry `unit`.

- INV-1 — `tests/unit/BundleShadowMaskTest.cpp`, new.
- INV-2, INV-3, INV-4, INV-5, INV-6, INV-7 —
  `tests/unit/BakeShadowMaskTest.cpp`, new.
- INV-8, INV-9, INV-10 — `tests/device/RenderShadowMaskTest.cpp`, new.
- `tests/unit/BakeGoldenTest.cpp`, re-recorded.

Each new test is seen failing against the code before its rule exists.

**Measured by hand after the code lands:**

1. Bake DM-Bishop, DM-Closer and DM-Crane. Record in UTA-0326's body the bake
   time added, the texel size, the atlas size, the pair count and the share of
   pairs that are all-lit, dropped and `moverReach`.
2. Time `ut-bench frame` on the three maps, at ultra, 3840x2160, with the mask
   and with it switched off in a scratch build, alternated in one `cc-job`,
   on UTA-0323's cameras. Record medians and 99th percentiles. The item ships
   only if every map is faster.
3. `ut-shot` UTA-0323's player-start views with and without the mask, and
   compare the images numerically: a shadow present in one and absent in the
   other is a defect.

## 8. Alternatives considered (and rejected)

- **Four lights a texel, by channel colouring (Unity's shadowmask).** Lost:
  § 2 consequence 3 — most lights get no channel on every measured map.
- **Baking the whole direct light into a light map.** The larger saving —
  Bishop's 12 ms floor with no direct light — but it loses per-pixel light
  on parallax and normal detail, and each light's own flicker and pulse
  (UTA-0014 § 4.9). Not chosen; not queued.
- **The mask times the shadow map for a `moverReach` pair.** Lost: where both
  hold the same partial edge the product darkens it twice, the very fault
  the item names.
- **The shadow map holding movers only, combined by the smaller value.** It
  would let a `moverReach` pair use the mask, but needs a second tile set.
  Deferred until characters cast shadows (§ 9).
- **The cluster loop, finding each light's pair in the chart's list.** Lost:
  a search per light per pixel, and the cluster still serves the lights the
  bake found blocked.
- **Reusing `AOCC`'s charts and atlas.** Lost: the two texel sizes coarsen
  separately, and one atlas would tie them.

## 9. Out of scope

- Characters and other actors casting shadows from baked lights — deferred;
  not yet queued. UTA-0159 and UTA-0160 are near it.
- Soft shadows that harden near contact — tracked by UTA-0296. A baked mask
  makes them a bake-time cost.
- Dropping shadow tiles that nothing reads any longer — deferred; not yet
  queued.
- Block compression of the atlas — deferred, as UTA-0164 § 8 deferred it.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleShadowMaskTest.cpp` |
| INV-2, INV-3, INV-4, INV-5, INV-6, INV-7 | `tests/unit/BakeShadowMaskTest.cpp` |
| INV-8, INV-9, INV-10 | `tests/device/RenderShadowMaskTest.cpp` |
| The frame is faster on the slow maps | **nothing** — § 7's step 2, run by hand |
| Real maps show the same shadows | **nothing** — § 7's step 3, run by hand |
| Bake time and atlas size | **nothing** — § 7's step 1, run by hand |

## 11. Cross-doc impact

- `docs/specs/UTA-0014-vulkan-draw-path.md` § 4.6 and § 4.8 — a level surface
  lights from its chart's list and reads the mask; pointers now, an amendment
  recording what was built when it lands.
- `docs/specs/UTA-0008-bundle-container-and-origin.md` — the section order.
- `CLAUDE.md` § Standing facts — the bundle format and baker revision.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0326-baked-shadow-mask-loop-log.md`.

## 13. Resource cost

- The atlas is at most `SHADOW_MASK_ATLAS_LIMIT` squared bytes, 16 MiB. The
  probe of § 2 put the chart-and-light area, before any pair is dropped or
  found all-lit, at 8.45, 2.19 and 13.25 million texels at 16 units on
  Bishop, Closer and Crane, and four times that at 8.
- A pair is 12 bytes and a chart 12; Bishop's 3785 lit polygons with at most
  67 pairs each bound its pairs near 3 MB.
- Each vertex gains 12 bytes on the GPU.

## 14. Migration / compatibility

`FORMAT_VERSION` becomes 23. `ubundle::read` refuses any other version, so
every map baked before this item must be baked again.

## 15. Open questions

- **A big polygon's list may hold lights a given fragment's cluster would
  not.** If § 7's timing shows the loop is longer than the cluster's on
  large floors, split a chart's list by region of its rectangle.
