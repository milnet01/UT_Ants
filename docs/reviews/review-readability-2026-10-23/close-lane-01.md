# close-findings: lane 01, renderer frame side

Closes every finding in [`lane-01-urender-frame.md`](lane-01-urender-frame.md)
under `UTA-0412`, and its correctness lead under `UTA-0399`. Run
2026-10-23. Roadmap lookup: `roadmap_query ids:[UTA-0399, UTA-0412]`, both
found, both `planned`; no other bullet names these findings.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| 1 | texture-array slots worked out by hand at six sites | Q1 | HIGH | yes: `upload`, `writeDescriptors`, `drawView` each sum the slots themselves; `firstLight` adds the sky's 1 unconditionally | fixed | `Frame.cpp` | six hand sums → `TextureSlots`/`textureSlotsOf`, one order; `writeDescriptors` writes each texture at its slot index. No red run: nothing observable changes. Swapping two slots in `textureSlotsOf` survives the device tier, as it should: every site now moves together | `UTA-0163`, `UTA-0164` SS 4.5, `UTA-0326` SS 4.5, `UTA-0337` texture order: agrees |
| 2 | `recordFrame` long; `drawBatches` mixes three filters; object 0 = the level is implied | Q2 | MEDIUM | yes: the filter block in `drawBatches` | queued, `UTA-0426` (the split); stuck points fixed | `Frame.cpp` | inline sky-capture conditions → `skyCaptureLeavesOut`, its rule stated and object 0 named as the level (Geometry.h). Same truth table, checked case by case | `UTA-0337` capture rule: agrees |
| 3 | `drawView` long; the size check's scope is unstated | Q2 | MEDIUM | yes, narrowed: the check already sits just above the two writes it guards; what is missing is what it guards | queued, `UTA-0426` (the split); stuck point fixed | `Frame.cpp` | comment now says only the lights and shadow faces are sized at upload; clusters fixed, objects by geometry, volume lights capped (`Fog.cpp` `VOLUME_LIGHT_CAPACITY`) | n/a |
| 4 | the file header lists the stages out of order and leaves some out | Q4 | MEDIUM | yes: fog (0.5) listed after step 1; shadows, depth pre-pass, flames missing | fixed | `Frame.cpp` | header lists recordFrame's sections in run order, shadow tiles, pre-pass and flames included | recordFrame's section headings: agrees |
| 5 | the fog set's bindings are bare numbers | Q5 | MEDIUM | yes: `Pipelines.cpp` fog layout, `writeDescriptors` | fixed | `ShaderTypes.h`, `Pipelines.cpp`, `Frame.cpp`, three `.comp` | bare 0–4 → `gpu::FogBinding`; each fog shader points at it | `fog_scatter.comp`, `fog_integrate.comp`, `shadow_reduce.comp` numbers: agree |
| 6 | `CLUSTER_COUNT` restates `CLUSTER_GRID` | Q1 | MEDIUM | yes: `ShaderTypes.h`, `Clusters.h` header and index comment | fixed | `ShaderTypes.h`, `Clusters.h` | product of `CLUSTER_GRID`; comments name the grid and `Clusters.cpp`'s index | `tests/unit/RenderClusterTest.cpp` literal: agrees (a test's own expected value) |
| 7 | `ShaderTypes.h` claims every offset is asserted | Q4 | MEDIUM | yes: the listed fields have no `offsetof` assert | fixed | `ShaderTypes.h` | missing asserts added; a script over every struct now finds no field without one | header claim: agrees |
| 8 | `smoothNoise` and `flickerOf` hold at different limits | Q4 | LOW | yes: `0x1p62` and `0x1p63` | fixed | `Lights.cpp` | both through one `stepOf`, limit 2^63. Behaviour moves only for times past 1.5e18 s | `UTA-0217`: agrees |
| 9 | `defaultTier`'s comment points at a conversation | Q4 | LOW | yes | fixed | `Tiers.cpp` | names 2, 4 and 8 GB, SS 4.3's table and SS 3 decision 4 | `UTA-0051` SS 3, SS 4.3: agrees |
| 10 | `flashFog` holds ViewFog | Q3 | LOW | yes: `ShaderTypes.h`, `post.frag`, UTA-0215 spec | fixed | `ShaderTypes.h`, `post.frag`, `UTA-0215` spec | renamed `viewFog`; spec SS 4.2 amended in the same commit | review records naming `flashFog`: frozen |
| 11 | `halvesPerPixel` is 2 for an RGBA8 target | Q3 | LOW | yes: `readback` | fixed | `Frame.cpp` | → `bytesPerPixel`; the half count named where halves are read | n/a |
| L | `shapeOf` does not sample `bundle.rooms` (UTA-0399) | lead | — | yes: `upload` reads rooms for light zones and `skyLayersOf` | fixed (`UTA-0399`) | `Frame.cpp`, `RenderFogTest.cpp` | rooms sampled. Red: edited 0, control 255; green after; removing the zone sample re-reds | `BundleShape` comment: fixed |

## Run-level

- cited_by: old names searched tree-wide (`flashFog`, `halvesPerPixel`,
  `0x1p62`, `CLUSTER_COUNT`, bare fog bindings); hits outside `docs/reviews/`
  were the UTA-0215 spec and `RenderClusterTest.cpp`. No doc cites a line of
  an edited file outside `docs/reviews/`.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
- collateral: none.
- surfaced: none.
- out_of_scope: none.
- falsified: none.
