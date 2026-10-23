I found 14 readability findings in this lane, none rated HIGH, plus 5 correctness leads. Of the 16 shortlisted candidates, 3 are confirmed and 13 dismissed.

## Files read
All read in full with native `Read`. The count is the last line number `Read` showed. In every file that last line was empty, so `wc -l` will likely report one less.
- src/urender/CMakeLists.txt — 128/128
- src/urender/Device.cpp — 485/485
- src/urender/Device.h — 164/164
- src/urender/Geometry.cpp — 129/129
- src/urender/Geometry.h — 64/64
- src/urender/Materials.cpp — 374/374
- src/urender/Materials.h — 114/114
- src/urender/Pipelines.cpp — 748/748
- src/urender/Pipelines.h — 123/123
- src/urender/Present.cpp — 73/73
- src/urender/Resources.cpp — 276/276
- src/urender/Resources.h — 143/143
- src/urender/Shadows.cpp — 303/303
- src/urender/Shadows.h — 196/196
- src/urender/Swapchain.cpp — 97/97
- src/urender/Swapchain.h — 73/73
- src/urender/cmake/EmbedSpirv.cmake — 28/28

## What I started with
- The global `~/.claude/CLAUDE.md` (rules 0–20a).
- A session-start hook note advertising the Ants MCP verbs.
- The user's email address, from the environment context.
- The review-lane agent definition.
- Environment facts: working directory `/home/ants/.cache/lane-neutral`, not a git repository, date 2026-10-23.
- No project `CLAUDE.md`, no memory index and no git snapshot.

## Summary
This code connects the game's renderer to the graphics card through Vulkan:
- It picks a suitable graphics card and checks it can do everything the renderer needs.
- It builds the fixed drawing setups ("pipelines") for the level, flames, shadows, fog, bloom and upscaling.
- It copies each level's textures, materials and shapes into video memory.
- It works out where each light's shadow goes in one large shared shadow image.
- It shows the finished picture in the window.

The shader files are compiled when the program is built, and are stored inside the program itself.

## Findings

[Q1] MEDIUM `src/urender/Materials.cpp:93`, `:214`, `:228` — `const gpu::Material defaults{0, 1, 2, 3, gpu::NONE, 0, 0, gpu::NONE, gpu::NONE, gpu::NONE, gpu::NONE, {0, 0}};` — Three hand-kept, by-position fill-ins of `gpu::Material`'s 13 fields, almost all `uint32`. Someone adding a field to `gpu::Material` must edit all three. If they insert it mid-struct to match `types.glsl`, every later value shifts by one place and it still compiles. The `static_assert`s in ShaderTypes.h check the struct's layout, not these lists. `:93` already gives 12 values and relies on `tileKind` silently becoming 0. — Fix: name each field in all three lists (C++20 designated initialisers).
Cost: one edit — Searched: `docs/` for `MaterialSet::upload`. UTA-0040 names the function but not the order of its fill-ins, and this fix renames nothing. No ANCHORS.

[Q1] MEDIUM `src/urender/Pipelines.cpp:204-235`, `:295-324`, `:376-400`, `:444-468` (create-info), and `:128-156`, `:251-275`, `:351-373`, `:413-427` (input assembly, viewport, rasteriser, multisample, depth) — `depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;` — Four builders repeat the same Vulkan boilerplate. Someone changing a shared convention would have to find every copy:
- **Reversed depth:** the compare op is at `:161`, `:275` and `:373`.
- **Multisampling:** it is set at `:152`, `:269`, `:368` and `:427`.

Most differences are deliberate and commented: cull mode, bias, attachments. One is not: the two additive blends disagree on alpha.
- `flamePipeline` `:284-285`: src ZERO / dst ONE, which keeps the target's alpha.
- `postPipeline` `:436-437`: src ONE / dst ZERO, which replaces it.

No comment says which is intended. — Fix: one helper that fills the shared states, with each builder overriding only what differs.
Cost: one edit — Searched: `docs/` for `scenePipeline|flamePipeline|shadowPipeline|postPipeline`. The only hit is a review record (`docs/reviews/review-code-2026-10-15/lane-05…`), not a spec. No ANCHORS.
Confirms dup:5ba6ee8ace15 and dup:9434554bd083.

