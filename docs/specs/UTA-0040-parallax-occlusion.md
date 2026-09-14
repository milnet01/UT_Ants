# UTA-0040 — parallax occlusion on baked surfaces

**Status:** spec draft (2026-09-14). The user ruled it takes no review.
**Kind:** implement.
**Source:** ROADMAP UTA-0040 (user-request-2026-09-03, re-requested 2026-09-14).

**Layman:** flat walls gain visible depth — bricks, panels and ceiling lights
look recessed and shift with your view instead of reading as painted pictures.

## 1. Goal

A lit or unlit level surface whose material carries a parallax depth is shaded
at texture coordinates displaced along the view through its height map, so its
detail reads as recessed. One byte per material in `MATS` carries the depth, the
baker writes it, the curated library can set it, and the quality tier decides
the step count, with Low drawing no parallax at all.

## 2. Problem

1. `umat::generate` already builds a height map for every material
   (`src/umat/Generate.cpp`, `MapKind::Height`, the Rec. 709 luma of the base
   colour, `heightOf` in `src/umat/Derive.cpp`), and `MaterialSet::upload`
   binds it as `gpu::Material::height`. `scene.frag` never samples it, so the
   depth it describes is invisible.
2. Nothing says how deep a material is. `ubundle::MaterialRecord` holds `id`
   and `metallic`; `umat::CuratedOverride` holds `metallic`, `baseRoughness`,
   `emissive` and `emissiveThreshold`. A luma-derived height reads badly on
   some pictures (see the research below), so depth must be per material.
3. `urender::Feature` is empty (`src/urender/Tiers.h`), so no tier yet switches
   a feature; this item adds the first row, as UTA-0051 § 4.1 foresaw.

Research with sources is kept outside the repository at
`/mnt/Games/Scripts/Linux/ut-ants-uta0040-research.md`. Where a number below
rests on a source it names the source; the rest is judgement and says so.

## 3. Scope decisions (agreed with the user)

1. **A short spec with no review** — the user, 2026-09-14.
2. **How it looks is this project's to settle by research and measurement**,
   reviewed later by the user over real matches — the user, 2026-09-14. Every
   number in § 4 follows from that; none was put to the user by eye.
3. **Cheap before geometric** — the user, 2026-09-14, for UTA-0157's light
   fixtures: parallax is the first route, so light materials are in scope.

## 4. Design

### 4.1 The depth, in the bundle

```cpp
namespace uta::ubundle {
inline constexpr std::uint32_t FORMAT_VERSION = 9; // 9 since UTA-0040
struct MaterialRecord {
    std::string id;
    bool metallic = false;
    /// How deep the height map's full range reaches below the surface, in
    /// texels of the material's base level. 0: no parallax.
    std::uint8_t parallaxDepth = 0;
};
}
```

`MATS` gains one `u8` after `metallic`. Every value is defined. An element's
minimum encoded size becomes 6. Texels rather than world units because a UT99
texture maps one texel to one world unit at `DrawScale` 1 (UTA-0109 INV-11), so
a depth in texels scales with the surface as its picture does.

### 4.2 The depth, in the material maker and the library

```cpp
namespace uta::umat {
/// Judgement: shallow, because a luma-derived height is a guess (research:
/// no source covers heights derived from albedo; Tatarchuk advises gentle ones).
inline constexpr std::uint8_t GENERATED_PARALLAX_DEPTH = 4;
struct MaterialSettings { /* existing fields */ std::uint8_t parallaxDepth = GENERATED_PARALLAX_DEPTH; };
struct CuratedOverride  { /* existing fields */ std::optional<std::uint8_t> parallaxDepth; };
struct Material         { /* existing fields */ std::uint8_t parallaxDepth = 0; };
}
```

`applied` copies a present `parallaxDepth`. `digestOf` adds it after
`emissiveThreshold`, in UTA-0010 § 4.6's presence-byte form. `generate` copies
`settings.parallaxDepth` into `Material`. No curated entry sets it in this item.

### 4.3 The baker

The opaque variant's `MATS` record carries its material's `parallaxDepth`. **The
`#masked` variant's carries 0**: a masked picture is a grate, fence or foliage
cut-out, and displacing its coordinates would move the cut-out off the geometry
(judgement; the research found no source). `BAKER_REVISION` becomes 7 and the
golden bake is re-recorded.

### 4.4 The tier

```cpp
namespace uta::urender {
enum class Feature : std::uint8_t { ParallaxOcclusion };
// minimumTier(ParallaxOcclusion) == Tier::Medium
struct ParallaxSteps { std::uint32_t minimum, maximum; };
/// Low {0, 0}; Medium {8, 16}; High {8, 32}; Ultra {16, 48}.
[[nodiscard]] constexpr ParallaxSteps parallaxStepsOf(Tier tier) noexcept;
}
```

