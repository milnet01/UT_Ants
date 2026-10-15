Line counts as read: `ShaderTypes.h` 462, `cluster.comp` 117, `flame.frag` 37, `flame.glsl` 98, `flame.vert` 34, `fog.glsl` 137, `fog_scatter.comp` 131, `light.glsl` 190, `liquid.glsl` 159, `mote.frag` 31, `mote.vert` 49, `post.frag` 116, `probes.glsl` 115, `scene.frag` 531, `scene.vert` 50, `scene_bindings.glsl` 68, `shadow_reduce.comp` 33, `shadows.glsl` 142, `sky.glsl` 31, `types.glsl` 191, `variation.glsl` 67, `water.glsl` 119.

**Already in my context on arrival:** the user's global `~/.claude/CLAUDE.md`, the user-email context block, and this agent's own system prompt (review-lane). I had no project memory index and no git snapshot. The global `CLAUDE.md` names `review-code` but carries nothing about UT_Ants.

**Reach beyond the subject, disclosed:**
- I read only the binding spec sections, as slices: UTA-0326 §4.4–4.5, UTA-0338 §4.2–4.5, UTA-0263 §4.4–4.5, UTA-0277 §4.4, UTA-0105 §4.4–4.4a, UTA-0015 §4.3–4.4 and UTA-0014 §4.6.
- Two searches of `src/` (test tree excluded) returned context lines from `Frame.cpp`, `Device.cpp` and `Pipelines.cpp`. I used them only to check claims the shaders make.
- I opened no test file.
- No disagreement between the brief and the lane file needed resolving.

## Critical (0)

## High (0)

## Medium (1)

- **[dim 3]** `scene.frag:414` — `vec2 atlasSize = vec2(textureSize(textures[nonuniformEXT(frame.shadowMaskTexture)], 0));`
  - **What happens:** any charted fragment runs this line, even when every pair it holds is `MASK_ALL_LIT` or `moverReach` and the mask texture is never read.
  - **Why it can break:** `Frame.cpp:1696` sets `shadowMaskTexture` to `NONE` whenever `impl.shadowMask.view()` is empty. If a bundle can carry charts with no texels, the shader then indexes `textures[0xFFFFFFFF]`. That is undefined behaviour, and on a GPU it can lose the device.
  - **Uncertain:** whether the bundle reader refuses such a bundle, or the uploader always creates a view, is outside this lane. Bundles are untrusted input.
  - **Fix:** take the size inside the `pair.x != MASK_ALL_LIT` branch, or treat `charted` as false when `shadowMaskTexture == NONE`.

## Low / Info

- **[dim 2]** `shadow_reduce.comp:15` — `layout(push_constant) uniform TileBlock { uvec4 tile; } block;`
  - This push-constant block has no struct or assertions in `ShaderTypes.h`. That header's own rule (`ShaderTypes.h:9-11`) says a new struct on the shader interface "goes here, with its assertions, in the same change".
  - The C++ side pushes an ad-hoc `std::array<std::uint32_t, 4>` (`Frame.cpp:1172`). The layout matches today; nothing checks it.
  - **Fix:** add `gpu::ReduceConstants` with `sizeof` and `offsetof` assertions, and push that.
