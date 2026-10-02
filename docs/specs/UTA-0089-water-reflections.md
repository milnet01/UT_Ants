<!-- ants-spec-format: 1 -->
# UTA-0089 — water reflections and less visible tiling

**Status:** implemented (2026-10-02); accepted the same day, unreviewed. Contract reviews are cancelled for this
project (`CLAUDE.md` § Contract reviews); no review runs unless the user asks.
**Kind:** feature.
**Source:** ROADMAP UTA-0089 (user-request-2026-09-10; the lake-look and
anti-repeat request, user 2026-10-02; the water-only scope, user 2026-10-02).
**Blocked by:** UTA-0105, shipped.
**Pairs with:** UTA-0272 (glass) and UTA-0273 (refraction, depth tint and the
shore fade), both split from this item by the user on 2026-10-02.

**Layman:** water shows a faint reflection of the sky or the room around it,
stronger when you look across it than straight down, as a real lake does, and
the picture on a big pool no longer repeats in an obvious grid.

## 1. Goal

A WetTexture or WaveTexture surface reflects its surroundings. The reflection
comes from the sky where the water's zone or the viewer's zone can see the
sky, and from the light probes everywhere else. How much it reflects follows
Fresnel's law for water, so it is faint looking straight down and strong at a
grazing angle. A WetTexture's picture no longer repeats in an exact grid: two
copies of it are blended by slow noise, and from far off the picture fades
toward its own mean colour. All of it is done in the shader. No map is baked
again.

## 2. Problem

1. The user, 2026-10-02, after seeing UTA-0105's moving water: *"Any way we
   can make the texture not look so repeated. Sort of make it look like real
   water a real lake would look like."* And earlier the same day: *"I want
   water to look like real water."* The ask of 2026-09-10: *"please use
   modernised graphics techniques to show waters / liquids. But please do it
   as cheaply as possible."*
2. Water reflects nothing today. `scene.frag`'s `main()` draws a liquid with
   the same lit or unlit path as any surface. The only sky sample is
   `skyAt`, for a `PF_FAKE_BACKDROP` surface.
3. The picture repeats exactly. `scene.frag` samples `material.base` once at
   `shadingUv`, and `liquid.glsl`'s `liquidAt` shifts it by at most
   `LIQUID_WARP_TEXELS` texels, so every repeat of a 256-texel picture shows
   the same detail in the same place.
4. Nothing tells the renderer whether a place can see the sky. A probe holds
   light bounced from the level's own lights (UTA-0112 § 1). The occlusion
   value measures how enclosed the space just above a surface is (UTA-0164
   § 1), not whether sky is above it. `gpu::Zone` carries a zone's ambient
   bytes and a `reserved` word.
5. A real water surface reflects about 2% of light looking straight down and
   all of it at a grazing angle. Schlick's approximation gives
   `R(θ) = R0 + (1 − R0)(1 − cos θ)^5`, with `R0 = ((n1 − n2)/(n1 + n2))²`,
   which is about 0.02 for air and water (n = 1.333). Source:
   https://en.wikipedia.org/wiki/Schlick%27s_approximation
6. Measured during UTA-0105's fit (2026-10-02, DM-ArcaneTemple central pool,
   recorded on the roadmap item): the pool's mean displayed luma is 81 here
   against 111 in the original, with the ripple on or off.

## 3. Scope decisions (agreed with the user)

- **Water only, and the cheap part only** — the user, 2026-10-02. Glass is
  UTA-0272. Refraction, depth tint and the shore fade are UTA-0273.
- **Cheapest method that still looks modern, first iteration** — the user,
  2026-09-14.
- **Looks are decided by measurement, never by asking the user** — the
  user's standing instruction. § 4.6 is where that happens.
- **Faked reflections are acceptable, and cheap is preferred** — the user,
  2026-09-10.
- **Which surfaces count as water: a WetTexture or a WaveTexture** — this
  spec's call. UTA-0105 already tags them, so no bake change is needed. Lava
  and slime reflect too; both are wet, and 2% straight down barely shows on
  an emissive surface. An IceTexture does not reflect: most are lasers and
  plasma (UTA-0105 § 2 item 5).
- **The sky test is per zone, worked out at load** — this spec's call. A zone
  sees the sky when one of its surfaces is a `PF_FAKE_BACKDROP` window, since
  that is how a UT99 level shows its sky.
- **The brightness gap of § 2 item 6 is reported, not fitted** — this spec's
  call. Its cause is not known, and a constant fitted to hide an unknown
  cause encodes it.

