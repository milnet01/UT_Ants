# Research record: bounce light, probes and sky light (2026-09-30)

A dated record of online research the user asked for on 2026-09-30, while
AS-Frigate's ship interior drew near black (`UTA-0253`, `UTA-0254`,
`UTA-0255`). It is a record, not a rule: nothing is built from it until a
roadmap item says so. A background research agent gathered it; this session
opened none of the pages itself.

Source grades: **A** tool source or official documentation; **B** a
developer's own post or course notes; **C** a community guide, or a search
result whose page was not opened.

## What it found

### Map compilers for games of this kind

- **ericw-tools `light` (Quake)** [A]. Bounce is off by default. `-bounce N`
  sets the bounce count, `-bouncescale` defaults to 1, `-bouncecolorscale`
  to 0. Sky fill is `_sunlight2`, a dome of lights over the upper hemisphere,
  default 0. `_minlight` defaults to 0. Dirt depth defaults to 128 units.
- **Valve VRAD** [A, SDK source]. Bounces run until a pass adds under 1.0 a
  channel. Reflectivity is the texture's average colour, clamped to 0.99.
  Sky ambient is its own light type: a hemisphere ray that reaches a sky face
  returns the `light_environment` ambient colour.
- **q3map2** [A]. `-bounce N` is off by default. Sky light is
  `q3map_skylight amount iterations`.
- **Quake II RTX** [A]. One indirect bounce by default. The painted sky is
  replaced by a procedural one.
- **Quake II 2023 rerelease** [A]. Re-baked with ericw-tools at 8 units a
  lightmap texel, with baked occlusion and a light grid for entities.

### Sky light

All three compilers return a sky colour for a hemisphere ray that escapes to
sky. Each has the colour set by hand. No tool found averages the skybox
texture. A Valve community guide's example is sun 400 against ambient 150
[C].

### Probes against lightmaps

Every engine checked lights static surfaces from per-texel lightmaps and
keeps probes for things that move: Unity [A], Unreal [A], Source [B], the
Quake II rerelease [A]. DDGI calls probe light "inherently low frequency"
and stops leaks with a depth test per probe [A, abstract only].

### More bounces

- Unreal Lightmass [A]: the first bounce costs the most, and later ones "do
  not add very much light".
- Total indirect over first bounce is `1 / (1 - albedo)`: 1.25 at 0.2, 1.43
  at 0.3, 2 at 0.5.
- Measured albedos [A, Unreal]: worn asphalt 0.08, soil 0.13, sand 0.36,
  fresh concrete 0.51.

### Why a dim interior reads as lit in a modern game

Exposure. Defaults found: Unreal 4.27 adapts brightness between 0.03 and 8.0
at speeds 3.0 up and 1.0 down [A]; Unity HDRP spans -1 to 14 EV, and its
default mode is fixed [A]; Quake II RTX spans luminance 0.0002 to 1.0 [A];
Godot's is off by default [A].

### Samples and denoising

Unity [A]: about 100 indirect samples is often enough outdoors, more
indoors. Intel Open Image Denoise has a lightmap filter [A]. Lightmap texel
density: 16 units in Quake and Source, 8 in the Quake II rerelease.

### Maps whose lights were placed without bounce

Thin, and the sources disagree. One community page says Quake 3 maps use
`-bouncescale 0.75` to stop washing out [C]; an Unvanquished developer says
q3map2's bounce is too low [B]. No first-hand measured report was found.

## What the two sister projects said (messages, 2026-09-30)

- **DOOM_Ants**: three bounces by repeated gather passes into SH-L1 probes,
  one probe a floor cell, 256 rays each. Its real-time tracer reads the same
  probes, so it has no multi-bounce reference either. Its hand estimate for
  one sun patch gave a wall about 0.002 against 0.12 on the floor.
- **Vestige**: L2 spherical-harmonic probes on a regular grid, more bounces
  by re-capturing with the last pass bound as ambient, the sky drawn in the
  captures. Four brightness bugs it met were all factors of pi or a doubled
  cosine. It pins its maths with a furnace test: uniform radiance in, the
  same radiance out. It has no brightness reference.

Both reached the same estimate as the research: one bounce from a small sun
patch is about a hundredth of the patch. Sky light and exposure are what a
real room has that ours lacks.

## Ranked by the research, cheapest first

1. Sky light in the existing bake: a ray that reaches sky returns a sky
   colour.
2. Exposure that adapts within a clamped range.
3. Per-texel indirect light in the existing occlusion atlas for static
   surfaces, with probes kept for movers.
4. More bounces by repeated gather: gain bounded by `1 / (1 - albedo)`.
5. A minimum ambient: the user's call, since it sits beside the multiplier
   ruled out on 2026-09-20.

## What this session measured the same day

The room in the user's capture was dark for a different reason: `UTA-0255`.
Its lamp had been joined into a false strip with lights on two other decks.
With that fixed, mean linear light in the view doubled. Bounce light then
adds about 3% of the view's mean, so the ranking above is still open work
(`UTA-0254`).

## Not found

DDGI's bias figures; any id Software documentation; a source comparing
repeated gather with path tracing per texel.

## Sources

- https://ericw-tools.readthedocs.io/en/latest/light.html
- https://celephais.net/board/view_thread.php?id=61211&start=420
- https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/utils/vrad/vrad.cpp
- https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/utils/vrad/lightmap.cpp
- https://en.wikibooks.org/wiki/Q3Map2/Light
- https://q3map2.robotrenegade.com/docs/shader_manual/q3map-global-directives.html
- https://raw.githubusercontent.com/NVIDIA/Q2RTX/master/doc/client.md
- https://bethesda.net/en-US/news/enhancing-quake-ii
- https://valvedev.info/guides/custom-2d-skyboxes-and-the-light-environment-entity/
- https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_lightmap_gi.html
- https://docs.unity3d.com/Manual/LightProbes.html
- https://docs.unity3d.com/2022.3/Documentation/Manual/progressive-lightmapper.html
- https://dev.epicgames.com/documentation/en-us/unreal-engine/cpu-lightmass-global-illumination-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-lightmaps-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials-in-unreal-engine
- https://therealmjp.github.io/posts/sg-series-part-1-a-brief-and-incomplete-history-of-baked-lighting-representations/
- https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/Algorithms.md
- https://ndotl.wordpress.com/2018/08/29/baking-artifact-free-lightmaps/
- https://forums.unvanquished.net/viewtopic.php?f=9&t=2067
