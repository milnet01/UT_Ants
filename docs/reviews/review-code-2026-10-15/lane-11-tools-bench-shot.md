Line counts as read: map-pic `Cli.cpp` 187, `Cli.h` 52, `main.cpp` 14. ut-bench `Cli.cpp` 419, `Cli.h` 85, `Frame.cpp` 345, `Frame.h` 40, `main.cpp` 22. ut-bound-scan `Cli.cpp` 205, `Cli.h` 38, `Scan.cpp` 123, `Scan.h` 68, `main.cpp` 14. `UTACam.uc` 162, `ut-compare.py` 330. ut-ref `Reference.cpp` 262, `Reference.h` 93, `main.cpp` 208. ut-shot `Cli.cpp` 238, `Cli.h` 85, `main.cpp` 221.

**What I already held when I arrived:** the user's global `~/.claude/CLAUDE.md`, which describes the review skills and this machine; the review-lane agent prompt; the environment block and the user's email context. No project `CLAUDE.md` and no git snapshot. Since then I have read the shared brief, the 21 files above, UTA-0129 §§ 4.3–6 and UTA-0292 §§ 4.3–6, and `cpp.md` in full.

**Outside the packet, to check claims:** the bodies of `ubake::detail::mapNameOf` and `urecipe::mapNameOf`, `ubundle::applyTileAnswersFile`, and `src/ubake/Bake.cpp` lines 1071–1128 (`findLevel`, `findModel`, `resolveTexture`). I ran one `workspace_search` limited to `src/*.{h,cpp}`. It passed no `exclude_glob`; it was confined to `src/` instead. I opened no test file and ran no test-tree count.

This is a depth pass: the packet carries no breadth marker.

## Critical (0)

## High (0)

## Medium (3)

- [dim 11] `tools/ut-ref/main.cpp:132` — `const unsigned workers = std::max(1u, std::thread::hardware_concurrency() - 2);`
  - The subtraction is unsigned. When `hardware_concurrency()` returns 0 ("cannot tell") or 1 (a single-core VM), it wraps to about 4.29 billion.
  - `max(1u, …)` keeps the wrapped value, so `Reference::trace` (`Reference.cpp:188`) tries to create about 4 billion `jthread`s. Thread creation then fails, the exception is not caught, and the program terminates. Every `trace` dies on such a machine.
  - fix: `const unsigned hc = std::thread::hardware_concurrency(); const unsigned workers = hc > 2 ? hc - 2 : 1;`

- [dim 2] `tools/ut-compare/ut-compare.py:25` — `NOTHING OUTSIDE --work AND --out IS WRITTEN.`
  - `bake()` (lines 244–246) runs ut-bake with `--out ~/.cache/ut-ants/content/bakes` and `--texture-cache ~/.cache/ut-ants/textures`. Both are outside `--work` and `--out`.
  - Xvfb also writes `/tmp/.X<n>-lock`.
  - I can't tell which side is wrong. The sentence reads as a promise about the live install, so the document may be overclaiming (see Open questions).
  - fix: either narrow the sentence to "nothing in the live install is written", or bake into `--work`.

- [dim 4] `tools/ut-ref/main.cpp:139-145` — `fields >> ... >> fov; if (!fields) {...} const auto out = reference.trace(camera, fov, ...)`
  - ut-shot rejects a field of view outside (0, 180) through `usableHorizontalFov` (`ut-shot/Cli.h:35`, UTA-0224). ut-bench's `readCameras` rejects it too (`Frame.cpp:136`).
  - ut-ref is a copy of ut-shot's camera reading (`Reference.cpp:162-167` says so) and accepts any value.
  - Results: fov 0 gives `f = 1/tan(0) = inf`; fov ≥ 180 gives a negative or infinite tangent. Either way a garbage `.f32` is written, and `score` then measures against it with no error.
  - fix: call `uta::shot::usableHorizontalFov`, or the same check, before `trace`.

## Low / Info