The ranges sit inside what shipping engines use — Godot 8–32, HDRP 5–15,
Tatarchuk 8–50 — and which tier gets which is judgement. `Pipelines::create`
takes the tier and passes the two counts to `scene.frag` as specialization
constants, so Low's pipelines compile the march out.

### 4.5 The shader

In `scene.frag`, before any material map is sampled:

1. Take `dFdx(uv)` and `dFdy(uv)` once. Every material map is then sampled with
   `textureGrad` at the shading coordinate, and the height map inside the march
   with `textureLod` at a level fixed before the loop: implicit-derivative
   sampling inside a loop with an early exit is undefined (Khronos,
   "Non-Uniform Control Flow").
2. **Parallax runs only where** the maximum step count is above 0,
   `material.parallaxDepth` is above 0, and the batch is neither `PF_Masked` nor
   `PF_FakeBackdrop`. `PF_Unlit` surfaces take it: light fixtures are unlit.
3. The tangent frame is the one `perturbed` already builds from screen
   derivatives, factored into a function both use. The view direction goes into
   that frame.
4. **Fade by mip.** With `lod` the height map's `textureQueryLod`, the depth is
   scaled by `1 - clamp(lod - PARALLAX_FADE_MIP, 0, 1)`, `PARALLAX_FADE_MIP = 4`.
   HDRP fades over one mip from level 5; this starts a level sooner because UT99
   pictures start at 256 or 512 texels (judgement). Past the band no step runs.
5. **March.** White is high and depth is pushed inward, as Tatarchuk, HDRP and
   Godot take it. Steps are `mix(maximum, minimum, abs(viewTangent.z))`. The
   ray's total offset is `parallaxDepth / textureSize(base, 0)` in UV per axis,
   along `viewTangent.xy / viewTangent.z`, with the z term clamped away from 0.
   A linear search finds the first layer below the height, then one linear
   interpolation between the last two samples refines it, as Godot does.
6. The refined coordinate shades everything: base, normal, roughness, emissive
   and the mask test's alpha. Depth, velocity and shadows are unchanged — no
   depth is written from the shader, so early depth testing survives.

