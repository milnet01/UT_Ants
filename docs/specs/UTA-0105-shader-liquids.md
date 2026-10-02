<!-- ants-spec-format: 1 -->
# UTA-0105 — shader liquids

**Status:** accepted (2026-10-02), unreviewed. Contract reviews are cancelled for this
project (`CLAUDE.md` § Contract reviews); no review runs unless the user asks.
**Kind:** feature.
**Source:** ROADMAP UTA-0105 (user-request-2026-09-10; the shader decision,
user 2026-10-02).

**Layman:** water, slime and lava ripple and drift again, and the other
textures that moved by themselves in the original — glowing lasers, warps,
plasma — slide and wobble the way they did, instead of standing still.

## 1. Goal

A surface whose texture is a WetTexture, an IceTexture or a WaveTexture moves.
The renderer shifts the texture's picture with moving noise before sampling
it, so the picture ripples and drifts. On a lit surface the same noise tilts
the shading normal, so the light on the liquid ripples too. A WaveTexture,
which has no picture, is drawn from that noise in its own colours, with a
moving highlight. Each texture's own settings set how strong and how fast
its motion is.

## 2. Problem

1. The user, 2026-09-14: *"I would like liquid to look like liquid even if we
   fake it."* The user, 2026-10-02, chose a modern shader of three options:
   each liquid keeps its still and the shader adds moving ripples, flow and
   shine from noise, with speed and scale from that texture's own settings.
2. Today these textures are stills. `Bake.cpp`'s `makeVariant` shows a
   WetTexture or IceTexture as its SourceTexture's picture (UTA-0155), and a
   texture with no picture anywhere as a flat fill in the colour nearest its
   palette's mean (UTA-0177). Every WaveTexture is such a fill.
3. The fact that a material was a liquid is gone after the bake.
   `ubundle::MaterialRecord` carries `id`, `metallic`, `parallaxDepth` and
   UTA-0263's `flame`, and nothing liquid-specific is read from the texture:
   `makeVariant` reads `Palette`, `SourceTexture` and `DrawScale`, and
   FireTexture's settings only through `fireSettingsOf`.
4. No liquid texture stores pixels of its own. Census
   (`~/.cache/uta-scratch/u105/REPORT.md` § 3: `ut-dump --install <install>
   --ndjson` over every map in `Maps/`, then `liquid_census.py`):

   | class | textures | maps | surfaces | stored pixels |
   |---|---|---|---|---|
   | WetTexture | 160 | 846 | 29574 | none; 158 name a SourceTexture, 2 do not resolve |
   | IceTexture | 32 | 75 | 1456 | none; all name a SourceTexture and a GlassTexture |
   | WaveTexture | 14 | 62 | 697 | none, and no source |

5. **Not every one is a liquid.** The most-used IceTextures are lasers,
   flares, plasma and warps (`terranius.laserman`, `xfx.flarezw`,
   `xbpfx.blueplasma`, `genwarp.warpdemo1`); no map in the reference install
   has true ice. The third most-surfaced WetTexture is a crystal
   (`tcrystal.taryd2`, 6267 surfaces). Same census.
6. Most of these surfaces are unlit. Of the WetTexture surfaces, 12560 carry
   `PF_Unlit`; of the IceTexture surfaces, 406 (same census). On such a
   surface `scene.frag` shows the picture with no lighting, so a tilted
   normal alone would show nothing there.
7. What UT99 does, from Epic's own manual for these classes (Source:
   https://www.zx.net.nz/mirror/unreal.epicgames.com/Fire/AnimatingTextures.htm):
   a WetTexture distorts its source's picture by a simulated wave field,
   horizontally only, with `WaveAmp` scaling the waves; a WaveTexture shades
   the wave field with a bump light (`BumpMapLight`, `BumpMapAngle`) and a
   highlight (`PhongRange`, `PhongSize`) through its palette; an IceTexture
   slides one layer over the other in one of five `PanningStyle`s, its
   `HorizPanSpeed` and `VertPanSpeed` having their zero at 128, with
   `Frequency` and `Amplitude` setting the circular and wavy parts.
