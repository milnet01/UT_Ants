<!-- ants-spec-format: 1 -->
# UTA-0350 — open the sky a map only painted on its walls

**Status:** accepted (2026-10-11), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** fix.
**Source:** ROADMAP UTA-0350 (in-session-2026-10-11; widened by the user
2026-10-11: "this is an opportunity to build something to cater for these
kinds of maps").
**Pairs with:** UTA-0338, UTA-0346, UTA-0337.

**Layman:** some maps paint a sky picture on the walls of a closed box
instead of using a real sky. A map's recipe can now turn those walls into
open sky: they show a blue sky with a few clouds, the map's sun shines in
through them, and daylight reaches the level.

## 1. Goal

A map's recipe can name the textures that paint its sky. The baker treats
every surface wearing one as `PF_FakeBackdrop`, so the sun and the probes'
sky rays pass through it exactly as they pass through a real sky. The
bundle carries the sky's look in a new `PSKY` section. Where a map has
`PSKY` and no sky view, the renderer draws those surfaces as a gradient sky
with a static cloud layer and the UTA-0338 sun disc, and the probe bake
takes daylight from the same sky. The analyser lists the maps that look
painted, so each can be given a recipe. A map whose recipe has no `[sky]`
bakes and draws exactly as today.

## 2. Problem

1. **Some maps have no sky.** `MH-()mG-TheBoat-V2mini-BP` carries no
   `PF_FakeBackdrop` surface and no SkyZoneInfo; its sky is the `DayskyB`
   texture on an ordinary closed box. *Command:* `build/tools/ut-dump/ut-dump
   --install "<install>" --surface-list "<install>/Maps/MH-()mG-TheBoat-V2mini-BP.unr"`,
   grouped by `texture` and `polyFlags` → every texture `0x0` but
   `WAT7PROC` `0x200`.
2. **Zone ambient lit them, and it is withdrawn.** The same map's `ZONE`
   section gives entry 1 ambient brightness 255 and the other six 0 (read
   from the r50 bundle's `ZONE` bytes, `ubundle::detail::readZone`'s
   layout). UTA-0112 § 4.12 item 3 withdrew zone ambient from `scene.frag`
   (UTA-0156 INV-14), so the deck is dark.
3. **The sun cannot get in.** `ubake::sunSeen` (`src/ubake/LightProbes.cpp`)
   and the SMSK sun site (`src/ubake/ShadowMask.cpp`) see the sun only where
   a ray's first hit is a backdrop surface. A closed box has none, so
   TheBoat's UTA-0346 sun lights nothing.
4. **A backdrop with no sky view has no sky.** `scene.frag` draws a
   backdrop surface with `skyAt` only while `frame.skyTexture != NONE`;
   otherwise it draws its own picture unlit. `ubake::surfaceFrom` returns
   nothing for a probe ray that meets a backdrop with no sky view, so such
   a sky gives no daylight.

## 3. Scope decisions (agreed with the user)

- **A recipe opts each map in; nothing is automatic** (session's
  recommendation, user's direction 2026-10-11). The standing rule is that
  lighting is decided per map, never by a blanket rule (`CLAUDE.md`
  § Where this project is).
- **Zone ambient stays withdrawn** (user, 2026-10-11, choosing this fix over
  restoring flat light for full-bright zones).
- **The sky has a sun and a few clouds** (user, 2026-10-11: "add in a sky
  and sun, perhaps even a few clouds"). The clouds are still; moving them is
  UTA-0337's.
- **The analyser finds the candidates** — new kinds of check go into it
  (user, 2026-10-11, UTA-0347).

## 4. Design

### 4.1 The recipe's sky section — `urecipe`

Recipe version 5 adds one optional `[sky]` section. Like `[sun]`, it needs
`[map] sha256`.

```
[sky]
textures = <texture> [<texture> ...]   ; 1 to 8, each once, as [material] names one
top = <rrggbb>                          ; sRGB hex; optional, default 3a78c8
horizon = <rrggbb>                      ; optional, default b4d4ee
clouds = <0..100>                       ; percent of the sky covered; optional, default 20
```

```cpp
struct Sky {                      // urecipe, Recipe.h
    std::vector<std::string> textures; // as MaterialAssignment::texture, sorted, each once
    std::array<std::uint8_t, 3> top{0x3a, 0x78, 0xc8};
    std::array<std::uint8_t, 3> horizon{0xb4, 0xd4, 0xee};
    std::uint8_t clouds = 20;     // 0 to 100
};
std::optional<Sky> Recipe::sky;
```

`write` emits every key. `bakeDigest` adds the section after the sun's
fields, so a recipe with no `[sky]` keeps its version-4 digest.

### 4.2 Baking it — `ubake`

- **Refusals.** A `[sky]` on a map whose `skyViewOf` is set refuses the
  bake: that map already has a sky. A named texture no surface wears is
  reported in the bake report's `recipeUnused`, as an unused `[material]`
  is.
- **The flag.** A step beside `maskByTexture` (`src/ubake/Bake.cpp`) ORs
  `PF_FakeBackdrop` into every `BspSurf` whose texture resolves to a named
  one, before any step reads the flags. From there GEOM batches, charts
  (none: backdrop is in `UNLIT_FLAGS`), the SMSK, the shadow pass and the
  probes all treat it as sky. Collision does not read the flag, so the
  walls stay solid.
- **Daylight.** `surfaceFrom`'s backdrop branch, with no sky view and a
  painted sky, returns a sky hit; the probe gathers
  `scaled(ubundle::paintedSkyLight(sky, w), REFLECTANCE_SCALE)` for it — the
  scale a sky's unlit picture sends (§ 4.12 item 2). The sun stays out of
  that radiance, as it does for a real sky (UTA-0338 § 4.4).
- `BAKER_REVISION` becomes 51.

### 4.3 The `PSKY` section — `ubundle`

Bundle format 27 adds an optional `PSKY` section, at most one, written when
the recipe has `[sky]`.

```cpp
struct PaintedSky {               // ubundle, Bundle.h
    std::array<float, 3> top{};     // linear RGB, each in [0, 1]
    std::array<float, 3> horizon{}; // linear RGB, each in [0, 1]
    float clouds = 0;               // share covered, in [0, 1]
};
std::optional<PaintedSky> Bundle::paintedSky;
```

Bytes: the seven `f32` in that order. `read` refuses a component outside
`[0, 1]` or not finite.

`ubundle/Sky.h` gains the one daylight function both sides use:

```cpp
/// The painted sky's light from direction `d` (unit, z up): the gradient,
/// clouds blended in at their cover where they show -- UTA-0350 SS 4.3.
[[nodiscard]] std::array<float, 3> paintedSkyLight(const PaintedSky& sky,
                                                   const std::array<double, 3>& d) noexcept;
```

- `gradient(d) = mix(horizon, top, sqrt(clamp(d.z, 0, 1)))`.
- `cloudFade(d) = smoothstep(0.02, 0.25, d.z)`.
- `paintedSkyLight(d) = mix(gradient(d), CLOUD, clouds * cloudFade(d))`,
  with `CLOUD = 0.85` in each channel.

### 4.4 Drawing it — `urender`

- `Frame` gains `paintedSky` (1 where the bundle has `PSKY` and no sky view)
  and the seven values, in `ShaderTypes.h` and `types.glsl` together.
- `skyAt(d)`: with `frame.skyTexture == NONE` and `frame.paintedSky != 0` it
  returns `paintedSkyAt(d) + sunInSky(d)`. The backdrop branch in `main`
  takes `skyAt` when either sky exists.
- `paintedSkyAt(d)` is `gradient(d)` with clouds over it:
  `mix(gradient(d), CLOUD, cloudFade(d) * mask)`, where `mask` is value-noise
  fbm (four octaves) over `d.xy / max(d.z, 0.05)`, thresholded so a share
  near `clouds` of the dome is covered. It is static.
- Water reflects it through `skyAt`, as now; `waterSeesSky` already keys on
  backdrop batches.

### 4.5 Finding the maps — `scripts/map-analyser.py`

Each map's JSON line gains `paintedSky`: `null` where the map has a
`PF_FakeBackdrop` surface; otherwise the textures, from `ut-dump
--surface-list`, whose name contains `sky` (any case), and `brightZones`,
the number of `ZONE` entries with ambient brightness of 128 or more, read
from the bundle. A non-empty texture list marks a candidate; the totals line
counts them.

### 4.6 The first painted sky — `recipes/`

`recipes/mh-()mg-theboat-v2mini-bp.recipe` moves to version 5 and gains
`[sky]` naming its `DayskyB` texture, with the defaults. Its `[sun]` stays.

## 5. Invariants

- **INV-1** — a recipe with a sky round-trips through `parse` and `write`,
  and `parse` refuses an unknown key, a second `[sky]`, no `textures`, more
  than 8 or a repeated texture, a malformed colour, `clouds` above 100, a
  `[sky]` below version 5, and a sky with no `[map] sha256`.
  *Test:* `tests/unit/RecipeFormatTest.cpp`.
  *Breaks when:* a range is checked on one path only; the sha256 rule is
  dropped.

- **INV-2** — `bakeDigest` of a sky-free recipe equals its version-4 value,
  and changing any one sky field changes it.
  *Test:* `tests/unit/RecipeFormatTest.cpp`, against a recorded digest.
  *Breaks when:* the tag changes for every recipe; a field is left out.

- **INV-3** — `PSKY` round-trips, and `read` refuses a second section and a
  value outside `[0, 1]` or not finite.
  *Test:* `tests/unit/BundleSkyTest.cpp`, new.
  *Breaks when:* a range is checked on write only.

- **INV-4** — the baker sets `PF_FakeBackdrop` on exactly the surfaces
  wearing a named texture, and refuses a `[sky]` on a map with a sky view.
  *Test:* `tests/unit/BakeSkyTest.cpp`, new: a fixture with two textures,
  one named.
  *Breaks when:* the flag lands after a step that reads it; every surface
  of the package is flagged.

- **INV-5** — in a closed fixture room whose roof wears a named texture,
  under a sun above it, the floor's SMSK sun pair sees the sun and a probe
  under the roof holds daylight; without `[sky]`, neither.
  *Test:* `tests/unit/BakeSkyTest.cpp`.
  *Breaks when:* the flag is set after the SMSK or probe bake reads it;
  `surfaceFrom` still returns nothing with a painted sky.

- **INV-6** — `paintedSkyLight` is `horizon` at `d.z = 0`, `top` blended
  toward `CLOUD` by `clouds` at `d.z = 1`, and `horizon` below the horizon;
  the shader's gradient and fade agree with it within the parity test's
  tolerance.
  *Test:* `tests/unit/BundleSkyTest.cpp`; `tests/device/shaders/light_parity.comp`.
  *Breaks when:* the shader's gradient or fade drifts from the C++ one.

- **INV-7** — a backdrop surface in a bundle with `PSKY` and no sky view
  draws the painted sky, not its own picture; with neither, it draws its
  picture as today.
  *Test:* `tests/device/RenderSunTest.cpp`, extended.
  *Breaks when:* `main`'s backdrop branch still tests `skyTexture` alone.

- **INV-8** — the analyser reports `paintedSky` for a map with no backdrop
  surface and a sky-named texture, and `null` for a map with a backdrop.
  *Test:* run on TheBoat and DM-Eagles, which UTA-0346 gave a sun through its sky — manual.
  *Breaks when:* the backdrop test reads the wrong flag bit.

## 6. Failure modes

- **A named texture also paints something that is not sky** — those
  surfaces turn to sky too. The recipe's author picks the textures; the
  analyser lists each one's surfaces' count to help.
- **The box is small, so the sky's walls are near.** The sky is drawn by
  direction, not position, so it looks infinitely far whatever the box's
  size; only its depth is the wall's (UTA-0252).
- **A light inside the box now leaks out through the walls** in the SMSK
  and shadow pass, as through any sky. Nothing stands outside the box to
  receive it.
- **The cloud cover is approximate.** The probe uses the mean (`clouds`),
  the renderer a pattern whose covered share is near it.

## 7. Tests

| Test | Locks |
|------|-------|
| `tests/unit/RecipeFormatTest.cpp` | INV-1, INV-2 |
| `tests/unit/BundleSkyTest.cpp`, new | INV-3, INV-6 |
| `tests/unit/BakeSkyTest.cpp`, new | INV-4, INV-5 |
| `tests/device/shaders/light_parity.comp` | INV-6 |
| `tests/device/RenderSunTest.cpp` | INV-7 |
| manual analyser run | INV-8 |

Each new case is seen failing before its code lands. Then TheBoat is
re-baked into scratch and shot at the user's 2026-10-07 cameras: the sky
shows blue with clouds, and direct light on the deck rises from today's
r50 frames.

## 8. Alternatives considered (and rejected)

- **Restore zone ambient for full-bright zones.** Quick, but a blanket rule,
  flat and shadowless; the user chose against it on 2026-10-11.
- **Recipe lamps only.** No code, but the sky stays a dim picture and the
  sun still lights nothing.
- **Show the painted picture as the sky.** The `DayskyB` picture is tiled on
  flat walls, so its seams and box corners show; drawing by direction hides
  the box.
- **Derive the sky's colours from the painted texture.** Possible later; a
  recipe's three values are simpler and the defaults suit a clear day.

## 9. Out of scope

- Moving clouds, and telling cloudy from clear for captured skies —
  UTA-0337.
- Volumetric skies — UTA-0348.
- Recipes for painted-sky maps other than TheBoat — deferred; not yet
  queued. The analyser's list is where they come from.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/RecipeFormatTest.cpp` |
| INV-2 | `tests/unit/RecipeFormatTest.cpp` |
| INV-3 | `tests/unit/BundleSkyTest.cpp` |
| INV-4 | `tests/unit/BakeSkyTest.cpp` |
| INV-5 | `tests/unit/BakeSkyTest.cpp` |
| INV-6 | `tests/unit/BundleSkyTest.cpp` and `light_parity.comp` |
| INV-7 | `tests/device/RenderSunTest.cpp` |
| INV-8 | **nothing** — a manual run; the analyser has no tests |
| § 4.4's cloud cover | **nothing** — judged on TheBoat's frames |

## 11. Cross-doc impact

- `CLAUDE.md` § Standing facts: bundle format 27, baker revision 51.
- UTA-0113's recipe format gains version 5 — noted in `Recipe.h`'s header.
- UTA-0112 § 4.12 item 2: a painted sky gives daylight with no sky view —
  a line there pointing here.
- CHANGELOG entry.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0350-painted-skies-loop-log.md`.

## 13. Migration / compatibility

Format 27 and revision 51 make every bundle stale, as every format bump
does; a bundle without `PSKY` reads and draws as before. Recipes at
versions 1 to 4 parse unchanged.
