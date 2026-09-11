# UTA-0109 — `ubake`: turn the level's BSP tables into triangles, and write the geometry section

**Status:** accepted (2026-09-11).
**Kind:** implement.
**Source:** ROADMAP UTA-0109 (user-request-2026-09-10, split out of
UTA-0011).

**Blocked by:** UTA-0011, shipped.
**Blocker for:** UTA-0014, which draws what this writes.
**Pairs with:** UTA-0104 (the project's own surface kinds, added beside the
raw flags later), UTA-0111 (collision, which reads the `Model` itself),
UTA-0112 (baked light, which will need coordinates of its own).

**Layman:** Rebuild each level's walls, floors and ceilings as modern 3D
geometry the renderer can draw.

## 1. Goal

A bake writes the level's drawable surfaces as triangles, in a new `GEOM`
section. Each triangle carries its position, its surface's normal and its
texture coordinates. Each run of triangles names the material it wears and
the surface flags UT99 gave it. The renderer draws a level from this section
and `MATS`/`TEXS` alone, without reading a UT file.

## 2. Problem

1. **Nothing turns the level's `Model` into triangles.** `upkg::readModel`
   returns the file's own tables and does no geometry work, by the user's
   choice in UTA-0004 § 3.1, which puts that work in `ubake`.
   `ubake::detail::bake` writes `ROOM`, `NAVG`, `WIRG`, `TEXS` and `MATS`,
   and its header's SCOPE comment names geometry as this item's.
2. **The bundle has nowhere to put geometry.** `ubundle::Bundle` has no
   member for it, and UTA-0014 owes *"the first draw of a bundle's geometry
   with its PBR materials"*.
3. **Nothing binds a surface to its material.** UTA-0009 § 9 leaves
   *"picking each surface's variant"* to UTA-0011, which picks it per
   texture, and UTA-0011 § 9 leaves *"binding each surface to its material"*
   to this item.
4. **The file's polygons are not ready to draw as they stand.** Some drawable
   nodes wind against their surface's normal, and some have no area. The
   progress note on ROADMAP UTA-0109 records the figures measured over the
   reference install, and § 7's real-asset case prints them again.

## 3. Scope decisions (agreed with the user)

1. **Geometry is its own item.** *User, 2026-09-10*, splitting UTA-0011.
2. **Each surface keeps UT99's raw PolyFlags.** *User, 2026-09-11.*
   UTA-0104 adds the project's own surface kinds beside them later, which
   changes the bundle format again.
3. **`GEOM` is a section of its own, appended after `MATS`.** Mine. It is
   the pattern UTA-0052 set with `TEXS` and UTA-0011 followed with `MATS`.
4. **Invisible surfaces are dropped, and every other surface is kept with
   its flags.** Mine. UT99 never draws a surface with `PF_Invisible`. What
   a portal, a sky surface or a two-sided surface looks like is the
   renderer's call (UTA-0014), so the flags go through untouched.
5. **Texture coordinates are normalised**: `1.0` is one repeat of the
   texture. Mine. `umat` enlarges a material's maps, and a normalised
   coordinate does not change when it does.
6. **Positions stay in UT99's own coordinates and units.** Mine. A
   conversion is a renderer decision, and a bake that converted would bind
   every later section to that choice.

## 4. Design

### 4.1 The files

| File | Holds |
|---|---|
| `src/ubundle/Bundle.h` | the `GEOM` types, `Bundle::geometry` and `FORMAT_VERSION` (§ 4.2) |
| `src/ubundle/Sections.h`, `GeometrySection.cpp` | the `GEOM` codec and its validation (§ 4.2) |
| `src/ubake/Geometry.h/.cpp` | `buildGeometry` (§ 4.3) |
| `src/ubake/Bake.h/.cpp` | the material lookup and the bake's new step (§ 4.4) |
| `src/ubake/Name.h` | `BAKER_REVISION` (§ 4.4) |

`GeometrySection.cpp` joins `uta_ubundle` and `Geometry.cpp` joins
`uta_ubake`. No target is added and no link changes. `ubundle` still links
nothing but `uta_core`, `uta_umap` and `uta_unav` (UTA-0008 INV-10).

