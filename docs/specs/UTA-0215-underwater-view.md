<!-- ants-spec-format: 1 -->
# UTA-0215 — the underwater view, first part

**Status:** implemented (2026-10-02); § 7's comparison against the original's frames is pending.
**Kind:** feature.
**Source:** ROADMAP UTA-0215 (user request 2026-09-26; restated 2026-10-02).

**Layman:** When the player's head goes under water, the view turns the
water's colour and goes murky with distance, and wobbles gently, the way
the original tinted it and more.

## 1. Goal

With the camera inside a water zone, the frame takes the original's tint,
things fade toward the water's colour with distance, and the view wobbles
gently. Out of water nothing changes. Surfaces in water carry moving
caustics (§ 4.5), and the surface seen from below shows as it does from
above (§ 4.6), both added after the first part. Light shafts fall through
the water (§ 4.7). Drifting particles follow (§ 9).

## 2. Problem

1. **No zone says it is water.** `ubundle::Zone` (`src/ubundle/Bundle.h`)
   holds ambient light, the fog flag and the pan speeds; `bWaterZone` is in
   no section, so nothing knows the camera is under water.
2. **The original's tint is not drawn.** UT99's `PlayerPawn.ViewFlash`
   (Engine.u's UnrealScript, read 2026-10-02) sets the screen's scale to
   `1 + DesiredFlashScale + ConstantGlowScale + HeadRegion.Zone.ViewFlash.X`
   and its fog to `DesiredFlashFog + ConstantGlowFog + HeadRegion.Zone.ViewFog`,
   easing both at 10 a second. A zone's `ViewFog` and `ViewFlash` are its
   whole underwater colour, and the compiled defaults carry them
   (`~/.cache/uta-scratch/u269/water_defaults.py`): WaterZone `ViewFog`
   (0.1289, 0.1953, 0.1758) and `ViewFlash` X −0.078; SlimeZone, LavaZone,
   NitrogenZone and TarZone each their own; ZoneInfo none.
3. **The user's bar is more than a tint.** *"When under water, it needs to
   look like you [are] underwater"* (2026-09-26), restated 2026-10-02 after
   UTA-0089. The roadmap body lists colour absorption with distance and a
   gentle wobble first among the cheap parts.

## 3. Scope decisions (agreed with the user)

- **Cheapest methods that still look modern, first** — the user, 2026-09-14.
- **The underwater view is the whole impression, not a colour filter** —
  the user, 2026-09-26; taken up after UTA-0089 (2026-10-02).
- **Which zones are water is read from the map, not guessed** — the
  roadmap body.
- **Judged by reference captures and numbers, never by the user's eye** —
  the standing rule.

## 4. Design

### 4.1 The zone's water data — `ubundle` and `ubake`

```cpp
struct Zone {
    // ... brightness, hue, saturation, fog, panSpeed, unchanged, then:
    std::uint8_t water = 0;            ///< UTA-0215: 1 where bWaterZone is set
    std::array<float, 3> viewFog{};    ///< UTA-0215: ViewFog, display units
    float viewFlash = 0;               ///< UTA-0215: ViewFlash.X
};
```

On the wire, after `panSpeed`: `water` as `u8`, then `viewFog` and
`viewFlash` as four `f32`; an entry is 29 bytes. `FORMAT_VERSION` 19.
Validation: a `water` byte above `1`, or a speed, fog or flash that is not
finite, is refused both ways. `buildZones` resolves `bwaterzone` (`Bool`),
`viewfog` (`Vector`) and `viewflash` (`Vector`, its X) as it resolves
`bfogzone`: the actor's record, else its class's default, else `0`.
`BAKER_REVISION` moves.

### 4.2 The original's tint — `urender`, the output stage

With the camera's zone water, `post.frag` writes, per channel, from the
display value `d` it would have written:

```text
d' = clamp(d × (1 + viewFlash) + viewFog, 0, 1)
```

`d` is the sRGB-encoded value; the shader writes `d'`'s linear light, which
the `_SRGB` target encodes back to `d'`. It applies under
`Config::linearOutput` too, since it is a view effect and not part of the
light model. It is instant where UT99 eases at 10 a second: a still capture
then shows the settled tint, and easing is § 9's.

`PostConstants` gains `flashScale`, `wobbleSeconds` and `flashFog`, which
`draw` sets from the camera's zone: `1 + viewFlash` and `viewFog` when it is
water, else `1` and `0`. `flashFog`'s fourth part is the water flag.

### 4.3 Fading with distance — `urender`, `scene.frag`

**Amended 2026-10-02, after § 7's comparison.** First built as a fade to
black, which drew the pool at about half the original's brightness. The
original's farthest pixels under water are one flat colour, (67, 98, 90),
and 1.922 × `ViewFog`, the tint applied to a scene showing `ViewFog` itself,
is (63, 96, 86). So the original fades far things toward the water's colour
and then tints. What was built:

With the camera's zone water, a fragment of a surface whose own zone is
water keeps all its colour out to `WATER_FOG_START`, none past
`WATER_FOG_END`, and a straight line between, `z` its view depth; the rest
becomes the light that the output stage shows as `ViewFog`. It is applied
before the fog adds its light. A translucent surface keeps only its share, as
with the fog, and a flame is dimmed alike. A surface out of the water, seen
through its surface, is not faded: the original shows the sky above its pool
unfaded. `FrameData` carries `cameraUnderwater` and that light as
`waterFog`, the sRGB decode of `ViewFog` divided by the exposure, which the
tone map leaves alone at these levels.

```glsl
// fog.glsl, which scene.frag and flame.frag both include
const float WATER_FOG_START = 300.0; // UT units: all of a surface's light kept
const float WATER_FOG_END = 1000.0;  // none kept
```

**Linear, and chosen by the original's detail.** The original's near
surfaces read as ours unfaded while its far ones are one flat colour, which
an exponential cannot give at once. Two measures on the pool's level and
looking-down frames disagree, because our pool floor is lit darker than the
original's (`UTA-0278`): 16-pixel block RMS favours heavy fog (0 to 300
units scores 11.3, 300 to 1000 scores 19.5), while 8-pixel texture detail,
the original's 2.9 and 2.0, favours light fog (0.1 and 0.3 at 0 to 300; 0.9
and 1.8 at 300 to 1000). At 0 to 300 the floor the original shows looking
down vanished into flat colour; at 300 to 1000 it shows. So the values are
the detail measure's; the record is beside the constants. Under water the
frame now reads 0.74 to 1.17 of the original's brightness, at 1.0e-3 of
§ 4.7's scattering.

