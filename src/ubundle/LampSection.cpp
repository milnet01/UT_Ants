// The LAMP section: the lamps a map's recipe adds --
// docs/specs/UTA-0256-added-lamps.md SS 4.2 and INV-3.
//
// A lamp is a restores flag (SS 4.7), then a LITE record, a GEOM shape and
// FLAM records, each in its own
// section's encoding and under its own section's rules, so this file reads,
// writes and validates them through those sections' files rather than carrying
// a second copy of any.

#include "Sections.h"

#include <string>
#include <utility>

namespace uta::ubundle::detail {
namespace {

/// The restores flag, a light record, an empty geometry's three counts and an
/// empty flame count.
constexpr std::uint64_t MIN_LAMP = 1 + LIGHT_SIZE + 12 + 4;

[[nodiscard]] Result<AddedLamp> readLamp(Cursor& cursor) {
    AddedLamp lamp;
    UTA_TRY(const std::uint8_t restores, cursor.readU8());
    if (restores > 1) return fail(ErrorCode::MalformedData, "LAMP: a restores flag is neither 0 nor 1");
    lamp.restores = restores == 1;
    UTA_TRY(lamp.light, readLight(cursor));
    UTA_TRY(lamp.shape, readGeometry(cursor));
    UTA_TRY(lamp.flames, readVector<Flame>(cursor, FLAME_RECORD, "lamp flames", readFlame));
    return lamp;
}

void putLamp(Sink& sink, const AddedLamp& lamp) {
    sink.putU8(lamp.restores ? 1 : 0);
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
    std::size_t adding = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const AddedLamp& lamp = (*bundle.lamps)[i];
        const std::string where = "LAMP: lamp " + std::to_string(i);
        UTA_CHECK(validateLight(lamp.light, where + "'s light", code));
        // A strip's other members are LITE's; a lamp copies one light alone.
        if (lamp.light.strip != STRIP_NONE) return fail(code, where + "'s light is part of a strip");
        const Result<void> shape = validateGeometry(lamp.shape, code);
        if (!shape.has_value()) return fail(code, where + "'s shape: " + std::string(shape.error().message()));
        // SS 4.7: a restoring record is the map's light alone, after every
        // record that adds.
        if (lamp.restores) {
            if (!lamp.shape.indices.empty() || !lamp.shape.vertices.empty() || !lamp.flames.empty())
                return fail(code, where + " restores a map light and has a shape or a flame");
        } else {
            if (adding != i) return fail(code, where + " adds a lamp after a record that restores");
            ++adding;
        }
        for (std::size_t f = 0; f < lamp.flames.size(); ++f) {
            const std::string flame = where + "'s flame " + std::to_string(f);
            // No light: applyAddedLamps ties a lamp's flames to its own light,
            // so a stored one names none (-1).
            UTA_CHECK(validateFlame(bundle, lamp.flames[f], 0, flame, code));
        }
    }
    // SS 4.7: each of the probes' two layers is there exactly when a record of
    // its kind is.
    if (bundle.lightProbes) {
        const std::size_t restoring = count - adding;
        const std::size_t probes = bundle.lightProbes->probes.size();
        const std::size_t wantAdded = adding == 0 ? 0 : probes;
        const std::size_t wantRestored = restoring == 0 ? 0 : probes;
        if (bundle.lightProbes->added.size() != wantAdded)
            return fail(code, "LPRB: " + std::to_string(bundle.lightProbes->added.size())
                                  + " added-lamp cubes, and LAMP's " + std::to_string(adding) + " adding records need "
                                  + std::to_string(wantAdded));
        if (bundle.lightProbes->switched.size() != wantRestored)
            return fail(code, "LPRB: " + std::to_string(bundle.lightProbes->switched.size())
                                  + " switched-off cubes, and LAMP's " + std::to_string(restoring)
                                  + " restoring records need " + std::to_string(wantRestored));
    }
    return {};
}

} // namespace uta::ubundle::detail
