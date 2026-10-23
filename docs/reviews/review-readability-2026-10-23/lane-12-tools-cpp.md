## Files read
All files were read in full with native `Read`. The reader also showed one blank line past the end of each file (the trailing newline). Lines read / total:

- tools/CMakeLists.txt — 17/17
- tools/common/Json.h — 126/126
- tools/map-pic/CMakeLists.txt — 14/14
- tools/map-pic/Cli.cpp — 188/188
- tools/map-pic/Cli.h — 52/52
- tools/map-pic/main.cpp — 14/14
- tools/ut-bake/CMakeLists.txt — 9/9
- tools/ut-bake/Cli.cpp — 471/471
- tools/ut-bake/Cli.h — 42/42
- tools/ut-bake/main.cpp — 15/15
- tools/ut-bench/CMakeLists.txt — 40/40
- tools/ut-bench/Cli.cpp — 416/416
- tools/ut-bench/Cli.h — 86/86
- tools/ut-bench/Frame.cpp — 345/345
- tools/ut-bench/Frame.h — 40/40
- tools/ut-bench/main.cpp — 22/22
- tools/ut-bound-scan/CMakeLists.txt — 8/8
- tools/ut-bound-scan/Cli.cpp — 213/213
- tools/ut-bound-scan/Cli.h — 38/38
- tools/ut-bound-scan/Scan.cpp — 125/125
- tools/ut-bound-scan/Scan.h — 68/68
- tools/ut-bound-scan/main.cpp — 14/14
- tools/ut-dump/CMakeLists.txt — 9/9
- tools/ut-dump/Cli.cpp — 1849/1849 (in three pages)
- tools/ut-dump/Cli.h — 39/39
- tools/ut-dump/main.cpp — 14/14
- tools/ut-origin/CMakeLists.txt — 10/10
- tools/ut-origin/Cli.cpp — 51/51
- tools/ut-origin/Cli.h — 27/27
- tools/ut-origin/main.cpp — 14/14
- tools/ut-ref/CMakeLists.txt — 8/8
- tools/ut-ref/Reference.cpp — 262/262
- tools/ut-ref/Reference.h — 93/93
- tools/ut-ref/main.cpp — 228/228
- tools/ut-shot/CMakeLists.txt — 10/10
- tools/ut-shot/Cli.cpp — 238/238
- tools/ut-shot/Cli.h — 85/85
- tools/ut-shot/main.cpp — 221/221

## What I started with
- `/home/ants/.claude/CLAUDE.md`, the user's global rules, injected at start.
- A SessionStart hook message saying the Ants MCP is connected.
- An environment block: working directory `/home/ants/.cache/lane-neutral`, not a git repo.
- The user's email, as context.
- No project `CLAUDE.md`, no memory index and no git snapshot of this project.
- The shared-context file, read as STEP 1.

## Summary
These are eight small command-line programs the developer runs by hand or from scripts:
- **ut-dump** reports what is inside a map file as JSON.
- **ut-bake** turns a map into the game's own format.
- **ut-bench** times baking and drawing.
- **ut-shot** draws pictures from given camera positions.
- **ut-ref** computes "true" lighting to score ut-shot's pictures against.
- **ut-origin** checks one header byte.
- **ut-bound-scan** hunts a known map defect.
- **map-pic** saves a map's built-in preview picture.

They share one small helper, `Json.h`, which writes text safely into JSON output.

## Findings

[Q1] HIGH `tools/ut-shot/main.cpp:150` — `for (int index = 0; std::getline(cameras, line); ++index) {` — The camera-line reader (`x y z pitch yaw roll fov` plus the horizontal-to-vertical field-of-view conversion) has three hand copies, and they have diverged:
- ut-shot: `main.cpp:150-167`
- ut-bench: `Frame.cpp:122-152` (`readCameras`, `between`)
- ut-ref: `main.cpp:150-164` plus `Reference.cpp:164-167`

