# close-findings: lane 11, the ut-ants app

Closes every finding in
[`lane-11-app-ut-ants.md`](lane-11-app-ut-ants.md) under `UTA-0422`, its
correctness leads under `UTA-0408`, and lane 04's carried-in note: `main.cpp`
wrote its own class-ancestry test. Run 2026-10-23. Roadmap lookup:
`UTA-0422` and `UTA-0408` exist, both planned before this run.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| L1 | P records the live camera, not `shownView` | lead | — | yes, narrowly: the camera moves only at `update()`, so the two differ only when [ or ] came earlier in the same batch of events, or before the first frame | fixed, `6c8b04b` | `main.cpp` | P writes `poseLine(camera.camera(...))` → `shownView`, else the camera; the comment above `shownView` said a mouse motion moved the camera, and now names the field of view. No red run: the SDL half has no test harness. Xvfb run: P's line and the capture's `camera.txt` match | F12's capture: agrees |
| L2 | empty baker version reads "older ut-bake" | lead | — | yes, `bakeState` returned `OlderBaker` for an empty `currentBaker` | fixed, `6c8b04b` | `MapList.h`, `MapList.cpp`, `Launcher.cpp`, `ClientMapListTest.cpp` | → `BakeState::BakerUnknown`, shown as "ut-bake did not say its version". Red: `2 == 1`. Both branches mutated, both killed | `isCurrentBake`: agrees (still false) |
| L3 | narrow strings instead of `fs::utf8` | lead | — | no: `CMakeLists.txt` links `cmake/utf8.manifest` into every MSVC program, so the narrow encoding is UTF-8 there too; `CoreFileSystemTest.cpp` "the narrow encoding is UTF-8 on every platform" checks it | dismissed | — | no edit. Windows not run here | n/a |
| L4 | launcher gamepads: handle dropped, failure silent | lead | — | half: the handle is not lost (SDL finds it by ID at REMOVED; SDL_Quit closes the rest); the silent failure is real | fixed, `6c8b04b` | `Launcher.cpp` | → reports "a gamepad did not open", as `main.cpp`'s `Gamepads` does; the handle's keeper is said beside the call. Built, not run with a pad | n/a |
| L5 | `create_directories` error ignored | lead | — | yes: the refusal then named the capture folder and a reason about the directory above it | fixed, `6c8b04b` | `Capture.cpp`, `ClientCaptureTest.cpp` | → refused as "cannot make the captures directory". Red on its `starts_with` check | n/a |
| C1 | `main.cpp` repeats `ubundle::isOrDescendsFrom` | lane 04 | — | yes; both test the path, then each ancestry entry, by equality | fixed | `main.cpp` | → `isOrDescendsFrom(actorClass, "engine.playerstart")`. Xvfb run started at the map's PlayerStart, as before | `Bundle.h` doc: agrees |
| 1 | launcher rebuilds the viewer's command line by hand | Q1 | HIGH | yes | fixed | `Cli.h`, `Cli.cpp`, `Launcher.cpp`, `ClientCliTest.cpp` | → `inheritedArguments(Options)` beside `parseArguments`; `Options`' doc names it; the launcher appends its result. New round-trip test; the body emptied, two options dropped, the field of view rounded and the tier always passed: all five killed | usage text: agrees |
| 2 | help text's Tab description is stale | Q1 | MEDIUM | yes | fixed | `Cli.cpp` | → the four-stop cycle, and marks among what is kept | `Launcher.cpp` hints: agree |
| 3 | help text hard-codes the field-of-view values | Q1 | MEDIUM | yes | fixed | `Cli.cpp` | → streamed from `FlyCamera`'s constants; `--help` still prints "90, UT99's own, without it; 60 to 130." | `FlyCamera.h` constants: agree |
| 4 | `pageRows` and `draw` compute the list height apart | Q1 | MEDIUM | yes | fixed | `Launcher.cpp` | → `listRows(Grid)` and `LIST_TOP`, used by both; a page is one row short, said as keeping the edge row in view. Same numbers as before | n/a |
| 5 | `installIsUsable` repeats `besideThisProgram` | Q1 | MEDIUM | yes | fixed | `Launcher.h`, `Launcher.cpp`, `main.cpp` | → declared in `Launcher.h`, called by `installIsUsable`. Xvfb run passed the install check | n/a |
| 6 | `writeMarks` saves through `writeNotes` | Q3 | MEDIUM | yes | fixed | `MapList.cpp` | → both call a file-local `writeOrRemove` | `MapList.h` docs of both: agree |
| 7 | `loadNotes` and `notesFor_` also cover marks | Q3 | LOW | yes | fixed | `Launcher.cpp` | → `loadSelectedMap`, `loadedMap_`, each with a line saying it covers marks | constructor comment: fixed in the same pass |
| 8 | Tab's `4` and `3` hard-code the `Focus` count | Q5 | MEDIUM | yes | fixed | `Launcher.cpp` | → `FOCUS_STOPS`, from `Focus::Notes`; Shift+Tab is `FOCUS_STOPS - 1` | n/a |
| 9 | `run()`'s event chain splits one action's bindings | Q2 | MEDIUM | yes | fixed in part; the lift queued as `UTA-0430` | `main.cpp` | → quit and the flashlight each take one branch for key and pad. Lifting the capture lambda and the dispatch is a restructure: queued | n/a |
| 10 | `Capture.h` folder name omits the `Z` and the suffix | Q4 | MEDIUM | yes | fixed | `Capture.h` | → `…HHMMSS>Z`, and the `-2`, `-3` suffix | `ClientCaptureTest.cpp`: agrees |
| 11 | `main.cpp` cites UTA-0051 SS 4.5 for dynamic resolution | Q4 | LOW | half: SS 4.5 is where ut-ants always sets it | fixed | `main.cpp` | → cites both: SS 4.5 asks, SS 4.4 says what it does | spec headings: agree |
| 12 | `main.cpp` header names two of five window-free files | Q4 | LOW | yes | fixed | `main.cpp` | → points at `apps/ut-ants/CMakeLists.txt` | n/a |
| 13 | `Cli.cpp` header says nothing is remembered | Q4 | LOW | yes | fixed | `Cli.cpp` | → the install and a map, or no map for the launcher | n/a |
| 14 | `CaptureInfo::map` omits the bundle-stem case | Q4 | LOW | yes | fixed | `Capture.h` | → adds the bundle's stem without --notes | `Cli.h` `notes` doc: agrees |
| 15 | the render-scale sentence sits in the wrong paragraph | Q4 | LOW | yes | fixed | `main.cpp` | → moved into the pinning paragraph | n/a |
| 16 | the list label's `7` | Q5 | LOW | yes | fixed | `Launcher.cpp` | → `TAG_ROOM`, from "FAILED" and a space | n/a |
| 17 | text scale limits repeated, floors differ unexplained | Q5 | LOW | yes | fixed | `Launcher.cpp` | → `MIN_SCALE`, `MAX_SCALE`, `MIN_STARTING_SCALE`, one comment over the three. No reason for the floor of 2 is recorded anywhere, so none is invented | n/a |
| 18 | Ctrl drops text, pad press returns to list, Ctrl+F list-only: no reasons | Q5 | LOW | yes | fixed | `Launcher.cpp` | → a line on each | n/a |
| 19 | `lowered` three copies | Q1 | LOW | yes: neither client copy is left | fixed by lane 07 (`close-lane-07.md`), before this run | — | both sites call `uta::asciiFolded` | n/a |

## Run-level

- cited_by: tree search, review records left out, for `finishChild`,
  `loadNotes`, `notesFor_`, `writeNotes`, `besideThisProgram`,
  `installIsUsable`, `pageRows`, `older ut-bake` and `Tab moves`: only
  `apps/ut-ants/`, its tests and `ROADMAP.md`. No spec names a changed
  symbol. `ROADMAP.md`'s UTA-0357 note quoting "60 to 130" is a dated
  record: frozen, and still true.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
  Xvfb runs: the viewer drew, P's line matched the capture's `camera.txt`,
  Escape quit with 0; the launcher drew, took Page Down and the Tab cycle,
  and quit with 0 on Escape.
- collateral: none.
- surfaced: none.
- out_of_scope: `MapList.cpp`'s other `path::string()` sites, the same
  non-defect as L3.
- falsified: none.
