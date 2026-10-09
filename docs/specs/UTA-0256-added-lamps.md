<!-- ants-spec-format: 1 -->
# UTA-0256 — added lamps for rooms the accurate light leaves too dark

**Status:** accepted (2026-10-09), unreviewed. Contract reviews are cancelled for this
project (`CLAUDE.md` § Contract reviews); no review runs unless the user asks.
**Kind:** feature.
**Source:** ROADMAP UTA-0256 (user-request-2026-09-30; decisions of
2026-10-08).

**Layman:** where a room is far darker than in the original game, the map's
recipe adds a copy of one of the map's own lamps, flame and holder included.
A graphics setting takes every added lamp away again.

## 1. Goal

A map's recipe can list added lamps. Each copies one of the map's own lights
and the fitting that holds it, moved to a new place. The baker writes them to
a new `LAMP` section, gives them shadows and bounce light of their own, and
keeps everything they add apart from the map's own light. The renderer draws
them, or, with the setting off, draws the map exactly as a bake without them.

## 2. Problem

1. **Accurate light leaves four views of DM-ArcaneTemple far darker than the
   original.** UTA-0256's 2026-10-08 note: baker revision 46 against the
   original with `ut-compare` on UTA-0292's nav views, mean grey ours over
   the original's, views 3 to 6 at 0.35 to 0.65, with 79/85/32/35% of our
   pixels under 16 against 41/53/11/9% of the original's. The original's
   light there is its zones' flat ambient fill, which UTA-0292 replaced with
   real bounce. *Command:* `~/.cache/uta-scratch/u256/measure.py` over
   `~/.cache/uta-scratch/u256/cmp-arcane`.
2. **The map's fittings are level geometry, not meshes.** `urender` draws no
   actor mesh. A DM-ArcaneTemple torch is a small holder brush (`Wrcka3` and
   `az-floor` faces) and three crossed flame sheets textured `ancflame2`,
   beside a plain `Light` actor; UTA-0263 turns the sheets into one FLAM
   flame. *Command:* `ut-dump --install <UT> --surface-list` on the map,
   surfaces grouped by `brush` within 80 units of three flame sheets
   (scratch script in `~/.cache/uta-scratch/u256/dump/`). So "copy a fitting"
   means copying named brushes' surfaces and flames.
3. **Light that is baked cannot be switched off unless it is kept apart.**
   Direct light is drawn per frame from the light list (`urender/Lights.h`),
   but level shadows are baked per polygon and light (SMSK, UTA-0326) and
   bounce is baked into probe cubes (LPRB, UTA-0112). A probe cube that mixed
   added lamps in could not be taken apart again.
4. **No per-map list of additions exists.** The recipe (UTA-0113) is the one
   per-map file kept outside the install, and its § 9 already expects later
   versions to carry more than materials.

## 3. Scope decisions (agreed with the user)

- **A lamp only where a room is under half the original's brightness and the
  original is not itself dark there** — the user, 2026-10-08.
- **A lamp copies a fitting the map already uses** — the user, 2026-10-08. So
  a lamp with no fitting is refused (§ 4.1).
- **Each map lists its added lamps, and a graphics setting turns them all
  off** — the user, 2026-10-08.
- **The list lives in the recipe** — the session's call: it is already the
  per-map file outside the install, it already renames the bake, and
  UTA-0331's added shapes can join it as a further section.
- **With the setting off the frame is exactly the frame of a bake without the
  lamps** — the session's call, so the setting is a true "as the map was
  made". It is why the copied fitting casts no shadow and blocks no ray in any
  bake (§ 4.3): a fitting that shadowed would leave its shadow behind.
- **A copied fitting has no collision** — the session's call. Fittings are
  small and wall-mounted; UTA-0331 brings collision for added shapes.

## 4. Design

### 4.1 The recipe's lamp section — `urecipe`

`RECIPE_VERSION` becomes 2. A recipe may hold any number of `[lamp <name>]`
sections, up to `LAMP_LIMIT` (64):

```ini
[lamp east-hall]
light = Light23                      # the map's Light actor it copies
fitting = Brush95 Brush99 Brush100   # brushes whose surfaces it copies
at = -1100 900 200                   # where the copy's light stands
yaw = 16384                          # optional; 65536 to a turn
```

- `<name>` is 1 to 32 of `a-z`, `0-9` and `-`, unique in the recipe.
- `light`, `fitting` and `at` are required; `yaw` defaults to 0 and is an
  integer in [0, 65535]. `fitting` names one or more object names, no name
  twice. `at` is three finite numbers.
- A recipe holding a lamp must carry `[map] sha256`: actor names and
  coordinates mean nothing in another file of the same name.
- `urecipe::Recipe` gains `std::vector<AddedLamp> lamps`, in file order;
  `AddedLamp {name, light, fitting, at, yaw}`. `parse` and `write` round-trip
  it. Every refusal is `MalformedData` naming the line, as today.
- `bakeDigest` of a recipe with no lamp is unchanged. With lamps it is the
  SHA-256 of `uta-recipe-bake-2\n`, the version-1 digest input, then each
  lamp's fields in file order in a fixed encoding, so any lamp edit renames
  the bake.

