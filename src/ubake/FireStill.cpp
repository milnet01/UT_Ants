// A still picture of a FireTexture -- UTA-0176. FireStill.h says what is
// adapted from SurrealEngine and what is altered.

#include "FireStill.h"

#include <algorithm>
#include <array>

namespace uta::ubake {
namespace {

// UT99's spark types, by their stored value. Only those modelled are named.
constexpr std::uint8_t BURN = 0;
constexpr std::uint8_t PULSE = 2;
constexpr std::uint8_t SIGNAL = 3;
constexpr std::uint8_t BLAZE = 4;
constexpr std::uint8_t OZ_HAS_SPOKEN = 5;
constexpr std::uint8_t CONE = 6;
constexpr std::uint8_t BLAZE_RIGHT = 7;
constexpr std::uint8_t BLAZE_LEFT = 8;
constexpr std::uint8_t EMIT = 13;

/// A fixed xorshift sequence, so a texture bakes to one picture everywhere.
class Random {
public:
    int byte() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<int>(state_ >> 24);
    }

private:
    std::uint32_t state_ = 0x9E3779B9u;
};

struct Particle {
    enum class Kind { Drift, DriftGravity } kind = Kind::Drift;
    float x = 0, y = 0, speedX = 0, speedY = 0;
    int heat = 0;
    int heatDecay = 0;  ///< Drift
    std::uint8_t age = 0; ///< DriftGravity
};

class Field {
public:
    Field(int width, int height) : width_(width), height_(height), heat_(std::size_t(width) * height) {}

    void set(int x, int y, int value) noexcept {
        if (x >= 0 && y >= 0 && x < width_ && y < height_)
            heat_[std::size_t(x) + std::size_t(y) * width_] = static_cast<std::uint8_t>(value);
    }

    /// A particle's pixel: wrapped across one edge at most, as the original.
    void setWrapped(float fx, float fy, int value) noexcept {
        int x = static_cast<int>(fx), y = static_cast<int>(fy);
        if (x < 0) x += width_; else if (x >= width_) x -= width_;
        if (y < 0) y += height_; else if (y >= height_) y -= height_;
        set(x, y, value);
    }

    /// Blur and cool the field, moving it up a row when `rising`.
    void spread(const std::array<std::uint8_t, 4 * 256>& fade, bool rising) {
        std::vector<std::uint8_t> next(heat_.size());
        const int shift = rising ? 1 : 0;
        for (int y = 0; y < height_; ++y) {
            const std::size_t source = std::size_t((y + shift) % height_) * width_;
            const std::size_t below = std::size_t((y + shift + 1) % height_) * width_;
            for (int x = 0; x < width_; ++x) {
                const int left = heat_[source + std::size_t((x + width_ - 1) % width_)];
                const int centre = heat_[source + std::size_t(x)];
                const int right = heat_[source + std::size_t((x + 1) % width_)];
                const int under = heat_[below + std::size_t(x)];
                next[std::size_t(y) * width_ + std::size_t(x)] = fade[std::size_t(left + centre + right + under)];
            }
        }
        heat_ = std::move(next);
    }

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] const std::vector<std::uint8_t>& heat() const noexcept { return heat_; }

private:
    int width_, height_;
    std::vector<std::uint8_t> heat_;
};

/// A spark heating a point scattered by its first two bytes: Sparkle's rule,
/// and what every unmodelled type does so that no fire comes out blank.
void scatter(Field& field, const upkg::Spark& spark, int heat, Random& random) {
    const int x = (spark.x + (random.byte() * spark.byteA + 128) / 256) % field.width();
    const int y = (spark.y + (random.byte() * spark.byteB + 128) / 256) % field.height();
    field.set(x, y, heat);
}

} // namespace