ut-bench skips blank lines and trims `\r`. ut-shot and ut-ref fail on a blank line. ut-ref re-declares `usableHorizontalFov` and `MAX_DIMENSION` instead of using `ut-shot/Cli.h`'s. The two copies name ut-shot as their source, but nothing at ut-shot points to them. A stranger who changes the camera format starting there edits one reader of three. — Fix: one shared camera-line parser and FOV conversion, called by all three tools.
Cost: unsure — UTA-0121 § 4.8 describes `tools/common/` as holding "the one string escaper", so a second shared header there may need that spec touched. UTA-0129 names `tools/ut-bench/Frame.cpp` only as a compiled file. Searched: `docs/specs` and `docs/standards` for `Frame.cpp|ut-shot/Cli|ut-shot/main|tools/common|ut-ref/main|Reference.cpp|usableHorizontalFov|MAX_DIMENSION`, and `docs/design.md` for the same terms (no hits). Searched: `verticalFovDegrees =` tree-wide, which found the three tool sites plus non-tool sites.

[Q4] MEDIUM `tools/ut-dump/Cli.cpp:180` — `// guard ubake's findModel applies, restated here because ut-dump does not link the baker.` — This is false. `tools/ut-dump/CMakeLists.txt:9` links `uta_ubake`, and `Cli.cpp` includes `ubake/Actors.h`, `Install.h` and `Zones.h` and calls `ubake::buildActors`. ut-bound-scan already calls `ubake::detail::findModel` (`ut-bound-scan/Cli.cpp:107`). A stranger who trusts the comment keeps a hand copy of the guard; the includes at the top contradict it. — Fix: delete the reason, or replace the guard with `ubake::detail::findModel`.
Cost: one edit (comment). Searched: `findModel|levelInfoOf` tree-wide.

[Q1] MEDIUM `tools/ut-dump/Cli.cpp:185` — `if (level.model.kind() != uta::upkg::ObjectReferenceKind::Export` — The "level's Model export in range, then readModel" guard has three copies:
- `writeSurfaces` 185-196
- `writeLighting` 1100-1109
- `writePlayerStarts` 1146-1155 (an exact twin of `writeLighting`)

They have diverged. On a failed read, `writeSurfaces` writes the reader's own error (`model.error().message()`), while the other two write the fixed text "the level's Model did not read". The comment at 182-184 says that refusals were made to "name themselves" because a bare message could not be told apart. A stranger who changes one refusal leaves two behind. — Fix: one helper that returns the model or the refusal text, used by all three.
Cost: unsure if the messages are reconciled — I did not check whether `UTA-0012-ut-dump-output-shape.md` pins the error text. A merge that keeps each message is one edit. Searched: `docs/specs`, `docs/standards` and `design.md` for `the level's Model did not read`, and `docs` for the function names (only review records, no spec). Candidate `dup:e5ca27669486`.

[Q1] MEDIUM `tools/ut-dump/Cli.cpp:952` — `if (const auto site = uta::upkg::resolveClass(map, mapName, entry.objectClass, resolver);` — The walk "resolveClass → readAncestry → effectiveDefaults, then the actor's own properties over the top" has five copies:
- `kindOf` 469
- `writeActorWiring` 789-852
- `writeExits` 952-971
- `writeWaypoints` 1031-1048
- `writeBrushes`' `classOf` 1336-1383

The "level actors as a deduplicated export set" loop appears at 769, 939, 1018 and 1360, and in different forms at 543, 564 and 590. The copies have diverged: only `writeActorWiring` treats a chain that `readAncestry` refuses as unresolved. That is the fix recorded at 823-827 ("review-code 2026-09-26"). In `writeExits`, `writeWaypoints` and `writeBrushes` the same refusal silently yields no class defaults, so for example a blank exit `tag`. A stranger who fixes the walk in one place reproduces the 2026-09-26 situation. — Fix: one class-facts walk (chain, end, defaults) shared by the five sites.
Cost: one edit — Searched: `docs` for `writeActorWiring|writeExits|writeWaypoints|writeBrushes|takeWiringProperty|takePosition|dumpPackage|…`; the only hits are review records under `docs/reviews/`, not specs. Candidates `dup:c14603cf0ba7` and `dup:530684f20dfc`.

[Q2] MEDIUM `tools/ut-dump/Cli.cpp:789` — `if (entry.objectClass.kind() != uta::upkg::ObjectReferenceKind::Null) {` — `writeActorWiring` loses the reader here. A 48-line cache-fill sits inside the per-actor loop and nests eight deep (`switch` inside `if (ancestry)` inside `if (site)` inside `if (found == end)`). The reader must hold all of that before reaching the actor's own output at 846. — Fix: extract the cache-fill into a `classWiringOf(key)` function returning `const ClassWiring&`.
Cost: one edit — Searched: as above, no spec names `writeActorWiring`. Candidate `tools/ut-dump/Cli.cpp:754:writeActorWiring`.