- [dim 11] `tools/ut-ref/main.cpp:85-86` and `Reference.cpp:173` — `std::vector<float> out(std::size_t{width} * height * CHANNELS, 0.0F);`
  - Width and height are only checked to be above 0. Near `UINT32_MAX` each, the 64-bit product overflows.
  - The vector is then undersized, and line 183's `std::copy` writes past its end. `score`'s `readFloats` multiplies the same way.
  - ut-shot caps both sides at `MAX_DIMENSION` 16384; ut-ref has no cap, so the copies have diverged.
  - fix: reject sizes above `uta::shot::MAX_DIMENSION` in both `trace` and `score`.

- [dim 3] `tools/ut-bound-scan/Scan.cpp:54` — `const std::int32_t vert = node.iVertPool + k;`
  - `iVertPool` comes from an untrusted community map. At or near `INT32_MAX`, adding `k` is signed overflow, which is undefined behaviour. The `inTable` check runs only after the addition.
  - [tool: UBSan / clang-tidy `bugprone-*`]
  - fix: do the arithmetic in `std::int64_t`.

- [dim 7] `tools/ut-bound-scan/Cli.cpp:155-163` — `for (stdfs::directory_iterator entry(folder, listing), end; !listing && entry != end; ...)`
  - A listing error, at the start or part-way through, ends the list without a word. No `refused` line is written and the exit code stays 0.
  - `Cli.h:33` promises "0 when every map was scanned".
  - fix: when `listing` is set, write a `refused` line for the folder and set `refused`.

- [dim 2] `tools/ut-bench/Cli.cpp:262,276` — `std::thread::hardware_concurrency()`
  - When it returns 0, `"logicalCores"` is written as `0`. UTA-0129 § 4.3 says "a value the tool cannot learn is the string `"unknown"`".
  - `conditions()` (line 267) also warns "busy" whenever the load is above 0.
  - fix: write `"unknown"` and skip the load comparison when the core count is 0.

- [dim 4] `tools/ut-bench/Cli.cpp:237-247` against `:309-323` — the phase path is built twice.
  - `summarise` keys each phase by its full path within each run. `writeTable` rebuilds paths from the depth order of `summary.phases`.
  - A child phase that first appears in a later run is appended at the end of `summary.phases`. The table then joins it to whichever phase came before it, so it is printed under the wrong parent's path. The JSON is unaffected.
  - fix: carry the path in `PhaseSummary`, or in a list beside it, and have the table print that.

- [dim 7] `tools/map-pic/Cli.cpp:147-148` — `if (!mapped.has_value() || mapped->bytes().empty()) { err << ... ": unreadable or empty\n";`
  - The cause in `mapped.error()` is thrown away; every other branch in this function prints its error.
  - fix: print `mapped.error().message()` when `mapped` failed.

- [dim 16] `tools/ut-compare/ut-compare.py:185-189,206-210` — `free_display()` then `Popen(["Xvfb", display, ...], stderr=DEVNULL)`
  - The display is checked, then Xvfb is started: another program can take it in between. Xvfb's exit is never checked.
  - A failed Xvfb then shows up only as "the original gave 0 frames for N cameras".
  - fix: after the sleep, check `xvfb.poll() is None` and fail naming Xvfb. Or use `-displayfd`.

- [dim 2] `tools/ut-compare/ut-compare.py:30` — "the run fails if anything there changed".
  - `changed_since` (lines 179–182) checks only `System` and `System64`, not the whole install. The read-only bind is what actually protects the rest.
  - The document side overclaims. fix: say "if a file in its System directories changed".

- [dim 2, document side] UTA-0292 § 4.4 and the code have drifted apart.
  - The synopsis lacks `trace`'s `--no-added-lamps` (`main.cpp:79`).
  - The channel table counts only `viaSky` hits as sky, but `Reference.cpp:145` also counts `paintedSky` (UTA-0350).
  - The code looks deliberate. Hand both to `review-contract`.

- [dim 11, conditional] `tools/map-pic/Cli.cpp:158` `mapPath.stem().string()` and `tools/ut-shot/Cli.cpp:220` `(folder / "camera.txt").string()`.
  - On Windows, `path::string()` throws, or loses characters, for a name the code page cannot hold. The rest of the tree uses `fs::utf8`.
  - The project notes call Windows first-class, but my tail names no portability commitment, so this is conditional.
  - fix: use `fs::utf8(...)`, and open the camera file from the `path`, not from a string.

