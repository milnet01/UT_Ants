<!-- ants-spec-format: 1 -->
# UTA-0338 — a sun on clear skies, baked as a light and drawn in the sky

**Status:** accepted (2026-10-09), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** feature.
**Source:** ROADMAP UTA-0338 (user-request-2026-10-08, split from UTA-0337
part 2 on 2026-10-09).
**Blocker for:** UTA-0334.

**Layman:** a map with a clear sky gets a sun. It lights and shadows the
level from where it stands, shows as a bright disc in the sky, and appears in
water reflections.

## 1. Goal

A map's recipe can declare one sun: where it stands in the sky, its colour
and its brightness. The baker writes it to a new `SUN` section and bakes it
like any other light — shadow masks for the level, bounce into the probes —
except that it shines from infinitely far away, and a point sees it only
where a ray toward it reaches the sky. Players and movers take its light
scaled by a visibility each probe stores. The renderer draws its disc in the
sky. A map whose recipe declares no sun bakes and draws exactly as today.

## 2. Problem

1. **No light shines from infinitely far.** `ubundle::Light`
   (`src/ubundle/Bundle.h`) is a placed actor's record, and
   `ubake::lightAt` (`src/ubake/LightModel.h`) measures every light from
   `litFrom`, a point, with `falloff(distance, radius)`. Nothing in `src/`
   reads UT's sunlight effect or `bSunAffect`. *Command:* `rg -i
   'sunlight|bSunAffect|LE_Sun|directional' src` → no match.
2. **A sun is a light, so it must be in the bake before UTA-0334's maps are
   baked**, or they are baked twice (UTA-0337 part 2).
3. **The baker has no picture of the sky.** The sky cube is captured by the
   renderer (`urender/Sky.h`, `Frame.cpp`'s `skyCapture`) once a bundle is
   drawn; the baker knows only `skyViewOf` (`src/ubundle/Sky.h`), a
   location. So the sun cannot be found from the sky picture at bake time
   without new work (§ 8).
4. **Level shadows come only from SMSK pairs** (UTA-0326 INV-10), and movers
   and actors are shadowed by shadow maps that `Shadows.h` builds only for
   point lights and spotlights. A light with no pairs lights no level
   fragment; a light with no shadow map lights actors through walls.

## 3. Scope decisions (agreed with the user)

- **A sun on a clear sky, moving clouds on a cloudy one, and outdoor maps
  without the box look** — the user, 2026-10-08 (UTA-0337).
- **The sun is split out and goes first** — the session, 2026-10-09; the
  user said that day they would go with the session's recommendations. Only
  the sun changes the bake.
- **Each map's recipe declares its sun; nothing is guessed** — the session's
  call. The recipe already carries per-map additions (UTA-0256), and § 2
  item 3 means a guess needs a sky picture the baker lacks. Telling cloudy
  from clear stays with UTA-0337.
- **Movers cast no sun shadow, and players and movers are sun-shadowed by
  the probes' baked visibility, not by a shadow map** — the session's call,
  under the standing rule that the first renderer uses the cheapest methods
  that still look like a modern game (`CLAUDE.md`, user 2026-09-14).
- **No setting turns the sun off** — the session's call; none was asked
  for.

## 4. Design

### 4.1 The recipe's sun section — `urecipe`

`RECIPE_VERSION` becomes 3. A recipe may hold one `[sun]` section:

```ini
[sun]
yaw = 8192          # compass direction the sun stands in; 65536 to a turn
pitch = 9000        # height above the horizon, in (0, 16384]
hue = 28            # UT's LightHue
saturation = 210    # UT's LightSaturation
brightness = 180    # UT's LightBrightness, 1 to 255
```

- Every key is required and is an integer in its range: `yaw` in
  [0, 65535], `hue` and `saturation` in [0, 255].
- A recipe holding a sun must carry `[map] sha256`, as a lamp does.
- `urecipe::Recipe` gains `std::optional<Sun> sun`;
  `Sun {yaw, pitch, hue, saturation, brightness}`. `parse` and `write`
  round-trip it; a refusal is `MalformedData` naming the line.
- `bakeDigest` of a recipe with no sun is unchanged. With one, the sun's
  fields are encoded after the lamps under the tag `uta-recipe-bake-3\n`, so
  any sun edit renames the bake.

### 4.2 The `SUN` section and its light index — `ubundle`

`FORMAT_VERSION` becomes 25. `Bundle::sun` is `std::optional<Sun>`, written
after `LAMP`, absent when the recipe declares none:

```cpp
struct Sun {
    std::int32_t yaw = 0, pitch = 0;      // where it stands, as SS 4.1
    std::uint8_t hue = 0, saturation = 0, brightness = 0;
};
```

- **Its light index follows the lamps.** On the wire the sun is index
  `|LITE| + |lamps|`, `|lamps|` being 0 when LAMP is absent. `MaskPair::light`
  may name it. A sun pair's `moverReach` is always 0 (§ 3).
- **LPRB gains `sunSeen`**: empty, or one float in [0, 1] per probe in
  `probes`' order. It is empty exactly when `sun` is absent.
- **Refused on read and on write:** a `pitch` outside (0, 16384]; a
  `brightness` of 0; a `sunSeen` whose size is neither 0 nor `|probes|`, or
  whose size disagrees with `sun`; a value outside [0, 1]; a sun pair with
  `moverReach` 1.
- **Folding.** `ubundle::applySun(bundle)` appends the sun to LITE as a
  `Light` whose `effect` is `SUN_EFFECT` (255, no UT effect) and whose
  rotation points the way its light travels, `(-pitch, yaw + 32768, 0)`; then
  empties `sun`. It runs after `applyAddedLamps`, which changes in one way:
  **off, it renames the sun's pairs from `|LITE| + |lamps|` to `|LITE|`**, so
  the sun's index is LITE's last whichever way lamps are set. Every caller of
  `applyAddedLamps` calls `applySun` next.

### 4.3 The light it puts on a surface — `ubake` and `light.glsl`

`lightAt` for a `SUN_EFFECT` light is colour times `lightIntensity`, times
the level's brightness, times `max(0, n · s)`, where `s` is the unit vector
toward the sun. No falloff, no spot factor, no radius. `light.glsl`'s
`lightAt` gains the same branch; the parity test (`BakeLightModelTest`,
`light_parity.comp`) covers it.

**Seen or not.** `sunSeen(x, s, rays)` is whether the ray from `x` along `s`
first meets a `PF_FakeBackdrop` surface, in a ray set in which backdrop
surfaces are occluders. A ray that meets nothing is not seen: a map open to
the void is not lit from it.

### 4.4 Baking it — `ubake`

- **SMSK.** The sun is paired with each lit polygon as UTA-0326 § 4.1 pairs
  a light, its texel shares from `sunSeen` in place of the segment test to
  `litFrom`. Polygons facing away from it, or with no texel seeing it, get no
  pair.
- **Probes.** `lightReaching` and `sentFrom` take the sun as one of their
  lights, `sunSeen` in place of `rays.blocked(x, litFrom)`. It goes into the
  base layer, never into LAMP's `added`. A ray restarted from the sky view
  (UTA-0112 § 4.12) never reaches the sun: the baked sky holds no disc.
- **`sunSeen` per probe.** The share of `directions()`' rays, within the
  sun's angular radius `SUN_RADIUS` of `s`, that `sunSeen` passes from the
  probe's point; 1 or 0 where the radius holds one direction. The spread
  gives soft edges at the cost of a few rays per probe.
- `BAKER_REVISION` becomes 48.

### 4.5 Drawing it — `urender`

- **Level fragments** take the sun from their SMSK pairs, as any light.
- **Other fragments** — movers, actors, flames lit by lights — take the
  sun's `lightAt` times `sunSeen` interpolated from the probe lattice at the
  point the probes are sampled. The sun is in no cluster and gets no shadow
  map.
- **The disc.** The frame block gains the sun's direction and colour, zero
  when there is none. `skyAt` adds a disc of radius `SUN_RADIUS` and a halo
  that falls off around it, in the sun's colour, so water reflections show
  it too. It adds nothing while `frame.skyCapture` is set: the captured cube
  stays the map's own sky.

### 4.6 The first sun — `recipes/`

One map with an open clear sky gets a sun in its recipe, chosen at build by
its captured sky (mostly blue, little variation) and its sky share in
`ut-ref score` (UTA-0292). Its values are set so the sunlit ground reads as
daylight against the map's lamps; § 7 measures it.

## 5. Invariants

- **INV-1** — a recipe with a sun round-trips through `parse` and `write`,
  and `parse` refuses a missing key, an unknown key, a second `[sun]`, a
  value out of range, and a sun with no `[map] sha256`.
  *Test:* `tests/unit/RecipeFormatTest.cpp`.
  *Breaks when:* a range is checked on one path only; the sha256 rule is
  dropped.

- **INV-2** — `bakeDigest` of a sun-free recipe equals its version-2 value,
  and changing any one sun field changes it.
  *Test:* `tests/unit/RecipeFormatTest.cpp`, against a recorded digest.
  *Breaks when:* the tag changes for every recipe; a field is left out.

- **INV-3** — `SUN` and LPRB's `sunSeen` round-trip, and `read` and `write`
  each refuse § 4.2's cases.
  *Test:* `tests/unit/BundleSunTest.cpp`, new.
  *Breaks when:* a `sunSeen` of the wrong size is accepted; a sun pair with
  `moverReach` 1 is accepted.

- **INV-4** — `lightAt` of a sun is the same at any distance, scales with
  `n · s`, and is 0 facing away; `light.glsl` agrees with it within the
  parity test's tolerance.
  *Test:* `tests/unit/BakeLightModelTest.cpp`;
  `tests/device/shaders/light_parity.comp`.
  *Breaks when:* falloff is applied to the sun; the direction's sign is
  flipped.

- **INV-5** — in a fixture room with a roof hole showing a backdrop surface
  and the sun above it, the floor texels under the hole see the sun and
  those beside it do not; a ray toward the sun that meets a plain wall, or
  nothing, is not seen.
  *Test:* `tests/unit/BakeSunTest.cpp`, new, map from
  `UnrealPackageBuilder`.
  *Breaks when:* backdrop surfaces let the ray through, so it meets nothing
  and is counted seen or unseen by accident; any first hit counts as seen.

- **INV-6** — baking the fixture with and without the sun gives byte-equal
  LITE, LAMP, GEOM, OCCL and SMSK pairs of every other light; with it, the
  floor polygon under the hole has a sun pair, LPRB `sunSeen` is above 0 at
  the probe under the hole and 0 at a probe walled off from it, and the base
  probes there are brighter.
  *Test:* `tests/unit/BakeSunTest.cpp`.
  *Breaks when:* the sun goes into `added`; `sunSeen` is not filled; an other
  light's pairs move.

- **INV-7** — after `applyAddedLamps` (either way) and `applySun`, every sun
  pair names LITE's last light and that light is the sun; a sun-free bundle
  is unchanged by `applySun`.
  *Test:* `tests/unit/BundleSunTest.cpp`, with and without lamps.
  *Breaks when:* lamps off drops the sun's pairs with the lamps' range.

- **INV-8** — the renderer draws the fixture's floor under the hole brighter
  than with the sun absent, a still mover there brighter than one walled off,
  and a disc pixel in the sky's direction; a sky-capture frame holds no disc.
  *Test:* `tests/device/RenderSunTest.cpp`, new.
  *Breaks when:* the disc is drawn into the capture; `sunSeen` is not
  applied to non-level fragments.

## 6. Failure modes

- **A sun declared on a map whose sky has no backdrop surface open to it**
  lights nothing; the bake succeeds. § 4.6's choice and the § 7 measurement
  catch it for the shipped recipe.
- **A sun low enough that its rays graze the far walls** lights long strips;
  the recipe's `pitch` is the remedy.
- **A mover under open sky** casts no sun shadow (§ 3); a player beside it is
  lit by the probe's visibility, which is soft and coarse at the lattice's
  spacing.
- **The painted sky may already show a sun** in a different place. The
  recipe's `yaw` and `pitch` are chosen to stand on it where it does.

## 7. Tests

The device tests carry the `device` label; the rest carry `unit`.

- INV-1, INV-2 — `tests/unit/RecipeFormatTest.cpp`.
- INV-3, INV-7 — `tests/unit/BundleSunTest.cpp`, new.
- INV-4 — `tests/unit/BakeLightModelTest.cpp`,
  `tests/device/shaders/light_parity.comp`.
- INV-5, INV-6 — `tests/unit/BakeSunTest.cpp`, new.
- INV-8 — `tests/device/RenderSunTest.cpp`, new.
- `tests/unit/BakeGoldenTest.cpp` re-recorded, and `BundleFormatTest`'s
  format literals raised, for § 4.2 and § 4.4.

Each new test is seen failing against the code before its rule exists.

**Measured by hand after the code lands:** bake § 4.6's map with its recipe
and score its nav views with `ut-ref score` (UTA-0292). The renderer's total
gap must stay within 2 points of the sun-free bake's; record both in
UTA-0338's body.

## 8. Alternatives considered (and rejected)

- **Find the sun from the sky picture.** Needs the sky cube at bake time,
  which only the renderer captures (§ 2 item 3), and misfires on painted or
  space skies (research note § UTA-0337). Left for UTA-0337, which needs the
  picture anyway to tell cloudy from clear.
- **Put the sun in LITE.** LITE is ascending by `exportIndex` and every
  light is an actor (UTA-0110 INV-7, INV-9); the sun has neither.
- **A shadow map for the sun.** Cascaded or not, it is the costly real
  thing for what a baked per-probe visibility emulates cheaply; movers would
  need it to cast sun shadows, which § 3 leaves out.
- **Treat the sun as sky light.** The probes already take the sky's glow
  (UTA-0112 § 4.12), but a sun through probes alone gives no sharp shadow
  on the level.

## 9. Out of scope

- Telling cloudy from clear, moving clouds, distance haze — UTA-0337.
- Suns for any map but § 4.6's — chosen as UTA-0334's maps are baked.
- Movers casting sun shadows, and a sun shadow map — deferred; not yet
  queued.
- Sun shafts in volumetric fog (UTA-0015's `lightThrough`) — deferred; not
  yet queued.
- A setting to turn the sun off.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2 | `tests/unit/RecipeFormatTest.cpp` |
| INV-3, INV-7 | `tests/unit/BundleSunTest.cpp` |
| INV-4 | `tests/unit/BakeLightModelTest.cpp`, `tests/device/shaders/light_parity.comp` |
| INV-5, INV-6 | `tests/unit/BakeSunTest.cpp` |
| INV-8 | `tests/device/RenderSunTest.cpp` |
| The first sun keeps the renderer near exact light | **nothing** — § 7's measurement, run by hand |
| The disc stands where the painted sky's sun is | **nothing** — chosen by hand per map |

## 11. Cross-doc impact

- `docs/specs/UTA-0113-recipe-format.md` § 9 — the sun section; a pointer.
- `docs/specs/UTA-0256-added-lamps.md` § 4.4 — off renames the sun's pairs.
- `docs/specs/UTA-0326-baked-shadow-mask.md` § 4.2 — `MaskPair::light` may
  name the sun.
- `docs/specs/UTA-0112-baked-light-probes.md` — LPRB's `sunSeen`.
- `CLAUDE.md` § Standing facts — the bundle format and baker revision.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0338-baked-sun-loop-log.md`.

## 13. Migration / compatibility

§ 4.2 moves the bundle format and § 4.4 the baker revision, so every map is
baked again. Version-1 and -2 recipes still parse; a game reading version 2
refuses a version-3 recipe by its header.