[Q1] MEDIUM `src/urender/Shadows.cpp:23` — `double radiusOf(const ubundle::Light& light) noexcept { return 25.0 * (light.radius + 1); }` — UT99's light-radius formula is copied four times:
- `src/urender/Shadows.cpp:23`
- `src/ubake/ShadowMask.cpp:82`
- `src/ubake/LightModel.cpp:130`
- `src/urender/shaders/light.glsl:70`

The shadow tile's reach must equal the shader's light falloff. Changing one copy, for example to model a cylinder light's reach, cuts shadows off before the light ends. Nothing at `:23` names UT99's `WorldLightRadius` or the shader twin; only LightModel.h:47 does. — Fix: one shared C++ function, plus a comment at it and in light.glsl naming each other.
Cost: unsure — Searched: `docs/` for `radiusOf|WorldLightRadius`. UTA-0112 states the formula but names no function. The GLSL copy cannot call C++, so a merge leaves two copies tied only by comments.
Searched: `src/` for `25.0 *` and `radius + 1`.

[Q1] LOW `src/urender/Shadows.cpp:16-17` — `constexpr std::uint8_t LE_SPOTLIGHT = 12;` — The light-effect numbers are defined five times:
- Shadows.cpp
- `src/urender/Fog.h:32`, as a public `uint32`
- `src/ubundle/LightSection.cpp:119-120`
- `src/ubake/LightModel.cpp:23-24`
- `light.glsl:17-18`

The "is a spot" test `effect == LE_SPOTLIGHT || effect == LE_STATIC_SPOT` appears in four of those places. The values are UT99's and unlikely to change. But if Shadows.cpp ever includes Fog.h, the two `uta::urender` definitions become ambiguous. — Fix: one shared definition in ubundle, and Fog.h and Shadows.cpp use it.
Cost: one edit — Searched: `docs/` for `isSpot`; no hits. No ANCHORS.
Searched: the whole tree, excluding `build*`, for `LE_SPOTLIGHT|LE_STATIC_SPOT`.

[Q1/Q3] MEDIUM `src/urender/Pipelines.cpp:590-594` — `{blending != 0, twoSided == 1, false, blending == 2}` — The blend mode is a bare int whose meaning is kept by hand in three places:
- the comment at Pipelines.h:103: opaque / translucent / modulated = 0 / 1 / 2
- this loop, which also fills `SceneVariant`'s four bools by position
- the mapping in `sceneFor` at `:736-738`

Someone adding a blend mode, or a `SceneVariant` field, must keep all three in step. Reordering the bools compiles silently. `:600` passes only three of the four. — Fix: an enum for the blend index, and named fields when building `SceneVariant`.
Cost: one edit — Searched: `docs/` for `sceneFor|scenePipeline`; no hits. No ANCHORS.

[Q2] MEDIUM `src/urender/Pipelines.cpp:701` — `VkPipelineLayoutCreateInfo shadowLayoutInfo = sceneLayoutInfo;` — `Pipelines::create` runs 227 lines. The reader loses the thread here. The shadow layout is built by copying `sceneLayoutInfo`, which was declared 160 lines earlier at `:543`, and it sits under no section banner, after the fog section. Someone who adds a second set or another push-constant range to the scene layout would silently change the shadow layout too. The compute-pipeline struct `compute` is likewise reused from the cluster section into the fog section, at `:650` and `:688`. — Fix: split each `// --` section into its own function, and build the shadow layout explicitly.
Cost: one edit — Searched: `docs/` for `Pipelines::create`; no spec names it. No ANCHORS.
Confirms `src/urender/Pipelines.cpp:485:Pipelines::create`.