## 4. Design

### 4.1 Which zones see the sky — `urender`

```cpp
/// One flag per zone, 1 where a PF_FAKE_BACKDROP triangle of `geometry` has
/// a vertex in that zone -- UTA-0089 SS 4.1. Sized `zoneCount`; a vertex zone
/// at or past it is ignored.
[[nodiscard]] std::vector<std::uint8_t> zonesSeeingSky(const ubundle::Geometry& geometry,
                                                       std::size_t zoneCount);
```

It lives in `src/urender/Zones.h` and `Zones.cpp`, new, so a unit test can
call it without a device. `Renderer::Impl::upload` calls it once per bundle and
writes each zone's flag into `gpu::Zone`, whose `reserved` word becomes
`sky`. A mover's geometry is not read: a sky window on a mover is not a sky.

`FrameData::reserved1` becomes `cameraZone`, the zone `drawView` already
finds for the camera.

### 4.2 Fresnel and the reflected colour — `water.glsl`

A new `src/urender/shaders/water.glsl` holds the water look. For a fragment of
a Wet or Wave liquid:

- **The normal** is the surface normal tilted by `wet.tilt`, the ripple's
  slope from UTA-0105, on lit and unlit surfaces alike. The normal map is not
  added: on a liquid the ripple is the surface.
- **The view direction** `v` runs from the fragment to `frame.eye`, and the
  reflected direction is `reflect(-v, n)`.
- **The share reflected** is § 2 item 5's formula with `R0 = WATER_R0`,
  0.02, at `cos θ = max(dot(n, v), 0)`. Not fitted: it is water's own value.
- **The reflected colour** is the sky, `skyAt(r)`, when `frame.skyTexture`
  is not `NONE`, `r` points upward, and `zones[zone].sky` or
  `zones[frame.cameraZone].sky` is set. Otherwise it is the probes,
  `indirectAt` at the probe point `scene.frag` already uses, with `r` as the
  normal, scaled as `scene.frag` scales indirect light. Where neither has
  anything, it is black.

### 4.3 Where the reflection goes — `scene.frag`

After the surface's colour is lit or taken unlit, and before emission and
fog:

- an **opaque** liquid: `colour = mix(colour, reflected, share)`;
- a **translucent** liquid: `colour += reflected * share`. The pass blends
  `ONE, ONE_MINUS_SRC_COLOR`, so the added light also hides more of what is
  behind, which is what a reflection does.

Fog then applies as today, so a reflection fades with distance in fog.

### 4.4 Less visible tiling — `water.glsl`

For a Wet liquid only. A Wave has no picture: its colour is noise through its
ramp, which does not repeat.

**Two copies.** The base picture is sampled twice, at `shadingUv` plus two
offsets, and blended. Which two offsets, and the blend between them, come from
`flameNoise` at a scale of `TILE_VARIATION_REPEATS` repeats a cell, as in
Quilez's third technique, with his noise texture replaced by the noise
UTA-0263 and UTA-0105 already use. Both samples use the undisplaced
derivatives, so filtering is unchanged. Source:
https://iquilezles.org/articles/texturerepetition/

**The fade.** Where the picture is drawn small, its repeats line up across
the screen. The level of detail the sampler picks, `textureQueryLod`, says how
small. From `TILE_FADE_START` levels to `TILE_FADE_END` levels, the picture
fades toward its mean colour, read from its smallest mip level. Past the end
it is the mean colour, with the reflection and the ripple's light still on
it.

A masked liquid takes its alpha from the blend, so its holes move with the
picture.

### 4.5 Tiers

The look runs on every tier with the same shader. Its cost is measured
(§ 13). If it adds more than 3% to the median frame time of § 13's views at
ultra, it goes behind a new `Feature::WaterLook` in `Tiers.h` at
`Tier::Medium`, and below that a liquid draws as UTA-0105 left it.

**Measured 2026-10-02: 3.0% to 5.6%, so it is behind `Feature::WaterLook`.**
`scene.frag` reads it as specialization constant 2, `WATER_LOOK`. The figures
are on the roadmap item.

### 4.6 Measured constants

`TILE_VARIATION_REPEATS`, `TILE_FADE_START` and `TILE_FADE_END` are fitted,
not chosen, as UTA-0105 § 4.6 fitted the ripple. Each records its sweep in a
comment beside it.