8. The settings that vary, over the census's textures
   (`liquid_census.json`, stored bytes tallied per class):
   - WetTexture: `WaveAmp` is 128 on 126 of 160; `FX_Frequency` is 8 on 111.
   - WaveTexture: `BumpMapAngle` 170, `PhongSize` 32 and `BumpMapLight` 50 are
     the usual values; four textures differ.
   - IceTexture: `PanningStyle` is absent (Linear) on 17, Circular on 14,
     Gestation on 1; `HorizPanSpeed` is 128 on 10 and 64 on 10.

## 3. Scope decisions (agreed with the user)

- **A modern shader, not UT99's simulation replayed** — the user, 2026-10-02,
  of three offered. § 8 says what lost.
- **The scope split** — the user, 2026-10-02, recorded on the roadmap item.
  This item moves the picture and tilts the shading normal. Reflections,
  Fresnel and depth tint are UTA-0089's; moving geometry UTA-0055's; caustics
  and the underwater view UTA-0215's.
- **Cheapest method that still looks modern, first iteration** — the user,
  2026-09-14.
- **Looks are decided by measurement, never by asking the user** — the user's
  standing instruction. § 4.6 is where that happens.
- **Every texture of the three classes moves, by class, with no curated
  list** — this spec's call. Unlike fire (UTA-0263 § 4.1), the motion here is
  UT99's own kind of motion for each class: a laser IceTexture slid in UT99
  and slides here; a crystal WetTexture wobbled and wobbles. What is NOT
  given to every one is the liquid's shine: an IceTexture gets no normal
  tilt, because most are not liquid (§ 2 item 5).
- **The bundle carries each texture's own settings unchanged; the shader
  turns them into motion** — this spec's call. So re-fitting § 4.6's
  constants never needs a re-bake.
- **A WetTexture warps in both directions** — this spec's call. UT99's is
  horizontal only (§ 2 item 7); a two-way warp reads as liquid where a
  one-way one reads as a heat shimmer.
- **An IceTexture's GlassTexture is not drawn** — this spec's call. The still
  shows the SourceTexture only (UTA-0155), and the motion slides that
  picture. `MoveIce` is not read: no census texture stores it.

## 4. Design

### 4.1 Which textures are liquids — `ubake`

`makeVariant` already folds the texture's class name to tell a FireTexture.
A texture whose class folds to `wettexture`, `icetexture` or `wavetexture`
gets a liquid look of that kind. Any other class gets none, so a subclass of
these declared in a map's own script keeps its still.

The look is set after the still is made and does not change it: the still,
its fingerprint, the curated library's entry and `umat::generate` run as
today. A liquid that is also on UTA-0263's flame list — none is, the flame
list holding FireTextures only — would carry both, and § 4.4 draws the flame.

### 4.2 `MATS` gains a liquid look — `ubundle`

```cpp
enum class LiquidKind : std::uint8_t { Wet = 1, Ice = 2, Wave = 3 };

/// A liquid material's own settings -- UTA-0105 SS 4.2. Each byte is the
/// texture's stored property, or its class's default where it stores none.
struct LiquidLook {
    LiquidKind kind = LiquidKind::Wet;
    std::uint8_t amplitude = 0; ///< Wet, Wave: WaveAmp. Ice: Amplitude.
    std::uint8_t frequency = 0; ///< Wet, Wave: FX_Frequency. Ice: Frequency.
    std::uint8_t panning = 0;   ///< Ice: PanningStyle, 0 to 4. Otherwise 0.
    std::array<std::uint8_t, 2> pan{128, 128}; ///< Ice: HorizPanSpeed, VertPanSpeed. Otherwise 128.
    std::array<std::uint8_t, 3> bump{};        ///< Wave: BumpMapLight, BumpMapAngle, PhongSize. Otherwise 0.
    std::array<std::uint16_t, 2> size{};       ///< the texture's own width and height, texels
    /// Wave: eight of its palette's colours at evenly spaced brightness,
    /// darkest first, as linear RGB (SS 4.3). Otherwise zero.
    std::array<std::array<float, 3>, 8> ramp{};
};
```

