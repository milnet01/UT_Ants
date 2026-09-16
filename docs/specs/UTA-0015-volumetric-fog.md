# UTA-0015 — volumetric fog, light shafts and the flashlight

**Status:** shipped (2026-09-15), green on the matrix. No review ran, by the user's standing instruction.
**Kind:** implement.
**Source:** ROADMAP UTA-0015 (design-2026-09-03).
**Pairs with:** UTA-0164 (ambient occlusion, split out of this item).

**Layman:** a light haze fills every map so lamps throw visible beams, the
fog an author put in a map glows where they put it, and a key turns on a torch.

## 1. Goal

Every map draws a light, even haze that lights scatter through, so a lamp
throws beams that walls and pillars cut. A light the author made volumetric
glows and thickens the air around it whenever the camera and the light are in
a fog zone, as in UT99. The client's camera carries a flashlight that one key
toggles.

## 2. Problem

1. Nothing in the frame lights the air. `Frame.cpp`'s `recordFrame` draws
   surfaces, their bloom and the output stage, and no pass reads a point that
   is not on a surface.
2. The authored fog is volumetric lights in fog zones, and the bundle carries
   half of it. `ubundle::Light` already holds `volumeBrightness`,
   `volumeRadius` and `volumeFog`, which `ubake/Actors.cpp` reads. `ZoneInfo`'s
   `bFogZone` is in no section (`rg -n fogzone src/` finds nothing).
