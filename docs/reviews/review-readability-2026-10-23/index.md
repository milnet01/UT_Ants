# review-readability, whole tree, 2026-10-23

The first `review-readability` run in this project. Report only: nothing was
fixed and nothing was written to the roadmap. Each lane's return is kept
verbatim in its own file beside this one.

## Lanes

Every lane was a `review-lane` on Opus, run headless through `neutral-lane`
from outside the project, so no lane loaded this project's `CLAUDE.md` or
memory. Each lane read every file it was given in full.

| Lane | Covers | Turns | Cost (USD) | HIGH | MEDIUM | LOW |
|---|---|---|---|---|---|---|
| [01](lane-01-urender-frame.md) | `src/urender`: the frame, lights, clusters, fog, probes, sky, zones, placement, tiers | 33 | 1.84 | 1 | 6 | 4 |
| [02](lane-02-urender-vulkan.md) | `src/urender`: device, pipelines, resources, shadows, materials, geometry, swapchain, present, CMake | 30 | 1.68 | 0 | 6 | 8 |
| [03](lane-03-urender-shaders.md) | `src/urender/shaders` | 42 | 1.66 | 1 | 7 | 14 |
| [04](lane-04-ubake-bake-content.md) | `src/ubake`: the bake, install, naming, actors, movers, lamps, flames, liquids, zones, strips, hide, tile kinds | 46 | 2.20 | 0 | 9 | 8 |
| [05](lane-05-ubake-geometry-light.md) | `src/ubake`: geometry, collision, occlusion, charts, shadow mask, surface rays, light model, probes, sway | 33 | 1.85 | 0 | 8 | 14 |
| [06](lane-06-ubundle.md) | `src/ubundle` | 44 | 1.96 | 1 | 1 | 12 |
| [07](lane-07-upkg-umap.md) | `src/upkg`, `src/umap` | 45 | 1.99 | 1 | 4 | 9 |
| [08](lane-08-umat-urecipe.md) | `src/umat`, `src/urecipe` | 40 | 1.80 | 0 | 5 | 7 |
| [09](lane-09-core-uworld-ufire.md) | `src/core`, `src/uworld`, `src/ufire`, `src/CMakeLists.txt` | 35 | 1.34 | 0 | 3 | 10 |
| [10](lane-10-unav-ut-paths.md) | `src/unav`, `tools/ut-paths` | 28 | 1.05 | 0 | 2 | 7 |
| [11](lane-11-app-ut-ants.md) | `apps/` | 29 | 1.33 | 1 | 8 | 10 |
| [12](lane-12-tools-cpp.md) | `tools/` in C++, except `ut-paths` | 61 | 2.37 | 1 | 9 | 8 |
| [13](lane-13-scripts-python.md) | `scripts/*.py`, `scripts/mutations/`, `tools/ut-compare/` | 22 | 1.25 | 0 | 2 | 13 |
| [14](lane-14-build-gate.md) | `scripts/*.sh`, `.githooks/`, root and test `CMakeLists.txt`, `.github/workflows/ci.yml` | 29 | 1.29 | 1 | 7 | 8 |

Lane total: 23.61 USD, from each lane's `neutral-lane` usage line. A first
dispatch of the same lanes was lost: another session deleted the shared
scratch directory while they ran. Its cost was not recorded.

## Candidates handed to the lanes

`function_sizes` (top 60), `duplicate_blocks` (8 lines or more, top 40) and
`comment_claims` ran once over the whole tree. Each lane received the rows for
its own files. `comment_claims` returned no tool-decided findings.

## Anchors handed to the lanes

`source_scrapes` found no test reading source as text. A search of `tests/`
found one: `tests/guard/quarantine-guard-test.sh` reads the
`FORMAT_VERSION` line of `src/ubundle/Bundle.h`. The mutation catalogs in
`scripts/mutations/` also hold verbatim source lines; lanes 04, 06 and 10
were told which of their files those catalogs quote.

## Premise checks

Each HIGH finding and each correctness lead resting on code outside the
lane's quote was re-checked by search or read on 2026-10-23.

| Lane | Item | Mark | Check |
|---|---|---|---|
| 01 | HIGH `Frame.cpp:1801`, one edit | confirmed | specs name `occlusionTexture` (UTA-0089, UTA-0164, UTA-0326); the fix renames no named field |
| 03 | HIGH `probes.glsl:79`, pinned | confirmed | UTA-0112 names `indirectAt` and `probeShareAt` together |
| 06 | HIGH `LampSection.cpp:20`, pinned | confirmed | grep `LIGHT_SIZE\|FLAME_RECORD\|MIN_LAMP`: copies at `LightSection.cpp:19`, `FlameSection.cpp:14`, `map-analyser.py:100`; UTA-0162 names `LIGHT_SIZE` |
| 07 | HIGH `Class.cpp:31`, one edit | confirmed, wider than stated | grep `tolower`: also `urender/Tiers.cpp`, `tools/map-pic/Cli.cpp`, `tools/ut-dump/Cli.cpp`, `apps/ut-ants/main.cpp`, `apps/ut-ants/MapList.cpp` |
| 11 | HIGH `Launcher.cpp:389`, one edit | confirmed | grep `Launcher\|finishChild` in `apps/ut-ants/Cli.*`: nothing points at the launcher's copy |
| 12 | HIGH `ut-shot/main.cpp:150`, unsure | confirmed | grep `verticalFovDegrees =`: the three tool readers; ut-shot names neither copy |
| 14 | HIGH `ci.sh:112` | confirmed | `docs-only.sh` accepts `docs/*` and its never-list has no `*.utab` |
| 01 | lead: `shapeOf` does not sample `bundle.rooms` | confirmed | no `rooms` in `Frame.cpp` `shapeOf`; `upload` reads it |
| 05 | lead: bounce passes' `lit` has no `reach` | confirmed | `lit.reach` is read at `LightProbes.cpp:441` and never set on the bounce copy |
| 12 | lead: `OverBudget` uses `result->budget` unchecked | refuted | `Bake.cpp` sets `outcome.result` on the one `OverBudget` path, and `budget` is a plain member (`Bake.h`) |
| 14 | lead: `--help` stops mid-sentence | confirmed | `ci.sh` prints lines 2 to 20 and `ci-matrix.sh` lines 2 to 30, each ending mid-sentence |

Other leads were not re-checked. Each rests on code the lane quoted from its
own files. MEDIUM and LOW findings were not re-checked one by one. A search of
every return for absence claims ("no caller", "unused", "dead code",
"unpinned") found three, each about code inside the lane's own files.
