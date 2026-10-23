## Files read
- `apps/CMakeLists.txt` — 4 / 4
- `apps/ut-ants/BuildCommit.h.in` — 12 / 12
- `apps/ut-ants/CMakeLists.txt` — 114 / 114
- `apps/ut-ants/Capture.cpp` — 139 / 139
- `apps/ut-ants/Capture.h` — 99 / 99
- `apps/ut-ants/Cli.cpp` — 185 / 185
- `apps/ut-ants/Cli.h` — 73 / 73
- `apps/ut-ants/FlyCamera.cpp` — 194 / 194
- `apps/ut-ants/FlyCamera.h` — 99 / 99
- `apps/ut-ants/Launcher.cpp` — 815 / 815
- `apps/ut-ants/Launcher.h` — 15 / 15
- `apps/ut-ants/MapList.cpp` — 554 / 554
- `apps/ut-ants/MapList.h` — 189 / 189
- `apps/ut-ants/Png.cpp` — 68 / 68
- `apps/ut-ants/Png.h` — 45 / 45
- `apps/ut-ants/WriteBuildCommit.cmake` — 39 / 39
- `apps/ut-ants/main.cpp` — 497 / 497
- Brief: `shared-context.md` — 272 / 272

## What I started with
- The global `~/.claude/CLAUDE.md`.
- A SessionStart hook message saying the Ants MCP is connected, with tips on its verbs.
- The userEmail context block.
- The review-lane agent instructions.
- No memory index, no git snapshot, and no project `CLAUDE.md`.

No disagreement between the brief and the lane file needed settling.

## Summary
This is the game program. Given an Unreal Tournament install and one prepared ("baked") map, it opens a full-screen window and lets the player fly a camera through the map with mouse, keyboard or gamepad. The camera stops at walls. Given only the install, it opens a map-picker window instead. The picker lists the maps, prepares the chosen one with the separate `ut-bake` tool, and then starts a second copy of the game to view it. It also keeps notes and labels for each map. A key press saves a "capture": the current picture, a second unprocessed picture, and a text file describing the view, so the view can be redrawn later.

## Findings

[Q1] HIGH `Launcher.cpp:389` — `std::vector<std::string> args{besideThisProgram("ut-ants")};` — The launcher rebuilds the viewer's command line by hand (lines 389–413). This is a third hand-kept copy of the option set, after `Options`/`parseArguments` (Cli.cpp) and the usage text. A stranger adding a viewer option (as `--fov` and `--no-added-lamps` were added) would add it to `Options`, the parser and the usage text. They would miss this list, and nothing near the parser points here. The parser accepts the option in launcher mode, so the launcher would silently drop it and the viewer would never receive it. — Fix: put the Options→arguments conversion beside `parseArguments` in Cli.cpp, or at least add a comment on `Options` naming `Launcher::finishChild`.
Cost: one edit — Searched: `docs/` for `besideThisProgram|writeNotes|loadNotes|pageRows|installIsUsable|notesFor_|writeCaptureFolder` (only review records hit, no spec). No ANCHORS.

[Q1] MEDIUM `Cli.cpp:28` — `"Up and Down pick, typing filters the list, Enter opens, Tab moves to the\n"` / `"notes and back, …"` — The help text says Tab toggles between the list and the notes. Since UTA-0386, Tab cycles through four stops: list, name, duplicate, notes (`Launcher.cpp:492-494`). The on-screen hint (`Launcher.cpp:589`, "Tab: fields") was updated; the help text was not. The help also never mentions the visual-name and duplicate fields. A stranger trusting the help would misdescribe the launcher's keys. — Fix: update the launcher paragraph of `usage()` to match the four-stop Tab cycle and the two mark fields.
Cost: one edit — Searched: whole tree outside `apps/` for `Tab moves` (no test or spec pins it).

