I've finished reading all 25 lane files and run the searches; here is the report.

## Files read
- src/umat/CMakeLists.txt — 82 / 82
- src/umat/Cloth.h — 33 / 33
- src/umat/CuratedCloth.cpp — 72 / 72
- src/umat/CuratedFlames.cpp — 71 / 71
- src/umat/CuratedMaterials.cpp — 643 / 643, read in two pages
- src/umat/Derive.cpp — 150 / 150
- src/umat/Derive.h — 55 / 55
- src/umat/Enlarge.cpp — 166 / 166
- src/umat/Enlarge.h — 71 / 71
- src/umat/Fingerprint.cpp — 29 / 29
- src/umat/Fingerprint.h — 43 / 43
- src/umat/Flames.h — 35 / 35
- src/umat/Generate.cpp — 100 / 100
- src/umat/Generate.h — 68 / 68
- src/umat/Library.cpp — 55 / 55
- src/umat/Library.h — 74 / 74
- src/umat/Material.cpp — 293 / 293
- src/umat/Material.h — 133 / 133
- src/umat/Resolve.cpp — 122 / 122
- src/umat/Resolve.h — 32 / 32
- src/urecipe/CMakeLists.txt — 28 / 28
- src/urecipe/Lookup.cpp — 78 / 78
- src/urecipe/Lookup.h — 60 / 60
- src/urecipe/Recipe.cpp — 713 / 713
- src/urecipe/Recipe.h — 171 / 171

## What I started with
- The global `~/.claude/CLAUDE.md`, injected by the harness.
- A SessionStart hook note saying the Ants MCP is connected.
- Environment details: platform, date 2026-10-23, working directory `/home/ants/.cache/lane-neutral`.
- A user-email context block.
- The review-lane agent instructions.
- I had no project `CLAUDE.md`, no memory index and no git snapshot.
- Besides the lane files and `shared-context.md`, I read only the search results below and `src/ubundle/TextureSection.cpp` lines 26–40.

## Summary
This code turns each old, low-resolution game texture into a modern material:
1. It converts the picture to full colour.
2. It enlarges it, up to 4 times.
3. It works out how bumpy, rough or glowing each part is.
4. It packs the result into the compressed formats a graphics card reads.

It also checks that a map's textures fit a memory budget, and can trim them to fit if asked. Three short hand-kept lists say which pictures are metal or lights, which are flames, and which are cloth. The `urecipe` half reads and writes the "recipe" text file, which holds a player's per-map changes (material tweaks, added lamps, a sun, a sky, hidden brushes). It also finds which recipe applies to a map.

## Findings

[Q4] MEDIUM `src/urecipe/Recipe.h:158` — `/// SHA-256 of SS 4.4's canonical bytes: the materials only.` — The comment opens by saying only materials are hashed. The next ten lines then add, version by version, lamps, sun, switch-off, sky, mesh, haze and hidden brushes. The real rule is buried under history. The same history has left three section comments in `Recipe.cpp` false:
- `:675` "the switched-off names last"
- `:683` "the sky last"
- `:694-695` "a sky without one keeps its version-6 digest" — a sky recipe with no lamp mesh is version 5.

In fact the hidden brushes come last (`:701`). Someone adding a ninth section, or reproducing the digest from these comments, would put bytes in the wrong place. — Fix: say the current byte order once, as a list, and drop the "last" and "version-N digest" wording. Cost: one edit — comment only.

[Q4] MEDIUM `src/umat/Enlarge.cpp:120` — `/// One axis of toPowerOfTwo: \`count\` lines of \`length\` texels, \`stride\` bytes apart along the axis and \`step\` bytes between lines, to \`outLength\`.` — `stretchAxis` has no `stride` or `step` parameter. The layout is chosen by `bool alongRows` (`:123`), which the comment never mentions. Someone calling it for a new layout would look for parameters that do not exist. In the same function, `(void)lines;` at `:127` claims `lines` is unused, but `:128` uses it. — Fix: describe `alongRows` in the comment and delete the `(void)lines;`. Cost: one edit — comment, plus one dead statement.

[Q2] MEDIUM `src/urecipe/Recipe.cpp:375` — `Result<Recipe> parse(std::string_view text) {` — Each section kind is handled in five places inside this one function, about 180 lines apart:
- the `In` enum (`:378`)
- the heading branch (`:446-505`)
- the `sectionComplete` lambda (`:396`)
- the key dispatch (`:514-529`)
- the "must give the map's sha256" refusals (`:539-553`), five near-copies

