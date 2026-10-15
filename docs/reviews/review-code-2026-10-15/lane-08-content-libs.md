**Lane: materials, fire, recipes, room map, navigation. Depth pass: the packet carries no `BREADTH PASS` marker.**

**Line counts as read (native `Read`):** `ufire/Fire.cpp` 254 · `ufire/Fire.h` 115 · `umap/Build.cpp` 602 · `umat/CuratedFlames.cpp` 71 · `umat/Enlarge.cpp` 166 · `umat/Enlarge.h` 71 · `umat/Flames.h` 35 · `umat/Material.cpp` 293 · `umat/Material.h` 132 · `umat/Resolve.cpp` 122 · `unav/Build.cpp` 420 · `unav/Graphs.h` 151 · `unav/Reach.h` 70 · `urecipe/Lookup.cpp` 78 · `urecipe/Lookup.h` 60 · `urecipe/Recipe.cpp` 620 · `urecipe/Recipe.h` 139.

**Already in my context when I arrived:** the global `~/.claude/CLAUDE.md`, including its rule 14 and rule 18 text about review lanes. The harness's user-email note. This lane prompt. No project `CLAUDE.md` and no memory index. The shared-context file was read as STEP 1.

**Brief and agent file:** I found no disagreement. My return opens with line counts and the context list, then follows the brief's format.

## Critical (0)

## High (0)

## Medium (2)

- [dim 5] `umap/Build.cpp:357-358` — `for (std::size_t which = 0; which < next; ++which) { std::vector<std::vector<Point2>> rings = traceComponent(lattice, label, which);` — Each component gets its own `traceComponent` call. Each call allocates `edgeTo` for every corner of the whole lattice, scans every cell (lines 265-278), and then scans every corner (line 288).
  - So one room costs about components × lattice size, and that repeats for each of up to 64 rooms.
  - `MAX_COLUMNS` allows 2^24 columns. A hostile or noisy map whose zone splits into thousands of small pieces hangs the bake.
  - Maps are untrusted community files, so this is also resource exhaustion (dim 3).
  - **Fix:** trace every component in one pass over a shared edge table. Or limit each component's scan to its own bounding box, which can be gathered during labelling.

- [dim 2] `urecipe/Recipe.cpp:239-248` — `for (const std::string_view word : wordsOf(value)) {` — A quoted empty value such as `textures = ""` gives zero words, but the key still counts as seen. `sectionComplete` (line 363) only checks that the key is present. So a `[sky]` with no textures is accepted, though `Recipe.h:91` and UTA-0350 say 1 to 8.
  - Round-trip breaks: `write` then emits `textures =` (line 534) with nothing after it. `parse` refuses that line with "the key has no value". This violates UTA-0113 § 4.2: *"`parse(write(r))` equals `r`"*.
  - Same gap in `setLampKey` `fitting = ""` (line 177). A version-2 or 3 recipe passes the required-`fitting` check with an empty list. That is a lamp with no fitting, which `Recipe.h:117-118` says needs version 4. `versionOf` then reports 4, so the bake digest changes.
  - Same gap in `setSwitchOffKey` (line 216), which accepts zero names against its "one or more".
  - Related: `write` never quotes list values. A sky texture that came from a quoted value starting with `"` is written bare and does not read back.
  - **Fix:** refuse an empty word list in all three setters. Quote, or refuse, a list word that starts with `"`.

## Low / Info

- [dim 5] `umap/Build.cpp:560-567` — `const std::uint32_t room = roomAt(map, point);` — Up to `MAX_SAMPLES` (2^30) samples. Each one is a descent that `roomAt` bounds only by `map.nodes.size()` (`umap/Rooms.cpp`). With a hostile node table (a long chain), that is 2^30 × node count, effectively a hang. Even honest depth costs tens of billions of steps at the cap. The comment at lines 21-23 marks the cap UNVERIFIED. **Fix:** cap the samples × node-count product, or lower `MAX_SAMPLES` to a measured figure.
- [dim 7] `unav/Build.cpp:72-83` — `return std::string{};` (depth cap reached) — A cyclic or over-long outer chain is malformed. Here it becomes an empty package name, so the class silently "does not descend" and its nodes vanish, with no count and no error. UTA-0006 § 4.5 says a count of zero is not an absence. **Fix:** return `MalformedData`, or count it.
- [dim 2] `umat/Material.h:16-18` — `Nothing here drops, resizes or re-compresses a texture to make it fit` — Contradicted by `fitToBudget` in the same header (lines 91-101), which drops mip levels. **The comment is the wrong side, not the code.** **Fix:** reword the header to "never degrades unless `--fit-budget` is asked for".
- [dim 2] Doc-side, for `review-contract`: UTA-0006 § 6 row *"An actor's property list does not parse | Propagated"* contradicts the spec's own § 4.5 (UTA-0244: skipped and counted). The code (`unav/Build.cpp:316-320`) follows § 4.5. **The § 6 row is the wrong side.**
- [dim 7] `unav/Build.cpp:339-340` — `UTA_TRY(const std::string_view value, package.name(std::get<upkg::NameRef>(property.value).index));` — If a property's name *value* index is out of range, this fails the whole wiring graph. UTA-0244 meant to stop exactly that: one damaged actor hiding all the others. I could not confirm whether `readProperties` already validates value indices (see Open questions).
- INFO: I did not open `core/FileSystem` `readFile`, so I cannot say whether a recipe's file size is capped. `parse` caps lamps (64) and sky textures (8) but not `[material]` sections.