[Q4] MEDIUM `tools/ut-dump/Cli.cpp:1330` — `/// with its collision; \`actors\`, every other actor's Location.` — The code puts each blocker into `blockers` and also into `actors`, because there is no `continue` after 1444. The spec says `actors` holds "every actor not in `list`, blockers included" (`UTA-0012-ut-dump-output-shape.md:373`). The help text at 1696 says "every other actor" too. A stranger reading either would take the missing `continue` for a bug and add it. — Fix: say "every actor not in `list`, blockers included" in the comment and in the help text.
Cost: one edit (comment and help text). Searched: `blockers` tree-wide. `tests/unit/DumpCliTest.cpp:1521` pins the code's behaviour.

[Q4] MEDIUM `tools/common/Json.h:4` — `// Header-only. ut-dump, ut-bake and ut-paths each hand-write an output shape` — The header names three users. It is also included by ut-bench (`Cli.cpp:7` and `Frame.cpp:5`) and ut-bound-scan (`Cli.cpp:7`). ut-bound-scan's output is the hand-off agreed with UT_MonsterHunt (`ut-bound-scan/Cli.h:6`). A stranger changing `writeJsonString` checks the three named tools and misses that contract. Line 11 ("all three tools") is history about the UTA-0202 defect. — Fix: drop the tool list, or point at `uta_tools_common`'s link sites.
Cost: one edit (comment). Searched: `common/Json.h` tree-wide.

[Q1] MEDIUM `tools/ut-bake/Cli.cpp:103` — `const auto takeValue = [&](std::optional<std::string_view>& into) {` — The "take the next argument as this option's value" helper has six hand copies:
- ut-bake `Cli.cpp:103`
- ut-paths `Cli.cpp:65`, the same as ut-bake apart from the tool name
- ut-bench `Cli.cpp:78`
- ut-bench `Frame.cpp:55`
- ut-shot `Cli.cpp:122`
- ut-dump `Cli.cpp:1739-1747`, inline

They have diverged. ut-bake and ut-paths refuse an option given twice; ut-bench, ut-shot and ut-dump silently keep the last value. ut-dump prints only the usage text for a missing value, with no message saying why. Within ut-bench, `number` (`Cli.cpp:85`, which takes a minimum) and `count` (`Frame.cpp:62`, minimum hard-coded to 1, through `whole`) are a second diverged pair. A stranger who fixes "given twice" in one tool assumes it holds everywhere. — Fix: one shared option-value helper for the tools.
Cost: unsure — the natural home is `tools/common/`, which UTA-0121 § 4.8 describes as holding only the escaper. Searched: as for the camera finding. Candidate `dup:94ee2d1a3489` (confirmed by reading `tools/ut-paths/Cli.cpp:55-85`).

[Q1] MEDIUM `tools/ut-bench/Frame.cpp:304` — `out << "{\"schema\": 1, \"workload\": \"frame\", ";` — The frame workload writes the schema as a literal 1. The bake workload writes `SCHEMA` (`Cli.cpp:28`, `:362`). Both print the shared `writeMachineAndBuild` members. A stranger who bumps `SCHEMA` for a change to those shared members leaves frame output claiming schema 1. The `EXIT_*` constants are also declared twice (`Cli.cpp:30-32`, `Frame.cpp:28-30`), and `runCli` compares `runFrame`'s return value against its own copy (`Cli.cpp:343`). — Fix: declare `SCHEMA` and the exit codes once, in `Cli.h`'s `detail`.
Cost: one edit — no name is moved that a spec names. Searched: `docs/specs` for `Frame.cpp`; UTA-0129:370 names only the file.

[Q2] MEDIUM `tools/ut-shot/main.cpp:65` — `if (bundle->lightProbes.has_value()) {` — `main` loses the reader here. Between loading the bundle (51) and creating the renderer (125) sit 55 lines of diagnostics: probe luma statistics, a triangle-area coverage calculation five levels deep, and a dump of every light. None of it affects the drawing, but a stranger tracing "what does ut-shot draw" must read through all of it. — Fix: move the probe and light diagnostics into named functions above `main`.
Cost: one edit — Searched: `docs/specs` and `docs/standards` for `ut-shot/main` (no hits). Candidate `tools/ut-shot/main.cpp:35:main`.

