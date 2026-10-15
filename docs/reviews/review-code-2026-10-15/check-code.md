# check-code report — UT_Ants, UTA-0329 tool half

**Scope: `--tree` (pinned by argument; `main` is the default branch, resolved from `origin/HEAD`). Not narrowed: no tool list, no `--quick`, no project-config preset or disablement. clang-tidy ran on the whole tree (src/ took ~6 min, the rest ~16 min at -j4), so the src/-only narrowing was not needed.** Run 2026-10-15, HEAD `77b239a`, working tree had `ROADMAP.md` modified (scanned as on disk).

- **Config:** `tools/audit/audit-config.json` → `suppressions_doc: tools/audit/suppressions.md`.
- **Gate-script flag probe:** `scripts/local-ci.sh`, `Makefile`, `.pre-commit-config.yaml` absent; `.github/workflows/ci.yml` names `shellcheck` and `yamllint` only as packages to install, with no flags. No flags adopted.
- **Route:** direct invocation (not `audit_run`): every tool under `nice -n 19 ionice -c3`, parallelism capped at 4, so the overnight bake was not starved. `audit_run`'s tool set also lacks glslang, typos, actionlint, zizmor, yamllint and vulture.
- **Exclusions:** the default set. Of its directories only `build/` exists, and it holds no tracked file. The binary-extension rule removed 1 PNG from the text scanners. `third_party/` was **not** excluded, because the rail forbids it (it holds tracked `.c`/`.h`); it is calibrated out by `suppressions.md` instead. Scope = `git ls-files`: 646 tracked files, 645 text.
- **Ledger:** `.audit_cache/learned-fp.jsonl` (32 records). `.audit_allowlist.json` and `.ants_review_falsepos.jsonl` are absent. The fingerprint is sha256(`file|rule|message`)[:16], recovered with an `audit_dismiss` dry run. It reproduced for clang-tidy (absolute path, rule `clang-tidy`, `<msg> [<check>]`), typos (`./<path>`, `typos`, `` `x` should be `y` ``) and ruff (relative path, code, message). **It did not reproduce for cppcheck**: none of the 11 cppcheck records matched any message form tried. So cppcheck's ledger count is 0 because the key could not be rebuilt, not because the records were tested and failed. The bandit, ruff-S602, semgrep `subprocess-shell-true` and zizmor `cache-poisoning` records matched no finding in this run.

## Tool lines

