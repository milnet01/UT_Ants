# UTA-0263 — shader flames

**Status:** accepted (2026-10-01), unreviewed. Contract reviews are cancelled for this
project (`CLAUDE.md` § Contract reviews); no review runs unless the user asks.
**Kind:** feature.
**Source:** ROADMAP UTA-0263 (user-request-2026-10-01, split from UTA-0105).

**Layman:** torch flames and burning surfaces are drawn the way modern games
draw them — moving, glowing, never paper-thin from the side — and the light
around a torch flickers with its flame.

## 1. Goal

A flame in a map is drawn by the renderer from moving noise, not from UT99's
FireTexture picture. A thin flame sheet becomes one upright flame that turns to
face the camera. A solid surface covered in flame shows moving flame on the
surface itself. Both glow through bloom. The steady light standing in a flame
flickers in step with it. A FireTexture that is not a flame — a shield, a
waterfall, lightning — looks as it does today.

## 2. Problem

1. The user, 2026-10-01: *"Please replace fires / flames with modern forms of
   creating them."*
2. Today a FireTexture is a still. `ubake/FireStill.h`'s `fireStill` runs
   UT99's simulation for `FIRE_STILL_FRAMES` frames and the bake keeps the last
   frame as an ordinary picture. Nothing moves.
3. A torch flame is a flat two-sided sheet (AS-Frigate's `TORCHES2` surface
   carries polyFlags `0x40010C`: translucent, not solid, two-sided, unlit —
   `ut-dump --surface-list` on `Maps/AS-Frigate.unr`). Seen edge on, it is a
   line.
4. The fact that a material was a FireTexture is gone after the bake.
   `ubundle::MaterialRecord` holds `id`, `metallic` and `parallaxDepth` only,
   and `Bake.cpp` uses the texture's palette and sparks only to make the still.
5. Such a flame cannot bloom. `scene.frag` emits only for a surface without
   `PF_UNLIT`, and the translucent pass in `Frame.cpp` binds one colour
   attachment, so it writes no emission.
6. Nothing ties a light to a surface. The `LITE` and `GEOM` sections are
   independent.
7. **FireTexture is not the same as flame.** UT99 uses the class for any
   spark-driven effect. A census of the reference install found 624 of its
   1,445 maps drawing 41,801 surfaces with a FireTexture of a package the map
   imports, across 145 textures, counting a package and name once however
   they are capitalised. The most common are flames (`ancflame2`,
   `TORCHES2`, `ancflame4`, `TORCHES3`, `SmallFireH3`), but the list also holds
   `waterfall`, `Snow_1`, `Storm2`, `bolt`, `BlueShield` and `Energy1`.
   Measured with the scratch scripts in `~/.cache/uta-scratch/u263/`:
   `firenames.py` lists each package's FireTexture exports from its tables,
   `ut-dump --install <install> --surface-list --ndjson Maps/*.unr` lists the
   surfaces, and `census2.py` matches a surface's texture name against the
   FireTextures of the map and the packages it imports. Matching by name
   alone, without the package, over-counts: names such as `Black` and `Invis`
   also belong to plain textures.
8. Most of those surfaces are sheets. Of the 41,801, 13,198 carry translucent,
   not solid, two-sided and unlit; 6,157 add masked; 8,300 carry none of the
   flags the census reads (same census).

## 3. Scope decisions (agreed with the user)

- **Fire is replaced, not replayed** — the user, 2026-10-01. So UT99's spark
  simulation is not ported to run live.
- **Shader flames, of three offered** — the user, 2026-10-01. The other two
  were particle flames and shader flames now with particles later. § 8 says
  what lost.
- **Cheapest method that still looks modern, first iteration** — the user,
  2026-09-14.
- **Looks are decided by measurement, never by asking the user to compare** —
  the user's standing instruction. § 4.6 is where that happens.
- **Lava stays with the liquids** — this spec's call. It is UTA-0105's, with
  UTA-0089; the user asked that liquid look like liquid.
