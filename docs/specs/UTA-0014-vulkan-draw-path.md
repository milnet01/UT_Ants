# UTA-0014 — bring up a Vulkan device and draw a baked map

**Status:** accepted (2026-09-12), at the review's cap of two loops for a spec —
a calm cap: one of the final loop's ten verified findings landed on text the run
itself wrote. The loop log is
[`docs/reviews/UTA-0014-vulkan-draw-path-loop-log.md`](../reviews/UTA-0014-vulkan-draw-path-loop-log.md).
**Kind:** implement.
**Source:** ROADMAP UTA-0014 (design-2026-09-03).
**Blocked by:** none — `ubundle` ships, so the roadmap item's
`Blocked-by: ubundle` is met.
**Blocker for:** `UTA-0016` (load a bundle and walk through it), `UTA-0059`
(which defers itself until this lands), `UTA-0015`, `UTA-0040`, `UTA-0044`,
`UTA-0045`, `UTA-0051`, `UTA-0053`, `UTA-0054`, `UTA-0055`, `UTA-0089`.
**Pairs with:** `UTA-0075` (jitter and motion vectors), whose provisions § 4.11
builds now because retrofitting them touches every pass.

**Layman:** This is the item that finally puts a picture on the screen — it
starts the graphics card up and draws one of your baked maps, lit by the map's
own lights casting real shadows.

## 1. Goal

`src/urender/` exists and builds a `uta_urender` library that brings up a
Vulkan 1.3 device, reads a `ubundle::Bundle`, and draws its `GEOM` batches
wearing their `MATS` materials, lit by its `LITE` lights with shadow maps and
clustered culling, with its `LPRB` probes supplying indirect light, tone-mapped
to a linear-to-sRGB output. The same library renders **with no surface, no
swapchain and no window**, which is what lets the draw path be graded on a
machine with no graphics card — including GitHub's runners. The render graph
carries `UTA-0075`'s camera jitter, per-pixel motion vectors and
after-upscaling UI composite from its first frame.

What is **not** true after this ships: nothing opens a window. Creating the
window and its `VkSurfaceKHR`, and running a camera through a level, is
`UTA-0016`. **The swapchain over a caller-supplied surface is this item's** —
§ 4.1 lists `Swapchain.cpp` and `Present.cpp`, § 4.3 gives the presenting path
and § 4.10 its format — and it is the one part of this item no CI leg grades,
per § 4.12's third tier.

## 2. Problem

Nothing in this project draws anything. `src/` holds `core`, `ubake`,
`ubundle`, `umap`, `umat`, `unav` and `upkg`; there is no `src/urender/` and no
`src/uworld/`. Every open `urender` item in the 0.1.0 section waits on this one
— they are listed in the **Blocker for:** line above and again in § 9 — and
`UTA-0016`, the item `docs/design.md` **S1** is measured on, is `Blocked-by:`
it.

Three consequences, and the third is the one that makes this a contract rather
than a feature.

1. **A bundle's contents have never been read by a consumer.** `ubundle::read`
   is exercised by `tests/unit/` and by the real-asset tier, both of which
   check structure. No code has yet tried to *draw* a `Geometry`, resolve a
   `MaterialRecord` to its `CompressedTexture` maps, or turn a `Light`'s UT99
   bytes into light. A reader that cannot is a bake nobody can use.

2. **Three specs already bind to this one, and one of them contradicts the
   bundle.** `docs/specs/UTA-0112-baked-light-probes.md` § 4.9 is titled *"What
   the renderer does with a probe — UTA-0014's contract"* and assigns this item
   the direct-light formula, the probe blend, exposure and tone mapping.
   `docs/specs/UTA-0119-mover-shapes.md` § 4.5 states the matrices *"the
   renderer places a pivot-space point"* with. `ROADMAP UTA-0075` calls itself
   *"a constraint on UTA-0014's render graph"*. But UTA-0112's § Out of scope
   also hands this item *"ZoneInfo's `AmbientBrightness`, `AmbientHue` and
   `AmbientSaturation` … a UT99 number the renderer turns into light like any
   other"* — and **no bundle section carries those numbers**. `Ambient` appears
   nowhere under `src/`:

   ```
   $ rg -c Ambient src/ ; echo "exit=$?"
   exit=1
   ```

   So one of this item's stated inputs does not exist. § 9 records where that
   goes; it is not silently absorbed.

3. **`docs/design.md` rule 5 says `urender` reads `uworld`, and `uworld` does
   not exist.** It is `UTA-0017`, in the 0.2.0 section. A spec that took the
   rule literally would block this item behind a milestone it precedes. § 3
   decision 3 settles the reading.

And the render path is the one part of this project that no test can currently
reach, because every runner this project builds on has no graphics card.
`.github/workflows/ci.yml`'s Linux toolchain step installs `g++-14` /
`clang-19`, `ninja-build`, `shellcheck`, `yamllint` and `ccache`; the Windows
leg installs nothing. There is no Vulkan loader, no `glslc` and no driver on
any leg. Left unsettled, this item ships the largest subsystem in the project
with its correctness graded by reading.

## 3. Scope decisions (agreed with the user)

1. **The matrix grades the draw path on Mesa's software renderer, on the Linux
   legs only.** *User, 2026-09-12.* The two Linux legs install a CPU Vulkan
   driver and really draw frames and compare pixels; the MSVC leg acquires the
   Vulkan SDK (§ 4.12 says why it must), compiles `uta_urender` and runs only
   the tests that need no device. This knowingly
   weakens `docs/design.md` § The stack's *"Every release is built and its
   tests run on both"* for the device tests alone, and § 9 records the gap
   rather than leaving it to be discovered. The alternatives — SwiftShader on
   Windows, or no device tests at all — are in § 8 with the reasons they lost.

2. **Lighting is clustered forward (Forward+), not clustered deferred.**
   *User, 2026-09-12.* One shading pass over cluster-culled light lists. UT99
   carries see-through and cut-out surfaces as ordinary map geometry
   (`PF_Translucent`, `PF_Masked`), which a deferred G-buffer cannot shade and
   would need a forward pass beside it anyway; and `UTA-0075`'s velocity buffer
   is written by the same pass that shades. § 8 carries deferred's case.

3. **This item reads a `ubundle::Bundle` and a camera, never `uworld`.**
   *This session, 2026-09-12.* `docs/design.md` rule 5 — *"`urender` reads
   `uworld`; it never writes to it"* — is a **prohibition on the edge's
   direction**, and it is satisfied trivially by an item that does not depend
   on `uworld` at all. `uworld` is `UTA-0017`, in 0.2.0, and this item is in
   0.1.0; reading the rule as a requirement to depend on it would order the
   milestones backwards. When `uworld` lands, its bodies reach the renderer
   through the same camera-and-scene input this item defines.

4. **`uta_urender` does not create the window and does not link SDL3.** *This
   session, 2026-09-12.* The caller hands in a `VkSurfaceKHR` it created, or
   hands in nothing. Three reasons: `docs/design.md` § The stack gives SDL3 to
   window, input and gamepads, which is `uinput`'s and `UTA-0016`'s territory;
   rule 2 requires `ut-ants-server`'s link closure to hold no `urender`, and a
   library that owns a window is harder to keep out; and the no-surface case is
   not a degraded mode here but the one the tests use.

5. **The light model is written once, in GLSL, and is never copied into C++.**
   *This session, 2026-09-12.* `UTA-0112` § 4.9 requires a test holding this
   item's lights to `ubake::lightAt`. That test runs the real shader on a
   device and compares against `ubake` on the CPU, rather than comparing
   `ubake` against a C++ transcription of the shader. § 8 records the
   transcription option and the measured reason it lost.

6. **A device test that finds no device fails; it never skips.** *This session,
   2026-09-12.* This project has already paid for the opposite twice —
   `docs/build-and-test-lessons.md` records `spec_lint`'s `findings: []` being
   silent rather than clean, and `UTA-0098` records the real-asset tier's INV-6
   ring checks examining zero footprints on every map. A renderer test that
   passes by finding nothing to render on is the same defect.

## 4. Design

### 4.1 The library, and what it may link

