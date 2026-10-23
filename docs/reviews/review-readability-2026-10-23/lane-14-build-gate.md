## Files read
- `.githooks/commit-msg`: read 161 of 161 lines
- `.githooks/pre-commit`: read 121 of 121
- `.githooks/pre-push`: read 34 of 34
- `scripts/ccache-windows.sh`: read 29 of 29
- `scripts/check-legs.sh`: read 54 of 54
- `scripts/ci-matrix.sh`: read 230 of 230
- `scripts/ci.sh`: read 391 of 391
- `scripts/docs-only.sh`: read 41 of 41
- `scripts/quarantine-guard.sh`: read 135 of 135
- `scripts/setup-hooks.sh`: read 21 of 21
- `scripts/std-includes.sh`: read 77 of 77
- `scripts/windows-gate-reap.sh`: read 45 of 45
- `CMakeLists.txt`: read 328 of 328
- `tests/CMakeLists.txt`: read 311 of 311
- `.github/workflows/ci.yml`: read 358 of 358

## What I started with
- The global `~/.claude/CLAUDE.md`.
- The lane's system prompt (`review-lane`).
- A SessionStart hook note that the Ants MCP is connected.
- The shared-context brief.
- No project `CLAUDE.md`, memory index or git snapshot was in my context when I arrived.

Disagreement settled: the review-lane file forbids writing replacement text, and the brief asks for a one-line fix. Both say the same thing, so I followed both.

## Summary
These files are the project's safety checks and build recipe.

- `CMakeLists.txt` tells the compiler how to build the engine, its tools and its tests on Linux and Windows.
- `scripts/ci.sh` is the single list of checks. GitHub runs it on every push, and so does the local push gate, once per compiler, through `ci-matrix.sh`.
- A cheaper "documentation-only" mode skips compiling when only prose changed.
- A "quarantine guard" stops files derived from the player's Unreal Tournament install from ever being committed.
- The git hooks check commit-message shape and markdown links, and hand the push check to a machine-wide hook.

## Findings

[Q4] HIGH `scripts/ci.sh:112` — `# ut-origin, which this mode does not build. No push can use the gap: scripts/docs-only.sh never classifies a push carrying a .utab as documentation-only` — The comment is false. `docs-only.sh:18` accepts `docs/*`, so `docs/x.utab` counts as documentation, and the bundle-origin check never runs on that push. Someone trusting this comment would add a bundle under `docs/` and believe it was checked. Lines 241–243 make the same claim. — Fix: add `*.utab` to `docs-only.sh`'s never-list, or narrow the comments to match.
Cost: one edit for the comment. Adding to the never-list touches `docs-only.sh` only, but the never-list mirrors `~/.claude/githooks/pre-push:301` `NEVER_DOCS`, so that copy would differ afterwards.
Searched: `Grep` of `~/.claude/githooks/pre-push` for the never-list.

[Q4] MEDIUM `scripts/ci.sh:38` — `sed -n '2,20p' "$0"` (and `scripts/ci-matrix.sh:39`, `sed -n '2,30p' "$0"`) — `--help` prints a fixed range of lines from the header comment. Nothing in either header says so.
- In both files the range has already drifted: `ci.sh` stops mid-sentence ("is a green that" / "lies." is on line 21), and `ci-matrix.sh` stops mid-sentence at line 30 ("which is what the" / "CI leg compiles against").
- Anyone editing either header shifts the help text without knowing.
- Fix: end the header at a marker line and print up to it, or say beside the header that `--help` prints these lines.
Cost: one edit (this touches only the two scripts; no spec names `--help` line ranges, and no search was needed for a comment-and-range change).

[Q1] MEDIUM `.github/workflows/ci.yml:235` — `key: ccache-${{ matrix.cxx || 'msvc' }}-${{ github.sha }}` — The per-job cache key is spelled out by hand in six places: lines 107, 235, 236, 279, 307 and 308.
- If the restore key (235) and the save key (279) ever differ, the cache silently never hits. The prune list (307) has to match both.
- The `deps_key` beside them is already computed once in the "Decide the mode" step. The ccache key is not.
- Fix: emit the ccache key from the "Decide the mode" step, as `deps_key` is, and refer to that output.
Cost: one edit — Searched: `workspace_search` for `matrix.cxx || 'msvc'` across the non-hidden tree, docs and specs included: 0 hits.

[Q1] MEDIUM `.github/workflows/ci.yml:168` — `echo "CCACHE_MAXSIZE=1G"` — The ccache environment (`NOHASHDIR`, `MAXSIZE`, `BASEDIR`, `DIR`) is set by hand in three places: here, again at ci.yml:208–213, and in the remote script at `scripts/ci-matrix.sh:161–162`.
- The comment at line 198 says "The settings mirror the Linux ones above", so the copies are meant to be kept in step.
- Someone changing the cache size or a setting would edit one block and miss the others.
- Fix: put the shared settings in one place, for example have `ccache-windows.sh` export them or document them in one place, and name the three sites beside each.
Cost: unsure — the three blocks run in different settings (a GitHub env file, Git Bash on Windows, a heredoc over SSH), so a single home may not reach all three.