3. Volume radius is the signal, not volume brightness or fog.
   `ut-ants-uta0156/ambient-census/vol-census` over the reference install
   (output in `vol2.txt`) finds `VolumeBrightness` at `Actor`'s class default
   and `VolumeFog` at zero on nearly every light. A non-zero `VolumeRadius` is
   set on a few hundred maps, most of which also have a fog zone. DM-Deck16][
   sets none and has no fog zone, so without a haze it has no atmosphere.
4. UE1 shows volumetric light only in a fog zone. Lode's UnrealEd lighting
   tutorial (Source: https://lodev.org/unrealed/lighting/lighting.html) says
   the fog is visible only when the camera is in a zone with `bFogZone` set,
   and that the light must be placed in a fog zone. Neither statement was
   checked against engine source.
5. UE1's volumetric light values are known.
   SurrealEngine's `OnMapLoaded` (the copy read is
   `ut-ants-uta0156/surreal/LightSystem.cpp`) builds a fog ball from a light with a non-zero
   `VolumeRadius`: colour from hue, saturation and brightness, a brightness of
   `LightBrightness / 255 × VolumeBrightness / 64`, a density of
   `VolumeFog / 255`, and a radius of `25 × (VolumeRadius + 1)`.
6. UT99's flashlight is not a spotlight. `UnrealShare/Classes/Flashlight.uc`
   in the 469 SDK traces along the view each tick and moves a
   `FlashLightBeam` to 64 units short of the hit. `FlashLightBeam.uc`'s
   defaults are `LE_NonIncidence`, hue `32`, saturation `142`, brightness
   `250`, radius `7`.

## 3. Scope decisions (agreed with the user)

- **Fog source: each map's own fog zones and volumetric lights, plus a light
  even haze on every map** — the user, 2026-09-15. UTA-0113's recipe may tune
  the haze per map later.
- **Scope: volumetric fog, light shafts and the flashlight; ambient occlusion
  becomes UTA-0164** — the user, 2026-09-15.
- **The flashlight is a spotlight on the camera, toggled by a key in
  `ut-ants`** — the user, 2026-09-15. The key is `F`: this spec's call.
- **Cheapest method that still looks modern, first iteration** — the user,
  2026-09-14. So one froxel volume, no temporal reprojection and no jitter.
- **How it looks is decided by measurement, never by asking the user** — the
  user's standing instruction. § 7 is where.
- **No ray tracing** — the roadmap item.

## 4. Design

### 4.1 A zone's fog flag — `ubundle`

`ZoneAmbient` is renamed `Zone`, since it no longer holds only ambient light,
and gains the flag:

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 12; // 12 since UTA-0015

/// One zone's ZoneInfo values (or the LevelInfo's) -- UTA-0156, UTA-0015.
struct Zone {
    std::uint8_t brightness = 0;
    std::uint8_t hue = 0;
    std::uint8_t saturation = 0;
    std::uint8_t fog = 0; ///< 1 where bFogZone is set
};

/// The zone of the room at `location`; 0 outside every room, or where the
/// room's zone is not below `zoneCount`. Moved from ubake, unchanged.
[[nodiscard]] std::uint8_t zoneAt(const umap::RoomMap& rooms, const std::array<float, 3>& location,
                                  std::size_t zoneCount);

}  // namespace uta::ubundle
```

On the wire a `ZONE` entry is UTA-0156 § 4.1's three bytes, then `fog` as one
`u8`. **Validation**, `MalformedData` on `read` and `InvalidArgument` on
`write`: a `fog` byte above `1`.

`zoneAt` moves from `src/ubake/Zones.cpp` to `src/ubundle/ZoneSection.cpp`, so
the renderer can find the camera's zone without linking `ubake`
(`docs/design.md` rule 2).

### 4.2 The bake — `ubake`

`buildZones` resolves `bfogzone` on the same actor as the ambient values, as a
`Bool` through `detail::resolvedRecord`: the actor's own record, else its
class's default, else false. `BAKER_REVISION` becomes `15`.

### 4.3 The fog volume — `urender`

Two 3D images of `FOG_GRID`, `R16G16B16A16_SFLOAT`, storage and sampled, kept
in `GENERAL` layout:

- **scattering**: in-scattered light per unit length in `rgb`, extinction per
  unit length in `a`;
- **integrated**: in-scattered light reaching the eye in `rgb`, transmittance
  in `a`.

```cpp
namespace uta::urender {

inline constexpr std::array<std::uint32_t, 3> FOG_GRID{160, 90, 64};
inline constexpr float FOG_NEAR = 16.0f;   ///< where slice 1 starts
inline constexpr float FOG_FAR = 8192.0f;  ///< where the last slice ends
inline constexpr std::uint32_t VOLUME_LIGHT_CAPACITY = 64;

enum class Feature : std::uint8_t {
    ParallaxOcclusion,
    Bloom,
    VolumetricFog, ///< UTA-0015: minimumTier is Tier::Medium
};

}  // namespace uta::urender
```

All grid values, and `Tier::Medium`, are this spec's call. Below that tier the
two images are one texel each, cleared once to `(0, 0, 0, 1)`, and neither
compute stage is dispatched.

**Slices.** Slice `k` spans view depth `z(k)` to `z(k + 1)`, where `z(0) = 0`
and `z(k) = FOG_NEAR × (FOG_FAR / FOG_NEAR)^(k / FOG_GRID[2])` for `k ≥ 1`. A
froxel's sample point is its tile's centre ray at the slice's middle depth.

**Stage 1, `fog_scatter.comp`**, one invocation per froxel. At its world
point `x`, with `v` the unit direction from `frame.eye` to `x`:

- *Haze.* Extinction `HAZE_EXTINCTION × hazeScale`. For each light of the
  cluster containing `x` that has shadow faces, or is the flashlight:
  `lightThrough(light, x) × light.flicker × shadowOf(light, x) ×
  phase(dot(v, u)) × HAZE_SCATTER × hazeScale`, where `u` is the unit
  direction from `x` to `litFrom(light, x)`, so looking into a light is
  forward scattering.
- *Volumetric lights.* For each index in `VOLUME_LIGHTS`, with
  `f = lightFalloff(distance(litFrom(light, x), x), lightRadius(light.volumeRadius))`:
  in-scattering `lightColour(hue, saturation) × brightness / 255 ×
  volumeBrightness / 64 × f × VOLUME_GLOW_SCALE`, and extinction
  `volumeFog / 255 × f × VOLUME_FOG_SCALE`. Neither is shadowed: § 7 step 2
  measured a shadowed glow drawing nothing on DM-Fetid where the original
  draws its glow.

`lightThrough` is new in `light.glsl`: `lightAt` with the normal pointing at
the light, so the incidence factor is 1 and no part of the model is written
twice. It returns `lightAt`'s value at distance zero when `x` is on the light.
`phase` is Henyey–Greenstein with `FOG_ANISOTROPY = 0.2`, this spec's call.
`clusterOf` moves from `scene.frag` to a new `shaders/clusters.glsl` that both
include.

**Stage 2, `fog_integrate.comp`**, one invocation per froxel column, front to
back. With `σ` the slice's extinction, `S` its in-scattering, and `Δ` its
length along the column's ray:

```glsl
float sliceT = exp(-sigma * delta);
vec3 sliceS = sigma > 0.0 ? (S - S * sliceT) / sigma : S * delta;
scattered += transmittance * sliceS;
transmittance *= sliceT;
// texel k = vec4(scattered, transmittance)
```

This is the energy-conserving form from Hillaire's "Physically Based and
Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015.

**The constants** live in a new `shaders/fog.glsl`, so § 7's sweeps are
shader-only: `HAZE_EXTINCTION`, `HAZE_SCATTER`, `VOLUME_GLOW_SCALE` and
`VOLUME_FOG_SCALE`, each set by § 7.

**Applying it.** `scene.frag` samples `FOG_VOLUME` at
`(gl_FragCoord.xy / frame.viewportSize, s)`, where
`s = log(max(z, FOG_NEAR) / FOG_NEAR) / log(FOG_FAR / FOG_NEAR) − 0.5 / FOG_GRID[2]`
and `z` is the fragment's view depth. Texel `k` holds the integral to slice
`k`'s far edge, which the half-slice offset lines up. With the sample `(S, T)`:
an opaque or masked surface writes `colour × T + S`; a `PF_Translucent`
surface writes `colour × T`, since the surface behind it already carries `S`.
Every surface is fogged, unlit and sky included. `outEmission` is not.

### 4.4 The frame — `urender`

`recordFrame` gains a step between step 0, clustered light culling, and step
1, the forward pass: **the fog volume**, stage 1 then stage 2, when
`VolumetricFog` is on. It reads the cluster lists and the shadow atlas, which
are final by then.

`ShaderTypes.h` and `types.glsl`:

```cpp
enum Binding : std::uint32_t {
    // ... FRAME through SHADOW_ATLAS, unchanged, then:
    FOG_VOLUME = 12, ///< UTA-0015: the integrated image, as a sampler3D
    TEXTURES = 13,   ///< last: it is the variable-count binding
};