### 4.2 The `LAMP` section and the light index — `ubundle`

`FORMAT_VERSION` becomes 24. `Bundle::lamps` is
`std::optional<std::vector<AddedLamp>>`, written after `SMSK`, absent when the
recipe adds none:

```cpp
struct AddedLamp {
    Light light;                  // the template's record, moved (SS 4.3)
    Geometry shape;               // the fitting's drawn surfaces, world space
    std::vector<Flame> flames;    // its flames; each `light` is -1
};
```

- **One light index space.** Index `i < |LITE|` is LITE's; index
  `|LITE| + k` is `lamps[k].light`. `MaskPair::light` may now name the
  second range; LITE itself is untouched, so the map's own lights keep their
  indices and their order. A lamp's flames store `light = -1` on the wire;
  the renderer ties each to its lamp's index.
- **Refused on read and on write:** a lamp whose `shape` has no triangle and
  whose `flames` is empty (no fitting); a `MaskPair::light` at or past
  `|LITE| + |lamps|`; any check `Geometry`, `Light` or `Flame` already makes
  on its own section.
- **LPRB gains one array.** `LightProbes::added` is empty, or one cube per
  probe in `probes`' order: the bounce of the added lamps alone. It is empty
  exactly when `lamps` is absent.

### 4.3 Building a lamp — `ubake`

After `buildActors`, for each recipe lamp, in file order:

1. `light` must name a `Light` placement whose record `litDirectly` accepts;
   else refuse, naming the lamp. Its location is `L`.
2. The transform is `p' = at + Rz(yaw) (p - L)`, about the vertical axis.
   The copy's `Light` is the template's record with `location = at` and
   `yaw` added to its rotation's yaw. Nothing else of the record changes.
3. Each `fitting` name must be a brush actor of the map that owns at least
   one drawn surface or flame sheet; else refuse, naming the lamp and the
   brush. Its drawn surfaces' triangles, as GEOM builds them, are copied and
   transformed into `shape`, normals turned by `Rz(yaw)`, each vertex's zone
   being `zoneAt` the transformed point. Its flame sheets' FLAM records are
   copied, `base` transformed, and stored in `flames`.
4. **Bakes see the lamp's light and never its fitting.** SMSK pairs the
   lamp's light with level polygons as it pairs any light (UTA-0326 § 4.1),
   occluded by the level alone. The pairs of the map's own lights are those a
   bake without the lamps would make. `shape` is in no ray set: no shadow, no
   occlusion, no bounce off it.
5. **Probes bake two layers.** `probes` is gathered with the map's lights,
   sky and emission exactly as without lamps; `added` with the lamps' lights
   alone, no sky, no emission. Light adds, so on is base plus added.

`BAKER_REVISION` becomes 47.

### 4.4 The setting and the renderer — `urender` and the apps

- `urender::Config::addedLamps`, default `true`. The flag
  `--no-added-lamps` sets it false in `ut-ants`, `ut-shot` and `ut-ref`.
  A settings screen, when one exists, carries the same field.
- **On:** the light list is LITE then each lamp's light; every SMSK pair
  is used; lamp flames are drawn, tied to their lamp's index; each `shape` is
  drawn as a mover's geometry is, at the identity placement, lit per frame by
  the light list and the shadow maps; probes read `cube + added`.
- **Off:** the light list is LITE alone; pairs naming the second range are
  skipped; no lamp flame or shape is drawn; probes read `cube`.

### 4.5 ArcaneTemple's lamps — `recipes/`

`recipes/DM-ArcaneTemple.recipe` ships with the lamps views 3 to 6 need,
each copying a torch the map already has. Where each goes is chosen by hand,
by § 3's rule, and checked by § 7's measurement.

## 5. Invariants

- **INV-1** — a recipe with lamps round-trips through `parse` and `write`,
  and `parse` refuses: a missing `light`, `fitting` or `at`; an unknown lamp
  key; a repeated lamp name; a bad name; a `yaw` out of range; a fitting name
  given twice; a lamp with no `[map] sha256`; a sixty-fifth lamp.
  *Test:* `tests/unit/RecipeFormatTest.cpp`.
  *Breaks when:* a lamp key is checked on `parse` only; the sha256 rule is
  dropped.

- **INV-2** — `bakeDigest` of a lamp-free recipe equals its version-1 value,
  and changing any one field of any lamp changes the digest.
  *Test:* `tests/unit/RecipeFormatTest.cpp`, against a recorded digest.
  *Breaks when:* the tag string changes for every recipe; a field is left out
  of the encoding.

- **INV-3** — `LAMP` and LPRB's `added` round-trip, and `read` and `write`
  each refuse § 4.2's cases.
  *Test:* `tests/unit/BundleLampsTest.cpp`, new.
  *Breaks when:* the fitting check runs on one path only; a pair past the
  second range is accepted.

