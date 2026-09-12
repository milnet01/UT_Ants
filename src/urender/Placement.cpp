// Where things are -- Placement.h.

#include "urender/Placement.h"

#include <glm/glm.hpp>

#include <cmath>
#include <numbers>

namespace uta::urender {

namespace {

gpu::Mat4 toArray(const glm::dmat4& m) noexcept {
    gpu::Mat4 out{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) out[column * 4 + row] = static_cast<float>(m[column][row]);
    return out;
}

glm::dmat4 toGlm(const gpu::Mat4& m) noexcept {
    glm::dmat4 out(0.0);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) out[column][row] = m[column * 4 + row];
    return out;
}

/// The exact sine and cosine of `angle` UT units: 2 pi angle / 65536.
double sineOf(std::int32_t angle) noexcept { return std::sin(angle * 2.0 * std::numbers::pi / 65536.0); }
double cosineOf(std::int32_t angle) noexcept { return std::cos(angle * 2.0 * std::numbers::pi / 65536.0); }

/// UTA-0119 SS 4.5's Y * P * R, for pitch, yaw and roll.
glm::dmat3 rotationOf(const std::array<std::int32_t, 3>& rotation) noexcept {
    const double cp = cosineOf(rotation[0]), sp = sineOf(rotation[0]);
    const double cy = cosineOf(rotation[1]), sy = sineOf(rotation[1]);
    const double cr = cosineOf(rotation[2]), sr = sineOf(rotation[2]);
    // glm is column-major: each constructor argument triple is a COLUMN, so
    // these read as the SS 4.5 matrices transposed.
    const glm::dmat3 yaw(cy, sy, 0, -sy, cy, 0, 0, 0, 1);
    const glm::dmat3 pitch(cp, 0, sp, 0, 1, 0, -sp, 0, cp);
    const glm::dmat3 roll(1, 0, 0, 0, cr, -sr, 0, sr, cr);
    return yaw * pitch * roll;
}

double radical(std::uint64_t index, std::uint64_t base) noexcept {
    double result = 0;
    double fraction = 1.0 / static_cast<double>(base);
    for (; index > 0; index /= base, fraction /= static_cast<double>(base))
        result += fraction * static_cast<double>(index % base);
    return result;
}

} // namespace

gpu::Mat4 identity() noexcept { return toArray(glm::dmat4(1.0)); }

gpu::Mat4 multiply(const gpu::Mat4& a, const gpu::Mat4& b) noexcept { return toArray(toGlm(a) * toGlm(b)); }

gpu::Mat4 viewOf(const Camera& camera) noexcept {
    const glm::dmat3 axes = rotationOf(camera.rotation);
    const glm::dvec3 forward = axes[0], right = axes[1], up = axes[2];
    const glm::dvec3 eye(camera.location[0], camera.location[1], camera.location[2]);
    glm::dmat4 view(1.0);
    for (int column = 0; column < 3; ++column) {
        view[column][0] = right[column];
        view[column][1] = up[column];
        view[column][2] = forward[column];
    }
    view[3][0] = -glm::dot(right, eye);
    view[3][1] = -glm::dot(up, eye);
    view[3][2] = -glm::dot(forward, eye);
    return toArray(view);
}

gpu::Mat4 projectionOf(const Camera& camera, std::uint32_t width, std::uint32_t height) noexcept {
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double f = 1.0 / std::tan(camera.verticalFovDegrees * std::numbers::pi / 360.0);
    const double n = camera.nearPlane, far = camera.farPlane;
    glm::dmat4 p(0.0);
    p[0][0] = f / aspect;
    p[1][1] = -f; // Vulkan's clip space runs +Y down
    p[2][2] = far / (far - n);
    p[2][3] = 1;
    p[3][2] = -far * n / (far - n);
    return toArray(p);
}

std::array<float, 2> haltonJitter(std::uint64_t frame) noexcept {
    const std::uint64_t index = frame % 8 + 1;
    return {static_cast<float>(radical(index, 2) - 0.5), static_cast<float>(radical(index, 3) - 0.5)};
}

gpu::Mat4 jittered(const gpu::Mat4& projection, std::array<float, 2> pixels, std::uint32_t width,
                   std::uint32_t height) noexcept {
    gpu::Mat4 out = projection;
    // Column 2 multiplies view-space z, which is clip w here, so this moves
    // NDC by a constant: 2 / width of NDC is one pixel.
    out[2 * 4 + 0] += 2.0f * pixels[0] / static_cast<float>(width);
    out[2 * 4 + 1] += 2.0f * pixels[1] / static_cast<float>(height);
    return out;
}

gpu::Mat4 moverModel(const ubundle::MoverShape& mover) noexcept {
    const glm::dmat3 r = rotationOf(mover.rotation);
    glm::dmat4 m(1.0);
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row) m[column][row] = mover.postScale[row] * r[column][row];
    for (int row = 0; row < 3; ++row) m[3][row] = mover.location[row];
    return toArray(m);
}

gpu::Mat4 normalMatrixOf(const gpu::Mat4& model) noexcept {
    const glm::dmat3 upper(toGlm(model));
    const double det = glm::determinant(upper);
    if (det == 0) return toArray(glm::dmat4(0.0));
    return toArray(glm::dmat4(glm::transpose(glm::inverse(upper))));
}

} // namespace uta::urender
