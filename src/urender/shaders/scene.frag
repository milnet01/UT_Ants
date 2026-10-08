#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The forward pass's shading -- UTA-0014 SS 4.5 to SS 4.10, with UTA-0040's
// parallax occlusion.
//
// One shader for the opaque and the translucent pass. The translucent pass
// binds only the colour attachment, so its velocity output goes nowhere --
// which is SS 4.11's "PF_Translucent batches write none".

#include "scene_bindings.glsl"
#include "clusters.glsl"
#include "flame.glsl"
#include "fog.glsl"
#include "light.glsl"
#include "liquid.glsl"
#include "probes.glsl"
#include "shadows.glsl"
#include "variation.glsl"
#include "water.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 currentClip;
layout(location = 4) in vec4 previousClip;
layout(location = 5) flat in uint zone; // UTA-0156 SS 4.4
layout(location = 6) in vec2 occlusionUv; // UTA-0164 SS 4.5
layout(location = 7) flat in uint maskChart; // UTA-0326 SS 4.5
layout(location = 8) in vec2 maskTexel;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outVelocity;
// UTA-0053: this surface's emission alone, which feeds the bloom chain. The
// translucent pass binds colour only, so its emission does not bloom.
layout(location = 2) out vec4 outEmission;

// UTA-0040 SS 4.4: the tier's step counts, set by Pipelines.cpp. A tier with
// no steps compiles the march out.
layout(constant_id = 0) const uint PARALLAX_MIN_STEPS = 0u;
layout(constant_id = 1) const uint PARALLAX_MAX_STEPS = 0u;
// UTA-0089 SS 4.5: the water look, set by Pipelines.cpp from the tier. Off, a
// liquid draws as UTA-0105 left it.
layout(constant_id = 2) const bool WATER_LOOK = false;
// UTA-0180: large-scale variation, set by Pipelines.cpp from the tier.
layout(constant_id = 3) const bool TILE_VARIATION = false;
// UTA-0215: Feature::Caustics, from Medium.
layout(constant_id = 4) const bool CAUSTICS = false;
// UTA-0277 SS 4.4: Feature::TileShuffle. Off, a Shuffle material draws as Fixed.
layout(constant_id = 5) const bool SHUFFLE_TILES = false;
// UTA-0040 SS 4.5 step 4: parallax fades out over the mip level above this.
const float PARALLAX_FADE_MIP = 4.0;

// A byte umat wrote as 127.5 * c + 128 (src/umat/Derive.cpp), read back.
float normalComponent(float stored) {
    return (stored * 255.0 - 128.0) / 127.5;
}

// The world directions in which u and v grow, built from screen derivatives --
// GEOM carries no tangents. Scaled together, so their ratio is kept.
struct TextureAxes {
    vec3 u;
    vec3 v;
};

TextureAxes textureAxes(vec3 n, vec3 p, vec2 duv1, vec2 duv2) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 gradientU = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 gradientV = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = inversesqrt(max(max(dot(gradientU, gradientU), dot(gradientV, gradientV)), 1e-20));
    return TextureAxes(gradientU * scale, gradientV * scale);
}

// The surface normal `n` tilted by a normal map's `tangentNormal`.
//
// umat's X is the height's descent along +u, and its Y along -v: its Sobel
// measures down the image, which is +v, and keeps that sign. So the tangent is
// the direction u grows and the bitangent the direction v SHRINKS.
//
// The frame's sign needs no correction here: `n` is turned toward the viewer
// before this is called and the projection's +Y-down is fixed, so the screen
// orientation is the same for every visible fragment. Multiplying it out was
// tried, and a mutation removing it changed no pixel.
vec3 perturbed(vec3 n, TextureAxes axes, vec3 tangentNormal) {
    return normalize(axes.u * tangentNormal.x - axes.v * tangentNormal.y + n * tangentNormal.z);
}

// The depth below the surface at `at`, white being high. UTA-0277 SS 4.4: a
// shuffled material's height is its two copies' blend, so the march finds the
// depth of the picture drawn there. The blend is the noise's alone: the
// contrast term needs both base samples, which the march does not take.
float depthAt(Material material, vec2 at, float lod, bool shuffled, TileBlend tile) {
    if (!shuffled) return 1.0 - textureLod(textures[nonuniformEXT(material.height)], at, lod).r;
    float a = textureLod(textures[nonuniformEXT(material.height)], at + tile.offsetA, lod).r;
    float b = textureLod(textures[nonuniformEXT(material.height)], at + tile.offsetB, lod).r;
    return 1.0 - mix(a, b, smoothstep(0.2, 0.8, tile.along));
}