[Q1] MEDIUM `Cli.cpp:61` — `"UT99 counts it. 90, UT99's own, without it; 60 to 130.\n"` — The help text hard-codes the values of `DEFAULT_FOV`, `MIN_FOV` and `MAX_FOV` (FlyCamera.h:66-68). The parser's error message 60 lines further down uses the constants. A stranger changing the field-of-view limits would update the constants and leave the help stating the old range. — Fix: format the help line from the `FlyCamera` constants, as the error message at Cli.cpp:127 does.
Cost: one edit — Searched: tree outside `apps/` for `60 to 130` (only ROADMAP.md, which is not a spec).

[Q1] MEDIUM `Launcher.cpp:551` — `return std::max(1, g.rows - 6 - static_cast<int>(disclaimer(g).size()));` — `pageRows()` recomputes the list height that `draw()` computes at line 602 (`g.rows - top - 2 - disclaimerRows`, with `top = 3`, which gives rows − 5). The two copies differ by one row, and no comment says whether that is deliberate (paging one row short to keep context?). A stranger changing the layout would fix `draw()` and leave PageUp/PageDown paging by a stale amount. — Fix: compute the visible list height in one helper used by both, with a comment if the −1 is intended.
Cost: one edit — Searched: `docs/` for `pageRows` (nothing).

[Q1] MEDIUM `main.cpp:56` — `const char* const base = SDL_GetBasePath();` — `installIsUsable` (main.cpp:56-62) and `besideThisProgram` (Launcher.cpp:38-46) are two copies of "find a program beside this one". Both carry the same ticket comment, `// SDL's strings are UTF-8 both ways (UTA-0221)`, which shows a past fix had to patch both. The next fix (for example a different Windows suffix) would also be needed twice. — Fix: expose `besideThisProgram` (e.g. in MapList.h or a small shared header) and call it from `installIsUsable`.
Cost: one edit — Searched: tree for `SDL_GetBasePath(` (exactly these two sites) and `docs/` for `besideThisProgram|installIsUsable` (no spec).

[Q3] MEDIUM `MapList.cpp:507` — `return writeNotes(marks, map, text);` — `writeMarks` saves the marks file through `writeNotes`, using its rule that empty text deletes the file. The name says "notes", so a stranger changing how notes are saved (keep empty files, add a header, change the extension) would also change marks without knowing. — Fix: rename `writeNotes`'s body to a neutral "write or remove a per-map text file" helper that both call.
Cost: unsure — `docs/` has no hit for `writeNotes`, but I did not search the test tree for direct callers of `writeNotes`, which a rename would touch.

[Q3] LOW `Launcher.cpp:246` — `void loadNotes() {` — This function also re-reads the selected map's marks and resets `marksReadable_`. `notesFor_` is likewise the key `editMarks` saves marks under (line 310). A stranger changing how marks load would not look in `loadNotes`. The comment at line 205 hints at it ("(loadNotes)"). — Fix: rename to something like `loadSelectedMapText` / `selectedMap_`.
Cost: one edit — Searched: `docs/` for `loadNotes|notesFor_` (only a review record).

[Q5] MEDIUM `Launcher.cpp:494` — `focus_ = static_cast<Focus>((static_cast<int>(focus_) + step) % 4);` — The `4`, and the `3` on line 493 meaning "go back one", hard-code the number of `Focus` values. The enum's comment (line 186, "Tab's order") states the ordering but not this count. A stranger adding a fifth field to the enum would get a Tab cycle that never reaches it. — Fix: derive the count from the enum (a trailing count value or a named constant), and comment that Shift+Tab is "count − 1".
Cost: one edit — Searched: `docs/` for the names above (nothing).

[Q2] MEDIUM `main.cpp:318` — `while (SDL_PollEvent(&event)) {` — `run()` mixes renderer setup, a 70-line capture lambda (240–312) and a 13-branch `if/else` event chain. The thread is lost in that chain: the same action is bound inconsistently. P (335) and F12 (347) combine key and pad in one branch. The flashlight is split between line 324 (F) and line 332 (pad North), with the field-of-view branch between them. Quit is likewise split between 319 and 321. A stranger rebinding the flashlight would find one branch and miss the other. — Fix: lift the capture lambda and the event dispatch into named functions, and group each action's key and pad bindings in one branch.
Cost: one edit — Searched: `docs/` for `writeCaptureFolder` (review record only, no spec).
Confirms `apps/ut-ants/main.cpp:186:run`.

