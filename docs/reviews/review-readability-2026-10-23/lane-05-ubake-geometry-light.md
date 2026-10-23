I found 21 readability findings and one likely bug. The bug: the bake's bounce passes never apply the per-probe "reach" mask that the spec requires. The rest are mostly hand-kept copies of one rule spread across several files, plus four long functions.

## Files read
Totals are the last line with content; each file ends with a newline. All read in full with `Read`.
- src/ubake/Charts.cpp — 162 / 162
- src/ubake/Charts.h — 57 / 57
- src/ubake/Collision.cpp — 205 / 205
- src/ubake/Collision.h — 38 / 38
- src/ubake/CollisionQuery.h — 22 / 22
- src/ubake/Geometry.cpp — 474 / 474
- src/ubake/Geometry.h — 135 / 135
- src/ubake/LightModel.cpp — 224 / 224
- src/ubake/LightModel.h — 171 / 171
- src/ubake/LightProbes.cpp — 634 / 634
- src/ubake/LightProbes.h — 212 / 212
- src/ubake/MaterialLight.cpp — 66 / 66
- src/ubake/MaterialLight.h — 37 / 37
- src/ubake/Occlusion.cpp — 172 / 172
- src/ubake/Occlusion.h — 47 / 47
- src/ubake/ShadowMask.cpp — 484 / 484
- src/ubake/ShadowMask.h — 63 / 63
- src/ubake/SurfaceRays.cpp — 352 / 352
- src/ubake/SurfaceRays.h — 109 / 109
- src/ubake/Sway.cpp — 423 / 423
- src/ubake/Sway.h — 86 / 86

## What I started with
- The global `~/.claude/CLAUDE.md` (machine-wide rules).
- A user-email context block.
- A SessionStart hook note saying the Ants MCP verbs are connected.
- No project `CLAUDE.md`, no memory index, no git snapshot.
- The brief, and `shared-context.md` (the five questions and the quoted `coding.md` sections).

No disagreement between the brief and the lane file needed settling. I went outside my files only to confirm findings: `Hide.cpp` lines 120–162, spec UTA-0112 § 4.13, and the searches named under each finding.

## Summary
This code turns a Unreal Tournament map's level data into the ready-made files the game engine loads:
- the triangles to draw, and the collision tree
- small light "probes" that hold light bounced between surfaces
- a darkening map for corners (ambient occlusion)
- a shadow map per light
- which cloth and water surfaces sway, and how much

It all runs once, offline, and must give the same bytes on every computer. Most of it is careful, and its comments are good. The weak spots are rules copied by hand across files, which a stranger would change in one place and miss in the others. Four functions are long enough that a stranger loses the thread partway through.

## Findings

[Q5] MEDIUM `src/ubake/LightProbes.cpp:441` — `if (index < lit.reach.size() && !probeReaches(lit.reach[index], d)) continue;`
- **What goes wrong:** the size guard quietly turns spec § 4.13's reach mask off whenever `reach` is empty. `bakeLightProbes` builds `lit` with only `spacing` and `probes` (lines 613–615), so every bounce pass bakes with the mask off. Nothing says so.
- **The stranger's mistake:** spec § 4.13 item 2 says the bake's own copy of the blend applies the mask. A stranger reading `indirectAt` would believe the bake masks, and would chase a mismatch with `probes.glsl` in the wrong place.
- **Fix:** copy `out.reach` into `lit`, and say beside the guard that an empty `reach` means "no mask".
- **Cost:** one edit. Searched: `\.reach\b` across the tree. Only `LightProbes.cpp:441/583/598` touch `reach` in ubake; nothing else sets `lit.reach`.

[Q1] MEDIUM `src/ubake/Geometry.h:41` — `/// The EPolyFlags bits this library reads.`
- **What goes wrong:** the comment claims this is the list of flags ubake reads. It is not:
  - `Charts.cpp:14-17` adds `PF_FAKE_BACKDROP`, `PF_UNLIT` and `PF_PORTAL`.
  - `Geometry.cpp:56` adds `PF_PORTAL` again.
  - `LightProbes.cpp:24-26` copies `PF_TWO_SIDED`, which `Geometry.h:48` already holds.
  - `ShadowMask.cpp:29-31` copies `PF_MASKED` and `PF_TWO_SIDED`.
  - `Bake.cpp:862` has another `PF_FAKE_BACKDROP`.
  - `SurfaceRays.h:30` writes 0x04, 0x08 and 0x40 as raw hex, though `Geometry.h` names two of them.