// The map `map` at `at`, its two copies blended by `weight` when shuffled.
vec4 tiled(uint map, vec2 at, vec2 duv1, vec2 duv2, bool shuffled, TileBlend tile, float weight) {
    if (!shuffled) return textureGrad(textures[nonuniformEXT(map)], at, duv1, duv2);
    return mix(textureGrad(textures[nonuniformEXT(map)], at + tile.offsetA, duv1, duv2),
               textureGrad(textures[nonuniformEXT(map)], at + tile.offsetB, duv1, duv2), weight);
}

// UTA-0040 SS 4.5: the coordinate the view meets the height field at. White is
// high and depth is pushed inward. The height map is sampled at one mip level
// fixed before the loop: implicit derivatives inside a loop with an early exit
// are undefined.
vec2 parallaxUv(Material material, TextureAxes axes, vec3 n, bool shuffled, TileBlend tile) {
    // Before any per-fragment return: the caller branches only on per-draw
    // values, so this is still uniform control flow (review-code 2026-09-26).
    float lod = textureQueryLod(textures[nonuniformEXT(material.height)], uv).x;

    vec3 toEye = normalize(frame.eye - worldPosition);
    float facing = dot(n, toEye);
    if (facing <= 0.0) return uv;

    float fade = 1.0 - clamp(lod - PARALLAX_FADE_MIP, 0.0, 1.0);
    if (fade <= 0.0) return uv;

    // The UV distance the view crosses per unit of depth, in each axis, and the
    // whole depth as a fraction of one repeat of the base picture.
    vec2 texels = vec2(textureSize(textures[nonuniformEXT(material.base)], 0));
    vec2 across = vec2(dot(toEye, axes.u), dot(toEye, axes.v)) / max(facing, 0.05);
    vec2 reach = across * (float(material.parallaxDepth) / texels) * fade;

    uint steps = max(1u, uint(mix(float(PARALLAX_MAX_STEPS), float(PARALLAX_MIN_STEPS), facing)));
    float layer = 1.0 / float(steps);
    vec2 stepUv = reach * layer;

    vec2 at = uv;
    float layerDepth = 0.0;
    float depthHere = depthAt(material, at, lod, shuffled, tile);
    for (uint i = 0u; i < PARALLAX_MAX_STEPS; ++i) {
        if (i >= steps || layerDepth >= depthHere) break;
        at -= stepUv;
        layerDepth += layer;
        depthHere = depthAt(material, at, lod, shuffled, tile);
    }

    // One linear interpolation between the last two samples, as Godot does.
    vec2 before = at + stepUv;
    float after = depthHere - layerDepth;
    float previous = depthAt(material, before, lod, shuffled, tile) - (layerDepth - layer);
    float weight = after / min(after - previous, -1e-6);
    return mix(at, before, clamp(weight, 0.0, 1.0));
}

// UTA-0163: the level's sky in direction `d`, from the six faces urender drew
// from its SkyZoneInfo -- Sky.h. The face is the axis `d` is longest on, in
// shadows.glsl's order, and a sample stays half a texel inside its cell.
vec3 skyAt(vec3 d) {
    vec3 a = abs(d);
    uint face;
    if (a.x >= a.y && a.x >= a.z) face = d.x >= 0.0 ? 0u : 1u;
    else if (a.y >= a.z) face = d.y >= 0.0 ? 2u : 3u;
    else face = d.z >= 0.0 ? 4u : 5u;
    ShadowFace sky = shadowFaces[frame.skyFirstFace + face];
    vec4 clip = sky.viewProj * vec4(d * 100.0, 1.0);
    vec2 cell = clip.xy / clip.w * 0.5 + 0.5;
    vec2 size = vec2(textureSize(textures[nonuniformEXT(frame.skyTexture)], 0));
    vec2 halfTexel = 0.5 / (size * sky.atlasRect.zw);
    cell = clamp(cell, halfTexel, 1.0 - halfTexel);
    return textureLod(textures[nonuniformEXT(frame.skyTexture)], sky.atlasRect.xy + cell * sky.atlasRect.zw, 0.0).rgb;
}

