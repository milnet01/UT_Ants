# close-findings: lane 03, renderer shaders

Closes every finding in [`lane-03-urender-shaders.md`](lane-03-urender-shaders.md)
under `UTA-0414`, and its correctness leads under `UTA-0402`. Run
2026-10-23. Roadmap lookup: `roadmap_query ids:[UTA-0402, UTA-0414]`, both
found, both `planned`; a query for the four shader names over active items
found no other bullet naming these findings.

Every fix is a pure readability change: no rendered pixel moves. Proof:
`ut-shot` built before and after the batch drew the same 27 views (3 maps
baked at revision 67: DM-ArcaneTemple's pool from above and below,
AS-Frigate outdoors, DM-Fetid's fog), tier ultra, light time 0, on
lavapipe; every frame is byte-identical.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| 1 | eight-corner probe blend written twice | Q1 | HIGH | yes | fixed | `probes.glsl` | per-function corner walk → `probeCorner` (share and reach test once); `indirectAt` names its twin for the fallback; loop local `step` → `offset` | UTA-0112 § 4.13 item 2 names both functions: agrees (both kept, both apply it); probe_eval.comp (INV-7): builds, passes |
| 2 | three probe points half a spacing off, diverged | Q1 | MEDIUM | yes | fixed | `scene.frag` | three copies of the offset → `probePointOn(side)`; reflection and haze say why they read the eye's side (lead 1) | UTA-0089 § 4.2: amended under `UTA-0402`; UTA-0215 § 4.6: agrees |
| 3 | `main`'s lit branch too long | Q2 | MEDIUM | yes | fixed | `scene.frag` | chart and cluster gathering → `chartedLight`, `clusterLight`; nothing new held across the light loop (UTA-0352) | UTA-0326 § 4.5, UTA-0345: agree |
| 4 | waterfall loop's end unstated | Q4 | MEDIUM | yes | fixed | `scene.frag` | comment says how it ends and that nothing goes before the break | n/a |
| 5 | `fogCoordinate`'s comment heads the water block | Q4 | MEDIUM | yes | fixed | `fog.glsl` | moved onto `fogCoordinate` | n/a |
| 6 | 70 lines of superseded refits | Q4 | MEDIUM | yes | fixed | `fog.glsl`, `docs/fog-constant-fits-history.md`, UTA-0015 | rule + latest sweep stay; older fits moved verbatim; undated "today's" dated | UTA-0015 § 7 "scores beside the constants": three older ones repointed, two current ones agree |
| 7 | sRGB encode/decode copied | Q1 | MEDIUM | yes | fixed | `fog.glsl`, `scene.frag`, `post.frag` | `linearToDisplayed` beside `displayedToLinear`; scene.frag's three and post.frag's pair call them | UTA-0015 § 4.3 (`displayedToLinear` in `fog.glsl`): agrees |
| 8 | workgroup width 32 a bare literal | Q5 | MEDIUM | yes | fixed | `cluster.comp` | → `CLUSTER_GROUP_SIZE` for the size and both arrays | n/a |
| 9 | `reach` holds a strength | Q3 | LOW | yes | fixed | `cluster.comp` | → `strength`/`strengthLists` | no doc names it |
| 10 | `step` hides `step()` | Q3 | LOW | yes | fixed | `liquid.glsl` | → `dropStep` | n/a |
| 11 | NOT FITTED reads onto `ICE_PAN_PER_UNIT` | Q4 | LOW | yes | fixed | `liquid.glsl` | says it is the Ice look's churn; the pan rate is measured | n/a |
| 12 | "the four LIQUID_ ones" | Q4 | LOW | yes | fixed | `liquid.glsl` | names the four | n/a |
| 13 | "the four look constants" | Q4 | LOW | yes | fixed | `fog.glsl` | names the five | n/a |
| 14 | `LE_NON_INCIDENCE` test cannot be false | Q4 | LOW | yes (now `light.glsl:144`) | fixed | `light.glsl` | condition dropped | `ubake/LightModel.cpp`'s twin: out of scope |
| 15 | `Zone` described as ambient | Q4 | LOW | yes | fixed | `types.glsl` | says `sky` and `water` are read, the ambient bytes not | n/a |
| 16 | 2π as literals | Q1 | LOW | yes | fixed in `scene.frag` | `scene.frag` | → `TWO_PI` (same float32). `mote.vert`'s 6.2832 left: changing it moves pixels | n/a |
| 17 | `ProbeLattice` built from `frame` three times | Q1 | LOW | yes | fixed | `probes.glsl`, `scene.frag`, `water.glsl` | → `frameProbeLattice()`, compiled only after `scene_bindings.glsl` | probe_eval.comp has no `frame`: builds |
| 18 | `depthAt` copies the blend edges | Q5 | LOW | yes | fixed | `variation.glsl`, `scene.frag` | → `TILE_BLEND_START`/`END`, used by both | UTA-0277 names `tileWeight`: agrees (kept) |
| 19 | masked/backdrop exclusion unexplained | Q5 | LOW | yes | fixed | `scene.frag` | → `cutOutOrBackdrop` with its reasons | UTA-0040 § 4.3, UTA-0277 § 4.2: agree |
| 20 | `fsr_easu.frag`'s `PostBlock` copy untied | Q1 | LOW | yes | fixed | `fsr_easu.frag` | comment names post.frag and `PostConstants` | n/a |
| 21 | "4 x 4" is `HAZE_SHADOW_STEP` | Q4 | LOW | yes | fixed | `fog_scatter.comp` | names it | n/a |
| 22 | cluster grid numbers copied | Q4 | LOW | yes | fixed | `clusters.glsl` | names `gpu::CLUSTER_GRID` | n/a |
| L1 | reflection reads the eye's side's probes, not the lit side's | lead | — | yes, and deliberate: `46383af` (UTA-0215) moved the lighting's probe point and edited this line's sky test in the same commit | not a bug (`UTA-0402` annotated); spec sentence predates UTA-0215, amended | `UTA-0089` | — | n/a |
| L2 | dead `LE_NON_INCIDENCE` test | lead | — | yes | not a bug: dead code, no output; removed under finding 14 (`UTA-0402` annotated) | — | — | n/a |

## Run-level

- cited_by: `trace` on `probeShareAt` and `encodeSrgb`;
  grep of the tree for `encodeSrgb`, `decodeSrgb`, `reachLists`, "four
  look constants", "the probe point `scene.frag`", "beside the
  constant(s)": hits only in `docs/reviews/`, UTA-0015 § 7 (handled) and
  `ubake/LightModel.cpp`. No test reads a shader as text.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
- collateral: none.
- surfaced: none.
- out_of_scope: `src/ubake/LightModel.cpp` carries the same dead
  `LE_NON_INCIDENCE` test after its own early return; handed to `UTA-0416`
  by annotation.
- falsified: none.
