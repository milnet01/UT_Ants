# UTA-0009 — `umat`: generate a PBR material from a 1999 texture

**Status:** accepted (2026-09-10).
**Kind:** implement.
**Source:** ROADMAP UTA-0009 (design-2026-09-03; scope settled with the
user 2026-09-05, 2026-09-09 and 2026-09-10).

**Pairs with:** UTA-0010 (the curated library, consulted before this),
UTA-0104 (the map standard, which owns how a map names what it uses).
**Blocker for:** UTA-0011 (`ubake` calls this to make every material).

**Layman:** Turn each flat 1999 texture into a modern material — bigger,
with depth, roughness and shine — worked out automatically and the same
way on every machine.

## 1. Goal

After this ships, `umat` turns one Unreal Tournament texture into one
**material**. That is its base colour enlarged, plus normal, roughness
and height maps derived from it, plus an emissive map where the material
is marked to glow. Each map carries a full mip chain and is
block-compressed by UTA-0052's `umat::compress`. The same input and
settings give the same bytes on every machine, compiler and worker count.
A locally supplied replacement image can stand in for the original.
Nothing here runs an AI model.

## 2. Problem

1. **Nothing turns a texture into a material yet.** `upkg::readTexture`
   returns palettised `upkg::Mip` levels and `upkg::readPalette` their
   `upkg::Palette`. `src/upkg/Texture.h`'s header says: *"Turning a 1999
   texture into a material is `umat` (UTA-0009)"*. `umat` holds only
   UTA-0052's compression and budget today.
2. **UTA-0052 left the generating half to this item by name.** Its § 4.9
   says of `compress`: *"Generating the levels and deriving the five maps
   are UTA-0009's."* Its § 4.2 fixes each map's block format.
3. **The roadmap body's PolyFlags premise is false.** It says *"the
   original texture's PolyFlags say which surfaces are glass, water, sky
   or self-lit"*. Measured 2026-09-10 and recorded on UTA-0009: texture
   objects carry no `PolyFlags` property, and surfaces sharing one texture
   disagree on the flags often. The flags live on surfaces —
   `upkg::BspSurf::polyFlags` and `upkg::Polygon::polyFlags`. So a water or
   glass tag on a material would be wrong for many of its surfaces, and
   this item derives none (§ 9).
4. **See-through texels depend on the surface, not the texture.** Palette
   index 0 is see-through on a masked surface. Most textures also use it as
   an ordinary colour. Measured 2026-09-10 in a scratch run: textures
   marked `bMasked` hold far more index-0 texels than the rest. That run is
   not in this repository, so the figure is unverified here until § 7's
   census reproduces it. § 4.2 answers with a masked variant.

## 3. Scope decisions (agreed with the user)

1. **The baker enlarges with a classic method built into it. AI
   upscaling is an optional separate tool.** Its output is a replacement
   image (decision 4). *Decided by the user, 2026-09-10*, offered against
   *classic only* and *AI inside the baker*. The reason is `docs/design.md`
   § What every part does the same way. Its Content addressing bullet says
   every other bake input *"must be covered by one of the three, or the
   name is a lie"*. Its Determinism bullet rules out *"no platform maths
   library in the simulation or the baker"*.

2. **The enlarger is Lanczos.** *Chosen by measurement at the user's
   request* — the user asked for research, not a visual pick. Measured
   2026-09-10 in a scratch run: one square texture per texture package in
   the reference install, shrunk to a quarter, enlarged back ×4, scored by
   mean PSNR against the original. Lanczos (a = 3) scored highest, bicubic
   second, and the pixel-art scaler (Scale2x twice) below plain
   nearest-neighbour. That run is not in this repository; INV-13 ships the
   same measurement.

   The sources agree. Wikipedia's *Pixel-art scaling algorithms* says of
   those scalers: *"Such changes may be undesirable, especially if the goal
   is to faithfully reproduce the original appearance."* Its *Image
   scaling* gives bicubic and sinc/Lanczos for continuous-tone images.
   Source: <https://en.wikipedia.org/wiki/Pixel-art_scaling_algorithms>.
   Source: <https://en.wikipedia.org/wiki/Image_scaling>.

