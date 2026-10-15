**Line counts as read** (wc-style; `Read` also showed one empty row after each last line): Capture.cpp 131 · Cli.cpp 181 · Cli.h 67 · FlyCamera.cpp 194 · FlyCamera.h 99 · Launcher.cpp 698 · MapList.cpp 490 · MapList.h 170 · WriteBuildCommit.cmake 37 · main.cpp 488.

**Already in my context when I arrived:**
- The user's global `~/.claude/CLAUDE.md`, which names `review-code` and `review-contract`.
- The user's email address.
- The `review-lane` agent prompt.
- No git snapshot and no project `CLAUDE.md`.

**How the brief and the lane file were reconciled:** the brief's output starts at `## Critical`, but the lane file says the opening of the return belongs to it. So the line counts and the list above come first, and then the brief's format follows.

**Reads beyond the lane files:**
- From the contracts I read only the sections that bind this code: UTA-0051 §§ 3 and 4.3–6, and UTA-0014 § 4.12.
- I opened `standards/languages/cpp.md` in full.
- To check my own suspicions I ran five searches, on `linearOutput`, the UTF-8 manifest, `sanitised`, `BuildCommit` and the tile-answer warnings. These ruled out three candidate findings:
  - **Windows narrow-encoding paths:** `cmake/utf8.manifest` sets the active code page to UTF-8.
  - **Tile-answer warnings injecting terminal escapes:** the warnings echo only line numbers.
  - **The linear capture being clamped on the FSR path:** `fsr_easu.frag` carries `linearOutput` too.

## Critical (0)

## High (0)

## Medium (1)

- [dim 2] `main.cpp:271-273`: `renderer.pinLightSeconds(renderer.lightSeconds()); renderer.setLinearOutput(true); const auto redrew = renderer.draw(bundle, *shownView);`
  - **The problem:** the second capture image is meant to be "the same view without exposure or the tone map" (usage text, `Cli.cpp:45`). The code pins the light time so the two images agree, but it does not pin the render scale.
  - **Why it matters:** `ut-ants` always sets `dynamicResolution` (`main.cpp:209`, UTA-0051 § 4.5). So the redraw can run at whatever scale the controller chooses next, not the shown frame's. `details.txt` records only the shown frame's `stats.renderScale` (`main.cpp:295`). The two images can then differ in sharpness and in upscaling artefacts, for a reason that has nothing to do with what is being compared.
  - **Uncertainty:** I did not open the renderer to confirm that the scale is recomputed on every `draw`.
  - **Fix:** pin the scale for the redraw (a fixed scale equal to `stats.renderScale`), or record the redraw's own `renderScale` as well.

## Low / Info

- [dim 16] `main.cpp:357-361` with `main.cpp:349`: `const auto resized = renderer.resize(config.width, config.height);`
  - **The problem:** if a resize and an F12 press arrive in the same batch of events, `writeCaptureFolder` reads back targets that were rebuilt but never drawn.
  - **Why the check misses it:** they are the new size, so `Capture.cpp:86`'s size check passes.
  - **Effect:** `poseLine(*shownView, config.width, config.height)` also pairs the old vertical field of view with the new aspect ratio, so `camera.txt` is wrong.
  - **Fix:** reset `shownView` on a resize.
- [dim 9] `Capture.cpp:100-106`: `for (int n = 2; std::filesystem::exists(candidate, ec); ++n)`
  - **The problem:** this check-then-use can pick the same folder in two viewers in the same second, and the second viewer's files replace the first's. An `exists` error also ends the loop as though the folder were free.
  - **Fix:** claim the folder with `create_directory` and its return value, looping while it reports the folder already exists.
- [dim 13] `Capture.cpp:56`: `std::format("{}-{:%Y%m%d-%H%M%S}", map, seconds)`
  - **The problem:** a `system_clock` time point formats as UTC, and the folder name carries no zone marker, so a reader will take it for local time.
  - **Fix:** append `Z`, or format the local time.