struct Light {
    // ... location through shadowFaceCount, unchanged, then:
    std::uint32_t volumeRadius;     ///< 56, was reserved0
    std::uint32_t volumeBrightness; ///< 60, was reserved1
    std::array<float, 3> span;      ///< 64, unchanged
    std::uint32_t volumeFog;        ///< 76, was the float reserved2
};

/// The two fog stages' push constants.
struct FogConstants {               // 96 bytes, offsets asserted
    Mat4 viewToWorld;               // 0
    std::array<float, 2> tanHalfFov; // 64: across, then down, for the region
    std::uint32_t volumeLightCount; // 72
    std::uint32_t flashlight;       // 76: an index into LIGHTS, or NONE
    float hazeScale;                // 80: Config::hazeScale
    std::array<float, 3> reserved;  // 84
};
```

The fog stages bind the scene set as set `0` and a fog set as set `1`: binding
`0` the scattering image, binding `1` the integrated image, binding `2`
`VOLUME_LIGHTS`, a storage buffer of `uint` indices into `LIGHTS` with room
for `VOLUME_LIGHT_CAPACITY`.

**Volumetric lights, chosen on the CPU**, in a new `src/urender/Fog.{h,cpp}`:

```cpp
struct VolumeLightChoice {
    std::vector<std::uint32_t> indices; ///< into `lights`
    std::uint32_t dropped = 0;          ///< qualifying lights past the capacity
};