## Covered by spec and looks correct

- **`Enlarge.cpp`**: `firstTap` checked algebraically against `floor((2j+1-k)/2k) - (A-1)`. The `kWeights2` and `kWeights4` phases each sum to 16384 (checked by hand). The largest tap sits at the nearest texel. Edges wrap, and the code is integer-only (UTA-0009 § 4.3).
- **`stretchAxis` indexing**: correct for both axes.
- **`Resolve.cpp`**: matches § 4.2. Masked alpha, 8-neighbour wrap fill, each pass reading the previous state, and no division by zero (every candidate has a filled neighbour).
- **`Fire.cpp`**: matches UTA-0286 § 4.1 and § 4.4. The `Model` Wheel and SphereLightning rules, `colour` alpha (INV-5) and `stepsPerSecond` (INV-6, including NaN reading as 30) all match. The `fade_` index stays within its 1,024 entries.
- **`Recipe.cpp`**: all number parsing uses `std::from_chars`, so the known `stod` locale trap does not apply. `pointOf` refuses out-of-range floats with no narrowing, which meets `cpp.md` § Range guards. Header version checks, refusing UnsupportedVersion, the BOM skip and the v1 `bakeDigest` layout match UTA-0113 § 4.2 and § 4.4.
- **`Lookup.cpp`**: order is named, then player, then shipped. A named file that does not exist is refused. The map name and sha256 checks match § 4.3.
- **`Reach.h`**: the flag values, the default of 125, and the `mayTraverse` rule all match.
- **`umap/Build.cpp`**: the lattice guards are checked before the cast. Footprint cells are centred, and the rings trace correctly at diagonal pinch corners. `assignFloors` matches § 4.5, including the band at z = 0.
- **Zombie features (dim 2b)**: every promised entry point has a non-test caller: `isFlame`, `toPowerOfTwo`, `fitToBudget` and `located` (in `ubake`/`apps`), and `colour`, `stepsPerSecond`, `PRIME_STEPS` and `mayTraverse` (in `urender`/`ut-paths`).
- **N/A**: dimensions 10, 12 and 15. No contract is named, and no user-facing surface is in this lane. Dimension 13: nothing found; ASCII folding is used throughout.
- **Nothing found**: dimensions 4, 8, 9, 16 and 17 in these files. Jobs write only their own rows or blocks, and these files do no persistence.

## Open questions

- `Fire.cpp:204` — `--particle.age;` on a `uint8_t`. A Blaze-side particle with `byteC == 0` wraps to 255 and lives 254 steps. Twirl checks `age > 0` before it decrements. Is this the original UT/SurrealEngine behaviour, kept on purpose under INV-3's byte-for-byte rule, or a bug? I cannot tell from UTA-0286.
- Does `upkg::readProperties` validate a `NameRef` property's value index? If not, the dim-7 wiring item above is real.
- The RDP stage measures distance to the infinite line, not the segment. UTA-0007 § 4.4 promises not to move a vertex "further than that tolerance". Standard RDP has the same behaviour, so I left it unfiled.

## 3 items to fix first

1. **Empty list values in `Recipe.cpp`.** Recipes come from other people. Today an accepted recipe can fail its own `write` round trip, and a version-2 lamp can quietly become version 4, which changes the bake name.
2. **`traceBucket` cost in `umap/Build.cpp`.** A hostile or noisy community map can hang the bake, and the bake is not interactive.
3. **The sampling cost cap.** It completes item 2: the samples × node-count product is the other unbounded cost on the same untrusted input.