3. **Metal is one setting per material, not a map.** *Decided by the
   user, 2026-09-10*, offered against guessing metal texel by texel. The
   curated library (UTA-0010) and a map's recipe mark real metal.

4. **A replacement image may stand in for the original texture.** *User,
   2026-09-05 and 2026-09-09*: upgrading the original is preferred, and an
   Internet-sourced replacement is allowed. It is referenced by the recipe
   and supplied locally, never committed. Its bytes reach the bundle's name
   through the recipe, or the Content addressing bullet is broken.

5. **A material is named by the package its texture came from.** *User,
   2026-09-10, on UTA-0104*: two creators' maps must never be read
   differently. UTA-0104's body records a measured clash: two unrelated
   packages named `wonderland`, one of textures and one of sounds. So a
   bare texture name is not an identity (§ 4.6).

## 4. Design

### 4.1 The files, and what `uta_umat` links

**Each stage is its own file**, so separate sessions can work on
separate stages:

| File | Holds |
|---|---|
| `src/umat/Material.h/.cpp` | UTA-0052's `compress` and budget, unchanged |
| `src/umat/Resolve.h/.cpp` | palette to RGBA8, and the masked variant (§ 4.2) |
| `src/umat/Enlarge.h/.cpp` | the Lanczos table and `enlarge` (§ 4.3) |
| `src/umat/Derive.h/.cpp` | the mip chain and the derived maps (§ 4.4, § 4.5) |
| `src/umat/Generate.h/.cpp` | `materialId` and `generate` (§ 4.6, § 4.7) |

Everything runs at bake time. `docs/design.md` § The parts lists `umat`
under *Build-time only*. Rows run on `core`'s `JobSystem::parallelFor`,
one index per row, each job writing only its own row. **A job reads only
the level it derives from, never the level being written.**

**`Resolve` takes `upkg` types, so `uta_umat` gains the `uta_upkg`
link.** UTA-0052's § 4.1 says *"UTA-0009 adds the link and amends INV-12
in the same change"*. § 11 carries it.

### 4.2 Resolving the palette, and the masked variant

Each texel takes its palette entry's `r`, `g` and `b`. The entry's `a` is
ignored and alpha is set here:

- **Opaque variant:** alpha 255 everywhere. Index 0 is an ordinary colour.
- **Masked variant:** alpha 0 on index-0 texels, 255 elsewhere.

**The masked variant fills see-through texels' colour from their
neighbours before anything filters it.** Otherwise the index-0 colour
bleeds into the edge of every cutout. One pass gives every unfilled
index-0 texel with a filled neighbour the rounded mean of those
neighbours' colours. Neighbours are the 8 around it, with wrap edges.
Each pass reads the previous pass's state, and passes repeat until
nothing changes. A texture with no opaque texel keeps its palette
colours.

The bake asks for the masked variant only where a surface uses the
texture masked. The two variants are two materials (§ 4.6).

**A replacement image carries its own alpha, and no fill runs on it.**
Its masked variant keeps that alpha as given; its opaque variant has
alpha set to 255. UTA-0011 prepares the variant before calling
`generate`, which takes its base as given.

### 4.3 Enlarging: a fixed table, integer arithmetic, wrap edges

The factor is `umat::upscaleFactor(width, height, requested)`: 1, 2 or
4, bounded by `ubundle::MAX_UPSCALE_FACTOR`.

- **Factor 1 copies the input bytes.** Nothing is filtered.
- **Output texel `j` samples source position `(j + ½) / k − ½`** at
  factor `k`. Its phase is `j mod k`, and it reads the `2a` source texels
  nearest that position.