```cmake
# src/urender/CMakeLists.txt
add_library(uta_urender
    Device.cpp Swapchain.cpp Frame.cpp Bundle.cpp Materials.cpp
    Lights.cpp Clusters.cpp Shadows.cpp Probes.cpp Present.cpp)
target_link_libraries(uta_urender PUBLIC uta_core uta_ubundle
                                  PRIVATE Vulkan::Vulkan glm::glm)
```

`uta_core` for `Result`, logging and the job system; `uta_ubundle` for the
content types. **Nothing else of this project's**, and the set is asserted at
configure time in `src/urender/CMakeLists.txt` the way
`src/ubundle/CMakeLists.txt` asserts `ubundle`'s (that item's INV-10) and
`src/umat/CMakeLists.txt` asserts `umat`'s (UTA-0052's INV-12). `uta_upkg`,
`uta_umat` and `uta_ubake` are build-time only (`docs/design.md` rule 2) and an
edge to any of them is a configure failure, not a review finding.

**The permitted list is `uta_core;uta_ubundle;Vulkan::Vulkan;glm::glm`, and the
two external targets are in it.** `ubundle`'s assertion compares the target's
whole `LINK_LIBRARIES` property with `STREQUAL`, so a form copied across
verbatim would fail configure on a *conforming* build here — this library links
two external targets and that one links none. Copy the mechanism, not the
string.

`Vulkan::Vulkan` and `glm::glm` are `PRIVATE`: a consumer of this library's
headers must not need the Vulkan headers on its include path, so no Vulkan type
appears in any header under `src/urender/` that another subsystem includes.
That is what keeps `ut-ants-server` able to compile against `ugame` without a
Vulkan loader present.

### 4.2 Acquisition — ADR-0007's first route-3 landing

`docs/decisions/ADR-0007-acquire-dependencies-by-route.md` § Decision already
routes every input this item needs, and this section adds nothing to it.

| Input | Route | How |
|---|---|---|
| Vulkan headers, loader, `glslc` | 3 — found, never fetched, on **both** platforms | one `find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)`, used as `Vulkan::Vulkan` and `Vulkan::glslc` |
| `glm` | 1 — fetched at an exact tag | `FetchContent` in the **root** `CMakeLists.txt`, in `tests/CMakeLists.txt`'s pattern |
| Validation layers | 3, development prerequisite | Not found by CMake at all — ADR-0007 says `FindVulkan` searches for a validation-layer library only under `IOS`. The README states them; nothing checks them |
| The graphics driver | never acquired | The machine's. `docs/design.md` rules out below Vulkan 1.3 |

`glm` is this project's first fetched runtime dependency: `rg -l glm src tests`
returns nothing today, and `tests/CMakeLists.txt` holds the only
`FetchContent_Declare` in the tree (Catch2 `v3.16.0`). The declaration moves to
the root `CMakeLists.txt`, because `glm` is linked by a `src/` target and
`tests/` is not configured at all when `UTA_BUILD_TESTS` is off.

**Shaders are compiled to SPIR-V at build time by `glslc` and the result is not
committed.** A CMake custom command per shader, with its output listed as a
source of `uta_urender` so a shader edit rebuilds it.

