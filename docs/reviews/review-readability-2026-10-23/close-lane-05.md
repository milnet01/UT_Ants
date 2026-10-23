# close-findings: lane 05, baker geometry and light

Closes every finding in
[`lane-05-ubake-geometry-light.md`](lane-05-ubake-geometry-light.md) and
`UTA-0416`'s notes from lanes 02 and 03 under `UTA-0416`, the bounce-mask
lead under `UTA-0400`, and the cover-point lead under `UTA-0404`, with lane
04's open `cornersOf` question. Run 2026-10-23. Roadmap lookup:
`roadmap_query ids:[UTA-0416, UTA-0400, UTA-0404]`, all three found,
`planned`; no other bullet names these findings.

The readability fixes change no baked byte. Proof: `ut-bake` built at
`bf7016a` (UTA-0400) and after the batch baked DM-ArcaneTemple, AS-Frigate,
DM-Fetid and CTF-Face; each bundle and each material light file is
byte-identical. `UTA-0400` changes every one of those four bundles' probes,
so the baker revision is 68. `UTA-0404` changes a bake only where a cover
point is past the bound, which no map baked here has; the golden bake is
unchanged by it.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| 1 | reach guard silently drops the mask | Q5 | MEDIUM | yes | fixed under `UTA-0400` | `LightProbes.cpp` | the bounces' `lit` gets `reach`; the guard says no reach is no mask | UTA-0112 § 4.13 item 2: now agrees |
| 2 | PolyFlags copied in six files | Q1 | MEDIUM | yes, plus `SurfaceRays.h`'s raw hex | fixed | `Geometry.h`, `Charts.cpp`, `Geometry.cpp`, `LightProbes.cpp`, `ShadowMask.cpp`, `Bake.cpp`, `SurfaceRays.h` | every bit in `Geometry.h`; `LIGHT_PASSES_FLAGS` built from names | UTA-0109 and UTA-0055 name `Geometry.h` as the home: agree |
| 3 | ShadowMask rewrites radius, litFrom, LE codes | Q1 | MEDIUM | yes | fixed | `ShadowMask.cpp`, `Bundle.h` | `lightRadius`; LE codes from `Bundle.h`; `litFrom` kept, naming its two twins | `light.glsl`'s own copies: agree with `Bundle.h` |
| 4 | sun and lamp texel loops drifted | Q1 | MEDIUM | yes | fixed | `ShadowMask.cpp` | one `bakeTexels` taking a per-sample share; `bakeSunPair` returns an optional | bake identity |
| 5 | `anyInFront` has no pointer to its twin | Q1 | MEDIUM | yes | fixed | `SurfaceRays.h`, `SurfaceRays.cpp` | one `walkInFront` with a margin, 0 or `PAD`; both names kept | UTA-0164 names both: agrees |
| 6 | tree walk written six times | Q1 | LOW | yes | fixed | `SurfaceRays.h`, `SurfaceRays.cpp` | `blocked(a, b)` calls the hole overload; `first` and `firstHas` share `nearest`, one tie rule | UTA-0112 names `blocked(a, b)`: kept |
| 7 | shelf packing and width loop copied | Q1 | LOW | yes | fixed | `Charts.h`, `Occlusion.cpp`, `ShadowMask.cpp` | `charts::shelve` with a start offset, `charts::widthFor`, `FIRST_ATLAS_WIDTH` | bake identity |
| 8 | "sends its picture" test written twice | Q1 | LOW | yes | fixed | `LightProbes.cpp` | `sendsItsPicture` | n/a |
| 9 | `draw` lambda owns the totals | Q2 | MEDIUM | yes | fixed | `Geometry.cpp` | a `Drawing` class whose `draw` is a named function; `FanTotals` adds and removes a fan | UTA-0109 names `buildGeometry`: kept |
| 10 | slab walk six levels deep | Q2 | MEDIUM | yes | fixed | `LightProbes.cpp` | `candidatesOf`, its maths explained | UTA-0112 names `bakeLightProbes`: kept |
| 11 | `prepare`'s overlap pass does five things | Q2 | MEDIUM | yes | fixed | `Sway.cpp` | `joinOverlap` records both edges; the caller joins patches | n/a |
| 12 | coarsening loop and height 4 unexplained | Q2 | LOW | yes | fixed | `ShadowMask.cpp` | `bakeAndPack` per texel size; empty-atlas exit and the 4-row floor commented (SMSK refuses height 0) | UTA-0326 names `bakeShadowMask`: kept |
| 13 | `sunSeen` means "meets sky first" | Q3 | LOW | yes | fixed | `LightProbes.h`, `LightProbes.cpp`, `ShadowMask.cpp`, `BakeSunTest.cpp`, UTA-0338, UTA-0350, UTA-0387 | → `meetsSkyFirst`; LPRB's `sunSeen` field keeps its name | specs: amended where they name the function |
| 14 | `batches` counts jobs | Q3 | LOW | yes | fixed | `LightProbes.cpp`, `ShadowMask.cpp`, `Occlusion.cpp` | → `jobCount` | local names; no doc |
| 15 | `Drawn`'s doc line sits on `Mapping` | Q4 | LOW | yes | fixed | `Geometry.cpp` | moved | n/a |
| 16 | `LIGHT_PASSES_FLAGS`' comment in the .cpp | Q4 | LOW | yes | fixed | `SurfaceRays.h`, `SurfaceRays.cpp` | moved onto the constant | n/a |
| 17 | step 4's surface half is in `draw` | Q4 | LOW | yes | fixed | `Geometry.cpp` | steps 4b and 4c named in `draw` | n/a |
| 18 | strip sample offset misstated | Q4 | LOW | yes | fixed | `ShadowMask.cpp` | states (`offset` + 0.5) / 4 | n/a |
| 19 | `bakeLightProbes` doc omits fields | Q4 | LOW | yes | fixed | `LightProbes.h` | names `reach`, `sunSeen`, `skySeen` | n/a |
| 20 | dead `LE_NON_INCIDENCE` test | Q5 | LOW | yes | fixed | `LightModel.cpp` | dropped; spot test is `isSpotEffect` | n/a |
| 21 | bare `keys[0]` | Q5 | LOW | yes | fixed | `ShadowMask.cpp` | `try_emplace(0)`, commented | n/a |
| 22 | sway's bare 8 | Q5 | LOW | yes | fixed | `Sway.cpp` | `ESTIMATE_SKIP_FACTOR`, with why lowering it changes a bake | n/a |
| N1 | UTA-0413: ubake's LE values and radius body | — | — | yes | fixed | `LightModel.cpp`, `Bundle.h`, `Strips.cpp` | all LE values ubake reads in `Bundle.h`; `lightRadius` returns `worldLightRadius` | `Strips.cpp`'s `LE_CYLINDER` went too |
| N2 | lane 03: dead `LE_NON_INCIDENCE` test | — | — | yes | fixed, as 20 | — | — | — |
| L1 | bounce passes skip the reach mask | lead | — | yes | real, fixed under `UTA-0400` (`bf7016a`) | `LightProbes.cpp`, `Name.h`, `BakeGoldenTest.cpp`, `BakeLightProbesTest.cpp`, `CLAUDE.md`, `CHANGELOG.md` | red: a roofed room under a lit hold held 4.494, 0 after; revision 68 | UTA-0112 § 4.13: agrees |
| L2 | cover points skip UTA-0217's check | lead | — | yes: Hide's corners come from possibly invisible nodes; an infinite corner gives a NaN hull point | real, fixed under `UTA-0404` (`9abef96`) | `Geometry.cpp`, `Geometry.h`, `BakeGeometryTest.cpp`, UTA-0109 | red: a NaN cover point was drawn; refused now. Mutant `<=` → `<`: killed | UTA-0109 § 4.6 step 4 and INV-12: amended |
| L3 | lane 04: `cornersOf` lacks Flames' bound check | lead | — | yes | not needed: closed by L2, which checks what `cornersOf` builds where it is drawn | — | — | n/a |

## Run-level

- cited_by: tree search for `sunSeen(`, the removed PolyFlags and LE copies,
  `batches`, `anyInFront`, `gatherInFront`, `blocked(a, b)`, `shelve` and
  `LIGHT_PASSES_FLAGS`: hits in UTA-0338, UTA-0350 and UTA-0387 (amended),
  UTA-0164 and UTA-0292 (agree), and `docs/reviews/` (frozen).
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
  `BakeGoldenTest.cpp`'s note that 67's bounces read the reach was false;
  revision 68's note says so.
- collateral: none.
- surfaced: none.
- out_of_scope: `LT_BACKDROP_LIGHT` is still copied in `LightProbes.cpp`,
  `Strips.cpp`, `urender/Lights.h` and `ubundle/LightSection.cpp`. The unit
  tests keep private PolyFlags copies (`BakeLightProbesTest.cpp`,
  `BakeGeometryTest.cpp`).
- falsified: none.