[Q4] LOW `tools/ut-dump/Cli.cpp:148` — `/// UTA-0201: the map's BSP surfaces, grouped by the texture they use and the` — This 27-line doc block describes `writeSurfaces`, but it sits directly above the forward declaration of `writeZoneActor` (174-175). Both a reader and a doc tool attach it to the wrong function. — Fix: move the forward declaration above the block.
Cost: one edit (moves a declaration only).

[Q4] LOW `tools/ut-bake/Cli.cpp:63` — `"and why not. The second form bakes one map into <dir>, named by the\n"` — The help text lists four forms: `--check`, `--game-types`, the bake, then `--help`. The bake is the third, not the second. Line 70 also hard-codes "its 10 largest textures", a hand copy of `BUDGET_LIST_SHORT` (38) that goes stale when the constant changes. — Fix: say "the third form", and build the number from the constant.
Cost: one edit (help text).

[Q4] LOW `tools/ut-bench/Cli.h:77` — `std::vector<PhaseSummary> phases; ///< in the first run's order` — `summarise` appends each phase path the first time any run shows it (`Cli.cpp:316-320`). A phase first seen in run 2 therefore comes after all of run 1's phases, the case `writeTable`'s own comment (`Cli.cpp:236-238`) describes. A stranger who relies on the documented order to find parents would mis-nest such a phase. The file header (`Cli.h:4`) and `runCli`'s exit-code doc (`Cli.h:35-37`) also describe only the bake workload, not frame. — Fix: "in the order runs first show them"; mention frame.
Cost: one edit (comment).

[Q3] LOW `tools/ut-bench/Frame.cpp:185` — `std::string fixed(double value) { return std::format("{:.3f}", value); }` — `Cli.cpp:165` defines a `fixed` in the same `uta::bench` namespace, each in an anonymous namespace, with `{:.6f}`. A stranger moving code between the two files gets a silent change of precision in the JSON. — Fix: name them by precision or purpose (for example `milliseconds3` and `seconds6`).
Cost: one edit — both are file-local; no spec names them (searched `docs/specs` for `Frame.cpp`).

[Q1] LOW `tools/map-pic/Cli.cpp:30` — `std::string foldCase(std::string_view text) {` — There are private case-folders in map-pic (30), ut-dump (`Cli.cpp:70`), `src/unav/Build.cpp:48` and `src/upkg/Class.cpp:26`. Meanwhile `ubake::detail::fold` (`src/ubake/Install.h:110`) is public, and ut-bake and ut-bound-scan already call it. Both map-pic and ut-dump link `uta_ubake`. They behave the same today. — Fix: call `ubake::detail::fold` in the two tools.
Cost: one edit — Searched: `docs/specs`, `docs/standards` and `design.md` for `foldCase` (no hits). Searched: the fold-function regex tree-wide.

[Q1] LOW `tools/ut-bound-scan/Cli.cpp:35` — `std::string hex(const std::array<std::byte, 16>& digest) {` — This is a private hex encoder, while `ubake::detail::hex(std::span<const std::byte>)` (`src/ubake/Name.h:93`) is declared in a header this file already includes (`ubake/Name.h`), and ut-bench uses it. — Fix: call `ubake::detail::hex`.
Cost: one edit — Searched: `std::string hex\(` tree-wide.

[Q2] LOW `tools/ut-dump/Cli.cpp:1452` — `bool navGraph, bool wiringGraph, bool surfaceList, bool lighting, bool brushes, bool first) {` — The function's length is fine: it is a flat sequence of sections. The hazard is six positional bools. The call sites end in `…, lighting, brushes, true)` (1833) and `…, brushes, first)` (1842). A stranger adding a seventh flag can put it in the wrong position and still compile. — Fix: pass an options struct.
Cost: one edit — Searched: `docs` for `dumpPackage`; only a review record, no spec. Candidate `tools/ut-dump/Cli.cpp:1452:dumpPackage` (confirmed for this hazard only, not for length).

[Q5] LOW `tools/ut-dump/Cli.cpp:1415` — `list << (firstKey ? "" : ", ") << key.second;` — `keyRot` writes the array indices of the stored `KeyRot` slots, not their rotations. No comment says so. The spec does (`UTA-0012-ut-dump-output-shape.md:360`), so a stranger "fixing" it to write the values would be corrected if they read the spec. — Fix: a one-line comment naming the spec's definition.
Cost: one edit (comment). Searched: `keyRot` in UTA-0012.