- **INV-4** — in a fixture room with one light, a holder brush and a flame
  sheet beside it, a lamp at `at` with `yaw = 16384` holds the holder's
  triangles moved and turned a quarter-turn about `L`, one flame with its base
  so moved, and the template's light record with only its location and yaw
  changed.
  *Test:* `tests/unit/BakeLampsTest.cpp`, new, map from
  `UnrealPackageBuilder`.
  *Breaks when:* the turn is about the origin rather than `L`; normals are not
  turned; a template field is lost.

- **INV-5** — a lamp is refused, naming it, when its `light` names no light
  or one `litDirectly` rejects, and when a `fitting` brush is absent or
  yields neither surface nor flame.
  *Test:* `tests/unit/BakeLampsTest.cpp`.
  *Breaks when:* an empty fitting is silently skipped, giving a lamp with no
  fitting.

- **INV-6** — baking the fixture with and without the lamp gives byte-equal
  LITE, GEOM, FLAM, OCCL, LPRB `probes`, and SMSK pairs of every map light;
  with the lamp, `added` is non-zero at the probe nearest it.
  *Test:* `tests/unit/BakeLampsTest.cpp`.
  *Breaks when:* the fitting enters a ray set; the lamp's light joins the
  base probe gather.

- **INV-7** — with `addedLamps` false, the fixture with its lamp renders the
  same pixels as the fixture baked without it; with it true, the floor under
  the lamp is brighter and the flame and holder are drawn.
  *Test:* `tests/device/RenderLampsTest.cpp`, new, at `--light-time 0`.
  *Breaks when:* a pair of the second range is used while off; `added` is read
  while off; a shape is drawn while off.

## 6. Failure modes

- **A player's own recipe for a map hides the shipped one**, lamps included
  (UTA-0113 § 4.3, first match wins). A player who wants both copies the
  lamp sections.
- **A fitting copied onto a wall that is not flat** may float or sink: the
  copy keeps the source's shape, cut by the source's walls. Placement is
  checked by eye when a lamp is added.
- **A lamp near a mover** gets the mover's shadow through the shadow maps, as
  any light does; its fitting casts none.

## 7. Tests

The device test carries the `device` label; the rest carry `unit`.

- INV-1, INV-2 — `tests/unit/RecipeFormatTest.cpp`.
- INV-3 — `tests/unit/BundleLampsTest.cpp`, new.
- INV-4, INV-5, INV-6 — `tests/unit/BakeLampsTest.cpp`, new.
- INV-7 — `tests/device/RenderLampsTest.cpp`, new.
- `tests/unit/BakeGoldenTest.cpp`, re-recorded for the format and revision.

Each new test is seen failing against the code before its rule exists.

**Measured by hand after the code lands:**

1. Bake DM-ArcaneTemple with its shipped recipe. Re-run UTA-0256's
   `ut-compare` measurement on the ten nav views. Each of views 3 to 6 must
   reach at least half the original's mean grey; record every view's ratio in
   UTA-0256's body. No other view may fall.
2. The same with `--no-added-lamps` gives r46's figures for every view.

## 8. Alternatives considered (and rejected)

- **Add the lamps to LITE.** LITE is strictly ascending by `exportIndex`
  (UTA-0110), and a lamp has no export of its own; every tool reading LITE
  would then see lights the map does not have.
- **Copy the fitting into GEOM.** GEOM is batched by material and keeps no
  polygon's brush, and the setting would then have to remove triangles from
  inside batches and SMSK charts.
- **Bake the map twice, with and without lamps.** Doubles every bake and every
  bundle for one setting; light adds, so a second probe layer is enough.
- **Give the lamps no bounce.** Simpler, but a lamp lighting only what it
  sees directly reads as a stage light against the map's real bounce.
- **A separate per-map lamps file.** A second per-map file beside the recipe,
  with its own lookup and its own share of the bake name, for nothing the
  recipe cannot carry.

## 9. Out of scope

- Measuring and lamping any map but DM-ArcaneTemple. UTA-0334's maps are
  measured once baked.
- Added shapes that are not lamps, and collision for added shapes —
  UTA-0331.
- An editor for placing lamps — UTA-0034.
- A settings screen.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2 | `tests/unit/RecipeFormatTest.cpp` |
| INV-3 | `tests/unit/BundleLampsTest.cpp` |
| INV-4, INV-5, INV-6 | `tests/unit/BakeLampsTest.cpp` |
| INV-7 | `tests/device/RenderLampsTest.cpp` |
| Views 3 to 6 reach half the original | **nothing** — § 7's step 1, run by hand |
| A copied fitting sits right on its wall | **nothing** — checked by eye on placing |

## 11. Cross-doc impact

- `docs/specs/UTA-0113-recipe-format.md` § 9 — the lamp section is the first
  later version it foresaw; a pointer.
- `docs/specs/UTA-0326-baked-shadow-mask.md` § 4.2 — `MaskPair::light` may
  name the lamp range.
- `docs/specs/UTA-0112-baked-light-probes.md` — LPRB's `added` layer.
- `CLAUDE.md` § Standing facts — the bundle format and baker revision.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0256-added-lamps-loop-log.md`.

## 13. Migration / compatibility

§ 4.2 moves the bundle format and § 4.3 the baker revision, so every map is
baked again. A version-1 recipe still parses; a version-1 game refuses a version-2
recipe by its header.