- cppcheck 2.22 — `--project=build/compile_commands.json`, file-filtered to src/ apps/ tools/ tests/ third_party/; `--enable=all --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=unknownMacro --std=c++23 --language=c++` (passed) `-j2`; no `--library=qt` (no `find_package(Qt…)`). 280 file-configurations checked. **First pass (-j4) hit a critical `syntaxError` in Catch2's `catch_lifetimebound.hpp` (`__has_cpp_attribute`), which silently ended analysis of every test TU.** It was re-run with `-D__has_cpp_attribute(x)=0`, and `--checkers-report` then showed "No critical errors"; the results below are from that run. Raw 752: 11 outside scope (build/_deps), 467 moved by suppressions.md, 0 by ledger (key not reproducible, see above), **273 open**, plus 1 `checkersReport` information line. Completion confirmed.
- clang-tidy (LLVM 23.1.3) — `run-clang-tidy -j 4 -p build -checks='-*,bugprone-*,clang-analyzer-*'`, header-filter `^<root>/(src|apps|tools|tests|third_party)/`, no `--quiet`. No `.clang-tidy` exists, so the check set was chosen here: `bugprone-*` matches the 2026-09-26 calibration's checks, and `clang-analyzer-*` was added. No PCH on project TUs. 259 TUs (97 src + 162 apps/tools/tests/third_party), 0 compile errors. Both relative and absolute paths were parsed, and duplicates across TUs were merged. Raw 2076 unique: 2 outside scope, 1887 by suppressions.md, 18 by ledger, **169 open**. Completion confirmed.
- clazy-standalone (LLVM 23.1.3) — `-p build`, default check set (`CLAZY_CHECKS` unset = level1). 258 TUs (bc7enc.c skipped as C), all rc 0. Raw 1150 unique, all `clazy-non-pod-global-static`, all moved by suppressions.md. **0 open.** Completion confirmed.
- glslangValidator 16.5 — `-S <stage from extension> --target-env vulkan1.3 -I<src/urender/shaders> -I<third_party/fsr1> -o /dev/null`. 20 stage files (.frag/.vert/.comp, incl. tests/device/shaders): 18 compiled clean. `fsr_easu.frag` and `fsr_rcas.frag` were tool-failed, not findings: they use `#include` without declaring `GL_GOOGLE_include_directive`. Both compile clean under `glslc --target-env=vulkan1.3` (the build's compiler, which enables it by default). The 12 `.glsl` include files were skipped because their stage cannot be derived (they are compiled only through their includers). A deliberate-error probe was caught. **0 findings.**
- ruff — `--select E,F,B,S --no-cache` (no ruff config), 9 .py files. Raw 203: 145 `E501` in scripts/** moved by suppressions.md, 1 by ledger (S603 `scripts/mutation-probe.py:127`), **57 open**. Completion confirmed (exit 1 = findings).
- bandit — `-f json -ll`. **bandit: MEDIUM and above (`-ll`); LOW not reported** (18 LOW dropped). 9 files, 1693 LOC, 0 errors. **2 open** (two hits on one line).
- vulture — `--min-confidence 80`, 9 files, no whitelist. **0 findings (clean)**. Completion not confirmed (the tool prints no file count).
- mypy / pyright — **row skipped**: no `mypy.ini`, `pyproject.toml`, `setup.cfg` or `pyrightconfig.json` exists, so the project has never type-checked (lang-python.md). Nearest closed-list reason: no config to run against.
- shellcheck — `-f json1` on 10 `*.sh` files plus the 3 bash hooks in `.githooks/` (shebang, no extension; included by choice, not by the signal). **0 findings (clean)**. Completion not confirmed (no file count reported); exit 0.
- shfmt — **not run: no config to run against** (`.editorconfig` has only `[*]`, no section selecting `*.sh`).
- actionlint — `.github/workflows/ci.yml`. **0 findings (clean)**. Completion not confirmed (no count reported).
- zizmor 1.29 — `.github/workflows/`, offline mode (some online audits unavailable, per its own warning). Log: "completed .github/workflows/ci.yml". **0 findings (clean).**
- yamllint — the project's `.yamllint` declares rules, so it runs with that config: `ci.yml` and `.yamllint`. **0 findings (clean)**. Completion not confirmed.
- semgrep — `--timeout 60 --metrics=off --jobs 2`. (a) `p/security-audit` (the C++ pack choice) over 645 text files: 645 scanned, **0 results**, 4 `PartialParsing` warnings in third_party headers (rgbcx.h, ffx_a.h, ffx_fsr1.h, stb_image_write.h), no timed-out rules. (b) `p/python` over 9 .py files: 9 scanned, 0 errors, **2 results** (no ledger match).
- gitleaks — `gitleaks dir` on a copy of the 645-file resolved list (copy-and-scan pattern), `--redact`, JSON report. ~8.49 MB scanned. **0 findings (clean).**
- typos — `--force-exclude --format json` over the 645-file list (`_typos.toml` honoured). Raw 42: 16 `mis-` hyphen-prefix (standing calibration, matched on the next source byte), 2 ledger, **24 open**.

**Language census (tracked, in scope):** cpp 271, h 115, glsl 12, frag 8, comp 7, vert 5, py 9, sh 10 (+3 extensionless bash hooks), c 1, yml 1. **Source-carrying extensions that match no step 2 row:** `.uc` (UnrealScript, `tools/ut-compare/UTACam/Classes/UTACam.uc`); CMake (`CMakeLists.txt` files and 2 `.cmake`); `.in` template (`apps/ut-ants/BuildCommit.h.in`). These are unanalysed. `.recipe` (18 files) is the project's own data format, and no analyser class exists for it.

## Findings not explained by a recorded calibration

### cppcheck (273)
- `src/upkg/Class.cpp:77` — returnDanglingLifetime — Returning pointer that will be invalid when returning.
- `src/ubundle/TileAnswers.cpp:83` — constParameterReference — Parameter 'bundle' can be declared as reference to const
- `src/umat/Library.h:44` — uninitMemberVarNoCtor — Member variable 'CuratedEntry::fingerprint' has no initializer.
- `src/umat/Library.h:47` — uninitMemberVarNoCtor — Member variable 'CuratedEntry::source' has no initializer.
- `src/umat/Flames.h:24` — uninitMemberVarNoCtor — Member variable 'FlameEntry::fingerprint' has no initializer.
- `src/ubake/Geometry.cpp:143` — uninitMemberVarNoCtor — Member variable 'Portal::normal' has no initializer.
- `src/ubake/LightModel.cpp:196` — identicalConditionAfterEarlyExit — Identical condition 'd>=radius', second condition is always false
- `src/ubake/Bake.cpp:201` — returnDanglingLifetime — Returning pointer that will be invalid when returning.
- `src/urender/Device.cpp:21` — uninitMemberVarNoCtor — Member variable 'Requirement::met' has no initializer.
- `src/urender/Materials.cpp:234` — uninitMemberVarNoCtor — Member variable 'Placement::type' has no initializer.
- `src/urender/Materials.cpp:192` — constVariableReference — Variable 'picture' can be declared as reference to const
- `src/urender/Pipelines.cpp:59` — uninitMemberVarNoCtor — Member variable 'SceneVariant::translucent' has no initializer.
- `src/urender/Pipelines.cpp:60` — uninitMemberVarNoCtor — Member variable 'SceneVariant::twoSided' has no initializer.
- `src/urender/Pipelines.cpp:69` — uninitMemberVarNoCtor — Member variable 'SceneConstants::parallax' has no initializer.
- `src/urender/Renderer.h:171` — returnByReference — Function 'lastFrameStats()' should return member 'stats' by const reference.
- `src/urender/Frame.cpp:587` — shadowMember — Local variable 'depth' shadows outer member
- `src/urender/Frame.cpp:737` — shadowMember — Local variable 'drawn' shadows outer member
- `src/urender/Frame.cpp:823` — shadowMember — Local variable 'sky' shadows outer member
- `tools/ut-dump/Cli.cpp:409` — returnDanglingLifetime — Returning pointer that will be invalid when returning.
- `tools/ut-paths/Seeds.cpp:192` — uninitMemberVarNoCtor — Member variable 'Hops::moverSpot' has no initializer.
- `tools/ut-bench/Cli.cpp:234` — uninitMemberVarNoCtor — Member variable 'Row::phase' has no initializer.
- `apps/ut-ants/Png.cpp:26` — constParameterCallback — Parameter 'data' can be declared as pointer to const. However it seems that 'collect' is a callback function, if 'data' is declared with const you might also need to cast function pointer(s).
- `tools/ut-bound-scan/Cli.cpp:193` — containerOutOfBounds — Either the condition 'args.empty()' is redundant or expression 'args[a]' causes access out of bounds.
- `apps/ut-ants/Launcher.cpp:690` — redundantInitialization — Redundant initialization for 'status'. The initialized value is overwritten before it is read.
- `tests/unit/BakeActorsTest.cpp:63` — returnDanglingLifetime — Returning pointer that will be invalid when returning.
- `tests/unit/BakeCollisionTest.cpp:201` — returnDanglingLifetime — Returning pointer that will be invalid when returning.
- `src/upkg/Package.h:80` — uninitMemberVarNoCtor — Member variable 'ImportEntry::classPackage' has no initializer.
- `tests/unit/BakeFixture.h:191` — uninitMemberVarNoCtor — Member variable 'Import::classPackage' has no initializer.
- `tests/support/UnrealPackageBuilder.h:57` — uninitMemberVarNoCtor — Member variable 'ImportEntry::classPackage' has no initializer.
- `tests/unit/BakeShadowMaskTest.cpp:62` — passedByValue — Function parameter 'material' should be passed by const reference.
- `tests/unit/BakeMoversTest.cpp:103` — returnDanglingLifetime — Returning pointer that will be invalid when returning.
- `tests/support/FCoordsPort.h:33` — passedByValue — Function parameter 'a' should be passed by const reference.
- `tests/support/FCoordsPort.h:33` — passedByValue — Function parameter 'b' should be passed by const reference.
- `tests/support/FCoordsPort.h:34` — passedByValue — Function parameter 'a' should be passed by const reference.
- `tests/support/FCoordsPort.h:34` — passedByValue — Function parameter 'b' should be passed by const reference.
- `tests/support/FCoordsPort.h:45` — passedByValue — Function parameter 'point' should be passed by const reference.
- `tests/support/FCoordsPort.h:51` — passedByValue — Function parameter 'vector' should be passed by const reference.
- `tests/support/FCoordsPort.h:64` — passedByValue — Function parameter 'point' should be passed by const reference.
- `tests/support/FCoordsPort.h:142` — passedByValue — Function parameter 'location' should be passed by const reference.
- `tests/support/FCoordsPort.h:142` — passedByValue — Function parameter 'prePivot' should be passed by const reference.
- `tests/unit/BakeMoversTest.cpp:160` — passedByValue — Function parameter 'location' should be passed by const reference.
- `tests/unit/BakeMoversTest.cpp:160` — passedByValue — Function parameter 'postScale' should be passed by const reference.
- `tests/unit/BundleMaterialTest.cpp:386` — uninitMemberVarNoCtor — Member variable 'Case::breakIt' has no initializer.
- `tests/unit/BundleMaterialTest.cpp:432` — uninitMemberVarNoCtor — Member variable 'Case::breakIt' has no initializer.
- `tests/unit/ClientCaptureTest.cpp:47` — useInitializationList — Variable 'path_' is assigned in constructor body. Consider performing initialization in initialization list.
- `tests/unit/ClientMapListTest.cpp:31` — useInitializationList — Variable 'path_' is assigned in constructor body. Consider performing initialization in initialization list.
- `tests/unit/NavReachTest.cpp:25` — knownConditionTrueFalse — The comparison 'R_WALK == 1' is always true.
- `tests/unit/NavReachTest.cpp:26` — knownConditionTrueFalse — The comparison 'R_FLY == 2' is always true.
- `tests/unit/NavReachTest.cpp:27` — knownConditionTrueFalse — The comparison 'R_SWIM == 4' is always true.
- `tests/unit/NavReachTest.cpp:28` — knownConditionTrueFalse — The comparison 'R_JUMP == 8' is always true.
- `tests/unit/NavReachTest.cpp:29` — knownConditionTrueFalse — The comparison 'R_DOOR == 16' is always true.
- `tests/unit/NavReachTest.cpp:30` — knownConditionTrueFalse — The comparison 'R_SPECIAL == 32' is always true.
- `tests/unit/NavReachTest.cpp:31` — knownConditionTrueFalse — The comparison 'R_PLAYERONLY == 64' is always true.
- `tests/unit/NumericContractTest.cpp:158` — duplicateExpression — Same expression on both sides of '/'.
- `tests/unit/NumericContractTest.cpp:159` — duplicateExpression — Same expression on both sides of '/'.
- `tests/unit/PackageContentTest.cpp:475` — duplicateExpression — Same expression on both sides of '/'.
- `tests/unit/RecipeFormatTest.cpp:160` — uninitMemberVarNoCtor — Member variable 'Refusal::line' has no initializer.
- `tests/unit/RecipeFormatTest.cpp:314` — uninitMemberVarNoCtor — Member variable 'Refusal::what' has no initializer.
- `tests/unit/RecipeFormatTest.cpp:316` — uninitMemberVarNoCtor — Member variable 'Refusal::line' has no initializer.
- `tests/unit/RecipeFormatTest.cpp:410` — uninitMemberVarNoCtor — Member variable 'Refusal::what' has no initializer.
- `tests/unit/RecipeFormatTest.cpp:412` — uninitMemberVarNoCtor — Member variable 'Refusal::line' has no initializer.
- `tests/unit/RecipeFormatTest.cpp:518` — uninitMemberVarNoCtor — Member variable 'Refusal::what' has no initializer.
- `tests/unit/RecipeFormatTest.cpp:520` — uninitMemberVarNoCtor — Member variable 'Refusal::line' has no initializer.
- `tests/unit/RecipeFormatTest.cpp:614` — uninitMemberVarNoCtor — Member variable 'Refusal::what' has no initializer.
- `tests/unit/RecipeFormatTest.cpp:616` — uninitMemberVarNoCtor — Member variable 'Refusal::line' has no initializer.
- `tests/device/RenderLiquidsTest.cpp:62` — passedByValue — Function parameter 'look' should be passed by const reference.
- `tests/device/RenderLightParityTest.cpp:292` — unusedStructMember — struct member 'SkyCase::horizonAndClouds' is never used.
- `tests/device/RenderLightParityTest.cpp:293` — unusedStructMember — struct member 'SkyCase::d' is never used.
- `tests/device/RenderOutputStageTest.cpp:43` — uninitMemberVarNoCtor — Member variable 'Case::y' has no initializer.
- `tests/device/RenderOutputStageTest.cpp:46` — uninitMemberVarNoCtor — Member variable 'Case::regime' has no initializer.
- `tests/device/RenderShadowTest.cpp:76` — uninitMemberVarNoCtor — Member variable 'Case::what' has no initializer.
- `tests/device/RenderShadowTest.cpp:78` — uninitMemberVarNoCtor — Member variable 'Case::occluderY' has no initializer.
- `tests/device/RenderShadowTest.cpp:78` — uninitMemberVarNoCtor — Member variable 'Case::occluderZ' has no initializer.
- `tests/device/RenderShadowTest.cpp:79` — uninitMemberVarNoCtor — Member variable 'Case::shadowed' has no initializer.
- `tests/device/RenderShadowTest.cpp:80` — uninitMemberVarNoCtor — Member variable 'Case::control' has no initializer.
- `tests/device/RenderShadowTest.cpp:353` — uninitMemberVarNoCtor — Member variable 'Triangle::normal' has no initializer.
- `tests/device/RenderSurfaceFlagsTest.cpp:84` — uninitMemberVarNoCtor — Member variable 'Square::what' has no initializer.
- `tests/device/RenderSurfaceFlagsTest.cpp:85` — uninitMemberVarNoCtor — Member variable 'Square::material' has no initializer.
- `tests/device/RenderSurfaceFlagsTest.cpp:86` — uninitMemberVarNoCtor — Member variable 'Square::flags' has no initializer.
- `tests/device/RenderSurfaceFlagsTest.cpp:87` — uninitMemberVarNoCtor — Member variable 'Square::facingAway' has no initializer.
- `tests/device/RenderUnderwaterTest.cpp:127` — uninitMemberVarNoCtor — Member variable 'Case::fog' has no initializer.
- `tests/device/BenchFrameTest.cpp:42` — useInitializationList — Variable 'path_' is assigned in constructor body. Consider performing initialization in initialization list.
- **Class `comparisonOfFuncReturningBoolError` (191 hits, step 6 trigger):** message on every hit: "Comparison of a function returning boolean value using relational (<, >, <= or >=) operator.". Sites: `tests/unit/BakeCliTest.cpp:154`, `tests/unit/BakeCliTest.cpp:155`, `tests/unit/BakeCliTest.cpp:156`, `tests/unit/BakeCliTest.cpp:157`, `tests/unit/BakeCliTest.cpp:158`, `tests/unit/BakeCliTest.cpp:166`, `tests/unit/BakeCliTest.cpp:167`, `tests/unit/BakeCliTest.cpp:168`, `tests/unit/BakeCliTest.cpp:169`, `tests/unit/BakeCliTest.cpp:177`, `tests/unit/BakeCliTest.cpp:178`, `tests/unit/BakeCliTest.cpp:182`, `tests/unit/BakeCliTest.cpp:184`, `tests/unit/BakeCliTest.cpp:189`, `tests/unit/BakeCliTest.cpp:190`, `tests/unit/BakeCliTest.cpp:191`, `tests/unit/BakeCliTest.cpp:192`, `tests/unit/BakeCliTest.cpp:195`, `tests/unit/BakeCliTest.cpp:200`, `tests/unit/BakeCliTest.cpp:208`, `tests/unit/BakeCliTest.cpp:209`, `tests/unit/BakeCliTest.cpp:210`, `tests/unit/BakeCliTest.cpp:213`, `tests/unit/BakeCliTest.cpp:222`, `tests/unit/BakeCliTest.cpp:223`, `tests/unit/BakeCliTest.cpp:226`, `tests/unit/BakeCliTest.cpp:228`, `tests/unit/BakeCliTest.cpp:229`, `tests/unit/BakeCliTest.cpp:236`, `tests/unit/BakeCliTest.cpp:248`, `tests/unit/BakeCliTest.cpp:249`, `tests/unit/BakeCliTest.cpp:250`, `tests/unit/BakeCliTest.cpp:256`, `tests/unit/BakeCliTest.cpp:259`, `tests/unit/BakeCliTest.cpp:281`, `tests/unit/BakeCliTest.cpp:282`, `tests/unit/BakeCliTest.cpp:283`, `tests/unit/BakeCliTest.cpp:286`, `tests/unit/BakeCliTest.cpp:287`, `tests/unit/BakeCliTest.cpp:289`, `tests/unit/BakeCliTest.cpp:296`, `tests/unit/BakeCliTest.cpp:297`, `tests/unit/BakeCliTest.cpp:318`, `tests/unit/BakeCliTest.cpp:324`, `tests/unit/BakeCliTest.cpp:338`, `tests/unit/BakeCliTest.cpp:339`, `tests/unit/BakeCliTest.cpp:348`, `tests/unit/BakeCliTest.cpp:349`, `tests/unit/BakeCliTest.cpp:353`, `tests/unit/BakeCliTest.cpp:354`, `tests/unit/BakeCliTest.cpp:358`, `tests/unit/BakeCliTest.cpp:359`, `tests/unit/BakeCliTest.cpp:374`, `tests/unit/BakeCliTest.cpp:400`, `tests/unit/BakeCliTest.cpp:401`, `tests/unit/BakeCliTest.cpp:402`, `tests/unit/BakeCliTest.cpp:403`, `tests/unit/BakeCliTest.cpp:409`, `tests/unit/BakeCliTest.cpp:410`, `tests/unit/BakeCliTest.cpp:411`, `tests/unit/BakeCliTest.cpp:417`, `tests/unit/BakeCliTest.cpp:418`, `tests/unit/BakeCliTest.cpp:419`, `tests/unit/BakeCliTest.cpp:486`, `tests/unit/BakeCliTest.cpp:490`, `tests/unit/BakeCliTest.cpp:498`, `tests/unit/BakeCliTest.cpp:510`, `tests/unit/BakeCliTest.cpp:516`, `tests/unit/BakeCliTest.cpp:517`, `tests/unit/BakeCliTest.cpp:518`, `tests/unit/BakeCliTest.cpp:519`, `tests/unit/BakeCliTest.cpp:520`, `tests/unit/BakeCliTest.cpp:526`, `tests/unit/BakeCliTest.cpp:527`, `tests/unit/BakeCliTest.cpp:541`, `tests/unit/BakeCollisionTest.cpp:363`, `tests/unit/BakeCollisionTest.cpp:408`, `tests/unit/BakeGeometryTest.cpp:148`, `tests/unit/BakeGeometryTest.cpp:170`, `tests/unit/BakeGeometryTest.cpp:171`, `tests/unit/BakeGeometryTest.cpp:172`, `tests/unit/BakeGeometryTest.cpp:173`, `tests/unit/BakeSunTest.cpp:173`, `tests/unit/BakeSunTest.cpp:174`, `tests/unit/BakeSunTest.cpp:176`, `tests/unit/BakeSunTest.cpp:178`, `tests/unit/BakeSunTest.cpp:180`, `tests/unit/BakeSunTest.cpp:183`, `tests/unit/BakeSunTest.cpp:191`, `tests/unit/BakeSunTest.cpp:192`, `tests/unit/BakeSkyTest.cpp:206`, `tests/unit/BakeSkyTest.cpp:207`, `tests/unit/BakeSkyTest.cpp:209`, `tests/unit/BakeShadowMaskTest.cpp:343`, `tests/unit/BakeTest.cpp:1086`, `tests/unit/BakeTest.cpp:1087`, `tests/unit/BakeFlamesTest.cpp:188`, `tests/unit/BakeFlamesTest.cpp:189`, `tests/unit/BakeFlamesTest.cpp:190`, `tests/unit/BakeFlamesTest.cpp:191`, `tests/unit/BakeFlamesTest.cpp:192`, `tests/unit/BakeFlamesTest.cpp:193`, `tests/unit/BakeFlamesTest.cpp:194`, `tests/unit/FlameLibraryTest.cpp:22`, `tests/unit/FlameLibraryTest.cpp:24`, `tests/unit/FlameLibraryTest.cpp:25`, `tests/unit/BundleActorsTest.cpp:307`, `tests/unit/BundleSunTest.cpp:276`, `tests/unit/ClientMapListTest.cpp:207`, `tests/unit/ClientMapListTest.cpp:208`, `tests/unit/ClientMapListTest.cpp:209`, `tests/unit/ClientMapListTest.cpp:210`, `tests/unit/ClientMapListTest.cpp:212`, `tests/unit/ClientMapListTest.cpp:219`, `tests/unit/ClientMapListTest.cpp:246`, `tests/unit/ClientMapListTest.cpp:272`, `tests/unit/CoreJobsTest.cpp:122`, `tests/unit/CoreJobsTest.cpp:123`, `tests/unit/CoreJobsTest.cpp:170`, `tests/unit/CoreJobsTest.cpp:176`, `tests/unit/CoreJobsTest.cpp:195`, `tests/unit/CoreJobsTest.cpp:196`, `tests/unit/CoreJobsTest.cpp:198`, `tests/unit/CoreJobsTest.cpp:199`, `tests/unit/CoreJobsTest.cpp:204`, `tests/unit/CoreJobsTest.cpp:205`, `tests/unit/DumpCliTest.cpp:426`, `tests/unit/MaterialLibraryTest.cpp:155`, `tests/unit/MaterialLibraryTest.cpp:161`, `tests/unit/MaterialLibraryTest.cpp:167`, `tests/unit/MaterialLibraryTest.cpp:173`, `tests/unit/MaterialLibraryTest.cpp:180`, `tests/unit/MaterialLibraryTest.cpp:188`, `tests/unit/MaterialLibraryTest.cpp:190`, `tests/unit/MaterialLightTest.cpp:47`, `tests/unit/MaterialLightTest.cpp:48`, `tests/unit/MaterialLightTest.cpp:49`, `tests/unit/MaterialLightTest.cpp:50`, `tests/unit/MaterialLightTest.cpp:51`, `tests/unit/MaterialLightTest.cpp:52`, `tests/unit/NavReachTest.cpp:38`, `tests/unit/NavReachTest.cpp:39`, `tests/unit/NavReachTest.cpp:40`, `tests/unit/NavReachTest.cpp:41`, `tests/unit/NavReachTest.cpp:44`, `tests/unit/NavReachTest.cpp:46`, `tests/unit/NavReachTest.cpp:52`, `tests/unit/NavReachTest.cpp:53`, `tests/unit/NavReachTest.cpp:56`, `tests/unit/NavReachTest.cpp:59`, `tests/unit/NavReachTest.cpp:64`, `tests/unit/NavReachTest.cpp:65`, `tests/unit/NavReachTest.cpp:66`, `tests/unit/NavReachTest.cpp:67`, `tests/unit/NumericContractTest.cpp:162`, `tests/unit/NumericContractTest.cpp:164`, `tests/unit/PathTraceTest.cpp:66`, `tests/unit/PathTraceTest.cpp:71`, `tests/unit/PathTraceTest.cpp:76`, `tests/unit/PathSeedsTest.cpp:90`, `tests/unit/PathSeedsTest.cpp:245`, `tests/unit/PathSeedsTest.cpp:269`, `tests/unit/PathSeedsTest.cpp:274`, `tests/unit/PathSeedsTest.cpp:285`, `tests/unit/PathSeedsTest.cpp:667`, `tests/unit/PathWalkableTest.cpp:83`, `tests/unit/PathWalkableTest.cpp:84`, `tests/unit/PathWalkableTest.cpp:97`, `tests/unit/PathWalkableTest.cpp:128`, `tests/unit/PathWalkableTest.cpp:129`, `tests/unit/PathWalkableTest.cpp:140`, `tests/unit/PathWalkableTest.cpp:152`, `tests/unit/PathWalkableTest.cpp:153`, `tests/unit/RenderTiersTest.cpp:91`, `tests/unit/RenderTiersTest.cpp:92`, `tests/unit/RenderTiersTest.cpp:112`, `tests/unit/RenderTiersTest.cpp:113`, `tests/unit/RenderTiersTest.cpp:119`, `tests/unit/RenderTiersTest.cpp:125`, `tests/unit/RenderTiersTest.cpp:126`, `tests/unit/RenderTiersTest.cpp:132`, `tests/unit/RenderTiersTest.cpp:133`, `tests/unit/RenderTiersTest.cpp:139`, `tests/unit/RenderTiersTest.cpp:140`, `tests/unit/RenderTiersTest.cpp:146`, `tests/unit/RenderTiersTest.cpp:147`, `tests/unit/RenderTiersTest.cpp:153`, `tests/unit/RenderTiersTest.cpp:154`, `tests/unit/RenderTiersTest.cpp:161`, `tests/unit/RenderTiersTest.cpp:162`, `tests/device/RenderOutputStageTest.cpp:97`
  - Spot-check (3 hits): `tests/unit/BakeCliTest.cpp:154` `CHECK(isOneObject(result.out));`, `tests/unit/BakeTest.cpp:1087` `CHECK(rays.blocked(...))`, `tests/device/RenderOutputStageTest.cpp:97` `CHECK(near(...))`. Each is a Catch2 `CHECK` on a bool-returning call. The class appeared only once the `-D__has_cpp_attribute(x)=0` workaround let test TUs be analysed. Candidate for one `suppressions_doc` line; not judged here.

### clang-tidy (169)
- `src/ufire/Fire.cpp:83` — bugprone-misplaced-widening-cast — either cast from 'int' to 'std::size_t' (aka 'unsigned long') is ineffective, or there is loss of precision before the conversion
- `src/ufire/Fire.cpp:178` — clang-analyzer-security.FloatLoopCounter — Variable 'i' with floating point type 'float' should not be used as a loop counter
- `src/ufire/Fire.cpp:178` — bugprone-float-loop-counter — loop induction expression should not have floating-point type
- `src/ufire/Fire.cpp:180` — bugprone-incorrect-roundings — casting (double + 0.5) to integer leads to incorrect rounding; consider using lround (#include <cmath>) instead
- `src/ubake/ShadowMask.cpp:196` — bugprone-unchecked-optional-access — unchecked access to optional value
- `src/ubundle/FlameSection.cpp:67` — bugprone-unchecked-optional-access — unchecked access to optional value
- `src/ubundle/LampSection.cpp:58` — bugprone-unchecked-optional-access — unchecked access to optional value
- `src/ubundle/MaterialSection.cpp:204` — bugprone-unchecked-optional-access — unchecked access to optional value
- `src/ubundle/MaterialSection.cpp:212` — bugprone-unchecked-optional-access — unchecked access to optional value
- `src/ubundle/MaterialSection.cpp:215` — bugprone-unchecked-optional-access — unchecked access to optional value
- `src/ubundle/MaterialSection.cpp:222` — bugprone-unchecked-optional-access — unchecked access to optional value
- `src/ubake/Flames.cpp:118` — clang-analyzer-core.CallAndMessage — Called C++ object pointer is null
- `src/urender/Tiers.h:68` — bugprone-branch-clone — switch has 5 consecutive identical branches
- `src/urender/Lights.cpp:132` — bugprone-unchecked-optional-access — unchecked access to optional value
- `src/urender/Frame.cpp:381` — clang-analyzer-optin.performance.Padding — Excessive padding in 'struct uta::urender::Renderer::Impl' (32 padding bytes, where 0 is optimal). Optimal fields order: gpu, pipelines, swapchain, materialSampler, nearestSampler, shadowSampler, pool, sceneSet, postSet, easuSet, rcasSet, emissionSet, bloomSampler, fogFrames, fogSet, frameIndex, start, lastLightSeconds, renderScale, drawnScale, averagedMilliseconds, passTimes, timestampNanoseconds, timestampMask, pinnedLightSeconds, lightZones, moverBounds, previousModels, bloomSets, frameData, objects, lights, clusterCounts, clusterIndices, clusterBounds, probeCells, probes, shadowFaces, zones, flames, volumeLightIndices, maskCharts, maskPairs, mirrored, hdr, velocity, depth, output, upscaleInput, upscaled, shadowAtlas, emission, fogScattering, fogIntegrated, hazeShadow, fogHistory, sky, occlusion, shadowMask, shadowPlanner, config, stats, shape, geometry, bloom, materials, probeSpacing, probeCount, probeTableMask, probeLongestRun, flameCount, cameraPanSpeed, skyView, viewZone, previousViewProj, skyPending, skyReady, jitter, resized, drawn, lastDrawSkipped, swapchainRetired, tier, consider reordering the fields or adding explicit padding members
- `tests/unit/BakeOcclusionTest.cpp:200` — clang-analyzer-security.FloatLoopCounter — Variable 'x' with floating point type 'double' should not be used as a loop counter
- `tests/unit/BakeOcclusionTest.cpp:200` — bugprone-float-loop-counter — loop induction expression should not have floating-point type
- `tests/unit/BakeOcclusionTest.cpp:201` — clang-analyzer-security.FloatLoopCounter — Variable 'y' with floating point type 'double' should not be used as a loop counter
- `tests/unit/BakeOcclusionTest.cpp:201` — bugprone-float-loop-counter — loop induction expression should not have floating-point type
- `tests/unit/BakeOcclusionTest.cpp:202` — clang-analyzer-security.FloatLoopCounter — Variable 'y' with floating point type 'double' should not be used as a loop counter
- `tests/unit/BakeOcclusionTest.cpp:202` — bugprone-float-loop-counter — loop induction expression should not have floating-point type
- `tests/unit/BakeOcclusionTest.cpp:203` — clang-analyzer-security.FloatLoopCounter — Variable 'z' with floating point type 'double' should not be used as a loop counter
- `tests/unit/BakeOcclusionTest.cpp:203` — bugprone-float-loop-counter — loop induction expression should not have floating-point type
- `tools/ut-paths/Seeds.cpp:244` — clang-analyzer-security.FloatLoopCounter — Variable 'x' with floating point type 'double' should not be used as a loop counter
- `tools/ut-paths/Seeds.cpp:245` — clang-analyzer-security.FloatLoopCounter — Variable 'y' with floating point type 'double' should not be used as a loop counter
- `tools/ut-bench/Cli.cpp:101` — bugprone-assignment-in-if-condition — an assignment within an 'if' condition is bug-prone
- `tools/ut-bench/Cli.cpp:103` — bugprone-assignment-in-if-condition — an assignment within an 'if' condition is bug-prone
- `tools/ut-bench/Cli.cpp:105` — bugprone-assignment-in-if-condition — an assignment within an 'if' condition is bug-prone
- `tools/ut-bench/Cli.cpp:186` — bugprone-unchecked-optional-access — unchecked access to optional value
- `tools/ut-bench/Cli.cpp:188` — bugprone-unchecked-optional-access — unchecked access to optional value
- `tests/unit/BakeLightModelTest.cpp:273` — clang-analyzer-security.FloatLoopCounter — Variable 'light' with floating point type 'double' should not be used as a loop counter
- `tests/unit/BakeLightModelTest.cpp:273` — bugprone-float-loop-counter — loop induction expression should not have floating-point type
- `tools/ut-bench/Frame.cpp:96` — bugprone-assignment-in-if-condition — an assignment within an 'if' condition is bug-prone
- `tools/ut-bench/Frame.cpp:235` — bugprone-unchecked-optional-access — unchecked access to optional value
- `tools/ut-bench/Frame.cpp:242` — bugprone-unchecked-optional-access — unchecked access to optional value
- `tools/ut-bench/Frame.cpp:251` — bugprone-unchecked-optional-access — unchecked access to optional value
- `tools/ut-bench/Frame.cpp:311` — bugprone-unchecked-optional-access — unchecked access to optional value
- `tools/ut-bench/Frame.cpp:334` — bugprone-unchecked-optional-access — unchecked access to optional value
- `tests/unit/BakeFireStillTest.cpp:110` — bugprone-too-small-loop-variable — loop variable has narrower type 'std::uint8_t' than iteration's upper bound 'size_type'
- `tests/unit/BundleFormatTest.cpp:926` — clang-analyzer-optin.core.EnumCastOutOfRange — The value '2' provided to the cast expression is not in the valid range of values for 'Origin'
- `tests/unit/BundleFormatTest.cpp:932` — clang-analyzer-optin.core.EnumCastOutOfRange — The value '2' provided to the cast expression is not in the valid range of values for 'BundleKind'
- **Class `bugprone-easily-swappable-parameters` (128 hits, step 6 trigger):** message on every hit: "2 adjacent parameters of 'meetAt' of similar type ('const Vec3 &') are easily swapped by mistake". Sites: `src/ubake/SurfaceRays.cpp:41`, `src/ubake/Strips.cpp:57`, `src/ubundle/Codec.h:252`, `src/upkg/Package.cpp:35`, `src/upkg/Package.cpp:67`, `src/ufire/Fire.cpp:55`, `src/urender/Shadows.cpp:62`, `src/ubake/Lamps.cpp:49`, `src/ubake/ShadowMask.cpp:159`, `src/ubake/Charts.cpp:151`, `src/urender/Sky.cpp:20`, `src/urender/Resources.cpp:92`, `src/urender/Resources.cpp:264`, `src/urender/Clusters.cpp:55`, `src/urender/Clusters.cpp:56`, `src/ubake/LightModel.cpp:111`, `src/ubake/LightModel.cpp:166`, `src/core/FileSystem.cpp:220`, `src/umat/Resolve.cpp:41`, `src/umat/Material.cpp:133`, `src/umat/Material.cpp:151`, `src/umat/Derive.cpp:51`, `src/umat/Derive.cpp:104`, `src/urender/Tiers.cpp:49`, `src/umat/Generate.cpp:42`, `src/urender/Placement.cpp:95`, `src/upkg/Texture.cpp:34`, `src/upkg/Class.cpp:100`, `src/urender/Lights.cpp:38`, `src/urender/Frame.cpp:364`, `tests/unit/RenderLightsTest.cpp:20`, `tests/device/ComputeFixture.cpp:25`, `tests/unit/PathWalkableTest.cpp:37`, `tests/support/UnrealPackageBuilder.cpp:156`, `tests/support/UnrealPackageBuilder.cpp:163`, `tests/support/UnrealPackageBuilder.cpp:171`, `tests/support/UnrealPackageBuilder.cpp:177`, `tests/support/UnrealPackageBuilder.cpp:183`, `tests/support/UnrealPackageBuilder.cpp:189`, `tests/support/UnrealPackageBuilder.cpp:211`, `tests/support/UnrealPackageBuilder.cpp:220`, `tests/support/UnrealPackageBuilder.cpp:246`, `tests/support/UnrealPackageBuilder.cpp:267`, `tests/support/UnrealPackageBuilder.cpp:289`, `tests/support/UnrealPackageBuilder.cpp:329`, `tests/support/UnrealPackageBuilder.cpp:614`, `tests/support/UnrealPackageBuilder.cpp:692`, `tests/support/UnrealPackageBuilder.cpp:695`, `tests/support/UnrealPackageBuilder.cpp:812`, `tests/unit/WiringGraphTest.cpp:67`, `tests/unit/WiringGraphTest.cpp:101`, `tests/unit/Bytes.h:55`, `tools/common/Json.h:27`, `tests/unit/ZonesSeeingSkyTest.cpp:29`, `apps/ut-ants/MapList.cpp:400`, `apps/ut-ants/MapList.cpp:434`, `tests/unit/NavGraphTest.cpp:93`, `tests/unit/NavGraphTest.cpp:105`, `tests/unit/MaterialCompressTest.cpp:58`, `tests/unit/MaterialCompressTest.cpp:72`, `tests/unit/MaterialCompressTest.cpp:89`, `tests/unit/MaterialCompressTest.cpp:436`, `tests/unit/BakeOcclusionTest.cpp:52`, `tests/device/RenderParallaxTest.cpp:74`, `apps/ut-ants/Launcher.cpp:160`, `apps/ut-ants/Launcher.cpp:572`, `tests/unit/BakeStripsTest.cpp:35`, `tests/unit/BundleGeometryTest.cpp:102`, `tools/ut-bound-scan/Cli.cpp:63`, `tools/ut-bound-scan/Cli.cpp:168`, `tools/ut-paths/Seeds.cpp:262`, `tools/ut-paths/Seeds.cpp:634`, `tools/ut-paths/Seeds.cpp:688`, `tests/unit/PathSeedsTest.cpp:83`, `tests/unit/BakeGeometryTest.cpp:67`, `tests/device/RenderUnderwaterTest.cpp:79`, `tests/unit/PackageContentTest.cpp:153`, `tests/unit/PackageContentTest.cpp:385`, `tests/unit/PackageContentTest.cpp:427`, `tests/unit/BakeCollisionTest.cpp:80`, `tests/unit/BakeCollisionTest.cpp:217`, `tests/unit/BakeFixture.cpp:101`, `tests/unit/BakeFixture.cpp:107`, `tests/unit/BakeFixture.cpp:126`, `tests/unit/BakeFixture.cpp:352`, `tests/unit/BakeFixture.cpp:371`, `tests/unit/BakeFixture.cpp:471`, `tools/ut-paths/Cli.cpp:319`, `tests/device/RenderTileShuffleTest.cpp:120`, `tests/unit/PathTraceTest.cpp:29`, `tests/unit/RoomBuildTest.cpp:43`, `tests/unit/BakeTest.cpp:102`, `tests/unit/PackageClassTest.cpp:54`, `tools/ut-paths/Walkable.cpp:50`, `tools/ut-paths/Walkable.cpp:50`, `tests/unit/BundleShadowMaskTest.cpp:146`, `tests/unit/LightFixture.cpp:22`, `apps/ut-ants/FlyCamera.cpp:83`, `apps/ut-ants/FlyCamera.cpp:105`, `tests/unit/RenderVolumeLightsTest.cpp:21`, `tests/device/RenderWaterTest.cpp:192`, `tests/device/RenderWaterTest.cpp:193`, `tests/unit/MaterialLibraryTest.cpp:52`, `apps/ut-ants/Png.cpp:26`, `tools/ut-ref/Reference.cpp:116`, `tools/ut-ref/Reference.cpp:160`, `tools/ut-ref/Reference.cpp:161`, `tools/ut-ref/Reference.cpp:161`, `tools/ut-ref/Reference.cpp:193`, `tools/ut-ref/Reference.cpp:193`, `tests/device/RenderFogTest.cpp:126`, `tests/unit/BundleCollisionTest.cpp:58`, `tests/unit/BakeSkyTest.cpp:121`, `tests/unit/BakeZonesTest.cpp:80`, `tests/device/RenderProbeTest.cpp:80`, `tests/unit/BakeShadowMaskTest.cpp:62`, `tests/unit/BakeShadowMaskTest.cpp:92`, `src/ubundle/Codec.h:252`, `tests/device/DeviceFixture.cpp:129`, `tests/device/DeviceFixture.cpp:151`, `tests/device/DeviceFixture.cpp:163`, `tests/device/DeviceFixture.cpp:173`, `tests/device/DeviceFixture.cpp:214`, `tests/unit/MaterialGenerateTest.cpp:84`, `tests/unit/NavReachTest.cpp:14`, `tests/unit/CoreFileSystemTest.cpp:343`, `tests/unit/PackageAncestryTest.cpp:45`, `tests/unit/PackageAncestryTest.cpp:220`
  - Spot-check (3 hits): `src/ubake/SurfaceRays.cpp:41` `meetAt(const Vec3& o, const Vec3& d, const Vec3& a, ...)`, `apps/ut-ants/Launcher.cpp:160` `offeredMaps(const path& install, std::string& note, ...)`, `tests/unit/PackageAncestryTest.cpp:220` `threePackages(const Package*, const Package*, ...)`. Adjacent same-typed parameters, as the check describes. Candidate for one `suppressions_doc` line; not judged here.

### ruff (57)
- `scripts/map-analyser.py:104` — E731 — Do not assign a `lambda` expression, use a `def`
- `scripts/map-analyser.py:104` — B905 — `zip()` without an explicit `strict=` parameter
- `scripts/map-analyser.py:163` — B905 — `zip()` without an explicit `strict=` parameter
- `scripts/map-analyser.py:174` — E731 — Do not assign a `lambda` expression, use a `def`
- `scripts/map-analyser.py:248` — E741 — Ambiguous variable name: `l`
- `scripts/map-analyser.py:249` — E741 — Ambiguous variable name: `l`
- `scripts/map-analyser.py:264` — S603 — `subprocess` call: check for execution of untrusted input
- `scripts/map-analyser.py:264` — S607 — Starting a process with a partial executable path
- `scripts/map-analyser.py:270` — S603 — `subprocess` call: check for execution of untrusted input
- `scripts/map-analyser.py:270` — S607 — Starting a process with a partial executable path
- `scripts/tile-questions.py:99` — B905 — `zip()` without an explicit `strict=` parameter
- `scripts/tile-questions.py:121` — S603 — `subprocess` call: check for execution of untrusted input
- `tools/ut-compare/ut-compare.py:122` — E741 — Ambiguous variable name: `l`
- `tools/ut-compare/ut-compare.py:169` — E741 — Ambiguous variable name: `l`
- `tools/ut-compare/ut-compare.py:170` — E741 — Ambiguous variable name: `l`
- `tools/ut-compare/ut-compare.py:173` — S603 — `subprocess` call: check for execution of untrusted input
- `tools/ut-compare/ut-compare.py:187` — S108 — Probable insecure usage of temporary file or directory: "/tmp/.X11-unix/X"
- `tools/ut-compare/ut-compare.py:187` — S108 — Probable insecure usage of temporary file or directory: "/tmp/.X"
- `tools/ut-compare/ut-compare.py:206` — S603 — `subprocess` call: check for execution of untrusted input
- `tools/ut-compare/ut-compare.py:206` — S607 — Starting a process with a partial executable path
- `tools/ut-compare/ut-compare.py:212` — S603 — `subprocess` call: check for execution of untrusted input
- `tools/ut-compare/ut-compare.py:233` — E741 — Ambiguous variable name: `l`
- `tools/ut-compare/ut-compare.py:244` — S603 — `subprocess` call: check for execution of untrusted input
- `tools/ut-compare/ut-compare.py:281` — E741 — Ambiguous variable name: `l`
- `tools/ut-compare/ut-compare.py:313` — S603 — `subprocess` call: check for execution of untrusted input
- `tools/ut-compare/ut-compare.py:322` — S603 — `subprocess` call: check for execution of untrusted input
- `tools/ut-compare/ut-compare.py:322` — S607 — Starting a process with a partial executable path
- **Class `E501` (30 hits, step 6 trigger):** message on every hit: "Line too long (89 > 88)". Sites: `tools/ut-compare/ut-compare.py:21`, `tools/ut-compare/ut-compare.py:22`, `tools/ut-compare/ut-compare.py:55`, `tools/ut-compare/ut-compare.py:69`, `tools/ut-compare/ut-compare.py:97`, `tools/ut-compare/ut-compare.py:151`, `tools/ut-compare/ut-compare.py:173`, `tools/ut-compare/ut-compare.py:176`, `tools/ut-compare/ut-compare.py:182`, `tools/ut-compare/ut-compare.py:187`, `tools/ut-compare/ut-compare.py:192`, `tools/ut-compare/ut-compare.py:193`, `tools/ut-compare/ut-compare.py:195`, `tools/ut-compare/ut-compare.py:200`, `tools/ut-compare/ut-compare.py:206`, `tools/ut-compare/ut-compare.py:211`, `tools/ut-compare/ut-compare.py:213`, `tools/ut-compare/ut-compare.py:237`, `tools/ut-compare/ut-compare.py:244`, `tools/ut-compare/ut-compare.py:245`, `tools/ut-compare/ut-compare.py:246`, `tools/ut-compare/ut-compare.py:265`, `tools/ut-compare/ut-compare.py:298`, `tools/ut-compare/ut-compare.py:305`, `tools/ut-compare/ut-compare.py:310`, `tools/ut-compare/ut-compare.py:313`, `tools/ut-compare/ut-compare.py:314`, `tools/ut-compare/ut-compare.py:322`, `tools/ut-compare/ut-compare.py:323`, `tools/ut-compare/ut-compare.py:324`
  - `E501` outside scripts/** is not covered by suppressions.md's `scripts/**` glob.

### bandit (2)
- `tools/ut-compare/ut-compare.py:187` — B108 (MEDIUM/MEDIUM) — Probable insecure usage of temp file/directory.
- `tools/ut-compare/ut-compare.py:187` — B108 (MEDIUM/MEDIUM) — Probable insecure usage of temp file/directory.

### semgrep (2)
- `scripts/tile-questions.py:121-122` — python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args — Detected subprocess function 'run' with user controlled data. A malicious actor could leverage this to perform command injection. You may consider using 'shlex.quote()'.
- `tools/ut-compare/ut-compare.py:313-314` — python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args — Detected subprocess function 'run' with user controlled data. A malicious actor could leverage this to perform command injection. You may consider using 'shlex.quote()'.

### typos (24)
- `ROADMAP.md:14942` — typos — `offred` should be `offered`
- `ROADMAP.md:14954` — typos — `Anc` should be `And`
- `ROADMAP.md:14957` — typos — `offred` should be `offered`
- `ROADMAP.md:14959` — typos — `offred` should be `offered`
- `ROADMAP.md:14962` — typos — `offred` should be `offered`
- `ROADMAP.md:14969` — typos — `offred` should be `offered`
- `ROADMAP.md:17333` — typos — `vegitation` should be `vegetation`
- `ROADMAP.md:18150` — typos — `metalness` should be `metallicity`
- `ROADMAP.md:18154` — typos — `metalness` should be `metallicity`
- `ROADMAP.md:19644` — typos — `hom` should be `home`
- `docs/specs/UTA-0286-replayed-fire-textures.md:26` — typos — `offred` should be `offered`
- `docs/specs/UTA-0286-replayed-fire-textures.md:34` — typos — `offred` should be `offered`
- `scripts/docs-only.sh:20` — typos — `nevers` should be `never`
- `scripts/docs-only.sh:27` — typos — `nevers` should be `never`
- `tests/real/flame-labels.txt:11` — typos — `offred` should be `offered`
- `tests/real/flame-labels.txt:13` — typos — `offred` should be `offered`
- `tests/real/flame-labels.txt:38` — typos — `offred` should be `offered`
- `tests/real/flame-labels.txt:95` — typos — `lightnin` should be `lightning`
- `tests/unit/BakeOcclusionTest.cpp:199` — typos — `froms` should be `forms`
- `tests/unit/BakeOcclusionTest.cpp:201` — typos — `froms` should be `forms`
- `tests/unit/BakeOcclusionTest.cpp:203` — typos — `froms` should be `forms`
- `tests/unit/BakeOcclusionTest.cpp:207` — typos — `froms` should be `forms`
- `tests/unit/BakeOcclusionTest.cpp:233` — typos — `froms` should be `forms`
- `tests/unit/BakeOcclusionTest.cpp:234` — typos — `froms` should be `forms`

## Calibration applied

- suppressions.md: cppcheck 467, clang-tidy 1887, clazy 1150, ruff 145.
  - cppcheck: 140 × suppressions.md: cppcheck useStlAlgorithm **
  - cppcheck: 128 × suppressions.md: any any third_party/**
  - cppcheck: 109 × suppressions.md: cppcheck normalCheckLevelMaxBranches **
  - cppcheck: 64 × suppressions.md: cppcheck uninitMemberVarNoCtor src/urender/ShaderTypes.h
  - cppcheck: 26 × suppressions.md: cppcheck shadowFunction **
  - clang-tidy: 994 × suppressions.md: any any third_party/**
  - clang-tidy: 527 × suppressions.md: clang-tidy bugprone-unchecked-optional-access tests/**
  - clang-tidy: 220 × suppressions.md: clang-tidy bugprone-signed-bitwise **
  - clang-tidy: 53 × suppressions.md: clang-tidy bugprone-narrowing-conversions **
  - clang-tidy: 38 × suppressions.md: clang-tidy bugprone-unchecked-optional-access src/urender/Frame.cpp
  - clang-tidy: 22 × suppressions.md: clang-tidy bugprone-implicit-widening-of-multiplication-result **
  - clang-tidy: 22 × suppressions.md: clang-tidy bugprone-throwing-static-initialization **
  - clang-tidy: 11 × suppressions.md: clang-tidy bugprone-invalid-enum-default-initialization **
- learned-fp ledger: clang-tidy 18, ruff 1, typos 2; cppcheck 0 (key not reproducible).
  - `src/core/Error.h:126` bugprone-macro-parentheses
  - `src/core/Error.h:127` bugprone-macro-parentheses
  - `src/core/Error.h:128` bugprone-macro-parentheses
  - `src/core/Log.cpp:64` bugprone-unhandled-exception-at-new
  - `src/core/Log.cpp:113` bugprone-empty-catch
  - `tests/unit/MaterialCompressTest.cpp:342` clang-analyzer-optin.core.EnumCastOutOfRange
  - `apps/ut-ants/Launcher.cpp:206` bugprone-incorrect-roundings
  - `apps/ut-ants/Launcher.cpp:206` bugprone-incorrect-roundings
  - `tests/unit/RenderSkyTest.cpp:100` bugprone-integer-division
  - `tools/ut-paths/Seeds.cpp:228` clang-analyzer-security.FloatLoopCounter
  - `tools/ut-paths/Seeds.cpp:228` bugprone-float-loop-counter
  - `tools/ut-paths/Seeds.cpp:244` bugprone-float-loop-counter
  - `tools/ut-paths/Seeds.cpp:245` bugprone-float-loop-counter
  - `apps/ut-ants/main.cpp:315` bugprone-branch-clone
  - `apps/ut-ants/main.cpp:320` bugprone-branch-clone
  - `tests/unit/BundleActorsTest.cpp:480` clang-analyzer-optin.core.EnumCastOutOfRange
  - `tests/unit/BundleActorsTest.cpp:504` clang-analyzer-optin.core.EnumCastOutOfRange
  - `tests/unit/CoreJobsTest.cpp:111` bugprone-std-exception-baseclass
  - `ROADMAP.md:10723` typos `perpose`
  - `tests/unit/PackageContentTest.cpp:1194` typos `seeked`
  - `scripts/mutation-probe.py:127` ruff S603
- typos `mis-` prefix standing calibration: 16 (`ROADMAP.md:2121`, `ROADMAP.md:4414`, `ROADMAP.md:5882`, `ROADMAP.md:6891`, `ROADMAP.md:11624`, `ROADMAP.md:11624`, `ROADMAP.md:11655`, `docs/reviews/ADR-0007-acquire-dependencies-by-route-loop-log.md:17`, `docs/reviews/UTA-0052-texture-memory-budget-loop-log.md:13`, `docs/specs/UTA-0003-package-container.md:48`, `docs/specs/UTA-0003-package-container.md:71`, `docs/specs/UTA-0005-class-tables-and-ancestry.md:256`, `src/upkg/Script.cpp:17`, `src/upkg/Script.cpp:415`, `src/upkg/Script.cpp:423`, `src/upkg/Script.h:40`)
- Out of scope (step 3, `build/`): cppcheck 11 in build/_deps (Catch2 headers), clang-tidy 2.

## Config recommendations

- cppcheck needs `-D__has_cpp_attribute(x)=0` (or an equivalent) to analyse any TU that includes Catch2. Without it, every test TU stops at a critical `syntaxError` that shows up as one XML entry. Worth recording as a per-project flag in `audit-config.json`.
- glslangValidator cannot read `fsr_easu.frag` / `fsr_rcas.frag` unless they declare `GL_GOOGLE_include_directive`. A future run should either use `glslc` for them or record that.
- No path exclusion is recommended. No rule-id suppression is recommended (forbidden here); the two dominant classes above are for `close-findings` / `suppressions_doc` to decide.

## Tree changed during the run

Another session edited 10 files between 18:19 and 18:25 SAST: `src/core/FileSystem.cpp`, `src/core/Log.cpp`, `src/ubake/Lamps.cpp`, `src/ubundle/AddedSun.cpp`, `src/upkg/Properties.cpp`, `tests/unit/BundleSunTest.cpp`, `tests/unit/CoreLogTest.cpp`, `tests/unit/PathSeedsTest.cpp`, `tools/ut-bound-scan/Scan.cpp`, `tools/ut-paths/Cli.cpp`. This run wrote nothing to the tree. The light tools (18:05–18:12), cppcheck (both passes, the second ending about 18:21) and clazy (ending 18:19) read those files before the edits. clang-tidy's src/ pass (18:20–18:26) may have read either version. The second clang-tidy pass (18:26–18:42) read the edited files. Findings at those paths may therefore describe the pre-edit code.