- **Only `k` phases exist at an integer factor, so the weights are a
  table in the source.** They are generated offline from the Lanczos
  kernel, so the baker calls no `sin`. That keeps the Determinism bullet
  true by construction.
- **Weights are the normalised kernel in fixed point.** The raw kernel
  does not sum to 1 at a fractional offset, so each phase's values are
  divided by their sum, scaled by `WEIGHT_ONE` and rounded. The largest
  tap absorbs the rounding remainder, so each phase sums to exactly
  `WEIGHT_ONE`.
- **Separable.** A horizontal pass writes an 8-bit intermediate, then a
  vertical pass writes the output. Each pass sums texel times weight in
  `std::int64_t`, adds half of `WEIGHT_ONE`, shifts, and clamps to
  [0, 255]. No floating point.
- **Edges wrap.** World textures tile; a clamped edge seams every repeat.
- **All four channels go through the same filter**, alpha included. The
  renderer alpha-tests, so an alpha between 0 and 255 is not an error.

```cpp
namespace uta::umat::detail {
inline constexpr int LANCZOS_A = 3;
inline constexpr std::int32_t WEIGHT_ONE = 1 << 14;
/// kWeights2[phase][tap] and kWeights4[phase][tap], 2 * LANCZOS_A taps.
}
namespace uta::umat {
[[nodiscard]] Result<Image> enlarge(const Image& rgba, std::uint32_t factor,
                                    JobSystem& jobs);
}
```

Source: <https://en.wikipedia.org/wiki/Lanczos_resampling>.

### 4.4 Deriving the maps

**Height** is the Rec. 709 luma of the base colour, in integer weights
summing to 256:

```cpp
// Y' = 0.2126 R' + 0.7152 G' + 0.0722 B', scaled to 256.
height = (54 * r + 183 * g + 19 * b + 128) >> 8;
```

Source: <https://en.wikipedia.org/wiki/Luma_(video)>.

**Normal** is the Sobel gradient of the height, with wrap edges:

```text
Gx = [-1 0 +1; -2 0 +2; -1 0 +1]      Gy = [-1 -2 -1; 0 0 0; +1 +2 +1]
v   = (-s * k * Gx,  s * k * Gy,  255 * 2^l)    k: the applied factor
len = isqrt(vx² + vy² + vz²)                   l: the mip level
```

Source: <https://en.wikipedia.org/wiki/Sobel_operator>.

- **`k` and `2^l` keep one surface's slope the same at every factor and
  level.** A texel at level `l` of a base enlarged by `k` spans `2^l / k`
  texels of the image handed to `generate`.
- **The axes are pinned, because the renderer binds to them.** X is
  +right. Y is +toward row 0, the top of the image — so the `Gy` term is
  positive, `Gy` being measured downward.
- **Storage** follows glTF 2.0's mapping of a channel's [0, 1] to
  [−1, 1]. X and Y go into BC5's two channels as
  `(255 * vc + 256 * len) / (2 * len)` for component `vc` — the integer
  form of `127.5 * c + 128`, rounded down. Z is reconstructed as
  UTA-0052's § 4.2 says. Source: <https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc>.
- **Integer arithmetic throughout.** `s` is an integer constant, and
  `isqrt` is an integer square root, rounded down, written in `umat`. No
  floating point and no maths library, so the Determinism bullet holds
  here by construction, as in § 4.3.

**Roughness** is a heuristic, and is stated as one — darker texels
rougher, lighter texels smoother:

```cpp
rough = std::clamp(settings.baseRoughness + (128 - height) / 4, 0, 255);
```

The curated library and the recipe override `baseRoughness` where it is
wrong.

**Metal is not a map** (§ 3 decision 3); `Material::metallic` carries it.

**Emissive** exists only when `settings.emissive` is set. It is the base
colour where the height is at or above `settings.emissiveThreshold`, and
black elsewhere. Without it the map is absent, rather than a black map
paying the full per-texel cost.

### 4.5 Mip chains