### 4.2 The `GEOM` section

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 4;  // 4 since UTA-0109

/// One corner of a triangle.
struct GeometryVertex {
    std::array<float, 3> position{};  ///< UT99's own coordinates and units
    std::array<float, 3> normal{};    ///< its surface's normal, as the file stores it
    float u = 0;                      ///< 1.0 is one repeat of the texture
    float v = 0;
};

/// A run of triangles wearing one material under one set of flags.
struct GeometryBatch {
    std::string material;          ///< a MATS id, or empty for none
    std::uint32_t polyFlags = 0;   ///< UT99's PolyFlags, verbatim
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;  ///< three per triangle
};

struct Geometry {
    std::vector<GeometryVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<GeometryBatch> batches;
};

struct Bundle {
    // ... the existing members, then:
    std::optional<Geometry> geometry;
};

}  // namespace uta::ubundle
```

The section id is the bytes `G`, `E`, `O`, `M`. Its payload is three
vectors in UTA-0008 § 4.2's encoding, in this order: `vertices`, `indices`
as `u32`, `batches`.

| Element | Encoding |
|---|---|
| `GeometryVertex` | six `f32` — `position` x, y, z, then `normal` x, y, z — then `u` and `v` as `f32` |
| index | `u32` |
| `GeometryBatch` | `material` as `string`, then `polyFlags`, `firstIndex`, `indexCount` as `u32` |

**Minimum encoded sizes**, joining UTA-0008 § 4.2's table: `GeometryVertex`
32 bytes, fixed; `GeometryBatch` 16 bytes, a `u32` length for an empty
`material` then three `u32`.

**`GEOM`'s floats are not validated.** They are moved through their bits,
as UTA-0008 § 4.2 moves every `f32`.

**Validation, in UTA-0008 § 4.9's manner.** `read` refuses with
`MalformedData`, and `write` refuses the same with `InvalidArgument`:

- an index not less than `vertices.size()`;
- batches not in strictly ascending order of `material` bytewise, then
  `polyFlags` — which also makes each key unique;
- a batch whose `indexCount` is zero or not a multiple of three;
- batches that do not tile `indices`: the first starts at `0`, each next
  starts where the last ended, and the last ends at `indices.size()`. With no
  batches, `indices` is empty. Computed so that `firstIndex + indexCount`
  cannot overflow.

A vertex no index names is not refused. `ubundle` does not check that a
`material` is a `MATS` id: a section never reads another's meaning, and the
baker guarantees it (INV-10).

**`write` emits `GEOM` last**: `ROOM`, `NAVG`, `WIRG`, `TEXS`, `MATS`,
`GEOM`. Appended, so UTA-0008 § 4.10's order clause is extended rather than
contradicted. **`FORMAT_VERSION` becomes `4`**; nothing else about the
framing moves.

### 4.3 Building the triangles

```cpp
namespace uta::ubake {

/// What a surface wears: a made material's id, and the texels one repeat of
/// its texture spans on each axis.
struct SurfaceMaterial {
    std::string id;
    double uSize = 0;
    double vSize = 0;
};

/// The material a surface naming `texture` wears, masked or not, or nullptr
/// when none was made.
using MaterialLookup =
    std::function<const SurfaceMaterial*(upkg::ObjectReference texture, bool masked)>;

/// The level's drawable surfaces as triangles. MalformedData when a drawn
/// node's index leaves its table.
[[nodiscard]] Result<ubundle::Geometry> buildGeometry(const upkg::Model& model,
                                                      const MaterialLookup& materials);

}  // namespace uta::ubake
```

The PolyFlags bits named here are UT99's `EPolyFlags`: `PF_Invisible`
`0x00000001`, `PF_Masked` `0x00000002`, `PF_FakeBackdrop` `0x00000080`,
`PF_TwoSided` `0x00000100`, `PF_Portal` `0x04000000`.
Source: <https://github.com/stephank/surreal/blob/master/Engine/Inc/UnObj.h>,
UT 4.32's public headers.

**For each node, in ascending index:**

1. **Fewer than three vertices** (`numVertices`) — the node draws nothing.
   Skip it.
2. **Its surface.** `iSurf` must index `surfs`.
3. **`PF_Invisible`** on the surface's `polyFlags` — skip the node.
4. **Its polygon.** `iVertPool` to `iVertPool + numVertices` must lie within
   `verts`, and each entry's `pVertex` must index `points`. The polygon is
   those points, `P0` to `Pn-1`, in the pool's order. The surface's `pBase`
   must index `points`, and its `vNormal`, `vTextureU` and `vTextureV` must
   index `vectors`. Call them `Base`, `N`, `TU` and `TV`.
5. **Its orientation.** `s` is the sum over `k` from 1 to `n-2` of
   `((Pk - P0) × (Pk+1 - P0)) · N`, in `double`. Where `s` is zero, skip the
   node: it has no area. Where `s` is negative, reverse the polygon to `P0`,
   `Pn-1`, …, `P1`.
6. **Its material.** `materials(surf.texture, masked)`, where `masked` is
   whether the surface carries `PF_Masked`. A null surface texture asks
   nothing and wears none.
7. **Its vertices**, one per point, in the polygon's order:
   `position` is the point, `normal` is `N`, and

   ```text
   u = ((P - Base) · TU + surf.panU) / uSize
   v = ((P - Base) · TV + surf.panV) / vSize
   ```

   in `double`, then rounded to `float`. A surface wearing no material has
   `u = v = 0`.
8. **Its triangles**, a fan from the first vertex: `(0, k, k+1)` for `k`
   from 1 to `n-2`, offset by where its vertices begin.

Steps 2 and 4 refuse the bake with `MalformedData`, naming the node and the
index. A node skipped at step 1 or 3 is checked no further, so a surface UT99
never draws cannot refuse a bake.

**Batches.** A drawn node belongs to the batch keyed by its material id
(empty for none) and its surface's `polyFlags`. Batches are emitted in
ascending key order — id bytewise, then flags. Within a batch, nodes keep
node order. Vertices and indices are appended in the order their nodes are
emitted, so each batch's vertices are contiguous too.

**Why the pan is added.** The UT 4.32 OpenGL driver computes
`(MapCoords.XAxis · (P - MapCoords.Origin) - Info.Pan.X) * UMult`, where
`UMult = 1 / (Info.UScale * Info.USize)` (`OpenGLDrv/Src/OpenGL.cpp` in the
source cited above). The code that fills `Info.Pan` from a surface's `PanU`
is not public. On the reference install, where two surfaces share an edge,
a texture and equal `TU`, `TV` and `N` but different pans, the pans line up
far more often with `+` than with `-`, on both axes. ROADMAP UTA-0109's
progress note holds the counts, and § 7's real-asset case prints them.

**Arithmetic is the same on every compiler.** `CMakeLists.txt` turns off
floating-point contraction and fast-math on all three (UTA-0049), which is
what keeps the rounded `float`s, and so the bake's bytes, identical.

**A level whose emitted vertices or indices would reach 2^32 is refused**
with `MalformedData`, since a `u32` could not count them. Indices get there
no later: a node of `n` vertices emits `3(n - 2)` of them.

### 4.4 The baker's part

`detail::bake` gains one step, after the materials:

> **`GEOM`** is `buildGeometry` over the level's `Model`, with a lookup over
> the variants the materials step made. Its refusal refuses the bake, naming
> the map.

**The lookup answers** `(texture, masked)` with the variant the materials
step made for that texture reference, or nullptr where it made none — a
texture that did not resolve, or was skipped (UTA-0011 § 4.6). A variant
exists exactly where a `MATS` record does.

**`uSize` and `vSize`** are the width and height of the texture's base level
(`mips[0]`), each times the texture's scale. **The scale is the texture's
`DrawScale` property**, a `float`, and `1` where it carries none or carries
one that is not finite and positive. A package names a property by its
script declaration, and `DrawScale` is the script's name for the member
UT 4.32's `Engine/Inc/UnTex.h` calls `UTexture::Scale`, *"Scaling relative
to parent"*: the script declares `Diffuse`, `Specular`, `Alpha`,
`DrawScale`, `Friction`, `MipMult` where the header declares `Diffuse`,
`Specular`, `Alpha`, `Scale`, `Friction`, `MipMult`, and defaults it to `1`.
Source: <https://github.com/Slipyx/UT99/blob/master/Engine/Texture.uc>, UT's
469b scripts. That the driver's `Info.UScale` holds it is not in the public
source; § 15 keeps that open.

`umat::resolve` refuses a base level of zero width or height, so a made
variant's sizes are never zero.

**`GEOM` is written, and empty where no node is drawn**, as UTA-0011 § 4.5
requires of every section it examines. The budget step is unchanged: it
measures textures only.

**`BAKER_REVISION` becomes `2`**, and UTA-0011 INV-5's golden value is
recorded again under it.

### 4.5 As built (2026-09-11)

Recorded after the build; nothing above changed direction.

- **Each invariant's test was seen failing by mutating the code it locks,
  after that code existed.** § 7's list and five more mutations; the commit
  that shipped this item carries what killed each.
- **INV-11 was added in the build.** Nothing tested § 4.4's scale read, so a
  mutation ignoring `DrawScale` passed every listed test.
- **Additions beyond this section's API, none of which a § 4 caller binds
  to.** `Bake.h` gains `detail::textureScale` and `detail::resolveTexture`,
  which the real-asset case calls to resolve textures as the bake does
  without generating materials. `Geometry.h` carries `PF_INVISIBLE` and
  `PF_MASKED`; `Bake.cpp`'s own `PF_MASKED` is gone.
- **Two references resolving to one texture share its variants**: each
  variant keeps every reference naming it, so both surfaces find it.
- **The fixture's squares hang off no other node**, so the room builder never
  reaches them. UTA-0011 INV-13's decoy gains as many undrawn nodes, to stay
  the larger `Model` in every table.

## 5. Invariants

- **INV-1** — `GEOM` round-trips through `ubundle::write` and
  `ubundle::read`, float bits included, and `write` emits it after `MATS`.
  *Test:* `tests/unit/BundleGeometryTest.cpp`, a geometry holding a `-0.0`
  and a signalling NaN — quiet bit clear, non-zero payload, made with
  `std::bit_cast` — in a bundle that also carries `MATS`. A quiet NaN keeps
  its bits through a `double`, so it cannot show that breakage.
  *Breaks when:* a float passes through a wider type or a comparison, or
  `GEOM` is emitted ahead of another section.

- **INV-2** — `read` refuses with `MalformedData`, and `write` with
  `InvalidArgument`, each § 4.2 rule: an index equal to `vertices.size()`;
  two batches of one material whose flags descend; two batches of one key;
  a batch of `indexCount` zero; one of `indexCount` four; a first batch not
  starting at `0`; a gap between two batches; indices past the last batch;
  indices with no batches; and batches `{0, 3}`, `{3, 4294967295}` and
  `{2, 3}` over five indices, which tile only when summed in 32 bits.
  *Test:* `tests/unit/BundleGeometryTest.cpp`, one case per rule. Each
  fixture breaks that rule alone — the count-of-four case tiles its four
  indices exactly, and `4294967295` is a multiple of three — so no other
  rule can refuse it first.
  *Breaks when:* a rule is checked on one path only, the order is checked
  with `<=`, or the tiling test computes `firstIndex + indexCount` in 32
  bits.

- **INV-3** — A convex quad whose pool order winds along `N` gives four
  vertices in pool order and the indices `0, 1, 2, 0, 2, 3`.
  *Test:* `tests/unit/BakeGeometryTest.cpp`.
  *Breaks when:* the fan starts at another vertex, or a correctly wound node
  is reversed.

- **INV-4** — Every polygon `buildGeometry` emits has a positive `s` along
  its surface's `N`. A node wound the other way is emitted as `P0`, `Pn-1`,
  …, `P1`. A node whose `s` is zero emits nothing.
  *Test:* `tests/unit/BakeGeometryTest.cpp`: a triangle given against its
  normal, whose output must be `P0, P2, P1`; and three collinear points,
  which must emit nothing while a fourth non-collinear node beside it does.
  *Breaks when:* the reversal drops or moves `P0`, the orientation is taken
  from the node's plane rather than the surface's `vNormal`, or a zero sum
  is emitted.

- **INV-5** — A node of fewer than three vertices, or whose surface has
  `PF_Invisible`, emits nothing. A node whose surface carries `PF_Portal`,
  `PF_FakeBackdrop` or `PF_TwoSided` is emitted, its batch keyed by the
  surface's `polyFlags` unchanged.
  *Test:* `tests/unit/BakeGeometryTest.cpp`, one surface per flag, and a
  fifth carrying both `PF_Portal` and `PF_Invisible`, which must emit
  nothing.
  *Breaks when:* a flag is masked or translated, or an invisible surface is
  drawn.

- **INV-6** — `u` and `v` are § 4.3 step 7's expressions, rounded to
  `float`.
  *Test:* `tests/unit/BakeGeometryTest.cpp`: a surface with `Base` off the
  origin, a `TU` of length other than one, `panU` 16 and `panV` -8, and a
  lookup giving `uSize` 128 and `vSize` 64. The expected floats are the
  expressions written out in the test. A second case differs only in `uSize`.
  *Breaks when:* the pan is subtracted, `Base` is ignored, or the size is
  not applied.

- **INV-7** — A surface with `PF_Masked` asks the lookup for the masked
  variant, and one without asks for the opaque. A surface the lookup answers
  nullptr for, and a surface with a null texture, has an empty material and
  `u = v = 0`.
  *Test:* `tests/unit/BakeGeometryTest.cpp`, with a stub lookup that records
  what it was asked and answers only the masked variant.
  *Breaks when:* the variant follows anything but the surface's own flag.

- **INV-8** — Batches are in ascending `(material, polyFlags)` order with
  each key once; within a batch, nodes keep node order.
  *Test:* `tests/unit/BakeGeometryTest.cpp`: nodes whose materials descend
  in node order; two nodes of one material with different flags; and two
  nodes of one key, which must keep node order inside their batch.
  *Breaks when:* batches follow node order, or an unordered container
  decides it.

- **INV-9** — A node of three or more vertices whose `iSurf` leaves
  `surfs`, and a drawn node whose vertex pool, `pVertex`, `pBase`,
  `vNormal`, `vTextureU` or `vTextureV` leaves its table, are refused with
  `MalformedData` naming the node. A node skipped at step 1 is not checked
  at all, and one skipped at step 3 is checked for `iSurf` only.
  *Test:* `tests/unit/BakeGeometryTest.cpp`, one case per index, each index
  one past its table's end; a node of two vertices whose `iSurf` and vertex
  pool are both out of range; and a `PF_Invisible` node whose vertex pool is
  out of range. The last two must succeed.
  *Breaks when:* an index is used unchecked, or a skipped node is held to
  the checks.

- **INV-10** — Every non-empty `GEOM` material a bake writes is a `MATS`
  id, and a surface whose texture was skipped wears none.
  *Test:* `tests/unit/BakeTest.cpp`, over the standard fixture with one
  texture carrying a `Format` property and a polygon on each surface.
  *Breaks when:* the lookup answers with the id of a variant that was not
  made.

- **INV-11** — A bake's `uSize` and `vSize` are the base level's width and
  height times the texture's `DrawScale`, and a `DrawScale` that is not a
  finite positive number counts as `1`. *Added in the build (§ 4.5).*
  *Test:* `tests/unit/BakeTest.cpp`: one floor texture four texels wide on a
  64-unit square, baked with no `DrawScale`, with `2`, with `-1` and with a
  NaN. Its far corner's `u` is `16`, `8`, `16` and `16`.
  *Breaks when:* the property is not read, is read under the C++ member's
  name `Scale`, or a value that is not finite and positive is used as given.

## 6. Failure modes

| When | What happens |
|---|---|
| A drawn node's index leaves its table | The bake is refused, naming the map and the node |
| A skipped node's indices are bad | Nothing. A node of fewer than three vertices is not read at all, and an invisible one is read only as far as `iSurf` |
| A node has no area | It emits nothing |
| A surface's texture was skipped, or is null | Its triangles are drawn with no material and `u = v = 0` |
| A texture's `DrawScale` is zero, negative, infinite or NaN | The scale is `1` |
| The emitted vertices or indices would reach 2^32 | The bake is refused |
| The level draws no node | `GEOM` is written empty |
| A version-61 `Model` (UTA-0072) | `readModel` refuses first, as today |

## 7. Tests

**Unit, on every CI leg, with no Unreal Tournament present:**
`tests/unit/BundleGeometryTest.cpp` for INV-1 and INV-2;
`tests/unit/BakeGeometryTest.cpp` for INV-3, INV-4, INV-5, INV-6, INV-7,
INV-8 and INV-9, calling
`buildGeometry` on a `upkg::Model` built in memory; `tests/unit/BakeTest.cpp`
for INV-10 and INV-11. Each is seen failing before the code it locks exists.

**The fixtures grow.** `ModelExportWriter` in
`tests/support/UnrealPackageBuilder.h` gains `points`, `vectors` and `verts`,
a node's `iVertPool` and `numVertices`, and a surface's `pBase`, `vNormal`,
`vTextureU`, `vTextureV`, `panU` and `panV`. `MapBuilder` in
`tests/unit/BakeFixture.h` gives each surface a polygon, so UTA-0011 INV-5's
golden bake covers `GEOM`.

**Real-asset tier, local only:** a new file, `tests/real/RealGeometryTest.cpp`,
kept out of `RealInstallTest.cpp` because UTA-0103 splits that file by
subject. For every map it runs `buildGeometry` and asserts INV-4 on every
emitted polygon. It prints:

- nodes drawn, reversed, and skipped for each of: no area, `PF_Invisible`,
  fewer than three vertices; and drawn nodes carrying `PF_Portal` or
  `PF_FakeBackdrop`;
- the pan-sign tally § 4.3 rests on: per axis, seams where only `+` lines
  up and seams where only `-` does. A seam is two drawn polygons of
  different surfaces sharing two points by value, whose surfaces name the
  same texture with equal `TU`, `TV` and `N` and different pans. A sign lines
  up on U where both sides' `(P - Base) · TU ± panU` differ by a whole
  multiple of `uSize`, and on V where their `(P - Base) · TV ± panV` differ
  by a whole multiple of `vSize`, within a thousandth of a texel;
- how many baked textures carry `DrawScale`, and the values it takes;
- maps refused, by reason.

**Mutation, by hand** (`CLAUDE.md` § Build and test): subtract the pan; skip
the reversal; emit zero-area nodes; drop the `PF_Invisible` test; take the
opaque variant for a masked surface; order batches by node; remove the
`pVertex` bounds check (under `--asan`); compute the tiling sum in 32 bits.
Each must be killed by the invariant that names it.

## 8. Alternatives considered (and rejected)

- **The project's own surface kinds now.** The user chose raw flags now and
  kinds later (§ 3 decision 2); kinds are UTA-0104's.
- **Polygons, left for the renderer to triangulate.** Where a surface's
  shape is decided is the baker (UTA-0004 § 3.1), and a renderer reading
  n-gons repeats that work on every load.
- **Welding vertices shared between nodes.** Neighbouring surfaces differ in
  normal and texture coordinates, so few would weld, and welding needs a
  float-equality rule this item has no other use for.
- **Keeping the file's winding.** The nodes that wind against their normal
  would be culled by any renderer that culls back faces.
- **Keeping invisible surfaces.** UT99 never draws them. Collision reads the
  `Model` itself (UTA-0111), so it loses nothing.
- **Subtracting the pan.** The seam tally in § 4.3 refutes it.
- **Masking the editor bits (`PF_Selected`, `PF_Memorized`) out of the
  flags.** That is a decision about what a surface's flags mean, which is
  UTA-0104's. Its cost here is a few extra batches.
- **`u16` indices.** A level would have to be split wherever it passed
  65,536 vertices, and the saving is two bytes an index.
- **Reading a property named `Scale`.** That is the C++ member's name, and
  a package names a property by its script declaration, so no file carries
  it.
- **A second coordinate set for baked light now.** UTA-0112 has not chosen
  how it bakes, and a set added then changes the format either way.

## 9. Out of scope

- The project's own surface kinds, and whether a texture's own flags combine
  with a surface's — tracked by UTA-0104.
- What any flag means when drawn: culling, portals, sky, translucency,
  panning — tracked by UTA-0014, with water and glass by UTA-0089.
- Coordinates for baked light — tracked by UTA-0112.
- Collision — tracked by UTA-0111.
- The geometry of movers, each its own `Model` — baked by UTA-0119 into
  `MOVR`. A static brush actor is already in the level's `Model`: the editor
  merged it in.
- Textures carrying a `Format` property, which get no material — tracked by
  UTA-0118.
- A version-61 `Model` — tracked by UTA-0072.

## 10. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1, INV-2 | `tests/unit/BundleGeometryTest.cpp`, a unit test |
| INV-3, INV-5, INV-6, INV-7, INV-8, INV-9 | `tests/unit/BakeGeometryTest.cpp`, a unit test |
| INV-4 | `tests/unit/BakeGeometryTest.cpp`, a unit test; and `tests/real/RealGeometryTest.cpp`, a real-asset test, over every map |
| INV-10, INV-11 | `tests/unit/BakeTest.cpp`, a unit test |
| `BAKER_REVISION` covering geometry | **Partial:** `tests/unit/BakeGoldenTest.cpp`, a golden-hash test, catches what its fixture draws; a change reached only by real content passes |
| The PolyFlags bit values being UT99's | **nothing** — the tests use the constants the code uses, and the values rest on the cited header |
| The pan's sign matching UT99 | **Partial:** `tests/real/RealGeometryTest.cpp`, a real-asset test, prints the seam tally; nothing asserts it, and no CI leg runs it |
| The driver's `Info.UScale` being the texture's `DrawScale` | **nothing** — the public source does not show what fills it; § 15 |
| The renderer taking § 4.3's winding as front-facing | **nothing** until UTA-0014 draws a bundle |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md`, in the same change
  as the code: § 4.2's minimum-size table gains `GeometryVertex` and
  `GeometryBatch`; § 4.3's `formatVersion` and § 4.4's id list gain
  version `4` and `GEOM`; § 4.10's API and order clause gain `GEOM`; INV-4
  is annotated in place with version `4`.