// UTA-0275: a detail texture's weight falls linearly from whole at the eye to
// nothing at DETAIL_FAR, in UT units; and its texels are DETAIL_SCALE times
// finer than the two pictures' size ratio alone gives. Fitted on UT_MonsterHunt's
// paired on/off captures of DM-Fetid (work/uta0269/detail2/, nine poses 64 to
// 1023 units, 1280x720), scoring the spread of log(on/off) by distance on
// every surface that names a detail: whole-then-cut (0 to 380, gone at 440)
// scored rms 0.021; a linear fade from the eye, 0.0074 to 0.0045 for FAR 400
// to 520, rising again past it (0.0062 at 640). Scale by how much of the
// grain sits under 3 px against 15 px: 0.41 to 0.47 in the original, 0.15 to
// 0.35 at scale 1, 0.37 to 0.47 at 8, 0.41 to 0.53 at 16 (scratch:
// ~/.cache/uta-scratch/u275/prof.py, fine.py). Surfaces naming no detail
// changed by 0.000 in the original at every distance.
const float DETAIL_FAR = 520.0;
const float DETAIL_SCALE = 8.0;

// UTA-0275: `colour` (linear) with its detail multiplied in, as UT99 does on
// displayed values: twice the detail's grey, so 128 leaves it as it was. The
// detail's coordinates are the base's, scaled by how many detail repeats one
// base repeat holds.
vec3 detailOver(vec3 colour, Material material, vec2 uv, vec2 duv1, vec2 duv2, float distanceToEye) {
    float weight = clamp(1.0 - distanceToEye / DETAIL_FAR, 0.0, 1.0);
    if (weight <= 0.0) return colour;
    vec2 repeats = vec2(material.detailRepeatU, material.detailRepeatV) * DETAIL_SCALE;
    float grey = textureGrad(textures[nonuniformEXT(material.detail)], uv * repeats, duv1 * repeats, duv2 * repeats).r;
    vec3 displayed = mix(colour * 12.92, 1.055 * pow(colour, vec3(1.0 / 2.4)) - 0.055,
                         greaterThan(colour, vec3(0.0031308)));
    displayed = clamp(displayed * mix(1.0, 2.0 * grey, weight), 0.0, 1.0);
    return mix(displayed / 12.92, pow((displayed + 0.055) / 1.055, vec3(2.4)), greaterThan(displayed, vec3(0.04045)));
}