Every map carries the full chain, so `mipCount` is
`std::bit_width(max(width, height))` — the maximum UTA-0052's INV-2
admits. **Base colour and height levels** are 2×2 box averages of the
level above, `(a + b + c + d + 2) >> 2` per channel. Each level is
floored at one texel per axis. **Normal, roughness and emissive are
derived per level** from that level's height and base colour. They are
never averaged: an averaged normal is no longer unit length.

### 4.6 Identity

```text
<package>.<path>[#masked]:<map>        ASCII, lower-cased
<path> is each group holding the texture, outermost first, then its
       name, joined by .
<map>  is one of base | normal | rough | height | emit
```

`umat::materialId` returns the part before the colon, which becomes
`Material::id`. UTA-0011 builds `<path>` from the export's `outer` chain
and passes it already joined; UTA-0010's library writes ids in the same
form. Two textures sharing a name in different groups of one package
therefore differ.
Each map's `ubundle::CompressedTexture::name` is the whole string. UT99
resolves names case-insensitively, hence the lower case. **The bundle and the renderer look maps up by this name**, which is
why it is fixed here. A replacement keeps the identity of the texture it
replaces.

`<package>` is the package name as UT99 resolves it. Two creators can
ship different packages under one name (§ 3 decision 5). *Decided by the
user, 2026-09-10*: they are told apart by a fingerprint of each
package's contents. Its form is UTA-0104's, and only the `<package>`
segment changes when it lands (§ 15).

### 4.7 The API

```cpp
namespace uta::umat {

enum class MapKind : std::uint8_t { Base, Normal, Rough, Height, Emit };

struct MaterialSettings {
    /// What the material asks for; upscaleFactor decides what applies.
    std::uint32_t requestedUpscale = ubundle::MAX_UPSCALE_FACTOR;
    bool metallic = false;
    std::uint8_t baseRoughness = 191;
    bool emissive = false;
    std::uint8_t emissiveThreshold = 192;
};

struct Material {
    std::string id;
    bool metallic = false;
    /// In MapKind order. Emit is absent unless settings.emissive.
    std::vector<ubundle::CompressedTexture> maps;
};

/// Resolve.h -- one palettised level to RGBA8, the masked fill included
/// (§ 4.2).
[[nodiscard]] Result<Image> resolve(const upkg::Mip& level,
                                    const upkg::Palette& palette,
                                    bool masked);

/// Generate.h -- § 4.6.
[[nodiscard]] std::string materialId(std::string_view package,
                                     std::string_view path, bool masked);

/// Generate.h -- one material from an RGBA8 base level: a resolved texture
/// or a replacement. Its dimensions become every map's sourceWidth and
/// sourceHeight.
[[nodiscard]] Result<Material> generate(std::string id, const Image& base,
                                        const MaterialSettings& settings,
                                        JobSystem& jobs);

} // namespace uta::umat
```

`Derive.h` exposes each stage over one level — `heightOf`, `normalOf`,
`roughnessOf`, `emissiveOf` and `mipChain` — so a test can check a stage
before compression.

**Refusals, each `InvalidArgument`:**
- `resolve`: a zero dimension, a pixel count that is not width × height,
  or an index past the palette.
- `generate`: a base that is not 4 channels, or not a power of two in
  each axis; and every refusal of `compress`.

`generate` never shrinks a texture to make it fit; UTA-0052's
`enforceBudget` decides that. A job body that throws fails the whole
material, never returning part of one.

### 4.8 As built (2026-09-10)

Recorded after the build. None of it changes a contract above.

- `enlarge` and `compress` run rows on `parallelFor`. The fill and the
  `Derive.h` stages run serially: they cost little beside the encoder, and
  serial code cannot depend on the worker count.
- The tables are `detail::kWeights2` and `detail::kWeights4`. A phase's
  first tap is `floor((2j + 1 − k) / 2k) − (A − 1)`, computed without
  dividing a negative.