- **The stranger's mistake:** they trust the header as the full list and miss the hidden copies. A past fix already pulled one copy out: spec UTA-0109 says "`Bake.cpp`'s own `PF_MASKED` is gone". `ShadowMask.cpp:30` has grown it back.
- **Fix:** keep every PolyFlags bit in `Geometry.h` (or one flags header) and delete the private copies.
- **Cost:** pinned, but in the fix's favour. UTA-0109 (around line 326) and UTA-0055 (around line 67) name `Geometry.h` as the flags' home. Searched: `constexpr std::uint32_t PF_…` in src, and the PF_ names in docs.

[Q1] MEDIUM `src/ubake/ShadowMask.cpp:82` — `site.reach = 25.0 * (light.radius + 1);`
- **What goes wrong:** this file includes `LightModel.h`, yet rewrites three things from it:
  - `lightRadius` (`LightModel.cpp:130`), here at line 82
  - `litFrom` (`LightModel.cpp:156`), here as `ShadowMask.cpp:89`
  - the effect codes `LE_NON_INCIDENCE` and `LE_CYLINDER` (`LightModel.cpp:25-26`), here as `ShadowMask.cpp:34-35`
- **The stranger's mistake:** `LightModel.h` says "THIS IS THE REFERENCE" and names only the runtime's copies. Someone changing the radius or the strip rule there would miss the shadow-mask copy. Its comment points at `light.glsl`, not at `LightModel`.
- **Fix:** call `lightRadius`, and share the effect codes from `LightModel.h`. `litFrom` can be fed a `Site`, or keep it with a comment naming the twin.
- **Cost:** one edit. Searched: `25\.0 \* \(|lightRadius\(` in the tree and `siteOf` in docs; no spec names `siteOf`. `urender/Shadows.cpp:23` is a runtime copy, which `LightModel.h` allows.

[Q1] MEDIUM `src/ubake/ShadowMask.cpp:227` — `int visible = 0;`
- **What goes wrong:** `bakeSunPair` (lines 223–244) and `bakeChart` (lines 276–299) each hold their own copy of one loop: per texel, four samples at 0.25/0.75, round, then track "all lit" and "all dark". The copies have drifted apart:
  - `int` against `double` for the count
  - a side chosen once from `facing`, against a side chosen per sample from `litFrom`
  - "no pair" signalled by `light = index + 1`, against `continue`
- **The stranger's mistake:** they change the sample pattern or the rounding in one and not the other. Then sun shadows and lamp shadows sample differently, and nothing fails.
- **Fix:** one texel loop that takes a per-sample "share seen" function.
- **Cost:** one edit. Searched: `bakeSunPair` and `bakeChart` in docs; neither appears.
- Confirms dup:d93e0331fc70.

[Q1] MEDIUM `src/ubake/SurfaceRays.cpp:222` — `if (std::max({dot(t.a - origin, normal), ...}) <= 0) continue;`
- **What goes wrong:** `anyInFront` (lines 201–227) and `gatherInFront` (lines 229–258) must keep the same two tests, the second loosened by `PAD`. Only `gatherInFront`'s comment (lines 232–234) says so; `anyInFront` carries no pointer back.
- **The stranger's mistake:** they tighten `anyInFront`'s triangle test alone. `opennessAt`'s shortcut and its gathered list then disagree, and occlusion texels change silently.
- **Fix:** one shared walk taking a margin (0 or `PAD`), or at least a comment on `anyInFront` naming its twin.
- **Cost:** one edit, keeping both names. UTA-0164 (around lines 299 and 304) names both functions, but a shared body renames nothing. Searched: both names in docs.
- Confirms dup:e15318739da0, in part.

