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
};

/// The view a frame is drawn from -- UT99's own units and angle encoding, so a
/// caller holding a `Placement` or a `MoverShape` already has both fields in
/// the right form.
struct Camera {
    std::array<float, 3> location{};
    std::array<std::int32_t, 3> rotation{}; ///< pitch, yaw, roll; 65536 to a turn
    float verticalFovDegrees = 90;
    float nearPlane = 1, farPlane = 32768;
    bool flashlight = false; ///< UTA-0015 SS 4.5: a spotlight from the eye
};

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
    std::uint32_t droppedVolumeLights = 0; ///< UTA-0015: glowing lights past VOLUME_LIGHT_CAPACITY
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
    enum class Target { Colour, Velocity };

    /// Copy the last frame's `target` into host memory, tightly packed: RGBA8
    /// for Colour, two floats per pixel for Velocity. Surfaceless path only --
    /// the presenting path's frames go to the swapchain and are not read back.
    /// Velocity is refused after a frame drawn below scale 1, whose region is
    /// smaller than the target (UTA-0051 SS 4.4).
    [[nodiscard]] Result<std::vector<std::byte>> readback(Target target = Target::Colour);

    /// The last frame's SS 6 counts.
    [[nodiscard]] FrameStats lastFrameStats() const noexcept;

    /// UTA-0075's sub-pixel jitter (SS 4.11 provision 1). OFF until a pass
    /// consumes it: with no temporal resolve to average it away, a jittered
    /// frame is a frame that shimmers.
    void setJitter(bool enabled) noexcept;

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
