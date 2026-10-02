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
gently. Out of water nothing changes. This is the cheapest part of the
item's bar; caustics, light shafts from the surface, the surface seen from
below and drifting particles follow (§ 9).

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

### 4.3 Absorption with distance — `urender`, `scene.frag`

With the camera's zone water, every fragment's colour, after its fog, is
scaled by `exp(−z / WATER_VISIBILITY)`, `z` its view depth. `FrameData`
carries the flag as `cameraUnderwater`. Far things go
dark and § 4.2's tint then shows as the water's colour, so far things settle
on exactly the zone's `ViewFog` and near ones keep their own. A translucent
surface and a flame take the same factor, as the fog's transmittance does.

```glsl
// fog.glsl, which scene.frag and flame.frag both include
const float WATER_VISIBILITY = 600.0; // UT units over which light falls to 1/e
```

`WATER_VISIBILITY` is this spec's call, about twelve metres at UT99's scale,
and is not fitted: the original has no absorption to fit it against.

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

- **INV-4** — absorption: under water, the same wall at twice the distance
  is darker by `exp(−z / WATER_VISIBILITY)`'s ratio before the tint.
  *Test:* `tests/device/RenderUnderwaterTest.cpp`.
  *Breaks when:* absorption ignores depth, or applies out of water.

- **INV-5** — the wobble moves the picture under water and not out of it: a
  vertical edge's column differs between two light times under water and
  is the same out of it.
  *Test:* `tests/device/RenderUnderwaterTest.cpp`.
  *Breaks when:* the wobble is always on, or never.

## 6. Failure modes

- **The camera on the surface** flips between tinted and not as it crosses;
  UT99 eases it (§ 9).
- **A map with no `ZONE`** has one zero zone: never water.
- **Things above the water seen from below** are absorbed over their whole
  distance, though part of it is air. Accepted for this first part.
- **A water zone of `ViewFog` 0** goes dark with distance and takes no
  colour. That is the map's own setting.

## 7. Tests

- INV-1 — `tests/unit/BundleZonesTest.cpp`, extended; `unit`.
- INV-2 — `tests/unit/BakeZonesTest.cpp`, extended; `unit`.
- INV-3, INV-4, INV-5 — `tests/device/RenderUnderwaterTest.cpp`, new;
  `device`, at `Tier::Low`.

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

- Caustics on surfaces under water — tracked by UTA-0215, its next part.
- Light shafts from the surface, the surface seen from below, drifting
  particles — tracked by UTA-0215, later parts.
- Easing the tint in and out as UT99 does — deferred; not yet queued.
- Water volumes that behave like water — tracked by UTA-0090.
- Per-zone visibility — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleZonesTest.cpp` |
| INV-2 | `tests/unit/BakeZonesTest.cpp` |
| INV-3, INV-4, INV-5 | `tests/device/RenderUnderwaterTest.cpp` |
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
