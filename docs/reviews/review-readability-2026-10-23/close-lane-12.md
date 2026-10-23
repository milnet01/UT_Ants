# close-findings: lane 12, the C++ tools

Closes every finding in
[`lane-12-tools-cpp.md`](lane-12-tools-cpp.md) under `UTA-0423`, its
correctness leads under `UTA-0409`, and two carried-in notes: `ut-dump`
wrote its own class-ancestry test (lane 04), and
`tests/real/RealPathSeedsTest.cpp` kept a copy (lane 10). Run 2026-10-23.
Roadmap lookup: `UTA-0423` and `UTA-0409` exist, both planned before this
run.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| L1 | ut-bench builds paths from narrow strings | lead | — | no: `CMakeLists.txt` adds `cmake/utf8.manifest` to every MSVC program by `add_link_options`, before `add_subdirectory(tools)`; `CoreFileSystemTest.cpp` "the narrow encoding is UTF-8 on every platform" checks it | dismissed | — | no edit. Windows not run here | n/a |
| L2 | `--size` has no upper bound | lead | — | yes: `--size 20000x16` on lavapipe exited 0 with a timing table while the validation layer reported `VUID-VkImageCreateInfo-extent-02252` and three more | fixed, `26de15e` | `Frame.cpp`, `BenchCliTest.cpp`, UTA-0129 | → refused above `tools::MAX_DIMENSION`, exit 2. Red: `1 == 2` twice. Both bounds mutated, both killed | UTA-0129 SS 4.4: fixed in the same pass |
| L3 | a refused chain drops exits' and waypoints' defaults silently | lead | — | yes, and wider: a missing package does too | fixed, `4ffdd81` | `ut-dump/Cli.cpp`, `DumpCliTest.cpp`, UTA-0012 | → each element ends with `chainEnd`, from one walk, `classFamilyOf`. Red on four checks. Four mutations, all killed | UTA-0012 SS 4.7a, 4.7b: fixed; SS 4.9 says adding a key is no schema change |
| C1 | `ut-dump` writes its own ancestry test | lane 04 | — | no: `kindOf` reads a `upkg::Ancestry` chain of names; `ubundle::isOrDescendsFrom` takes a baked `ActorClass`. Different input, so nothing to fold | dismissed | — | no edit | n/a |
| C2 | `RealPathSeedsTest.cpp` repeats `isOrDescendsFrom` | lane 10 | — | yes; the same path-then-ancestry equality test on `ActorClass` | fixed | `RealPathSeedsTest.cpp` | → the helper, by a using-declaration | n/a |
| 1 | three diverged camera-line readers | Q1 | HIGH | yes | fixed, `68ed2ca` | `tools/common/Camera.h`, ut-shot, ut-bench, ut-ref | → one reader, conversion and both limits. Behaviour change: ut-shot and ut-ref skip a blank line, as ut-bench did | UTA-0121 SS 4.8: agrees (it names the one escaper, not the folder's only file); UTA-0292: fixed |
| 2 | `ut-dump` says it does not link the baker | Q4 | MEDIUM | yes | fixed | `ut-dump/Cli.cpp` | → the comment went with the guard; `modelOf` says it checks as `findModel` does | n/a |
| 3 | the Model guard written three times | Q1 | MEDIUM | yes | fixed | `ut-dump/Cli.cpp` | → `modelOf`. Every refusal now names the reader's reason: "the level's Model did not read: ..." | no spec pins the text (searched `docs/specs`, `tests`) |
| 4 | the class-family walk written five times | Q1 | MEDIUM | yes | fixed | `ut-dump/Cli.cpp` | → `classFamilyOf` in all five; `levelActorExports` replaces every deduplicated actor-set loop. `levelInfoOf` keeps its own: it stops at the first LevelInfo | INV-8 tests: agree |
| 5 | `writeActorWiring`'s cache-fill nests eight deep | Q2 | MEDIUM | yes | fixed | `ut-dump/Cli.cpp` | → a short block through `classFamilyOf` and `familyWiring`; no separate function needed | n/a |
| 6 | `brushes`' "every other actor" | Q4 | MEDIUM | yes | fixed | `ut-dump/Cli.cpp` | → "every actor not in `list`, blockers included", in the comment and the help | UTA-0012 SS 4.7e: agrees |
| 7 | `Json.h` names three users of five | Q4 | MEDIUM | yes | fixed | `Json.h` | → no list; says each include is a consumer, ut-bound-scan's among them | n/a |
| 8 | the option-value helper written six times | Q1 | MEDIUM | yes | queued as `UTA-0431` | — | spans ut-paths and changes each tool's refusals: one rule to decide for six tools | n/a |
| 9 | frame output writes schema 1 literally; exit codes twice | Q1 | MEDIUM | yes | fixed | ut-bench `Cli.h`, `Cli.cpp`, `Frame.cpp` | → `detail::SCHEMA` and `EXIT_*` once, in `Cli.h` | n/a |
| 10 | ut-shot `main` carries 55 lines of diagnostics | Q2 | MEDIUM | yes | fixed | `ut-shot/main.cpp` | → `reportProbes`, `reportLights`. Standard error identical | n/a |
| 11 | `writeSurfaces`' doc sits over another declaration | Q4 | LOW | yes | fixed | `ut-dump/Cli.cpp` | → the forward declaration moved above it | n/a |
| 12 | ut-bake help: "second form", a hand-copied 10 | Q4 | LOW | yes | fixed | `ut-bake/Cli.cpp` | → "the --install form"; the count streamed from `BUDGET_LIST_SHORT` | n/a |
| 13 | `MapSummary::phases` order; header names bake only | Q4 | LOW | yes | fixed | ut-bench `Cli.h` | → the order runs first show them; both workloads named | n/a |
| 14 | two `fixed` of different precision | Q3 | LOW | yes | fixed | ut-bench `Cli.cpp`, `Frame.cpp` | → `sixPlaces`, `threePlaces` | n/a |
| 15 | private case-folders in map-pic and ut-dump | Q1 | LOW | yes: neither copy is left | fixed by lane 07 (`close-lane-07.md`), before this run | — | — | n/a |
| 16 | ut-bound-scan's own hex encoder | Q1 | LOW | yes | fixed | `ut-bound-scan/Cli.cpp` | → `ubake::detail::hex`. Its md5 equals `md5sum` on DM-Fetid and on a non-package | n/a |
| 17 | `dumpPackage`'s six positional bools | Q2 | LOW | yes | fixed | `ut-dump/Cli.cpp` | → a `Sections` struct | n/a |
| 18 | `keyRot` writes indices, unsaid | Q5 | LOW | yes | fixed | `ut-dump/Cli.cpp` | → a line naming UTA-0012 SS 4.7e | n/a |

## Run-level

- cited_by: tree search, review records left out, for
  `usableHorizontalFov`, `MAX_DIMENSION`, `-<line>`, `propertiesRead`,
  `the level's Model did not read`, `descendsFrom`, `common/Json.h`: hits
  in `tools/`, `tests/unit/ShotCliTest.cpp`, `tests/unit/DumpCliTest.cpp`
  and UTA-0292, all updated. UTA-0129 already said blank lines are
  skipped.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
  Identity runs: ut-dump with every flag on DM-Fetid, CTF-Face, AS-Frigate,
  DM-ArcaneTemple and four MH maps, byte-identical to the `UTA-0409`
  commit; over the whole `Maps` folder, one map differs, the finding 3
  refusal text. ut-shot, ut-ref and ut-bench on one camera file: same
  cameras, same pictures, same `.f32`, same standard error. ut-bake on the
  four maps: the same bundles and the same JSON but for the output folder.
- collateral: none.
- surfaced: none.
- out_of_scope: none.
- falsified: none.
