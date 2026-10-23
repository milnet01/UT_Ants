## Files read
- `src/urender/shaders/bloom.frag`: 68 / 68
- `cluster.comp`: 117 / 117
- `clusters.glsl`: 20 / 20
- `flame.frag`: 37 / 37
- `flame.glsl`: 98 / 98
- `flame.vert`: 34 / 34
- `fog.glsl`: 137 / 137
- `fog_integrate.comp`: 43 / 43
- `fog_scatter.comp`: 131 / 131
- `fsr_easu.frag`: 37 / 37
- `fsr_rcas.frag`: 31 / 31
- `light.glsl`: 190 / 190
- `liquid.glsl`: 173 / 173
- `mote.frag`: 31 / 31
- `mote.vert`: 49 / 49
- `post.frag`: 116 / 116
- `post.vert`: 8 / 8
- `probes.glsl`: 153 / 153
- `scene.frag`: 655 / 655
- `scene.vert`: 66 / 66
- `scene_bindings.glsl`: 68 / 68
- `shadow.frag`: 19 / 19
- `shadow.vert`: 20 / 20
- `shadow_reduce.comp`: 33 / 33
- `shadows.glsl`: 142 / 142
- `sky.glsl`: 43 / 43
- `sway.glsl`: 99 / 99
- `types.glsl`: 220 / 220
- `variation.glsl`: 67 / 67
- `water.glsl`: 119 / 119

## What I started with
- The global `~/.claude/CLAUDE.md`, in full.
- The `review-lane` agent instructions.
- A SessionStart hook message saying the Ants MCP is connected.
- A userEmail context block.
- No project `CLAUDE.md`, no memory index and no git snapshot.
- The shared context file (`shared-context.md`), read in STEP 1.

## Summary
These files are small programs that run on the graphics card and decide what colour each pixel on screen is. One large program, `scene.frag`, lights every surface. It adds the sun, the sky, fog, water reflections, flames and shadows, and it is tuned to match the original Unreal Tournament's look. Other files fill in the fog volume, sort lights into screen regions, blur bright spots into glow, and scale and sharpen the final image. Many shared pieces (light, fog, probes, water) live in "include" files that several programs pull in.

## Findings

[Q1] HIGH `probes.glsl:79` — `vec3 indirectAt(ProbeLattice lattice, vec3 x, vec3 n) {` — The eight-corner probe blend appears twice: here for colour, and in `probeShareAt` (`probes.glsl:115-138`) for one number. A past change had to patch both: UTA-0112 § 4.13 item 2 says "`probes.glsl`'s `indirectAt` and `probeShareAt` apply it". `probeShareAt`'s comment points back at `indirectAt`, but nothing in `indirectAt` points forward. A stranger changing the corner weighting or the reach fallback in `indirectAt` would leave the sun share and the sky openness on the old rule. — Fix: one shared corner-weight walk that both functions call, or at least a comment in `indirectAt` naming its twin.
Cost: pinned — UTA-0112 § 4.13 item 2 names `indirectAt` and `probeShareAt`.
Confirms dup:f6dcc0a89e98. Searched: docs for `indirectAt|probeShareAt`.

[Q1] MEDIUM `scene.frag:480` — `: waterProbeReflection(worldPosition + surface * (0.5 * float(frame.probeSpacing)), r);` — Three places step half a probe spacing off the surface: lines 480, 574 and 647. They have diverged. Line 574 steps along `litSurface`, which UTA-0215 mirrors for a liquid seen from behind (lines 501-505). Lines 480 and 647 step along `surface`. No comment says why the reflection and the haze read the probes on the other side. UTA-0089 § 4.2 says the reflection uses "the probe point `scene.frag` already uses". A stranger would not know which of the three is right. — Fix: compute the probe point once and use it at all three sites, or comment why reflection and haze differ.
Cost: one edit — Searched docs for `litSurface`: nothing. (UTA-0089 § 4.2 describes the intended behaviour; it does not name the code.)

[Q2] MEDIUM `scene.frag:495` — `} else {` — `main` runs 298 lines. A stranger loses the thread at this lit branch. It runs about 120 lines and covers two light-gathering strategies (the chart pairs with a subgroup loop, and the clusters), the sun, the probes, openness, the display power, the debug light terms and caustics. The nesting is six deep at lines 537-541. Working out what `direct` holds when it reaches line 603 means holding all of that at once. — Fix: extract the chart and cluster gathering into named functions that return `direct`.
Cost: one edit — Searched docs for `scene.frag's main`, `chartIndex` and `subgroupBroadcastFirst`. Only a research note mentions the pattern; no spec names this code.
Confirms src/urender/shaders/scene.frag:358:main.

