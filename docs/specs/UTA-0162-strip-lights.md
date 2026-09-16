# UTA-0162 — strip lights

**Status:** spec draft (2026-09-14). The user ruled it takes no review unless they ask.
**Kind:** feature.
**Source:** ROADMAP UTA-0162 (user-request-2026-09-14).

**Layman:** a row of ceiling lights along one fixture lights a long band of
floor and wall, instead of a string of round spots.

## 1. Goal

The bake finds each row of identical lights along one fixture and marks it as
one strip light: the row's lowest-numbered light carries the segment between
the row's two end lights, and the rest are marked as absorbed into it. The
renderer and the probe bake light a point from the segment's nearest point, so
a strip casts a band. The bundle's light list still holds every light the map
placed, with its own values.

## 2. Problem

1. The user flew DM-Deck16][ after UTA-0156 and reported that *"the
   horizontal lights are showing as round lights on the geometry they are
   shining on"*.
2. Such a fixture is several `LE_Cylinder` lights on one line. Measured with
   `/mnt/Games/Scripts/Linux/ut-ants-uta0156/rows.py` over that directory's
   `ours-lights.txt` (`python3 rows.py ours-lights.txt 16 100
   type,effect,b,h,s,r 1.5`): neighbours on a row are often further apart than
   the lights reach, so each lights its own pool. The original game blurs the
   pools with coarse lightmaps; ours draws each one crisply.
3. The same measurement shows real rows are **not evenly spaced** — the long
   rows alternate gaps — and drift off a straight line by several units. The
   roadmap item's first draft named even spacing as a criterion; § 4.2 does
   not use it.
4. `ubake::lightAt` and `shaders/light.glsl` evaluate every light from one
   point, `Light::location`. Nothing in `LITE`, the light model, cluster
   culling (`shaders/cluster.comp`) or shadows (`src/urender/Shadows.cpp`) can
   describe a light spread along a line.

## 3. Scope decisions (agreed with the user)

- **This item comes before UTA-0156's soft light edges** — the user,
  2026-09-14, for *"the more realistic look ... in terms of the shapes of the
  lights shining on geometry"*.
- **Cheapest method that still looks modern, first iteration** — the user,
  2026-09-14. So a strip is lit from its nearest point, not integrated as an
  area light.
- **How it looks is decided by measurement, never by asking the user to
  compare** — the user's standing instruction. § 7's measurement step is where
  that happens.
- **Rows are detected once, in the bake** — this spec's call, from the code.
  `src/urender/CMakeLists.txt` refuses any link but `uta_core`, `uta_ubundle`,
  Vulkan and glm, and `src/ubundle/` holds only section readers and writers.
  So a detector run at load would have to be a second copy in `urender`,
  beside the light model that already exists twice.
- **A strip is as bright as one of its lights** — this spec's call. § 8 says
  what lost.

## 4. Design

### 4.1 `LITE` gains strip fields

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 10; // 10 since UTA-0162

inline constexpr std::uint8_t STRIP_NONE = 0;     ///< an ordinary point light
inline constexpr std::uint8_t STRIP_LEADER = 1;   ///< carries its row's segment
inline constexpr std::uint8_t STRIP_ABSORBED = 2; ///< lit by its row's leader

struct Light {
    // ... every existing member, unchanged, then:
    std::uint8_t strip = STRIP_NONE;
    std::array<float, 3> stripFrom{}; ///< one end of the segment; zero unless a leader
    std::array<float, 3> stripTo{};   ///< the other end; zero unless a leader
};

}  // namespace uta::ubundle
```

A `Light` on the wire is UTA-0110 § 4.4's 44 bytes, then `strip` as `u8`,
`stripFrom` as three `f32` and `stripTo` as three `f32`: **69 bytes, fixed**.
`LIGHT_SIZE` in `src/ubundle/LightSection.cpp` becomes `69`. UTA-0156 § 4.5
later appended the level's brightness, making it `73`.

**Validation**, `MalformedData` on `read` and `InvalidArgument` on `write`,
added to UTA-0110 § 4.4's:

- a `strip` byte above `2`;
- a leader whose `stripFrom` equals its `stripTo`;
- a light that is not a leader with any non-zero `stripFrom` or `stripTo`
  component.

`ubundle` does not check that a leader's row exists. The baker guarantees it
(§ 4.2).

### 4.2 Detecting a row — `ubake`

A new `src/ubake/Strips.{h,cpp}`:

```cpp
namespace uta::ubake {

inline constexpr double STRIP_LINE_TOLERANCE = 16.0; ///< units from the row's line
inline constexpr double STRIP_GAP_REACH = 1.5;       ///< a gap's bound, in radii

/// Marks each row of `lights` as one strip, in place. `lights` is strictly
/// ascending by exportIndex, as LITE holds it.
void markStrips(std::vector<ubundle::Light>& lights);

}  // namespace uta::ubake
```

`bake` calls it on `actors.lights` after `buildActors` and before
`bakedLights` feeds `bakeLightProbes`, so the probes and the bundle see the
same strips.

**A row** is a set of three or more lights where all of these hold:

1. **Every field but `exportIndex`, `location` and the strip fields is
   equal** — the twelve bytes, `rotation` and the four bools.
2. **None is excluded.** Excluded: `effect` `LE_Spotlight` (12) or
   `LE_StaticSpot` (8); `type` `LT_BackdropLight`; `specialLit`; `brightness`
   `0`. The first two have a direction a segment does not carry. The rest are
   never drawn or never lit.
3. **They lie on one line.** The line runs through the two members furthest
   apart. Every member lies within `STRIP_LINE_TOLERANCE` of it.
4. **No gap is too wide.** Ordered by position along the line, each
   neighbour's distance along it is above `0` and at most `STRIP_GAP_REACH ×
   R`, with `R` UTA-0112 § 4.3's radius.

**Every row is maximal, and no light is in two.** Where two candidate rows
share a light, the one with more members is kept, and on a tie the one whose
lowest `exportIndex` is lower. The result is a function of the light list
alone, so a bake stays deterministic.

**Marking.** The member with the lowest `exportIndex` becomes
`STRIP_LEADER`, with `stripFrom` and `stripTo` the `location`s of the two end
members, lower `exportIndex` first. The rest become `STRIP_ABSORBED`. No
`location` moves.

`BAKER_REVISION` becomes `12`.

### 4.3 The light model

UTA-0112 § 4.3 gains one rule, in `ubake::lightAt` and `shaders/light.glsl`
together.

- **A strip leader.** With `a = stripFrom`, `b = stripTo`, `s = b − a`, and
  `t = clamp(((x − a) · s) / (s · s), 0, 1)`, the leader is evaluated as a
  point light at `p = a + t × s`, every other factor unchanged. `LE_Cylinder`'s
  horizontal distance is `x`'s from `p`.
- **An absorbed light puts nothing anywhere.** `ubake::lightAt` returns zero
  for one. `urender::directLights` and `ubake::bakedLights` leave them out, so
  the shader never receives one and `gpu::Light` needs no flag for it.

`gpu::Light` in `src/urender/ShaderTypes.h` and `types.glsl`'s `Light` gain
`span` (`vec3`, `b − a` for a leader, zero otherwise) at offset `64` and a
reserved `float` at `76`, so the record is 80 bytes. `drawnLights` uploads a
leader with `location = stripFrom`.

`ubake`'s probe gather casts its shadow ray toward `p` rather than toward
`location`.

### 4.4 Cluster culling

`cluster.comp` keeps its sphere test for a light whose `span` is zero. A light
with a non-zero `span` reaches a cluster where the segment from `location` to
`location + span` meets the cluster's box grown by `R` on every side — a slab
test. That is conservative at the grown box's corners, as `cluster.comp`'s
sphere already is for a spotlight.

### 4.5 Shadows

A leader's shadow is cast from its segment's midpoint `(a + b) / 2`, with reach
`R + |b − a| / 2`. `shadowTileSize`, `shadowViewProj`, `boxReaches` and
`shadows.glsl`'s face choice all use that centre and reach. `shadowFacesOf`
still gives six.

## 5. Invariants

- **INV-1** — `LITE` round-trips a leader, an absorbed light and a point light
  with § 4.1's bytes, and refuses each of § 4.1's three invalid records on both
  `read` and `write`.
  *Test:* `tests/unit/BundleActorsTest.cpp`, the golden-bytes and refusal
  cases.
  *Breaks when:* the strip fields are written in another order; a `strip` byte
  of `3`, a leader with equal ends, or a point light with a non-zero end is
  accepted.

- **INV-2** — `markStrips` makes one strip of an uneven, slightly crooked row:
  three identical `LE_Cylinder` lights, radius byte `10` (`R = 275`), with
  gaps of `250` and `300` units along the line and the middle light `10` units
  off it. The lowest `exportIndex` leads, its ends are the two outer lights,
  and the other two are absorbed.
  *Test:* `tests/unit/BakeStripsTest.cpp`.
  *Breaks when:* even spacing is required (the gaps differ); the gap bound is
  `R` rather than `1.5 × R` (`300` exceeds `275`); the line tolerance is under
  `10`.

- **INV-3** — `markStrips` marks no strip when INV-2's row differs in exactly
  one respect: two lights only; a gap of `1.5 × R + 1`; the middle light `17`
  units off the line; one light's `brightness` different; `effect`
  `LE_Spotlight`.
  *Test:* `tests/unit/BakeStripsTest.cpp`, one section per respect, each
  starting from INV-2's row so that respect alone is what refuses it.
  *Breaks when:* any one of § 4.2's rules is dropped.

- **INV-4** — a row of five marks as one strip, and two rows sharing a light
  keep only the larger.
  *Test:* `tests/unit/BakeStripsTest.cpp`.
  *Breaks when:* a row is split into two strips, or one light ends up in two.

- **INV-5** — `ubake::lightAt` for a leader at `x` equals `lightAt` for the
  same light as a point light at § 4.3's `p`, for effects `LE_None`,
  `LE_NonIncidence` and `LE_Cylinder`, at `x` whose `t` clamps to `0`, lies
  between, and clamps to `1`. An absorbed light gives zero.
  *Test:* `tests/unit/BakeLightModelTest.cpp`.
  *Breaks when:* the midpoint or `location` is used for `p`; `t` is not
  clamped; an absorbed light lights.

- **INV-6** — UTA-0014 INV-6 holds with strip cases: its case table gains
  leaders of each non-spot effect, placed so `t` clamps at both ends and falls
  between.
  *Test:* `tests/device/RenderLightParityTest.cpp`.
  *Breaks when:* `light.glsl`'s nearest point differs from `ubake::lightAt`'s.

- **INV-7** — `drawnLights` and `bakedLights` omit absorbed lights; a leader
  is drawn with `location = stripFrom` and `span = stripTo − stripFrom`.
  *Test:* `tests/unit/RenderLightsTest.cpp` and
  `tests/unit/BakeLightProbesTest.cpp`.
  *Breaks when:* an absorbed light is drawn or gathered, doubling the row's
  light.

- **INV-8** — a strip lights a pixel within `R` of its far end but further
  than `R` from its leader's `location`.
  *Test:* `tests/device/RenderLightingTest.cpp`, a new case.
  *Breaks when:* `cluster.comp` tests the sphere at `location` for a strip, so
  the cluster never lists the light.

- **INV-9** — a leader's shadow centre is its segment's midpoint and its reach
  `R + |b − a| / 2`, in `shadowTileSize`, `shadowViewProj` and `boxReaches`.
  *Test:* `tests/unit/RenderShadowsTest.cpp`.
  *Breaks when:* `location` or `R` alone is used.

- **INV-10** — the golden bake is re-recorded under `BAKER_REVISION` `12`.
  *Test:* `tests/unit/BakeGoldenTest.cpp`.
  *Breaks when:* what the baker writes changes with no bump, or the bump lands
  without re-recording.

## 6. Failure modes

- **A row the rules miss** stays a string of point lights, as today. Nothing
  breaks; it looks as it did.
- **Two separate lamps joined into one strip** light the floor between them.
  `STRIP_GAP_REACH` is what limits that; § 7's measurement is where it is
  checked.
- **A long strip's shadow is cast from one point.** Near its ends, a shadow
  falls as if from its middle. Accepted for the first iteration; the bake's
  probes still shadow toward the nearest point.
- **A point exactly on a strip's segment has no direction to the light.** Its
  nearest point is computed, so float and double can fall on either side of
  UTA-0112 § 4.3's `d == 0` rule there, and the two copies may disagree. A
  surface does not lie on a light's line in a real map; INV-6's strip cases
  keep their points off it.
- **A long strip fills more clusters.** A cluster that overflows drops its
  furthest lights, as UTA-0014 § 6 already handles.
- **A bundle from before this item** is refused by its format version (§ 14).

## 7. Tests

All carry the `unit` label but INV-6 and INV-8, which carry `device`.

- INV-1 — `tests/unit/BundleActorsTest.cpp`, extended.
- INV-2, INV-3, INV-4 — `tests/unit/BakeStripsTest.cpp`, new.
- INV-5 — `tests/unit/BakeLightModelTest.cpp`, extended.
- INV-6 — `tests/device/RenderLightParityTest.cpp`, extended.
- INV-7 — `tests/unit/RenderLightsTest.cpp` and
  `tests/unit/BakeLightProbesTest.cpp`, extended.
- INV-8 — `tests/device/RenderLightingTest.cpp`, extended.
- INV-9 — `tests/unit/RenderShadowsTest.cpp`, extended.
- INV-10 — `tests/unit/BakeGoldenTest.cpp`, re-recorded.

Each extended test is seen to fail against the code before this item.

**Measurement, not asserted.** After the code lands: re-bake DM-Deck16][,
draw the PlayerStart views in `ut-ants-uta0156/cams.txt` with
`tools/ut-shot`, and run
`ut-ants-uta0156/compare.py` against the original's frames. Record the mean
displayed luma and block RMS beside UTA-0156's. Re-fit `EXPOSURE` only if the
fit moves, by UTA-0156's rule.

## 8. Alternatives considered (and rejected)

- **Detect rows when a bundle loads.** Lost: `urender` may not link `ubake`,
  so it would be a second detector beside two copies of the light model.
- **A separate `STRP` section listing each strip's members.** Lost: a second
  list to keep in step with `LITE`, for facts that belong to each light.
- **Replace a row's lights with one light in `LITE`.** Lost: the bundle would
  stop holding the map's own lights, which a later editor (UTA-0034) edits one
  by one.
- **Sum the row's brightness along the strip.** Lost for the first iteration:
  the measured rows mostly barely overlap, so one light's brightness is close
  to what the original shows along the fixture. § 7 measures it.
- **One shadow cube per absorbed light.** Lost: shadow cost grows with every
  member, for a difference seen only near a strip's ends.
- **Integrated area lights (linearly transformed cosines).** Lost: not the
  cheapest method, which § 3 requires first.

## 9. Out of scope

- Soft light edges — tracked by UTA-0156.
- Light fixtures with real depth — tracked by UTA-0157.
- Rows of spotlights — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleActorsTest.cpp` |
| INV-2 | `tests/unit/BakeStripsTest.cpp` |
| INV-3 | `tests/unit/BakeStripsTest.cpp` |
| INV-4 | `tests/unit/BakeStripsTest.cpp` |
| INV-5 | `tests/unit/BakeLightModelTest.cpp` |
| INV-6 | `tests/device/RenderLightParityTest.cpp` |
| INV-7 | `tests/unit/RenderLightsTest.cpp`, `tests/unit/BakeLightProbesTest.cpp` |
| INV-8 | `tests/device/RenderLightingTest.cpp` |
| INV-9 | `tests/unit/RenderShadowsTest.cpp` |
| INV-10 | `tests/unit/BakeGoldenTest.cpp` |
| How strips look against the original | **nothing** — § 7's measurement is run by hand |

## 11. Cross-doc impact

- `docs/specs/UTA-0110-lights-and-placements.md` § 4.4 — the `LITE` record.
- `docs/specs/UTA-0112-baked-light-probes.md` § 4.3 — the strip rule.
- `docs/specs/UTA-0014-vulkan-draw-path.md` § 4.6 — cluster culling of a
  strip, and its shadow centre.
- `CLAUDE.md` § Where this project is — the bundle format version.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0162-strip-lights-loop-log.md`.

## 13. Migration / compatibility

`FORMAT_VERSION` becomes `10`. `ubundle::read` refuses any other version, so
every map baked before this item must be baked again.
