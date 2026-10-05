// UT99's FireTexture animation -- Fire.h says what is adapted from
// SurrealEngine and what is altered.

#include "ufire/Fire.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace uta::ufire {
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
constexpr std::uint8_t SPHERE_LIGHTNING = 25; // Model only
constexpr std::uint8_t WHEEL = 26;            // Model only

/// A byte angle, 256 to the turn, in radians.
constexpr float BYTE_TURN = 2.0f * std::numbers::pi_v<float> / 256.0f;

} // namespace

Fire::Fire(std::uint32_t width, std::uint32_t height, std::vector<Spark> sparks, const Settings& settings,
           Turning turning)
    : width_(width == 0 || height == 0 ? 0 : width), height_(width == 0 || height == 0 ? 0 : height),
      settings_(settings), turning_(turning), sparks_(std::move(sparks)),
      heat_(std::size_t(width_) * height_), next_(heat_.size()) {
    const float heatLoss = 1.0f - static_cast<float>(255 - settings.renderHeat) / 16.0f;
    for (std::size_t i = 0; i < fade_.size(); ++i)
        fade_[i] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(i) * 0.25f + heatLoss, 0.0f, 255.0f));
}

int Fire::randomByte() noexcept {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return static_cast<int>(random_ >> 24);
}

void Fire::set(int x, int y, int value) noexcept {
    if (x >= 0 && y >= 0 && x < int(width_) && y < int(height_))
        heat_[std::size_t(x) + std::size_t(y) * width_] = static_cast<std::uint8_t>(value);
}

/// A particle's pixel: wrapped across one edge at most, as the original.
void Fire::setWrapped(float fx, float fy, int value) noexcept {
    const int w = int(width_), h = int(height_);
    int x = static_cast<int>(fx), y = static_cast<int>(fy);
    if (x < 0) x += w; else if (x >= w) x -= w;
    if (y < 0) y += h; else if (y >= h) y -= h;
    set(x, y, value);
}

/// A spark heating a point scattered by its first two bytes: Sparkle's rule,
/// and what every unmodelled type does so that no fire comes out blank.
void Fire::scatter(const Spark& spark, int heat) {
    const int x = (spark.x + (randomByte() * spark.byteA + 128) / 256) % int(width_);
    const int y = (spark.y + (randomByte() * spark.byteB + 128) / 256) % int(height_);
    set(x, y, heat);
}

/// Blur and cool the field, moving it up a row when it rises.
void Fire::spread() {
    const int w = int(width_), h = int(height_);
    const int shift = settings_.rising ? 1 : 0;
    for (int y = 0; y < h; ++y) {
        const std::size_t source = std::size_t((y + shift) % h) * width_;
        const std::size_t below = std::size_t((y + shift + 1) % h) * width_;
        for (int x = 0; x < w; ++x) {
            const int left = heat_[source + std::size_t((x + w - 1) % w)];
            const int centre = heat_[source + std::size_t(x)];
            const int right = heat_[source + std::size_t((x + 1) % w)];
            const int under = heat_[below + std::size_t(x)];
            next_[std::size_t(y) * width_ + std::size_t(x)] = fade_[std::size_t(left + centre + right + under)];
        }
    }
    std::swap(heat_, next_);
}