## Candidates dismissed
- `tools/ut-dump/Cli.cpp:1452:dumpPackage` — for size: a flat sequence of labelled sections that reads top to bottom. Its parameter hazard is reported above.
- `tools/ut-dump/Cli.cpp:1725:runCli` — parse, list, open the resolver, write: linear and readable.
- `tools/ut-shot/Cli.cpp:115:parseOptions` — one flat option chain, then two clearly commented branches.
- `tools/ut-dump/Cli.cpp:1333:writeBrushes` — the `classOf` lambda is counted in the ancestry-walk finding; the rest is three labelled output arms.
- `tools/ut-bench/Frame.cpp:231:runFrame` — load, start, draw, write, table: every step is short and in order.
- `tools/ut-bake/Cli.cpp:326:runBake` — request, JSON, then a switch on the verdict; readable.
- `tools/ut-dump/Cli.cpp:176:writeSurfaces` — the length follows its sections. Its comment problems are reported separately (180, 148).
- `tools/ut-ref/main.cpp:89:trace` — argument checks, load, then a camera loop; readable. Its camera copy is reported under Q1.
- `tools/ut-bound-scan/Scan.cpp:41:scanModel` — three passes, each introduced by a comment.
- `tools/ut-bake/Cli.cpp:246:writeResult` — a long flat writer of one JSON shape. Its two identical skipped-texture lambdas (266-272, 285-291) are a pair with no record of a past fix to both.
- `dup:e5ca27669486` — confirmed and widened into the three-copy model-guard finding.
- `dup:c14603cf0ba7`, `dup:530684f20dfc` — confirmed, folded into the five-copy ancestry-walk finding.
- `dup:94ee2d1a3489` — confirmed, folded into the six-copy option-value finding.
- `tools/{map-pic,ut-bake,ut-bench,ut-bound-scan,ut-dump}/CMakeLists.txt` census "2 lets"/"2 permits" — "2" is design.md's rule number, not a count. Rule 2 exists and is about this (`docs/design.md:82`).
- `tools/ut-dump/Cli.cpp:161` "587 maps", `:741` "2905 actors", `:1396-1397` "84 brushes"/"666 others" — dated measurements recorded as the reason for the code, not live counts.
- `tools/ut-ref/Reference.cpp:21` "432 headers" — a version name (UT 432), not a count.
- `tools/ut-ref/Reference.h:75`, `:77`, `:89` "8 blocks", "48 pixels", "3 floats" — each matches the code today (`BLOCK = 8` and `KEEP = 48` at `Reference.cpp:195-196`, and the `terms` layout).

## Correctness leads
- `tools/ut-bench/Cli.cpp:186-190` — builds paths with `std::filesystem::path(std::string_view)` where ut-bake uses `uta::fs::pathFromUtf8` (`ut-bake/Cli.cpp:330-338`). A non-ASCII install or map path may be read wrongly on Windows. — Searched: compared against ut-bake's `runBake` only.
- `tools/ut-bake/Cli.cpp:428` — the `OverBudget` arm uses `outcome->result->budget` with no `has_value()` check. Every other use in this function checks first. — Searched: none.
- `tools/ut-bench/Frame.cpp:82-90` — `--size` has no upper bound. ut-shot refuses sizes above `MAX_DIMENSION` because "an unchecked value sized the readback before the device said so" (`ut-shot/Cli.h:27-30`). The bench does no readback, so this may be harmless. — Searched: none.
- `tools/ut-dump/Cli.cpp:952-971`, `:1031-1048` — when `readAncestry` refuses a chain, `exits` and `waypoints` silently lose their class defaults (for example, `tag` becomes `""`) with no flag. `writeActorWiring` reports the same case as unresolved. — Searched: none beyond the file.

## Not checked
- The rest of `docs/specs/UTA-0012-ut-dump-output-shape.md`. I read only the `blockers` and `keyRot` lines, so I cannot say whether it pins ut-dump's refusal texts. That is why the model-guard reconciliation is graded unsure.
- The `ubake`, `upkg` and `unav` library functions the tools call, beyond confirming that `findModel`, `fold` and `hex` exist and are declared where cited.
- How this run went:
  - One `mcp__ants__workspace_search` was rate-limited (the `docs/design.md` search). I re-ran it with `Grep`, so no check was left unrun.
  - Every search I relied on returned `truncated: false`.
  - I disagreed with neither the brief nor the lane file, so I resolved no disagreement between them.
