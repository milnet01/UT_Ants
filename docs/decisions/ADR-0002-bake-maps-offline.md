# ADR-0002: Convert maps offline into our own format rather than read `.unr` at runtime

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

The game must play maps authored for Unreal Tournament — both Epic's and
the 610 community Monster Hunt maps on the author's server. Those maps
are stored as `.unr` packages holding CSG brush geometry, palettised
8-bit textures, and lighting already baked flat into lightmaps.

The obvious route is to read them at load time and draw what is there.
That route caps the visual result at what the file describes, because
anything better — cleaned geometry, generated PBR materials, indirect
bounce lighting, fog volumes, light shafts, reflection probes — costs far
more than a load screen can afford, and none of it exists in the file to
begin with.

There is a second pressure. **S7** requires a public repository that a
stranger can clone and build with no Unreal Tournament present, and the
content quarantine (ADR-0003) requires that nothing derived from Epic's
files ever leaves the player's machine.

## Decision

A map is **converted once, offline**, into a UT_Ants map bundle: optimised
geometry, resolved PBR materials, collision, baked indirect light, the
navigation graph and the level's event-wiring graph. The runtime loads
bundles and nothing else.

The package reader is therefore a **build-time component that the runtime
does not link**.

## Consequences

The visual ceiling rises a long way, because expensive work is paid once.
Load times improve rather than degrade. Bundles are editable, which is
what makes the map editor possible at all. And the runtime cannot
accidentally read a file it should not.

The costs. Conversion quality becomes ours to own — where the original
engine simply drew what the file said, a wrong material or a broken
brush is now a bug in our baker, which is why a material review tool is
part of the pipeline rather than a nicety. A bundle also diverges from the
map it came from, so a bundle and its source can disagree, and the recipe
that produced it has to be versioned alongside it.

What has to be true: baking must be reproducible. The same map, recipe
and baker version must produce the same bundle on any machine, or players
cannot verify they are playing the same level.
