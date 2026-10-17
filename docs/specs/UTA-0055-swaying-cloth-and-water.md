<!-- ants-spec-format: 1 -->
# UTA-0055 — Sway hanging cloth and undulate water

**Status:** spec draft (2026-10-16), unreviewed. Contract reviews are
cancelled for this project (`CLAUDE.md` § Contract reviews); building it is
the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0055 (user-request-2026-09-04; water moved into
0.1.0 by the user, 2026-09-26).
**Pairs with:** UTA-0105 (liquid surface motion), UTA-0215 (underwater).

**Layman:** banners, flags, sails and curtains sway, and water and lava
surfaces rise and fall in slow waves, instead of standing still.

## 1. Goal

Free-hanging cloth drawn on map surfaces sways, pinned at its top. Water
and other liquid surfaces facing up or down undulate, held still where
they meet the shore. The motion is in the vertex stage, on surfaces the
bake has cut into a fine grid. Shadows, collision and every baked light
stay on the flat surface.

## 2. Problem

1. Every drawn surface is rigid. `ubake::buildGeometry` writes each BSP
   polygon as a fan from its first vertex, and `scene.vert` moves a vertex
   only by its object's matrix. A banner that is one rectangle has four
   corners, so it could not bend even if the shader moved them.
2. Nothing says which surfaces may move. `ubundle::MaterialRecord` carries
   a liquid look (`liquid`), but no tag for cloth, and no surface rule picks
   a liquid's top from its walls.
3. The original game's water is flat geometry too; its motion is the
   texture's. UTA-0105 already animates that texture. The user asked for
   "the usual undulating surface" on top (ROADMAP UTA-0055, 2026-09-26).

## 3. Scope decisions (agreed with the user)

- Water is in 0.1.0 (user, 2026-09-26). Banners and flags ride with it.
- Taken by this session, each recorded in the roadmap body:
  - Flags drawn as decorations (meshes) are out: nothing draws a mesh yet
    (UTA-0341).
  - Hanging moss, cobwebs, vines and seaweed also sway in a real map, and
    the census found them often. They belong to vegetation, UTA-0293.
  - A cloth picture moves only on a surface flagged two-sided or not
    solid. The same tapestry flat on a wall stays still, as it would.
  - Lava moves as water does: UT99's WetTexture class carries both, and a
    slow swell suits both.

## 4. Design

### 4.1 Which surfaces move

The bake decides, per drawn polygon, one **sway kind**:

```
enum class SwayKind : std::uint8_t { None = 0, Cloth = 1, Water = 2 };
```

- **Cloth** — the material's picture is on the cloth list (§ 4.6), and the
  surface's PolyFlags carry `PF_TWO_SIDED` (0x100) or `PF_NOT_SOLID`
  (0x08).
- **Water** — the material carries a liquid look of kind `Wet` or `Wave`,
  and the polygon's normal has `|z| >= 0.7`. Ice is excluded: it is glass.
- **None** — everything else. Movers (MOVR) are None in this item; their
  polygons are drawn by their own matrix and are few.

`PF_TWO_SIDED` joins the flags in `src/ubake/Geometry.h`, cited to the
same header.

### 4.2 Patches and slicing

Moving polygons with the same kind and material that share an edge form a
**patch**. Each polygon of a patch is sliced by the world planes
`x = k·S`, `y = k·S` and `z = k·S`, for every integer `k` between its
extremes; `S` is 32 units for cloth and for water. Each piece is convex,
and is fanned as `buildGeometry` fans a polygon today.

**Why world planes.** Two polygons sharing an edge are cut where the same
planes cross that edge, so both carry the same split points and no crack
opens. A crossing is computed from the edge's two ends taken in a fixed
order (lexicographic on position), so both polygons compute the same
bits. A plane within 1/64 unit of a whole polygon cuts nothing.

UVs and the zone are carried to each new vertex by the plane's linear
interpolation. A swaying vertex's normal is the mean of the normals of
every moving polygon holding that point, so a fold's two sides move along
one direction and no seam opens. Where those normals cancel, it is the
polygon's own. AOCC and SMSK are charted after slicing, so new vertices
get charts as any other.

**Budget.** At most 524288 sliced vertices a map. Over it, the kind
holding more of them has its `S` doubled first, cloth up to 64 units and
water up to 256; then the other kind. If neither can coarsen further,
water stays unsliced and still, then cloth. The bake report lists the
spacings and what was left still.

### 4.3 How far each vertex may move

Each vertex carries a **weight**, a byte, 0 (pinned) to 255 (free).

- **Hanging cloth** (patch normal `|z| < 0.7`): pinned at the patch's
  highest point. `weight = min(1, (top − z) / 256)`, where `top` is the
  highest `z` over the patch's vertices. A banner moves more the further
  it hangs.
- **Water, and flat cloth** (an awning): pinned at the patch's outline.
  `weight = min(1, d / 64)`, where `d` is the distance in the patch's
  plane to its nearest boundary edge: an edge of a patch polygon no other
  patch polygon shares.