No silhouette correction: no mainstream engine ships it (Bevy's documentation,
HDRP's "POM is always inward"). No self-shadowing: § 9.

### 4.6 The renderer's material record

`gpu::Material` gains `std::uint32_t parallaxDepth` after `metallic`, 28 bytes,
mirrored in `types.glsl`. `MaterialSet::upload` copies the record's value; the
built-in default material carries 0.

## 5. Invariants

- **INV-1** — `MATS` round-trips `parallaxDepth` for every byte value, and a
  record encodes in 6 bytes with an empty id.
  *Test:* `tests/unit/BundleMaterialTest.cpp`, values 0, 1, 4 and 255.
  *Breaks when:* the byte is not written, not read, read into another field, or
  the minimum size stays 5.

- **INV-2** — a generated material's `parallaxDepth` is
  `GENERATED_PARALLAX_DEPTH`; a curated entry setting it replaces it; and the
  library digest changes when an entry's `parallaxDepth` is set.
  *Test:* `tests/unit/MaterialLibraryTest.cpp` and
  `tests/unit/MaterialGenerateTest.cpp`.
  *Breaks when:* `applied` ignores the field, `generate` drops it, or `digestOf`
  leaves it out.

- **INV-3** — a baked opaque variant's record carries its material's
  `parallaxDepth`, and its `#masked` variant's carries 0.
  *Test:* `tests/unit/BakeTest.cpp`, one texture named by a masked and an
  unmasked surface.
  *Breaks when:* the masked variant keeps the depth, or the opaque one loses it.

- **INV-4** — `minimumTier(Feature::ParallaxOcclusion)` is Medium, and
  `parallaxStepsOf` returns Low {0, 0}, Medium {8, 16}, High {8, 32} and Ultra
  {16, 48}.
  *Test:* `tests/unit/RenderTiersTest.cpp`, one literal per tier.
  *Breaks when:* a row moves or Low runs steps.

- **INV-5** — on Medium, a square whose base map splits in two colours along a
  line and whose height map is high on one side and low on the other, viewed
  from the low side, draws the high colour further across that line than Low
  does; viewed from the high side, no further; and the same square with
  `parallaxDepth` 0 draws identically on both tiers.
  *Test:* `tests/device/RenderParallaxTest.cpp`, the shift's direction and a
  minimum size measured on lavapipe and written into the test. Both sides are
  needed: the march reaches past the picture's repeat, so from one side alone a
  march going the wrong way can look like one going the right way.
  *Breaks when:* the shader ignores `parallaxDepth`, marches the wrong way, runs
  on Low, or runs at depth 0.

## 6. Failure modes

- **A height from luma is wrong for a picture** — a dark painted groove on a flat
  panel reads as a pit. The generated depth is shallow for that reason, and a
  curated entry can set 0 or more per picture.
- **Grazing views** stair-step at the step counts above. Steps rise with the
  angle; a surface past the fade band gets none.
- **A material with no height map** samples the default height map, which is
  flat, so nothing moves.
- **A surface whose UVs change sharply across a triangle** gets a poor
  derivative frame. That is `perturbed`'s existing limit and nothing here fixes
  it.

## 7. Tests

| File | Label | Locks |
|---|---|---|
| `tests/unit/BundleMaterialTest.cpp` | `unit` | INV-1 |
| `tests/unit/MaterialLibraryTest.cpp`, `tests/unit/MaterialGenerateTest.cpp` | `unit` | INV-2 |
| `tests/unit/BakeTest.cpp` | `unit` | INV-3 |
| `tests/unit/RenderTiersTest.cpp` | `unit` | INV-4 |
| `tests/device/RenderParallaxTest.cpp` | `device` | INV-5 |
| `tests/unit/BakeGoldenTest.cpp` | `unit` | re-recorded under revision 7 |

Each is watched failing before its code exists. INV-5 is also watched failing
with the march's sign flipped and with the depth ignored.

## 8. Alternatives considered (and rejected)

- **Steep parallax with no refinement** — visible layering at these step counts;
  one interpolation costs one sample more.
- **Relief mapping's binary search** — more samples for detail a 256-texel
  picture does not hold.
- **Depth in world units** — would not scale with a surface's `DrawScale` as its
  picture does.
- **A global depth instead of a per-material one** — the roadmap body rejects it:
  a picture with no real depth reads worse with parallax than without.
- **Self-shadowing on every clustered light** — no source covers it, and it
  multiplies the march by the light count.

## 9. Out of scope

- Self-shadowing in height space — deferred; not yet queued.
- Silhouette correction — deferred; not yet queued.
- Curated depths for particular pictures, the light fixtures first — tracked by
  UTA-0157.
- Parallax on movers' surfaces beyond what the shared shader already gives —
  deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 to INV-4 | the unit tests in § 7 |
| INV-5 | `tests/device/RenderParallaxTest.cpp`, on Linux CI's lavapipe only |
| § 4.5 step 1's explicit gradients | **nothing** — a driver may still sample correctly with implicit ones; the validation layer does not report it |
| § 4.5 step 4's fade | **nothing** — no test looks at a distant surface |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md`: `MaterialRecord`'s
  minimum becomes 6.
- `docs/specs/UTA-0011-map-baker.md` § 4.10: the `parallaxDepth` field and
  format version 9.
- `docs/specs/UTA-0010-curated-material-library.md` § 4.3 and § 4.6: the new
  override field and its place in the digest.
- `docs/specs/UTA-0051-quality-tiers.md` § 4.1: the first `Feature` row.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

No review: the user ruled on 2026-09-14 that this spec takes none.

## 13. Resource cost

One `u8` per material in `MATS` and four bytes per material on the GPU. The
march costs up to the tier's maximum step count in height samples per shaded
pixel of a parallax surface.

**Measured 2026-09-14** on the RX 6600, DM-Deck16][ from its first PlayerStart,
`ut-ants --windowed --frames 300` at 1280 × 720, three runs per tier. Before is
commit `d1bc5af` on a format-8 bake; after is this item on a format-9 bake. Each
figure is the last frame's time in milliseconds, at scale 1 in every run:

| Tier | Before | After |
|---|---|---|
| Low | 9.40 – 9.43 | 8.22 – 9.42 |
| Medium | 9.47 – 9.77 | 8.45 – 9.73 |
| High | 9.33 – 9.45 | 9.52 – 10.34 |
| Ultra | 8.06 – 9.64 | 9.64 – 10.02 |

Runs of one build spread by up to 1.6 ms, so the cost is below that at Low and
Medium and at most about 1 ms at High and Ultra. **Not measured**: the cost at
4K, where it scales with the shaded pixels, and a figure averaged over frames
rather than one frame paced by FIFO presentation.

## 14. Migration / compatibility

Format version 9 refuses every version-8 bundle (UTA-0008 INV-4). 0.1.0 has not
been cut, so no shipped bundle is orphaned; a map is re-baked.
