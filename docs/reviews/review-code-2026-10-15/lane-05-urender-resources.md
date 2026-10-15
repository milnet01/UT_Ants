14 / 14 subject files read in full with native `Read`.

## Line counts (as read)
- Device.cpp 474, Device.h 150, Geometry.cpp 126, Geometry.h 63, Lights.cpp 153, Lights.h 57
- Materials.cpp 354, Materials.h 96, Pipelines.cpp 737, Pipelines.h 121, Resources.cpp 275
- Shadows.cpp 289, Shadows.h 184, Swapchain.h 72

## Already in my context when I arrived
- The global `~/.claude/CLAUDE.md` and the review-lane agent prompt.
- A user-email context note from the harness.
- This brief, which says the shared context file is provided once for all lanes.

There was no project `CLAUDE.md`, no memory index and no git snapshot. The global `CLAUDE.md` describes review skills, but nothing about this project's history.

## How I resolved two conflicts
- **The language standard.** It lives outside the project. I read it whole with native `Read`, because the brief names it as a contract.
- **The brief's "must not be re-read" line.** It sits inside the shared context, and I read that file only once. I read the subject files once each, as the tail directs.

## Critical (0)

## High (1)
- [dim 2] [tool: Khronos validation] `Materials.cpp:266-269` — `std::memcpy(staging.mapped() + cursor, ...); starts.push_back(cursor); cursor += source.bytes.size();`
  - **What's wrong:** each texture's start in the staging buffer is the plain running total. Vulkan says `bufferOffset` must be a multiple of the format's block size, which is 16 bytes for BC5 and BC7.
  - **When it happens:** any BC4 map with a full mip chain totals 8 more than a multiple of 16. That is because its 4×4, 2×2 and 1×1 levels are one 8-byte block each, and every larger level is a multiple of 32 (confirmed by `ubundle/TextureSection.cpp`'s halving loop). An RGBA8 fire picture can also end off a 16-byte boundary. Whichever BC7 or BC5 map comes next starts misaligned.
  - **Result:** `vkCmdCopyBufferToImage` runs with an invalid offset, which is undefined behaviour.
  - **Fix:** round `cursor` up to a multiple of 16 before each source, and size `total` the same way.

## Medium (3)
- [dim 5] `Materials.cpp:307,313-315` — `const auto target = static_cast<std::uint64_t>(std::floor(clock * replay.stepsPerSecond)); ... if (!pinned) due = std::min(due, FIRE_CATCH_UP_STEPS);`
  - **The problem:** a pinned light time is never capped. `Lights.cpp:60` says a pinned time "may be negative or huge (UTA-0217)".
  - **When it happens:** at about 1e6 seconds the loop runs tens of millions of fire steps on one frame, which hangs it. Once `clock × stepsPerSecond` reaches 2^64, the cast is undefined.
  - **Fix:** clamp `target` before the cast, as LT_FLICKER does. Also bound `due` when pinned, for example by replaying from `primed` modulo a period, or with a hard cap.
- [dim 11] `Pipelines.cpp:495-498` — `p->textureCapacity_ = std::min({limits.maxPerStageDescriptorSampledImages, limits.maxPerStageDescriptorSamplers, limits.maxDescriptorSetSampledImages, limits.maxDescriptorSetSamplers, TEXTURE_CEILING + RESERVED}) - RESERVED;`
  - **What's missing:** `maxPerStageResources` is not in the min. The fragment stage also sees the scene set's storage buffers, and Vulkan's pipeline-layout rules count all of them together against that limit.
  - **When it fails:** on a device whose `maxPerStageResources` is below roughly capacity + 2 + the storage-buffer count (the binding range from `FRAME` to `MASK_PAIRS`), creating the scene pipeline layout breaks the spec.
  - **Fix:** also subtract the fragment stage's other resources from `maxPerStageResources`, and take the min. Each GPU reports its own limits, so I could not tell which devices this hits.
- [dim 16] `Device.cpp:384,391`, `Resources.cpp:103,122,189,213,271`, `Pipelines.cpp:528,539,555,...` — `UTA_CHECK(check(vkCreateDevice(..., &gpu->device_), ...))` (the same pattern at each site)
  - **The problem:** Vulkan says a failed create leaves its output parameters undefined. Each of these sites then returns with the member still holding whatever was written. The destructor or `reset()` hands that value to `vkDestroyDevice`, `vkDeviceWaitIdle`, `vkFreeMemory`, `vkDestroyBuffer` and so on, which is undefined behaviour on an out-of-memory path.
  - **Evidence it's an oversight:** only `vkCreateInstance` (`Device.cpp:299`, `gpu->instance_ = VK_NULL_HANDLE;`) guards against this. Pipelines are safe because the spec sets those handles to null on failure.
  - **Fix:** create into a local and assign it only on success, or reset the member to `VK_NULL_HANDLE` on failure.

## Low / Info
- [dim 2] `Geometry.h:51-52` — `PF_Portal and PF_Invisible batches are not here (SS 4.5).` The code at `Geometry.cpp:76` draws portals, as UTA-0188 and the spec's § 4.5 table require. **The header comment is wrong.**
- [dim 2] `Pipelines.h:9-10` — `so four pipelines cover every flag word`. The code builds a 3×2 `scene_` array (adding the modulated variant, UTA-0271) plus two depth pipelines. **The comment is wrong.**
- [dim 2] UTA-0014 § 6 says lights "are admitted in descending projected size", and § 4.8's opening says tiles are "allocated by the light's projected screen size". The code (`Shadows.cpp:242`, `shadowTileSize`) sizes tiles by the light's reach, as the UTA-0166 amendment in § 4.8 says. **The document is wrong; this goes to `review-contract`.**
- [dim 7] `Materials.cpp:60` — `if (ubundle::expectedBlockBytes(texture) != texture.blocks.size() || texture.width == 0 || texture.height == 0)`
  - **The gap:** `mipCount` is not checked. `expectedBlockBytes` returns 0 when `mipCount == 0`, so a hand-built bundle with no blocks passes, and `vkCreateImage` gets `mipLevels = 0`.
  - **Also:** a hand-built `mipCount` of 32 or more makes `texture.width >> level` (line 70) undefined, which is exactly what `TextureSection.cpp:172` warns about. `Geometry.h:56-58` already says a bundle built in memory skips `ubundle::read`.
  - **Fix:** apply the same `[1, bit_width(max(w,h))]` check here.
- [dim 8] `Device.cpp:234-237` against `Device.cpp:465-468` — the error count is incremented before `first` is written under the lock. If `run` reads the count in between, it reports "N error(s); the first: " with an empty message. **Fix:** write `first` under the lock before incrementing, or increment inside the lock.
- [dim 7] `Lights.cpp:20-21` — `seconds / cycleSeconds(...)` with a non-finite `seconds` gives NaN for pulse, blink and strobe. LT_FLICKER and `smoothNoise` guard against this; `cyclePosition` does not. See the open questions on whether the caller sanitises the time.
- **INFO, coverage:**
  - **Dimension 2b (zombie features):** I searched only `mipCount` in `src/ubundle`. No search for callers of contract-promised entry points was run, so this dimension is not covered.
  - **Shaders:** not read, so I could not judge whether stages match the layouts, or whether shader writes to storage buffers need the stores-and-atomics features.
- **N/A, or nothing found:**
  - Dimension 3, security: nothing beyond the hand-built-bundle checks above. `ubundle::read` validates `mipCount`.
  - Dimension 9, data integrity: N/A, nothing here persists.
  - Dimensions 10 and 12: N/A, no commitment named.
  - Dimension 13, localisation: no assumed locale found.
  - Dimension 15, privacy: nothing leaves the machine. Validation messages are logged, and they hold no secrets.
  - Dimension 17, upgrade compatibility: N/A.
  - Dimension 4, diverged duplication: the instance-handle reset above is the one case found.

## Covered by spec and looks correct
- `Device.cpp`'s requirement table matches § 4.4's rows and order. The three refusals match the spec; the third names each device and its first failing requirement. Discrete GPUs are preferred, then integrated. The enabled features are exactly the required ones, and the swapchain extension is enabled only on the presenting path. A missing validation layer is logged (§ 6).
- `Materials.cpp`'s formats match § 4.5's table: BC7 sRGB for base and emit, UNORM for the rest. A MATS id with no maps falls back to the default and is logged once (§ 6). The default base is grey, or magenta with `showMissing`.
- `Pipelines.cpp`:
  - The pipeline is chosen by `polyFlags`; translucent wins over modulated.
  - Translucent pipelines test depth without writing it and have one attachment.
  - The depth prepass has a vertex stage only.
  - Front face and depth bias are dynamic state.
  - WaterLook is specialisation constant 2 (UTA-0089 § 4.5).
  - The destructor covers every object `create` makes.
- `Shadows.cpp`:
  - The atlas handles 64–1024 power-of-two tiles and places them largest first.
  - The replan trigger compares the fields § 4.8 lists.
  - A moved mover redraws a light's tiles only when its box comes within the light's reach.
  - Lights the atlas cannot hold are counted.
  - The finer-texel search follows UTA-0303.
- `Geometry.cpp`'s range checks on indices, batches and mask charts are sound as read.

## Open questions
1. Does the renderer sanitise the light time (NaN or infinity) before calling `flickerOf`?
2. Can a pinned time actually reach `advanceFires` at extreme values?
3. Is the velocity target a format that supports blending? `flamePipeline` (`Pipelines.cpp:286`) blends into all three attachments, and blending an integer format is invalid.
4. Does `ubundle` check that `FireLook::size` is non-zero and bounded? A zero size gives `vkCreateImage` an empty extent (`Materials.cpp:195`).
5. Do baked bundles carry full mip chains today? UTA-0052 § 6 says mips may not have been generated yet. That decides how often the High finding fires, but not whether it is a defect.
6. `aspectOf` (`Resources.cpp:58-65`) maps D24S8 and D32S8 to the colour aspect. That only matters if a depth format with stencil can ever be chosen, and the format choice is outside this lane.

## 3 items to fix first
1. **Align the staging offsets in `Materials.cpp:266`.** It is a Vulkan rule broken on the ordinary map-load path, as soon as a BC4 map with mips sits before a BC7 or BC5 map. It is a one-line rounding fix.
2. **Bound fire replay under a pinned time (`Materials.cpp:307-315`).** The input is one the code already says it expects, and the result is a hang or undefined behaviour.
3. **Null the handles on failed creates (`Device.cpp`, `Resources.cpp`, `Pipelines.cpp`).** Destroying an undefined handle turns a clean out-of-memory error into a crash. The fix pattern already exists at `Device.cpp:299`.