- **A flame keeps its own colours** — this spec's call. The flame's colour
  ramp comes from the FireTexture's palette, so a purple brazier (`ancpurp`,
  `URN-PURP`) stays purple.
- **The light flickers only if the map left it steady** — this spec's call. A
  light the map already made `LT_FLICKER` or `LT_PULSE` keeps its own effect.

## 4. Design

### 4.1 Which FireTextures are flames — `ubake`

The bake decides, once, per FireTexture material. **A FireTexture is a flame
when a curated entry names it, and not otherwise.** There is no rule for a
texture the list does not name.

The curated entries live beside UTA-0010's table, keyed the same way, by
picture fingerprint, as a new `umat` list of `{fingerprint, name}`. A
FireTexture's picture is the still `fireStill` makes, so a change to
`ubake/FireStill.cpp` changes every flame's key. INV-3 is what notices.

The list comes from a labelled fixture, `tests/real/flame-labels.txt`: one
line per census texture giving its package, its name, and `flame` or `other`.
Each texture was labelled by its name and its still picture, and the unclear
ones by how maps use them. The implementer labelled them, never the user.

**Why no rule.** A rule on the stored properties was fitted against those
labels and none was good enough. The candidate was `bRising` plus at least
half the sparks of a burning type. It missed 17 of the 34 flames, among them
`TORCHES2` and `TORCHES3`, whose sparks are all `OzHasSpoken`, and it called a
smoke texture a flame. The best rule with no false flames, over every set of
spark types the flames use, found 15 of the 34. A false flame turns a shield
or a waterfall into fire; a missed flame keeps today's still. So a texture
outside the list keeps its still.

A FireTexture that is not a flame keeps today's still, unchanged (UTA-0176).

### 4.2 `MATS` gains a flame look — `ubundle`

```cpp
struct FlameLook {
    /// The palette sampled at eight evenly spaced heats, coldest first, as
    /// linear RGB. Entry 7 is the hottest.
    std::array<std::array<float, 3>, 8> ramp{};
};

struct MaterialRecord {
    std::string id;
    bool metallic = false;
    std::uint8_t parallaxDepth = 0;
    /// UTA-0263: set when the bake judged this material a flame (§ 4.1).
    std::optional<FlameLook> flame;
};
```

A flame material keeps its still picture too, which the renderer never draws
while it draws flames and which a tool can still show.

### 4.3 A new `FLAM` section — `ubundle`, `ubake`

One record per flame the renderer draws as a camera-facing flame:

```cpp
struct Flame {
    std::uint32_t material;   ///< index into MATS; that record has a flame look
    std::array<float, 3> base; ///< the flame's foot, world units
    float width;              ///< world units
    float height;             ///< world units, up from `base`
    std::uint32_t seed;       ///< decorrelates two flames of one material
    std::int32_t light;       ///< index into LITE, or -1 (§ 4.5)
};
```

`read` and `write` refuse a record whose `material` is out of range or names a
record with no flame look, a `light` below -1 or out of range, or a width or
height that is not finite and positive.

**Which surfaces become records.** A surface whose material is a flame, and
whose polyFlags carry `PF_NOT_SOLID` and one of `PF_TRANSLUCENT` or
`PF_MASKED`, is a flame sheet. The bake takes it out of `GEOM` and makes a
record from it:

- `base` is the centre of the sheet's lowest edge, and `height` is the
  sheet's vertical extent;
- `width` is its horizontal extent;
- `seed` is the surface's index in the level's surface list.

**Sheets that make one flame are merged.** Torches are often two crossed
sheets. Two flame sheets of one material, with `base` points closer than half
the smaller `width`, become one record whose extents cover both. The rule
applies pairwise until nothing merges.

A flame surface that is not a sheet stays in `GEOM`, and is drawn as moving
flame on the surface (§ 4.4).

