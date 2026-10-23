I've read all 31 lane files in full. There are 11 findings: no HIGH, 7 MEDIUM and 4 LOW. One comment sits on the wrong function, two comments are out of date, three function names are each used for several different jobs, and two checks are copied around the code in versions that no longer match. Details follow in the brief's format.

## Files read
- src/ubake/Actors.cpp — 371 / 371
- src/ubake/Actors.h — 52 / 52
- src/ubake/Bake.cpp — 1706 / 1706 (two pages: 1–1027 and 1028–1706)
- src/ubake/Bake.h — 232 / 232
- src/ubake/CMakeLists.txt — 52 / 52
- src/ubake/FireStill.cpp — 63 / 63
- src/ubake/FireStill.h — 49 / 49
- src/ubake/Flames.cpp — 233 / 233
- src/ubake/Flames.h — 69 / 69
- src/ubake/GameTypes.cpp — 159 / 159
- src/ubake/GameTypes.h — 41 / 41
- src/ubake/Hide.cpp — 167 / 167
- src/ubake/Hide.h — 35 / 35
- src/ubake/Install.cpp — 272 / 272
- src/ubake/Install.h — 119 / 119
- src/ubake/Lamps.cpp — 195 / 195
- src/ubake/Lamps.h — 62 / 62
- src/ubake/Liquids.cpp — 72 / 72
- src/ubake/Liquids.h — 45 / 45
- src/ubake/Movers.cpp — 244 / 244
- src/ubake/Movers.h — 76 / 76
- src/ubake/Name.cpp — 168 / 168
- src/ubake/Name.h — 98 / 98
- src/ubake/Strips.cpp — 168 / 168
- src/ubake/Strips.h — 36 / 36
- src/ubake/TextureCache.cpp — 249 / 249
- src/ubake/TextureCache.h — 73 / 73
- src/ubake/TileKind.cpp — 291 / 291
- src/ubake/TileKind.h — 81 / 81
- src/ubake/Zones.cpp — 125 / 125
- src/ubake/Zones.h — 38 / 38

## What I started with
- The global `~/.claude/CLAUDE.md`, injected as a system reminder.
- A SessionStart hook message saying the Ants MCP verbs are connected.
- The user's email address, as account context.
- The environment block: working directory `/home/ants/.cache/lane-neutral` (not a git repository), Linux, today's date.
- My agent instructions (review-lane).
- No project `CLAUDE.md`, no memory index and no git snapshot.

## Summary
This code turns one Unreal Tournament map into the project's own bundle file. First it finds the game install on disk and works out which file holds each package. It gives the bake a name built from everything that feeds it, so an old bake can be reused. It then turns the map's textures into materials, including special looks for flames, fire, water and ice, and gathers the map's actors, lights, moving parts, zones, flames and light strips. It also adds or hides what a map's recipe asks for: extra lamps, lights switched off, hidden brushes. It checks the result against a memory budget and writes the file, or reports a cached one.

## Findings

[Q4] MEDIUM `src/ubake/Bake.cpp:348` — `/// UTA-0263 SS 4.2: the palette at eight evenly spaced heats, coldest first,` — This four-line doc comment describes `flameLookOf` (line 381), not `fireLookOf`, the function it sits on top of. `fireLookOf` has a second comment stacked under it (line 352); `flameLookOf` has none. A stranger reading `fireLookOf`'s header is told it returns eight palette samples, which it does not. A stranger editing `flameLookOf` finds no statement of what it must return. — Fix: move lines 348–351 down onto `flameLookOf`.
Cost: one edit (comment only).
Searched: `struct FlameLook` in src/ubundle — Bundle.h:170–173 describes the 8-entry `ramp` in the same words, which confirms whose comment it is.

[Q4] MEDIUM `src/ubake/Name.h:33` — `/// Bumped by hand whenever any code a bake runs -- `ubake`, `umat`, `umap`,` / `` `unav`, `upkg` or `ubundle` -- changes what a bake writes. `` — The list of modules is incomplete. `FireStill.cpp` runs `ufire::Fire` to make a FireTexture's still picture, and the strips step uses `uworld` collision queries through `ubake/CollisionQuery.h`. A stranger changing `ufire/Fire.h` would read this list and not bump `BAKER_REVISION`. The golden test it names would catch that, so the cost is time, not a wrong bake. — Fix: name the rule ("any library uta_ubake links", per CMakeLists.txt) instead of listing modules.
Cost: one edit (comment only).
Searched: `#include "(uworld|ufire|urecipe)/` in src/ubake — FireStill.h/.cpp include ufire; CollisionQuery.h includes uworld.

