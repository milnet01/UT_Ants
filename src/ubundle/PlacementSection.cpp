// The PLAC section: the level's placed actors and the classes they belong to
// -- docs/specs/UTA-0110-lights-and-placements.md SS 4.3 and SS 4.4, INV-1
// and INV-2.

#include "Sections.h"

#include <array>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace uta::ubundle::detail {
namespace {

/// SS 4.3: an empty name, the index, the kind and a one-byte value.
constexpr std::uint64_t MIN_PROPERTY_RECORD = 10;

/// SS 4.4: an empty path, the resolved byte, an empty ancestry, the end byte,
/// an empty `missing` and no defaults.
constexpr std::uint64_t MIN_ACTOR_CLASS = 18;

/// SS 4.4: the export index, an empty path, the class index and no properties.
constexpr std::uint64_t MIN_ACTOR_PLACEMENT = 16;

/// A string's own framing: its u32 length.
constexpr std::uint64_t MIN_STRING = 4;

constexpr auto LAST_KIND = static_cast<std::uint8_t>(ValueKind::Raw);
constexpr auto LAST_END = static_cast<std::uint8_t>(AncestryEnd::ClassMissing);

/// upkg::PropertyType's values, stated as a range because this library may not
/// include upkg (UTA-0008 INV-10).
constexpr std::uint8_t FIRST_RAW_TYPE = 1;
constexpr std::uint8_t LAST_RAW_TYPE = 15;

[[nodiscard]] std::string kindRefusal(std::uint8_t kind) {
    return "PLAC: a property's kind byte " + std::to_string(kind) + " is not 0 to "
           + std::to_string(LAST_KIND);
}

/// A byte that must be 0 or 1. Refused here, on `read` alone: a bool in memory
/// holds no other value, so `write` has nothing to refuse (SS 4.3).
[[nodiscard]] Result<bool> readFlag(Cursor& cursor, const char* what) {
    UTA_TRY(const std::uint8_t byte, cursor.readU8());
    if (byte > 1)
        return fail(ErrorCode::MalformedData,
                    std::string(what) + " byte " + std::to_string(byte) + " is not 0 or 1");
    return byte == 1;
}

template <class T>
[[nodiscard]] Result<std::array<T, 3>> readThree(Cursor& cursor) {
    std::array<T, 3> out{};
    for (T& part : out) {
        if constexpr (std::is_same_v<T, float>) {
            UTA_TRY(part, cursor.readF32());
        } else {
            UTA_TRY(part, cursor.readI32());
        }
    }
    return out;
}

[[nodiscard]] Result<PropertyRecord> readRecord(Cursor& cursor) {
    PropertyRecord record;
    UTA_TRY(record.name, readString(cursor));
    UTA_TRY(record.arrayIndex, cursor.readU32());
    UTA_TRY(const std::uint8_t kind, cursor.readU8());
    // Refused here as well as by the validator: a kind with no known width
    // leaves nothing after it readable.
    if (kind > LAST_KIND) return fail(ErrorCode::MalformedData, kindRefusal(kind));
    record.kind = static_cast<ValueKind>(kind);

    switch (record.kind) {
    case ValueKind::Byte: {
        UTA_TRY(const std::uint8_t value, cursor.readU8());
        record.value = value;
        break;
    }
    case ValueKind::Int: {
        UTA_TRY(const std::int32_t value, cursor.readI32());
        record.value = value;
        break;
    }
    case ValueKind::Bool: {
        UTA_TRY(const bool value, readFlag(cursor, "PLAC: a Bool"));
        record.value = value;
        break;
    }
    case ValueKind::Float: {
        UTA_TRY(const float value, cursor.readF32());
        record.value = value;
        break;
    }
    case ValueKind::Object:
    case ValueKind::Class:
    case ValueKind::Name:
    case ValueKind::String: {
        UTA_TRY(std::string value, readString(cursor));
        record.value = std::move(value);
        break;
    }
    case ValueKind::Vector: {
        UTA_TRY(const auto value, readThree<float>(cursor));
        record.value = value;
        break;
    }
    case ValueKind::Rotator: {
        UTA_TRY(const auto value, readThree<std::int32_t>(cursor));
        record.value = value;
        break;
    }
    case ValueKind::Raw: {
        RawValue raw;
        UTA_TRY(raw.type, cursor.readU8());
        UTA_TRY(raw.structName, readString(cursor));
        // Bounded by readBytes before anything is sized from the count.
        UTA_TRY(const std::uint32_t count, cursor.readU32());
        UTA_TRY(const std::span<const std::byte> bytes, cursor.readBytes(count));
        raw.bytes.assign(bytes.begin(), bytes.end());
        record.value = std::move(raw);
        break;
    }
    }
    return record;
}

[[nodiscard]] Result<ActorClass> readActorClass(Cursor& cursor) {
    ActorClass actorClass;
    UTA_TRY(actorClass.path, readString(cursor));
    UTA_TRY(actorClass.resolved, readFlag(cursor, "PLAC: a class's resolved"));
    UTA_TRY(actorClass.ancestry,
            readVector<std::string>(cursor, MIN_STRING, "ancestors", readString));
    // The range is the validator's, on both paths: an enum with a fixed
    // underlying type holds any byte, so `write` can be handed a 3 as well.
    UTA_TRY(const std::uint8_t end, cursor.readU8());
    actorClass.end = static_cast<AncestryEnd>(end);
    UTA_TRY(actorClass.missing, readString(cursor));
    UTA_TRY(actorClass.defaults,
            readVector<PropertyRecord>(cursor, MIN_PROPERTY_RECORD, "defaults", readRecord));
    return actorClass;
}

[[nodiscard]] Result<ActorPlacement> readActorPlacement(Cursor& cursor) {
    ActorPlacement actor;
    UTA_TRY(actor.exportIndex, cursor.readU32());
    UTA_TRY(actor.path, readString(cursor));
    UTA_TRY(actor.classIndex, cursor.readU32());
    UTA_TRY(actor.properties,
            readVector<PropertyRecord>(cursor, MIN_PROPERTY_RECORD, "properties", readRecord));
    return actor;
}

/// Whether `value` holds the alternative `kind` names. `read` builds the value
/// from its kind and cannot fail this; it is `write`'s rule (SS 4.3).
[[nodiscard]] bool holdsItsKind(const PropertyRecord& record) {
    switch (record.kind) {
    case ValueKind::Byte: return std::holds_alternative<std::uint8_t>(record.value);
    case ValueKind::Int: return std::holds_alternative<std::int32_t>(record.value);
    case ValueKind::Bool: return std::holds_alternative<bool>(record.value);
    case ValueKind::Float: return std::holds_alternative<float>(record.value);
    case ValueKind::Object:
    case ValueKind::Class:
    case ValueKind::Name:
    case ValueKind::String: return std::holds_alternative<std::string>(record.value);
    case ValueKind::Vector: return std::holds_alternative<std::array<float, 3>>(record.value);
    case ValueKind::Rotator:
        return std::holds_alternative<std::array<std::int32_t, 3>>(record.value);
    case ValueKind::Raw: return std::holds_alternative<RawValue>(record.value);
    }
    return false;
}

[[nodiscard]] Result<void> validateRecord(const PropertyRecord& record, ErrorCode code) {
    const auto kind = static_cast<std::uint8_t>(record.kind);
    if (kind > LAST_KIND) return fail(code, kindRefusal(kind));
    if (!holdsItsKind(record))
        return fail(code, "PLAC: property '" + record.name
                              + "' does not hold the alternative its kind names");
    if (record.kind == ValueKind::Raw) {
        const std::uint8_t type = std::get<RawValue>(record.value).type;
        if (type < FIRST_RAW_TYPE || type > LAST_RAW_TYPE)
            return fail(code, "PLAC: property '" + record.name + "' has Raw type "
                                  + std::to_string(type) + ", not 1 to 15");
    }
    return {};
}

void putRecord(Sink& sink, const PropertyRecord& record) {
    sink.putString(record.name);
    sink.putU32(record.arrayIndex);
    sink.putU8(static_cast<std::uint8_t>(record.kind));
    std::visit(
        [&sink](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::uint8_t>) {
                sink.putU8(value);
            } else if constexpr (std::is_same_v<T, std::int32_t>) {
                sink.putI32(value);
            } else if constexpr (std::is_same_v<T, bool>) {
                sink.putU8(value ? 1 : 0);
            } else if constexpr (std::is_same_v<T, float>) {
                sink.putF32(value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                sink.putString(value);
            } else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
                for (const float part : value) sink.putF32(part);
            } else if constexpr (std::is_same_v<T, std::array<std::int32_t, 3>>) {
                for (const std::int32_t part : value) sink.putI32(part);
            } else {
                sink.putU8(value.type);
                sink.putString(value.structName);
                sink.putU32(static_cast<std::uint32_t>(value.bytes.size()));
                for (const std::byte part : value.bytes) sink.putU8(static_cast<std::uint8_t>(part));
            }
        },
        record.value);
}