`FORMAT_VERSION` becomes `15` and `BAKER_REVISION` becomes `30`. `shapeOf` in
`urender/Frame.cpp` samples `FLAM` and the flame looks, since the renderer
uploads both; a section it does not sample is not re-uploaded after a re-bake.

### 4.4 Drawing — `urender`

**The flame shader.** One function, shared by both kinds of flame, turns a
point in the flame's own space `(u across, v up, both 0 to 1)` and the time
into heat:

1. two octaves of value noise, scrolled up at different speeds, one warping
   the other's coordinates, with the warp growing towards the top;
2. minus `v` times an erosion factor, so tongues thin and break off as they
   rise;
3. times a shape mask — for a camera-facing flame, a teardrop that fades to 0
   at the sides and the base; for a surface, 1.

Heat indexes the material's ramp. The colour is written as emission. Alpha is
not used: flames are **additive**, so their draw order does not matter and
nothing is sorted.

The noise is computed in the shader. No noise texture is added.

**Camera-facing flames.** One instanced draw per frame over `FLAM`. Each
instance is a quad standing on `base`, `width` by `height`, turned about the
world's vertical axis to face the camera — a cylindrical billboard, so flames
stay upright when the camera looks down on them. It is depth-tested and not
depth-written. It writes colour and the emission target, both additively.

**Flame on a surface.** A `GEOM` batch whose material has a flame look is
drawn with the flame shader instead of its picture, `(u, v)` taken from the
surface's texture coordinates. It writes emission too.

**Time** is the light clock `Frame.cpp` already keeps, so
`Renderer::pinLightSeconds` and `ut-shot --light-time` pin flames as they pin
light pulses.

**Tiers.** Flames draw on every tier with the same shader. Their cost is
measured (§ 13), not assumed.

### 4.5 The light in the flame — `ubake`, `urender`

The bake gives each record the nearest light whose position lies within
`1.5 × height` of `base`, or -1. Ties go to the lower index.

In `Lights.cpp`, a light that a `FLAM` record names and whose type is
`LT_STEADY` gets its flicker from the flame:

```cpp
float flameFlickerOf(std::uint32_t seed, double seconds) noexcept; // in [0.8, 1.0]
```

It is smooth noise, not the twenty-step jumps of `LT_FLICKER`, so the light
breathes with the flame. Any other light type keeps `flickerOf`.

### 4.6 Measured constants

Four constants are fitted, not chosen: the two noise speeds, the erosion
factor and the flame's brightness. They are fitted against original frames
that show flames — `ut-ants-uta0156`'s reference poses where they do, and
new poses captured with its `capture-original.sh` where they do not.

- **Brightness:** the mean displayed luma over the pixels where the original
  draws flame matches the original's within 10%, light time pinned.
- **Speeds and erosion:** the mean change over the flame's pixels between two
  frames 1/30 s apart matches the original's between two of its own frames
  that far apart.

Each constant records its sweep in a comment beside it, as `fog.glsl`'s
constants do.

## 5. Invariants

- **INV-1** — `MATS` round-trips a material with a flame look and one
  without, and refuses a look whose ramp holds a value that is not finite.
  *Test:* `tests/unit/BundleMaterialTest.cpp`, extended.
  *Breaks when:* the flame look is dropped or misplaced on write, or read
  back into the wrong record.

- **INV-2** — `FLAM` round-trips its records, and `read` and `write` each
  refuse § 4.3's four invalid records.
  *Test:* `tests/unit/BundleFlamesTest.cpp`, new — golden bytes, then one
  refusal per invalid record.
  *Breaks when:* a record naming a material with no flame look, or a light
  index past the end, is accepted.

- **INV-3** — every entry of `tests/real/flame-labels.txt` is judged by § 4.1
  as it is labelled.
  *Test:* `tests/real/RealFlamesTest.cpp`, new, in the real-asset tier: for
  each labelled texture, the bake's judgement equals the label.
  *Breaks when:* a curated entry calls a labelled waterfall a flame, a
  labelled torch has no entry, or a change to `fireStill` moves a flame's
  fingerprint away from its entry.