Outside the function there are four more: `versionOf`, `write`, `bakeDigest`, and the version lists in `Recipe.h` (`:6-14`, `:142-145`, `:149-155`). A reader loses the thread at `:446`. Someone adding a section kind is likely to miss the sha256 refusal at the very end, and the recipe would then silently be accepted without a map digest. Nothing names the full list of places to touch. The five version lists do match today; I checked each. — Fix: one table per section kind (name, minimum version, singular or multiple, needs digest, required keys) that drives the heading, completeness and digest checks. Cost: one edit — searched `docs` for `Recipe.cpp|lampNameOf`: only review records under `docs/reviews/` hit. Confirms candidate `src/urecipe/Recipe.cpp:375:parse`.

[Q1] MEDIUM `src/umat/Generate.cpp:18` — `constexpr std::uint32_t MAX_BASE_EDGE = 8192;` — The 8192 ceiling on a stored edge is written separately in at least five places:
- `Generate.cpp:18`, plus a literal in its message at `:61`
- `Material.cpp:35` (`MAX_EDGE`), plus a literal in its message at `:62`
- `Enlarge.cpp:154` and `:156` (bare literals)
- `ubundle/TextureSection.cpp:30` (a function-local `MAX_EDGE`), plus message literals at `:35` and `:38`
- `ubake/Bake.cpp:635` (`MADE_EDGE_LIMIT`, commented "umat refuses above 8192 in any case")

`Generate.cpp:17` admits it mirrors "compress() and ubundle's reader". Someone raising the ceiling changes one copy, and the error messages keep saying 8192. — Fix: one exported constant beside `MAX_UPSCALE_FACTOR` in `ubundle/Bundle.h`, used everywhere including the messages. Cost: unsure — `MAX_BASE_EDGE` is in no doc (searched `docs`), but I did not search the specs for `MAX_EDGE` or for a numbered edge ceiling. Searched: `8192` in `src`.

[Q1] MEDIUM `src/urecipe/Recipe.cpp:30` — `std::string lowered(std::string_view text) {` — The same ASCII lower-casing is written three times in this lane:
- `Generate.cpp:35` `appendLowered`, which builds `materialId`
- `Recipe.cpp:30` `lowered`
- `Lookup.cpp:56-58` inside `mapNameOf`

