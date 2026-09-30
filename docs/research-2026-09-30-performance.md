# Research record: performance (2026-09-30)

A dated record of online research the user asked for on 2026-09-30, on four
areas: the code, baking, real-time frame time, and smoothness while the
camera moves. It is a record, not a rule: `UTA-0129`'s benchmark decides
what is built. A background research agent gathered it; this session opened
none of the pages itself.

Tags: **[read]** the agent opened the page; **[snippet]** it saw a search
summary only, so the claim wants confirming before anything rests on it.

## Real time, Vulkan

- **Cached shadow maps.** The largest documented saving. Epic measured three
  shadow-casting point lights at 14.89 ms uncached and 0.9 ms cached [read].
  It needs static geometry and lights that stay put; a light whose reach
  holds a mover needs a re-render.
- **Culling with the map's own visibility data** plus the view frustum is
  still the cheap option for static indoor maps [snippet]. On a small map
  the saving is mostly in fragment and shadow work.
- **Multi-draw indirect and bindless textures** cut CPU submit time only
  [snippet]. No help if the frame is bound by the GPU, which is likely at
  3840x2160.
- **Culling lights by screen influence**: Unreal exposes thresholds
  [snippet]. No measured saving found.
- **Depth pre-pass**: the one page opened gives no figures and says to
  profile [read].
- **Volumetric fog**: about 1.1 ms on a 160x90x64 grid on PS4 [snippet].
  Cost follows the grid, not the screen, so tier the grid.
- **Upscalers**: FSR 1 is spatial, FSR 2 needs motion vectors [snippet].

## Hitches while the camera moves

- **Pipeline compilation at first use** is the classic cause [read]. Create
  every pipeline at load and keep one `VkPipelineCache` on disk [read].
  `RADV_DEBUG=psocachestats` reports cache hits and misses [read].
- **Uploads**: staging buffers with a byte budget a frame, and no
  `vkQueueWaitIdle` per upload [snippet].
- **Allocation and descriptors**: sub-allocate from large blocks, keep
  memory mapped [read].
- **Present modes**: FIFO is the only one guaranteed [snippet].
- **Measuring on this driver**: `MESA_VK_TRACE=rgp` writes a Radeon GPU
  Profiler capture to `/tmp`, which is memory on this machine [read].
  `GALLIUM_HUD` does not apply to RADV [read]. MangoHud logs frame times and
  summarises the 1% and 0.1% lows [snippet].

## Bake time

- **An early exit for shadow rays and culling lights by radius first.**
  tinybvh's figures show shadow rays about three times faster than nearest-
  hit rays [snippet].
- **Tree quality**: binned surface-area splits are the standard [snippet].
- **A wide tree**: tinybvh, one MIT header, has double-precision variants
  and comes within a few percent of Embree for single rays [read].
- **Embree** is single precision only [snippet], users report different
  results between AVX and AVX2 builds [read], and one builder is not
  deterministic across threads [snippet]. It conflicts with the byte-
  identical rule.
- **Hashing**: SHA-256 with the CPU's SHA instructions runs at roughly 1.5
  to 2.4 GB/s a core against about 0.4 to 0.65 in software [snippet], and
  gives the same digest, so bake names do not change. BLAKE3 is faster and
  renames every bake [read].

## C++ practice

- A single locked queue shows contention only beyond about 8 workers with
  jobs under about 20 microseconds [snippet, low authority]. Tune the chunk
  size of `parallelFor` before replacing the pool.
- GCC fuses multiply-adds across statements by default where the target has
  the instruction; Clang differs [snippet]. So `-march` above the baseline
  or link-time optimisation can change baked bytes unless contraction is
  turned off. `-ffast-math` breaks the rule outright.

## Measuring

- Use the **minimum** for CPU timings: noise only adds time [snippet].
- Compare two builds in interleaved runs: link order and environment size
  bias results [snippet].
- For frames, the median and the 1% low; the 0.1% low rests on two or three
  frames [snippet]. Discard the first pass [read].

## The research's ranked ten

| # | Change | Expected benefit | Cost | Risk to identical bakes |
|---|---|---|---|---|
| 1 | Cache static shadow maps | largest | medium | none |
| 2 | Create all pipelines at load, with a disk cache | removes first-use hitches | low | none |
| 3 | Early-exit shadow rays, cull lights by radius | large for the probe bake | low | none if the cull is exact |
| 4 | A shadow budget per light, culled by screen influence | smoother camera moves | low to medium | none |
| 5 | SHA-256 on the CPU's SHA instructions | about 3 to 4 times on hashing | low | none |
| 6 | Better tree, then a wide one, kept in double | 2 times or more, uncertain | medium to high | medium |
| 7 | Tier the fog grid | about 1 ms | low | none |
| 8 | An upload budget a frame | removes streaming hitches | medium | none |
| 9 | Tune `parallelFor` chunk size | small to medium | low | none |
| 10 | Multi-draw indirect and bindless | CPU only | medium | none |

## Not found

Measured depth pre-pass figures; float against double traversal cost;
packet tracing for probe rays; GPU ray-query determinism across vendors; a
determinism guarantee from Embree; desktop FSR cost tables.

## One thing this session measured the same day

`SurfaceRays` builds its tree in 0.02 s over AS-Frigate's 22811 triangles,
and the bake builds one in each of three places. At that size the repeat is
not worth removing.

## Sources

- https://dev.epicgames.com/documentation/en-us/unreal-engine/movable-lights?application_version=4.27
- https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@15.0/manual/Shadows-in-HDRP.html
- https://30fps.net/pages/pvs-portals-and-quake/
- https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html
- https://vkguide.dev/docs/gpudriven/draw_indirect/
- https://github.com/Novum/vkQuake/releases
- https://interplayoflight.wordpress.com/2020/12/21/to-z-prepass-or-not-to-z-prepass/
- https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf
- https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite
- https://www.khronos.org/blog/reducing-draw-time-hitching-with-vk-ext-graphics-pipeline-library
- https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/
- https://docs.mesa3d.org/envvars.html
- https://github.com/flightlessmango/MangoHud
- https://github.com/wolfpld/tracy
- https://www.sci.utah.edu/~wald/Publications/2007/ParallelBVHBuild/fastbuild.pdf
- https://github.com/jbikker/tinybvh/blob/main/README.md
- https://www.embree.org/
- https://github.com/RenderKit/embree/issues/51
- https://github.com/BLAKE3-team/BLAKE3
- https://kristerw.github.io/2021/11/09/fp-contract/
- https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3375r3.html
- https://arxiv.org/abs/1608.04295
- https://dl.acm.org/doi/10.1145/1508244.1508275
- https://www.capframex.com/blog/post/Explanation%20of%20different%20performance%20metrics