`MaterialRecord` gains `std::optional<LiquidLook> liquid`, after `flame`. On
the wire it is one byte — 0 for none, else the kind — and, when non-zero, the
look's fields in the order above. `read` and `write` refuse a kind above 3, a
`panning` above 4, a `size` of 0 or above 8192 on either side, and a ramp
value that is not finite.

**Where an absent setting comes from.** A property the texture does not
store takes its class's default, read through `upkg::readAncestry` and
`upkg::effectiveDefaults` over the texture's class. A default that cannot be
read — the class's package is missing — leaves the material with no liquid
look, and the bake reports it as it reports a skipped material (§ 6).

`FORMAT_VERSION` becomes `16` and `BAKER_REVISION` becomes `31`. `shapeOf` in
`urender/Frame.cpp` samples each liquid look, as it samples each flame look,
since the renderer uploads them.

### 4.3 A WaveTexture's colours — `ubake`

A WaveTexture has no picture, and UT99 picks its colours from its palette by
how lit the wave is. The ramp keeps that: the palette's entries, sorted by
luma, sampled at eight evenly spaced positions from darkest to brightest,
converted to linear RGB as `flameLookOf` converts. Sorting by luma, rather
than trusting palette order, makes the ramp right whichever way a palette
was laid out.

### 4.4 Drawing — `urender`

**One noise field.** `liquid.glsl` gives a height field over a liquid
surface, in the surface's texture coordinates and the time: two octaves of
value noise drifting in different directions, their size set in texels of
the texture (`size`) so a ripple spans the same share of a picture however
often it repeats. Its gradient is the **slope**. The noise is computed in the
shader; no noise texture is added. It is seeded by the material index, as
UTA-0263's flames are. For Wet and Wave it drifts at a speed scaled by
`frequency`; Ice does not use it.

**Per kind:**

- **Wet** — the picture is sampled at the texture coordinate plus the slope
  times a warp scaled by `amplitude`.
- **Ice** — the picture is sampled at the texture coordinate plus a panning
  offset. `Linear` (0) moves it at `pan − 128` in each direction; `Circular`
  (1) moves it round a circle whose radius is scaled by `amplitude` and
  whose rate is scaled by `frequency`; `Gestation` (2) scales it about the
  repeat's centre by a pulse of the same size and rate; `WavyX` (3) and
  `WavyY` (4) are `Linear` plus a sine across the other axis with that size
  and rate. There is no slope and no tilt.
- **Wave** — the colour is the ramp at a shade: one half, plus the bump light
  on the slope's normal from a direction set by `bump`'s light and angle,
  plus a highlight whose size is set by `bump`'s `PhongSize`. The shade
  replaces the picture.

**The tilt.** On a lit surface, Wet and Wave tilt the shading normal by the
slope, scaled by `amplitude`, on top of the normal map's tilt — so the
lights, shadows and probes show moving ripples. An unlit surface is not lit,
so it shows the motion through the picture only.

**In `scene.frag`.** The liquid offset is added to `shadingUv` before the base
picture is sampled, and every map sampled at `shadingUv` follows it.
Parallax is skipped on a liquid material: the march would chase a height map
the picture no longer sits on. The derivatives stay those of the undisplaced
coordinate. A masked liquid discards where the displaced picture is clear.
`Material` gains `uint liquid` — its index in a new `LIQUIDS` storage
buffer, or `NONE` — and grows to 36 bytes. `LIQUIDS` takes binding 13, and
`SHADOW_ATLAS`, `FOG_VOLUME` and `TEXTURES` move up one.

**Time** is the light clock UTA-0263 put in `FrameData::flameSeconds`, so
`Renderer::pinLightSeconds` and `ut-shot --light-time` pin liquids as they pin
flames.

**Tiers.** Liquids move on every tier with the same shader. Their cost is
measured (§ 13).

### 4.4a An Ice look's glass — `ubake`, `ubundle`, `urender`

**Amended by `UTA-0270`, recording what was built.** UT99 draws an
IceTexture by shifting where its SourceTexture is read by its GlassTexture,
which Epic's manual calls "an 8-bit distortion vector field". How a value
becomes a shift was measured from UT_MonsterHunt's face-on captures of
DOM-MetalDream's `blueplasma` (`work/uta0269/iceflatA/` and `iceflatB/`),
whose source is a ramp in u alone: each texel shows the source
`glass − 46` texels on along u, slope 1.0, the same in every frame to a
median of 2 texels.