- **INV-4** — a flame sheet leaves `GEOM` and yields one `FLAM` record; two
  crossed sheets of one material yield one record; a solid flame surface stays
  in `GEOM` and yields none; a non-flame FireTexture sheet stays in `GEOM`.
  *Test:* `tests/unit/BakeFlamesTest.cpp`, new, a baked fixture with each of
  the four. The crossed pair isolates the merge: without it the fixture yields
  two records.
  *Breaks when:* the sheet is drawn twice (in `GEOM` and as a flame), the
  crossed pair is not merged, or the solid surface is lost.

- **INV-5** — a steady light named by a flame flickers within [0.8, 1.0] and
  varies over time; a steady light no flame names returns 1; an `LT_PULSE`
  light named by a flame keeps `flickerOf`'s pulse.
  *Test:* `tests/unit/RenderLightsTest.cpp`, extended.
  *Breaks when:* the association overrides a map's own light effect, or a
  flame's light stays constant.

- **INV-6** — a camera-facing flame is not paper-thin: drawn from a camera
  looking along the plane of the sheet it came from, it covers at least half
  the pixels it covers seen face on.
  *Test:* `tests/device/RenderFlamesTest.cpp`, new. The fixture's flame comes
  from one sheet, so a renderer drawing the sheet in place covers almost
  nothing edge on and fails.
  *Breaks when:* flames are drawn in the sheet's own plane.

- **INV-7** — flames reach bloom: the emission target is non-zero over a
  flame's pixels.
  *Test:* `tests/device/RenderFlamesTest.cpp`, reading the emission target.
  *Breaks when:* flames are drawn in the translucent pass, which binds colour
  only.

- **INV-8** — at a pinned light time a frame is pixel-identical across two
  draws, and two times 0.25 s apart differ over the flame's pixels.
  *Test:* `tests/device/RenderFlamesTest.cpp`.
  *Breaks when:* the flame reads the wall clock, or does not move.

## 6. Failure modes

- **A flame the list does not name** — a FireTexture from a library outside
  the reference install. It keeps its still, as a non-flame does.
- **A degenerate sheet** — zero width or height after the bake's extents. It
  makes no record and stays in `GEOM`; the bake reports it as it reports a
  skipped material. `FLAM`'s refusal of a non-positive extent is the backstop.
- **A flame with no light near it.** `light` is -1 and nothing flickers.
- **Two flames within reach of one light.** Each record names its nearest
  light, so both may name it; the light follows the lower-indexed record.
- **Many flames.** One map draws 2,052 fire surfaces (the census's
  `MH-EnterToCore-Part2`). All camera-facing flames are one instanced draw;
  their pixel cost is measured on that map (§ 13).
- **A flame inside volumetric fog.** `scene.frag` applies the fog volume to
  each surface it shades, so no later pass would veil a flame. The flame
  shader reads the same fog volume and scales its emission by the
  transmittance to its depth, as `scene.frag` does, so a flame deep in
  DM-Fetid's fog dims like the wall behind it.
- **A curated entry for a texture the map does not use** — harmless; the table
  is keyed by picture.

## 7. Tests

- INV-1 — `tests/unit/BundleMaterialTest.cpp`, extended.
- INV-2 — `tests/unit/BundleFlamesTest.cpp`, new.
- INV-3 — `tests/real/RealFlamesTest.cpp`, new, `-DUTA_REAL_ASSET_TESTS=ON`
  only.
- The flame list's digest — `tests/unit/FlameLibraryTest.cpp` pins it, so an
  edit to the list fails until `BAKER_REVISION` is bumped and the new digest
  recorded. A cached bake is reused until the revision moves.
- INV-4 — `tests/unit/BakeFlamesTest.cpp`, new.
- INV-5 — `tests/unit/RenderLightsTest.cpp`, extended.
- INV-6, INV-7, INV-8 — `tests/device/RenderFlamesTest.cpp`, new, label
  `device`, run on lavapipe and on the GPU with synchronization validation.

