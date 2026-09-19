// The AOCC section: the level's baked ambient occlusion, and the rule tying
// its texture coordinates to GEOM's vertices --
// docs/specs/UTA-0164-ambient-occlusion.md SS 4.1 and INV-1.

#include "Sections.h"

#include <cmath>
#include <cstring>
#include <string>

namespace uta::ubundle::detail {
namespace {

/// Two f32.
constexpr std::uint64_t UV_ENTRY = 8;

[[nodiscard]] Result<std::array<float, 2>> readUv(Cursor& cursor) {
    std::array<float, 2> uv{};
    UTA_TRY(uv[0], cursor.readF32());
    UTA_TRY(uv[1], cursor.readF32());
    return uv;
}

void putUv(Sink& sink, const std::array<float, 2>& uv) {
    sink.putF32(uv[0]);
    sink.putF32(uv[1]);
}

} // namespace

Result<Occlusion> readOcclusion(Cursor& cursor) {
    Occlusion occlusion;
    UTA_TRY(occlusion.texelSize, cursor.readF32());
    UTA_TRY(occlusion.width, cursor.readU32());
    UTA_TRY(occlusion.height, cursor.readU32());
    using Uv = std::array<float, 2>;
    UTA_TRY(occlusion.uv, readVector<Uv>(cursor, UV_ENTRY, "occlusion uvs", readUv));
    // The texels as one run: the bound is readBytes', and nothing is sized
    // from the count before it has passed.
    UTA_TRY(const std::uint32_t count, cursor.readU32());
    UTA_TRY(const std::span<const std::byte> raw, cursor.readBytes(count));
    occlusion.texels.resize(raw.size());
    if (!raw.empty()) std::memcpy(occlusion.texels.data(), raw.data(), raw.size());
    return occlusion;
}

Result<void> validateOcclusion(const Occlusion& occlusion, ErrorCode code) {
    if (!std::isfinite(occlusion.texelSize) || occlusion.texelSize <= 0)
        return fail(code, "AOCC: a texel size of " + std::to_string(occlusion.texelSize)
                              + ", where only a finite positive size is allowed");
    for (const std::uint32_t side : {occlusion.width, occlusion.height})
        if (side == 0 || side > OCCLUSION_ATLAS_LIMIT)
            return fail(code, "AOCC: an atlas side of " + std::to_string(side) + ", where 1 to "
                                  + std::to_string(OCCLUSION_ATLAS_LIMIT) + " are allowed");
    const std::uint64_t expected = std::uint64_t{occlusion.width} * occlusion.height;
    if (occlusion.texels.size() != expected)
        return fail(code, "AOCC: " + std::to_string(occlusion.texels.size()) + " texels for a "
                              + std::to_string(occlusion.width) + " by " + std::to_string(occlusion.height)
                              + " atlas");
    for (std::size_t i = 0; i < occlusion.uv.size(); ++i)
        for (const float c : occlusion.uv[i])
            if (!std::isfinite(c) || c < 0 || c > 1)
                return fail(code, "AOCC: vertex " + std::to_string(i) + " has a uv component of "
                                      + std::to_string(c) + ", outside 0 to 1");
    return {};
}

std::vector<std::byte> encodeOcclusion(const Occlusion& occlusion) {
    Sink sink;
    sink.putF32(occlusion.texelSize);
    sink.putU32(occlusion.width);
    sink.putU32(occlusion.height);
    sink.putVector(occlusion.uv, putUv);
    sink.putU32(static_cast<std::uint32_t>(occlusion.texels.size()));
    for (const std::uint8_t texel : occlusion.texels) sink.putU8(texel);
    return std::move(sink).take();
}

Result<void> validateOcclusionVertices(const Bundle& bundle, ErrorCode code) {
    if (!bundle.occlusion) return {};
    if (!bundle.geometry) return fail(code, "AOCC is present and the bundle has no GEOM");
    const std::size_t vertices = bundle.geometry->vertices.size();
    if (bundle.occlusion->uv.size() != vertices)
        return fail(code, "AOCC holds " + std::to_string(bundle.occlusion->uv.size()) + " uvs, and GEOM "
                              + std::to_string(vertices) + " vertices");
    return {};
}

} // namespace uta::ubundle::detail