The bake reads the GlassTexture, converts each texel to its palette colour's
Rec. 709 luma, and stores it as `<id>:glass`, one BC4 level, when it is the
Ice texture's own size; and reads `MoveIce`, a Bool, into the look's
`moveIce` byte, written in `MATS` after `panning` (format 20). The shader
reads the glass at the texel's coordinate and adds `(glass − 46) / width`
along u. With `moveIce` the glass slides and the source stays; without, the
source slides. Either slides toward +u at `0.9 × (pan − 128)` texels a second
on each axis: the glass measured 65 a second at `HorizPanSpeed` 200, the
source 62. This replaced § 4.4's guessed 32 at 255, which slid toward −u.
With no glass the look slides its source as before.

### 4.5 What does not change

The geometry does not move, so velocity is unchanged. A translucent liquid
is drawn in the translucent pass as today. Fog is applied after shading as
today.

### 4.6 Measured constants

The constants in `liquid.glsl` are fitted, not chosen: the ripple size in
texels, the drift speed per unit of `frequency`, the warp per unit of
`amplitude`, the tilt per unit of `amplitude`, the Ice pan rate per unit of
`pan − 128`, the circular and wavy size and rate, and the Wave bump and
highlight strengths. They are fitted against original frames, as UTA-0263
§ 4.6 fitted flames, with `ut-ants-uta0156`'s `capture-original.sh`, at poses
on these maps (`~/.cache/uta-scratch/u105/candidates.txt`):

- Water: DM-ArcaneTemple (`rainfx.swater4a`, and `hubeffects.waterrings2`, a
  WaveTexture), AS-OceanFloor (`hubeffects.pond1`).
