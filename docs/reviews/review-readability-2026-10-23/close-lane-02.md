# close-findings: lane 02, renderer Vulkan plumbing

Closes every finding in [`lane-02-urender-vulkan.md`](lane-02-urender-vulkan.md)
under `UTA-0413`, and its correctness leads under `UTA-0401`. Run
2026-10-23. Roadmap lookup: `roadmap_query ids:[UTA-0401, UTA-0413]`, both
found, both `planned`; no other bullet names these findings.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| 1 | `gpu::Material` filled by position at three sites | Q1 | MEDIUM | yes: `defaults` gives 12 of 13 values | fixed | `Materials.cpp` | positional → designated initialisers naming all 13 fields. No red run: nothing observable changes | `ShaderTypes.h` `Material` field order: agrees |
| 2 | four pipeline builders repeat the fixed states; the additive blends disagree on alpha | Q1 | MEDIUM | yes | fixed | `Pipelines.cpp` | shared states → `GraphicsStates`/`sharedStates`, one `graphicsPipeline` creates every one; builders set only what differs. Each blend's alpha choice now says nothing reads it (lead 5). Same states field by field; the device tier runs under validation on both drivers | UTA-0014 SS 4.4–4.5 pass rules: agrees |
| 3 | UT99's light radius copied four times | Q1 | MEDIUM | yes | fixed in urender; ubake copies handed to `UTA-0416` | `Bundle.h`, `Shadows.cpp`, `light.glsl` | → `ubundle::worldLightRadius`; it and `light.glsl`'s `lightRadius` name each other | `UTA-0112` formula: agrees; `LightModel.cpp` `lightRadius`: out of scope, `UTA-0416` annotated |
| 4 | light-effect numbers defined five times | Q1 | LOW | yes | fixed in urender and ubundle; ubake handed to `UTA-0416` | `Bundle.h`, `LightSection.cpp`, `Shadows.cpp`, `Fog.h`, `Fog.cpp` | → `ubundle::LE_STATIC_SPOT`, `LE_SPOTLIGHT`, `isSpotEffect`; Fog.h's public copy removed | `light.glsl` values: agree |
| 5 | blend mode a bare int; `SceneVariant` filled by position | Q1/Q3 | MEDIUM | yes | fixed | `Pipelines.cpp`, `Pipelines.h` | → `enum class Blending`; `SceneVariant` holds it and is built with named fields; `sceneFor` returns by it | `Pipelines.h` `scene_` comment: fixed |
| 6 | `Pipelines::create` long; shadow layout copies the scene's | Q2 | MEDIUM | yes | queued, `UTA-0427` (the split); stuck points fixed | `Pipelines.cpp` | shadow layout built explicitly under its own banner; cluster and fog compute each own their create-info | n/a |
| 7 | `MaterialSet::upload` long; `liquidByRecord`'s rule unstated | Q2 | MEDIUM | yes | queued, `UTA-0427` (the split); stuck point fixed | `Materials.cpp` | one-entry-per-record rule stated beside `liquidByRecord` | `UTA-0040` names `upload`: agrees (name kept) |
| 8 | `check` says NotFound | Q4 | MEDIUM | yes: `Device.cpp` `check` | fixed | `Device.h` | → OutOfMemory or Unknown | n/a |
| 9 | a MATS record "carries only its id and metallic" | Q4 | LOW | yes | fixed | `Materials.h` | → carries no maps of its own | n/a |
| 10 | "five maps a material" | Q4 | LOW | yes: seven names looked up | fixed | `Resources.h` | → up to seven | n/a |
| 11 | `free_`'s comment sits on `size_` | Q4 | LOW | yes | fixed | `Shadows.h` | moved; `size_` says it is the atlas's side | n/a |
| 12 | `Plan::faces` | Q4 | LOW | yes | fixed | `Shadows.h` | → `ShadowPlan::faces` | n/a |
| 13 | `ShadowPlan::unitsPerTexel` holds the near lights' only | Q3 | LOW | yes; uses: `Shadows.cpp`, `RenderShadowsTest.cpp` | fixed | `Shadows.h`, `Shadows.cpp`, `RenderShadowsTest.cpp` | → `nearUnitsPerTexel`, beside `restUnitsPerTexel` | specs and docs: no hit outside `docs/reviews/` |
| 14 | `stageRoom`'s fallback to `RESERVED` unexplained | Q5 | LOW | yes: `Frame.cpp` refuses a bundle over `textureCapacity()` | fixed | `Pipelines.cpp` | comment: capacity 0, every bundle refused at load | n/a |
| 15 | `REQUIREMENTS`' count kept by hand | Q5 | LOW | yes | fixed | `Device.cpp` | → `std::to_array<Requirement>` | UTA-0014 SS 4.4 order: agrees (unchanged) |
| L1 | `created` not used for the image view and swapchain | lead | — | yes, and wider: `Device.cpp` `run`'s fence, `Frame.cpp`'s samplers, query pool, descriptor pool | fixed (`UTA-0401`) | `Resources.cpp`, `Swapchain.cpp`, `Device.cpp`, `Frame.cpp` | → `created`. No red run: no driver here can be made to fail those creates | `Device.h` `created` doc: agrees |
| L2 | a liquid-only material drops its look | lead | — | no: `umat::generate` writes a base map for every material | dismissed (`UTA-0401` annotated) | — | — | SS 6 default-material rule: agrees |
| L3 | fire materials upload their still's maps, then discard them | lead | — | yes | fixed (`UTA-0401`) | `Materials.cpp`, `RenderFireReplayTest.cpp` | maps skipped for a fire look. Red: 6 == 5; green after; restoring the upload re-reds | `UTA-0286` SS 4.4: agrees |
| L4 | additive blends disagree on alpha | lead | — | no: no shader reads those targets' alpha | dismissed (`UTA-0401` annotated); comments added under finding 2 | `Pipelines.cpp` | — | n/a |

## Run-level

- cited_by: old names searched in `docs/`, `README.md`, `CLAUDE.md`,
  `scripts/`, `tools/`, `apps/` (`radiusOf`, `unitsPerTexel`, `SceneVariant`,
  `REQUIREMENTS`, Fog.h's `LE_SPOTLIGHT`, the corrected comment phrases): no
  hit outside `docs/reviews/`. `scripts/mutations/ubundle.py` quotes a
  `Bundle.h` line this run did not change.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
- collateral: none.
- surfaced: none.
- out_of_scope: ubake's copies of the light radius and spot values, handed
  to `UTA-0416` by annotation.
- falsified: none.