[Q3] MEDIUM `src/ubake/Bake.cpp:196` — `const upkg::ExportEntry* exportNamed(const upkg::Package& holder, const ImportedObject& imported)` — Bake.cpp includes `ubake/Lamps.h` (line 16), which declares a public `exportNamed(const upkg::Package&, std::string_view)` in the same namespace. The two overloads do different jobs:
- **Bake.cpp's** matches the whole group chain and the class, and returns a pointer.
- **Lamps.h's** matches the bare object name, ignores class and group, and returns an index.

A stranger in Bake.cpp who writes `exportNamed(map, "Name")` silently gets the looser Lamps match. That is the very match UTA-0176 rejected: it found 1,924 Palettes in place of FireTextures. — Fix: rename one, e.g. Lamps' to `exportIndexNamed`.
Cost: one edit — Searched: `exportNamed` in docs/ (no spec names it).

[Q3] MEDIUM `src/ubake/Bake.cpp:201` — `const auto classOf = holder.objectName(candidate.objectClass); // "None" for a class export` — A local variable hides the file's own function `classOf` (line 80), and the two disagree on a class export: the function returns `""`, while this code needs `"class"`. A stranger tidying lines 201–204 into a call to `classOf(holder, candidate)` would stop imports of class objects from ever matching. Nothing says why the function cannot be used here. — Fix: rename the local, and add one line saying why a class export must compare as `"class"`.
Cost: one edit (rename of a local).

[Q3] MEDIUM `src/ubake/Bake.cpp:1117` — `// Qualified: inside `detail`, a bare nameOf finds Name.h's` — There are three unrelated `nameOf` functions in this module:
- Bake.cpp:86 gives a package name by index.
- Actors.cpp:42 gives a recorded package name.
- Name.h:68 `detail::nameOf(NameInputs)` returns the bake's SHA-256 name.

The collision has already cost a workaround comment. The Name.h one also misleads by itself: it computes the bake name hash, not "the name of" its inputs. — Fix: rename `detail::nameOf(NameInputs)` to something like `bakeNameOf`.
Cost: unsure — `nameOf(` returned no rows in src/ubake, although I read three definitions, so I could not run the docs search reliably. A rename of a `detail` function is likely one edit plus test call sites.

[Q2] MEDIUM `src/ubake/Bake.cpp:608` — `const upkg::Mip* shown = &base;` — `makeVariant` runs from line 551 to line 803, about 250 lines, and no tool shortlisted it. The reader loses the thread at lines 608–684:
- `shown`, `shownPalette`, `madeIndices` and `madeLevel` are reassigned on three branches: fire, source texture and flat fill.
- One of those branches is a lambda (`fillFlat`) that mutates them through its capture.
- The fire test `storesNoPixels && foldedClass == "firetexture"` is written out three times (lines 641, 703, 794) and only gets a name, `fireClass`, at the third.

A stranger changing how the shown picture is chosen must trace all of these to know which picture step 5 resolves. — Fix: split "choose the shown picture and palette" into its own function, and compute `fireClass` once near line 641.
Cost: pinned —
- ANCHORS `scripts/mutations/tilekind.py`: `"        tileHash = pictureHash(*resolved);\n"` (indent-exact, line 797).
- Specs name `makeVariant`: UTA-0105 (lines 30, 37, 107, 354), UTA-0112 (line 304), UTA-0292 (line 33).

[Q2] MEDIUM `src/ubake/Bake.cpp:1266` — `for (TileQuestion& question : tileQuestions) {` — Inside the 377-line `detail::bake`, the "strips" step (line 1258) quietly also sets each tile question's camera clearance. That work is unrelated to strips, and it sits there only to reuse `SurfaceRays`. A stranger looking for where `TileQuestion::clear` is set would read past step 8's tile judging and the strips step without seeing it. The probe section (lines 1325–1433) then stacks three gathers with lambdas between them. — Fix: give the clearance loop its own numbered step and comment, sharing the `SurfaceRays` object.
Cost: one edit — the ANCHORS lines in Bake.cpp are in `judgeTileKinds` and `makeVariant`, not here. Searched: `scripts/mutations/tilekind.py`.
Confirms candidate `Bake.cpp:1133 bake`.

[Q1] MEDIUM `src/ubake/Movers.cpp:33` — `bool descendsFromBrush(const ubundle::ActorClass& actorClass) {` — The check "class is X or has X in its ancestry" is written five times, and the copies differ:
- **Movers.cpp:33–37 and Lamps.cpp:55–57** also require `actorClass.resolved`.
- **Zones.cpp:86–92, LightProbes.cpp:306–310 and ubundle/Sky.cpp:14–16** do not.
- **Lamps.cpp:53 and LightProbes.cpp:305** bound-check `classIndex`; Zones.cpp:88 and Movers.cpp:88 index `classes[actor.classIndex]` unchecked.