void main() {
    Material material = materials[draw.materialIndex];

    // Derivatives first, in uniform control flow: every material map is sampled
    // with these, at whatever coordinate parallax gives (UTA-0040 SS 4.5 step 1).
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    // A two-sided surface seen from behind is lit, and displaced, on the side you see.
    vec3 surface = normalize(worldNormal);
    if (!gl_FrontFacing) surface = -surface;
    TextureAxes axes = textureAxes(surface, worldPosition, duv1, duv2);

    // UTA-0105 SS 4.4: a liquid shifts its picture by moving noise, and skips
    // parallax, whose march would chase a height map the picture has left.
    // The derivatives stay the undisplaced coordinate's.
    bool liquid = material.liquid != NONE;
    LiquidSample wet;
    wet.offset = vec2(0.0);
    wet.tilt = vec2(0.0);
    wet.colour = vec3(0.0);
    if (liquid) wet = liquidAt(liquids[material.liquid], uv, frame.flameSeconds, draw.materialIndex, material.glass);
    bool waving = liquid && liquids[material.liquid].kind == LIQUID_WAVE;

    vec2 shadingUv = uv + wet.offset;
    // UTA-0281: a sky is painted light and depth, so it is drawn flat --
    // generated relief raised every cloud's edge, like embossed plaster.
    bool skyFlat = frame.skyCapture != 0u;
    // UTA-0277 SS 4.4: a material the bake judged natural is drawn so its
    // repeats do not line up -- every map at the same two offsets and weight,
    // so the lighting stays on the picture it belongs to. No fade toward the
    // mean, as water has: a wall would flatten to one colour. Decided per draw,
    // so the flow stays uniform.
    bool shuffled = SHUFFLE_TILES && material.tileKind == TILE_SHUFFLE && !liquid && !skyFlat
                    && (draw.polyFlags & (PF_MASKED | PF_FAKE_BACKDROP)) == 0u;
    TileBlend tile = tileBlendAt(uv, draw.materialIndex);
    if (!liquid && !skyFlat && PARALLAX_MAX_STEPS > 0u && material.parallaxDepth != 0u
        && (draw.polyFlags & (PF_MASKED | PF_FAKE_BACKDROP)) == 0u)
        shadingUv = parallaxUv(material, axes, surface, shuffled, tile);

    // An _SRGB block format: the sampler returns linear (SS 4.10). UTA-0089
    // SS 4.4: a Wet liquid's picture is varied so its repeats do not line up.
    bool wetPicture = WATER_LOOK && liquid && liquids[material.liquid].kind == LIQUID_WET;
    vec4 base = wetPicture ? waterPicture(material.base, uv, shadingUv, duv1, duv2, draw.materialIndex)
                           : textureGrad(textures[nonuniformEXT(material.base)], shadingUv + (shuffled ? tile.offsetA : vec2(0.0)),
                                         duv1, duv2);
    float tileMix = 0.0;
    if (shuffled) {
        vec4 second = textureGrad(textures[nonuniformEXT(material.base)], shadingUv + tile.offsetB, duv1, duv2);
        tileMix = tileWeight(tile, base.rgb, second.rgb);
        base = mix(base, second, tileMix);
    }
    // A Wave has no picture of its own: its colour is the noise's, through its ramp.
    if (waving) base.rgb = wet.colour;
    // UTA-0275: UT99 multiplies a texture's DetailTexture into it near the
    // camera, twice its displayed grey, so mid-grey changes nothing.
    if (material.detail != NONE && !liquid)
        base.rgb = detailOver(base.rgb, material, shadingUv, duv1, duv2, distance(frame.eye, worldPosition));

    // UTA-0263 SS 4.4: a material with a flame look draws moving flame, from
    // the texture coordinates, instead of its picture -- unlit, as emission.
    // A repeat is one flame, its foot at the picture's bottom row.
    bool flaming = material.flame != NONE;
    float heat = flaming ? flameHeat(vec2(fract(uv.x), 1.0 - fract(uv.y)), frame.flameSeconds, draw.materialIndex, false)
                         : 0.0;

    // Bit by bit, never by equality: PF_Masked | PF_TwoSided is still masked.
    // A masked flame's holes are where it is cold, not where its still was.
    if ((draw.polyFlags & PF_MASKED) != 0u && (flaming ? heat <= 0.0 : base.a < MASK_THRESHOLD)) discard;

    // UTA-0271: UT99 multiplies what is behind a modulated surface by twice
    // its displayed colour, so mid-grey leaves it as it was. Raised to 2.2,
    // that factor multiplies the linear light the target holds; the pipeline
    // blends DST_COLOR, ZERO. Unlit, unfogged and unreflected, as UT99 draws
    // it, and PF_Translucent wins over it.
    if ((draw.polyFlags & (PF_MODULATED | PF_TRANSLUCENT)) == PF_MODULATED) {
        vec3 displayed = mix(base.rgb * 12.92, 1.055 * pow(base.rgb, vec3(1.0 / 2.4)) - 0.055,
                             greaterThan(base.rgb, vec3(0.0031308)));
        outColour = vec4(pow(2.0 * displayed, vec3(2.2)), 1.0);
        outVelocity = vec2(0.0);
        outEmission = vec4(0.0);
        return;
    }

    vec3 colour;
    if (flaming) {
        colour = vec3(0.0); // all of it is emission, added below
    } else if ((draw.polyFlags & PF_FAKE_BACKDROP) != 0u && frame.skyTexture != NONE) {
        // UTA-0163: a window onto the sky zone, already lit when it was drawn.
        colour = skyAt(normalize(worldPosition - frame.eye));
    } else if ((draw.polyFlags & (PF_UNLIT | PF_FAKE_BACKDROP)) != 0u) {
        // No light applied -- but the output stage still applies (SS 4.10).
        colour = base.rgb;
    } else {
        vec2 stored = tiled(material.normal, shadingUv, duv1, duv2, shuffled, tile, tileMix).rg;
        vec2 tilt = skyFlat ? vec2(0.0) : vec2(normalComponent(stored.x), normalComponent(stored.y)) + wet.tilt;
        vec3 n = perturbed(surface, axes, vec3(tilt, sqrt(max(0.0, 1.0 - dot(tilt, tilt)))));
        // UTA-0215: UT99 lights a liquid's sheet once, as its front, and shows
        // it so from both sides. Seen from behind, the normal is mirrored back.
        vec3 litSurface = surface;
        if (liquid && !gl_FrontFacing) {
            n -= 2.0 * dot(n, surface) * surface;
            litSurface = -surface;
        }

        // SS 4.9: the flicker scalar multiplies the direct term and never the
        // indirect.
        vec3 direct = vec3(0.0);
        if (maskChart != MASK_NO_CHART) {
            // UTA-0326 SS 4.5: a level fragment lights from its chart's pairs
            // in place of its cluster's lights, each seen through the baked
            // mask -- or the shadow map, where a mover can come between.
            MaskChart chart = maskCharts[maskChart];
            vec2 chartSize = vec2(chart.width, chart.height);
            vec2 atlasSize = vec2(textureSize(textures[nonuniformEXT(frame.shadowMaskTexture)], 0));
            for (uint k = 0u; k < chart.pairCount; ++k) {
                MaskPair pair = maskPairs[chart.firstPair + k];
                Light light = lights[pair.light];
                vec3 lit = lightAt(light, worldPosition, n);
                if (all(equal(lit, vec3(0.0)))) continue;
                float seen = 1.0;
                if (pair.moverReach != 0u) {
                    seen = softShadowOf(light, worldPosition, surface);
                } else if (pair.x != MASK_ALL_LIT) {
                    // The border texel keeps a filtered read inside the rectangle.
                    vec2 at = vec2(pair.x, pair.y) + clamp(maskTexel, vec2(0.5), chartSize - 0.5);
                    seen = textureLod(textures[nonuniformEXT(frame.shadowMaskTexture)], at / atlasSize, 0.0).r;
                }
                direct += lit * (light.flicker * seen);
            }
            // No pair names the flashlight, which moves with the camera.
            if (frame.flashlight != NONE) {
                Light light = lights[frame.flashlight];
                direct += lightAt(light, worldPosition, n) * (light.flicker * softShadowOf(light, worldPosition, surface));
            }
        } else {
            // SS 4.6: only this fragment's own cluster's lights.
            uint cluster = clusterOf(gl_FragCoord.xy, (frame.view * vec4(worldPosition, 1.0)).z);
            uint count = clusterCounts[cluster] & ~CLUSTER_OVERFLOW;
            for (uint k = 0u; k < count; ++k) {
                Light light = lights[clusterIndices[cluster * CLUSTER_CAPACITY + k]];
                // UTA-0323: a cluster lists every light whose sphere meets its box,
                // so many give this point nothing. Their nine shadow reads were a
                // fifth of DM-Bishop's frame; skipping them changes no pixel.
                vec3 lit = lightAt(light, worldPosition, n);
                if (all(equal(lit, vec3(0.0)))) continue;
                // SS 4.8: the shadow map stands in for UTA-0112's `blocked`.
                direct += lit * (light.flicker * softShadowOf(light, worldPosition, surface));
            }
        }
        // SS 4.7: the probes, through the same normal the lights use.
        ProbeLattice lattice =
            ProbeLattice(frame.probeSpacing, frame.probeCount, frame.probeTableMask, frame.probeLongestRun);
        // UTA-0185: read half a spacing off the surface. UT99 builds on the
        // grid, so walls and floors often lie on a lattice plane, where the
        // blend gives the probes inside the room no weight and only the probes
        // on the plane count -- which sit on the surface itself and are often
        // not probes at all. Such a wall got no indirect light in stepped patches.
        vec3 probePoint = worldPosition + litSurface * (0.5 * float(frame.probeSpacing));
        vec3 indirect = indirectAt(lattice, probePoint, n);
        // UTA-0292 withdrew UTA-0156 SS 4.4's zone ambient: against exact light
        // it added up to 1.6 times the true light. Bounce and sky light, in the
        // probes, take its place (UTA-0112 SS 4.12).
        // UTA-0164 SS 4.5: how open the space above the surface is darkens the
        // light that arrives from all around it, never a light's own.
        float open = frame.occlusionTexture != NONE
            ? textureLod(textures[nonuniformEXT(frame.occlusionTexture)], occlusionUv, 0.0).r
            : 1.0;
        // UTA-0187: UT99 combines light and texture on display values, so the
        // light is taken to DISPLAY_LIGHT_POWER before it meets reflectance rho.
        // It replaced UTA-0112 SS 4.9's rho * (direct + indirect).
        // UTA-0253: a probe holds bounced light as a surface shows it, already
        // through the power, so it is added after it. Inside it, a share and an
        // albedo well under 1 were raised to the power as well, and a dark room
        // beside a lit floor got a tenth of the bounce.
        // UTA-0180: a lit surface's reflectance varies slowly across the world.
        // A liquid has its own variation (water.glsl), so it is left alone.
        vec3 reflectance = TILE_VARIATION && !liquid ? base.rgb * variationAt(worldPosition) : base.rgb;
        colour = reflectance * (pow(LIGHT_GAIN * direct, vec3(DISPLAY_LIGHT_POWER))
                             + indirect * (open * pow(LIGHT_GAIN, DISPLAY_LIGHT_POWER)));
        // UTA-0215: under a rippling surface, the light reaching a lit surface
        // in a water zone gathers into moving lines. The water itself is not.
        if (CAUSTICS && !liquid && zones[zone].water != 0u)
            colour += reflectance * causticLight(worldPosition, surface, frame.flameSeconds);
    }
    // UTA-0089 SS 4.2 and SS 4.3: a Wet or Wave liquid reflects the sky or the
    // probes, by Fresnel's law, about its surface tilted by the ripple alone.
    if (WATER_LOOK && !flaming && waterReflects(material.liquid) && (draw.polyFlags & PF_FAKE_BACKDROP) == 0u) {
        vec3 v = normalize(frame.eye - worldPosition);
        vec3 n = perturbed(surface, axes, vec3(wet.tilt, sqrt(max(0.0, 1.0 - dot(wet.tilt, wet.tilt)))));
        vec3 r = reflect(-v, n);
        // UTA-0215: from under water a surface mirrors the water, never the
        // sky, however its ripples tilt it.
        vec3 reflected = frame.cameraUnderwater == 0u && waterSeesSky(r, zone)
            ? skyAt(r)
            : waterProbeReflection(worldPosition + surface * (0.5 * float(frame.probeSpacing)), r);
        float share = waterShare(n, v);
        // A translucent pass blends ONE, ONE_MINUS_SRC_COLOR, so the added light
        // also hides more of what is behind, as a reflection does.
        colour = (draw.polyFlags & PF_TRANSLUCENT) != 0u ? colour + reflected * share : mix(colour, reflected, share);
    }

    // Emission is added to a lit surface only, as before UTA-0040, and at the
    // displaced coordinate like every other map.
    vec3 emitted = vec3(0.0);
    if (flaming)
        emitted = flameColour(material.flame, heat);
    else if ((draw.polyFlags & (PF_UNLIT | PF_FAKE_BACKDROP)) == 0u && material.emit != NONE)
        emitted = tiled(material.emit, shadingUv, duv1, duv2, shuffled, tile, tileMix).rgb;
    colour += emitted;
    outEmission = vec4(emitted, 1.0);

    // UTA-0015 SS 4.3: the fog between the eye and this surface. Texel k holds
    // the integral to slice k's far edge, which the half-slice offset lines up.
    // A translucent surface takes the transmittance only: what is behind it
    // already carries the in-scattering. Below the fog's tier the volume is one
    // texel of no fog.
    float viewDepth = (frame.view * vec4(worldPosition, 1.0)).z;
    vec3 fogAt = vec3(gl_FragCoord.xy / frame.viewportSize, fogCoordinate(viewDepth) - 0.5 / float(FOG_GRID.z));
    vec4 fogged = textureLod(fogVolume, fogAt, 0.0);
    // UTA-0215 SS 4.3: under water, far things in the water fade out, and the
    // output stage's mix then shows them as the water's colour; what is seen
    // through its surface does not fade. This comes before the fog adds its
    // light, which the fog pass has faded itself.
    colour *= zones[zone].water != 0u ? waterKept(viewDepth, frame.cameraUnderwater) : 1.0;
    colour = (draw.polyFlags & PF_TRANSLUCENT) != 0u ? colour * fogged.a : colour * fogged.a + fogged.rgb;

    outColour = vec4(colour, 1.0);
    // Current minus previous, in the target's UV units: +x right, +y down.
    outVelocity = (currentClip.xy / currentClip.w - previousClip.xy / previousClip.w) * 0.5;
}