/// SS 4.4: the drawn lights that glow this frame, nearest `eye` first, at most
/// VOLUME_LIGHT_CAPACITY. None unless `zones[cameraZone].fog`; then each
/// light whose volumeRadius is non-zero and whose zone is a fog zone. A zone
/// index not below zones.size() reads as zone 0.
[[nodiscard]] VolumeLightChoice volumeLights(std::span<const gpu::Light> lights,
                                                      std::span<const std::uint8_t> lightZones,
                                                      std::uint8_t cameraZone,
                                                      std::span<const ubundle::Zone> zones,
                                                      const std::array<float, 3>& eye);
```

`upload` caches each drawn light's zone with `ubundle::zoneAt` at its
`location`; `draw` finds the camera's the same way. With no `ROOM`, both are
`0`. With no `ZONE`, `zones` is one zero entry. Lights dropped past the
capacity are `dropped`, which `draw` reports as
`FrameStats::droppedVolumeLights`.

### 4.5 The flashlight — `urender` and `ut-ants`

```cpp
struct Camera {
    // ... every existing member, unchanged, then:
    bool flashlight = false; ///< UTA-0015: a spotlight from the eye
};

struct Config {
    // ... every existing member, unchanged, then:
    /// UTA-0015: scales the haze's extinction and scattering. For tests and
    /// diagnosis; 1 in normal play. `create` refuses a negative or
    /// non-finite value with InvalidArgument.
    float hazeScale = 1;
};