### 4.4 The wobble — `urender`, the output stage

With the camera's zone water, `post.frag` samples the frame and its bloom
at a coordinate moved by

```text
WOBBLE_AMPLITUDE × (sin(v × WOBBLE_FREQUENCY + t × WOBBLE_SPEED),
                    cos(u × WOBBLE_FREQUENCY + t × WOBBLE_SPEED))
```

`u`, `v` the output coordinate and `t` the light clock's seconds, wrapped as
flames' are. Constants: amplitude `0.003` of the frame, frequency `24`,
speed `1.5` — this spec's call, chosen to be faint.

### 4.5 Caustics — `urender`, `scene.frag`

**Amended 2026-10-02, recording what was built.** A lit surface whose own
zone is water, and is not a liquid, gains focused light: reflectance times
`causticLight`, two drifting value-noise layers bright where they cross
their middle value. The pattern lies on the axis plane the surface faces
most, so a slope never smears it; walls take half. `water.glsl` holds the
constants: a 64-unit cell, 0.25 cells a second, a mean of 0.03 of the
reflectance. They are this item's call, chosen on DM-ArcaneTemple's pool,
since the original draws none. It is added rather than multiplied, so it
shows in a pool the map leaves dim; there it raised the pool's mean by two
to five bytes. The surface's zone decides, not the camera's: caustics show
from above the water too. `Feature::Caustics`, from Medium. Each zone's
`water` reaches the GPU in its zone record, and the bundle fingerprint
samples it.