Each new test is seen failing before its code exists. § 4.6's fits are
recorded beside their constants, not asserted by a test.

## 8. Alternatives considered (and rejected)

- **Particle flames** — many small glowing puffs that rise, fade and drift.
  The fuller modern method, and right from any angle, but it needs a particle
  system this renderer does not have. Offered to the user and not chosen.
- **Replaying UT99's simulation live** — `fireStill` run per frame. It would
  move, but it is the 1999 look the user asked to replace, and a sheet would
  still be paper-thin.
- **A flipbook** — pre-rendered frames of a flame. Cheap to draw, but it is
  an art asset this project would have to make and ship, and fixed frames
  cannot take each flame's own palette without a recolouring step that costs
  what the noise does.
- **A noise texture instead of shader noise** — one fetch cheaper per sample,
  but a new asset and a new upload for a cost two octaves of value noise do not
  need.
- **A rule on spark types and `bRising`** — fitted and rejected; § 4.1 gives
  the measurement.
- **Every FireTexture a flame** — the census's list of non-flame FireTextures
  (§ 2 item 7) would turn waterfalls and shields to fire.
- **Soft depth fade** where a flame meets geometry. No shader samples the
  scene's depth today. The teardrop mask fading to 0 at the base hides the
  seam in the common case of a flame standing in a cup; deferred (§ 9).
- **Drawing flames in the translucent pass** — it binds colour only, so flames
  would not bloom (§ 2 item 5).

## 9. Out of scope

- Sparks, embers and smoke above a flame — deferred; not yet queued.
- Soft depth fade where a flame meets geometry — deferred; not yet queued.
- Flames on actors (sprites, meshes, decorations): `urender` draws no actors
  yet. Deferred; not yet queued.
- Lava and liquid motion — tracked by UTA-0105 and UTA-0089.
- Non-flame FireTextures moving (shields, lightning) — tracked by UTA-0105.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleMaterialTest.cpp` |
| INV-2 | `tests/unit/BundleFlamesTest.cpp` |
| INV-3 | `tests/real/RealFlamesTest.cpp` — real-asset tier only, so the gate on a machine without the install does not run it |
| INV-4 | `tests/unit/BakeFlamesTest.cpp` |
| INV-5 | `tests/unit/RenderLightsTest.cpp` |
| INV-6, INV-7, INV-8 | `tests/device/RenderFlamesTest.cpp` |
| § 4.6's fitted constants | **nothing** — a look fit is recorded, not asserted |
| `shapeOf` samples `FLAM` | **Partial:** a stale upload shows only on a re-bake in a running viewer; no test re-bakes under a live renderer |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md` — `FLAM` joins the
  section list.
- `docs/specs/UTA-0011-map-baker.md` — `MATS` gains the flame look.
- `docs/specs/UTA-0014-vulkan-draw-path.md` — the flame pass, and that a
  flame material's `GEOM` batch is drawn with the flame shader.
- `CLAUDE.md` § Standing facts — the new format and baker revision.
- `CHANGELOG.md` — one entry.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0263-shader-flames-loop-log.md`.

## 13. Resource cost

- **Bundle:** 32 bytes a `FLAM` record, 96 bytes a flame look.
- **GPU:** the records and looks as one storage buffer each; no new texture.
- **Frame time:** measured with `ut-bench frame` at `--tier ultra --size
  3840x2160` on AS-Frigate and on `MH-EnterToCore-Part2`, before and after,
  turn and turn about. Recorded on the roadmap item. No budget is set in
  advance: a first measurement decides whether one is needed.

## 14. Migration / compatibility

`FORMAT_VERSION` becomes `15`. `ubundle::read` refuses any other version, so
every map baked before this item must be baked again. `BAKER_REVISION` becomes
`30`, so the bake cache re-bakes on its own.
