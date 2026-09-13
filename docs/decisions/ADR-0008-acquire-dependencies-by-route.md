# ADR-0008: Acquire each dependency by the route its own nature dictates

- **Status:** Accepted
- **Date:** 2026-09-13
- **Supersedes:** [ADR-0007](ADR-0007-acquire-dependencies-by-route.md). The
  decision is unchanged. Its operating procedure moved to
  [`docs/standards/dependency-acquisition.md`](../standards/dependency-acquisition.md).

## Context

Linux and Windows are both first-class targets (`docs/design.md` § The stack).
Windows has no system package a build can assume, so "take the distribution's
package" stopped being a default. **S7** still requires a clone with no Unreal
Tournament to build and test.

Some mechanisms fail on facts rather than taste. ADR-0007's Context records
the measurements:

- SDL3 is absent from `ubuntu-24.04` and from `windows-2022`.
- shaderc cannot be built alone. Its `third_party/CMakeLists.txt` needs
  glslang, SPIRV-Tools and SPIRV-Headers synced beside it.
- The Vulkan headers, loader and `glslc` are a set the platform publishes,
  used against a driver the machine already has.

ADR-0007 carried the decision and the procedure for applying it. Its first
review run reached its cap, and that run's last loop-log row records the
decision settled and the later findings falling on the procedure
(`docs/reviews/ADR-0007-acquire-dependencies-by-route-loop-log.md`).
The renderer has since landed, so the procedure has code to be checked
against. A procedure that code conforms to is a standard, not a decision.

## Decision

Four routes. Each dependency takes the one its nature dictates.

1. **Fetched by the build, at an exact tag.** Code the compiler builds from
   source, whose version need not match anything installed. Catch2, glm,
   SDL3.
2. **Vendored in the repository.** Code whose build produces nothing anyone
   links, or that cannot build without sources it does not ship. Dear ImGui,
   `bc7enc`.
3. **Required from the platform, found and never fetched.** The Vulkan
   headers, loader and `glslc`, at the Vulkan level `docs/design.md`
   requires.
4. **Fetched only for the one non-runtime target that links it.** Assimp,
   for `ut-ed`.

`docs/standards/dependency-acquisition.md` holds the question that routes a
new dependency, and what each route requires of the build, CI and README.

**glm is pinned rather than found.** `ADR-0002` requires one map, recipe and
baker version to hash to one bundle on any machine. glm is arithmetic the
baker runs, so its version sits inside that requirement.

**Route 3 is a floor, not a pin.** Nothing it supplies enters a bundle.
Compiled shaders are built into the engine binary, and `docs/design.md`
§ The parts does not list them among a bundle's contents. So `ADR-0002` does
not reach route 3.

**vcpkg is rejected on cost, not on capability.** It has ports that build the
Vulkan loader, headers, validation layers, shaderc and glslang from source.
Using it puts a tool and a manifest in every contributor's loop, to replace
one SDK installer on Windows and one package line on Linux. Re-open this when
a second dependency needs that treatment.

**The graphics driver is never acquired by the build.** It is the machine's.

## Consequences

- The Vulkan components are a prerequisite on both platforms. **S7** is
  unaffected: its subject is a machine without Unreal Tournament, not one
  without a toolchain.
- Fetching removes a version decision, not a package list. On Linux, SDL3's
  X11, Wayland and audio backends compile against system development
  headers, and SDL3 builds without those backends when the headers are
  missing.
- The first configure needs the network. A configured build directory does
  not.
- A cold build is slower, because fetched dependencies compile from source.
- A contributor cannot substitute a distribution's SDL3 or glm without
  editing the build. For glm that is the point.
- Two things have no mechanical check: the validation layers, and whether a
  vendored copy is stale. The standard says so where it states each.