[Q4] MEDIUM `Capture.h:64` — `/// The folder one press gets: \`<map>-<YYYYmmdd-HHMMSS>\`, the time in UTC.` — The code appends `Z` (Capture.cpp:57, UTA-0372), and the test expects `MH-Example-20260920-143005Z`. A stranger writing a tool or script from the header would match the wrong pattern. The correction is only in the .cpp file. — Fix: add the `Z` (and the `-2`, `-3` suffix for a clash) to the header comment.
Cost: comment only. Searched: `tests/unit/ClientCaptureTest.cpp` for `captureFolderName` (asserts the `Z` form).

[Q4] LOW `main.cpp:209` — `config.dynamicResolution = true; // UTA-0051 SS 4.5` — Capture.h:45 cites "UTA-0051 SS 4.4's dynamic resolution" for the same feature. In the spec, § 4.4 is "Dynamic resolution" and § 4.5 is "`ut-ants`". Each citation is defensible, but a stranger following main.cpp's would land on the section about ut-ants, not the dynamic-resolution rules. — Fix: cite § 4.4 (or both) at main.cpp:209.
Cost: comment only. Searched: `docs/specs/UTA-0051*` headings.

[Q4] LOW `main.cpp:5` — `// launcher (UTA-0170, Launcher.cpp). Everything that needs no window is in Cli.cpp and FlyCamera.cpp, which the unit tests compile too.` — This is incomplete: MapList.cpp, Png.cpp and Capture.cpp are also window-free and compiled into the tests (CMakeLists.txt:3-4). A stranger would believe this file's helpers are the only SDL code, or would put window-free code in the wrong place. — Fix: name all five files, or point at the CMakeLists comment.
Cost: comment only.

[Q4] LOW `Cli.cpp:1` — `// …the install and the map are both named on the command line, and nothing is remembered between runs.` — Since UTA-0170 the map is optional, and the launcher remembers bakes, notes, results and marks between runs. History has buried the current shape. The usage text directly below corrects it, so a stranger recovers quickly. — Fix: drop or update the second sentence.
Cost: comment only.

[Q4] LOW `Capture.h:35` — `std::string map; ///< the map's name, as the launcher lists it` — For a run opened directly from the command line, main.cpp:237-238 sets this to the bundle's stem, which is a content hash. That case is documented in Cli.h:52-55, so the stranger can recover. — Fix: add "or the bundle's stem when no notes file was given".
Cost: comment only.

[Q4] LOW `main.cpp:271` — `// The scale too: dynamic resolution may have moved it since the shown frame, and the redraw must not steer it (UTA-0372).` — This is appended to the end of the paragraph about the flash and motion vectors. It belongs with the light-time pin paragraph (259–262) that explains `pinRenderScale`. A stranger reading top-down attaches it to the wrong subject. — Fix: move the sentence into the pin paragraph.
Cost: comment only.

[Q5] LOW `Launcher.cpp:656` — `text(column, row + i, clipped(label, width - 7));` — The `7` is the room kept for the "FAILED"/"baked" tag plus a space. Nothing says so, and a longer tag would overlap the label. — Fix: derive it from the longest tag, or comment it.
Cost: one edit.

[Q5] LOW `Launcher.cpp:211` — `scale_ = std::clamp(static_cast<int>(height / 360.0 + 0.5), 2, 12);` — The limit `12` is repeated at line 486. The automatic floor is 2 but the manual floor at line 487 is 1, and the difference is unexplained. A stranger changing the maximum would edit one site. — Fix: named min/max constants, with a note on why the automatic floor differs.
Cost: one edit.