Two must agree with the first. `Recipe.cpp:28` says recipe texture names must "compare equal to the ids the bake makes". `ubake/Name.h:75-76` says `mapNameOf` is "the `package` a map's own textures are named under". Only `Recipe.cpp` says so at the copy. Someone changing how `materialId` folds case (to fold non-ASCII, say) would leave the map-name copy, and possibly the recipe copy, behind, and lookups would quietly stop matching. — Fix: one ASCII-lower helper in `core`, which both `uta_umat` and `uta_urecipe` already link. Cost: one edit — searched `src/core` for `'A'|lower`: no helper exists. Specs name `materialId` (UTA-0009 table line 112, UTA-0113:100), but the fix keeps that name and file. Searched: `- 'A' + 'a'` across the tree (also hits `unav/Build.cpp`, `ubake/Install.cpp`, `tools/ut-paths/Cli.cpp` and a test), and `mapNameOf` in `src` (`ubake::detail::mapNameOf` delegates to `urecipe`'s).

[Q4] LOW `src/urecipe/Recipe.h:18` — `and no umat, whose CuratedOverride the six fields below mirror.` — `CuratedOverride` (`Library.h:30-36`) has five fields. The sixth field of `MaterialAssignment`, `upscale`, has no counterpart there; `Recipe.h:47-48` says so correctly. Someone mirroring a new field would hunt for a sixth override that does not exist. — Fix: "whose CuratedOverride fields, plus the upscale, the fields below mirror", with no count. Cost: one edit — comment only.

[Q3] LOW `src/urecipe/Recipe.cpp:493` — `UTA_TRY(std::string name, lampNameOf(trimmed(heading.substr(4)), line));` — `[hide]` section names are checked by `lampNameOf`. Someone changing the lamp-name rule (UTA-0256) would change the hide-name rule without knowing. Only `Recipe.h:115` (`///< as AddedLamp::name`) records the link, and it is not at the function. — Fix: rename it to say it validates a section name (e.g. `sectionNameOf`) and drop "lamp" from its message. Cost: one edit — searched `docs` for `lampNameOf`: no hit outside review records.

[Q5] LOW `src/urecipe/Recipe.cpp:454` — `heading.starts_with("material") && isBlank(heading[8])` — The bare indices 8 and 4 (`:454-455`, `:461-463`, `:491-493`) are the lengths of "material", "lamp" and "hide", and nothing says so. Someone adding a named section would copy the pattern with the wrong index. — Fix: take the length from the keyword string. Cost: one edit — local to `parse`.

[Q4] LOW `src/umat/Generate.cpp:82` — `const std::array<const std::vector<Image>*, MAPS.size()> chains{&colour, &normal, &rough, &height, &emit};` — This array pairs with `MAPS` by position only. `MAPS` carries a "MapKind order" comment (`:26`); `chains` carries none. Someone reordering or adding a map kind in `MAPS` would compress the wrong chain under the wrong name and format, with no error. — Fix: a comment tying `chains` to `MAPS` order, or a `static_assert` on `MAPS[i].kind`. Cost: one edit — local.

[Q4] LOW `src/umat/Generate.cpp:26` — `/// MapKind order and UTA-0052 SS 4.2's formats (INV-8).` — Read as written, the INV-8 is UTA-0052's. That one is the fast-math rule (UTA-0052:779, and `CMakeLists.txt:47`). The rule meant is probably UTA-0009's INV-8, "a material carries Base, Normal, Rough and Height always" (UTA-0009:418). Someone following the citation lands on the wrong spec. — Fix: write "UTA-0009 INV-8". Cost: one edit — comment only. Searched: `INV-8` in the UTA-0009, 0010 and 0052 specs.

[Q1] LOW `src/umat/CuratedFlames.cpp:56` — `constexpr bool sortedAndUnique() {` — Three identical copies of this check exist: `CuratedMaterials.cpp:630`, `CuratedCloth.cpp:57`, `CuratedFlames.cpp:56`. `ClothEntry` and `FlameEntry` are also the same struct (`Cloth.h:21`, `Flames.h:23`), with the same accessor and binary search. Nothing has diverged, so a stranger copying a fourth list would get it right. This is rule-of-three only. — Fix: one templated sorted-table check shared by the three. Cost: pinned only if the cloth and flame files merge — UTA-0055 lines 160-161 name `src/umat/CuratedCloth.cpp` and `isCloth`, and UTA-0286:154 names `umat::isFlame`. Sharing only the check is one edit.

[Q1] LOW `src/umat/Enlarge.cpp:136` — `const auto wrap = [length](std::int64_t i) {` — The tiling wrap of an index is written four times:
- `Enlarge.cpp:17` (`wrap`)
- `Enlarge.cpp:136` (a local lambda that shadows the file-level `wrap` with a different arity)
- `Derive.cpp:51` (`wrapped`)
- `Resolve.cpp:44` (inline, correct only for offsets of ±1)

Inside `stretchAxis`, someone reading `wrap(whole)` may go looking for the two-argument `wrap` above. — Fix: use the file-level `wrap` in `stretchAxis`. Optionally share one helper across files. Cost: one edit — `stretchAxis`, `wrap` and `wrapped` are not in any doc (searched `docs` for `stretchAxis`: review records only).

## Candidates dismissed
- `src/umat/Enlarge.cpp:39:enlarge` — two symmetric passes, each with its own comment. The validation lines are flat. A stranger can follow it.
- `src/umat/CMakeLists.txt:2` "2 keeps" — a rule number (design.md rule 2), not a count.
- `src/umat/Material.h:6` "2 keeps this" — the same rule number.
- `src/urecipe/Recipe.h:7` "2 adds lamps" — a format version number, not a count.
- `src/urecipe/Recipe.h:16` "3 makes" — a rule number (design.md rule 3).
- `src/umat/Fingerprint.h:33` "4 bytes" — the byte layout of a hashed format. It matches `Fingerprint.cpp:17-19`, and changing it is a format change.
- `src/umat/Library.h:66` "8 bytes" — the same; it matches `Library.cpp:42-43`.

## Correctness leads
- `src/urecipe/Recipe.cpp:493` — a bad `[hide NAME]` is refused with "'…' is not a lamp name", which names the wrong section to the player. Searched: none.
- `src/umat/CuratedMaterials.cpp:264` — the note `"fordtext.metals.pumppanel-03 "` has a trailing space. It is audit-only text, but the matching label may not compare equal. Searched: none.
- `src/umat/Enlarge.cpp:127` — `(void)lines;` on a parameter that is used at `:128`. [tool?: redundant void cast / dead statement]

## Not checked
- I did not open the specs to check the section numbers cited in comments (e.g. `MAX_EDGE` "SS 4.3" vs `MAX_OUTPUT_EDGE` "SS 4.5"), apart from the INV-8 search above.
- I did not search the specs for `MAX_EDGE`, so the 8192 finding's cost is unsure.
- I did not check that the curated, cloth and flame tables agree with their label files. That is test territory.
- I did not check that `Enlarge.h`'s Lanczos weights are correct.
- My `materialId|appendLowered|…` search was cut at 40 rows (`truncated: true`; later rows unread). The lower-case finding rests on the separate, complete searches for `- 'A' + 'a'` and in `src/core`, not on that one.