- INFO — Known trap, ut-shot `--linear`: the usage text (`ut-shot/Cli.cpp:57-58`) and `Cli.h:10-11` both say the output is sRGB-encoded. ut-compare does not pass `--linear`. Nothing found.
- INFO — Dimensions 8, 9, 10, 12, 13, 15 and 17 — nothing found.
  - 10 and 12: N/A; no contract or standard named.
  - 13: no locale-dependent parsing. ut-shot uses `from_chars` deliberately.
  - 15: nothing leaves the machine. ut-compare blocks the Wayland socket and runs under `bwrap`.
  - 9: map-pic writes atomically. ut-shot and ut-ref check the stream after `close()`.
  - 17: no persisted state, apart from ut-compare's `--work` copy.
- INFO — Dimension 2b: no zombie found. Each documented entry point I traced (`runCli`, `runFrame`, `previewOf`, `scanModel`, `Reference::trace` and `scoreView`) is called from its `main`.

## Covered by spec and looks correct

- **ut-bench bake against UTA-0129 § 4.3, checked line by line:**
  - the JSON members and when each appears;
  - `error` only on `refused`;
  - `unattributed` counts depth-0 phases only;
  - `share` is a phase's minimum over the total's minimum;
  - the median of an even count;
  - `force` is set on every bake;
  - the bundle is hashed and then removed;
  - exit codes 0, 1 and 2;
  - the three warnings;
  - `load1` is read before the first run.
- **ut-bench frame against § 4.4:**
  - four untimed settling frames, then `--still`, then `--steps`;
  - nearest-rank p99 (100 down to 1 gives 1, 50.5, 99 and 100);
  - `shadowTiles` is a mean;
  - `passes` is `null` when no timestamps were kept;
  - render scale 1 and light time pinned at 0;
  - blank camera lines are skipped;
  - argument and bundle errors exit before a device is asked for.
- **ut-ref against UTA-0292 § 4.4:**
  - per-pixel seeding from the coordinates alone; each pixel writes only its own slots;
  - the 8×8 block rule, keeping a block with at least 48 pixels lit in both;
  - the indirect term is set against first bounce plus sky;
  - stops are taken only where both sides are above 0;
  - the PFM is read bottom row first.
- ut-shot refuses `--light-terms` together with `--emission`, per § 4.3.
- ut-bound-scan's output lines match `Cli.h`'s format. The post-order walk is iterative, counts a node reached twice once, and cuts a link back up the tree; I traced it by hand.

## Open questions

- ut-compare line 25: is "nothing outside `--work` and `--out` is written" meant to cover ut-bake's shared cache under `~/.cache/ut-ants`? This decides which side the Medium finding is filed against.
- map-pic passes `stem().string()` as `mapName`, which keeps the file's capitals. ut-bound-scan and the bake pass `mapNameOf`, which lowercases. `findLevel` uses the name only in error text. `resolveTexture` passes it on to `siteOf`, which I did not open. If `siteOf` matches the map's own package by name, case-sensitively, a mixed-case map file could fail to resolve its own MyLevel picture.
- `Reference::trace` runs `pixel` on many threads, each sharing `rays_` and `eyeRays_`. I did not check that `ubake::SurfaceRays`' queries are safe to run at the same time.
- ut-ref `score`'s mean row divides by every view, including views with 0 kept blocks, which add zeros. The spec says only "a mean row". Is that weighting intended?
- ut-ref `trace` does not apply the tile-answers file, though ut-shot and ut-bench do. From `applyTileAnswers` it changes only `tileKind`, so it probably does not affect light. I did not confirm that.
- `map-pic` treats a stored `Format` that is not a `std::uint8_t` value as "not palettised". I did not check what type a stored ByteProperty becomes in `upkg::Property::value`.

## 3 items to fix first

1. **ut-ref's worker count underflow (`main.cpp:132`).** It is a one-line fix, and today every `trace` on a 0- or 1-core report terminates.
2. **ut-ref's unchecked field of view and size (`main.cpp:139`, `:85`).** The reference is the yardstick other measurements rely on. A bad camera line or size gives a wrong reference or memory corruption, with no error. ut-shot already has both guards to copy.
3. **ut-compare's "nothing outside" promise (`ut-compare.py:25`).** It is a safety claim the user may rely on, and today it is untrue. Settle which side is wrong before anyone trusts it.