[Q5] LOW `Launcher.cpp:459` — `if ((SDL_GetModState() & SDL_KMOD_CTRL) != 0) break;` — Typed text is dropped while Ctrl is held, with no reason given (presumably so Ctrl+F, Ctrl+= and Ctrl+- do not also type). Similarly, line 473 sends any gamepad button press back to the list, and line 488 allows Ctrl+F only from the list. — Fix: one-line comments on each.
Cost: comment only.

[Q1] LOW `MapList.cpp:32` — `std::string lowered(std::string_view text) {` — There are three copies in the tree: MapList.cpp:32, main.cpp:43 (identical, using `std::tolower`) and src/urecipe/Recipe.cpp:30 (ASCII-only, which its comment ties to `umat::materialId`). The two client copies are the same, so a fix to one would be needed in both. — Fix: share one client-side helper, keeping Recipe.cpp's deliberate ASCII version separate.
Cost: one edit — Searched: tree for `std::string lowered\(`.

## Candidates dismissed
- `apps/ut-ants/Cli.cpp:66:parseArguments` — Length alone. It is a flat run of self-contained option blocks, and the empty-value/given-twice coupling for `--notes`/`--baker-version` is stated at line 147.
- `Capture.h:87`, `Cli.h:60`, `MapList.h:55` (the "2 keeps" count) — This is a rule number, not a count. `docs/design.md` rule 2 is "Neither runtime target links upkg, umat or ubake" (searched).
- `FlyCamera.cpp:28` ("26 directions") — This is 3³−1 and matches the `x/y/z ∈ {-1,0,1}` loop and `std::array<Push, 26>`. A fixed geometric fact.
- `FlyCamera.cpp:107` ("65536 units") — This is UT's rotator unit. A fixed format fact.
- `FlyCamera.h:63` ("90 across") — A dated measurement of a fixed engine behaviour, not a count of anything in the code.
- `MapList.cpp:357` ("Line 2 names") — Correct. The one-based file lines 2–5 match `writeResult`'s output and `at(1)`–`at(4)`.
- `Png.h:14` ("Rule 19 permits it") — This is a rule number. `docs/design.md` rule 19 is "Any dependency these rules do not prohibit is allowed" (searched).

## Correctness leads
- `main.cpp:340` — P records `camera.camera(...)`, the camera as it currently stands, not `shownView`. That is the mismatch lines 228–231 say F12 avoids, so a P line can differ from the frame on screen. — Searched: none.
- `MapList.cpp:107` (`BakeState::OlderBaker` when `currentBaker.empty()`, used by `Launcher.cpp:691`) — When ut-bake reported no version, the details pane says "Baked by an older ut-bake", which is not what happened. — Searched: none.
- `main.cpp:238`, `main.cpp:449`, `main.cpp:464`, `MapList.cpp:44`, `MapList.cpp:266`, `Capture.cpp:24`, `Cli.cpp:141`, `Cli.cpp:166-168` — These use `path::string()` or a narrow `std::string` constructor where the rest of the code uses `fs::utf8`/`fs::pathFromUtf8` (UTA-0221). On Windows, non-ASCII map names or paths may be mangled. — Searched: none beyond these files.
- `Launcher.cpp:447` — The launcher opens gamepads and discards the handle, and silently ignores an open failure. main.cpp's `Gamepads` keeps and closes them and reports failure. The two copies have diverged. — Searched: none.
- `Capture.cpp:105` — The error from `create_directories(captures, made)` is ignored. The next step's error covers it, so this is probably harmless, but the reason is unstated. — Searched: none.

## Not checked
- The test tree, for direct callers of `writeNotes` (affects the cost of that rename).
- Whether `tests/unit/RenderCameraTest.cpp` exists as a file. It is listed in `tests/CMakeLists.txt:109`; I did not open it.
- Whether `ut-bake`'s real `--game-types` and bake output match `JsonScanner`'s assumptions.
- The CMake link-closure walk (CMakeLists.txt:66-113): I read it but did not judge it against CMake's semantics for every generator expression.
