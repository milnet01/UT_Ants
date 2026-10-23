## Files read
Counts are the last line number `Read` showed. For each file that ends in a newline, `Read` numbers one more line than `wc -l` reports.
- src/urender/Frame.cpp — 1–2117 / 2117
- src/urender/Clusters.cpp — 1–66 / 66
- src/urender/Clusters.h — 1–35 / 35
- src/urender/Fog.cpp — 1–60 / 60
- src/urender/Fog.h — 1–56 / 56
- src/urender/Lights.cpp — 1–152 / 152
- src/urender/Lights.h — 1–61 / 61
- src/urender/Placement.cpp — 1–122 / 122
- src/urender/Placement.h — 1–56 / 56
- src/urender/Probes.cpp — 1–58 / 58
- src/urender/Probes.h — 1–48 / 48
- src/urender/Renderer.h — 1–237 / 237
- src/urender/ShaderTypes.h — 1–532 / 532
- src/urender/Sky.cpp — 1–144 / 144
- src/urender/Sky.h — 1–88 / 88
- src/urender/Tiers.cpp — 1–62 / 62
- src/urender/Tiers.h — 1–139 / 139
- src/urender/Zones.cpp — 1–47 / 47
- src/urender/Zones.h — 1–44 / 44

## What I started with
- This agent's system prompt (review-lane).
- The global `~/.claude/CLAUDE.md`.
- A user-email context note.
- A SessionStart hook message saying Ants MCP is connected.
- The shared-context file named in STEP 1.

I had no project `CLAUDE.md`, no memory index and no git snapshot.

## Summary
This code draws each frame of a Unreal Tournament level that has already been converted ("baked") for this engine.

For every frame it works out where the camera is, and which lights are on and how bright they are. It decides which shadows need redrawing and where the fog glows. It then sends the graphics card a list of steps in order: shadows, sorting lights into screen regions ("clusters"), fog, the solid surfaces, see-through surfaces and flames, glow ("bloom"), and the final colour adjustment.

When a new level loads, it also takes pictures of the level's sky once, from six directions. It picks a quality tier from the graphics card, which turns costly effects off on weaker machines. It can lower the drawing resolution to keep the game running at 60 frames a second.

## Findings

[Q1] HIGH `src/urender/Frame.cpp:1801` — `frame.occlusionTexture = impl.occlusion.view() ? static_cast<std::uint32_t>(impl.materials->textures().size() + (impl.sky.view() ? 1 : 0))` — **What goes wrong:** where each extra texture sits in the shader's texture array is worked out by hand at six sites: upload `:818-819`, writeDescriptors `:933-935`, the push order at `:1077-1085`, and drawView `:1778`, `:1801-1803`, `:1805-1808` and `:1871-1873`. They even use different tests: `upload` asks the bundle (`skyView`, `occluded`, `bundle.shadowMask`); the others ask the images (`.view()`). And `firstLight` at `:1871` adds `+ 1` for the sky without testing for it, which is only right because it sits inside `if (impl.skyView)`. Someone adding a texture to the array, or reordering one, will miss a site, and a shader then samples the wrong texture without any error. Nothing at any one site lists the others. — **Fix:** one function that returns each texture's slot index and the total, called from all six sites.
Cost: one edit — Searched: docs for `occlusionTexture|shadowMaskTexture|writeDescriptors|drawView`. Specs name the `FrameData` fields (UTA-0164, UTA-0326) and `drawView` (UTA-0089), but this fix renames none of them.

