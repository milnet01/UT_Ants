<!-- ants-spec-format: 1 -->
# UTA-0277 — per-tile variation for natural textures

**Status:** accepted (2026-10-05), unreviewed. Contract reviews are cancelled for this
project (`CLAUDE.md` § Contract reviews); no review runs unless the user asks.
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
picture, stored per material in the bundle, and overridden by a per-user answer
file; a texture the bake cannot judge is drawn unchanged and listed, with a
view of the map and of the texture, for the user to answer.

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

`ubundle::MaterialRecord` gains one field, written and read with `MATS`:

```cpp
enum class TileKind : std::uint8_t {
    Fixed = 0,    // drawn as today: judged structured, or flat, or excluded
    Shuffle = 1,  // drawn with § 4.4's two-copy blend
    Unsure = 2,   // drawn as Fixed; listed for the user (§ 4.5)
};
TileKind tileKind = TileKind::Fixed;
```

`FORMAT_VERSION` 21 → 22, `BAKER_REVISION` 39 → 40. A value above 2 refuses the
section, as `MATS`'s liquid byte does. `shapeOf` in `Frame.cpp` samples it
(UTA-0014's re-upload rule), and `ShaderTypes.h`'s `Material` carries it as a
`uint`, mirrored in `types.glsl`.

### 4.2 The bake's judgement

Read from the material's base level as `umat::resolve` gives it, before any
upscale, in this order — the first that applies decides:

1. **Excluded → Fixed:** a liquid, flame or fire material (they carry their own
   look), a masked texture (moving it moves its holes), a material with
   `parallaxDepth > 0` (§ 15 item 1), or one with no surface in the level
   spanning two repeats in both `u` and `v` — nothing there repeats to hide.
2. **A user answer → Shuffle or Fixed** (§ 4.5), whatever the scores.
3. **Flat → Fixed:** luma standard deviation under 2 at 64 × 64.
4. **The two scores**, on luma box-filtered to 128 × 128, high-passed at
   1/32 cycles per texel (blotches removed, so only sharp features count):
   - *lines* = (variance of row means + variance of column means) / variance,
     × 64 — whole-width bands: mortar courses, plank edges, trim bands.
   - *spots* = the highest normalised autocorrelation of the high-passed
     picture at any offset outside a radius of 4 texels — a grid of rivets,
     studs or tiles that *lines* misses.
   - Shuffle when *lines* < `LINES_SHUFFLE` and *spots* < `SPOTS_SHUFFLE`;
     Fixed when *lines* ≥ `LINES_FIXED` or *spots* ≥ `SPOTS_FIXED`; else Unsure.

Measured for *lines* (`~/.cache/uta-scratch/u277/`, `tex-dump` over the
reference install's `Textures/`, scored at 128 × 128): dirt, marble, grimy
metal and plain plaster 1.5–4.8; bricks 10–23, planks 15–33, trims and tiles
41–61; natural rock, ice and wood grain 5–9, alongside a texture with a painted
word. So `LINES_SHUFFLE` 5 and `LINES_FIXED` 9 to start; by surfaces using a
texture of that name, about 27% Shuffle, 14% Unsure, 53% Fixed and 6% flat (the
name collides across packages, so these shares are approximate). A riveted
plate scored 4.7 on *lines*, which is why *spots* exists; its thresholds are
fitted at implementation on a labelled set and recorded beside the constants.

The scores are a pure function of the base level's bytes, so every compiler
and thread count gets the same verdict (the bake's existing rule).
The four limits are arguments of the judging function, defaulting to the
constants, so a test can force each outcome on a fixed picture.

### 4.3 What the bake's name covers

The answers file (§ 4.5) is a bake input. The lines whose content hash matches
one of this bake's materials feed the bake's name, beside the recipe
(UTA-0113). Only those: answering a texture re-bakes the maps that use it and
no other.

### 4.4 Drawing — `variation.glsl`

UTA-0089's two-copy blend moves out of `water.glsl` into `variation.glsl` as
one function both call, unchanged for water. For a Shuffle material in
`scene.frag`:

- The offset pair and blend weight are computed once per pixel from
  `shadingUv`, at UTA-0089's `TILE_VARIATION_REPEATS` and `TILE_OFFSETS`, with
  a seed from the material's index.
- **Every map the material samples takes the same two offsets and the same
  weight** — base, normal, roughness and emission — so the lighting stays on
  the picture it belongs to. The blend's contrast-preserving weight uses the
  base map's difference, as water's does.
- **No fade toward the mean.** Water fades its picture at distance; a wall
  would flatten to one colour, which is worse than repeats.
- A feature `Feature::TileShuffle`, a specialization constant as
  `TileVariation` is, from the tier its cost earns (measured with `ut-bench
  frame` at ultra 4K on DM-Deck16][ and at Low on lavapipe, as UTA-0180 was);
  below it, Shuffle draws as Fixed.

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
Blank and `#` lines are skipped. A malformed line, or a hash given two
different answers, is reported in the bake's `warnings` and ignored; the bake
does not fail on it.

**The question list.** The bake report gains, on `written` and `over-budget`:

```json
"tileQuestions": [{"material": "<id>", "hash": "<64 hex>", "lines": 0.0,
                   "spots": 0.0, "surfaces": 0,
                   "view": {"at": [0,0,0], "normal": [0,0,0], "extent": 0.0}}]
```

most surfaces first. `view` is the largest surface wearing it: its centre, its
normal and its longest side, in world units.

**The ask.** `scripts/tile-questions.py <report.json> <bundle> <out dir>`
writes, per question, `<n>-texture.png` (the base level, scaled up to at least
512 texels across, nearest-neighbour) and `<n>-view.png` (`ut-shot` from a
camera on the surface's normal at 1.5 × its extent, looking at its centre), and
an `answers.txt` template of the hashes. A session shows the user both
pictures — this is identifying a thing, not judging a look — and appends the
answers to the answers file.

## 5. Invariants

- **INV-1** — A material's tile kind is a pure function of its base level's
  bytes, its exclusions and the answers file: the same inputs give the same
  kind on every compiler and at any worker count.
  *Test:* `tests/unit/TileKindTest.cpp` — the same fixtures judged at 1 and 8
  workers, and the golden bake's `MATS` bytes on GitHub's three legs.
  *Breaks when:* the judgement reads a float the compilers round differently,
  or depends on the order materials finish.
- **INV-2** — A synthetic picture of whole-width bands is Fixed, a seeded
  value-noise picture is Shuffle, and a grid of spots with flat ground between
  is not Shuffle.
  *Test:* `tests/unit/TileKindTest.cpp`, each fixture made in the test, none
  masked, flat, liquid or parallax, so only § 4.2 step 4 can decide it.
  *Breaks when:* *lines* or *spots* is not computed, or a threshold is inverted.
- **INV-3** — An answer wins over the scores both ways: a band picture answered
  `shuffle` is Shuffle, a noise picture answered `fixed` is Fixed.
  *Test:* `tests/unit/TileKindTest.cpp`.
  *Breaks when:* answers are read after the scores, or keyed on the name.
- **INV-4** — An Unsure material, and any material at a tier below
  `Feature::TileShuffle`, draws the same picture as Fixed.
  *Test:* `tests/device/RenderTileShuffleTest.cpp` — one quad, one material,
  rendered Fixed and Unsure at High, and Shuffle at Low: the three images match
  exactly.
  *Breaks when:* the shader branches on `tileKind != Fixed`, or the feature
  constant is not consulted.
- **INV-5** — A Shuffle material's normal map takes the base map's offsets: a
  quad whose base and normal carry the same feature at the same texel draws its
  lit feature where its colour feature is.
  *Test:* `tests/device/RenderTileShuffleTest.cpp`.
  *Breaks when:* any map is sampled at the unshuffled `shadingUv`.
- **INV-6** — Changing the answer for a material's hash changes the bake's
  name; changing an answer for a hash no material of the map has does not.
  *Test:* `tests/unit/TileKindTest.cpp` on the name function.
  *Breaks when:* the answers file is not a bake input, or the whole file is.
- **INV-7** — `tileKind` round-trips through `ubundle::write` and
  `ubundle::read`, and a byte above 2 refuses the section.
  *Test:* `tests/unit/BundleMaterialTest.cpp`.
  *Breaks when:* the field is left out of the writer or the reader, or the
  reader accepts any byte.
- **INV-8** — A malformed answers line is a warning, not a failure: the bake
  writes, every well-formed line applies, and `warnings` names the bad line.
  *Test:* `tests/unit/TileKindTest.cpp`.
  *Breaks when:* a parse error aborts the bake or drops the good lines.
- **INV-9** — `tileQuestions` lists every Unsure material of the bake, once,
  most surfaces first, each with a `view` whose normal is unit length.
  *Test:* `tests/unit/BakeTest.cpp` on the golden map with one fixture texture
  forced Unsure by thresholds the test sets.
  *Breaks when:* a Fixed or Shuffle material is listed, or one is missing.

## 6. Failure modes

- **A natural texture judged Fixed:** it repeats as today; nothing breaks.
- **A structured texture judged Shuffle:** visible — broken courses or a
  scrambled sign. The thresholds are conservative so this is rare; the user's
  `fixed` answer corrects it for that picture everywhere.
- **The answers file is unreadable** (permissions, not UTF-8): a warning, and
  every material is judged on its scores.
- **No data directory** (`fs::dataDirectory` fails): no answers, a warning.
- **`ut-shot` cannot place the view** (the largest surface is inside a solid or
  sees nothing): the texture picture alone is shown, and the script says so.

## 7. Tests

`tests/unit/TileKindTest.cpp` (label `unit`) locks INV-1, INV-2, INV-3, INV-6 and INV-8;
`tests/unit/BundleMaterialTest.cpp` INV-7;
`tests/unit/BakeTest.cpp` INV-9; `tests/device/RenderTileShuffleTest.cpp`
(label `device`) INV-4 and INV-5. Each is seen failing against the code before
the change. Mutations go in `scripts/mutations/` per the project's rule.

## 8. Alternatives considered (and rejected)

- **Texture group names.** § 2 item 3: they do not decide it.
- **A hand list of every texture.** Thousands of pictures; the scores settle
  most, and the user answers only the ones in a map they bake.
- **Answers per map, in a recipe.** A picture is natural or not in every map;
  per map would ask the same question many times.
- **Keyed on the texture's name.** Names collide across packages with
  different pictures.
- **A per-surface kind.** A vertex or surface table field costs bundle space
  on every surface for a rare case: a natural texture fitted to one decal
  surface. Deferred, § 9.
- **One score (blotchiness).** Tried first: block-mean spread over 4 × 4
  blocks put dirt (0.51) with panels and bricks; it measures the wrong thing.
- **Water's fade toward the mean.** § 4.4.

## 9. Out of scope

- A per-surface exception for a natural texture fitted to one surface —
  deferred; not yet queued.
- Shuffling a material with parallax — § 15 item 1.
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
| INV-9 | `tests/unit/BakeTest.cpp` |
| Thresholds keep structured textures Fixed | **nothing** automatic — a wrong threshold is a look, found by eye or by a user's `fixed` answer |

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

Bake: one 128 × 128 FFT and one autocorrelation per material, once. Draw: for a
Shuffle pixel, one noise value and a second sample of each map it reads. The
tier is set from the measured cost (§ 4.4).

## 14. Migration / compatibility

Format 22 and baker revision 40: every bundle re-bakes, as at each bump. An
absent answers file is the normal state.

## 15. Open questions

1. **Parallax.** A material with a height map is excluded for now. Shuffling
   it means marching the height of two blended copies, about twice parallax's
   cost; revisit when the tier costs are measured.
2. **`SPOTS_*` thresholds** are fitted at implementation, not here.