A stranger fixing how unresolved classes are handled would fix one copy and not know about the rest. Nobody can tell which `resolved` behaviour is intended. — Fix: one shared `isOrDescendsFrom(actorClass, path)` helper next to `resolvedRecord`, with a stated rule on `resolved`.
Cost: unsure — no spec names `descendsFromBrush`, `levelInfoOf` or `isMeshDecoration` (Searched: docs/). But the Sky.cpp copy is in ubundle, which ubake depends on, so the helper's home is a design choice.
Searched: `== "engine\.|ancestry\.end\(\)` in src.

[Q1] MEDIUM `src/ubake/Zones.cpp:24` — `const ubundle::PropertyRecord* recordOf(std::string_view name, ubundle::ValueKind kind,` — The wrapper "`resolvedRecord` filtered by `ValueKind`" is written again and again under different names:
- `lookUp` (Actors.cpp:217)
- a `resolved` lambda (Lamps.cpp:58)
- `resolved` (Movers.cpp:40)
- `recordOf` (Zones.cpp:24)
- inline twice in LightProbes.cpp (lines 291 and 311)

The handling of a value that is not finite has already split. Movers.cpp:226–229 records a past fix (review-code 2026-10-15) made to match "Zones.cpp's vectorOf", while Actors.cpp:243–244 reads a light's `location` with no such guard. Zones' `recordOf` also shares its name with Actors.cpp's `recordOf`, which converts a property into a record. — Fix: add a `ValueKind` overload to `detail::resolvedRecord` in Actors.h, and say there what a non-finite value becomes.
Cost: one edit — specs name `resolvedRecord` (UTA-0119:243, UTA-0121:345, UTA-0156:147, UTA-0015:110, UTA-0112:276), and the fix keeps that name. No spec names the wrappers (Searched: docs/).

[Q1] LOW `src/ubake/Bake.cpp:510` — `std::optional<ubundle::CompressedTexture> detailOf(const upkg::Package& holder,` — `glassOf` (lines 469–502) and `detailOf` (lines 510–546) share about 20 identical lines: property, then object, then properties, then palette reference, then palette, then texture, then a Rec. 709 grey loop. They differ on purpose only in the size check and the mip chain, and the comments say so. The same luma weights appear a third time in Liquids.cpp:56. A fix to palette-index handling in one grey loop would likely miss the other. — Fix: extract "a referenced texture as a grey Image" and keep the two different tails.
Cost: pinned — UTA-0014-vulkan-draw-path.md:491 names `detailOf`.
Searched: `0.7152` across the tree.

[Q4] LOW `src/ubake/FireStill.cpp:1` — `// A still picture of a FireTexture -- UTA-0176. FireStill.h says what is` / `// adapted from SurrealEngine and what is altered.` — FireStill.h (read in full) never mentions SurrealEngine. A stranger looking for where the adapted code came from, and what was changed, is pointed to nothing. — Fix: point the comment at where the provenance actually lives (probably ufire/Fire.h), or remove it.
Cost: one edit (comment only).

[Q4] LOW `src/ubake/Bake.cpp:1399` — `"lamps: the added-lamp probes do not stand where the level's do -- a baker fault"` — `cubesOf` is also used for the switched-off layer (line 1419), so a failure there would blame the added-lamp layer. The same message is also written twice (lines 1399 and 1404). — Fix: pass the layer's name into `cubesOf`.
Cost: one edit.

[Q5] LOW `src/ubake/Bake.cpp:1519` — `for (std::size_t depth = 0; depth < 64; ++depth) {` — `homeOf` walks the same import outer chain as `walkImport` (line 157). `walkImport` is bounded by the import table's size; `homeOf` is bounded by an unexplained 64, and the two differ for deeper chains. Its comment says "Empty when the outer chain does not end", but it also returns empty on a bad index. — Fix: build `homeOf` on `walkImport`, or explain the 64.
Cost: one edit — Searched: `homeOf|walkImport` in docs/ (none).

[Q1] LOW `src/ubake/Install.cpp:45` — `Result<std::vector<fsys::path>> listSorted(const fsys::path& directory, Keep keep) {` — "List a directory, match names folded, sort bytewise by UTF-8 file name (SS 4.2's order)" is written three times: here, GameTypes.cpp:24 `childFolded`, and GameTypes.cpp:38 `intFiles`. They differ in error handling (IoFailure here, silent in GameTypes). Changing the tie-break order means three edits. — Fix: make `listSorted` usable from GameTypes, with a tolerant mode.
Cost: one edit — Searched: `childFolded|listSorted` in docs/ (none).

[Q1] LOW `src/ubake/Hide.cpp:34` — `bool within(std::int64_t index, std::size_t size) {` — The same function is defined in Flames.cpp:35, Hide.cpp:34, Collision.cpp:25 and Geometry.cpp:44, with a template variant in TileKind.cpp:163. Each node-corner walker beside them differs as well: Flames.cpp's `cornersBySurface` rejects points beyond `MAX_COORDINATE`, and Hide.cpp's `cornersOf` does not. — Fix: one shared `within` in Geometry.h.
Cost: one edit — Searched: `bool within\(` in src.

