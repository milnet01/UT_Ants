// Shadow maps -- Shadows.h.

#include "urender/Shadows.h"

#include "urender/Placement.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace uta::urender {

namespace {

constexpr std::uint8_t LE_STATIC_SPOT = 8;
constexpr std::uint8_t LE_SPOTLIGHT = 12;

/// The widest a spotlight's shadow frustum opens. A cone byte near 255 asks for
/// nearly a hemisphere, which a single perspective tile cannot hold.
constexpr double WIDEST_SPOT_DEGREES = 170.0;

double radiusOf(const ubundle::Light& light) noexcept { return 25.0 * (light.radius + 1); }

std::uint32_t levelOf(std::uint32_t size) noexcept {
    return static_cast<std::uint32_t>(std::countr_zero(LARGEST_SHADOW_TILE) - std::countr_zero(size));
}

using Vec = std::array<double, 3>;

Vec cross(const Vec& a, const Vec& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

Vec normalised(const Vec& v) noexcept {
    const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return {v[0] / length, v[1] / length, v[2] / length};
}

/// A view looking from `eye` along `forward`, as viewOf's: +X right, +Y up, +Z
/// forward. `up` need only not be parallel to `forward`.
gpu::Mat4 lookAlong(const std::array<float, 3>& eye, const Vec& forwardIn, const Vec& upHint) noexcept {
    const Vec forward = normalised(forwardIn);
    const Vec right = normalised(cross(upHint, forward));
    const Vec up = cross(forward, right);
    gpu::Mat4 m{};
    for (int column = 0; column < 3; ++column) {
        m[column * 4 + 0] = static_cast<float>(right[column]);
        m[column * 4 + 1] = static_cast<float>(up[column]);
        m[column * 4 + 2] = static_cast<float>(forward[column]);
    }
    const auto dot = [&](const Vec& v) { return v[0] * eye[0] + v[1] * eye[1] + v[2] * eye[2]; };
    m[12] = static_cast<float>(-dot(right));
    m[13] = static_cast<float>(-dot(up));
    m[14] = static_cast<float>(-dot(forward));
    m[15] = 1.0f;
    return m;
}

bool boxReaches(const std::array<std::array<float, 3>, 2>& box, const std::array<float, 3>& centre,
                double radius) noexcept {
    double gap = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const double nearest = std::clamp(static_cast<double>(centre[axis]), static_cast<double>(box[0][axis]),
                                          static_cast<double>(box[1][axis]));
        gap += (nearest - centre[axis]) * (nearest - centre[axis]);
    }
    return gap < radius * radius;
}

bool sameLight(const ubundle::Light& a, const ubundle::Light& b) noexcept {
    return a.location == b.location && a.rotation == b.rotation && a.radius == b.radius && a.cone == b.cone
           && a.effect == b.effect;
}

} // namespace

// -- ShadowAtlas ----------------------------------------------------------------

void ShadowAtlas::clear() {
    const std::uint32_t levels = levelOf(SMALLEST_SHADOW_TILE) + 1;
    free_.assign(levels, {});
    // Pushed in reverse, so the lowest address is handed out first.
    for (std::uint32_t y = SHADOW_ATLAS_SIZE; y > 0; y -= LARGEST_SHADOW_TILE)
        for (std::uint32_t x = SHADOW_ATLAS_SIZE; x > 0; x -= LARGEST_SHADOW_TILE)
            free_[0].push_back({x - LARGEST_SHADOW_TILE, y - LARGEST_SHADOW_TILE, LARGEST_SHADOW_TILE});
}

std::optional<AtlasTile> ShadowAtlas::allocate(std::uint32_t size) {
    if (size < SMALLEST_SHADOW_TILE || size > LARGEST_SHADOW_TILE || !std::has_single_bit(size)) return std::nullopt;
    const std::uint32_t wanted = levelOf(size);
    std::uint32_t level = wanted;
    // The smallest free tile at least as big as the one wanted.
    while (free_[level].empty()) {
        if (level == 0) return std::nullopt;
        --level;
    }
    AtlasTile tile = free_[level].back();
    free_[level].pop_back();
    // Halve it down to size, keeping the first quarter and freeing the other three.
    while (level < wanted) {
        const std::uint32_t half = tile.size / 2;
        ++level;
        free_[level].push_back({tile.x + half, tile.y + half, half});
        free_[level].push_back({tile.x, tile.y + half, half});
        free_[level].push_back({tile.x + half, tile.y, half});
        tile.size = half;
    }
    return tile;
}

void ShadowAtlas::release(const AtlasTile& tile) {
    free_[levelOf(tile.size)].push_back(tile);
}

// -- Lights ---------------------------------------------------------------------

bool isSpot(const ubundle::Light& light) noexcept {
    return light.effect == LE_SPOTLIGHT || light.effect == LE_STATIC_SPOT;
}

std::uint32_t shadowFacesOf(const ubundle::Light& light) noexcept {
    if (isSpot(light)) return light.cone == 0 ? 0 : 1;
    return 6;
}