[Q1] LOW `src/ubake/SurfaceRays.cpp:327` — `bool SurfaceRays::blocked(const Vec3& a, const Vec3& b, const Hole& hole) const {`
- **What goes wrong:** the stack-and-box tree walk is written six times. `blocked` and `blocked`-with-a-hole (lines 273–295 and 327–350) differ only in the hole test. `firstHas` restates `first()`'s tie rule in a different form (line 318 against line 177).
- **The stranger's mistake:** a change to the `0 < t < 1` range or the tie rule lands in one copy.
- **Fix:** one `blocked` with an optional hole, and `firstHas` built on a shared nearest-hit walk.
- **Cost:** one edit. UTA-0112 lists `blocked(a, b)` (around line 355); its signature stays. Searched: `blocked\(|firstHas` in docs.
- Confirms the rest of dup:e15318739da0.

[Q1] LOW `src/ubake/Occlusion.cpp:124` — `width = 64;`
- **What goes wrong:** shelf packing (`Occlusion.cpp:32-47` and `ShadowMask.cpp:311-326`) and the width-growing loop around it (`Occlusion.cpp:124-131` and `ShadowMask.cpp:430-437`) are copied. They differ in the white-block start and in `order.empty()`, and the bare `64` has no name in either.
- **The stranger's mistake:** they change the starting width or the packing order in one atlas only.
- **Fix:** name the starting width, and share one packer that takes a start offset.
- **Cost:** one edit. Searched: `shelve` in docs; nothing.
- Confirms dup:5b7c2a1685f3.

[Q1] LOW `src/ubake/LightProbes.cpp:469` — `if ((batch.polyFlags & PF_UNLIT) == 0 || !(mine.unlitGlows || hit->viaSky)) {`
- **What goes wrong:** this is `sentFrom`'s line 253 test, inverted. The rule "which unlit surface sends its picture" lives in two places, and the 2026-10-08 `viaSky` change had to land in both.
- **The stranger's mistake:** adding a new kind of glowing surface to one and not the other.
- **Fix:** one named test, such as `sendsItsPicture(batch, mine, hit)`.
- **Cost:** one edit. It is a new name, so nothing to search.

[Q2] MEDIUM `src/ubake/Geometry.cpp:273` — `const auto draw = [&](std::size_t n, std::vector<Vec> points) -> Result<void> {`
- **What goes wrong:** `buildGeometry` holds an 88-line lambda that changes two outer counters. Those counters are then unwound and redone at lines 417–418 and 432–433, and checked twice (lines 301 and 436).
- **Where the stranger gets stuck:** around line 417, `vertexTotal -= …`, working out who owns the count. Changing the fan size (for example, a sway piece) means finding all three sites.
- **Fix:** make `draw` a named function on a small builder that owns the totals.
- **Cost:** pinned. UTA-0109 (around line 71) names `buildGeometry` in `Geometry.h/.cpp`. The name survives an extraction, so the spec probably does not block it. Searched: `buildGeometry` in docs.

[Q2] MEDIUM `src/ubake/LightProbes.cpp:541` — `if (nd != 0) {`
- **What goes wrong:** the slab walk (lines 526–559) is six levels deep inside `bakeLightProbes`. Lines 542–549 mix world units, cell units and clamps to `lastD + 1` / `firstD - 1`, with one terse comment.
- **The stranger's mistake:** changing the slab's widening would likely drop candidates. The comment at lines 521–525 promises the candidate set equals the whole box's.
- **Fix:** move "candidate cells of one triangle" into its own function, with the slab maths explained there.
- **Cost:** pinned. UTA-0112 (around lines 371 and 435) names `bakeLightProbes`; its name survives. Searched: `bakeLightProbes` in docs.

[Q2] MEDIUM `src/ubake/Sway.cpp:207` — `for (const auto& [cell, members] : cells)`
- **What goes wrong:** `prepare`'s overlap pass (lines 207–242) is six levels deep. It does five things at once: the collinear test, the overlap span, the union, the covered stretches and the carried corners.
- **The stranger's mistake:** around line 227 they lose which edge is `self`, and so break the "same bits on both sides" promise of lines 233–234.
- **Fix:** move "join two overlapping edges" into its own function.
- **Cost:** one edit. Searched: `prepare\(` in docs; nothing.
- Confirms Sway.cpp:163:prepare.