- `detail::NORMAL_STRENGTH` is 1, the smallest integer `s` (§ 15).
- INV-6's goldens are one FNV-1a digest per map, not literal block arrays.
- INV-13's census and § 2's figures are the two `[umat]` cases in
  `tests/real/RealInstallTest.cpp`. The figures are their output.

## 5. Invariants

- **INV-1** — `resolve` gives every texel of the opaque variant, and
  every non-index-0 texel of the masked one, its palette entry's `r`,
  `g` and `b`. The masked variant's index-0 texels carry § 4.2's fill.
  The opaque variant's alpha is 255 everywhere. The masked
  variant's is 0 exactly on index-0 texels and 255 elsewhere.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`, a 4×4 palettised fixture
  with index 0 in known cells, resolved both ways. No arrow: the surface
  does not exist yet.
  *Breaks when:* index 0 is see-through in the opaque variant, holing
  every ordinary texture that uses it as a colour. The fixture's index-0
  entry carries alpha 0 in the palette, so reading the palette's `a`
  breaks this too.

- **INV-2** — the enlarged base measures `upscaleFactor(w, h, requested)`
  times the input in each axis. Every map records the input's dimensions
  as `sourceWidth` and `sourceHeight`. At factor 1 the output bytes equal
  the input.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`: a 64×64 base at requested
  1, 2 and 4, and a 2048×4 base at requested 4 — above `MAX_OUTPUT_EDGE`,
  so factor 1, and cheap to encode. No arrow: the surface does not exist
  yet.
  *Breaks when:* the requested factor is applied instead of
  `upscaleFactor`'s, or the 2048 case is shrunk rather than passed through.

- **INV-3** — each Lanczos phase's taps sum to exactly `WEIGHT_ONE`. Each
  weight is within one unit of `WEIGHT_ONE` times the normalised kernel:
  the kernel's values at that phase, divided by their sum. A constant
  image enlarges to the same constant.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`, recomputing the
  normalised kernel with `std::sin` in the test, never in the engine. No arrow: the surface
  does not exist yet.
  *Breaks when:* the table is generated for the wrong `a`, or left
  unnormalised, which brightens or darkens every enlarged texture.

- **INV-4** — enlarging commutes with wrapping. Enlarging a texture
  shifted cyclically by one texel equals enlarging it and shifting the
  result by `factor` texels.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`, a 16×16 noise fixture.
  No arrow: the surface does not exist yet.
  *Breaks when:* edges clamp instead of wrap. A clamping enlarger passes
  every other invariant here.

- **INV-5** — in the masked variant, no texel at any level of the base
  colour carries the index-0 colour unless an opaque texel did.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`: index 0 pure green,
  opaque texels with no green. No texel of any level may have green above
  zero. No arrow: the surface does not exist yet.
  *Breaks when:* the fill in § 4.2 is skipped, or runs after the enlarger
  rather than before it.

- **INV-6** — `generate` on a fixed synthetic base produces a fixed golden
  array for every map.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`, arrays literal in the
  source. **This item's cross-compiler check**: one constant, every CI
  leg. No arrow: the surface does not exist yet.
  *Breaks when:* a step uses floating point or the maths library, or
  depends on iteration order.

- **INV-7** — `generate` output does not depend on the worker count. One
  worker, two, and `hardware_concurrency()` give identical materials.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`, three separately built
  `JobSystem`s. No arrow: the surface does not exist yet.
  *Breaks when:* rows share a scratch buffer, or the Sobel pass reads the
  level being written instead of its input.

- **INV-8** — a material carries Base, Normal, Rough and Height always,
  and Emit only when `settings.emissive`. They come in `MapKind` order, in
  UTA-0052 § 4.2's formats: BC7, BC5, BC4, BC4, BC7.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`, one base with `emissive`
  false and true. No arrow: the surface does not exist yet.
  *Breaks when:* Emit is made for every material, or a map carries the
  wrong format. UTA-0052's reader accepts any format, so only this grades
  it.

