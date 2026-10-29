// urender -- draws a baked bundle with Vulkan 1.3.
//
// docs/specs/UTA-0014-vulkan-draw-path.md.
//
// THIS IS THE ONE HEADER ANOTHER SUBSYSTEM INCLUDES, AND NO VULKAN OR glm TYPE
// MAY APPEAR IN IT -- INV-2. That is what lets a program compile against
// urender's callers without a Vulkan loader installed, and it is why
// `Config::createSurface` passes the instance and the surface as integers and
// `readback` names its target with a plain enum. tests/unit/RenderHeaderIsolationTest.cpp compiles this header in a
// target that does not link Vulkan and refuses it if the Vulkan headers arrive.
//
// THE SURFACELESS PATH IS THE PRIMARY ONE (SS 4.3). With no surface the
// renderer creates no swapchain, requests no instance or device extension and
// needs no display, which is what lets the draw path be graded on a machine
// with no graphics card.
//
// It reads a ubundle::Bundle and a Camera and nothing else -- never uworld
// (SS 3 decision 3), never a window (SS 3 decision 4).

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uta::urender {

/// A quality tier -- docs/specs/UTA-0051-quality-tiers.md SS 4.1.
enum class Tier : std::uint8_t { Low = 0, Medium = 1, High = 2, Ultra = 3 };

/// "low", "medium", "high" or "ultra", case-insensitive; empty otherwise.
[[nodiscard]] std::optional<Tier> tierNamed(std::string_view name) noexcept;
/// The tier's name, lower case.
[[nodiscard]] std::string_view tierName(Tier tier) noexcept;

/// What the caller supplies.
struct Config {
    /// The instance extensions the caller's window needs -- what SDL3's
    /// SDL_Vulkan_GetInstanceExtensions returns.
    std::vector<std::string> instanceExtensions;
    /// Called once, with the renderer's own VkInstance cast to an integer,
    /// after that instance exists and before a device is chosen. Returns the
    /// caller's VkSurfaceKHR made from it, cast, or 0 to refuse. The renderer
    /// destroys the surface, before its instance; the caller's window must
    /// outlive the Renderer.
    ///
    /// SET SELECTS THE PRESENTING PATH, UNSET THE SURFACELESS ONE: no instance
    /// extension, no surface, no swapchain, and no VK_KHR_swapchain asked for.
    /// `create` refuses a Config whose instanceExtensions is empty while this
    /// is set, or non-empty while it is unset.
    std::function<std::uint64_t(std::uint64_t instance)> createSurface;
    std::uint32_t width = 0, height = 0; ///< the target's size in pixels; `resize` changes it
    bool validation = false;             ///< request the layer if installed
    /// UTA-0138: with `validation`, fail the next GPU submission once the layer
    /// has reported any error, naming the first. The device tests set it; the
    /// viewer's --validation only logs.
    bool failOnValidationError = false;
    /// Skip exposure and tone mapping, writing linear light to the target
    /// instead. It exists so INV-10 can compare a pixel against a literal --
    /// SS 4.10 says why nothing else can -- and it changes no other stage.
    bool linearOutput = false;
    /// UTA-0051 SS 4.3: the tier to draw at. Unset: chosen from the device.
    std::optional<Tier> tier;
    /// UTA-0051 SS 4.4: lower the render scale to hold 60 frames a second.
    bool dynamicResolution = false;
    /// Fixes the render scale, with or without `dynamicResolution`, clamped to
    /// the tier's floor and to 1. For tests and diagnosis; unset in normal play.
    std::optional<double> fixedRenderScale;
    /// UTA-0015 SS 4.5: scales the haze's extinction and scattering. For tests
    /// and diagnosis; 1 in normal play. `create` refuses a negative or
    /// non-finite value with InvalidArgument.
    float hazeScale = 1;
    /// UTA-0177: draw a material the bake could not make in magenta, the
    /// conventional colour of something missing, instead of neutral grey. For
    /// developer views and tests; unset in normal play, where a player sees grey.
    bool showMissingMaterials = false;
    /// UTA-0292 SS 4.3: a lit surface writes its light to the emission target
    /// instead of its emission -- red the direct term, green the indirect, each
    /// a luma, as scene.frag adds them before reflectance -- and bloom is off.
    /// For ut-shot --light-terms; unset in normal play, where it compiles out.
    bool lightTerms = false;
    /// UTA-0352: keep each pipeline's compiler statistics, for
    /// Renderer::shaderStatistics, where the device offers
    /// VK_KHR_pipeline_executable_properties. For ut-bench; unset in normal
    /// play, which enables no extension for it.
    bool shaderStatistics = false;
};

/// UTA-0352: what the driver's compiler reports for one stage of one of the
/// renderer's pipelines, as the driver names it -- RADV's "VGPRs" and
/// "Subgroups per SIMD", say. `pipeline` is the renderer's own name for it.
/// Each value is the number written out exactly: a 64-bit hash would not
/// survive a double.
struct ShaderStatistics {
    std::string pipeline;
    std::string stage;
    std::vector<std::pair<std::string, std::string>> values;
};

/// The view a frame is drawn from -- UT99's own units and angle encoding, so a
/// caller holding a `Placement` or a `MoverShape` already has both fields in
/// the right form.
struct Camera {
    std::array<float, 3> location{};
    std::array<std::int32_t, 3> rotation{}; ///< pitch, yaw, roll; 65536 to a turn
    float verticalFovDegrees = 90;
    /// UTA-0285: far enough for the diagonal of UT's world, 65536 a side, as
    /// UT99 draws at any distance. Depth precision is the near plane's, so
    /// this costs none.
    float nearPlane = 1, farPlane = 131072;
    bool flashlight = false; ///< UTA-0015 SS 4.5: a spotlight from the eye
};

/// UTA-0351: the steps of a frame the GPU's clock times, in the order drawn.
/// Fires is UTA-0286's upload of the replayed fire pictures; Shadows holds
/// UTA-0333's copy of the tiles into the haze's atlas; Depth is UTA-0260's
/// pre-pass; Flames UTA-0263's flames and UTA-0215's water specks; Post the
/// output stage, upscaling included. Only the blit out is in none. UTA-0352
/// split the old "scene" step into Depth, Opaque, Translucent and Flames.
enum class GpuPass : std::uint8_t { Fires, Shadows, Clusters, Fog, Depth, Opaque, Translucent, Flames, Bloom, Post };
inline constexpr std::size_t GPU_PASS_COUNT = 10;
/// Each step's name, lower case, indexed by GpuPass.
inline constexpr std::array<std::string_view, GPU_PASS_COUNT> GPU_PASS_NAMES{
    "fires", "shadows", "clusters", "fog", "depth", "opaque", "translucent", "flames", "bloom", "post"};

/// What the last frame had to give up -- SS 6. Lighting and shadows degrade
/// rather than fail, and these say by how much, so the caps can be judged
/// against a real map rather than guessed.
struct FrameStats {
    std::uint32_t overflowedClusters = 0; ///< clusters that dropped lights past their cap
    std::uint32_t unshadowedLights = 0;   ///< shadowing lights the atlas could not hold
    /// Shadow tiles drawn this frame. SS 4.8 keeps a still light's tiles, so a
    /// still camera over a still level draws none after its first frame.
    std::uint32_t renderedShadowTiles = 0;
    Tier tier = Tier::Low;        ///< UTA-0051: the tier in use
    double renderScale = 1;       ///< the scale this frame was drawn at
    double frameMilliseconds = 0; ///< the wall time of this frame's GPU work (UTA-0051 SS 4.4)
    /// UTA-0352: the host's own steps, wall time. `prepareMilliseconds` is
    /// draw's work before the GPU is given the frame -- the bundle check, the
    /// lights, the shadow plan, the fires' advance, the buffer writes -- which
    /// frameMilliseconds leaves out. `recordMilliseconds` is the recording of
    /// the frame's commands, which frameMilliseconds holds.
    double prepareMilliseconds = 0;
    double recordMilliseconds = 0;
    std::uint32_t droppedVolumeLights = 0; ///< UTA-0015: glowing lights past VOLUME_LIGHT_CAPACITY
    /// UTA-0351: each step's GPU time, indexed by GpuPass, read from the GPU's
    /// own clock. A step the frame skipped takes about none. All zero, and
    /// `passesTimed` false, on a queue that keeps no timestamps.
    std::array<double, GPU_PASS_COUNT> passMilliseconds{};
    bool passesTimed = false;
};

class Renderer {
public:
    [[nodiscard]] static Result<Renderer> create(const Config& config);

    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();

    /// Draw `bundle` from `camera`.
    [[nodiscard]] Result<void> draw(const ubundle::Bundle& bundle, const Camera& camera);

    /// Which target `readback` copies. A plain enum, so no Vulkan type reaches
    /// this header (INV-2) and a test can name a target without holding one.
    enum class Target { Colour, Velocity, Emission };

    /// Copy the last frame's `target` into host memory, tightly packed: RGBA8
    /// for Colour, two floats per pixel for Velocity, and four for UTA-0053's
    /// Emission, the bloom's source (UTA-0263 INV-7 reads it).
    /// Velocity and Emission are refused after a frame drawn below scale 1,
    /// whose region is smaller than the target (UTA-0051 SS 4.4).
    ///
    /// **Both paths.** This read the surfaceless path's frames only until
    /// UTA-0191, and the restriction was never a limit of the hardware: the
    /// presenting path draws into the same colour target and `Swapchain::
    /// recordBlit` blits THAT into the acquired swapchain image, so the pixels
    /// are here on both paths. Both also submit and wait before presenting
    /// (Present.cpp's own header says so), so the frame is finished by the time
    /// this is called and the copy needs no synchronisation of its own.
    /// UTA-0191's capture folder is what needed the presented frame back.
    [[nodiscard]] Result<std::vector<std::byte>> readback(Target target = Target::Colour);

    /// The last frame's SS 6 counts.
    [[nodiscard]] FrameStats lastFrameStats() const noexcept;

    /// UTA-0075's sub-pixel jitter (SS 4.11 provision 1). OFF until a pass
    /// consumes it: with no temporal resolve to average it away, a jittered
    /// frame is a frame that shimmers.
    void setJitter(bool enabled) noexcept;

    /// The next frame follows no earlier one: it shows no camera motion, and
    /// UTA-0356's fog keeps no history from before it, so it draws as a single
    /// frame does. Call it where the camera jumps to an unrelated view.
    void cut() noexcept;

    /// Skip exposure and the tone map from the next frame, as
    /// `Config::linearOutput` does from `create` (SS 4.10). Settable per frame
    /// because the flag is a push constant rather than a pipeline choice, so
    /// one renderer can draw a presented frame and then the same view
    /// unmapped -- which is how UTA-0191's capture folder holds both without
    /// standing a second device up.
    void setLinearOutput(bool enabled) noexcept;

    /// The light time the last frame was drawn at, in seconds -- SS 4.9's
    /// clock, which a flickering light's phase is measured from. Zero before
    /// any frame is drawn.
    [[nodiscard]] double lightSeconds() const noexcept;

    /// UTA-0129: the device's own name for itself. A frame time with no device
    /// named cannot be compared with another.
    [[nodiscard]] std::string deviceName() const;

    /// UTA-0352: every stage of every pipeline's compiler statistics, with
    /// Config::shaderStatistics set on a device that offers them; otherwise
    /// none. RADV's register count decides how many waves a stage runs, which
    /// moved the scene step by 3 ms (UTA-0352, 2026-10-14).
    [[nodiscard]] std::vector<ShaderStatistics> shaderStatistics() const;

    /// UTA-0138: whether the Vulkan validation layer is loaded and reporting.
    /// False when it was not asked for, or asked for and not installed.
    [[nodiscard]] bool validating() const noexcept;

    /// Draw every later frame at `seconds` instead of reading the clock.
    ///
    /// Two frames of the same view are otherwise drawn at different times, so
    /// on a map carrying an LT_PULSE or LT_SUBTLE_PULSE light they disagree for
    /// a reason that has nothing to do with what is being compared. UTA-0191
    /// pins the second of its two draws to the first's time for exactly that.
    void pinLightSeconds(double seconds) noexcept;

    /// Back to the clock, from the next frame.
    void unpinLightSeconds() noexcept;

    /// Draw every later frame at `scale`, clamped as `fixedRenderScale` is,
    /// and leave dynamic resolution's controller as it stands. A fixed scale
    /// still wins, and without dynamic resolution the pin does nothing: there
    /// is no upscale pass to draw a reduced region through (UTA-0440).
    /// UTA-0191's second draw pins the first's scale with it, or the two
    /// differ in sharpness for no reason of their own (UTA-0372).
    void pinRenderScale(double scale) noexcept;

    /// Back to the controller, or to full scale, from the next frame.
    void unpinRenderScale() noexcept;

    /// The target's new size in pixels -- the window's drawable size, not its
    /// size in window units. The next `draw` rebuilds every render target at
    /// it, and `readback` is refused until that frame is drawn. A zero width or
    /// height is refused with InvalidArgument.
    [[nodiscard]] Result<void> resize(std::uint32_t width, std::uint32_t height);

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace uta::urender