- `docs/specs/UTA-0011-map-baker.md` — § 4.5 gains the `GEOM` step and
  § 4.3's `BAKER_REVISION` moves to `2`. Recorded when built.
- `src/ubake/Bake.h` and `src/ubundle/Bundle.h` — their SCOPE and
  `FORMAT_VERSION` comments.
- `CHANGELOG.md` — an `### Added` entry for the geometry, and a
  `### Changed` entry for the bundle format's version `4`, as
  `docs/standards/versioning-overrides.md` § Override requires of a breaking
  change.
- `docs/design.md` — none. It names no section by id.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0109-map-geometry-loop-log.md`.

## 13. Resource cost

- No new target and no new dependency.
- 32 bytes per emitted vertex and 12 per triangle. A node has at most 255
  vertices (`numVertices` is a byte), so the section is bounded by the
  `Model`, which is bounded by the file.
- `buildGeometry` runs on one thread. Not measured; the real-asset case
  prints its time over the install.

## 14. Migration / compatibility

**No `.utab` exists that version `4` orphans**: `0.1.0` has not been cut,
which is UTA-0011 § 14's argument, unchanged. The reader accepts one version
(UTA-0008 INV-4), so a version-`3` file is refused rather than misread, and
UTA-0011 § 4.7's cache check bakes over it.

## 15. Open questions

- **What fills `Info.UScale`.** § 4.4 takes it to be the texture's
  `DrawScale`, on the strength of the header's comment; the code that fills
  it is not public. If UT draws with something else, § 4.4's scale rule
  changes and `BAKER_REVISION` moves.