[Q4] MEDIUM `scene.frag:524` — `for (;;) {` / `if (chartIndex != maskChart) continue;` — The comment says what this achieves ("one chart at a time across the subgroup"). It does not say how the loop ends. The loop finishes only because lanes that `break` drop out, and `subgroupBroadcastFirst` then picks the next remaining chart. A stranger would read `for (;;) … continue` as a possible endless loop. They might "fix" it, or put work after the `break` that breaks the pattern. — Fix: one line naming it a waterfall loop, saying why it terminates.
Cost: one edit (comment only).

[Q4] MEDIUM `fog.glsl:108` — `// Where view depth `z` sits along the slices, 0 at FOG_NEAR and 1 at FOG_FAR.` — This describes `fogCoordinate`, but the UTA-0215 water block (lines 109-124) now sits between it and `fogCoordinate` at line 126. So the comment heads the water constants, and `fogCoordinate` has none. A stranger would misread what `WATER_FOG_START` is, or not find the meaning of `fogCoordinate`. — Fix: move the line back to sit on `fogCoordinate`.
Cost: one edit (comment only).

[Q4] MEDIUM `fog.glsl:19-90` — `// Refitted by UTA-0166 …` / `// Refitted by UTA-0165 …` / `… today's 16.8/33.8.` — About 70 lines of superseded refits sit above three constants. They quote values and exposures that no longer apply (6.0e-3, EXPOSURE 2.2, 5.5, 5.4). The rule in force for `HAZE_SCATTER` is at lines 45-47, after four older refits. For the glow and fog scales it is at lines 84-87. "today's" (line 77) has no date. A stranger refitting has to dig the current rule out of the history. — Fix: keep the current rule and the latest sweep beside each constant, and move the refit history to a history file.
Cost: one edit (comment only).

[Q1] MEDIUM `scene.frag:352` — `vec3 displayed = mix(colour * 12.92, 1.055 * pow(colour, vec3(1.0 / 2.4)) - 0.055,` — The sRGB conversion is written out by hand several times:
- Encode: `scene.frag:352` and `:443`, and `post.frag:82` (`encodeSrgb`).
- Decode: `scene.frag:355`, `fog.glsl:100` (`displayedToLinear`) and `post.frag:83` (`decodeSrgb`).

`scene.frag` already includes `fog.glsl`, so it could call `displayedToLinear` instead of its own copy. A stranger correcting a threshold or the handling of negative values would patch one of three. — Fix: put an encode beside `displayedToLinear` in a shared include and call it everywhere.
Cost: one edit if `displayedToLinear` stays in `fog.glsl`. If it moves, pinned: UTA-0015 § 4.3 says "`displayedToLinear` is the sRGB decode, in `fog.glsl`". Searched docs for `displayedToLinear|encodeSrgb`.

[Q5] MEDIUM `cluster.comp:28` — `shared uint chosenLists[CLUSTER_CAPACITY][32];` — The workgroup width 32 is a bare literal in three places: lines 22, 28 and 29. They are tied together only by the comment at line 26. A stranger raising `local_size_x` to 64 would index past the shared arrays through `gl_LocalInvocationIndex`, and the 16 KiB claim would no longer hold. — Fix: one named constant used for `local_size_x` and both array widths.
Cost: one edit — Searched docs for `chosenLists|reachLists`: nothing.
Confirms src/urender/shaders/cluster.comp:26:census_number (the comment is true today; the risk is the repeated literal).

[Q3] LOW `cluster.comp:31` — `#define reach(k) reachLists[k][gl_LocalInvocationIndex]` — It holds each light's `key` (brightness times falloff at the box), not a reach. In this codebase "reach" means a distance (probe reach, a light's radius). A stranger reading `key <= reach(...)` (line 102) would assume a distance comparison. — Fix: rename to something like `strength` / `strengthLists`.
Cost: one edit — Searched docs for `reachLists`: nothing.

[Q3] LOW `liquid.glsl:154` — `float step = min(liquid.frequency, 256.0 - liquid.frequency) / 128.0;` — Line 150 declares `const float STEP = 0.1` (the finite-difference spacing). Four lines later `step` means a drop's phase step per tick, and it hides the built-in `step()`. A stranger would assume the two are related, or find `step()` mysteriously unusable later in the function. — Fix: rename to `dropStep` (or similar).
Cost: one edit — Searched docs for `\bSTEP\b`: nothing.