### 4.6 The surface seen from below — `scene.frag`

**Amended 2026-10-02, recording what was built.** UT99 lights a liquid's
sheet once and shows it so from both sides; ours lit its back as a surface
facing down, which the lights above never reach, so from below the sheet was
nearly black where the original's is cyan. Now a liquid seen from behind
mirrors its shading normal back to its front, and reads its probes half a
spacing in front of it. Any other two-sided surface is still lit on the side
you see. And from under water a reflection never takes the sky
(`UTA-0089` § 4.3, amended): ripples had tilted the back of the sheet into
bright patches of it. On DM-ArcaneTemple, looking up from the pool, ours went
from 0.57, 0.47 and 0.44 of the original in red, green and blue to 0.57,
0.59 and 0.59.

### 4.7 Light shafts — `urender`, `fog_scatter.comp`

**Amended 2026-10-02, recording what was built.** With the camera's zone
water, the fog pass scatters the same shadowed lights the haze does, at
`WATER_SCATTER` (1.0e-3) a unit times a pattern of two drifting value noises,
cubed, that ignores height: so the light gathers in upright columns, as light
falling through a rippled surface does, and the shadow maps cut them. Each
froxel's scattered light is faded by `WATER_FOG_START`/`END` at its own depth.
The scattering is this item's call, chosen on the pool: 1.6e-2 washed the
view grey and 4e-3 was strong once the fade was linear. From Medium, where
the fog pass runs.

## 5. Invariants

- **INV-1** — `ZONE`'s `water`, `viewFog` and `viewFlash` round-trip through
  their bits; a `water` byte of `2`, and a fog or flash that is not finite,
  is refused both ways.
  *Test:* `tests/unit/BundleZonesTest.cpp`, extended.
  *Breaks when:* a field is not written, or a check is missing.

- **INV-2** — `buildZones` gives a WaterZone class `water` `1` and its
  class's `ViewFog` and `ViewFlash`, an actor's own record winning, and a
  plain ZoneInfo `water` `0` and no tint.
  *Test:* `tests/unit/BakeZonesTest.cpp`, extended.
  *Breaks when:* class defaults are ignored, or the flag is read from the
  wrong actor.

- **INV-3** — the tint: with the camera in a water zone of `viewFog`
  `(0.1, 0.2, 0.3)` and `viewFlash` `−0.2`, an unlit grey wall's displayed
  value is `d × 0.8 + fog` per channel, after § 4.3's absorption at its
  distance; with the zone not water, the wall reads as before.
  *Test:* `tests/device/RenderUnderwaterTest.cpp`, new, `device`.
  *Breaks when:* the tint is applied in linear light, or applied out of water.

- **INV-4** — the fade: under water, a wall in the water at two distances
  keeps its share of light on § 4.3's line, the rest the water's colour,
  before the tint; at the full output a far one shows `ViewFog`
  through the tint; a wall out of the water takes the tint and no fade.
  *Test:* `tests/device/RenderUnderwaterTest.cpp`.
  *Breaks when:* the fade ignores depth, fades to black, skips the
  exposure, or fades a surface out of the water.

- **INV-5** — the wobble moves the picture under water and not out of it: a
  vertical edge's column differs between two light times under water and
  is the same out of it.
  *Test:* `tests/device/RenderUnderwaterTest.cpp`.
  *Breaks when:* the wobble is always on, or never.