[Q2] MEDIUM `src/urender/Materials.cpp:163-235` — `set.rampByRecord_[m], liquidByRecord[m]` — `MaterialSet::upload` runs 222 lines. The reader loses the thread in the third pass over MATS. It reads `rampByRecord_` and `liquidByRecord` by record position `m`. Those two lists are filled by two earlier passes (`:118`, `:134`) that must push exactly one entry per record. That rule is stated only for `rampByRecord_`, at Materials.h:108, and never for `liquidByRecord`. The 25-line fire set-up at `:187-211` is embedded in the middle of it. Someone adding a `continue` to either earlier pass misaligns every later material. — Fix: move the fire set-up and the two look tables into their own functions, and state the one-entry-per-record rule beside `liquidByRecord`.
Cost: pinned — UTA-0040 `docs/specs/UTA-0040-parallax-occlusion.md` lines 22 and 146 name `MaterialSet::upload`. Extracting helpers keeps that name, so the pin is likely harmless.
Confirms `src/urender/Materials.cpp:85:MaterialSet::upload`.

[Q4] MEDIUM `src/urender/Device.h:71` — `/// NotFound or Unknown carrying `call` and the VkResult, or success.` — The code at Device.cpp:227-229 returns **OutOfMemory** or Unknown, never NotFound. Someone writing a caller that handles out-of-memory, or that treats NotFound from `check` as "no device", would trust this header and get it wrong. The truth is in another file. — Fix: change the comment to "OutOfMemory or Unknown".
Cost: one edit — comment only.

[Q4] LOW `src/urender/Materials.h:6` — `// A MATS record carries only its id and metallic; its maps are the TEXS` — False. `ubundle::MaterialRecord` (Bundle.h:252-270) also carries `parallaxDepth`, `flame`, `liquid`, `fire`, `tileKind` and `tileHash`, and `upload` reads five of them. A stranger would wrongly expect `upload` to read only id and metallic. — Fix: say "carries no maps" instead of "only its id and metallic".
Cost: one edit — comment only.
Searched: `src/ubundle` for `struct MaterialRecord`.

[Q4] LOW `src/urender/Resources.h:7` — `// driver's allocation count -- a bundle's textures, five maps a material --` — Materials.cpp:166-172 looks up seven maps per material (base, normal, rough, height, emit, glass, detail), plus one picture per fire look. — Fix: drop the count, or say "up to seven maps".
Cost: one edit — comment only.

[Q4] LOW `src/urender/Shadows.h:95` — `/// Free tiles by level: level 0 is LARGEST_SHADOW_TILE, each level half the last.` — This comment sits on `size_` but describes `free_` on the next line. A reader takes `size_` for the per-level list. — Fix: move the comment to `free_`.
Cost: one edit — comment only.

[Q4] LOW `src/urender/Shadows.h:129` — `std::uint32_t face = 0; ///< into Plan::faces` — No type called `Plan` exists; the type is `ShadowPlan`. — Fix: say `ShadowPlan::faces`.
Cost: one edit — comment only.

[Q3] LOW `src/urender/Shadows.h:140` — `double unitsPerTexel = 0; ///< UTA-0303: the texel the near lights' tiles were sized at` — The bare name suggests the texel size used for every light. It holds only the near lights' value; `restUnitsPerTexel` holds the rest. The comment right beside it corrects this, so LOW. — Fix: rename to `nearUnitsPerTexel`.
Cost: unsure — Searched: `docs/` with the five-name pattern above, not for this name. I did not search the tree for other uses of the field.

[Q5] LOW `src/urender/Pipelines.cpp:503-504` — `limits.maxPerStageResources > besideTextures ? limits.maxPerStageResources - besideTextures : RESERVED;` — Falling back to `RESERVED` makes `textureCapacity_` exactly 0 after the subtraction at `:508`. Frame.cpp:820 then refuses every bundle. No comment says that is the intent. A reader would likely take it as "two slots" and "fix" it. — Fix: a one-line comment that a device too small here gets capacity 0 and is refused at load.
Cost: one edit — comment only.

