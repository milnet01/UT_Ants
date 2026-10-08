// Exact light to measure the renderer against -- Reference.h says what and why.

#include "Reference.h"

#include "ubake/LightModel.h"
#include "urender/Placement.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <thread>
#include <utility>

namespace uta::ref {
namespace {

using ubake::Rgb;
using ubake::Vec3;

/// PolyFlags' values -- the 432 headers' Engine/Inc/UnObj.h, as LightProbes.cpp.
constexpr std::uint32_t PF_FAKE_BACKDROP = 0x80;
constexpr std::uint32_t PF_TWO_SIDED = 0x100;
constexpr std::uint32_t PF_UNLIT = 0x400000;

/// SS 4.4's luma, the weights scene.frag's light terms use.
double luma(const Rgb& c) noexcept { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

Rgb times(const Rgb& a, const Rgb& b) noexcept { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
Rgb plus(const Rgb& a, const Rgb& b) noexcept { return {a.r + b.r, a.g + b.g, a.b + b.b}; }

Vec3 normalised(const Vec3& v) noexcept {
    const double size = length(v);
    return {v.x / size, v.y / size, v.z / size};
}

Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// xorshift64: a uniform double in [0, 1), advancing `state`.
double uniform(std::uint64_t& state) noexcept {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<double>(state >> 11) * (1.0 / 9007199254740992.0);
}

/// A direction about `n`, cosine-weighted, so the mean of what the rays bring
/// is the light a surface facing `n` receives, as a probe's face holds it.
Vec3 cosineAbout(const Vec3& n, std::uint64_t& random) noexcept {
    const double u1 = uniform(random), u2 = uniform(random);
    const double r = std::sqrt(u1), phi = 2 * std::numbers::pi * u2;
    const Vec3 a = std::abs(n.x) > 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 t = normalised(cross(a, n));
    const Vec3 b = cross(n, t);
    return normalised(t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0, 1 - u1)));
}

/// The batch whose run of indices holds `triangle`, as LightProbes.cpp finds it.
const ubundle::GeometryBatch& batchOf(const ubundle::Geometry& geometry, std::size_t triangle) {
    const auto index = static_cast<std::uint32_t>(3 * triangle);
    const auto after = std::upper_bound(
        geometry.batches.begin(), geometry.batches.end(), index,
        [](std::uint32_t value, const ubundle::GeometryBatch& batch) { return value < batch.firstIndex; });
    return *(after - 1);
}

/// A stream for one pixel, from its coordinates alone (INV-10).
std::uint64_t seedOf(std::uint64_t x, std::uint64_t y) noexcept {
    std::uint64_t z = (y << 32 | x) + 0x9E3779B97F4A7C15ull; // splitmix64
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return z == 0 ? 1 : z; // xorshift never leaves 0
}

} // namespace

Reference::Reference(const ubundle::Geometry& geometry, std::vector<ubundle::Light> lights, std::optional<Vec3> sky,
                     std::span<const ubake::MaterialLight> materials)
    : geometry_(geometry), rays_(geometry), eyeRays_(geometry, 0u), lights_(std::move(lights)), sky_(sky) {
    for (const ubake::MaterialLight& material : materials) materials_.emplace(material.id, material);
    albedo_ = [this](std::string_view id) {
        const auto found = materials_.find(std::string(id));
        return found == materials_.end()
                   ? Rgb{ubake::DEFAULT_ALBEDO, ubake::DEFAULT_ALBEDO, ubake::DEFAULT_ALBEDO}
                   : found->second.albedo;
    };
    own_ = [this](std::string_view id) {
        const auto found = materials_.find(std::string(id));
        return found == materials_.end() ? ubake::OwnLight{} : found->second.own;
    };
}

bool Reference::reflects(const ubake::SurfaceHit& hit) const {
    // An unlit liquid sends its picture and nothing it receives (sentFrom).
    return (hit.batch->polyFlags & PF_UNLIT) == 0 || !own_(hit.batch->material).unlitGlows;
}

Rgb Reference::radiance(const Vec3& p, const Vec3& w, int depth, std::uint64_t& random) const {
    const std::optional<ubake::SurfaceHit> hit = ubake::surfaceAlong(p, w, rays_, geometry_, sky_);
    if (!hit) return {};
    Rgb light = ubake::sentFrom(*hit, rays_, lights_, albedo_, own_);
    if (depth > 1 && reflects(*hit)) {
        const Rgb further = radiance(hit->at + hit->normal * ubake::SHADOW_OFFSET,
                                     cosineAbout(hit->normal, random), depth - 1, random);
        light = plus(light, times(ubake::reflectanceOf(albedo_(hit->batch->material)), further));
    }
    return light;
}

std::array<float, CHANNELS> Reference::pixel(const Vec3& eye, const Vec3& dir, int samples, int depth,
                                             std::uint64_t seed) const {
    std::array<float, CHANNELS> out{};
    const auto hit = eyeRays_.first(eye, dir);
    if (!hit) return out;
    const ubundle::GeometryBatch& batch = batchOf(geometry_, hit->triangle);
    // A surface light passes through is left out: the renderer's terms there
    // are of whatever is behind it, blended.
    if ((batch.polyFlags & (ubake::LIGHT_PASSES_FLAGS | PF_FAKE_BACKDROP | PF_UNLIT)) != 0) return out;
    const auto& stored = geometry_.vertices[geometry_.indices[3 * hit->triangle]].normal;
    Vec3 n = normalised({stored[0], stored[1], stored[2]});
    if (dot(dir, n) >= 0) {
        if ((batch.polyFlags & PF_TWO_SIDED) == 0) return out;
        n = n * -1.0;
    }
    const Vec3 x = eye + dir * hit->t;
    const Vec3 off = x + n * ubake::SHADOW_OFFSET;

    const Rgb e = ubake::lightReaching(x, n, rays_, lights_);
    out[DIRECT] = static_cast<float>(
        luma({ubake::shownLight(e.r), ubake::shownLight(e.g), ubake::shownLight(e.b)}));

    std::uint64_t random = seed;
    double first = 0, later = 0, sky = 0;
    for (int s = 0; s < samples; ++s) {
        const std::optional<ubake::SurfaceHit> bounce =
            ubake::surfaceAlong(off, cosineAbout(n, random), rays_, geometry_, sky_);
        if (!bounce) continue;
        const double sent = luma(ubake::sentFrom(*bounce, rays_, lights_, albedo_, own_));
        (bounce->viaSky ? sky : first) += sent;
        if (depth > 1 && reflects(*bounce)) {
            const Rgb further = radiance(bounce->at + bounce->normal * ubake::SHADOW_OFFSET,
                                         cosineAbout(bounce->normal, random), depth - 1, random);
            later += luma(times(ubake::reflectanceOf(albedo_(bounce->batch->material)), further));
        }
    }
    const double count = std::max(1, samples);
    out[FIRST] = static_cast<float>(first / count);
    out[LATER] = static_cast<float>(later / count);
    out[SKY] = static_cast<float>(sky / count);
    out[LIT] = 1;
    return out;
}

std::vector<float> Reference::trace(const urender::Camera& camera, double horizontalFovDegrees, std::uint32_t width,
                                    std::uint32_t height, int samples, int depth, unsigned workers) const {
    // ut-shot's own reading of a camera line: the horizontal field of view
    // becomes the vertical one at this aspect.
    urender::Camera view = camera;
    const double aspect = static_cast<double>(height) / width;
    const double halfRadians = horizontalFovDegrees * std::numbers::pi / 360.0;
    view.verticalFovDegrees = static_cast<float>(std::atan(std::tan(halfRadians) * aspect) * 360.0 / std::numbers::pi);
    const auto m = urender::viewToWorldOf(view);
    const double f = 1.0 / std::tan(view.verticalFovDegrees * std::numbers::pi / 360.0);
    const double across = static_cast<double>(width) / height;
    const Vec3 eye{view.location[0], view.location[1], view.location[2]};

    std::vector<float> out(std::size_t{width} * height * CHANNELS, 0.0F);
    std::atomic<std::uint32_t> nextRow{0};
    const auto work = [&] {
        for (std::uint32_t y; (y = nextRow.fetch_add(1)) < height;) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const double nx = 2 * (x + 0.5) / width - 1, ny = 2 * (y + 0.5) / height - 1;
                const double vx = nx * across / f, vy = -ny / f, vz = 1;
                const Vec3 dir = normalised({m[0] * vx + m[4] * vy + m[8] * vz, m[1] * vx + m[5] * vy + m[9] * vz,
                                             m[2] * vx + m[6] * vy + m[10] * vz});
                const auto channels = pixel(eye, dir, samples, depth, seedOf(x, y));
                std::copy(channels.begin(), channels.end(), out.begin() + (std::size_t{y} * width + x) * CHANNELS);
            }
        }
    };
    std::vector<std::jthread> pool;
    for (unsigned t = 0; t < std::max(1u, workers); ++t) pool.emplace_back(work);
    pool.clear(); // joins
    return out;
}