### 4.4 The bundle

`GeometryVertex` gains three bytes after `zone`:

```
SwayKind sway = None;         ///< 0 None, 1 Cloth, 2 Water
std::uint8_t swayWeight = 0;  ///< 0 pinned .. 255 free; 0 where sway is None
std::uint8_t swayCell = 0;    ///< S / 16: 1, 2, 4, 8 or 16; 0 where sway is None
```

On the wire the three follow the zone byte, so a vertex is 36 bytes
(`MIN_GEOMETRY_VERTEX`). A `sway` byte past 2 is refused, as MATS refuses
a liquid byte past 3. So is a None vertex with a weight or a cell, and a
swaying vertex whose cell is not a power of two up to 16.
`FORMAT_VERSION` becomes 29 and `BAKER_REVISION` 57.

### 4.5 The renderer

One attribute (`R8G8B8A8_UINT`, from the zone byte) carries the zone,
`sway`, `swayWeight` and `swayCell`. `Frame` gains
`previousFlameSeconds`, the light clock one frame earlier.

`sway.glsl` gives `Sway swayAt(vec3 p, vec3 n, uint kind, float weight,
float cell, float seconds)`, returning the offset and the moved normal:

- **Cloth** — three summed sines of world position along the normal,
  wavelengths 224, 144 and 96 units, amplitude `CLOTH_AMPLITUDE · w`
  (6 units), with the normal from their analytic derivative.
- **Water** — four Gerstner waves of steepness at most 0.3, wavelengths
  600, 340, 220 and 150 units, amplitude `WATER_AMPLITUDE · w` (3 units),
  along the plane's normal and within it.

A wave shorter than three cells of its surface's spacing fades out by
two cells, so a coarsened surface does not alias.

Every wave's angular rate is a whole number of cycles per
`FLAME_CLOCK_WRAP` (4096 s), so the clock's wrap is seamless.

`scene.vert` adds `swayOffset(..., frame.flameSeconds)` before the model
matrix, and its `previousClip` uses `frame.previousFlameSeconds`, so
motion vectors carry the sway. The depth pass runs the same stage, so its
`invariant gl_Position` still holds. `shadow.vert` is untouched: shadows
fall from the flat surface.

The amplitudes, wavelengths and rates are chosen, not fitted: the
original has no moving geometry to fit against. Starting values:
`CLOTH_AMPLITUDE` 6 units, `WATER_AMPLITUDE` 2 units.

### 4.6 The cloth list

`src/umat/CuratedCloth.cpp`, keyed by picture fingerprint as
`CuratedFlames.cpp` is, with `isCloth(fingerprint)`. Every entry is a
`cloth` line of `tests/real/cloth-labels.txt`, labelled by hand from the
census of 2026-10-16 (§ 13). An edit bumps `BAKER_REVISION`, and a unit
test pins the table's digest to say so.

## 5. Invariants

- **INV-1** — A polygon gets a sway kind only by § 4.1's rules.
  *Test:* `tests/unit/BakeGeometryTest.cpp`, one case per rule and per
  exclusion: a cloth picture on a solid one-sided wall, a Wet look on a
  wall, an Ice look on a floor.
  *Breaks when:* a rule drops its flag or normal test, or reads the
  liquid look without its kind.

- **INV-2** — Slicing leaves no crack: within a patch, every triangle edge
  is either on the patch's outline or shared by exactly two triangles,
  with bitwise equal end positions; and corners at one position carry
  one normal.
  *Test:* `tests/unit/BakeSwayTest.cpp`, two polygons sharing an edge,
  given in opposite winding and in a tilted plane;
  `tests/unit/BakeGeometryTest.cpp`, two cloth walls meeting at a fold.
  *Breaks when:* a crossing is computed from the edge's ends in the
  polygon's own order, or a vertex takes its polygon's normal.

- **INV-3** — A polygon of kind None is written exactly as today.
  *Test:* `tests/unit/BakeGoldenTest.cpp`, its vertices and indices
  unchanged apart from the three new zero bytes.
  *Breaks when:* slicing or weighting runs on a None polygon.

- **INV-4** — Weight is 0 at the pin: on a hanging cloth patch at every
  vertex at its top, and on water at every vertex on its outline; and it
  is 255 at a hanging vertex 256 or more units below the top.
  *Test:* `tests/unit/BakeSwayTest.cpp`.
  *Breaks when:* the top or the outline is taken per polygon rather than
  per patch.

- **INV-5** — GEOM's codec writes and reads all three bytes, and refuses
  a sway byte past 2, a None vertex with a weight or cell, and a swaying
  cell that is not a power of two up to 16.
  *Test:* `tests/unit/BundleGeometryTest.cpp`, round trip and refusals.
  *Breaks when:* the reader skips the bytes or accepts 3.

- **INV-6** — The budget holds: no map writes more than 524288 sliced
  vertices, and a coarsened or dropped map says so in its bake report.
  *Test:* `tests/unit/BakeSwayTest.cpp`, a water plane too big for 32
  units.
  *Breaks when:* the cap is checked after writing.