[Q4] LOW `liquid.glsl:66` — `// NOT FITTED. DOM-MetalDream's XbpFX.blueplasma changes 6.98 a frame …` — This paragraph sits directly on top of `ICE_PAN_PER_UNIT`. The next paragraph says that same constant was measured. A stranger would read the "NOT FITTED" label as applying to the constant below it. — Fix: say what the NOT FITTED note covers (the Ice look's churn), or move it off the constant.
Cost: one edit (comment only).

[Q4] LOW `liquid.glsl:28` — `// SS 4.6's constants. The four LIQUID_ ones are fitted, …` — There are now five tuning constants starting `LIQUID_`. `LIQUID_FAST_CALM` was fitted separately by UTA-0319, and `LIQUID_DRIFT` was refitted by UTA-0312. A stranger counting would not know which four are meant. — Fix: name the four.
Cost: one edit (comment only).

[Q4] LOW `fog.glsl:3` — `// Fog.h states the grid too. The four look constants live only here,` — The file has five: `FOG_ANISOTROPY`, `HAZE_EXTINCTION`, `HAZE_SCATTER`, `VOLUME_GLOW_SCALE` and `VOLUME_FOG_SCALE`. The water fade constants come on top of those. — Fix: drop the count, or name them.
Cost: one edit (comment only).

[Q4] LOW `light.glsl:143` — `if (light.effect != LE_NON_INCIDENCE) incidence = max(0.0, dot(n, l));` — An `LE_NON_INCIDENCE` light already returned at line 133, so this test is always true. A stranger would think non-incidence lights reach this code and would edit around a case that cannot happen. — Fix: drop the condition.
Cost: one edit (local, no name changes).

[Q4] LOW `types.glsl:201` — `// UTA-0156 SS 4.4: one zone's ambient light, as UT99's bytes.` — UTA-0292 withdrew zone ambient (`scene.frag:585`). No shader reads `brightness`, `hue` or `saturation` from a zone; what is read is `.sky` and `.water`. A stranger would think zone ambient is still applied somewhere. — Fix: describe the struct by what it is used for now.
Cost: one edit (comment only). Searched: `\]\.(brightness|hue|saturation)\b` over every shader file in the tree, tests included; the only match is `lights[i].brightness` in `cluster.comp`.

[Q1] LOW `scene.frag:307` — `float bearing = float(step) * (6.28318531 / float(HAZE_RING_STEPS));` — 2π is spelled out in five places:
- `light.glsl:24` (`TWO_PI`)
- `sway.glsl:20` (`SWAY_TWO_PI`)
- `scene.frag:307` and `:318` (literals)
- `mote.vert:36` (`6.2832`)

The precision differs between them. `scene.frag` already includes `light.glsl`, so it has `TWO_PI`. This is low risk, but a stranger searching for `TWO_PI` misses the literals. — Fix: use `TWO_PI` in `scene.frag`.
Cost: one edit — Searched docs for `\bTWO_PI\b|SWAY_TWO_PI`: nothing.

[Q1] LOW `scene.frag:567` — `ProbeLattice lattice = ProbeLattice(frame.probeSpacing, frame.probeCount, frame.probeTableMask, frame.probeLongestRun);` — Built from `frame` three times: `scene.frag:567`, `scene.frag:645` and `water.glsl:64`. A new lattice field means three edits. The compiler catches a change in the number of arguments, but not two fields swapped. — Fix: one `frameProbeLattice()` helper.
Cost: one edit — Searched docs for `ProbeLattice`: nothing.

[Q5] LOW `scene.frag:105` — `return 1.0 - mix(a, b, smoothstep(0.2, 0.8, tile.along));` — The 0.2 and 0.8 edges are copied from `tileWeight` (`variation.glsl:64`). The comment explains why the contrast term is dropped, but not that the curve edges must match. A stranger retuning `tileWeight` would leave the parallax heights on the old blend. — Fix: name the two edges in `variation.glsl` and use them in both places.
Cost: one edit — Searched docs for `tileWeight|depthAt`: UTA-0277 names `tileWeight`, which this fix does not rename.

[Q5] LOW `scene.frag:391` — `&& (draw.polyFlags & (PF_MASKED | PF_FAKE_BACKDROP)) == 0u;` — Masked and sky-backdrop surfaces are excluded from both tile shuffling and parallax (also at line 394), with no reason given. The sky-flat and liquid exclusions both have comments. A stranger enabling parallax on a masked material would not know whether the exclusion protects the cutout. — Fix: one line giving the reason.
Cost: one edit (comment only).

[Q1] LOW `fsr_easu.frag:10` — `layout(push_constant) uniform PostBlock {` — This is a second, truncated copy of `post.frag`'s push-constant block. The C++ `PostConstants` (`ShaderTypes.h`) asserts the field offsets, but nothing ties this copy to `post.frag`'s. A stranger reordering the start of `PostBlock` in `post.frag` and `ShaderTypes.h` would miss this one. — Fix: a comment naming `ShaderTypes.h`'s `PostConstants`, or a shared include.
Cost: one edit — Searched src for `upscaleInput|regionSize`.

[Q4] LOW `fog_scatter.comp:47` — `// the atlas -- the nearest depth of the 4 x 4 atlas texels it covers -- kept` — 4 is `HAZE_SHADOW_STEP`. That constant lives in `shadow_reduce.comp:22` and `Frame.cpp:65`, not in this file. The comment goes stale silently if the step changes. — Fix: name `HAZE_SHADOW_STEP` instead of the number.
Cost: one edit (comment only). Confirms src/urender/shaders/fog_scatter.comp:47:census_number. Searched src for `HAZE_SHADOW_STEP`.

[Q4] LOW `clusters.glsl:9` — `// The same slicing Clusters.cpp builds the boxes with: 16 tiles across, 8 down,` — True today: `ShaderTypes.h:44` has `CLUSTER_GRID{16, 8, 24}`. But the shader reads `frame.clusterGrid`, so these numbers are a copy of a C++ value that can go stale. — Fix: name `gpu::CLUSTER_GRID` rather than its values.
Cost: one edit (comment only). Confirms the candidates for `clusters.glsl:9` and `:10`. Searched src for `CLUSTER_GRID`.

## Candidates dismissed
- flame.glsl:12, flame.glsl:37 — fitting-record measurements dated 2026-10-01, not counts of anything in the code.
- fog.glsl:41 — "baker revision 27 bakes" is a historical record (it falls inside the fog.glsl:19-90 history finding above).
- fog.glsl:89 — "1024 units" is a measured distance band.
- light.glsl:16 — "432" is UT99's 432 release headers, not a count.
- liquid.glsl:30, :32, :48, :72 — capture settings and measurements in fitting records.
- post.frag:18 — "16 bytes" is true: the C++ `PostConstants` needs `reserved` to put `flashFog` at offset 32, and `ShaderTypes.h:473` asserts it (read).
- scene.frag:298 — sin 8° ≈ 0.14 and sin 20° ≈ 0.34; correct.
- scene.frag:332 — a measured pose range.
- scene.frag:344 — 128 is UT99's mid-grey, so a factor of 1; correct.
- scene.frag:454 — a historical VGPR measurement tied to UTA-0352.
- shadow_reduce.comp:4 — sits beside `HAZE_SHADOW_STEP = 4` in the same file.
- sway.glsl:30 — the wavelengths match the array, and 2458/4096 ≈ 0.6, 6963/4096 ≈ 1.7; correct.
- sway.glsl:40, :41 — the wavelengths match; 600 units at g = 515 gives ≈ 1514 cycles per wrap; correct.
- water.glsl:27, :28 — measurement poses.
- water.glsl:41 — "SS 3 says", a section reference, not a count.

## Correctness leads
- `scene.frag:480` — For a liquid seen from behind, the water reflection reads the probes on the viewer's side (`surface`), while its lighting reads them on the front (`litSurface`, line 574). UTA-0089 § 4.2 says the reflection uses the same probe point as the lighting. — Searched: UTA-0089 § 4.2 (read), and docs for `litSurface`.
- `light.glsl:143` — The `LE_NON_INCIDENCE` test can never be false here; harmless, but dead code. — Searched: none (the early return is in the same function).

## Not checked
- `scene.vert:43` (`16.0 * float(inSway.w)`): I did not search the C++ packer to confirm that a sway cell is stored in 16-unit steps, so I filed no finding on the unexplained 16.
- `ffx_a.h` and `ffx_fsr1.h` are AMD's FSR 1 headers, included but outside my file list; not read.
- The C++ copies of the probe blend (UTA-0112 § 4.7 step 7) and of the shared constants (`Fog.h`, `LightProbes.h`): not read, beyond the `ShaderTypes.h` and `Frame.cpp` lines named above.
- I could not compile, so whether a local named `step` hides `step()` cleanly under glslang is assumed from the GLSL scoping rules, not tested.