[Q2] MEDIUM `src/urender/Frame.cpp:1340` — `if (skyCapturing && item.objectIndex == 0) {` — **Where they get stuck:** `recordFrame` runs about 440 lines and holds every stage of the frame. Its `drawBatches` lambda, nested in the middle, mixes three filters in nine lines (`:1340-1348`): sky capture, layer capture with exceptions for the depth pass, and blended versus opaque. A stranger changing which batches a pass draws has to untangle all three. It also takes knowing that object 0 is the level, which is only implied at `:1366`. — **Fix:** one function per stage, named after the header's steps, with the sky-capture filter as its own named rule. Candidate `Frame.cpp:1141 recordFrame` confirmed.
Cost: pinned — UTA-0015 (`recordFrame` gains a step between step 0 … and step …) and UTA-0326 (`recordFrame`'s shadow pass) name the function and describe its stages.

[Q2] MEDIUM `src/urender/Frame.cpp:1926` — `if (frameLights.size() > impl.lightCapacity || plan.faces.size() + 6 > impl.shadowFaceCapacity)` — **Where they get stuck:** `drawView` runs about 325 lines and does eight jobs: choosing the scale, filling `FrameData`, mover models, the shadow plan, the fog constants, the size check, submitting the GPU work with the sky-face copy, and the stats and scale controller. The size check comes 180 lines after the first writes to GPU-shared memory (`:1769`, `:1818`). A stranger would have to work out that it guards only the light and shadow-face buffers. Someone adding a new per-frame upload above it would not know whether it is covered. — **Fix:** split out `FrameData` building, fog constants and stats into separate functions, with the check beside the writes it guards. Candidate `Frame.cpp:1708 drawView` confirmed.
Cost: pinned — UTA-0089 names `drawView`.

[Q4] MEDIUM `src/urender/Frame.cpp:4` — `// THE FRAME, IN ORDER:` — **What goes wrong:** the header lists step 1 (forward pass) before step 0.5 (fog). The code runs fog first (`:1255`, then `:1291`), and the forward pass reads the fog's result. The header also leaves out the shadow tiles (`:1159`), the depth-only pre-pass (`:1386`) and the flames (2.5, `:1427`). A stranger placing a new pass by the header would put it in the wrong place. — **Fix:** list the stages in the order `recordFrame` runs them, shadows and flames included.
Cost: one edit — comment only.

[Q5] MEDIUM `src/urender/Frame.cpp:1053` — `hazeShadowWrite.dstBinding = 3;` — **Where they get stuck:** the fog set's binding numbers (0–4) are bare numbers in at least four places:
- `Pipelines.cpp:660-666`
- Frame.cpp `:1043` (taken from a loop index), `:1053`, `:1061`, `:1069`, written in the order 0, 1, 3, 4, 2
- `fog_scatter.comp:18-23`, `fog_integrate.comp:14-15`, `shadow_reduce.comp:12`

The scene set has a named `gpu::Binding` enum with the note that "shaders/types.glsl numbers them the same". The fog set has nothing like it. A stranger adding a fog binding has to find every numbering by searching. — **Fix:** a named enum for the fog set beside `gpu::Binding`, used by Pipelines.cpp and Frame.cpp. Searched: `set\s*=\s*1\s*,\s*binding` repo-wide; `fogSetLayout|FOG_SET|fogBindings` repo-wide.
Cost: one edit — Searched: docs for `fogSetLayout|FOG_SET|fogBindings`: no match.

[Q1] MEDIUM `src/urender/ShaderTypes.h:45` — `inline constexpr std::uint32_t CLUSTER_COUNT = 16u * 8u * 24u;` — **What goes wrong:** the grid's size is written out again rather than worked out from `CLUSTER_GRID` on the line above. The same numbers appear in `Clusters.h:3` ("16 tiles across, 8 down, and 24 slices") and `Clusters.h:22` ("x + y * 16 + z * 16 * 8"). Someone tuning `CLUSTER_GRID` alone would make `clusterGrid` (Clusters.cpp:46) write past `bounds`, which is sized by `CLUSTER_COUNT` at Clusters.cpp:21. The two Clusters.h comments would also go stale. — **Fix:** compute `CLUSTER_COUNT` from `CLUSTER_GRID`, and write the Clusters.h index formula in terms of tilesX and tilesY. Confirms candidate `Clusters.h:3` (accurate today, but a copy). Searched: `CLUSTER_COUNT|CLUSTER_GRID` repo-wide; `tests/unit/RenderClusterTest.cpp:27` also holds the literal (tests not judged).
Cost: one edit — Searched: docs for `CLUSTER_COUNT`: only a ROADMAP line, no spec.

[Q4] MEDIUM `src/urender/ShaderTypes.h:4` — `Every struct's size and every field's offset is asserted below` — **What goes wrong:** this is false. The following are not asserted:
- `Zone::water`
- `FrameData::waterFog`
- `Liquid::kind`, `panning`, `amplitude` and `frequency`
- `SkyLayer::plane`, `u`, `v`, `light`, `kind` and `pad`
- `DrawConstants::reserved`
- `FlameInstance::reserved`

A stranger trusting the line would think swapping two same-sized fields is caught at compile time. It is not, and the header itself says a wrong read on a GPU means losing the device. — **Fix:** assert the missing offsets, or narrow the claim to what is asserted. Searched: `offsetof\(Zone|offsetof\(FrameData, waterFog|offsetof\(Liquid, kind`.
Cost: one edit — comment, or added asserts only.

[Q4] LOW `src/urender/Lights.cpp:39` — `/// eased between neighbours. A time past i64's range holds at step 0, as LT_FLICKER's does (UTA-0217).` — **What goes wrong:** `smoothNoise` stops at `0x1p62` (`:44`) while `flickerOf` stops at `0x1p63` (`:66`). The comment says the two behave the same, but they are a copy that has drifted apart. A stranger fixing the guard in one would trust the comment and miss the other. — **Fix:** make the two limits match, or state why `smoothNoise` stops a factor of two sooner. Searched: `0x1p6` repo-wide: only these two sites.
Cost: one edit — Searched: `0x1p6` repo-wide.

[Q4] LOW `src/urender/Tiers.cpp:41` — `// Half a GiB below each figure the user gave, since a card reports a little` — **Where they get stuck:** the comment points at a conversation that is not in the code. The intended sizes (2, 4 and 8 GiB) have to be worked out backwards from `3/2`, `7/2` and `15/2`. Tiers.h:123 does point at "SS 4.3's table". — **Fix:** name the 2, 4 and 8 GiB figures and SS 4.3.
Cost: one edit — comment only.

[Q3] LOW `src/urender/ShaderTypes.h:459` — `std::array<float, 4> flashFog;` — **What goes wrong:** a stranger would assume this holds UT99's ViewFlash. It holds the zone's ViewFog (Frame.cpp:1556-1559). The comment on the line above says so. — **Fix:** rename it to say ViewFog.
Cost: pinned — UTA-0215 spec (`PostConstants` gains `wobbleSeconds` and `flashFog`), and the post shader reads it by name.

[Q3] LOW `src/urender/Frame.cpp:2053` — `const std::size_t halvesPerPixel = target == Target::Emission ? 4 : 2;` — **What goes wrong:** the name says "16-bit halves", but it is 2 for the RGBA8 colour target, which holds none. It is really "16-bit units per pixel". The comment at `:2052` explains this. — **Fix:** rename it to what it counts, such as two-byte units per pixel.
Cost: one edit — local variable. Searched: none needed, the name is local to `readback`.

## Candidates dismissed
- `Frame.cpp:925 writeDescriptors` — the length is repeated Vulkan setup that a reader can follow. The pool-size sums at `:938-949` each carry a comment listing their parts, and I checked those against the writes. Its real problem is the bare fog bindings, reported above.
- `Frame.cpp:207 shapeOf` — a flat list, one commented block per bundle section; length alone. Specs pin it as the place each section is sampled (UTA-0164, 0263, 0277, 0286, 0326, 0338, 0387). Its gap is under Correctness leads.
- `Frame.cpp:806 upload` — a straight sequence with each step commented. The six-deep nesting is a small loop that builds a box around each mover.
- `Clusters.h:3` — matches `CLUSTER_GRID` today; folded into the CLUSTER_COUNT finding above.
- `Fog.h:31` "500 units" — a derived angle (cone 18 spans 200 units at 500), not a count that can go stale.
- `Frame.cpp:138` "650 units" — checks out: about 0.0075 × 86,400 ≈ 648. A float step near 650 is about 6.1e-5, against 1.25e-4 a frame at 60 fps.
- `Frame.cpp:1956`, `Sky.cpp:24`, `Sky.h:50` "90 degrees" — the code makes the shorter side span 90° (2·atan(height/side)), which matches.
- `Placement.cpp:98` "2 multiplies" — "Column 2" is a matrix column index, and the claim is right (`p[2][3] = 1`).
- `Placement.h:34` "2 across" — Halton base 2 across, matching `radical(index, 2)`.
- `Placement.h:49` "6 draws" — "SS 6" is a spec section, not a count.
- `Renderer.h:73`, `Tiers.h:27` "60 frames" — match `1000.0 / 60.0`.
- `Renderer.h:170` "6 counts" — "SS 6" is a spec section.
- `ShaderTypes.h:419` "8 bytes" — right: without `reserved`, `panOffset` would sit at 12, which is not 8-aligned. The assert holds it at 16.
- `ShaderTypes.h:455` "16 bytes" — `flashFog` sits at 32 (asserted). Dropping `reserved` would move it to 28.
- `Tiers.h:72` "12 cores" — a dated measurement record, not a count of the code.
- `Tiers.h:112` "64 units" — matches `SHADOW_ATLAS_SIZE = 4096` and `SHADOW_UNITS_PER_TEXEL = 64.0` (Shadows.h:36, 44).

## Correctness leads
`src/urender/Frame.cpp:207` — `shapeOf` still does not sample `bundle.rooms`. Yet `upload` reads it (`:859` each light's zone via `zoneAt`, and `skyLayersOf`, Sky.cpp:79). A bundle that differs only in its rooms keeps the old light zones and sky layers. The 2026-10-15 review (lane-04-urender-frame.md:40, :52) asked for "the ends of `bundle.rooms`" to be sampled alongside the lights; the lights were added, the rooms were not. — Searched: docs for `shapeOf|BundleShape`.

## Not checked
- I did not open `MaterialSet::upload` beyond one search of the fields it reads from each record. Those fields (`id`, `metallic`, `parallaxDepth`, `tileKind`, `flame`, `liquid`, `fire`) match what `shapeOf` samples, but I did not read the function in full.
- Whether the shaders' texture-array indexing agrees with the six hand-kept sites. I read no shader source beyond the binding-search rows.
- Whether object index 0 is documented as "the level" in Geometry.h. Not searched, so I filed nothing on it.
- `types.glsl` copies of the cluster and sky-layer constants. Not opened.