std::uint32_t shadowTileSize(const Camera& camera, std::uint32_t width, std::uint32_t height,
                             const ubundle::Light& light) noexcept {
    const gpu::Mat4 view = viewOf(camera);
    const auto& l = light.location;
    const double x = view[0] * l[0] + view[4] * l[1] + view[8] * l[2] + view[12];
    const double y = view[1] * l[0] + view[5] * l[1] + view[9] * l[2] + view[13];
    const double z = view[2] * l[0] + view[6] * l[1] + view[10] * l[2] + view[14];
    const double radius = radiusOf(light);

    const double tanV = std::tan(camera.verticalFovDegrees * std::numbers::pi / 360.0);
    const double tanH = tanV * width / height;
    // Wholly behind the camera, or wholly beyond a side of the view.
    if (z + radius < camera.nearPlane) return 0;
    if ((std::abs(x) - z * tanH) / std::sqrt(1 + tanH * tanH) > radius) return 0;
    if ((std::abs(y) - z * tanV) / std::sqrt(1 + tanV * tanV) > radius) return 0;
    if (z <= radius) return LARGEST_SHADOW_TILE; // the camera is inside it

    const double pixels = radius / (z * tanV) * height;
    const auto wanted = static_cast<std::uint32_t>(std::min(std::ceil(pixels), double(LARGEST_SHADOW_TILE)));
    return std::clamp(std::bit_ceil(std::max(wanted, 1u)), SMALLEST_SHADOW_TILE, LARGEST_SHADOW_TILE);
}

gpu::Mat4 shadowViewProj(const ubundle::Light& light, std::uint32_t face) noexcept {
    const double radius = radiusOf(light);
    Camera lens;
    lens.farPlane = static_cast<float>(radius);
    lens.nearPlane = static_cast<float>(std::max(1.0, radius / 512.0));

    gpu::Mat4 view;
    if (isSpot(light)) {
        const double p = light.rotation[0] * 2.0 * std::numbers::pi / 65536.0;
        const double yaw = light.rotation[1] * 2.0 * std::numbers::pi / 65536.0;
        const Vec forward{std::cos(p) * std::cos(yaw), std::cos(p) * std::sin(yaw), std::sin(p)};
        const Vec up = std::abs(forward[2]) > 0.99 ? Vec{1, 0, 0} : Vec{0, 0, 1};
        view = lookAlong(light.location, forward, up);
        // The cone's full angle: c = 1 - cone / 256 is the cosine of its half.
        const double c = std::max(1.0 - light.cone / 256.0, 0.0);
        lens.verticalFovDegrees =
            static_cast<float>(std::min(2.0 * std::acos(c) * 180.0 / std::numbers::pi, WIDEST_SPOT_DEGREES));
    } else {
        static constexpr std::array<Vec, 6> FORWARD = {{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1},
                                                        {0, 0, -1}}};
        static constexpr std::array<Vec, 6> UP = {{{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0},
                                                   {1, 0, 0}}};
        view = lookAlong(light.location, FORWARD[face % 6], UP[face % 6]);
        lens.verticalFovDegrees = 90.0f;
    }
    return multiply(projectionOf(lens, 1, 1), view);
}

// -- ShadowPlanner --------------------------------------------------------------

void ShadowPlanner::reset() {
    held_.clear();
    atlas_.clear();
}

ShadowPlan ShadowPlanner::plan(const std::vector<ubundle::Light>& lights, const Camera& camera,
                               std::uint32_t width, std::uint32_t height,
                               const std::vector<std::array<std::array<float, 3>, 2>>& movedMoverBounds) {
    std::vector<std::uint32_t> wanted(lights.size());
    for (std::size_t i = 0; i < lights.size(); ++i)
        wanted[i] = shadowFacesOf(lights[i]) == 0 ? 0 : shadowTileSize(camera, width, height, lights[i]);

    // Any light whose wanted size changed -- or a different set of lights --
    // re-admits every light, largest first, and draws all their tiles again.
    bool replan = held_.size() != lights.size();
    for (std::size_t i = 0; !replan && i < lights.size(); ++i)
        replan = held_[i].size != wanted[i] || !sameLight(held_[i].light, lights[i]);

    std::vector<bool> redraw(lights.size(), false);
    if (replan) {
        atlas_.clear();
        held_.assign(lights.size(), {});
        std::vector<std::size_t> order(lights.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return wanted[a] > wanted[b]; });
        for (const std::size_t i : order) {
            held_[i].light = lights[i];
            held_[i].size = wanted[i];
            if (wanted[i] == 0) continue;
            for (std::uint32_t face = 0; face < shadowFacesOf(lights[i]); ++face) {
                const auto tile = atlas_.allocate(wanted[i]);
                if (!tile) {
                    // Its faces are not worth half a light: none of them, and
                    // the ones already taken go back for the lights after it.
                    for (const AtlasTile& taken : held_[i].tiles) atlas_.release(taken);
                    held_[i].tiles.clear();
                    break;
                }
                held_[i].tiles.push_back(*tile);
            }
            redraw[i] = !held_[i].tiles.empty();
        }
    } else {
        for (std::size_t i = 0; i < lights.size(); ++i)
            for (const auto& box : movedMoverBounds)
                if (!held_[i].tiles.empty() && boxReaches(box, lights[i].location, radiusOf(lights[i]))) redraw[i] = true;
    }

    ShadowPlan plan;
    plan.firstFace.assign(lights.size(), -1);
    plan.faceCount.assign(lights.size(), 0);
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const Held& held = held_[i];
        if (held.tiles.empty()) {
            if (wanted[i] != 0) ++plan.unshadowed;
            continue;
        }
        plan.firstFace[i] = static_cast<std::int32_t>(plan.faces.size());
        plan.faceCount[i] = static_cast<std::uint32_t>(held.tiles.size());
        for (std::uint32_t face = 0; face < held.tiles.size(); ++face) {
            const AtlasTile& tile = held.tiles[face];
            const float scale = 1.0f / static_cast<float>(SHADOW_ATLAS_SIZE);
            if (redraw[i]) plan.draws.push_back({static_cast<std::uint32_t>(plan.faces.size()), tile});
            plan.faces.push_back({shadowViewProj(lights[i], face),
                                  {tile.x * scale, tile.y * scale, tile.size * scale, tile.size * scale}});
        }
    }
    return plan;
}

} // namespace uta::urender