[Q5] LOW `src/urender/Device.cpp:33` — `const std::array<Requirement, 14> REQUIREMENTS = {{` — The entry count is kept by hand. Adding a requirement without bumping 14 fails to compile. Removing one without lowering it leaves a zero-filled last entry whose `met` is a null pointer, which `firstMissingRequirement` then calls. — Fix: let the compiler count the entries (`std::to_array`).
Cost: one edit — Searched: `docs/` for `REQUIREMENTS`; not searched separately, but the fix renames nothing.

## Candidates dismissed
- `src/urender/Pipelines.cpp:77:scenePipeline` — Long, but each block is one Vulkan state and carries its reason. The reader does not lose the thread inside it. Its sharing with the other builders is reported under the dup finding.
- `src/urender/Device.cpp:269:Gpu::create` — Linear set-up steps, each with its reason (validation, instance, surface, device, pool). I found no point where the thread is lost.
- `src/urender/Geometry.cpp:32:SceneGeometry::upload` — The checks and the copy loop are short and commented. The long PF_PORTAL comment explains why not to restore the old test, which is a real "why".
- `src/urender/Pipelines.cpp:244:flamePipeline` — Its own content is clear; it is covered by the dup finding.
- `src/urender/Swapchain.cpp:11:Swapchain::create` — A linear query-then-create sequence, with special cases (zero extent, fallback formats) all commented.
- `src/urender/CMakeLists.txt:4:census_number` — "route 3" is a route's identifier, not a count. The cited `docs/standards/dependency-acquisition.md` exists.
- `src/urender/Geometry.cpp:70:census_number` — "56 nodes" describes a measured map in a dated fix rationale. It is not a count of anything in the code.
- `src/urender/Materials.h:42:census_number` — Verified. 65536 / 60 is about 18.2 minutes, and the cap of 60 per second is real (`ufire::stepsPerSecond`, Fire.cpp:251).
- `src/urender/Materials.h:47:census_number` — Verified. "16 bytes" matches `BLOCK = 16` at Materials.cpp:309, and is a multiple of the BC4 (8) and RGBA8 (4) blocks.
- `src/urender/Shadows.cpp:19:census_number` — Verified. A cone byte of 255 gives a half-angle of acos(1/256), about 89.8°, so "nearly a hemisphere" is true.
- `src/urender/Shadows.h:67:census_number` — "135 views" records a dated measurement behind the constant. The current rule comes first in that comment.

## Correctness leads
- `src/urender/Materials.cpp:219` — A material with a liquid look (or emit, glass or detail maps) but none of base, normal, rough or height falls back to `DEFAULT_MATERIAL`, and its `liquidByRecord[m]` is dropped. Unsure whether such a material can occur. — Searched: none.
- `src/urender/Materials.cpp:166-169` — For a fire material, base, normal, rough and height are uploaded through `mapIndex` before the fire branch, then discarded at `:214`. That is wasted video memory. — Searched: none.
- `src/urender/Resources.cpp:201` — `vkCreateImageView` goes through `check`, not `created`. On failure `view_` is undefined, and `reset()` at `:162` would destroy it. That is the exact case Device.h:74-77's `created` exists for. — Searched: read Resources.cpp in full.
- `src/urender/Swapchain.cpp:77` — `vkCreateSwapchainKHR` goes through `check`, not `created`. On failure `handle_` may be non-null garbage that `~Swapchain` destroys. — Searched: read Swapchain.cpp in full.
- `src/urender/Pipelines.cpp:284-285` vs `:436-437` — The two additive blends disagree on alpha (keep the target's vs replace it). This may or may not matter for the emission or bloom targets. — Searched: none.

## Not checked
- I did not open `Frame.cpp`. So I did not confirm that the flame and mote draws need no dynamic depth bias, or how the fixed `frontFace` interacts with per-draw state.
- I did not check `types.glsl` against `gpu::Material`'s field order, beyond the struct and its `static_assert`s.
- I did not check the UTA-0014 spec's SS 4.4 requirement table against `REQUIREMENTS`' order.
- I did not search the tree for uses of `ShadowPlan::unitsPerTexel`. The rename's cost is graded "unsure" for that reason.
- No ANCHORS were given, so none were checked.