void Fire::step() {
    if (heat_.empty()) return;
    const bool model = turning_ == Turning::Model;
    for (Spark& spark : sparks_) {
        // UT's limit is signed: a negative one admits nothing (UTA-0218).
        const bool canEmit = settings_.sparksLimit > 0
                             && sparks_.size() + particles_.size() < static_cast<std::size_t>(settings_.sparksLimit);
        switch (spark.type) {
        case BURN: set(spark.x, spark.y, randomByte()); break;
        case PULSE:
            scatter(spark, spark.heat);
            spark.heat = static_cast<std::uint8_t>(spark.heat + spark.byteD);
            break;
        case SIGNAL: {
            const int x = (spark.x + (randomByte() * spark.byteA + 128) / 256) % int(width_);
            const int y = (spark.y + (randomByte() * spark.byteB + 128) / 256) % int(height_);
            if (spark.heat > spark.byteC) set(x, y, spark.heat);
            if (int(spark.heat) + spark.byteD < 256) spark.heat = static_cast<std::uint8_t>(spark.heat + spark.byteD);
            else spark.heat = static_cast<std::uint8_t>(randomByte());
            break;
        }
        case EMIT:
            if (canEmit && randomByte() < 64) {
                particles_.push_back({.kind = Particle::Kind::Drift, .x = spark.x + 0.5f, .y = spark.y + 0.5f,
                                      .speedX = static_cast<std::int8_t>(spark.byteA) * (1.0f / 128.0f),
                                      .speedY = static_cast<std::int8_t>(spark.byteB) * (1.0f / 128.0f),
                                      .heat = spark.heat, .heatDecay = spark.byteD});
            }
            break;
        case OZ_HAS_SPOKEN:
            if (canEmit && randomByte() < 128) {
                const float speedX = byteFraction() * 2.0f - 1.0f;
                particles_.push_back({.kind = Particle::Kind::Drift, .x = spark.x + 0.5f, .y = spark.y + 0.5f,
                                      .speedX = speedX, .speedY = -0.5f, .heat = spark.heat, .heatDecay = 5});
            }
            break;
        case BLAZE:
            if (canEmit && randomByte() < 128) {
                const float speedX = byteFraction() * 2.0f - 1.0f;
                const float speedY = byteFraction() * 2.0f - 1.0f;
                particles_.push_back({.kind = Particle::Kind::Drift, .x = spark.x + 0.5f, .y = spark.y + 0.5f,
                                      .speedX = speedX, .speedY = speedY, .heat = spark.heat, .heatDecay = 5});
            }
            break;
        case BLAZE_LEFT:
        case BLAZE_RIGHT:
            if (canEmit && randomByte() < 64) {
                const float away = 0.5f + byteFraction() * 0.5f;
                particles_.push_back({.kind = Particle::Kind::DriftGravity, .x = spark.x + 0.5f,
                                      .y = spark.y + 0.5f, .speedX = spark.type == BLAZE_LEFT ? -away : away,
                                      .speedY = -0.1f, .heat = spark.heat, .age = spark.byteC});
            }
            break;
        case CONE:
            if (canEmit && randomByte() < 64) {
                const float speedX = byteFraction() - 0.5f;
                particles_.push_back({.kind = Particle::Kind::DriftGravity, .x = spark.x + 0.5f,
                                      .y = spark.y + 0.5f, .speedX = speedX, .speedY = 0.0f, .heat = spark.heat,
                                      .age = 50});
            }
            break;
        case WHEEL:
            if (!model) {
                scatter(spark, spark.heat);
                break;
            }
            // byteA the angle, byteB the twirl's age, byteC the turn a step,
            // byteD the twirl's own turn.
            if (canEmit) {
                particles_.push_back({.kind = Particle::Kind::Twirl, .x = spark.x + 0.5f, .y = spark.y + 0.5f,
                                      .angle = spark.byteA * BYTE_TURN,
                                      .rotSpeed = spark.byteD * (16.0f / 256.0f) * BYTE_TURN, .heat = spark.heat,
                                      .age = spark.byteB});
            }
            spark.byteA = static_cast<std::uint8_t>(spark.byteA + spark.byteC);
            break;
        case SPHERE_LIGHTNING:
            if (!model) {
                scatter(spark, spark.heat);
                break;
            }
            // byteC the radius, byteD the frequency: a line from the spark at a
            // random angle, its origin wandering as it goes.
            if (randomByte() >= spark.byteD) {
                const float angle = static_cast<float>(randomByte()) * BYTE_TURN;
                const float radius = spark.byteC * 0.5f;
                float x0 = spark.x + 0.5f, y0 = spark.y + 0.5f;
                const float dx = std::cos(angle), dy = std::sin(angle);
                const int hot = spark.heat, cold = spark.heat / 4;
                for (float i = 0; i < radius; i += 0.5f) {
                    const float t = i / radius;
                    setWrapped(x0 + dx * i, y0 + dy * i, static_cast<int>(hot + (cold - hot) * t + 0.5f));
                    x0 += byteFraction() * 2.0f - 1.0f;
                    y0 += byteFraction() * 2.0f - 1.0f;
                }
            }
            break;
        default: scatter(spark, spark.heat); break;
        }
    }

    for (std::size_t i = 0; i < particles_.size(); ++i) {
        Particle& particle = particles_[i];
        bool alive = false;
        switch (particle.kind) {
        case Particle::Kind::Drift:
            particle.heat -= particle.heatDecay;
            if (particle.heat > 0) {
                setWrapped(particle.x, particle.y, particle.heat);
                particle.x += particle.speedX;
                particle.y += particle.speedY;
                alive = true;
            }
            break;
        case Particle::Kind::DriftGravity:
            --particle.age;
            if (particle.age > 0) {
                setWrapped(particle.x, particle.y, particle.heat);
                particle.x += particle.speedX * 0.5f;
                particle.y += particle.speedY * 0.5f;
                particle.speedY = std::min(particle.speedY + 0.025f, 1.0f);
                alive = true;
            }
            break;
        case Particle::Kind::Twirl:
            if (particle.age > 0) {
                setWrapped(particle.x, particle.y, particle.heat);
                particle.x += std::sin(particle.angle) * 0.5f;
                particle.y += std::cos(particle.angle) * 0.5f;
                particle.angle += particle.rotSpeed;
                --particle.age;
                alive = true;
            }
            break;
        }
        if (!alive) {
            // The original's removal: the last particle takes this slot, and
            // is next looked at on the following step.
            particles_[i] = particles_.back();
            particles_.pop_back();
        }
    }

    spread();
}

void colour(std::span<const std::uint8_t> heat, const std::array<std::array<std::uint8_t, 3>, 256>& palette,
            bool masked, std::span<std::byte> rgba) noexcept {
    const std::size_t count = std::min(heat.size(), rgba.size() / 4);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& entry = palette[heat[i]];
        rgba[i * 4 + 0] = std::byte{entry[0]};
        rgba[i * 4 + 1] = std::byte{entry[1]};
        rgba[i * 4 + 2] = std::byte{entry[2]};
        rgba[i * 4 + 3] = masked && heat[i] == 0 ? std::byte{0} : std::byte{255};
    }
}

double stepsPerSecond(float maxFrameRate) noexcept {
    // UT99 steps a FireTexture whose MaxFrameRate is 0 once a drawn frame;
    // UTA-0263 SS 4.6 measured the original at 30 frames a second.
    if (!(maxFrameRate > 0.0f)) return 30.0;
    return std::min(static_cast<double>(maxFrameRate), 60.0);
}

} // namespace uta::ufire
