// UT99's FireTexture animation -- UTA-0176, moved here by UTA-0286.
//
// A FireTexture stores no pixels. UT99 draws each frame at run time from its
// sparks: each spark heats pixels of a heat field, directly or by releasing
// particles that drift and cool, and the field is then blurred and cooled,
// shifted up a row when the texture rises. The palette turns heat into colour.
// The bake runs it to make a still (ubake::fireStill); the renderer runs it
// live for a FireTexture that is not a flame
// (docs/specs/UTA-0286-replayed-fire-textures.md SS 4.1).
//
// ADAPTED FROM SurrealEngine's UFireTexture::UpdateFrame
// (SurrealEngine/Packages/Engine/Resources/Textures/UFireTexture.cpp,
// https://github.com/dpjudas/SurrealEngine), Copyright (c) 2021-2026 Magnus
// Norddahl, Lupert Everett and contributors, under the zlib licence, whose
// notice is at third_party/surrealengine/LICENSE. Altered from the original:
// - its random bytes come from the C library's rand(); these from a fixed
//   xorshift sequence, so one texture gives one picture on any machine
//   (docs/design.md, the numeric contract);
// - under Turning::Scatter, Wheel and SphereLightning, which turn angles with
//   sine and cosine -- a platform maths call the contract keeps out of the
//   baker -- heat as the types it does not model do, a scattered point a step;
// - it is a class holding its own state rather than a texture's members.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace uta::ufire {

/// The steps a fire is run before its first picture. A torch's particles live
/// at most 255 steps, so this is past the longest-lived one leaving its first
/// spark.
inline constexpr int PRIME_STEPS = 256;

/// One spark as UT99 stores it: a type, its heat, its place on the texture,
/// and four bytes whose meaning the type decides.
struct Spark {
    std::uint8_t type = 0;
    std::uint8_t heat = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t byteA = 0;
    std::uint8_t byteB = 0;
    std::uint8_t byteC = 0;
    std::uint8_t byteD = 0;
};

/// The FireTexture properties the animation reads.
struct Settings {
    std::uint8_t renderHeat = 0;  ///< how slowly heat fades; 255 fades least
    bool rising = false;          ///< the field moves up a row a step
    std::int32_t sparksLimit = 0; ///< sparks and live particles together
};

/// Wheel and SphereLightning: heat a scattered point, as the baker must
/// (Scatter), or turn and draw lines as UT99 does (Model).
enum class Turning { Scatter, Model };

class Fire {
public:
    /// A cold field. A zero side gives an empty field that steps to nothing.
    Fire(std::uint32_t width, std::uint32_t height, std::vector<Spark> sparks, const Settings& settings,
         Turning turning);

    /// One UT99 frame of the animation.
    void step();

    /// The heat of every pixel, row by row, top first: the palette indices.
    [[nodiscard]] std::span<const std::uint8_t> heat() const noexcept { return heat_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    struct Particle {
        enum class Kind { Drift, DriftGravity, Twirl } kind = Kind::Drift;
        float x = 0, y = 0, speedX = 0, speedY = 0;
        float angle = 0, rotSpeed = 0; ///< Twirl
        int heat = 0;
        int heatDecay = 0;    ///< Drift
        std::uint8_t age = 0; ///< DriftGravity, Twirl
    };

    int randomByte() noexcept;
    float byteFraction() noexcept { return static_cast<float>(randomByte()) / 255.0f; }
    void set(int x, int y, int value) noexcept;
    void setWrapped(float fx, float fy, int value) noexcept;
    void scatter(const Spark& spark, int heat);
    void spread();

    std::uint32_t width_, height_;
    Settings settings_;
    Turning turning_;
    std::vector<Spark> sparks_; ///< Pulse, Signal and Wheel change theirs
    std::vector<Particle> particles_;
    std::vector<std::uint8_t> heat_;
    std::vector<std::uint8_t> next_;
    std::array<std::uint8_t, 4 * 256> fade_{};
    std::uint32_t random_ = 0x9E3779B9u;
};

/// UTA-0286 SS 4.4: `heat` as sRGB bytes, four a texel. A texel takes the
/// palette entry its heat names; its alpha is 0 where `masked` and the heat is
/// 0, else 255. `rgba` holds four bytes per heat value.
void colour(std::span<const std::uint8_t> heat, const std::array<std::array<std::uint8_t, 3>, 256>& palette,
            bool masked, std::span<std::byte> rgba) noexcept;

/// UTA-0286 SS 4.4: the steps a second a fire runs at -- `maxFrameRate` when it
/// is above 0, capped at 60; 30 when it is 0 or not a number.
[[nodiscard]] double stepsPerSecond(float maxFrameRate) noexcept;

} // namespace uta::ufire