**The SPIR-V is embedded into the library as a generated C++ header, so
`uta_urender` loads no shader file at run time and there is no search path.**
That matters beyond tidiness: a run-time path would be a new thing for
`UTA-0016`'s packaging, the install layout and every `tests/device/` case to
bind to, all invented here. Embedding leaves nothing to bind to. A sibling
project does the same with `xxd -i`, which is the proven form
(`/mnt/Games/Scripts/Linux/DOOM_Ants/Makefile`). `glslc` emits no
dependency information for a GLSL `#include`, so **every include edge is
written out by hand in `src/urender/CMakeLists.txt`** — a rule this project
does not yet own and a sibling project states as a standing one
(`/mnt/Games/Scripts/Linux/DOOM_Ants/docs/standards/renderer.md`: *"when a
shader includes another, add the dependency explicitly in the Makefile or an
edit to the included file won't rebuild the dependents"*). § 4.6's light model
is an included file with two consumers, so this bites on the first shader
written.

### 4.3 Two paths, and the surfaceless one is the primary

```cpp
namespace uta::urender {

/// What the caller supplies.
///
/// NO VULKAN AND NO glm TYPE APPEARS IN THIS HEADER, which is INV-2 and is why
/// `surface` is an integer. A `VkSurfaceKHR` is a non-dispatchable handle and
/// is 64 bits wide on every platform, so the cast is lossless; `Device.cpp`
/// performs it and is the only file that may.
struct Config {
    std::uint64_t surface = 0;            ///< the caller's VkSurfaceKHR, cast.
                                          ///< ZERO is the surfaceless path: no
                                          ///< swapchain, and no
                                          ///< VK_KHR_swapchain extension asked for
    std::uint32_t width = 0, height = 0;  ///< the offscreen target's size
    bool validation = false;              ///< request the layer if installed
    /// Skip exposure and tone mapping, writing linear light to the target
    /// instead. It exists so INV-10 can compare a pixel against a literal --
    /// SS 4.10 says why nothing else can -- and it changes no other stage.
    bool linearOutput = false;
};

/// The view a frame is drawn from — UT99's own units and angle encoding, so a
/// caller holding a `Placement` or a `MoverShape` already has both fields in
/// the right form.
struct Camera {
    std::array<float, 3> location{};
    std::array<std::int32_t, 3> rotation{}; ///< pitch, yaw, roll; 65536 to a turn
    float verticalFovDegrees = 90;
    float nearPlane = 1, farPlane = 32768;
};

class Renderer {
public:
    [[nodiscard]] static Result<Renderer> create(const Config& config);

    /// Draw `bundle` from `camera`.
    [[nodiscard]] Result<void> draw(const ubundle::Bundle& bundle, const Camera& camera);

    /// Which target `readback` copies. A plain enum, so no Vulkan type reaches
    /// this header (INV-2) and a test can name a target without holding one.
    enum class Target { Colour, Velocity };

    /// Copy the last frame's `target` into host memory, tightly packed: RGBA8
    /// for Colour, two floats per pixel for Velocity. Surfaceless path only --
    /// the presenting path's frames go to the swapchain and are not read back.
    [[nodiscard]] Result<std::vector<std::byte>> readback(Target target = Target::Colour);
};

}  // namespace uta::urender
```

**`uta_urender` owns the projection, and the caller never builds one.** Three
consequences, and they are the reason `Camera` carries no matrix. The
handedness and the depth range are internal, so no caller has a convention to
match. § 4.11's jitter is added to a matrix this library builds, which is what
makes it invisible to `UTA-0016` and removable by `UTA-0075` without touching a
caller. And **the previous frame's view is cached here, not supplied** — motion
vectors need it, and a caller that had to keep it would be able to get it
wrong; `draw` called twice with the same `Camera` therefore produces zero
motion.

**The surfaceless path creates no `VkSurfaceKHR`, opens no swapchain, requests
no instance extension and no `VK_KHR_swapchain` device extension, and needs no
display.** Measured 2026-09-12 with a scratch probe outside the repository
(`probe.cpp` in this session's scratchpad, `c++ -std=c++23 -O1 -Wall -Wextra
probe.cpp -lvulkan`), against the Mesa `lvp` driver with `DISPLAY` and
`WAYLAND_DISPLAY` both unset. It clears a 64×64 `R8G8B8A8_UNORM` image to red
through `vkCmdBeginRendering`, draws one green triangle, barriers with
`vkCmdPipelineBarrier2`, copies to a host-visible buffer and reads two pixels:

```
$ env -u DISPLAY -u WAYLAND_DISPLAY \
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json ./probe
physical devices: 1
  llvmpipe (LLVM 23.1.0, 256 bits)         type=4 api=1.4 dynRender=1 sync2=1 graphicsFamily=0
centre  rgba = 0 255 0 255
corner  rgba = 255 0 0 255
SURFACELESS OFFSCREEN RENDER OK
```

**That measurement is the reason this path is the primary one rather than an
afterthought.** A sibling project's equivalent work went the other way and has
not recovered: `/mnt/Games/Scripts/Linux/DOOM_Ants/ROADMAP.md`'s `DOOM-0268`
records its capture mode unable to run from a non-interactive shell because
*"Vulkan hangs at window/swapchain creation under `SDL_VIDEODRIVER=x11`,
`wayland` AND `offscreen` alike (verified 2026-07-27 … so it is the environment
and not any local change)"*, and its scoped fix is *"a **true headless render
path that never creates a surface or swapchain**"* — still open. Building the
presenting path first and adding a headless one later is the mistake that
record documents.

**The presenting path** adds `VK_KHR_swapchain`, a swapchain over the caller's
surface, and `Present.cpp`. Its format choice is § 4.10's. Resize is reactive
only: `VK_ERROR_OUT_OF_DATE_KHR` from acquire or present, and
`VK_SUBOPTIMAL_KHR` from present, set a flag consumed at the top of the next
frame; nothing else triggers a rebuild.

### 4.4 Device selection — feature bits, and a refusal rather than a fallback

Enumerate physical devices; require **all** of the following, and reject a
device lacking any one of them:

| Requirement | Read from | Why this item needs it |
|---|---|---|
| `apiVersion` at least 1.3 | `VkPhysicalDeviceProperties` | `docs/design.md`'s floor |
| A queue family with `VK_QUEUE_GRAPHICS_BIT` | queue family properties | anything drawn |
| Present support on that family | `vkGetPhysicalDeviceSurfaceSupportKHR` | **presenting path only** — never queried when there is no surface |
| `dynamicRendering` | `VkPhysicalDeviceVulkan13Features` | § 4.3 builds no `VkRenderPass` objects |
| `synchronization2` | `VkPhysicalDeviceVulkan13Features` | the barrier form § 4.3 uses |
| `runtimeDescriptorArray`, `shaderSampledImageArrayNonUniformIndexing`, `descriptorBindingPartiallyBound`, `descriptorBindingVariableDescriptorCount` | `VkPhysicalDeviceVulkan12Features` | § 4.5's bindless material array |
| `textureCompressionBC` | `VkPhysicalDeviceFeatures` | the bundle stores BC4, BC5 and BC7 and nothing else — `ubundle::BlockFormat` |

**Read the feature bit, never the extension string.** A sibling project states
the reason in its own source
(`/mnt/Games/Scripts/Linux/DOOM_Ants/linuxdoom-1.10/r_vulkan.cpp`): *"the
extension strings alone don't guarantee the feature is usable."*

**A machine with no qualifying device is refused, with the missing requirement
named.** There is no fallback to pick from: `docs/design.md` § The stack rules
out *"No OpenGL fallback path — a machine without Vulkan 1.3 does not run
this."* **Three refusals, because the interesting cases have no device to name**
— and INV-5's own measured case is the first of them:

| What happened | What the error says |
|---|---|
| `vkCreateInstance` failed — no loader, or no driver behind it | that there is no Vulkan instance, and the `VkResult` |
| The instance came up and enumerated no physical device | that there is none |
| Devices exist and each fails a § 4.4 requirement | **per device**, its name and the first requirement it failed |

Only the third can name a device, so only the third promises one. A contract
that always promised a name would be unsatisfiable in the case the renderer
actually meets on a machine with no graphics driver.

Every requirement above is satisfied by Mesa's CPU driver, measured 2026-09-12:

```
$ VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json vulkaninfo \
  | grep -E '^\s+(runtimeDescriptorArray|descriptorBindingPartiallyBound|descriptorBindingVariableDescriptorCount|shaderSampledImageArrayNonUniformIndexing|dynamicRendering|synchronization2|textureCompressionBC)\s*=' | sort -u
	descriptorBindingPartiallyBound                    = true
	descriptorBindingVariableDescriptorCount           = true
	dynamicRendering                                   = true
	runtimeDescriptorArray                             = true
	shaderSampledImageArrayNonUniformIndexing          = true
	synchronization2                                   = true
	textureCompressionBC                    = true
```

So § 3 decision 1's grading is not a hope about what a software driver might
support: every bit this design requires is present on the one CI will install.

### 4.5 Reading a bundle

**Geometry.** One vertex buffer and one index buffer per bundle, uploaded from
`Geometry::vertices` and `Geometry::indices`. `GeometryVertex` is
`position`, `normal`, `u`, `v` — six floats and two — in UT99's own coordinates
and units, which this item does not convert: the camera works in them.
`Geometry::batches` is already *"strictly ascending by `material` bytewise,
then `polyFlags`, and tiling `indices` from its first element to its last"*
(`src/ubundle/Bundle.h`), so a batch is one draw of `indexCount` indices at
`firstIndex`.

**Pipeline identity is the batch's `polyFlags`, never its material.** The
material is a bindless index pushed as a constant (below), so two batches
differing only in material share a pipeline — while two sharing a material and
differing in `polyFlags` do **not**, because that word is what selects cull
mode, blending, depth write and the cutout `discard` in the table further down.
Batches are ordered by material *then* `polyFlags`, so a run of one material
routinely spans several pipelines, and keying on the material is how a
translucent batch gets drawn with the opaque state.

**Materials.** `MaterialRecord` carries only `id` and `metallic`
(`docs/specs/UTA-0011-map-baker.md` § 4.10: *"Its maps are the TEXS entries
named `<id>:<map>`"*). The five suffixes and their formats are
`src/umat/Generate.cpp`'s `MAPS` table, read rather than recalled:

| TEXS name | `BlockFormat` | Sampled as |
|---|---|---|
| `<id>:base` | `BC7` | `VK_FORMAT_BC7_SRGB_BLOCK` — § 4.10 |
| `<id>:normal` | `BC5` | `VK_FORMAT_BC5_UNORM_BLOCK` |
| `<id>:rough` | `BC4` | `VK_FORMAT_BC4_UNORM_BLOCK` |
| `<id>:height` | `BC4` | `VK_FORMAT_BC4_UNORM_BLOCK` |
| `<id>:emit` | `BC7` | `VK_FORMAT_BC7_SRGB_BLOCK` — absent unless the material is emissive |

`umat::materialId` is `<package>.<path>` lowercased, with `#masked` appended
for a masked variant (`src/umat/Generate.cpp`). A batch's `material` is opaque
to `ubundle` and may be empty, meaning none; an empty id draws with a
built-in default material rather than being skipped, so a bake with a missing
material is visible instead of invisible.

Every map is one entry in a bindless sampled-image array, indexed by a
per-batch material index pushed as a constant — which is why § 4.4 requires the
four descriptor-indexing bits. `descriptorBindingPartiallyBound` is what lets
the `:emit` slot be absent for a non-emissive material without binding a dummy.

**Why 0.5 rather than any other value.** `src/umat/Resolve.cpp` writes
`texel[3] = masked && index == SEE_THROUGH_INDEX ? std::byte{0} : std::byte{255}`
— so a masked material's alpha is **binary at the source**, and every value
between the two comes from BC7 compression and bilinear filtering at a cutout's
edge. Any threshold strictly inside the range therefore classifies every
authored texel identically; 0.5 splits the filtered edge evenly, and it is
written down so the shader constant and INV-11's fixture cannot be chosen from
each other.

**Surface flags.** `GeometryBatch::polyFlags` is *"UT99's PolyFlags, verbatim"*.
The values are `EPolyFlags` in Surreal's `Engine/Inc/UnObj.h`, the source
`docs/specs/UTA-0109-map-geometry.md` cites; the five that spec names agree
with it exactly, which is what makes the rest of the enum trustworthy here.

| Flag | Value | What this item does |
|---|---|---|
| `PF_Invisible` | `0x00000001` | Cannot appear — UTA-0109's INV-5 emits no geometry for it |
| `PF_Masked` | `0x00000002` | Alpha cutout: `discard` where sampled alpha is below **0.5**, opaque otherwise. Writes depth and velocity |
| `PF_Translucent` | `0x00000004` | Blended, drawn after every opaque batch, depth-tested and not depth-written. **Writes no velocity** — § 4.11 |
| `PF_TwoSided` | `0x00000100` | `VK_CULL_MODE_NONE` |
| `PF_Unlit` | `0x00400000` | Base colour emitted directly; no direct or indirect light applied |
| `PF_FakeBackdrop` | `0x00000080` | Drawn as the level's sky: depth written at the far plane, unlit |
| `PF_Portal` | `0x04000000` | Not drawn. It is a visibility marker, and UTA-0109 emits it deliberately so this item can choose |
| any other bit | — | **Ignored, and ignoring it is a decision.** `PF_Modulated`, `PF_Environment`, `PF_Mirrored`, `PF_NoSmooth` and `PF_SpecialLit` all have real UT99 meanings this item does not implement; § 9 says where each goes |

**Movers.** `MoverShape` holds pivot-space `geometry` plus `location`,
`rotation` and `postScale`. A point `q` is placed at
`location + postScale ⊙ (Y · P · R · q)` — the matrices, and the exact sine and
cosine of `2π × angle / 65536`, are `docs/specs/UTA-0119-mover-shapes.md`
§ 4.5's and are not restated here. That spec's INV-7 grades its own formula
against `tests/support/FCoordsPort.h`; INV-8 below grades this item's against
the same port, so the two cannot drift apart while both passing.

### 4.6 Direct light — one source of truth, in GLSL

`docs/specs/UTA-0112-baked-light-probes.md` § 4.3 defines the whole model:
colour from `hue` and `saturation`, intensity `brightness / 255`, radius
`25 × (radius + 1)`, falloff `(1 − (d/R)²)²`, incidence `max(0, n·l)` with
`LE_NonIncidence` (13) forcing 1, and the spot factor for `LE_Spotlight` (12)
and `LE_StaticSpot` (8). It is not restated here; it is *included*.

`src/urender/shaders/light.glsl` holds it, and is `#include`d by the shading
pass and by the parity test's compute shader. **No C++ transcription of it
exists anywhere in `src/urender/`.** `ubake` cannot be linked by a runtime
target (`docs/design.md` rule 2), so UTA-0112 § 4.9 is right that the renderer
writes the formula again — but *again* means once more, not twice more, and the
copy that would drift is the C++ one.

The parity test UTA-0112 § 4.9 requires therefore runs **the real shader**: a
compute shader including `light.glsl`, dispatched over a table of
`(Light, x, n)` cases, its results read back and compared against
`ubake::lightAt` computed on the CPU in the same test binary — which may link
both, that spec's § 4.9 saying so.

The comparison is in `float` and is **not bit-exact**, and the reason is worth
stating because this project holds bit-exactness elsewhere:
`docs/specs/UTA-0049-numeric-contract.md` governs the flags a **C++** compiler
is given and cannot reach SPIR-V, where precision is the driver's. An equality
assertion would be a test that passes only on the driver it was calibrated
against.

**The tolerance is `1e-3` absolute, fixed by this spec, and it is a CEILING
rather than a calibration.** A calibrated bar cannot fail its first run: the
number would be measured from the implementation under test, so a real sign or
clamp error would be rounded up into the bar and ship green. So the value is
set here, from the quantity's own range instead of from any measurement.
§ 4.3's unit puts `lightAt` in `[0, 1]`, `1e-3` is a thousandth of full range —
orders of magnitude above `float` rounding over a product of five terms, and
orders of magnitude below what a wrong sign, a missing `clamp` or a dropped
term produces, each of which moves a channel by a substantial fraction of full
range.

**A measured deviation above `1e-3` fails the test, and raising the constant is
a finding rather than a fix.** The deviation is then either a real divergence
from § 4.3 or a precision claim about a specific driver, and both need saying
out loud. That is what keeps the clause falsifiable — a bar that can be widened
to fit the result is the shape a sibling project recorded when a convergence
gate *"deterministically failed"* on one input while passing on another and
spent months treated as noise
(`/mnt/Games/Scripts/Linux/DOOM_Ants/docs/specs/DOOM-0009-path-tracer.md`).

**Clustered culling.** The view frustum is divided into a fixed grid of
froxels, exponential in depth; a compute pass builds a per-cluster light index
list; the shading pass iterates only its own cluster's. **The grid is 16 × 8 × 24
and the per-cluster light cap is 64**, both settings, and both stated here
rather than left as "a default" — § 6's overflow rule and § 10's coverage row
each rest on the cap being a number, and `UTA-0051` will bind to the grid when
it adds quality tiers. A cluster that overflows its cap drops the lights
furthest from its centre and records that it did — § 6. Determinism is not required of any of this: `ADR-0002`'s one-bundle
rule is about the baker, and nothing here writes a bundle.

### 4.7 Indirect light from probes

`LightProbes` carries `spacing` and probes *"strictly ascending by z, then y,
then x"*, each an ambient cube of six linear-RGB faces. UTA-0112 § 4.9 fixes
the evaluation:

```
indirect(n) = n.x² × cube[n.x ≥ 0 ? +X : −X]
            + n.y² × cube[n.y ≥ 0 ? +Y : −Y]
            + n.z² × cube[n.z ≥ 0 ? +Z : −Z]
```

That spec leaves this item two decisions, naming them as ours:

1. **How nearby probes are blended.** Trilinear over the eight probes of the
   lattice cell containing the point, each weighted by its corner's share.
   A missing corner — the probe list is sparse — contributes nothing and its
   weight is redistributed across the corners that are present.
2. **What a point with no probe near it gets.** Zero indirect light, and it is
   shaded by direct light alone. Not an extrapolation from the nearest probe:
   a point outside the lattice is usually outside the level, and a nearest-probe
   guess there produces light leaking through a wall, which is the artefact a
   probe lattice exists to avoid.

A surface of reflectance `ρ` shows `ρ × (direct + indirect)`, UTA-0112 § 4.9's
formula, with the shadow map standing in for its `blocked`.

### 4.8 Shadow maps

One depth atlas for every shadowing light, tiles allocated by the light's
projected screen size. A point light takes six tiles, one per cube face; a
spotlight takes one.

**A light whose actor does not move has its tiles rendered once and kept.** UT99
lights are overwhelmingly static — `ubundle::Light` carries no velocity and its
`location` comes from the actor's placement — so the cached case is the common
one, not an optimisation for later. The technique is the one Unity's HDRP
documents as cached shadow maps, whose stated condition is exactly this
project's: *"particularly useful if your environment consists of mostly static
GameObjects and the lights don't move, but there are few dynamic GameObjects
that you want the static lights to cast shadows for"*
(<https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Shadows-in-HDRP.html>).
A mover inside a static light's radius therefore needs that light's tiles
redrawn, and § 6 says what happens when the atlas cannot hold what a frame
asks for.

`Light::actorShadows` and `Light::specialLit` are carried verbatim by the
bundle and are **not** interpreted by this item; § 9 records them.

### 4.9 Flicker and the effects the bake left steady

UTA-0112 § 4.9: *"`lightAt` is a light's steady value: how its `type` varies it
over time, and what the effects § 4.3 bakes as `LE_None` add, are UTA-0014's.
The bake uses the steady value, so bounced light does not flicker."*

So the direct term is `lightAt` multiplied by a time-varying scalar derived
from `Light::type`, `period` and `phase`, which is 1.0 for a steady light. The
scalar's shape per `type` is implementation detail this spec does not pin —
nothing binds to it, and `UTA-0112`'s probes are unaffected by construction.
What this spec does pin: the scalar multiplies the direct term only, never the
indirect one, or a flickering light would make baked bounce flicker with it and
contradict the sentence above.

### 4.10 Colour: the transfer applied exactly once

The pipeline works in **linear** light throughout. UTA-0112 § 4.3's `linearOf`
decodes an 8-bit sRGB value to linear, its probe cubes are *"linear RGB"*, and
its § 4.3 sets the unit: *"A surface of reflectance 1 facing a white light of
brightness 255, at the light, shows `1.0`."*

So the transfer is applied twice in total and in opposite directions, once at
each end, and **each end is the hardware's**:

- **In:** base colour and emissive are sampled through an `_SRGB` block format
  (§ 4.5's table), so the sampler returns linear. Normal, roughness and height
  are not colour and use `UNORM`.
- **Out:** the swapchain image uses an `_SRGB` format, so the store encodes.
  **On the surfaceless path the offscreen target is `VK_FORMAT_R8G8B8A8_SRGB`,
  named here and not left to the implementer**, because a read-back pixel is
  compared against a literal and `B8G8R8A8` would swap two channels of it
  without failing anything else. `readback` returns those bytes tightly packed
  in that channel order, so a test never has to ask what format it got — which
  it could not do anyway, since an accessor returning a `VkFormat` is what
  INV-2 forbids.

**A sibling project reached the opposite conclusion and it does not transfer
here.** `/mnt/Games/Scripts/Linux/DOOM_Ants/linuxdoom-1.10/r_vulkan.cpp`
presents through `VK_FORMAT_B8G8R8A8_UNORM` and rejects `_SRGB` because DOOM's
palette colours are *already display-encoded*, so an sRGB swapchain applies the
transfer a second time; its `ROADMAP.md` records the bug that taught it, an
*"sRGB double-encode … that washed the world out"*. That reasoning is about
content that never entered linear space. This project's content does, at
`umat`'s decode, so the correct answer here is the other one — and it is
written down because copying the sibling's choice would produce exactly the
washed-out image its note describes, with the causality reversed.

**Exposure and tone mapping apply to every colour the frame writes, unlit
surfaces and sky included.** There is no per-batch bypass: `PF_Unlit` and
`PF_FakeBackdrop` mean *no light is applied*, not *no output stage*, and
exempting them would make the sky's brightness disagree with everything around
it under exactly the conditions the tone map exists for.

**That is why INV-10 needs `Config::linearOutput`.** With exposure and a
non-identity tone map between the two ends of the transfer, a texel drawn unlit
does **not** come back as the value it was stored with — so an invariant
comparing it against a literal could only pass by accident, and the two
available alternatives are both worse: a per-batch bypass changes what a real
frame looks like, and loosening the comparison to a tolerance stops it grading
the transfer at all. `linearOutput` skips the output stage and nothing else, so
INV-10 measures the sampler and the store and reaches a fixed answer.

**Exposure and tone mapping**, which UTA-0112 § 4.9 assigns here: a fixed
exposure with no automatic adaptation, set so that § 4.3's unit surface reaches
display white, then the Khronos PBR Neutral tone map. Fixed rather than
adaptive because a UT99 deathmatch map's brightness swings as the camera turns
and an auto-exposure that chases it makes aiming harder; PBR Neutral rather
than ACES because ACES shifts saturated hues, and a 1999 palette is mostly
saturated hues. `UTA-0053` owns any later grading.

### 4.11 What `UTA-0075` requires of the graph now

`ROADMAP UTA-0075` was deferred out of 0.1.0 by user decision, and its own body
sets the condition this section satisfies: *"It may move to a later milestone
freely PROVIDED it lands before the render graph has passes built on top of it
— the ordering is the point, not the milestone."* Three provisions, and no
upscaler:

1. **A sub-pixel jitter added to the projection matrix each frame**, from a
   Halton sequence, exposed so a later item can set it and turn it off.
2. **A per-pixel motion-vector target written by the opaque pass**, from the
   current and previous frame's clip positions, **excluding the jitter** — the
   FSR and XeSS documentation agree that motion vectors carry no jitter
   (<https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/>).
   `PF_Translucent` batches write none, per § 4.5: a blended surface has no
   single depth, so its velocity would be whatever drew last.
3. **A composite seam after the post stage**, so a UI pass can draw at output
   resolution into the presented image rather than into the image an upscaler
   consumes. `uui` does not exist yet; the seam is an empty pass with a stated
   position.

The negative mip bias those upscalers also want is
`log2(renderResolution / displayResolution) − 1.0` (same source). **This item
renders at output resolution and applies no bias at all** — there is no
upscaling for one to compensate for, and applying the formula's value at a 1:1
ratio would sharpen every texture for no reason. It is recorded so `UTA-0076`
does not re-derive it, and it is **not** a setting this item adds.

### 4.12 How this is graded

Three tiers, and the split is what § 3 decision 1 decided.

| Tier | Needs | Runs on |
|---|---|---|
| **Device-free** — cluster assignment, the jitter sequence, atlas tile allocation, mover placement against `FCoordsPort.h`, header isolation, every shared-struct layout `static_assert` | nothing but a compiler | every leg of the matrix, label `unit;fast` |
| **Device** — the surfaceless render, pixel comparisons, the § 4.6 light parity, the § 4.7 probe evaluation, BC texture upload and sampling | any Vulkan 1.3 device | both Linux legs, on Mesa's CPU driver; label `device` |
| **Presenting** — a window, a swapchain, resize | a display | a developer machine, by hand. `UTA-0016` is what makes it reachable |

`.github/workflows/ci.yml`'s Linux toolchain step gains three packages:
`libvulkan-dev` (loader and headers), `glslc` (the SPIR-V compiler) and
`mesa-vulkan-drivers` (the CPU driver). The first two are what a sibling
project's CI already installs for a build that links Vulkan without a GPU
(`/mnt/Games/Scripts/Linux/DOOM_Ants/packaging/ci-deps.txt`); **the third is
the one it does not**, and is the difference between compiling the renderer and
running it. `--no-install-recommends` stays, so no driver arrives by accident
and the device tier's device is a stated dependency rather than a lucky one.

**The MSVC leg installs the LunarG Vulkan SDK, and it has to.** § 4.2's
`find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)` is a *configure-time*
requirement on every leg that builds `uta_urender`, so a Windows job given
nothing new fails at `cmake -S . -B build` and never reaches a compiler —
permanently red, and red for a reason that looks nothing like a renderer
defect. `ADR-0007` § Decision already names the route: route 3 is satisfied by
*"the LunarG SDK on either platform, or on Linux the distribution's own
packages"*.

**The job installs it explicitly rather than relying on the runner image.**
Whether `windows-2022` preinstalls a Vulkan SDK is **not verified** — it cannot
be checked from this machine — and a build that silently depends on an image's
contents breaks when the image is rebuilt.

So what is Linux-only is **`mesa-vulkan-drivers` alone**, the CPU driver. The
headers, the loader and `glslc` are acquired on both. § 9's recorded gap is
therefore narrower than "Windows gains nothing": the MSVC leg configures,
compiles and runs the device-free tier, and **registers the `device` tests but
does not execute them** — § 7 deselects them there by label, which is a visible
exclusion rather than a target that quietly failed to exist.

**Cross-language struct layout is pinned at compile time, not by a test.** Every
struct shared with a shader carries a `static_assert` on its size, and one per
field on its offset. This is a borrowed bug class rather than a precaution: a
sibling project shipped a push-constant whose padding field was *"pure padding
… but MUST be present so the buffer-address fields below land at the SAME
push-constant offsets the shader reads them from … without this padding those
addresses shift 32 bytes and the NEE loop dereferences garbage
(device-lost)"*, latent across two features
(`/mnt/Games/Scripts/Linux/DOOM_Ants/linuxdoom-1.10/r_vulkan.cpp`), and now
guarded by exactly this. `static_assert` also survives `-DNDEBUG`, which
`assert` does not.

## 5. Invariants

- **INV-1** — `uta_urender`'s link set is `uta_core` and `uta_ubundle` and
  nothing else of this project's; an edge to `uta_upkg`, `uta_umat` or
  `uta_ubake` fails at configure time rather than at review.
  *Test:* the configure-time assertion in `src/urender/CMakeLists.txt`, and
  **that assertion alone** — the form `src/ubundle/CMakeLists.txt` already uses
  for that item's INV-10.
  *Breaks when:* a shading path needs a `umat` helper and someone links it
  rather than moving the helper, putting the package reader into `ut-ants`.
  **There is deliberately no C++ test for this.** A compiled Catch2 case cannot
  observe a CMake link set, and `uta_unit_tests` already links `uta_upkg`,
  `uta_umat` and `uta_ubake` itself — so a runtime check inside it would pass
  identically whether or not `uta_urender` had an edge to any of them. That is a
  test that cannot fail, which § 3 decision 6 exists to refuse.

- **INV-2** — no header of `uta_urender` that another subsystem may include
  declares, takes or returns a Vulkan type: a translation unit including every
  such header compiles with no Vulkan headers on its include path.
  *Test:* `tests/unit/RenderHeaderIsolationTest.cpp`, **in a target of its own**
  that links `uta_urender` and not `Vulkan::Vulkan`. It cannot live in
  `uta_unit_tests`: INV-3's fixtures need Vulkan's feature structs, so that
  binary has the Vulkan include path and this test could never fail inside it.
  *Breaks when:* a `VkDevice`, `VkFormat` or `VkSurfaceKHR` reaches a public
  signature, after which `ugame` cannot compile without a Vulkan loader
  installed. § 4.3's `Config::surface` is the field that invites it, which is
  why it is an integer there.
  **This is deliberately not the link-closure assertion `docs/design.md` rule 2
  describes.** That rule names `ut-ants-server`, and no such target exists:
  `rg -n add_executable` over this tree returns `ut-bake`, `ut-dump`,
  `ut-paths` and the two test binaries, and nothing else. An invariant about a
  program that does not exist could not fail, which is the tautology § 5's own
  rules forbid. § 10 carries the gap and § 11 the document it belongs to.

- **INV-3** — device selection rejects a device lacking any § 4.4 requirement,
  names the first requirement it failed, and never selects a device that fails
  one. *Test:* `tests/unit/RenderDeviceTest.cpp`, with a table of synthetic
  feature sets each missing one requirement.
  *Breaks when:* a requirement is checked by extension string rather than
  feature bit, so a driver advertising `VK_KHR_dynamic_rendering` without the
  usable feature is selected and every draw is undefined.

- **INV-4** — the surfaceless path creates no surface and no swapchain: with no
  display and no surface extension, `create`, `draw` and `readback` succeed and
  the pixels are the ones drawn. *Test:* `tests/device/RenderOffscreenTest.cpp`,
  label `device`, run with `DISPLAY` and `WAYLAND_DISPLAY` unset. Measured
  ahead of the code by this session's scratch probe → `centre rgba = 0 255 0
  255`, `corner rgba = 255 0 0 255`, exit 0.
  *Breaks when:* the swapchain or the `VK_KHR_swapchain` device extension is
  requested unconditionally, which fails before the first draw on a machine
  with no display — the state DOOM-0268 is stuck in.

- **INV-5** — a test labelled `device` run where no Vulkan device qualifies
  **fails**, naming the missing requirement. It never skips and never passes.
  *Test:* `tests/device/RenderDeviceAbsentTest.cpp`, run with the driver search
  path pointed at a file that does not exist. Measured on the scratch probe →
  `FAIL vkCreateInstance(...) -> -9`, exit 1, where `-9` is
  `VK_ERROR_INCOMPATIBLE_DRIVER`.
  *Breaks when:* the harness treats an absent device as a skip, so a CI leg
  that lost `mesa-vulkan-drivers` reports green over a renderer nothing
  executed — the shape `UTA-0098` records for the real-asset ring checks.
  **`RenderDeviceAbsentTest` is the one test this rule does not govern, and it
  carries the label `device-absent` instead.** It is the test that asserts the
  refusal, so it is *run* with no device on purpose and must PASS there — under
  the `device` label it would have to fail to satisfy the rule it exists to
  grade, and the only escape would be weakening the rule to a skip for exactly
  one test. The two labels are what keep the rule absolute.

- **INV-6** — the shading pass's direct light equals `ubake::lightAt` within
  the tolerance § 4.6 fixes, over a case table covering each light `effect` the
  model distinguishes — `LE_None`, `LE_StaticSpot` (8), `LE_Spotlight` (12) and
  `LE_NonIncidence` (13) — and the `d == 0` case § 4.3 singles out.
  *Test:* `tests/device/RenderLightParityTest.cpp`, label `device`, which links
  `uta_ubake` and dispatches a compute shader that `#include`s the same
  `light.glsl` the shading pass does.
  *Breaks when:* a term is written in the shader with the sign or the order
  UTA-0112 § 4.3 does not give — a spot factor without its `clamp`, say — which
  the isolation makes visible: only the light model differs between the two
  sides, so a mismatch cannot be blamed on the draw path. This is the fixture
  the rule under test is the only one able to reject.

- **INV-7** — the indirect term is § 4.7's ambient-cube sum, a point inside the
  lattice is the trilinear blend of the corners present, and a point with no
  corner present receives zero indirect light.
  *Test:* `tests/device/RenderProbeTest.cpp`, label `device`, dispatching a
  compute shader that `#include`s the same `probes.glsl` the shading pass does —
  the shape INV-6 uses for `light.glsl`, and for the same reason.
  *Breaks when:* a face is selected by the wrong sign of `n`, which a symmetric
  probe cube hides; the fixture therefore uses six distinct face colours, so
  every wrong selection changes the answer.
  **This is deliberately not a device-free test.** § 4.7's evaluation is
  per-pixel shader arithmetic, so grading it on the CPU would mean grading a
  second, C++ copy of it — which § 3 decision 5 forbids and which INV-6 was made
  a device test to avoid. A copy that passed while the shipped GLSL selected the
  wrong face is exactly the failure both rules exist for.

- **INV-8** — a mover's pivot-space point is placed at UTA-0119 § 4.5's
  `location + postScale ⊙ (Y · P · R · q)`.
  *Test:* `tests/unit/RenderMoverPlacementTest.cpp`, device-free, graded
  against `tests/support/FCoordsPort.h` — the port UTA-0119's own INV-7 grades
  its formula against.
  *Breaks when:* the rotations are composed in another order, or `postScale` is
  applied before the rotation instead of after. Both produce the same answer
  for an unrotated, unscaled mover, so the fixture uses a non-zero pitch, yaw
  and roll together with a non-uniform `postScale`.

- **INV-9** — every struct shared with a shader has a `static_assert` on its
  total size and on each field's offset.
  *Test:* the assertions themselves, in the header declaring each struct; a
  breach is a compile error on every leg of the matrix, including MSVC.
  *Breaks when:* a field is added or widened without adjusting the padding, so
  every field after it shifts and the shader reads the wrong bytes — latent
  across two features in the sibling project § 4.12 cites, and a device-lost
  rather than a wrong pixel.

- **INV-10** — the sRGB transfer is applied exactly once in each direction: with
  `Config::linearOutput` set, a base-colour texel sampled and written straight
  out, with no lighting, comes back with the value it was stored with.
  *Test:* `tests/device/RenderColourTransferTest.cpp`, label `device`: upload a
  known BC7 base colour, draw it unlit with `linearOutput`, read the pixel back
  and compare it with the literal it was stored as — § 4.10 fixes the target's
  format and `readback`'s channel order, so the comparison has a fixed answer.
  **`linearOutput` is load-bearing here, not a convenience:** without it
  exposure and the Khronos PBR Neutral tone map sit between the two ends, and
  neither is the identity, so the invariant would fail against a correct
  implementation.
  *Breaks when:* an `_SRGB` sampled format is paired with a `UNORM` output, or
  a `UNORM` sampled format with an `_SRGB` output. The first darkens the image
  and the second washes it out, and **both look plausible in a screenshot** —
  which is why the test compares a value rather than a human comparing images.

- **INV-11** — a `PF_Masked` batch cuts out below the threshold; a
  `PF_TwoSided` batch renders from both sides; a `PF_Translucent` batch writes
  no motion vector; a `PF_Portal` batch draws nothing; and a batch carrying a
  bit § 4.5's table does not name renders exactly as it would without it.
  *Test:* `tests/device/RenderSurfaceFlagsTest.cpp`, label `device`, one batch
  per flag plus one carrying `PF_Modulated`, which must be ignored.
  *Breaks when:* the flag word is compared for equality rather than tested bit
  by bit, so a surface carrying `PF_Masked | PF_TwoSided` matches neither case
  and silently renders as opaque and single-sided.

## 6. Failure modes

- **No qualifying device.** § 4.4 refuses with the missing requirement named.
  `ut-ants` cannot start; that is `docs/design.md`'s stated position, not a
  degradation to recover from.
- **`glslc` absent at configure time.** `find_package(Vulkan REQUIRED
  COMPONENTS glslc)` fails the configure with a message naming it. It is a
  route-3 prerequisite the README must state, and ADR-0007 already admits
  nothing checks the validation layers alongside it.
- **The validation layer is not installed.** The layer is requested only when
  present, and its absence is **logged at startup**, because a sibling project
  recorded an invariant going unexercised for exactly this reason (*"validation
  layer not installed on the dev box this run, so INV-8 unexercised here"*). A
  silent absence turns a validation-clean claim into an unfalsifiable one.
- **A cluster overflows its light cap.** The lights furthest from the cluster
  centre are dropped and the frame records how many clusters overflowed, so the
  cap can be judged against a real map rather than guessed. Lighting degrades;
  nothing fails.
- **The shadow atlas cannot hold the frame's shadowing lights.** Lights are
  admitted in descending projected size until the atlas is full; the rest are
  lit without shadows. Recorded per frame like the cluster overflow.
- **A `MATS` id with no `TEXS` maps.** `ubundle` does not check the pairing
  (`UTA-0011` INV-17 leaves it to the baker), so the renderer must: the default
  material stands in, and the id is logged once rather than per frame.
- **A bundle with no `GEOM`, no `LITE` or no `LPRB` section.** Each is
  `std::optional` and absent is distinct from empty
  (`src/ubundle/Bundle.h`). No geometry draws an empty frame; no lights draws
  unlit geometry; no probes gives zero indirect everywhere, which is § 4.7's
  no-probe rule applied uniformly rather than a special case.
- **A mover with a zero `postScale`.** `ubundle` passes the floats through
  unchecked and says so, calling a zero *"the renderer's"*. Its triangles
  collapse to a plane; the frame is still drawn.
- **The CI driver package stops being installed.** INV-5 turns this into a red
  leg rather than a green one over an unexecuted renderer.

## 7. Tests

`tests/unit/` already exists and is registered as `uta_unit_tests` with
`catch_discover_tests(... LABELS "unit;fast" TIMEOUT 30)`. This item adds
`tests/device/` as `uta_device_tests`, and one further target holding INV-2's
test alone, which is the only way that test can fail.

| Invariant | Test | Target | Label | Registered when |
|---|---|---|---|---|
| INV-1 | the assertion in `src/urender/CMakeLists.txt` | — | — | every configure, every leg |
| INV-9 | the `static_assert`s in each shared struct's header | — | — | every compile, every leg |
| INV-2 | `tests/unit/RenderHeaderIsolationTest.cpp` | its own, **no** `Vulkan::Vulkan` | `unit;fast` | always |
| INV-3 | `tests/unit/RenderDeviceTest.cpp` | `uta_unit_tests` | `unit;fast` | always |
| INV-8 | `tests/unit/RenderMoverPlacementTest.cpp` | `uta_unit_tests` | `unit;fast` | always |
| INV-4 | `tests/device/RenderOffscreenTest.cpp` | `uta_device_tests` | `device` | unconditionally, wherever `uta_urender` builds |
| INV-6 | `tests/device/RenderLightParityTest.cpp` | `uta_device_tests` | `device` | unconditionally, wherever `uta_urender` builds |
| INV-7 | `tests/device/RenderProbeTest.cpp` | `uta_device_tests` | `device` | unconditionally, wherever `uta_urender` builds |
| INV-10 | `tests/device/RenderColourTransferTest.cpp` | `uta_device_tests` | `device` | unconditionally, wherever `uta_urender` builds |
| INV-11 | `tests/device/RenderSurfaceFlagsTest.cpp` | `uta_device_tests` | `device` | unconditionally, wherever `uta_urender` builds |
| INV-5 | `tests/device/RenderDeviceAbsentTest.cpp` | `uta_device_tests` | `device-absent` | unconditionally, wherever `uta_urender` builds |

**Registration is never guarded on finding a Vulkan loader, and that is the
point.** A `Vulkan_FOUND` guard would mean a leg that lost `libvulkan-dev`
registered no device test and reported green over a renderer nothing executed —
the defect § 3 decision 6 and INV-5 exist to forbid, reintroduced by the
harness. The loader and `glslc` are missing at *configure* time, where § 4.2's
`REQUIRED` find already stops the build; only a missing **driver** gets as far
as a test, and INV-5 is what reds the leg for it.

**So the tiers differ in what they need, not in whether they are registered.**
On the MSVC leg the `device` and `device-absent` tests are registered and
`device` is deselected by label, because no driver is installed there — a
deliberate, visible exclusion rather than a target that silently vanished.

**`scripts/ci.sh` is what deselects them, and it does not do so today.** That
script is the whole gate (`CLAUDE.md` § Build and test), and its test step is
`ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure` — **no `-L`**,
so every registered test runs on every leg. Left alone it would run the `device`
tier on MSVC with no driver, which INV-5 requires to fail, leaving that leg
permanently red. So this item changes that step to select labels per platform:
`unit` and `device-absent` everywhere, `device` added where a driver is
installed. § 11 names the file, because a reader who changes only the workflow
would miss it.

**Each test is seen to fail before the code exists, and the two INV-4 and INV-5
fixtures already have their failing and passing runs recorded** — § 4.3 and
INV-5 carry the outputs, measured on a scratch probe rather than on this
library. That is evidence the *fixture* discriminates; it is not evidence about
`uta_urender`, which does not exist yet.

`RenderLightParityTest.cpp` links a build-time library beside a runtime one
(`uta_ubake` with `uta_urender`). That is already ordinary here —
`uta_unit_tests` links `uta_upkg`, `uta_umat` and `uta_ubake` alongside
`uta_ubundle` — because `docs/design.md` rule 2 is about the two runtime
**programs** and does not reach a test binary. `UTA-0112` § 4.9 asks for
exactly this: *"A test of UTA-0014's, which may link both"*.

`./scripts/mutation-probe.py` takes `ubundle` as its only subject, so every
invariant here is mutated by hand, per `CLAUDE.md` § Build and test. The
mutations worth running first are the ones INV-6, INV-8 and INV-10 name as
their breaking states, each of which produces a plausible image.

## 8. Alternatives considered (and rejected)

- **Clustered deferred instead of clustered forward.** *User, 2026-09-12.*
  Scales better at very high light counts and is the usual choice in a large
  engine. It lost because UT99 carries translucent and masked surfaces as
  ordinary map geometry, which a G-buffer cannot shade — so the forward pass
  gets built anyway, and two shading paths is the thing this item can least
  afford on its first frame.
- **SwiftShader on the Windows leg, so device tests run on both platforms.**
  *User, 2026-09-12.* It would keep `docs/design.md`'s both-platforms promise
  intact. It lost on acquisition and on signal: ADR-0007's question list has no
  route for an interchangeable driver — it is not linked, not fetched for one
  target, and not version-matched to the machine — and SwiftShader's feature
  coverage is narrower than lavapipe's, so a red Windows leg would sometimes be
  SwiftShader's rather than ours. Revisit if the Windows-only gap ever hides a
  defect.
- **No device tests at all; grade the draw path by hand on a real GPU.**
  *User, 2026-09-12.* Cheapest to set up and it lost outright: it is the
  arrangement `UTA-0098` and `docs/build-and-test-lessons.md` each record
  producing a green result over something nothing examined.
- **A C++ transcription of the light model, tested against `ubake`, with the
  shader trusted to match.** Rejected. It is the pattern the sibling project
  uses and it works there —
  `/mnt/Games/Scripts/Linux/DOOM_Ants/linuxdoom-1.10/tests/nee_sampling_test.cpp`
  ports the shader's hash *"verbatim from `pathtrace.comp`"* to prove the GPU
  selection unbiased on the CPU. But a verbatim port is a second copy, and this
  project has three records of a second copy drifting from the first
  (`CLAUDE.md` § How work is done here: *"A rule restated in two places is two
  rules that will disagree"*). Running the real shader costs a device, which
  § 3 decision 1 has now bought.
- **One file compiled as both C++ and GLSL, via a compatibility shim.**
  Rejected as over-engineering for the gain: it removes the copy the option
  above was rejected for, but a shim supplying `vec3`, swizzles and
  constructors is a second dialect to maintain, and running the shader already
  removes the copy.
- **Render passes and framebuffers instead of dynamic rendering.** Rejected.
  `docs/design.md` sets the floor at Vulkan 1.3, where `dynamicRendering` is
  core; the objection in the wider literature is that it is *"not yet available
  on every platform"*, which a 1.3 floor has already ruled out. Measured
  present on the CPU driver CI will use (§ 4.4).
- **Vulkan Memory Allocator for allocation.** Deferred rather than rejected,
  and the evidence is a sibling project's: it shipped a hardware path tracer on
  a hand-rolled sub-allocator and logged the VMA swap as debt for months
  (`DOOM-0058`, still `📋`), while its own spec text went on describing VMA as
  shipped. This item allocates per resource class with a documented manual
  sub-allocator and does **not** claim VMA anywhere, which is the half that
  project got wrong.
- **`volk` instead of linking the loader.** Rejected for now: it buys
  call-overhead reduction and extension loading this item does not need, and
  `find_package(Vulkan)` is the route ADR-0007 already prescribes.

## 9. Out of scope

- Creating the window and its `VkSurfaceKHR`, and moving a camera through a
  level — tracked by `UTA-0016`. **The swapchain itself is in scope here**, over
  the surface that item supplies; what is deferred is who creates the window.
- Choosing and integrating an upscaler, and the negative mip bias § 4.11
  records — tracked by `UTA-0075` and `UTA-0076`.
- Volumetric fog, light shafts, ambient occlusion and the flashlight —
  `UTA-0015`. Bloom, colour grading, anti-aliasing and sharpening —
  `UTA-0053`. Parallax occlusion — `UTA-0040`. Subsurface scattering —
  `UTA-0044`. Screen-space reflections — `UTA-0045`. Quality tiers and dynamic
  resolution — `UTA-0051`. Detail normals, dithered alpha, contact shadows and
  interior windows — `UTA-0054`. Vertex-animated banners, flags and water —
  `UTA-0055`. Water and glass — `UTA-0089`.
- **`ZoneInfo`'s `AmbientBrightness`, `AmbientHue` and `AmbientSaturation` —
  deferred; not yet queued.** `UTA-0112` § Out of scope hands them here, and
  § 2 consequence 2 shows why they cannot be taken: no bundle section carries
  them, and the renderer may not read a package. Applying them needs `ubake` to
  write them and `ubundle` to carry them — a format version bump — before this
  item has anything to read. Naming a `UTA-` id here would be a false pointer.
- **`PF_Modulated`, `PF_Environment`, `PF_Mirrored`, `PF_NoSmooth` and
  `PF_SpecialLit` — deferred; not yet queued.** Each has a real UT99 meaning
  and § 4.5 ignores all five deliberately. `PF_Mirrored` and `PF_Environment`
  are reflection surfaces and belong with `UTA-0045` or `UTA-0089` when either
  is taken; the other three have no home yet.
- **`Light::actorShadows`, `Light::corona`, `Light::lensFlare`,
  `Light::volumeBrightness`, `Light::volumeRadius` and `Light::volumeFog` —
  `UTA-0015`** carries the volumetric three. Coronas and lens flares are
  deferred; not yet queued. `Light::specialLit`, which pairs with
  `PF_SpecialLit`, goes with that flag above.
- **Executing the `device` tier on Windows — deferred; not yet queued.** This is
  the gap § 3 decision 1 and § 4.12 both point at, recorded here so it is a
  decision rather than something a reader discovers. The MSVC leg configures,
  compiles, and registers those tests; what it does not do is run them, no
  software Vulkan driver being installed there. Closing it means either a
  driver on that runner or `UTA-0039`'s own hardware, and § 8 carries the
  SwiftShader option that was weighed and rejected.
- Deciding which Unreal Tournament versions are accepted — `UTA-0117`.
- A benchmark that says where the renderer's time goes — `UTA-0129`. This item
  states no frame-rate target; `UTA-0039` owns the floor.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | the configure-time assertion in `src/urender/CMakeLists.txt`, and nothing else — a C++ test cannot see a link set |
| INV-2 | `tests/unit/RenderHeaderIsolationTest.cpp` |
| `docs/design.md` rule 2's closure assertion — `ut-ants-server` links no `urender` | **nothing** — no `ut-ants-server` target exists, so there is no closure to walk. It becomes gradeable with the item that creates that program; INV-2 covers the part that is gradeable now |
| INV-3 | `tests/unit/RenderDeviceTest.cpp` |
| INV-4 | `tests/device/RenderOffscreenTest.cpp` — registered everywhere, **executed on the Linux legs only**, the MSVC leg having no driver installed (§ 3 decision 1, § 7) |
| INV-5 | `tests/device/RenderDeviceAbsentTest.cpp`, label `device-absent` — this one runs on **every** leg, needing no driver by construction |
| INV-6 | `tests/device/RenderLightParityTest.cpp` — same platform limit as INV-4 |
| INV-7 | `tests/device/RenderProbeTest.cpp` — same platform limit as INV-4; device-free was impossible without a forbidden C++ copy of § 4.7 |
| INV-8 | `tests/unit/RenderMoverPlacementTest.cpp` |
| INV-9 | **Partial:** the `static_assert`s catch a struct that CHANGES — a breach is a compile error on every leg. Nothing catches a struct that is added to the shader interface and never given one, so the invariant's coverage grows only as carefully as the next author is |
| INV-10 | `tests/device/RenderColourTransferTest.cpp` — same platform limit |
| INV-11 | `tests/device/RenderSurfaceFlagsTest.cpp` — same platform limit; the velocity half is observable only because § 4.3's `readback` takes a `Target` |
| § 4.2's hand-written shader `#include` edges | **nothing** — `glslc` emits no dependency information, so a stale shader after editing an included file is silent. A sibling project states the same rule and checks it the same way, which is not at all |
| § 4.9's flicker scalar shape | **nothing** — deliberately unpinned; nothing binds to it |
| § 4.10's fixed exposure value | **Partial:** INV-10 fixes the transfer at both ends but not the exposure constant between them; a wrong constant is a uniformly dark or bright image no test here rejects |
| § 4.6's clustered culling correctness | **Partial:** `tests/unit/` grades cluster assignment; that a shaded pixel used its own cluster's list is not graded, and a cull that drops a light the pixel needed looks like a dim room |
| § 4.8's cached shadow tiles being invalidated when a mover enters a static light's radius | **nothing** — tracked as part of `UTA-0016`'s first real level, where a stale tile is visible. No test here builds a moving mover |
| `libvulkan-dev` and `glslc` staying installed | § 4.2's `REQUIRED` find — their absence fails the **configure**, before any test runs |
| `mesa-vulkan-drivers` staying installed | INV-5, which turns its absence into a red leg rather than an empty test run |
| The presenting path — swapchain format, present mode, resize | **nothing** in CI, by § 3 decision 1. Graded by hand under `UTA-0016` |

## 11. Cross-doc impact

- **`.github/workflows/ci.yml`** — **both** platforms change, and the Windows
  half is the one easy to miss. The Linux toolchain step gains `libvulkan-dev`,
  `glslc` and `mesa-vulkan-drivers`. The Windows job gains a Vulkan SDK install
  step, because § 4.2's find is `REQUIRED` at configure time on every leg that
  builds `uta_urender`; without it the MSVC leg fails before compiling
  anything. § 4.12 carries both.
- **`scripts/ci.sh`** — the test step gains per-platform label selection;
  § 4.12 says why. **This is the file a reader is most likely to miss**: it is
  the whole gate, its `ctest` call carries no `-L` today, and changing only
  `ci.yml` leaves the MSVC leg running the `device` tier with no driver.
- **`docs/design.md`** — § The stack's `shaderc` row reads *"From the Vulkan
  SDK"* while `ADR-0007` § Decision settles the third route-3 input as
  `glslc`, naming `shaderc` only as the route-2 branch it would take *"if
  nothing already supplied `glslc`"*. This item uses `glslc`, so the table's
  row is the stale one. Correcting it is a one-word edit to a row that is an
  index into the ADR, and `CLAUDE.md` rule 14's test decides whether it gates.
- **`README.md`** — the Vulkan SDK and the validation layers are a
  prerequisite on both platforms; `docs/design.md` § The stack already says the
  README has to state it, and this is the item that makes it true.
- **`CLAUDE.md`** — § Build and test gains the `device` label and how to run it
  headlessly; § Where this project is advances `Next:`.
- **`docs/specs/UTA-0112-baked-light-probes.md`** — its § 4.9 promises this
  spec exists. No edit is needed; the pointer now resolves.
- **`docs/design.md` rule 2 states a test that does not exist**, in the present
  tense: *"A test asserts the link closure of `ut-ants` and `ut-ants-server`
  contains none of the three"*, and *"The same test asserts
  `ut-ants-server`'s closure contains no `urender`, `uaudio` or `uui`."*
  Neither program is a target — `rg -n add_executable` over this tree returns
  `ut-bake`, `ut-dump`, `ut-paths` and the two test binaries. This item cannot
  fix that: it creates neither program. Recorded here because INV-2 was
  drafted *as* that test before the tree was checked, and the next spec to
  reach for it deserves the warning rather than the same correction.
- **`CHANGELOG.md`** — an `Added` entry when the code lands, not now.
- **`docs/standards/versioning-overrides.md`** — unread by this item. Nothing
  here adds a versioned public surface: `uta_urender` is linked by programs in
  this repository only.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0014-vulkan-draw-path-loop-log.md`.