[Q2] LOW `src/ubake/ShadowMask.cpp:438` — `if (order.empty() || (height != 0 && height <= atlasLimit)) break;`
- **What goes wrong:** the coarsening loop (lines 393–441) holds the bake, the packing and two nested exits.
- **Where the stranger gets stuck:** reading why "nothing to pack" leaves the loop, and why line 446 then forces a height of 4.
- **Fix:** move the per-texel-size bake-and-pack into its own function, and comment the empty-atlas case.
- **Cost:** pinned. UTA-0326 (around line 162) names `bakeShadowMask`; its name survives. Searched: docs.
- Confirms ShadowMask.cpp:334:bakeShadowMask.

[Q3] LOW `src/ubake/LightProbes.cpp:166` — `... count_if(all, [&](const Vec3& w) { return sunSeen(p, w, rays); });`
- **What goes wrong:** `sunSeen` actually means "the first surface this ray meets is sky", and `skySeenFrom` uses it for the sky.
- **The stranger's mistake:** they add a sun-only rule to `sunSeen` (say, the disc) and change sky-seen haze without knowing.
- **Fix:** rename it to something like `meetsSkyFirst`.
- **Cost:** pinned. UTA-0338 (around line 123), UTA-0350 (around line 41) and UTA-0387 (around line 45) name `sunSeen`. Searched: docs.

[Q3] LOW `src/ubake/LightProbes.cpp:587` — `const std::size_t batches = (out.probes.size() + PROBES_PER_JOB - 1) / PROBES_PER_JOB;`
- **What goes wrong:** `batches` holds the number of jobs. The same name appears in `ShadowMask.cpp:406` and `Occlusion.cpp:159`, a few lines after loops over `geometry.batches`, which are draw batches.
- **The stranger's mistake:** reading it as the draw-batch count.
- **Fix:** rename it to `jobCount`.
- **Cost:** unsure. It is a local name, so no spec should name it, but I did not search docs for it.

[Q4] LOW `src/ubake/Geometry.cpp:222` — `/// One drawn node, before it is batched.`
- **What goes wrong:** this doc line belongs to `Drawn` (line 248) but sits above `Mapping`, stacked on `Mapping`'s own line. Both lines attach to `Mapping`.
- **The stranger's mistake:** reading `Mapping` as the drawn node.
- **Fix:** move the line onto `Drawn`.
- **Cost:** comment only.

[Q4] LOW `src/ubake/SurfaceRays.cpp:89` — `// The 432 headers' Engine/Inc/UnObj.h. UTA-0168: PF_NotSolid measured ...`
- **What goes wrong:** this describes `LIGHT_PASSES_FLAGS` in `SurfaceRays.h:28`, but sits above a constructor in the .cpp.
- **The stranger's mistake:** looking here for the flag values, or deleting the comment as stray.
- **Fix:** move it to `SurfaceRays.h:28`.
- **Cost:** comment only.

[Q4] LOW `src/ubake/Geometry.cpp:383` — `// 4. Its polygon, and its surface's point and vectors.`
- **What goes wrong:** the checks on the surface's point and vectors (`pBase`, `vNormal`, `vTextureU`, `vTextureV`) are in `draw` (lines 277–284), not here.
- **The stranger's mistake:** looking here for the `pBase` refusal.
- **Fix:** say step 4's surface half is in `draw`.
- **Cost:** comment only.

[Q4] LOW `src/ubake/ShadowMask.cpp:98` — `/// weighed by UT99's falloff from it, starting a quarter `offset` of a step`
- **What goes wrong:** the code (line 108) starts at `(offset + 0.5) / 4` of a step. That is an eighth further in than the comment says.
- **The stranger's mistake:** "fixing" the code to match the comment shifts every strip shadow.
- **Fix:** state the extra half-quarter in the comment.
- **Cost:** comment only.

[Q4] LOW `src/ubake/LightProbes.h:204` — `/// Where `lights` holds a sun, each probe's `sunSeen` too`
- **What goes wrong:** the doc leaves out `skySeen` (filled when the level has sky) and `reach` (always filled), both of which `bakeLightProbes` writes (lines 582–583).
- **The stranger's mistake:** assuming those fields come from somewhere else.
- **Fix:** list all three.
- **Cost:** comment only.

