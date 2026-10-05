<!-- ants-spec-format: 1 -->
# UTA-0277 — per-tile variation for natural textures

**Status:** accepted (2026-10-05), unreviewed. Contract reviews are cancelled for this
project (`CLAUDE.md` § Contract reviews); no review runs unless the user asks.
Amended by implementation the same day (the loop log's `impl` row): answers
apply where a bundle loads, parallax is not an exclusion, and the scores and
limits are the ones built and fitted.
**Kind:** enhancement.
**Source:** ROADMAP UTA-0277 (in-session-2026-10-02, split from UTA-0180;
the unsure-texture rule, user 2026-10-05).
**Pairs with:** UTA-0089 (the two-copy blend this reuses), UTA-0180 (the
large-scale half, shipped).

**Layman:** rock, dirt and plaster stop showing the same patch over and over,
while bricks, planks, trims, panels and signs stay exactly where the mapper put
them; where the game cannot tell which a texture is, it leaves it alone and asks
the player once.

## 1. Goal

A texture judged natural — no lines or repeating features of its own — is
drawn so its repeats no longer line up: each repeat is sampled at a hashed
offset and blended across seams, as UTA-0089 § 4.4 does for water. Every other
texture draws exactly as today. The judgement is made at bake time from the
picture and stored per material in the bundle. A per-user answer file
overrides it where the bundle is loaded. A texture the bake cannot judge is
drawn unchanged and listed, with a view of the map and of the texture, for the
user to answer.

## 2. Problem

1. A natural texture repeated over a large wall or floor shows a visible grid.
   UTA-0180 shipped the half that moves no picture (`variation.glsl`'s
   `variationAt`, a slow brightness change); the repeats still line up.
2. Moving the picture is wrong for a texture drawn to fit: a trim's band, a
   panel's frame, a sign's text, and a regular pattern — bricks, planks, tiles —
   whose courses the blend would break.
3. The texture's name does not decide which it is. UTA-0180's census
   (`~/.cache/uta-scratch/u180/groups.py` over UTA-0105's map dumps): 26.6% of
   surfaces whose texture is in `Textures/` have a name in several groups, 11.4%
   none, and the "base" group mixes walls, trims and panels.

## 3. Scope decisions (agreed with the user)

1. **Unsure means unchanged** (user, 2026-10-05). A texture the bake cannot
   judge is drawn as today.
2. **The user answers the unsure ones** (user, 2026-10-05): "we must find a way
   for you to ask me what it is. I will probably need a view of the map and a
   view of the respective texture." § 4.5 is that way.
3. **Cheapest methods first** (user, 2026-09-14). The blend is UTA-0089's two
   samples; no texture synthesis.

Every other choice below is this spec's, made from the measurements in § 4.2.

## 4. Design

### 4.1 The material's tile kind — bundle format 22

`ubundle::MaterialRecord` gains two fields, written and read with `MATS` after
its fire look:

```cpp
enum class TileKind : std::uint8_t {
    Fixed = 0,    // drawn as today: judged structured, or flat, or excluded
    Shuffle = 1,  // drawn with § 4.4's two-copy blend
    Unsure = 2,   // drawn as Fixed; listed for the user (§ 4.5)
};
TileKind tileKind = TileKind::Fixed;
std::array<std::byte, 32> tileHash{};  // § 4.5's picture hash; all zero where § 4.2 step 1 excludes it
```

`FORMAT_VERSION` 21 → 22, `BAKER_REVISION` 39 → 40. A tile kind byte above 2
refuses the section, as `MATS`'s liquid byte does. `shapeOf` in `Frame.cpp`
samples the kind (UTA-0014's re-upload rule), and `ShaderTypes.h`'s `Material`
carries it as a `uint`, mirrored in `types.glsl`.

### 4.2 The bake's judgement

Read from the material's base level as `umat::resolve` gives it, before any
stretch or upscale, in this order — the first that applies decides:

1. **Excluded → Fixed, and the hash all zero:** a liquid, flame or fire
   material (they carry their own look), a masked variant (moving it moves its
   holes), or one with no surface in the level whose texture coordinates span
   two repeats in both `u` and `v` — nothing there repeats to hide. Parallax is
   not an exclusion: every generated material carries `GENERATED_PARALLAX_DEPTH`
   (`umat/Generate.h`), so excluding it excluded every texture; § 4.4 marches
   the blended height instead.
2. **Flat → Fixed:** luma variance under 4 (spread under 2) at 64 × 64.
3. **The two scores**, on luma `(54 R + 183 G + 19 B) / 256` box-resampled to
   128 × 128, less its 32-texel wrap-around box mean taken along rows then
   columns (blotches removed, so only sharp features count):
   - *lines* = the larger of (variance of row means + variance of column means)
     and (the same over the two wrap-around diagonal families), over the
     picture's variance, × 64 — whole-width bands: mortar courses, plank edges,
     trim bands, hazard stripes, herringbone. A noise picture scores about 1.
   - *spots* = the highest wrap-around autocorrelation of the high-passed
     picture, halved to 64 × 64 by 2 × 2 means, at any offset further than 2
     texels there (4 at 128), as a share of its energy — a grid of rivets,
     studs or tiles that *lines* misses.
   - Shuffle when *lines* < `LINES_SHUFFLE` and *spots* < `SPOTS_SHUFFLE`;
     Fixed when *lines* ≥ `LINES_FIXED` or *spots* ≥ `SPOTS_FIXED`; else Unsure.

The answers (§ 4.3) come after all three, where the bundle loads.

**The limits** are `LINES_SHUFFLE` 5, `LINES_FIXED` 12, `SPOTS_SHUFFLE` 0.5 and
`SPOTS_FIXED` 0.75, fitted with a prototype of these exact scores
(`~/.cache/uta-scratch/u277/fit/proto.py`) on the reference install's 400
most-used textures, 288 distinct pictures, labelled by eye from contact sheets
sorted by *lines*. All 56 pictures under the Shuffle limits are natural (dirt,
rock, grass, plaster, rust). Natural pictures run on to *lines* 12 and beyond
(wood grain, strata); panels and bricks start near 9; rivet plates sit at
*lines* 5.6 and *spots* 0.33, below both scores' Fixed limits — so the band
between is Unsure by design. By surfaces using those 400 textures: about 17%
Shuffle, 30% Unsure, 52% Fixed. An FFT high-pass at 1/32 cycles a texel, tried
first, read *lines* about 1.2 to 1.6 times lower, which is why its 5 and 9 did
not carry over. Real bakes at revision 40: DM-Fetid 0 Shuffle and 2 Unsure of
24 materials; DM-Deck16][ 2 Shuffle and 4 Unsure of 32.

The scores are sums, products and quotients of doubles in a fixed order, with
no library function a compiler rounds its own way (no FFT, whose sines would
be one), so every compiler and worker count gets the same verdict (the bake's
existing rule). The four limits are `ubake::TileLimits`, an argument of the
judging function and of `detail::bake`, defaulting to the values above, so a
test can force each outcome on a fixed picture.

### 4.3 Where the answers apply

**Where a bundle is loaded, not in the bake.** The viewer, `ut-shot` and
`ut-bench` each call `ubundle::applyTileAnswersFile` after reading a bundle: a
material whose `tileHash` an answer names takes that answer's kind, and a
material whose hash is all zero takes none. An answer therefore takes effect
the next time a map is loaded, with no re-bake, and the bake's name does not
change. The answers cannot feed the name: `Name.h` builds it before the bake,
from the map and its imports, and a picture's hash exists only once the bake
has decoded it.

### 4.4 Drawing — `variation.glsl`

UTA-0089's two-copy blend moves out of `water.glsl` into `variation.glsl` as
`tileBlendAt` (the two offsets and how far between them) and `tileWeight` (the
contrast-preserving weight); water calls both with its arithmetic unchanged.
For a Shuffle material in `scene.frag`:

- The offset pair is computed once per pixel from the undisplaced `uv`, at
  UTA-0089's `TILE_VARIATION_REPEATS` and `TILE_OFFSETS`, with a seed from the
  material's index — as water's is.
- **Every map the material samples takes the same two offsets and the same
  weight** — base, normal and emission — so the lighting stays on the picture
  it belongs to. The weight is the base map's contrast-preserving one, as
  water's is.
- **The parallax march reads the two copies' height**, blended by the noise's
  own weight (the contrast term needs both base samples, which the march does
  not take), so its depth is the picture's drawn there.
- **No fade toward the mean.** Water fades its picture at distance; a wall
  would flatten to one colour, which is worse than repeats.
- A feature `Feature::TileShuffle`, specialization constant 5 as
  `TileVariation`'s is 3, from Medium: measured at ultra, 3840 × 2160, on
  DM-Deck16]['s fifteen reference views on the RX 6600, quiet machine, with
  every judged material answered shuffle (14, against the bake's 2), the median
  frame went from 8.0 ms to 9.0 ms, twice each. A worst case past the 3% that
  would keep it on every tier, as `TileVariation`'s 7% to 9% was. Below it,
  Shuffle draws as Fixed.

### 4.5 Asking the user

**The answers file**, `<fs::dataDirectory()>/tile-kinds.txt` — the folder
UTA-0113's player recipes use. One answer a line:

```
# <comment>
<64 hex digits> shuffle|fixed  [# <package.group.name>]
```

The hash is SHA-256 over the base level as resolved: width and height as
little-endian `uint32`, then its RGBA bytes. Content, not name, so one picture
in two packages takes one answer, and two pictures sharing a name take two.
Blank and `#` lines are skipped, and a line repeating an earlier answer is not
a conflict. A malformed line, or a hash given two different answers, is a
warning printed where the bundle loads and is ignored; the load does not fail
on it.

**The question list.** The bake report gains, on `written` and `over-budget`:

```json
"tileQuestions": [{"material": "<id>", "hash": "<64 hex>", "lines": 0.0,
                   "spots": 0.0, "surfaces": 0,
                   "view": {"at": [0,0,0], "normal": [0,0,0], "extent": 0.0}}]
```

most surfaces first, the material id breaking a tie. `view` is the largest
surface wearing it, seen at its largest polygon: that polygon's centre, the
surface's unit normal and the largest side of the polygon's box, in world
units. A surface of separate pieces has its middle in whatever lies between
them, which drew the wrong surface on DM-Deck16][. A surface with no area or
no normal does not count.

**The ask.** `scripts/tile-questions.py <report.json> <bundle> <out dir>`
skips the questions the answers file already answers and writes, per question
left, `<n>-texture.png` (the base level as the bundle stores it, scaled up to
at least 512 texels across, nearest-neighbour) and `<n>-view.png` (`ut-shot`
from a camera on the surface's normal at 1.5 × its extent but no further than
192 units, looking at its centre — further, and a large floor's camera leaves
the level), and an `answers.txt` with `?` where each answer goes. An unedited
`?` line is malformed, so appending one answers nothing. A session shows the
user both pictures — this is identifying a thing, not judging a look — and
appends the answered lines to the answers file.

## 5. Invariants

- **INV-1** — A material's tile kind is a pure function of its base level's
  bytes, its exclusions and the answers file: the same inputs give the same
  kind on every compiler and at any worker count.
  *Test:* `tests/unit/TileKindTest.cpp` — a fixed picture's scores equal
  recorded bits, and the standard fixture baked at 1 and 8 workers gives the
  same kinds and hashes; the golden bake's digest on GitHub's three legs.
  *Breaks when:* the judgement reads a float the compilers round differently,
  or depends on the order materials finish.
- **INV-2** — A synthetic picture of whole-width bands is Fixed, a seeded
  value-noise picture is Shuffle, and a grid of spots with flat ground between
  is not Shuffle.
  *Test:* `tests/unit/TileKindTest.cpp`, each fixture made in the test, none
  masked, flat or liquid, so only § 4.2 step 3 decides it; with limits under
  which *lines* decides nothing, the grid is Fixed and the noise Shuffle.
  *Breaks when:* *lines* or *spots* is not computed, or a threshold is inverted.
- **INV-3** — An answer wins over the scores both ways: a band picture answered
  `shuffle` is Shuffle, a noise picture answered `fixed` is Fixed.
  *Test:* `tests/unit/TileKindTest.cpp`, through `ubundle::applyTileAnswers`.
  *Breaks when:* answers are keyed on the name, or not applied where the
  bundle loads.
- **INV-4** — An Unsure material, and any material at a tier below
  `Feature::TileShuffle`, draws the same picture as Fixed.
  *Test:* `tests/device/RenderTileShuffleTest.cpp` — one tiled quad at render
  scale 1: Unsure matches Fixed exactly at High, Shuffle matches Fixed exactly
  at Low, and Shuffle at High differs from Fixed.
  *Breaks when:* the shader branches on `tileKind != Fixed`, or the feature
  constant is not consulted.
- **INV-5** — A Shuffle material's normal map takes the base map's offsets: a
  quad whose base and normal carry the same feature at the same texel draws its
  lit feature where its colour feature is.
  *Test:* `tests/device/RenderTileShuffleTest.cpp` — three lit frames (both
  features, colour only, neither); the pixels the tilt changes lie on the
  pixels the colour changes, and Fixed puts the colour elsewhere.
  *Breaks when:* any map is sampled at the unshuffled `shadingUv`.
- **INV-6** — An answer changes the kind of exactly the materials whose picture
  hash it names, with no re-bake; a material § 4.2 step 1 excluded, whose hash
  is all zero, takes no answer.
  *Test:* `tests/unit/TileKindTest.cpp` on `ubundle::applyTileAnswers`.
  *Breaks when:* the zero hash is answerable, or answers match by anything but
  the hash.
- **INV-7** — `tileKind` and `tileHash` round-trip through `ubundle::write` and
  `ubundle::read`, and a tile kind byte above 2 refuses the section.
  *Test:* `tests/unit/BundleMaterialTest.cpp`.
  *Breaks when:* the field is left out of the writer or the reader, or the
  reader accepts any byte.
- **INV-8** — A malformed answers line is a warning, not a failure: every
  well-formed line applies, and the warnings name the bad lines.
  *Test:* `tests/unit/TileKindTest.cpp`.
  *Breaks when:* a parse error drops the good lines, or two different answers
  for one picture are both kept.
- **INV-9** — `tileQuestions` lists every Unsure material of the bake, once,
  most surfaces first, each with a `view` whose normal is unit length.
  *Test:* `tests/unit/BakeTest.cpp` on the golden map with its judged
  materials forced Unsure by limits the test sets; `tests/unit/TileKindTest.cpp`
  for the order, on a fixture whose materials differ in surface count.
  *Breaks when:* a Fixed or Shuffle material is listed, one is missing, or the
  order is the materials' own.

## 6. Failure modes

- **A natural texture judged Fixed:** it repeats as today; nothing breaks.
- **A structured texture judged Shuffle:** visible — broken courses or a
  scrambled sign. The limits are conservative so this is rare; the user's
  `fixed` answer corrects it for that picture everywhere.
- **The answers file is unreadable** (permissions): a warning where the bundle
  loads, and every material keeps its baked kind. A line that is not
  well-formed text is a malformed line.
- **No data directory** (`fs::dataDirectory` fails): no answers, a warning.
- **`ut-shot` cannot draw the view:** the texture picture alone is shown, and
  the script says so. A camera placed in solid space draws a wrong view and is
  not detected.

## 7. Tests

`tests/unit/TileKindTest.cpp` (label `unit`) locks INV-1, INV-2, INV-3, INV-6
and INV-8, § 4.2 step 1's span exclusion, the diagonal bands and each limit at
its edge; `tests/unit/BundleMaterialTest.cpp` INV-7; `tests/unit/BakeTest.cpp`
INV-9; `tests/device/RenderTileShuffleTest.cpp` (label `device`) INV-4 and
INV-5. The rules each guards are mutated in `scripts/mutations/tilekind.py`,
and every mutation is killed; the shader's three were mutated by hand on
lavapipe.

## 8. Alternatives considered (and rejected)

- **Texture group names.** § 2 item 3: they do not decide it.
- **A hand list of every texture.** Thousands of pictures; the scores settle
  most, and the user answers only the ones in a map they bake.
- **Answers per map, in a recipe.** A picture is natural or not in every map;
  per map would ask the same question many times.
- **Keyed on the texture's name.** Names collide across packages with
  different pictures.
- **Answers as a bake input, in the bake's name** (this spec's first draft).
  The name exists before the bake and a picture's hash only after, so only
  the whole file could enter it, and then every answer would re-bake every
  map. Applied at load instead (§ 4.3).
- **Excluding materials with parallax** (the first draft). Every generated
  material has a depth, so it excluded everything; the march blends the
  height instead (§ 4.4).
- **An FFT high-pass.** Its sines are rounded by each compiler's library, so a
  verdict at a limit could differ between the legs; the box mean has no such
  function.
- **Rows and columns alone for *lines*.** Two hazard-stripe textures scored
  0.7 and 1.8 and herringbone tiles 3.7, all under the Shuffle limit; with the
  diagonals, 48, 48 and 9.4.
- **A per-surface kind.** A vertex or surface table field costs bundle space
  on every surface for a rare case: a natural texture fitted to one decal
  surface. Deferred, § 9.
- **One score (blotchiness).** Tried first: block-mean spread over 4 × 4
  blocks put dirt (0.51) with panels and bricks; it measures the wrong thing.
- **Water's fade toward the mean.** § 4.4.

## 9. Out of scope

- A per-surface exception for a natural texture fitted to one surface —
  deferred; not yet queued.
- Texture synthesis beyond two blended copies — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/TileKindTest.cpp`; the golden bake on GitHub's matrix |
| INV-2 | `tests/unit/TileKindTest.cpp` |
| INV-3 | `tests/unit/TileKindTest.cpp` |
| INV-4 | `tests/device/RenderTileShuffleTest.cpp` |
| INV-5 | `tests/device/RenderTileShuffleTest.cpp` |
| INV-6 | `tests/unit/TileKindTest.cpp` |
| INV-7 | `tests/unit/BundleMaterialTest.cpp` |
| `shapeOf` samples `tileKind` | **Partial:** a stale upload shows only on a re-bake in a running viewer; no test re-bakes under a live renderer, as for UTA-0105, 0263 and 0286 |
| INV-8 | `tests/unit/TileKindTest.cpp` |
| INV-9 | `tests/unit/BakeTest.cpp`; `tests/unit/TileKindTest.cpp` for the order |
| Thresholds keep structured textures Fixed | **nothing** automatic — a wrong threshold is a look, found by eye or by a user's `fixed` answer |
| The script's view shows the surface | **nothing** automatic — a camera in solid space draws a wrong view (§ 6) |

## 11. Cross-doc impact

- `UTA-0011` § the bake's printed report: a `tileQuestions` bullet, "Added by
  `UTA-0277`", as UTA-0263, 0105 and 0286 did for theirs.
- `UTA-0089` § 4.4: the blend now lives in `variation.glsl`; water's behaviour
  is unchanged.
- `CLAUDE.md` § Standing facts: the format and baker numbers.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0277-per-tile-variation-loop-log.md`.

## 13. Resource cost

Bake: per judged material, one 128 × 128 resample and box high-pass and one
direct 64 × 64 autocorrelation, once; DM-Deck16][ bakes in 3.9 s with it.
Draw: for a Shuffle pixel, one noise value, a second sample of each map it
reads, and a second height sample at each parallax step; § 4.4 has the
measured worst case.

## 14. Migration / compatibility

Format 22 and baker revision 40: every bundle re-bakes, as at each bump.
`MATS` grows by 33 bytes a material. An absent answers file is the normal
state, and an answer needs no re-bake.

## 15. Open questions

None. Parallax and the `SPOTS_*` limits, open in the first draft, are settled
in § 4.4 and § 4.2.