- Lava: CTF-LavaGiant (`hubeffects.lavax2`).
- Slime: DM-Deck16][ (`hubeffects.goop3`).
- IceTexture: DOM-MetalDream (`xbpfx.blueplasma`).
- WaveTexture over a large area: MH-UM-Cubes (`detail.waterde2`).

The fits:

- **Motion:** the mean change over the liquid's pixels between two frames
  1/30 s apart matches the original's between two of its own frames that far
  apart, within 25%.
- **Wave brightness:** the mean displayed luma over a WaveTexture's pixels
  matches the original's within 10%, light time pinned.

Each constant records its sweep in a comment beside it, as `fog.glsl`'s do.

**As built (2026-10-02).** The four water constants are fitted on
DM-ArcaneTemple's central pool: the motion measure is 3.81 against the
original's 3.98. The Ice and Wave constants are not fitted. The
IceTexture's churn in the original comes from its GlassTexture, which this
item does not draw, so no pan rate matches it (UTA-0270). The only
captured WaveTexture is an unlit modulated layer over water, which no view
separates from the water beneath. `liquid.glsl` records the sweeps.

## 5. Invariants

- **INV-1** — `MATS` round-trips a material with each kind of liquid look and
  one without, and refuses a kind byte above 3, a `panning` above 4, a `size`
  side of 0 or above 8192, and a ramp value that is not finite.
  *Test:* `tests/unit/BundleMaterialTest.cpp`, extended.
  *Breaks when:* the look is dropped, misplaced or read into the wrong
  record, or one of the four invalid looks is accepted.

- **INV-2** — a baked WetTexture, IceTexture and WaveTexture each carry a look
  of their own kind holding their stored settings; a setting the texture does
  not store takes its class's default; a plain Texture and a FireTexture carry
  none.
  *Test:* `tests/unit/BakeLiquidsTest.cpp`, new, a baked fixture with the
  five textures and a fixture class whose default `WaveAmp` differs from the
  stored one of the other WetTexture. The default case isolates § 4.2's
  default rule: a bake reading only stored properties gives 0 there.
  *Breaks when:* a class is missed or misread, a stored setting is ignored, an
  absent one reads as 0, or a non-liquid gains a look.

- **INV-3** — a WaveTexture's ramp holds eight of its palette's colours,
  ordered by luma from darkest to brightest, the first the darkest and the
  last the brightest.
  *Test:* `tests/unit/BakeLiquidsTest.cpp`, a palette whose entries are in
  reverse brightness order. A ramp taken in palette order fails.
  *Breaks when:* the ramp follows palette order, or skips the ends.

- **INV-4** — every liquid texture of the reference install's census bakes
  with a look of its class's kind.
  *Test:* `tests/real/RealLiquidsTest.cpp`, new, in the real-asset tier, over
  the census maps of § 4.6.
  *Breaks when:* a real texture's class or settings do not read as the
  fixture's do, or its class default cannot be read from `Fire.u`.

- **INV-5** — a Wet liquid at a pinned light time draws pixel-identically
  twice, and two times 0.25 s apart differ over its pixels; the same picture
  as a plain material does not change between those times.
  *Test:* `tests/device/RenderLiquidsTest.cpp`, new, on an unlit square with
  a striped picture.
  *Breaks when:* the motion reads the wall clock, does not move, or moves a
  material with no look.

- **INV-6** — an Ice look in `Linear` with both pan speeds 128 does not move
  between two times, and with `HorizPanSpeed` 192 it does.
  *Test:* `tests/device/RenderLiquidsTest.cpp`, the striped square.
  *Breaks when:* the zero point is not 128, so a still IceTexture drifts.

- **INV-7** — the tilt reaches the light: a Wet liquid whose picture is one
  flat colour changes between two times on a lit square, and does not on an
  unlit one.
  *Test:* `tests/device/RenderLiquidsTest.cpp`. A flat picture hides the warp,
  so only the tilt can change the lit square.
  *Breaks when:* the slope does not tilt the normal, or tilts an unlit
  surface's colour.

- **INV-8** — the glass: a uniform glass of 50 shows the source 4 texels on
  along u and one of 46 shows it unshifted; without `moveIce` the source slides
  toward +u at `0.9 × (pan − 128)` texels a second; with it, a uniform glass
  sliding changes nothing; and the bake stores an Ice texture's glass as one
  BC4 level of its grey.
  *Test:* `tests/device/RenderLiquidsTest.cpp` and
  `tests/unit/BakeLiquidsTest.cpp`.
  *Breaks when:* the zero point is not 46, the slide runs toward −u, `moveIce`
  is ignored, or the glass is not read.

## 6. Failure modes

- **A liquid whose class default cannot be read** — `Fire.u` is missing or
  does not read. The material keeps its still and no look, and the bake
  names it, and why, in its report's `skippedLiquids`.
- **A liquid with no picture** — a WetTexture whose source is itself
  procedural takes UTA-0177's flat fill. The warp of a flat colour shows
  nothing; on a lit surface the tilt still ripples the light.
- **A WetTexture whose source does not resolve** — the two ArcFluid textures
  of the census. `makeVariant` already skips it; no look is made.
- **A size that is not its source's** — Epic's manual says a WetTexture's
  size should match its source's. The ripple's size follows the WetTexture's
  own `size`, since the shader warps in the coordinates the surface maps.
- **The clock wraps** every `FLAME_CLOCK_WRAP` seconds (`Frame.cpp`), and an
  Ice `Linear` pan jumps once there. Accepted; UTA-0263 accepted the same.
- **Many liquid surfaces** — MH-BattleCrypt draws 234 lava surfaces
  (REPORT.md § 4). The cost is per shaded pixel, not per surface, and is
  measured (§ 13).

## 7. Tests

- INV-1 — `tests/unit/BundleMaterialTest.cpp`, extended.
- INV-2, INV-3 — `tests/unit/BakeLiquidsTest.cpp`, new.
- INV-4 — `tests/real/RealLiquidsTest.cpp`, new, `-DUTA_REAL_ASSET_TESTS=ON`
  only.
- INV-5, INV-6, INV-7 — `tests/device/RenderLiquidsTest.cpp`, new, label
  `device`, run on lavapipe and on the GPU with synchronization validation.

Each new test is seen failing before its code exists. § 4.6's fits are
recorded beside their constants, not asserted by a test.

## 8. Alternatives considered (and rejected)

- **Replaying UT99's WetTexture and WaveTexture simulation** — the drops and
  wave field run per frame on the CPU and uploaded. Faithful, but it is the
  1999 look, it costs an upload per liquid per frame, and it needs `Drops[]`
  decoded, which `upkg` reads only as raw bytes. Offered to the user and not
  chosen.
- **Both, split by surface size** — the simulation on small surfaces, the
  shader on large. Offered and not chosen; two looks for one texture class.
- **A curated list of which textures are liquids**, as fire has — fire needed
  one because a FireTexture's motion was being replaced by flame. Here each
  class keeps its own kind of motion (§ 3), so a list would only withhold
  motion UT99 showed.
- **A noise or normal-map texture** — one fetch cheaper, but a new asset and
  upload for what two octaves of value noise do in the shader, as UTA-0263
  found.
- **Mapping the settings to motion in the bake** — fewer shader inputs, but
  every re-fit of § 4.6 would bump `BAKER_REVISION` and re-bake every map.
- **Drawing the GlassTexture over the source** — closer to UT99's IceTexture,
  but a second picture per material and a blend nothing else needs; the
  still already shows the source alone (UTA-0155). **The premise moved
  when the item was built:** the fit found the glass is what makes an
  IceTexture churn, which panning cannot imitate. Tracked by UTA-0270.
- **Colour cycling** — the user's idea of 2026-09-14, set aside by the user
  the same day.

## 9. Out of scope

- Reflections and Fresnel — UTA-0089. Depth tint — UTA-0273.
- Moving liquid geometry — UTA-0055.
- Caustics and the underwater view — UTA-0215.
- Surfaces that pan by their own flags (`PF_AutoUPan`, `PF_AutoVPan`): no
  part of `urender` reads them, and they pan any texture, not only liquids.
  Tracked by UTA-0269.
- Liquid textures on actors: `urender` draws no actors yet. Deferred; not yet
  queued.
- Decoding `Drops[]` — not needed by a shader look.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleMaterialTest.cpp` |
| INV-2, INV-3 | `tests/unit/BakeLiquidsTest.cpp` |
| INV-4 | `tests/real/RealLiquidsTest.cpp` — real-asset tier only, so the gate on a machine without the install does not run it |
| INV-5, INV-6, INV-7 | `tests/device/RenderLiquidsTest.cpp` |
| INV-8 | `tests/device/RenderLiquidsTest.cpp`, `tests/unit/BakeLiquidsTest.cpp` |
| Circular, Gestation and Wavy panning applied to a moving glass | **nothing** — only Linear was captured |
| § 4.6's fitted constants | **nothing** — a look fit is recorded, not asserted |
| `shapeOf` samples the liquid look | **Partial:** a stale upload shows only on a re-bake in a running viewer; no test re-bakes under a live renderer |
| Wave's highlight and Circular, Gestation and Wavy panning | **nothing** — no device test isolates them; § 4.6's fit is the only check |

## 11. Cross-doc impact

- `docs/specs/UTA-0011-map-baker.md` — `MATS` gains the liquid look.
- `docs/specs/UTA-0014-vulkan-draw-path.md` — a liquid material's coordinate
  offset and tilt, and the `LIQUIDS` binding.
- `docs/specs/UTA-0040-parallax-occlusion.md` — parallax is skipped on a
  liquid material.
- `docs/specs/UTA-0009-material-from-texture.md` — § 6's still for a
  procedural texture now moves for these three classes; its § 9 deferral is
  closed.
- `CLAUDE.md` § Standing facts — the new format and baker revision.
- `CHANGELOG.md` — one entry.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0105-shader-liquids-loop-log.md`.

## 13. Resource cost

- **Bundle:** 109 bytes a liquid look: the kind byte and 108 of fields.
- **GPU:** the looks as one storage buffer; 4 more bytes a material; no new
  texture.
- **Frame time:** measured with `ut-bench frame` at `--tier ultra --size
  3840x2160` on DM-ArcaneTemple and on MH-BattleCrypt, before and after, turn
  and turn about. Recorded on the roadmap item. No budget is set in advance.

## 14. Migration / compatibility

`FORMAT_VERSION` becomes `16`. `ubundle::read` refuses any other version, so
every map baked before this item must be baked again. `BAKER_REVISION` becomes
`31`, so the bake cache re-bakes on its own.