- **Repeat measure.** A camera straight down over a large WetTexture pool,
  light time pinned, ripple on. Take the water's pixels, shift the image by
  one repeat's width in pixels, and take the correlation of the image with
  its shifted self over the overlap. Lower is less repetition. The fit takes
  the lowest it can get.
- **Up close.** Over the water's pixels in the first repeat below the camera,
  the mean displayed luma stays within 5% of the same view without this item.
  The picture must still read as the picture it was.
- **Motion.** UTA-0105 § 4.6's motion measure on DM-ArcaneTemple stays within
  25% of the original's 3.98.

The candidate pools are UTA-0105 § 4.6's: DM-ArcaneTemple's central pool and
AS-OceanFloor's pond (`hubeffects.pond1`). The pool's brightness against the
original (§ 2 item 6) is measured again and recorded, not fitted (§ 3).

**Not fitted, 2026-10-02.** Both candidate pools are translucent over a floor,
and the repeat measure finds no repeat of the water's picture on them, with
this item or without it. The constants are first values; `water.glsl` records
the measurements beside them. The tiling meets the up-close and motion bounds
against the same build with the tiling off. The reflection itself does not:
it raises the up-close luma 9% and the motion measure to 5.88. Those bounds
were written for the tiling's constants, and the reflection is § 4.2's
physics, so it is reported rather than fitted (§ 3).

### 4.7 What does not change

No bake changes, and no map is baked again: `FORMAT_VERSION` and
`BAKER_REVISION` stay. Geometry does not move. An IceTexture and a plain
material draw as before. The translucent pass keeps its blend, attachments
and order.

## 5. Invariants

- **INV-1** — a liquid reflects more at a grazing angle than straight down:
  an unlit Wet square under probes whose colour differs from its picture
  moves further toward the probes' colour viewed at 80° from its normal than
  viewed along it.
  *Test:* `tests/device/RenderWaterTest.cpp`, new, a flat picture so only the
  reflection can differ between views.
  *Breaks when:* the share ignores the angle, or the reflection is not added.

- **INV-2** — only Wet and Wave liquids reflect: changing the probes' colour
  changes an unlit Wet square and an unlit Wave square, and does not change an
  unlit Ice square or an unlit plain square.
  *Test:* `tests/device/RenderWaterTest.cpp`. Unlit, so the probes reach the
  colour only through a reflection.
  *Breaks when:* an Ice or plain material reflects, or a Wave does not.

- **INV-3** — `zonesSeeingSky` sets a zone's flag when a `PF_FAKE_BACKDROP`
  triangle has a vertex in it, and no other.
  *Test:* `tests/unit/ZonesSeeingSkyTest.cpp`, new: three zones, a backdrop
  triangle in zone 1, a plain triangle in zone 2, and a backdrop triangle with
  a vertex zone at `zoneCount`. Expected `{0, 1, 0}`.
  *Breaks when:* the flag is read from the wrong bit, from every triangle, or
  an out-of-range zone is written.

- **INV-4** — the picture does not repeat exactly: on an unlit Wet square
  with `amplitude` 0, so UTA-0105's warp is still, two blocks one repeat apart
  differ; the same picture as a plain material gives identical blocks.
  *Test:* `tests/device/RenderWaterTest.cpp`, a square spanning many repeats,
  a picture with detail in every texel.
  *Breaks when:* both copies take the same offset everywhere. Dropping the
  second copy does not break it: the one left still moves with the noise, and
  shows as hard seams rather than as a repeat.

- **INV-5** — far off, the picture fades to its mean: a black-and-white
  checker on an unlit Wet square, drawn small enough that the sampler picks
  a level past `TILE_FADE_END`, has less contrast than the same checker on a
  plain material at the same size.
  *Test:* `tests/device/RenderWaterTest.cpp`. The checker's squares span
  more texels than that level halves away, so the plain square keeps its
  contrast there; a fine checker greys both squares by mip filtering alone
  and tests nothing. The plain square isolates the fade from that filtering.
  *Breaks when:* the fade does not run, or reads the wrong level.

- **INV-6** — a liquid draws the same twice at a pinned light time.
  *Test:* `tests/device/RenderWaterTest.cpp`, the INV-4 square drawn twice.
  *Breaks when:* the variation noise reads the wall clock or a varying
  input.

## 6. Failure modes

- **A water surface whose vertices carry its underwater zone.** That zone has
  no sky window, so § 4.2 also reads the camera's zone. A viewer indoors
  looking out at an outdoor lake still gets probes rather than sky. Accepted
  for the first iteration.
