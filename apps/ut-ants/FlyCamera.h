// A free-flying camera -- UTA-0016. The mouse looks and the keys fly, with no
// gravity. Given the level's collision tree it stops at walls and slides along
// them, as UT99's pre-match spectator does (UTA-0158); matching UT99's movement
// is 0.2.0's.
//
// No SDL here, so the unit tests grade it against urender's own view.

#pragma once

#include "ubundle/Bundle.h"
#include "urender/Renderer.h"

#include <array>
#include <cstdint>

namespace uta::client {

/// One frame's input. The movement axes run from -1 to 1; the look fields are
/// mouse motion in pixels.
struct FlyInput {
    float forward = 0;   ///< along the view, pitch included; negative is back
    float right = 0;     ///< to the view's right, level with the ground
    float up = 0;        ///< up the world's Z, whatever the pitch
    float lookRight = 0; ///< turns the view right
    float lookUp = 0;    ///< tips the view up
    bool fast = false;
};

/// UTA-0167: one gamepad's state, with no SDL in it. The sticks run from -1 to
/// 1 with +Y down, as SDL reports them; `rise` and `sink` from 0 to 1.
struct PadInput {
    float leftX = 0, leftY = 0;   ///< flies, as W, A, S and D do
    float rightX = 0, rightY = 0; ///< looks, as the mouse does
    float rise = 0;               ///< as Space does
    float sink = 0;               ///< as Ctrl does
    bool fast = false;            ///< as Shift does
};

/// A stick's travel, 0 to 1, that is ignored around its centre so a resting
/// stick does not drift the camera. A trigger's first stretch likewise.
inline constexpr float PAD_DEAD_ZONE = 0.2f;
/// UT angle units a second a stick held fully over turns the view: half a turn.
inline constexpr double PAD_LOOK_RATE = 32768;

/// Add `pad`, held for `seconds`, to `input`. Past the dead zone the move
/// grows in step with the stick, and the look with its square, so a small
/// push turns slowly enough to aim.
void addPad(FlyInput& input, const PadInput& pad, double seconds) noexcept;

class FlyCamera {
public:
    static constexpr double SPEED = 800; ///< units a second; UT99 runs at 400
    static constexpr double FAST = 4;    ///< the multiplier `fast` applies
    static constexpr double LOOK_UNITS_PER_PIXEL = 16; ///< UT angle units, 65536 to a turn
    /// Just short of a quarter turn, so the view never tips over the top.
    static constexpr std::int32_t PITCH_LIMIT = 16000;
    /// UTA-0158: units kept between the camera and a wall, well beyond the
    /// view's near plane of 1, so the wall is never cut open.
    static constexpr double WALL_MARGIN = 8;

    FlyCamera() = default;
    FlyCamera(std::array<float, 3> location, std::int32_t pitch, std::int32_t yaw) noexcept;

    /// Apply one frame's input over `seconds`. With `level`, the move stops
    /// WALL_MARGIN short of a wall and slides along it; a camera already in
    /// solid flies free, so it can leave. Without, it flies through anything.
    void update(const FlyInput& input, double seconds, const ubundle::CollisionTree* level = nullptr) noexcept;

    [[nodiscard]] urender::Camera camera() const noexcept;

private:
    std::array<double, 3> location_{};
    double pitch_ = 0, yaw_ = 0; ///< UT angle units
};

} // namespace uta::client