std::vector<std::byte> fireStill(std::uint32_t width, std::uint32_t height, std::span<const upkg::Spark> sparks,
                                 const FireSettings& settings) {
    if (width == 0 || height == 0) return {};
    Field field(static_cast<int>(width), static_cast<int>(height));
    std::vector<upkg::Spark> live(sparks.begin(), sparks.end()); // Pulse and Signal change their heat
    std::vector<Particle> particles;
    Random random;

    std::array<std::uint8_t, 4 * 256> fade{};
    const float heatLoss = 1.0f - static_cast<float>(255 - settings.renderHeat) / 16.0f;
    for (std::size_t i = 0; i < fade.size(); ++i)
        fade[i] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(i) * 0.25f + heatLoss, 0.0f, 255.0f));

    const auto byteFraction = [&random] { return static_cast<float>(random.byte()) / 255.0f; };
    for (int frame = 0; frame < FIRE_STILL_FRAMES; ++frame) {
        for (upkg::Spark& spark : live) {
            const bool canEmit = live.size() + particles.size() < static_cast<std::size_t>(settings.sparksLimit);
            switch (spark.type) {
            case BURN: field.set(spark.x, spark.y, random.byte()); break;
            case PULSE:
                scatter(field, spark, spark.heat, random);
                spark.heat = static_cast<std::uint8_t>(spark.heat + spark.byteD);
                break;
            case SIGNAL: {
                const int x = (spark.x + (random.byte() * spark.byteA + 128) / 256) % field.width();
                const int y = (spark.y + (random.byte() * spark.byteB + 128) / 256) % field.height();
                if (spark.heat > spark.byteC) field.set(x, y, spark.heat);
                if (int(spark.heat) + spark.byteD < 256) spark.heat = static_cast<std::uint8_t>(spark.heat + spark.byteD);
                else spark.heat = static_cast<std::uint8_t>(random.byte());
                break;
            }
            case EMIT:
                if (canEmit && random.byte() < 64) {
                    particles.push_back({.kind = Particle::Kind::Drift, .x = spark.x + 0.5f, .y = spark.y + 0.5f,
                                         .speedX = static_cast<std::int8_t>(spark.byteA) * (1.0f / 128.0f),
                                         .speedY = static_cast<std::int8_t>(spark.byteB) * (1.0f / 128.0f),
                                         .heat = spark.heat, .heatDecay = spark.byteD});
                }
                break;
            case OZ_HAS_SPOKEN:
                if (canEmit && random.byte() < 128) {
                    const float speedX = byteFraction() * 2.0f - 1.0f;
                    particles.push_back({.kind = Particle::Kind::Drift, .x = spark.x + 0.5f, .y = spark.y + 0.5f,
                                         .speedX = speedX, .speedY = -0.5f, .heat = spark.heat, .heatDecay = 5});
                }
                break;
            case BLAZE:
                if (canEmit && random.byte() < 128) {
                    const float speedX = byteFraction() * 2.0f - 1.0f;
                    const float speedY = byteFraction() * 2.0f - 1.0f;
                    particles.push_back({.kind = Particle::Kind::Drift, .x = spark.x + 0.5f, .y = spark.y + 0.5f,
                                         .speedX = speedX, .speedY = speedY, .heat = spark.heat, .heatDecay = 5});
                }
                break;
            case BLAZE_LEFT:
            case BLAZE_RIGHT:
                if (canEmit && random.byte() < 64) {
                    const float away = 0.5f + byteFraction() * 0.5f;
                    particles.push_back({.kind = Particle::Kind::DriftGravity, .x = spark.x + 0.5f,
                                         .y = spark.y + 0.5f, .speedX = spark.type == BLAZE_LEFT ? -away : away,
                                         .speedY = -0.1f, .heat = spark.heat, .age = spark.byteC});
                }
                break;
            case CONE:
                if (canEmit && random.byte() < 64) {
                    const float speedX = byteFraction() - 0.5f;
                    particles.push_back({.kind = Particle::Kind::DriftGravity, .x = spark.x + 0.5f,
                                         .y = spark.y + 0.5f, .speedX = speedX, .speedY = 0.0f, .heat = spark.heat,
                                         .age = 50});
                }
                break;
            default: scatter(field, spark, spark.heat, random); break;
            }
        }

        for (std::size_t i = 0; i < particles.size(); ++i) {
            Particle& particle = particles[i];
            bool alive = false;
            if (particle.kind == Particle::Kind::Drift) {
                particle.heat -= particle.heatDecay;
                if (particle.heat > 0) {
                    field.setWrapped(particle.x, particle.y, particle.heat);
                    particle.x += particle.speedX;
                    particle.y += particle.speedY;
                    alive = true;
                }
            } else {
                --particle.age;
                if (particle.age > 0) {
                    field.setWrapped(particle.x, particle.y, particle.heat);
                    particle.x += particle.speedX * 0.5f;
                    particle.y += particle.speedY * 0.5f;
                    particle.speedY = std::min(particle.speedY + 0.025f, 1.0f);
                    alive = true;
                }
            }
            if (!alive) {
                // The original's removal: the last particle takes this slot, and
                // is next looked at on the following frame.
                particles[i] = particles.back();
                particles.pop_back();
            }
        }

        field.spread(fade, settings.rising);
    }

    std::vector<std::byte> indices(field.heat().size());
    std::ranges::transform(field.heat(), indices.begin(), [](std::uint8_t heat) { return std::byte{heat}; });
    return indices;
}

} // namespace uta::ubake