- **INV-9** — every map's `mipCount` is `bit_width(max(width, height))`.
  Base colour level `l + 1` is the rounded 2×2 box average of level `l`.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`: `mipChain` on an 8×8
  base, and `generate` on it with `emissive` set, checking every map's
  `mipCount`. No arrow: the surface does not exist yet.
  *Breaks when:* the chain stops early, or rounds down instead of to
  nearest.

- **INV-10** — a flat height gives the normal (0, 0, 1) everywhere. A
  height rising to the right tilts X negative. A height rising toward row
  0 tilts Y negative. A linear ramp's interior normal bytes at levels 0
  and 1 differ by at most one, because the integer square root rounds.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`, `normalOf` on a constant,
  a horizontal ramp and a vertical ramp, plus the ramp's level 1. No
  arrow: the surface does not exist yet.
  *Breaks when:* an axis sign flips, which lights every bump from the
  wrong side, or the `2^l` term is dropped, which steepens every distant
  surface.

- **INV-11** — `materialId` follows § 4.6. Two textures named alike in
  different packages, or in different groups of one package, get
  different ids, and the masked variant differs from the opaque one.
  *Test:* `tests/unit/MaterialGenerateTest.cpp`: packages `A` and `B`
  each holding `Wall`, groups `G1` and `G2` of `A` each holding `Door`,
  and `A`'s masked variant, in mixed case. One id is compared as an
  exact string: `materialId("A", "G1.Door", true)` is `a.g1.door#masked`. No arrow:
  the surface does not exist yet.
  *Breaks when:* the id is the bare texture name — the collision UTA-0104
  exists to prevent.

- **INV-12** — `uta_umat`'s link entries are exactly `uta_core`,
  `uta_ubundle` and `uta_upkg`.
  *Test:* `src/umat/CMakeLists.txt`'s configure-time assertion, amended
  from UTA-0052's INV-12, broken once by adding `uta_umap_build`. No
  arrow: the amendment does not exist yet.
  *Breaks when:* a convenience dependency is added, and `docs/design.md`
  § What may depend on what stops being checkable.

- **INV-13** — over the reference install, `enlarge`'s round-trip mean
  PSNR is at least bicubic's and above nearest-neighbour's.
  *Test:* a census case in `tests/real/RealInstallTest.cpp` that WARNs
  each method's figure and asserts the ranking. Real-asset tier only. No
  arrow: the case does not exist yet.
  *Breaks when:* the integer implementation loses to what § 3 decision 2
  measured with a reference filter.

## 6. Failure modes

- **A procedural texture.** `upkg::isModelledTextureClass` refuses
  `WaveTexture` but accepts `WetTexture`, `IceTexture`, `ScriptedTexture`
  and `FireTexture` beside `Texture` — `MODELLED_CLASSES` in
  `src/upkg/Texture.cpp`. `generate` cannot tell a class apart. *Decided
  by the user, 2026-09-10*: the first version shows each procedural
  texture as a still picture where one exists. Which image stands in is
  UTA-0011's; motion is UTA-0105's.
- **A palette shorter than the indices.** `resolve` refuses; nothing
  reads past it.
- **A base that is not a power of two.** Refused, naming the texture
  before `compress` would.
- **A base already at or above `umat::MAX_OUTPUT_EDGE`.** Factor 1,
  stored unreduced; the budget decides whether it fits.
- **A job body throws.** `parallelFor` returns the count, and `generate`
  fails the whole material.
- **The roughness heuristic is wrong for a material.** The curated
  library or the recipe sets `baseRoughness`. Nothing here detects it.

## 7. Tests

| File | Locks |
|---|---|
| `tests/unit/MaterialGenerateTest.cpp` | INV-1, INV-2, INV-3, INV-4, INV-5, INV-6, INV-7, INV-8, INV-9, INV-10, INV-11 |
| `src/umat/CMakeLists.txt` | INV-12, at configure time |
| `tests/real/RealInstallTest.cpp` | INV-13, real-asset tier |