Score scoreView(std::span<const float> reference, std::span<const float> terms, std::uint32_t width,
                std::uint32_t height) {
    constexpr std::uint32_t BLOCK = 8;
    constexpr std::uint32_t KEEP = 48; // of BLOCK * BLOCK pixels, lit in both
    struct Block {
        double refDirect = 0, refBounce = 0, refLater = 0, refSky = 0, renDirect = 0, renIndirect = 0;
    };
    std::vector<Block> kept;
    for (std::uint32_t by = 0; by + BLOCK <= height; by += BLOCK)
        for (std::uint32_t bx = 0; bx + BLOCK <= width; bx += BLOCK) {
            Block sum;
            std::uint32_t lit = 0;
            for (std::uint32_t y = by; y < by + BLOCK; ++y)
                for (std::uint32_t x = bx; x < bx + BLOCK; ++x) {
                    const std::size_t at = std::size_t{y} * width + x;
                    const float* r = &reference[at * CHANNELS];
                    const float* t = &terms[at * 3];
                    if (r[LIT] <= 0 || t[0] + t[1] <= 0) continue;
                    ++lit;
                    sum.refDirect += r[DIRECT];
                    sum.refBounce += r[FIRST];
                    sum.refLater += r[LATER];
                    sum.refSky += r[SKY];
                    sum.renDirect += t[0];
                    sum.renIndirect += t[1];
                }
            if (lit < KEEP) continue;
            const double n = lit;
            kept.push_back({sum.refDirect / n, sum.refBounce / n, sum.refLater / n, sum.refSky / n,
                            sum.renDirect / n, sum.renIndirect / n});
        }

    Score score;
    score.blocks = kept.size();
    if (kept.empty()) return score;
    double stopsSum = 0;
    std::size_t stopsCount = 0;
    double totalGap = 0, directGap = 0, indirectGap = 0, sky = 0, later = 0;
    for (const Block& b : kept) {
        const double ref = b.refDirect + b.refBounce + b.refLater + b.refSky;
        const double ren = b.renDirect + b.renIndirect;
        score.reference += ref;
        score.renderer += ren;
        totalGap += std::abs(ren - ref);
        directGap += std::abs(b.renDirect - b.refDirect);
        // The renderer's probes hold one bounce, sky light included (UTA-0112
        // SS 4.12); later bounces are their own column.
        indirectGap += std::abs(b.renIndirect - (b.refBounce + b.refSky));
        sky += b.refSky;
        later += b.refLater;
        if (ref > 0 && ren > 0) {
            stopsSum += std::abs(std::log2(ren / ref));
            ++stopsCount;
        }
    }
    const double n = static_cast<double>(kept.size());
    score.reference /= n;
    score.renderer /= n;
    if (score.reference > 0) {
        score.total = totalGap / n / score.reference;
        score.direct = directGap / n / score.reference;
        score.indirect = indirectGap / n / score.reference;
        score.sky = sky / n / score.reference;
        score.later = later / n / score.reference;
    }
    score.stops = stopsCount == 0 ? 0 : stopsSum / static_cast<double>(stopsCount);
    return score;
}

} // namespace uta::ref