[Q4] MEDIUM `.github/workflows/ci.yml:56` — `# hang. The Windows leg has no compiler cache and takes about 8 minutes;` — This is false now. Lines 196–213 install ccache on Windows (UTA-0235), and the restore and save steps run for the `msvc` key. Someone tuning the timeout or chasing a Windows cache miss would be misled. Line 215 ("Catch2 is most of the objects in both Linux trees … Kept per compiler") likewise mentions Linux only. — Fix: restate the timeout's reason without "has no compiler cache".
Cost: one edit (comment only).

[Q4] MEDIUM `CMakeLists.txt:203` — `# mold saves almost nothing TODAY -- this project links one static library and one test executable` — This is stale. The build now links many libraries (`uta_core`, `uta_upkg`, … `uta_urender`), several tools, the client and two test binaries. The comment's "once urender and the third-party dependencies arrive" has already happened: line 268 names `uta_urender`, and glm and SDL3 are fetched. Someone reading it would think mold is dead weight and might remove it, though ci.yml:145 installs mold deliberately. — Fix: replace the "TODAY" paragraph with the current reason, or drop it.
Cost: one edit (comment only).

[Q1] MEDIUM `tests/CMakeLists.txt:157` — `target_sources(uta_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools/ut-bench/Cli.cpp"` — ut-bench's source list is kept by hand in three places: `tools/ut-bench/CMakeLists.txt:9`, here (157–158), and again in `uta_device_tests` (237–238). ut-paths' `Walkable.cpp` and `Seeds.cpp` are likewise listed twice in this file (140–141 and 302–303). Someone splitting `Frame.cpp` or adding a file to the tool would update the tool and miss a test target, or the other way round. — Fix: build each tool's testable sources once as a library in its own CMakeLists and link that library from the tests.
Cost: pinned. `docs/specs/UTA-0129-benchmark-tool.md` lines 291 and 370 say `tools/ut-bench/Cli.cpp` "is compiled into the tool and into the unit tests" and also name `Frame.cpp`. `docs/specs/UTA-0121-bot-path-seeds.md` § 4.1 is cited for the ut-paths sources. A library may or may not satisfy the spec's wording.
Searched: `workspace_search` across docs for `ut-bench/(Cli|Frame)\.cpp|uta_device_tests|uta_unit_tests` (8 spec files).

[Q5] MEDIUM `scripts/ci-matrix.sh:187` — `run_leg "Linux (GCC 14)" CC=gcc-14 CXX=g++-14 UTA_CI_BUILD_DIR=build-ci-gcc14` — `scripts/check-legs.sh:34–38` parses this file's text, and nothing in `ci-matrix.sh` says so. The parse needs:
- each `run_leg` line to start in column 0, with `CC=` directly followed by `CXX=`;
- a line indented with spaces, starting `ssh` and containing `bash.exe`;
- a line that is exactly `./scripts/ci.sh` (line 163).

Someone reformatting a `run_leg` call, or adding an argument to the remote `./scripts/ci.sh`, gets "the local gate and GitHub test different compilers — GitHub only: msvc", which points away from the real cause. — Fix: add a comment at each parsed line naming `check-legs.sh`.
Cost: one edit (comment only).
Searched: `workspace_search` for `check-legs`; no hit in `ci-matrix.sh`.

[Q3] LOW `scripts/ci-matrix.sh:50` — `JOBS=${UTA_CI_JOBS:-4}` — `UTA_CI_JOBS` sets only build parallelism, through `CMAKE_BUILD_PARALLEL_LEVEL`. `ci.sh:69` has `UTA_CI_TEST_JOBS` for tests, defaulting to `nproc`. Someone wanting fewer parallel tests because of the "RAM is tight" note (line 29) would set `UTA_CI_JOBS` and see no change. — Fix: rename it to `UTA_CI_BUILD_JOBS`, or say "build jobs" in the header.
Cost: one edit — Searched: `workspace_search` for `UTA_CI_JOBS|UTA_CI_TEST_JOBS`; hits only in these two scripts and a ROADMAP entry, no spec.

[Q5] LOW `scripts/ci.sh:299` — `if [[ -n ${UTA_CI_DEPS_DIR:-} && ${#deps_args[@]} -eq 0 ]]; then` — An empty `deps_args` is used to mean "the kept sources were incomplete, so refill them", and nothing says so. Someone adding any other configure flag to `deps_args` would silently stop the refill. — Fix: use a named boolean, for example `deps_kept=false/true`, set at line 280.
Cost: one edit — the change stays inside `ci.sh` and renames no name a spec could cite.