Each case is seen to fail against pre-change code; for most, that is a
compile failure. INV-1, INV-4, INV-5 and INV-10 also run once with their
rule deleted, and INV-12 once with its breaking change made. **Every
guard is mutated by hand**, per `CLAUDE.md` § Build and test — the
mutation probe knows only `ubundle`.

**The real-asset census also prints § 2's figures**: the surface-flag
disagreement and the index-0 comparison. The measurements this spec rests
on then come out of the tree, not a scratch run.

## 8. Alternatives considered (and rejected)

- **AI upscaling inside the baker** — rejected by the user (§ 3 decision
  1): it breaks bake determinism.
- **A pixel-art scaler (Scale2x, hqx, xBRZ)** — measured below
  nearest-neighbour on this content, and its sources scope it to pixel
  art.
- **Bicubic** — measured second to Lanczos; kept as INV-13's floor.
- **Computing the kernel with `sin` at run time** — a platform maths call
  in the baker. The table gives the same weights without one.
- **Clamped edges** — a seam at every tile repeat (INV-4).
- **Filtering the masked variant without the fill** — the index-0 colour
  bleeds into every cutout (INV-5).
- **Water, glass, sky and self-lit tags on the material** — wrong for
  many surfaces (§ 2 item 3).
- **A per-texel metal map** — rejected by the user (§ 3 decision 3).
- **An emissive map on every material** — the full per-texel cost spent
  on black.

## 9. Out of scope

- The curated library, consulted before this — tracked by UTA-0010.
- Carrying each surface's flags into the bundle, and the map standard —
  tracked by UTA-0104.
- Writing materials into the bundle and picking each surface's variant —
  tracked by UTA-0011.
- Rendering water and glass — tracked by UTA-0089.
- Decoding a replacement PNG into an `Image`, and the recipe field naming
  it — tracked by UTA-0106.
- The optional AI upscaling tool — tracked by UTA-0107.
- Animating procedural textures (`FireTexture`, `WaveTexture` and kin) —
  tracked by UTA-0105.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2, INV-3, INV-4, INV-5 | `tests/unit/MaterialGenerateTest.cpp` |
| INV-6 | `tests/unit/MaterialGenerateTest.cpp`, on every CI leg |
| INV-7, INV-8, INV-9, INV-10, INV-11 | `tests/unit/MaterialGenerateTest.cpp` |
| INV-12 | `src/umat/CMakeLists.txt` configure-time assertion |
| INV-13 | `Partial:` `tests/real/RealInstallTest.cpp` — local-only by design (S7), so no CI leg runs it |
| The roughness heuristic suits a material | **nothing** — a heuristic; the curated library and recipe override it |
| The enlarger looks right, not only scores right | **nothing** — PSNR measures faithfulness, not appearance |

## 11. Cross-doc impact

- `docs/specs/UTA-0052-texture-memory-budget.md` — INV-12 and § 4.1 gain
  `uta_upkg`, as that spec says this item does.
- `src/umat/CMakeLists.txt` — the INV-12 assertion's permitted list.
- `docs/design.md` § The parts — the `umat` row lists "metallic" among
  what `umat` generates. Under § 3 decision 3 it is one value per
  material, not a map, and the row says so.
- `CHANGELOG.md` — an Added entry when this ships.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0009-material-from-texture-loop-log.md`.

## 13. Resource cost

No new dependency: the Lanczos weights are literals, and the rest is
integer arithmetic over `core`, `upkg` and `ubundle`. Memory is bounded
by UTA-0052's budget, at the per-texel cost its § 4.2 gives each map.
Time is bake time.

## 15. Open questions

- **The normal strength `s`, and the `settings` defaults.** Starting
  values, set on seeing real materials. Each is one constant.
- **The form of UTA-0104's package fingerprint.** It changes
  `materialId`'s `<package>` segment and nothing else here.