void putActorClass(Sink& sink, const ActorClass& actorClass) {
    sink.putString(actorClass.path);
    sink.putU8(actorClass.resolved ? 1 : 0);
    sink.putVector(actorClass.ancestry,
                   [](Sink& out, const std::string& parent) { out.putString(parent); });
    sink.putU8(static_cast<std::uint8_t>(actorClass.end));
    sink.putString(actorClass.missing);
    sink.putVector(actorClass.defaults, putRecord);
}

void putActorPlacement(Sink& sink, const ActorPlacement& actor) {
    sink.putU32(actor.exportIndex);
    sink.putString(actor.path);
    sink.putU32(actor.classIndex);
    sink.putVector(actor.properties, putRecord);
}

} // namespace

Result<Placements> readPlacements(Cursor& cursor) {
    Placements placements;
    UTA_TRY(placements.classes,
            readVector<ActorClass>(cursor, MIN_ACTOR_CLASS, "classes", readActorClass));
    UTA_TRY(placements.actors,
            readVector<ActorPlacement>(cursor, MIN_ACTOR_PLACEMENT, "actors", readActorPlacement));
    return placements;
}

Result<void> validatePlacements(const Placements& placements, ErrorCode code) {
    const auto& classes = placements.classes;
    for (std::size_t i = 0; i < classes.size(); ++i) {
        const ActorClass& actorClass = classes[i];
        // Strictly ascending, which also makes each path unique. std::string's
        // `<` compares as unsigned char, so this is SS 4.4's bytewise order on
        // every compiler.
        if (i > 0 && !(classes[i - 1].path < actorClass.path))
            return fail(code, "PLAC: class " + std::to_string(i)
                                  + "'s path does not sort strictly after the one before it");
        const auto end = static_cast<std::uint8_t>(actorClass.end);
        if (end > LAST_END)
            return fail(code, "PLAC: class " + std::to_string(i) + "'s end byte "
                                  + std::to_string(end) + " is not 0 to 2");
        if (actorClass.missing.empty() != (actorClass.end == AncestryEnd::Root))
            return fail(code, "PLAC: class " + std::to_string(i)
                                  + "'s missing must be empty exactly when its end is Root");
        if (!actorClass.resolved && actorClass.end == AncestryEnd::Root)
            return fail(code, "PLAC: class " + std::to_string(i)
                                  + " is not resolved but ends at its root");
        for (const PropertyRecord& record : actorClass.defaults)
            UTA_CHECK(validateRecord(record, code));
    }

    const auto& actors = placements.actors;
    for (std::size_t i = 0; i < actors.size(); ++i) {
        const ActorPlacement& actor = actors[i];
        if (i > 0 && !(actors[i - 1].exportIndex < actor.exportIndex))
            return fail(code, "PLAC: actor " + std::to_string(i)
                                  + "'s exportIndex does not sort strictly after the one before it");
        if (actor.classIndex >= classes.size())
            return fail(code, "PLAC: actor " + std::to_string(i) + " names class "
                                  + std::to_string(actor.classIndex) + " of "
                                  + std::to_string(classes.size()));
        for (const PropertyRecord& record : actor.properties)
            UTA_CHECK(validateRecord(record, code));
    }
    return {};
}

std::vector<std::byte> encodePlacements(const Placements& placements) {
    Sink sink;
    sink.putVector(placements.classes, putActorClass);
    sink.putVector(placements.actors, putActorPlacement);
    return std::move(sink).take();
}

} // namespace uta::ubundle::detail