- **INV-6** — caustics: from Medium, a lit wall in a water zone changes
  between two light times; a dry wall does not, and below Medium neither
  does.
  *Test:* `tests/device/RenderUnderwaterTest.cpp`.
  *Breaks when:* caustics ignore the zone, the clock or the tier, or a
  re-bake differing only in `water` keeps the old zone records.

- **INV-7** — a liquid seen from behind is lit as its front, from probes
  in front of it; any other surface seen from behind is lit as its back.
  *Test:* `tests/device/RenderWaterTest.cpp`.
  *Breaks when:* the normal or the probe point is not mirrored, or every
  two-sided surface is mirrored.
  The sky half has no automated test: no device fixture draws a sky for
  water to reflect. It was checked on DM-ArcaneTemple's frames.

- **INV-8** — shafts: from Medium, under water, a shadowed light adds light
  in the water before a far wall; out of the water, and below Medium, it
  adds none with no haze.
  *Test:* `tests/device/RenderUnderwaterTest.cpp`.
  *Breaks when:* the water does not scatter, scatters out of water, or the
  fade is applied after the fog's light. The shafts' own fade by depth has no
  test: a near light, which a test can place, is not faded.

## 6. Failure modes

- **The camera on the surface** flips between tinted and not as it crosses;
  UT99 eases it (§ 9).
- **A map with no `ZONE`** has one zero zone: never water.
- **Things above the water seen from below** are not faded at all, though
  part of their distance is water. The original shows them so.
- **A water zone of `ViewFog` 0** goes dark with distance and takes no
  colour. That is the map's own setting.

## 7. Tests

- INV-1 — `tests/unit/BundleZonesTest.cpp`, extended; `unit`.
- INV-2 — `tests/unit/BakeZonesTest.cpp`, extended; `unit`.
- INV-3, INV-4, INV-5 — `tests/device/RenderUnderwaterTest.cpp`, new;
  `device`, at `Tier::Low`.
- INV-6 — the same file, at `Tier::Medium` and `Tier::Low`.
- INV-7 — `tests/device/RenderWaterTest.cpp`, at `Tier::Low`.
- INV-8 — `tests/device/RenderUnderwaterTest.cpp`, at `Tier::Medium` and
  `Tier::Low`.

Each is seen failing before the code it locks exists. **Measured, not
asserted:** the original's frames under and over water (asked of the
UT_MonsterHunt session, 2026-10-02) against ours at the same poses: the
tint's mean colour shift, per zone class.

## 8. Alternatives considered (and rejected)

- **Absorb toward the zone's colour in the scene shader, with no tint.**
  Loses the original's own formula, which the map's author tuned.
- **Ease the tint over time now.** A still capture would then depend on how
  long the camera had been under, so the measurement could not compare.
- **A water flag guessed from liquid surfaces.** The roadmap body rules it
  out: the map says which zones are water.

## 9. Out of scope

- Drifting particles — tracked by UTA-0215, a later part.
- Easing the tint in and out as UT99 does — deferred; not yet queued.
- Water volumes that behave like water — tracked by UTA-0090.
- Per-zone visibility — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleZonesTest.cpp` |
| INV-2 | `tests/unit/BakeZonesTest.cpp` |
| INV-3, INV-4, INV-5, INV-6, INV-8 | `tests/device/RenderUnderwaterTest.cpp` |
| INV-7 | `tests/device/RenderWaterTest.cpp` |
| No sky in a reflection from under water | **nothing** automated — checked on DM-ArcaneTemple's frames |
| Whether it reads as underwater | **nothing** automated — § 7's comparison against the original's frames, run by hand |

## 11. Cross-doc impact

- `docs/specs/UTA-0156-zone-ambient-light.md` § 4.1 — the zone record.
- `docs/specs/UTA-0014-vulkan-draw-path.md` § 4.10 — the output stage.
- `CLAUDE.md` — the bundle format and baker revision.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0215-underwater-view-loop-log.md`.

## 13. Migration / compatibility

`FORMAT_VERSION` 19: every map is baked again.
