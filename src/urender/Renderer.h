// urender -- draws a baked bundle with Vulkan 1.3.
//
// docs/specs/UTA-0014-vulkan-draw-path.md.
//
// THIS IS THE ONE HEADER ANOTHER SUBSYSTEM INCLUDES, AND NO VULKAN OR glm TYPE
// MAY APPEAR IN IT -- INV-2. That is what lets a program compile against
// urender's callers without a Vulkan loader installed, and it is why
// `Config::surface` is an integer and `readback` names its target with a plain
// enum. tests/unit/RenderHeaderIsolationTest.cpp compiles this header in a
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
#include <memory>
#include <vector>

namespace uta::urender {

/// What the caller supplies.
struct Config {
    std::uint64_t surface = 0;           ///< the caller's VkSurfaceKHR, cast. ZERO is the
                                         ///< surfaceless path: no swapchain, and no
                                         ///< VK_KHR_swapchain extension asked for
    std::uint32_t width = 0, height = 0; ///< the offscreen target's size
    bool validation = false;             ///< request the layer if installed
    /// Skip exposure and tone mapping, writing linear light to the target
    /// instead. It exists so INV-10 can compare a pixel against a literal --
    /// SS 4.10 says why nothing else can -- and it changes no other stage.
    bool linearOutput = false;
};

/// The view a frame is drawn from -- UT99's own units and angle encoding, so a
/// caller holding a `Placement` or a `MoverShape` already has both fields in
/// the right form.
struct Camera {
    std::array<float, 3> location{};
    std::array<std::int32_t, 3> rotation{}; ///< pitch, yaw, roll; 65536 to a turn
    float verticalFovDegrees = 90;
    float nearPlane = 1, farPlane = 32768;
};

/// What the last frame had to give up -- SS 6. Lighting and shadows degrade
/// rather than fail, and these say by how much, so the caps can be judged
/// against a real map rather than guessed.
struct FrameStats {
    std::uint32_t overflowedClusters = 0; ///< clusters that dropped lights past their cap
    std::uint32_t unshadowedLights = 0;   ///< shadowing lights the atlas could not hold
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
    [[nodiscard]] Result<std::vector<std::byte>> readback(Target target = Target::Colour);

    /// The last frame's SS 6 counts.
    [[nodiscard]] FrameStats lastFrameStats() const noexcept;

    /// UTA-0075's sub-pixel jitter (SS 4.11 provision 1). OFF until a pass
    /// consumes it: with no temporal resolve to average it away, a jittered
    /// frame is a frame that shimmers.
    void setJitter(bool enabled) noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace uta::urender
