// The LAMP section: the lamps a map's recipe adds --
// docs/specs/UTA-0256-added-lamps.md SS 4.2 and INV-3.
//
// A lamp is a LITE record, a GEOM shape and FLAM records, each in its own
// section's encoding and under its own section's rules, so this file reads,
// writes and validates them through those sections' files rather than carrying
// a second copy of any.

#include "Sections.h"

#include <string>
#include <utility>

namespace uta::ubundle::detail {
namespace {

/// A light record, an empty geometry's three counts and an empty flame count.
constexpr std::uint64_t MIN_LAMP = 73 + 12 + 4;
/// FLAM's record.
constexpr std::uint64_t FLAME_RECORD = 32;

[[nodiscard]] Result<AddedLamp> readLamp(Cursor& cursor) {
    AddedLamp lamp;
    UTA_TRY(lamp.light, readLight(cursor));
    UTA_TRY(lamp.shape, readGeometry(cursor));
    UTA_TRY(lamp.flames, readVector<Flame>(cursor, FLAME_RECORD, "lamp flames", readFlame));
    return lamp;
}

void putLamp(Sink& sink, const AddedLamp& lamp) {
    putLight(sink, lamp.light);
    sink.putEncoded(encodeGeometry(lamp.shape));
    sink.putVector(lamp.flames, putFlame);
}

} // namespace

Result<std::vector<AddedLamp>> readLamps(Cursor& cursor) {
    return readVector<AddedLamp>(cursor, MIN_LAMP, "lamps", readLamp);
}

Result<std::vector<std::byte>> encodeLamps(const std::vector<AddedLamp>& lamps) {
    Sink sink;
    sink.putVector(lamps, putLamp);
    return std::move(sink).finish("LAMP");
}

Result<void> validateLamps(const Bundle& bundle, ErrorCode code) {
    const std::size_t count = bundle.lamps ? bundle.lamps->size() : 0;
    for (std::size_t i = 0; i < count; ++i) {
        const AddedLamp& lamp = (*bundle.lamps)[i];
        const std::string where = "LAMP: lamp " + std::to_string(i);
        UTA_CHECK(validateLight(lamp.light, where + "'s light", code));
        // A strip's other members are LITE's; a lamp copies one light alone.
        if (lamp.light.strip != STRIP_NONE) return fail(code, where + "'s light is part of a strip");
        const Result<void> shape = validateGeometry(lamp.shape, code);
        if (!shape.has_value()) return fail(code, where + "'s shape: " + std::string(shape.error().message()));
        // SS 3: a lamp copies a fitting, so it has something to show.
        if (lamp.shape.indices.empty() && lamp.flames.empty())
            return fail(code, where + " has no fitting: no shape and no flame");
        for (std::size_t f = 0; f < lamp.flames.size(); ++f) {
            const std::string flame = where + "'s flame " + std::to_string(f);
            UTA_CHECK(validateFlame(bundle, lamp.flames[f], 0, flame, code));
        }
    }
    // SS 4.2: the probes' added layer is there exactly when a lamp is.
    if (bundle.lightProbes) {
        const std::size_t want = count == 0 ? 0 : bundle.lightProbes->probes.size();
        if (bundle.lightProbes->added.size() != want)
            return fail(code, "LPRB: " + std::to_string(bundle.lightProbes->added.size())
                                  + " added-lamp cubes, and LAMP's " + std::to_string(count) + " lamps need "
                                  + std::to_string(want));
    }
    return {};
}

} // namespace uta::ubundle::detail