- **[dim 2]** `ShaderTypes.h:247-248` — `/// One probe's ambient cube: six linear-RGB faces ... each padded to a vec4`
  - The comment says the fourth float is padding, but face 0's `.w` now carries `sunSeen` (UTA-0338 §4.5; `probes.glsl:109`). **The comment is wrong, not the code.**
  - **Fix:** say that `faces[3]` (face 0's fourth float) is `sunSeen`.
- **[dim 2]** `fog_scatter.comp:71-73` — `vec2 uv = (vec2(id.xy) + fog.jitter.xy) / ...; ... + (fog.jitter.z - 0.5) * (far - near);`
  - UTA-0015 §4.3 still says "A froxel's sample point is its tile's centre ray at the slice's middle depth". The code jitters it (UTA-0356), and §4.4's `FogConstants` already carries `jitter`.
  - **The document is stale, not the code.** This goes to `review-contract`.
- **INFO — bounds:** the shaders index buffers with values that come from the bundle, without bounds checks: `materials[draw.materialIndex]`, `liquids[material.liquid]`, `flameRamps[first + 7]`, `lights[pair.light]`, `maskPairs[chart.firstPair + k]`, `zones[zone]` and `textures[...]`. Whether the bundle reader validates these indices is outside this lane, so I could not judge dimension 3 here.
- **INFO — layout check:** I checked every `ShaderTypes.h` struct against its GLSL block by hand under std430 (and std430 push constants):
  - Buffer structs: `FrameData` 416, `Material` 56, `Light` 96, `Liquid` 176, `Zone` 20, `FlameInstance` 32, `Probe` 96, `ProbeCell`, `ShadowFace`, `MaskChart` and `MaskPair`.
  - Push constants: `DrawConstants` 24, `ShadowConstants` 80, `PostConstants` 48 and `FogConstants` 112.
  - The `Binding` enum against `scene_bindings.glsl`, 0 to 18.
  - Every offset and stride agrees. `BloomConstants`' GLSL block (`bloom.frag`) is not in my lane and I did not check it.

## Covered by spec and looks correct

- **Layouts:** every struct listed above. I worked each offset out by hand from the std430 rules, against the C++ assertions.
- **UTA-0326 §4.5:** `scene.frag:408-444`. The pair loop works out `seen` as specified (1 for `MASK_ALL_LIT`, `softShadowOf` for `moverReach`, otherwise a clamped atlas read). A `moverReach` pair reads the shadow map ahead of the mask check, which agrees with §4.4. The flashlight is added as the as-built note says.
- **UTA-0338 §4.3/4.5:**
  - `lightAt`'s sun branch matches §4.3.
  - Non-charted fragments take the sun as `lightAt × sunSeenAt` at the probe point.
  - `cluster.comp` skips `SUN_EFFECT`.
  - The disc and halo are suppressed during `skyCapture`.
  - The sun's direction (`-lightDirection`) agrees with §4.2's rotation, which points the way its light travels.
- **UTA-0277 §4.4:** one offset pair from the undisplaced `uv`, seeded by the material index. Base, normal and emission share the offsets and weight. The parallax march blends heights by the noise's own weight. There is no fade toward the mean. Specialisation constant 5.
- **UTA-0105 §4.4/4.4a:** the Ice pan sign and its 0.9 rate, the glass `(g − 46)/width` shift, and the MoveIce swap. Wet and Wave warp and tilt; a liquid skips parallax and keeps the undisplaced derivatives.
- **UTA-0015 §4.3:** haze is limited to shadowed lights plus the flashlight; volumetric glow is unshadowed and sRGB-decoded. The scene and flame fog lookup uses the −0.5-slice offset; a translucent surface takes transmittance only. The history reprojection's texel centre matches `fogCoordinate`.
- **UTA-0014 §4.6:** overflow handling in the cluster cap and insertion sort (`cluster.comp:97-112`). Shared memory comes to exactly 16 KiB.
- **Dimension 8:** in `cluster.comp`, the shade writes and the culling reads touch disjoint members, and each light index is written by one invocation only.
- **Subgroup ballot:** `Device.cpp:49-50` refuses a device without ballot support in the fragment stage, which the `scene.frag` waterfall loop relies on.

## Open questions

1. **Can a waterfall lane spin forever?** The loop at `scene.frag:418-439` ends for a lane only when `subgroupBroadcastFirst` returns that lane's chart. If an implementation counts helper invocations (including lanes that `discard` turned into helpers) as running the loop but not as *active* for the broadcast, such a lane could loop forever and lose the device. To check: the Vulkan rules for whether helper invocations count as active in subgroup operations, and how `discard` is lowered on the target drivers.
2. **Is the light shade always prepared before it is read?** `lightAt`, `lightThrough` and `sunInSky` now depend on `cluster.comp` having written each light's shade earlier in the same frame; the shade is uploaded as zero. Every `scene.frag` and fog use would then need a cluster dispatch first. That includes the UTA-0163/0281 sky-face capture and any path that skips culling. I could not see the frame recording from this lane.
3. **Is the sun meant to light the sky room during capture?** While `skyFlat` is set, `charted` is false, so level fragments in the sky room take the sun through `lightThrough × sunSeenAt` (`scene.frag:473-475`), while the disc itself is suppressed. UTA-0338 §4.5 does not say whether the captured sky room should be sunlit.
4. **Is the fog layout's binding 16 safe for `texelFetch`?** `shadow_reduce.comp:11` declares it as a plain `sampler2D`, but under the scene set it is a `sampler2DShadow`, so the descriptor's sampler presumably has compare turned on. `texelFetch` does not use the sampler, so this is probably legal. I did not confirm it against the validation layer.

## 3 items to fix first

1. **The `scene.frag:414` mask-texture index (Medium).** It is the one path where an untrusted bundle shape could, if it is reachable, index outside the texture array and lose the device. The guard is one line.
2. **Settle open question 2: the light shade's ordering.** If any draw reads a light before `cluster.comp` runs, every light in that draw is black. That is a whole-feature failure, the sky faces included.
3. **The missing `ShaderTypes.h` mirror for `TileBlock`.** It costs little to add, and it restores the header's own rule that every shader-interface struct is asserted in C++.