- [dim 2] `MapList.cpp:157-169`: `appendUtf8(out, code);`
  - **The problem:** a `\uD8xx\uDCxx` surrogate pair is encoded as two 3-byte sequences (CESU-8), not as one 4-byte character.
  - **Effect:** if `ut-bake` escapes a character outside the Basic Multilingual Plane (an emoji, say) in `path`, the launcher records a bundle path that does not exist, and the viewer fails to read it.
  - **Uncertainty:** whether `ut-bake` escapes such characters is unverified.
  - **Fix:** combine surrogate pairs, and reject lone surrogates.
- [dim 9] `Launcher.cpp:273`: `if (auto disk = readNotes(paths_.notes, notesFor_); disk && *disk != notesOnDisk_)`
  - **The problem:** when the re-read fails, the code falls through and writes over a notes file it could not read. `MapList.h:146-147` exists to prevent exactly that.
  - **Fix:** on a read error, drop the keystroke and set the status line.
- [dim 4] `main.cpp:304`: `<< folder.error().message()`, and `main.cpp:340`: `<< added.error().message()`.
  - **The problem:** these are not passed through `uta::sanitised`, while the same kind of write error at `Launcher.cpp:291` is.
  - **Why it matters:** the capture path carries a map name taken from a third-party file name.
  - **Uncertainty:** I did not check whether `Error::message` already sanitises.
  - **Fix:** wrap both in `uta::sanitised`.
- [dim 16] `WriteBuildCommit.cmake:29`: `COMMAND git -C "${SOURCE_DIR}" diff --quiet HEAD --`
  - **The problem:** this runs on every build, and `git diff` may refresh the index, taking `index.lock` for a moment. A git command the user runs at the same time can then fail with "index.lock exists".
  - **Uncertainty:** this is git's documented behaviour; I have not reproduced it here.
  - **Fix:** use `git --no-optional-locks`.
- [dim 2] `Cli.cpp:140`: `if (!options.bakerVersion.empty())`
  - **The problem:** `--baker-version ""` is accepted, and after it a second `--baker-version` is not reported as given twice. The launcher-mode refusal at line 173 also misses it.
  - **Fix:** track the flag's presence separately from its value, or reject an empty value as `--notes` does.
- **INFO (my search):** I did not open the urender, ubundle or core implementations behind `readback`, `resize`, `resolveUnder` or `writeFileAtomically`. They are other lanes' code, and the findings above that depend on them say so.

## Covered by spec and looks correct

- **UTA-0051 § 4.5, opened and confirmed:**
  - `--tier` sets `Config::tier` (`Cli.cpp:78-92`, `main.cpp:208`).
  - `dynamicResolution` is always set (`main.cpp:209`).
  - The `--frames` report includes the tier and the render scale (`main.cpp:405-408`).
- **UTA-0051 § 6:** an unknown tier exits with code 2 and lists the four names (`Cli.cpp:90`, `main.cpp:421-423`).
- **Usage text against the handlers (dim 2b, no zombies found):** every control the usage text promises has a handler, in both the launcher and the viewer: brackets, P/BACK, F12/WEST, NORTH, START, the shoulders and triggers, the left-stick press, Ctrl ±, Tab, and gamepad picking.
- **`Child`'s locking and lifetime (dim 8):** checked; `child_` is declared last, so it is destroyed first, and nothing was found.
- **`FlyCamera`:** pitch folding, the field-of-view conversion and `poseLine`'s inverse are consistent with each other.
- **`readResult`:** older and shorter result files fall back field by field (dim 17).

## Open questions

- Does `Renderer::draw` recompute the render scale on every call? That decides whether the Medium finding is real.
- Does `ut-bake`'s JSON writer escape non-ASCII characters as `\u` sequences? That decides the surrogate-pair finding.
- Dimensions 10, 12 and 15 are N/A: my tail names no observability, accessibility or privacy commitment. Nothing in this lane sends data off the machine.

## 3 items to fix first

1. **The capture redraw's unpinned render scale (Medium).** Capture exists to compare two images of the same view, and this can make them differ for no real reason on every capture taken under load.
2. **A resize and F12 in the same batch.** This one writes a capture folder that looks valid but holds a wrong image and a wrong camera line, and nothing reports it.
3. **`editNotes` writing over notes it could not read.** It is the only path in the lane that can lose text the user wrote.