struct FrameStats {
    // ... every existing member, unchanged, then:
    std::uint32_t droppedVolumeLights = 0; ///< UTA-0015: past VOLUME_LIGHT_CAPACITY
};
```

`Fog.h` declares `[[nodiscard]] gpu::Light flashlightOf(const Camera& camera)`:
`location` the camera's, `pitch` and `yaw` its rotation's, `effect`
`LE_Spotlight` (`12`), hue `32`, saturation `142` and brightness `250` from
`FlashLightBeam`, radius `255`, cone `18`, `flicker` `1`, no shadow faces, and
every volume field zero.

- **Radius `255`**, the farthest a byte reaches: UT99's trace runs 10000 units.
- **Cone `18`**: UT99's beam lights a sphere of `25 × (7 + 1) = 200` units. At
  500 units that spans a half-angle of `atan(200 / 500)`, whose cosine is
  `0.9285`, and `256 × (1 − 0.9285)` rounds to `18`. The 500-unit reference
  distance is this spec's call.

`draw` appends it after the shadow plan when `camera.flashlight` is set, so it
is the last entry of `LIGHTS` and joins the cluster lists. `upload` sizes
`LIGHTS` for one more light than `drawnLights` returns.

`apps/ut-ants/main.cpp` toggles the flag on each `F` key-down that is not a
repeat. `Cli.cpp`'s help text and `README.md` name the key.

## 5. Invariants

- **INV-1** — a `ZONE` entry's `fog` byte round-trips, and `read` and `write`
  both refuse a `fog` byte of `2`.
  *Test:* `tests/unit/BundleZonesTest.cpp`, extended.
  *Breaks when:* `fog` is not written; a value above `1` is accepted.

- **INV-2** — `buildZones` gives `fog` `1` to a zone whose `ZoneInfo` sets
  `bFogZone` itself, `1` to one whose class default sets it, and the
  `LevelInfo`'s value to a zone with no actor.
  *Test:* `tests/unit/BakeZonesTest.cpp`, extended.
  *Breaks when:* class defaults are ignored; the flag is read from the wrong
  actor.

- **INV-3** — `volumeLights` returns nothing when the camera's zone is not a
  fog zone. In a fog zone it returns the lights with a non-zero `volumeRadius`
  in fog zones, nearest `eye` first, and at most `VOLUME_LIGHT_CAPACITY` of
  them. A light with `volumeRadius` `0`, or in a zone without fog, is left out.
  A zone index past `zones` reads as zone 0.
  *Test:* `tests/unit/RenderVolumeLightsTest.cpp`, new.
  *Breaks when:* the camera's zone is not checked; the light's zone is not
  checked; the capacity keeps the furthest lights.

- **INV-4** — `flashlightOf` gives § 4.5's fields. A frame with no lights,
  probes or ambient, and `Camera::flashlight` set, draws a lit white wall
  ahead non-zero; the same frame with it unset draws zero.
  *Test:* `tests/unit/RenderVolumeLightsTest.cpp` and
  `tests/device/RenderFogTest.cpp`, new.
  *Breaks when:* the flashlight is not uploaded or not counted in
  `lightCount`; its direction ignores the camera's rotation.

- **INV-5** — at `Tier::Medium` with no lights and `hazeScale` `H`, an unlit
  white wall at distance `D` straight ahead draws, under `linearOutput`,
  within `0.02` of `exp(−HAZE_EXTINCTION × H × D)`. At `Tier::Low` it draws
  `1`. `create` refuses a `hazeScale` of `−1`.
  *Test:* `tests/device/RenderFogTest.cpp`, new.
  *Breaks when:* the integration is not front to back; slices are placed
  wrongly, so the transmittance is read at the wrong depth; `Tier::Low`
  samples a volume it never drew.

- **INV-6** — at `Tier::Medium` and `hazeScale` `0`, a light with a non-zero
  `volumeRadius` whose sphere holds the view ray: with `volumeFog` `255` and
  `volumeBrightness` `0`, an unlit white wall behind it draws darker in a fog
  zone than out of one, by at least 20 levels; with `volumeFog` `0` and
  `volumeBrightness` `255`, an unlit black wall draws above 20 levels in a fog
  zone and `0` out of one. The glow is unshadowed: a light behind a wall
  that fills the view still lights the air in front of it.
  *Test:* `tests/device/RenderFogTest.cpp`, new.
  *Breaks when:* the fog flag is ignored; `VOLUME_LIGHTS` is not bound or its
  count is not pushed; volume extinction is not applied; the glow reads the
  light's shadow.

- **INV-7** — at `Tier::Medium` with a large `hazeScale`, a black unlit
  backdrop: with `RenderShadowTest.cpp`'s occluder and shadowed point light, a
  pixel whose view ray crosses the occluder's shadow draws darker than one
  whose ray passes the light at the same distance unshadowed. With no lights
  and the flashlight on, the backdrop draws above zero; with it off, zero.
  *Test:* `tests/device/RenderFogTest.cpp`, new.
  *Breaks when:* in-scattering skips `shadowOf`; the flashlight is not let
  through the shadow-faces gate.

- **INV-8** — `gpu::FogConstants`' offsets, `gpu::Light`'s renamed fields and
  the `Binding` numbers match `types.glsl` and `scene_bindings.glsl`.
  *Test:* the `static_assert`s in `src/urender/ShaderTypes.h`.
  *Breaks when:* a field or binding moves in one file only.

- **INV-9** — `minimumTier(Feature::VolumetricFog)` is `Tier::Medium`.
  *Test:* `tests/unit/RenderTiersTest.cpp`, extended.
  *Breaks when:* the row is missing or names another tier.

- **INV-10** — the golden bake is re-recorded under `BAKER_REVISION` `15`.
  *Test:* `tests/unit/BakeGoldenTest.cpp`.
  *Breaks when:* what the baker writes changes with no bump, or the bump lands
  without re-recording.

## 6. Failure modes

- **Banding.** Sixty-four slices with no jitter step visibly on a strong beam.
  Accepted for the first iteration; temporal reprojection is out of scope.
- **Halos at edges.** A froxel is coarser than a pixel, so a lit froxel
  straddling a wall's silhouette leaks a thin glow onto it.
- **A light with no shadow faces throws no beam.** It lights surfaces as
  before. Lights the atlas could not hold are already counted in
  `FrameStats::unshadowedLights`. Scattering them unshadowed would shine
  beams through walls.
- **A volumetric light glows through walls**, as UT99's does, since its glow
  is unshadowed.
- **A cluster that overflowed** drops the same lights from the beams as from
  the surfaces.
- **Past `FOG_FAR`** the fog stops thickening: a fragment beyond it samples
  the last slice.
- **Past `VOLUME_LIGHT_CAPACITY`** the furthest volumetric lights do not glow,
  and `FrameStats::droppedVolumeLights` says how many.
- **A camera outside every room** is in zone 0.
- **A `specialLit` or backdrop light** is not drawn, so it does not glow.
- **Bloom is not fogged**, so an emissive surface's glow shows through fog.
- **The tutorial's zone rule is wrong.** § 7's capture shows a fog ball or
  shows none, which settles it.
- **A bundle from before this item** is refused by its format version (§ 14).

## 7. Tests

The `unit` label on the unit tests, `device` on the device tests.

- INV-1 — `tests/unit/BundleZonesTest.cpp`, extended.
- INV-2 — `tests/unit/BakeZonesTest.cpp`, extended.
- INV-3 — `tests/unit/RenderVolumeLightsTest.cpp`, new.
- INV-4 — `tests/unit/RenderVolumeLightsTest.cpp` and
  `tests/device/RenderFogTest.cpp`, new.
- INV-5 — `tests/device/RenderFogTest.cpp`, new.
- INV-6 — `tests/device/RenderFogTest.cpp`, new.
- INV-7 — `tests/device/RenderFogTest.cpp`, new.
- INV-8 — `src/urender/ShaderTypes.h`'s `static_assert`s.
- INV-9 — `tests/unit/RenderTiersTest.cpp`, extended.
- INV-10 — `tests/unit/BakeGoldenTest.cpp`, re-recorded.

Each is seen to fail against the code before this item.

**Measurement, not asserted.** After the code lands, with
`ut-ants-uta0156/compare.py`'s block RMS:

1. **Haze.** `HAZE_EXTINCTION` is `−ln(0.9) / FOG_FAR`: a tenth of the light
   is lost over the fog's whole depth, this spec's reading of "light". Sweep
   `HAZE_SCATTER` on DM-Deck16][, and keep the largest value whose block RMS
   is within `1.0` of the same build's at `hazeScale` `0`.
   *Result (2026-09-15):* `HAZE_SCATTER` was `4e-3`, the largest swept value
   within budget. *Refitted (2026-09-16):* UTA-0165's light model moved
   `EXPOSURE` to `2.2`, and the haze with it, to `6e-3`. The scores are beside
   the constant in `shaders/fog.glsl`.
   *Refitted again (2026-09-16), and the rule above no longer holds:* UTA-0166
   gave every light a shadow tile, so about fourteen times as many lights
   scatter into the haze. Haze now IMPROVES the match rather than costing
   against it -- `4e-4` scores `40.5` where no haze scores `45.7` -- so "within
   `1.0` of no haze" selects nothing useful. The rule is now the plain minimum,
   taking the largest of the values tied within the control's own `0.1` of
   repeat-to-repeat spread. `6e-3` under the new shadow rule scores `105.4`.
2. **Volumetric lights.** A scratch probe beside `ambient-census/` lists the
   stock maps' `PlayerStart`s that are in a fog zone and within a volumetric
   light's volume radius plus 1000 units. Capture one such map with
   `capture-original.sh`, with the capture copy's renderer set to draw
   volumetric lighting. Confirm the setting took: the same poses captured
   with it off must differ in block RMS. Sweep `VOLUME_GLOW_SCALE` and
   `VOLUME_FOG_SCALE` for the lowest block RMS on those poses.
   *Result (2026-09-15):* DM-Fetid, whose every PlayerStart the probe found
   in a fog zone within reach of its one volumetric light. The capture with
   volumetric lighting on differs from the one with it off, so the setting
   took. A shadowed glow drew nothing, so the glow is unshadowed (§ 4.3).
   `VOLUME_GLOW_SCALE` was `3e-3` and `VOLUME_FOG_SCALE` `3.2e-2`.
   *Refitted (2026-09-16):* at `EXPOSURE` `2.2` they are `4e-3` and `6.4e-2`,
   the lowest of the grid and the nearest to the original's mean brightness.
   The scores are beside the constants in `shaders/fog.glsl`.
   *Refitted again (2026-09-16):* on top of UTA-0166's haze they are `5e-3` and
   `1.28e-1`. Both are measured minima rather than grid edges: the earlier fog
   value sat at the widest swept and was still gaining, and swept wider it
   turns at `1.28e-1`.
3. **Nothing else moves.** DM-Deck16][ at `hazeScale` `0` keeps the block RMS
   it had before this item.
   *Result (2026-09-15):* `46.2` at exposure 3.2, as before.
   *Refitted (2026-09-16):* `45.8` at exposure 2.2, matching the same build
   with no haze, so the fog still moves nothing on a map that draws none.
   *Re-checked under UTA-0166 (2026-09-16):* `45.8` again, so giving every light
   a shadow tile costs this map nothing where it draws no haze. That is the
   control for the whole amendment: the picture changed only where fog is drawn.

## 8. Alternatives considered (and rejected)

- **Analytic distance or height fog only.** Lost: it throws no beams, and the
  user chose beams.
- **Ray marching every pixel.** Lost: a march per pixel, where froxels share
  one march across a tile.
- **Fog applied afterwards from the depth buffer.** Lost: a translucent
  surface would be fogged at the depth of what is behind it.
- **Clustering lights by their volume radius as well.** Lost: glow-only reach
  would crowd every surface's light list toward overflow.
- **Scattering every light, shadowed or not.** Lost: § 6's beams through
  walls.
- **Temporal reprojection with jitter.** Deferred: it needs history volumes,
  and the first iteration takes the cheapest method.
- **Growing `gpu::Light` for the volume fields.** Lost: its three reserved
  fields hold them.
- **A flashlight shadow.** Lost: a light at the eye casts its shadows behind
  what the eye sees.

## 9. Out of scope

- Ambient occlusion — tracked by UTA-0164.
- Per-map haze from a recipe — tracked by UTA-0113.
- Fog on meshes — tracked by UTA-0159.
- `ZoneInfo`'s `FogColor` and `FogDistance` distance fog — deferred; not yet
  queued. The census read `FogDistance` only on fog zones, and each was zero.
- Temporal reprojection of the fog volume — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleZonesTest.cpp` |
| INV-2 | `tests/unit/BakeZonesTest.cpp` |
| INV-3 | `tests/unit/RenderVolumeLightsTest.cpp` |
| INV-4 | `tests/unit/RenderVolumeLightsTest.cpp`, `tests/device/RenderFogTest.cpp` |
| INV-5 | `tests/device/RenderFogTest.cpp` |
| INV-6 | `tests/device/RenderFogTest.cpp` |
| INV-7 | `tests/device/RenderFogTest.cpp` |
| INV-8 | `src/urender/ShaderTypes.h`'s `static_assert`s |
| INV-9 | `tests/unit/RenderTiersTest.cpp` |
| INV-10 | `tests/unit/BakeGoldenTest.cpp` |
| The `F` key toggles the flashlight | **nothing** — `main.cpp` is run by hand (UTA-0016) |
| How the fog looks against the original | **nothing** — § 7's measurement is run by hand |

## 11. Cross-doc impact

- `docs/specs/UTA-0156-zone-ambient-light.md` § 4.1 and § 4.3 — the entry
  gains `fog`, the struct is renamed, and `zoneAt` moves to `ubundle`.
- `CLAUDE.md` § Where this project is — the bundle format version.
- `README.md` — the flashlight key.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0015-volumetric-fog-loop-log.md`.

## 13. Resource cost

Two images of `FOG_GRID` at `R16G16B16A16_SFLOAT` at `Tier::Medium` and above,
one texel each below it, and one buffer of `VOLUME_LIGHT_CAPACITY` indices. No
new dependency.

## 14. Migration / compatibility

`FORMAT_VERSION` becomes `12`. `ubundle::read` refuses any other version, so
every map baked before this item must be baked again.