- **INV-7** — A tagged surface moves and an untagged one does not; a
  weight-0 surface draws the same at two clock times.
  *Test:* `tests/device/RenderSwayTest.cpp`, a quad at two pinned light
  times, for each kind and for None.
  *Breaks when:* the shader ignores the kind or the weight.

- **INV-8** — The clock's wrap is seamless: a swaying quad draws the same
  at 0 s and at 4096 s.
  *Test:* `tests/device/RenderSwayTest.cpp`.
  *Breaks when:* a wave's rate is not a whole number of cycles per wrap.

- **INV-9** — Collision, AOCC and SMSK are built from flat geometry or
  per-vertex charts only; the collision section of a sway-bearing map is
  the same bytes with and without sway.
  *Test:* none. `buildCollision` reads the Model only, never GEOM, so no
  test can feed it sliced vertices.
  *Breaks when:* collision reads GEOM's sliced vertices.

- **INV-10** — The cloth list is the labels file's `cloth` lines over the
  reference install, and its digest is pinned.
  *Test:* `tests/real/RealClothTest.cpp` (real-asset tier) and
  `tests/unit/ClothLibraryTest.cpp`.
  *Breaks when:* one is edited without the other, or without a baker bump.

## 6. Failure modes

- **A cloth patch is not planar** (a sail bent over two polygons). Slicing
  needs no plane; the hang weight reads only `z`. It still works.
- **A water patch has no outline** (a closed box of water polygons). Every
  vertex reaches weight 1. Allowed: no wall meets it.
- **Self-shadowing on moving surfaces.** A dynamic shadow map holds the
  flat surface, so a crest may shade itself. At 2 units of water and 6 of
  cloth, the shadow bias may hide it; § 15 asks.
- **A bundle at format 28** is refused by the reader's version check, as
  every older bake is.

## 7. Tests

Each invariant's test is named in § 5. Every new test is seen to fail
before its code lands, as `write-code` asks. The device tests run on Mesa's
CPU driver under the validation layer, label `device`.

## 8. Alternatives considered (and rejected)

- **Tessellation shaders.** Rejected by the research note
  (`docs/research-2026-10-08-v0.1.0-techniques.md` § UTA-0055): a second
  pipeline stage on every surface for a few.
- **Midpoint subdivision of each fan triangle.** Two polygons sharing an
  edge split it a different number of times, which leaves T-junctions.
- **A per-material tag in MATS.** Cloth needs the surface's flags too, and
  water the polygon's normal. A per-vertex byte carries both decisions
  and the weight in bytes the vertex already pays for.
- **Displacing the shadow pass too.** Deferred to § 15: flat shadows are
  what the research note asks, and the cost is unknown.

## 9. Out of scope

- Mesh flags and banners — tracked by UTA-0341 (mesh decorations).
- Moss, cobwebs, vines, seaweed — tracked by UTA-0293 (vegetation).
- Water drawn with a plain texture rather than a liquid class. Deferred;
  not yet queued.
- Movers' surfaces. Deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 to INV-6 | `tests/unit/BakeSwayTest.cpp`, `BakeGeometryTest.cpp`, `BakeGoldenTest.cpp`, `BundleGeometryTest.cpp` |
| INV-9 | **nothing** — `buildCollision` reads the Model only |
| INV-7, INV-8 | `tests/device/RenderSwayTest.cpp` |
| INV-10 | `tests/real/RealClothTest.cpp`, `tests/unit/ClothLibraryTest.cpp` |
| The amplitudes look right | **nothing** — chosen, not fitted (§ 4.5) |

## 11. Cross-doc impact

- `CLAUDE.md` § Standing facts: the format and baker revision numbers.
- `docs/specs/UTA-0109-map-geometry.md`: GEOM's vertex layout gains three bytes.
- `CHANGELOG.md`: one entry.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0055-swaying-cloth-and-water-loop-log.md`.

## 13. Resource cost

Sliced vertices are capped per map (§ 4.2). The vertex stage adds a few
sines per vertex on moving surfaces only; others branch past it.
Frame time was measured with `ut-bench frame` on DM-KGalleon, which
carries sails, a curtain and water (2026-10-17, RX 6600, tier high,
1920x1080, six views, each build run twice in one job). The scene pass
rose 0.2 to 0.7 ms a view; the most was over the pond, 6.15 to 6.87 ms.
The bake held 477117 sliced vertices, 59415 of them cloth.

The census (2026-10-16, a throwaway program linking the build's
libraries, kept in `~/.cache/uta-scratch/u055/`) read 1610 of the
reference install's 1611 maps and labelled 227 name-matched textures by
hand: 35 cloth.

## 14. Migration / compatibility

Format 29 refuses format 28 bundles; every map is re-baked, as at every
format bump.

## 15. Open questions

None. Flat shadowing shows no acne on DM-KGalleon's sails (ut-shot,
2026-10-17), so shadows stay flat.