[Q5] LOW `src/ubake/LightModel.cpp:209` — `if (light.effect != LE_NON_INCIDENCE) incidence = std::max(0.0, dot(n, l));`
- **What goes wrong:** this test can never be false. `LE_NON_INCIDENCE` already returned at line 199.
- **The stranger's mistake:** believing non-incidence lights reach the spot and falloff path.
- **Fix:** drop the test.
- **Cost:** one edit, nothing renamed.

[Q5] LOW `src/ubake/ShadowMask.cpp:139` — `keys[0];`
- **What goes wrong:** a bare map access, there to seed the base position. Nothing at the line says so.
- **The stranger's mistake:** deleting it as a no-op, which drops the mover's own position from its reach box.
- **Fix:** write `try_emplace(0)` with a short "the base position" comment.
- **Cost:** one edit.

[Q5] LOW `src/ubake/Sway.cpp:347` — `if (estimated > 8.0 * static_cast<double>(SWAY_VERTEX_BUDGET)) {`
- **What goes wrong:** the "many times over" factor (8) has no name or reason beside it.
- **Where the stranger gets stuck:** they cannot tell whether 8 is safe to change.
- **Fix:** a named constant with its reason.
- **Cost:** one edit.

## Candidates dismissed
- **src/ubake/Geometry.cpp:145:hiddenBehindPortals** — 76 lines, but each block is commented and runs in order (chains, portals, cover test). A stranger can follow it.
- **src/ubake/Collision.cpp:61:buildCollision** — a flat sequence of checks, each with its own message; nothing loses the reader.
- **src/ubake/Charts.cpp:78:census_number** ("3 units") — a measured fact about one named map, given as the reason for the code. It does not go stale with the code.
- **src/ubake/Geometry.h:67:census_number** ("18 units") — the same: TheBoat's bow, measured, justifying the 64.
- **src/ubake/LightModel.cpp:22, LightProbes.cpp:21, SurfaceRays.cpp:89:census_number** ("432 headers") — 432 is the UT release whose headers are cited, not a count. Line 89 is a finding anyway, for being in the wrong place.
- **src/ubake/LightModel.cpp:193:census_number** — DM-Deck16]['s measured surface lists, given as evidence; a fixed fact about one map.
- **src/ubake/LightProbes.cpp:246:census_number** ("8 rejects") — "SS 8" is a spec section number.
- **src/ubake/LightProbes.h:38 and :40:census_number** — a dated measurement given as the margin's reason; it does not track the code.
- **src/ubake/LightProbes.h:61:census_number** ("24 units") — this is "2^24", a constant's value.
- **src/ubake/LightProbes.h:193:census_number** ("6 sends") — "step 6", a spec step number.

## Correctness leads
- `src/ubake/LightProbes.cpp:613` — the bounce passes' `lit` gets no `reach`, so § 4.13's mask is never applied in the bake, against spec item 2 ("so does the bake's own copy of the blend"). Searched: `\.reach\b` in the tree; the spec's § 4.13.
- `src/ubake/Geometry.cpp:407` — a cover's points skip UTA-0217's finite-and-in-bounds check, which node points get at lines 397–402. `Hide.cpp:120-162` builds the hull from points of the brush's own nodes, and those nodes may be invisible, so never checked. Searched: `Hide.cpp` lines 120–162 only. I did not read how `cornersOf` checks points.

## Not checked
- `Hide.cpp`'s `cornersOf`, and whether it bounds-checks points (this bears on the second lead).
- Whether `ut-ants-uta0156/fgethsv.txt`, cited at `LightModel.cpp:15`, exists. The spec cites the same path, and no file of that name turned up in the tree search; it may be outside the repository.
- Whether `SwayReport` leaving out `SwaySlicing::clothSlice` is deliberate. Not searched, so not filed.
- The repeated small maths helpers (`cross`, `dot`, `minus`, `positionOf`), copied across `Charts`, `Geometry`, `Sway`, `SurfaceRays` and `LightProbes`. Identical copies, and I could name no edit a stranger would get wrong, so I dropped them as taste.