[Q4] LOW `.github/workflows/ci.yml:161` — `# the same two CLAUDE.md sets locally.` — The project's `CLAUDE.md` does not set them. It only points to `docs/build-and-test.md` (CLAUDE.md:133–134), which holds `base_dir=/` and `hash_dir=false` (lines 77–78), as does `CMakeLists.txt:151–152`. The values also differ: CI sets `CCACHE_BASEDIR=$GITHUB_WORKSPACE`, not `/`. Someone looking in `CLAUDE.md` finds nothing. — Fix: point the comment at `docs/build-and-test.md`, and say why the base directory differs.
Cost: one edit (comment only).
Searched: `Grep` of `CLAUDE.md` for `ccache`, and `workspace_search` for `base_dir|hash_dir|CCACHE_BASEDIR`.

[Q4] LOW `CMakeLists.txt:116` — `# the UTA_SANITIZE and compiler-version checks below: explain why, then` — The compiler-version checks are above, at lines 27–36, not below. — Fix: say "above" for the version checks.
Cost: one edit (comment only).

[Q4] LOW `tests/CMakeLists.txt:288` — `# RealInstallTest.cpp was split by subject into the next four -- UTA-0103.` — 13 files follow, not four. Someone could not tell which files came from the split, and which later. — Fix: drop the count, or move the history to UTA-0103.
Cost: one edit (comment only).

[Q4] LOW `.githooks/commit-msg:137` — `# The cap guards against a typo, not a rule: enumerating` — This repeats lines 117–119 almost word for word. The hook's comments also bury the current rule under history ("which three independent reviews caught", lines 110–111 and 129–130). The cost is time spent reading, not a wrong edit. — Fix: keep one cap comment and move the review history to the commit body.
Cost: unsure — `ci.sh:194–196` says the hooks are maintained in `~/.claude/skeleton`, so an edit here would fork the hook from its skeleton copy.

[Q1] LOW `.githooks/pre-commit:60` — `[ -e "$p" ] || [ -e "$dir/$p" ] && continue` — This is a second markdown-link check. It has drifted from `ci.sh:133–137`:
- It accepts a file that merely exists on disk, where `ci.sh` requires a tracked one (UTA-0371).
- It treats a leading `/` as this machine's root, where `ci.sh` treats it as the repository's root.
- It skips `docs/` and links on dated lines.

Someone fixing link handling in one copy would not know the other exists, and the commit hook passes links the push gate then fails. — Fix: name the looser twin beside `ci.sh`'s link step.
Cost: unsure — the hook is the skeleton's (`ci.sh:194–196`), so reconciling it happens outside this repository.

[Q3] LOW `scripts/std-includes.sh:74` — `printf 'ci: %d standard function(s) used without their header` — Line 50 of this file uses the prefix `std-includes:`. This message says `ci:`, so someone searching `ci.sh` for the failing message would not find it. — Fix: use `std-includes:`.
Cost: one edit (string only).

## Candidates dismissed
- `.github/workflows/ci.yml:57` "30 leaves" — the number sits on the line directly above `timeout-minutes: 30`, so anyone changing one sees the other.
- `CMakeLists.txt:17` "20 modules" — this is "C++20 modules", a language version, not a count.
- `CMakeLists.txt:30` "18 reports" — this is "Clang 18 reports", a compiler version in a dated measurement.
- `scripts/ci.sh:64` "12 threads" — a dated measurement record ("Measured 2026-09-28"), not a claim about now.
- `scripts/ci.sh:169` "2 and reads" — grep's exit status 2. With `pipefail` the comment is true of the code below it.
- `scripts/ci.sh:345` "52 tests" — a historical figure tied to commit `f02396c`.
- `scripts/quarantine-guard.sh:88` "15 puts" — "Rule 15 puts", a design-rule number.
- `scripts/quarantine-guard.sh:98` "3 reads" — "Check 3 reads", a check's number, which matches the code.
- `tests/CMakeLists.txt:15` "2 keeps" — "design rule 2 keeps", a rule number.

## Correctness leads
- `scripts/docs-only.sh:18` — `docs/*` treats `docs/<anything>.utab` as documentation, so a non-authored bundle under `docs/` skips the bundle-origin check on both gates. — Searched: read `docs-only.sh` and `quarantine-guard.sh`; the never-list does not contain `*.utab`.
- `scripts/ci.sh:38`, `scripts/ci-matrix.sh:39` — `--help` already stops mid-sentence in both scripts. — Searched: none; read in full.
- `.githooks/pre-commit:60` — a link starting with `/` is tested against this machine's filesystem root, not the repository's. — Searched: compared with `ci.sh:131–133`.

## Not checked
- `~/.claude/githooks/pre-push` beyond its never-list. So I did not check the claim at `CMakeLists.txt:157–158` that "the pre-push hook's worktree is one depth every push" now that in-place runs (UTA-0238) exist. That depth may no longer be fixed.
- Whether `windows-gate-reap.sh`'s hand-kept list of build-tool names covers every program the Windows gate starts. I had no record of what the gate spawns.
- The Ants `workspace_search` skips hidden paths by default, so my key search did not cover `.github/`. I relied on reading `ci.yml` in full for that file.