- **A level with no sky, or the frame that captures the sky faces.**
  `frame.skyTexture` is `NONE`, and the reflection takes the probes.
- **A place with no probes.** `indirectAt` returns black, so the reflection
  adds nothing on a translucent liquid and darkens an opaque one by up to the
  share. At the 2% straight-down value that is small; at a grazing angle it
  shows as a dark sheen. Accepted: it marks a probe gap rather than hiding
  one.
- **The normal map repeats while the picture does not.** § 4.4 varies the
  picture only. On a lit liquid the ripple's tilt dominates the light; a
  visible repeat in the light is a reason to vary the normal map too, at two
  more samples.
- **Lava and slime reflect.** § 3 accepts it.

## 7. Tests

- INV-1, INV-2, INV-4, INV-5, INV-6 — `tests/device/RenderWaterTest.cpp`,
  new, label `device`, run on lavapipe and on the GPU with synchronization
  validation.
- INV-3 — `tests/unit/ZonesSeeingSkyTest.cpp`, new, label `unit`.

Each new test is seen failing before its code exists. § 4.6's fits are
recorded beside their constants, not asserted by a test.

## 8. Alternatives considered (and rejected)

- **A reflection image captured per room at bake time** — the roadmap's first
  idea. Sharper indoors, but a new bundle section, a capture pass per room,
  and a re-bake of every map, where the probes already hold each room's
  colour.
- **Screen-space reflections** — UTA-0045, deferred by the user on
  2026-09-08 as the expensive route.
- **Sky everywhere** — no load-time work, but an indoor pool would show the
  sky.
- **Sky by openness** — `frame.occlusionTexture` measures nearby enclosure,
  not sky (§ 2 item 4), so an open hall would show the sky.
- **Quilez's first or second technique** — four or nine samples instead of
  two, for a look the measure of § 4.6 can grade at two.
- **A larger second scale of the same picture** — one more sample, but the
  larger copy repeats too, at its own scale.
- **Fitting a brightness constant to match the original** — § 3.

## 9. Out of scope

- Glass — UTA-0272.
- Refraction, depth tint and the shore fade — UTA-0273.
- Screen-space reflections — UTA-0045.
- The underwater view — UTA-0215.
- The water's brightness against the original (§ 2 item 6), and the white
  patch at the pool's far edge noted on the roadmap item: measured and
  reported here, not fixed. Deferred; not yet queued.
- Tiling of every other texture — UTA-0180.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2, INV-4, INV-5, INV-6 | `tests/device/RenderWaterTest.cpp` |
| INV-3 | `tests/unit/ZonesSeeingSkyTest.cpp` |
| § 4.5's tier: no water look below Medium | `tests/device/RenderWaterTest.cpp`, and `tests/unit/RenderTiersTest.cpp` for `minimumTier` |
| § 4.2's sky branch: sky where a zone sees it, probes elsewhere | **Partial:** INV-3 checks the flags; no device test draws a sky, so the shader's choice is unchecked |
| § 4.3's translucent `+=` | **nothing** — every square of INV-1, INV-2, INV-4 and INV-5 is opaque, so a reflection missing from the translucent pass draws no failure |
| § 4.6's fitted constants | **nothing** — a look fit is recorded, not asserted |
| § 4.5's 3% budget | **nothing** — measured once with `ut-bench frame` and recorded on the roadmap item |

## 11. Cross-doc impact

- `docs/design.md` — `urender`'s row gains faked reflections on water.
- `docs/specs/UTA-0014-vulkan-draw-path.md` — the translucent pass's
  surfaces may now carry a reflection.
- `docs/specs/UTA-0105-shader-liquids.md` § 9 — its reflections line points
  here.
- `docs/specs/UTA-0156-zone-ambient-light.md` — `gpu::Zone`'s reserved word
  becomes `sky`.
- `CHANGELOG.md` — one entry.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0089-water-reflections-loop-log.md`.

## 13. Resource cost

- **Bundle:** nothing.
- **GPU:** none new. Four bytes a zone and four in `FrameData` reuse reserved
  words.
- **Shader:** one more base sample on a Wet liquid, and one sky or probe
  lookup on a Wet or Wave liquid, per shaded pixel.
- **Frame time:** measured with `ut-bench frame` at `--tier ultra --size
  3840x2160` on DM-ArcaneTemple's pool view and on MH-BattleCrypt, before and
  after, turn and turn about, against § 4.5's 3%. Recorded on the roadmap
  item.