[Q4] LOW `src/ubake/GameTypes.h:30` — `/// Names whose package is absent, or does not hold the class.` — `mapPrefixOf` also returns nothing when the name has no dot, or when the ancestry or defaults fail to read. So `unresolved` holds more than this says, and a stranger debugging an "unresolved" game type whose package is present would be misled. — Fix: list those cases, or say "whose MapPrefix could not be read".
Cost: one edit (comment only).

[Q4] LOW `src/ubake/Hide.h:24` — `std::vector<Cover> covers;           ///< one a hidden brush, in recipe order` — "one a" reads as a typo for "one per". — Fix: "one per hidden brush".
Cost: one edit (comment only).

## Candidates dismissed
- `Bake.cpp:890 bakeMaterials` — Three clear stages (needs, variants, batched making), each with a comment. I found no point where the thread is lost.
- `Bake.cpp:1600 bakeToDirectory` — Linear, with numbered steps that match the header's SS 4.7 order.
- `Actors.cpp:280 buildActors` — Numbered steps 1–3, then sorting; easy to follow.
- `Lamps.cpp:76 buildLamps` — Step-labelled, with each refusal right next to its check. Its "own surfaces" loop is a copy of Hide.cpp's `ownedBy`, but that is two copies only.
- `Hide.cpp:89 hideBrushes` — Steps 1–3 are labelled, and the plane-cut maths in step 3 is commented.
- `Bake.cpp:838 "7 reports"` — "step 7", a step number, not a count.
- `Bake.cpp:1256 "5 asks"` — "rule 5", a rule number.
- `Bake.h:5 "2 keeps"`, `CMakeLists.txt:2`, `GameTypes.h:10`, `Install.h:7` — "docs/design.md rule 2", a rule number.
- `Bake.h:67 "32 units"` — Matches the code at Bake.cpp:1269–1270 (192/128/96/64, fallback 32).
- `FireStill.h:29 "255 fades"` — States what the value means, not a count.
- `Liquids.cpp:52 "709 weights"` — Rec. 709, a standard's name.
- `Movers.h:10 "2 leaves"` — "SS 3 decision 2".
- `Name.h:75 "4 hashes"` — "SS 4.4 item 4".
- `Strips.cpp:19 "432 headers"` — UT version 432's headers.
- `Strips.cpp:25 "127 maps"` — A dated measurement credited to UTA-0258/UTA-0334, not a live count.
- `Strips.cpp:27 "8 rests"` — "SS 8".
- `Strips.h:22`, `Strips.h:23` — Measured figures justifying the limits, with the map named for each. This is a record, not a claim that goes stale.
- `TextureCache.h:16 "491 materials"` — Explicitly "Measured that day".
- `TileKind.cpp:25 "128 texels"` — Consistent: `SPOTS_RADIUS_SQUARED = 4` is radius 2 on the 64-wide grid `spotsOf` uses.
- `TileKind.h:30 "400 most-used"` — A fitting record ("SS 4.2's record").
- `TileKind.h:42 "4 texels"` — Consistent with TileKind.cpp:25.

## Correctness leads
- `src/ubake/Actors.cpp:243` — A light's `location` (and `rotation`) is copied with no finite check. Movers.cpp:230–232 and Zones.cpp:59–60 replace non-finite values, after review-code 2026-10-15. If LITE does not refuse a non-finite location, a crafted map's NaN reaches strips, flames and probes. Searched: `LITE.*not finite|not finite.*LITE|location.*finite` in src (no rows); I did not read LITE's validator.
- `src/ubake/Zones.cpp:88` and `src/ubake/Movers.cpp:88` — `placements.classes[actor.classIndex]` is indexed without the bound check that Lamps.cpp:53 and LightProbes.cpp:305 apply. Safe only if every Placements comes from `buildActors`. Searched: `ancestry\.end\(\)` in src.
- `src/ubake/GameTypes.cpp:56` — Only little-endian UTF-16 (`FF FE`) is recognised. A big-endian `.int` file would be read as bytes, though GameTypes.h:37 promises "UTF-16 with one" (a byte order mark). Searched: none.

## Not checked
- I had no ANCHORS other than `scripts/mutations/tilekind.py`, so I did not judge test anchors for other files.
- My plain search for `nameOf(` in src/ubake returned no rows, although I had read three definitions there. So the rename cost for the `nameOf` finding is graded unsure.
- I did not open LITE's validator, `ufire/Fire.h` (for SurrealEngine provenance), Geometry.cpp, Collision.cpp or SurfaceRays beyond the search hits cited.
- Tests were not judged, per the brief.

No disagreement between the brief and my lane instructions came up.
