// UTA-0110's container cases: the PLAC and LITE sections.
//
// docs/specs/UTA-0110-lights-and-placements.md SS 4.3 and SS 4.4, INV-1 and
// INV-2. The builder's cases are tests/unit/BakeActorsTest.cpp; nothing here
// reaches ubake.
//
// THE PAYLOADS ARE AUTHORED FROM SS 4.3 AND SS 4.4, field by field, never
// produced by `write` -- BundleGeometryTest.cpp's reason: a transposition
// present in both the reader and the writer round-trips perfectly.
//
// EACH INV-2 FIXTURE BREAKS ONE RULE of golden(), which every rule accepts,
// and is refused on the path or paths the rule names. A bool in memory holds
// no byte but 0 or 1, so the bool-byte rules are read's alone; read decodes a
// value from its kind, so the value-kind rule is write's alone.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::ActorClass;
using uta::ubundle::ActorPlacement;
using uta::ubundle::AncestryEnd;
using uta::ubundle::Bundle;
using uta::ubundle::Geometry;
using uta::ubundle::Light;
using uta::ubundle::Origin;
using uta::ubundle::Placements;
using uta::ubundle::PropertyRecord;
using uta::ubundle::RawValue;
using uta::ubundle::read;
using uta::ubundle::ValueKind;
using uta::ubundle::write;

namespace {

using Value = decltype(PropertyRecord::value);

std::uint32_t bitsOf(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

/// A signalling NaN, whose bits a float widened in transit does not keep.
const float SIGNALLING_NAN = std::bit_cast<float>(0x7FA01234u);

PropertyRecord record(std::string name, ValueKind kind, Value value,
                      std::uint32_t arrayIndex = 0) {
    PropertyRecord out;
    out.name = std::move(name);
    out.arrayIndex = arrayIndex;
    out.kind = kind;
    out.value = std::move(value);
    return out;
}

/// SS 4.3's table, one record. Written from the alternative the value holds,
/// so a Bool record holding a byte writes that byte -- which is how a read
/// case authors a Bool no `bool` can hold.
void putRecord(Bytes& out, const PropertyRecord& property) {
    out.str(property.name);
    out.u32(property.arrayIndex);
    out.u8(static_cast<std::uint8_t>(property.kind));
    std::visit(
        [&out](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::uint8_t>) {
                out.u8(value);
            } else if constexpr (std::is_same_v<T, std::int32_t>) {
                out.i32(value);
            } else if constexpr (std::is_same_v<T, bool>) {
                out.u8(value ? 1 : 0);
            } else if constexpr (std::is_same_v<T, float>) {
                out.f32(value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                out.str(value);
            } else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
                for (const float part : value) out.f32(part);
            } else if constexpr (std::is_same_v<T, std::array<std::int32_t, 3>>) {
                for (const std::int32_t part : value) out.i32(part);
            } else {
                out.u8(value.type);
                out.str(value.structName);
                out.u32(static_cast<std::uint32_t>(value.bytes.size()));
                for (const std::byte part : value.bytes) out.u8(static_cast<std::uint8_t>(part));
            }
        },
        property.value);
}

void putRecords(Bytes& out, const std::vector<PropertyRecord>& records) {
    out.u32(static_cast<std::uint32_t>(records.size()));
    for (const PropertyRecord& property : records) putRecord(out, property);
}

/// A PLAC payload in SS 4.4's order. `resolvedByte` replaces every class's
/// resolved byte, for the read case no `bool` can author.
Bytes placPayload(const Placements& placements, std::optional<std::uint8_t> resolvedByte = {}) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(placements.classes.size()));
    for (const ActorClass& actorClass : placements.classes) {
        out.str(actorClass.path);
        out.u8(resolvedByte.value_or(actorClass.resolved ? 1 : 0));
        out.u32(static_cast<std::uint32_t>(actorClass.ancestry.size()));
        for (const std::string& parent : actorClass.ancestry) out.str(parent);
        out.u8(static_cast<std::uint8_t>(actorClass.end));
        out.str(actorClass.missing);
        putRecords(out, actorClass.defaults);
    }
    out.u32(static_cast<std::uint32_t>(placements.actors.size()));
    for (const ActorPlacement& actor : placements.actors) {
        out.u32(actor.exportIndex);
        out.str(actor.path);
        out.u32(actor.classIndex);
        putRecords(out, actor.properties);
    }
    return out;
}

/// A LITE payload in SS 4.4's order: 44 bytes a light. `coronaByte` replaces
/// every light's corona byte, for the read case no `bool` can author.
Bytes litePayload(const std::vector<Light>& lights, std::optional<std::uint8_t> coronaByte = {}) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(lights.size()));
    for (const Light& light : lights) {
        out.u32(light.exportIndex);
        for (const float part : light.location) out.f32(part);
        for (const std::int32_t part : light.rotation) out.i32(part);
        for (const std::uint8_t part :
             {light.type, light.effect, light.brightness, light.hue, light.saturation, light.radius,
              light.period, light.phase, light.cone, light.volumeBrightness, light.volumeRadius,
              light.volumeFog})
            out.u8(part);
        out.u8(light.specialLit ? 1 : 0);
        out.u8(light.actorShadows ? 1 : 0);
        out.u8(coronaByte.value_or(light.corona ? 1 : 0));
        out.u8(light.lensFlare ? 1 : 0);
    }
    return out;
}

/// An empty GEOM payload: three empty vectors.
Bytes emptyGeom() {
    Bytes out;
    out.u32(0);
    out.u32(0);
    out.u32(0);
    return out;
}

/// A whole .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(7); // formatVersion -- 7 since UTA-0111 SS 4.2
    out.u8(1);  // origin: Authored
    out.u8(0);  // kind: Map
    out.u16(0); // reserved
    out.u32(static_cast<std::uint32_t>(sections.size()));

    std::uint64_t offset = 16 + 24 * sections.size();
    for (const auto& [id, payload] : sections) {
        out.id(id);
        out.u64(offset);
        out.u64(payload.size());
        out.u8(0); // compression
        out.u8(0);
        out.u8(0);
        out.u8(0);
        offset += payload.size();
    }
    for (const auto& section : sections) out.append(section.second);
    return out.data();
}

/// One record of every kind, a Raw struct among them, a -0.0 and a signalling
/// NaN; a resolved class and one whose package is missing; two actors.
Placements golden() {
    ActorClass lamp;
    lamp.path = "actorpkg.lamp";
    lamp.resolved = true;
    lamp.ancestry = {"engine.light", "engine.actor", "core.object"};
    lamp.end = AncestryEnd::Root;
    lamp.defaults = {
        record("bBool", ValueKind::Bool, true),
        record("Byte", ValueKind::Byte, std::uint8_t{200}),
        record("Class", ValueKind::Class, std::string("engine.light")),
        record("Float", ValueKind::Float, -0.0F),
        record("Int", ValueKind::Int, std::int32_t{-7}, 2),
        record("Name", ValueKind::Name, std::string("Lamp")),
        record("Object", ValueKind::Object, std::string()),
        record("Raw", ValueKind::Raw,
               RawValue{10, "Color", {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}}),
        record("Rotator", ValueKind::Rotator, std::array<std::int32_t, 3>{-16384, 32768, 1}),
        record("String", ValueKind::String, std::string("hello")),
        record("Vector", ValueKind::Vector, std::array<float, 3>{1.5F, -2.0F, SIGNALLING_NAN}),
    };

    ActorClass thing;
    thing.path = "nosuchpkg.thing";
    thing.resolved = false;
    thing.end = AncestryEnd::PackageMissing;
    thing.missing = "nosuchpkg";

    Placements placements;
    placements.classes = {lamp, thing};
    placements.actors = {
        ActorPlacement{7, "dm-fixture.lamp0", 0,
                       {record("Location", ValueKind::Vector, std::array<float, 3>{-0.0F, 8.0F, 64.0F})}},
        ActorPlacement{9, "dm-fixture.thing1", 1, {}},
    };
    return placements;
}

/// Every one of the twelve bytes distinct in the first light, and the four
/// bools set so no two of them agree across both lights -- a transposition of
/// any two fields shows.
std::vector<Light> goldenLights() {
    Light first;
    first.exportIndex = 7;
    first.location = {-0.0F, 8.0F, SIGNALLING_NAN};
    first.rotation = {-16384, 0, 65535};
    first.type = 1;
    first.effect = 2;
    first.brightness = 64;
    first.hue = 3;
    first.saturation = 255;
    first.radius = 65;
    first.period = 32;
    first.phase = 4;
    first.cone = 128;
    first.volumeBrightness = 5;
    first.volumeRadius = 6;
    first.volumeFog = 7;
    first.specialLit = true;
    first.corona = true;

    Light second;
    second.exportIndex = 9;
    second.type = 250; // past the last light type, and not range-checked
    second.corona = true;
    second.lensFlare = true;
    return {first, second};
}

bool sameValue(const Value& actual, const Value& expected) {
    if (actual.index() != expected.index()) return false;
    return std::visit(
        [&expected](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const T& other = std::get<T>(expected);
            if constexpr (std::is_same_v<T, float>) {
                return bitsOf(value) == bitsOf(other);
            } else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
                for (std::size_t i = 0; i < 3; ++i)
                    if (bitsOf(value[i]) != bitsOf(other[i])) return false;
                return true;
            } else if constexpr (std::is_same_v<T, RawValue>) {
                return value.type == other.type && value.structName == other.structName
                       && value.bytes == other.bytes;
            } else {
                return value == other;
            }
        },
        actual);
}

void sameRecords(const std::vector<PropertyRecord>& actual,
                 const std::vector<PropertyRecord>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(actual[i].name == expected[i].name);
        CHECK(actual[i].arrayIndex == expected[i].arrayIndex);
        CHECK(actual[i].kind == expected[i].kind);
        CHECK(sameValue(actual[i].value, expected[i].value));
    }
}

void samePlacements(const Placements& actual, const Placements& expected) {
    REQUIRE(actual.classes.size() == expected.classes.size());
    for (std::size_t i = 0; i < expected.classes.size(); ++i) {
        CHECK(actual.classes[i].path == expected.classes[i].path);
        CHECK(actual.classes[i].resolved == expected.classes[i].resolved);
        CHECK(actual.classes[i].ancestry == expected.classes[i].ancestry);
        CHECK(actual.classes[i].end == expected.classes[i].end);
        CHECK(actual.classes[i].missing == expected.classes[i].missing);
        sameRecords(actual.classes[i].defaults, expected.classes[i].defaults);
    }
    REQUIRE(actual.actors.size() == expected.actors.size());
    for (std::size_t i = 0; i < expected.actors.size(); ++i) {
        CHECK(actual.actors[i].exportIndex == expected.actors[i].exportIndex);
        CHECK(actual.actors[i].path == expected.actors[i].path);
        CHECK(actual.actors[i].classIndex == expected.actors[i].classIndex);
        sameRecords(actual.actors[i].properties, expected.actors[i].properties);
    }
}

void sameLights(const std::vector<Light>& actual, const std::vector<Light>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const Light& a = actual[i];
        const Light& e = expected[i];
        CHECK(a.exportIndex == e.exportIndex);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(bitsOf(a.location[axis]) == bitsOf(e.location[axis]));
            CHECK(a.rotation[axis] == e.rotation[axis]);
        }
        CHECK(a.type == e.type);
        CHECK(a.effect == e.effect);
        CHECK(a.brightness == e.brightness);
        CHECK(a.hue == e.hue);
        CHECK(a.saturation == e.saturation);
        CHECK(a.radius == e.radius);
        CHECK(a.period == e.period);
        CHECK(a.phase == e.phase);
        CHECK(a.cone == e.cone);
        CHECK(a.volumeBrightness == e.volumeBrightness);
        CHECK(a.volumeRadius == e.volumeRadius);
        CHECK(a.volumeFog == e.volumeFog);
        CHECK(a.specialLit == e.specialLit);
        CHECK(a.actorShadows == e.actorShadows);
        CHECK(a.corona == e.corona);
        CHECK(a.lensFlare == e.lensFlare);
    }
}

void readRefuses(const std::vector<std::byte>& file, std::string_view says) {
    const auto result = read(file);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK(result.error().message().find(says) != std::string_view::npos);
}

void writeRefuses(const Bundle& bundle, std::string_view says) {
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
    CHECK(written.error().message().find(says) != std::string_view::npos);
}

/// The same rule, blamed on the file or on the caller.
void placementsRefused(const Placements& placements, std::string_view says) {
    readRefuses(fileWith({{"PLAC", placPayload(placements)}}), says);
    Bundle bundle;
    bundle.placements = placements;
    writeRefuses(bundle, says);
}

void lightsRefused(const std::vector<Light>& lights, std::string_view says) {
    readRefuses(fileWith({{"LITE", litePayload(lights)}}), says);
    Bundle bundle;
    bundle.lights = lights;
    writeRefuses(bundle, says);
}

/// golden() with one of lamp's defaults replaced.
Placements withDefault(std::size_t index, PropertyRecord replacement) {
    Placements placements = golden();
    placements.classes[0].defaults[index] = std::move(replacement);
    return placements;
}

} // namespace

// ------------------------------------------------------------------ INV-1

TEST_CASE("the PLAC and LITE golden bytes decode to what they encode", "[ubundle][plac]") {
    const auto result = read(fileWith({{"GEOM", emptyGeom()},
                                       {"PLAC", placPayload(golden())},
                                       {"LITE", litePayload(goldenLights())}}));
    REQUIRE(result.has_value());
    CHECK(result->header.formatVersion == 7);
    REQUIRE(result->geometry.has_value());
    REQUIRE(result->placements.has_value());
    REQUIRE(result->lights.has_value());
    samePlacements(*result->placements, golden());
    sameLights(*result->lights, goldenLights());
}

TEST_CASE("write emits PLAC then LITE after GEOM as the golden bytes", "[ubundle][plac]") {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.geometry = Geometry{};
    bundle.placements = golden();
    bundle.lights = goldenLights();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"GEOM", emptyGeom()},
                                {"PLAC", placPayload(golden())},
                                {"LITE", litePayload(goldenLights())}}));
}

TEST_CASE("empty PLAC and LITE sections round-trip as present and empty", "[ubundle][plac]") {
    Bundle bundle;
    bundle.placements = Placements{};
    bundle.lights = std::vector<Light>{};
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    const auto result = read(*written);
    REQUIRE(result.has_value());
    REQUIRE(result->placements.has_value());
    REQUIRE(result->lights.has_value());
    CHECK(result->placements->classes.empty());
    CHECK(result->placements->actors.empty());
    CHECK(result->lights->empty());
}

// ------------------------------------------------ INV-2, both paths

TEST_CASE("two classes of one path are refused", "[ubundle][plac]") {
    Placements placements = golden();
    placements.classes[1].path = placements.classes[0].path;
    placementsRefused(placements, "class 1's path");
}

TEST_CASE("classes out of order are refused", "[ubundle][plac]") {
    Placements placements = golden();
    std::swap(placements.classes[0], placements.classes[1]);
    placementsRefused(placements, "class 1's path");
}

TEST_CASE("two actors of one slot are refused", "[ubundle][plac]") {
    Placements placements = golden();
    placements.actors[1].exportIndex = placements.actors[0].exportIndex;
    placementsRefused(placements, "actor 1's exportIndex");
}

TEST_CASE("actors out of order are refused", "[ubundle][plac]") {
    Placements placements = golden();
    std::swap(placements.actors[0], placements.actors[1]);
    placementsRefused(placements, "actor 1's exportIndex");
}

TEST_CASE("a classIndex equal to the class count is refused", "[ubundle][plac]") {
    Placements placements = golden();
    placements.actors[0].classIndex = 2;
    placementsRefused(placements, "names class 2");
}

TEST_CASE("an end byte of 3 is refused", "[ubundle][plac]") {
    Placements placements = golden();
    placements.classes[0].end = static_cast<AncestryEnd>(3);
    placements.classes[0].missing = "x"; // so the missing rule holds and only this one breaks
    placementsRefused(placements, "end byte 3");
}

TEST_CASE("missing empty on PackageMissing is refused", "[ubundle][plac]") {
    Placements placements = golden();
    placements.classes[1].missing.clear();
    placementsRefused(placements, "missing");
}

TEST_CASE("missing set on Root is refused", "[ubundle][plac]") {
    Placements placements = golden();
    placements.classes[0].missing = "x";
    placementsRefused(placements, "missing");
}

TEST_CASE("a class not resolved that ends at its root is refused", "[ubundle][plac]") {
    Placements placements = golden();
    placements.classes[0].resolved = false;
    placementsRefused(placements, "not resolved");
}

TEST_CASE("a kind byte of 11 is refused", "[ubundle][plac]") {
    placementsRefused(withDefault(1, record("Byte", static_cast<ValueKind>(11), std::uint8_t{200})),
                      "kind byte 11");
}

TEST_CASE("a Raw type byte of 0 is refused", "[ubundle][plac]") {
    placementsRefused(withDefault(7, record("Raw", ValueKind::Raw, RawValue{0, "", {}})),
                      "Raw type 0");
}

TEST_CASE("a Raw type byte of 16 is refused", "[ubundle][plac]") {
    placementsRefused(withDefault(7, record("Raw", ValueKind::Raw, RawValue{16, "", {}})),
                      "Raw type 16");
}

TEST_CASE("two lights of one slot are refused", "[ubundle][lite]") {
    std::vector<Light> lights = goldenLights();
    lights[1].exportIndex = lights[0].exportIndex;
    lightsRefused(lights, "light 1's exportIndex");
}

TEST_CASE("lights out of order are refused", "[ubundle][lite]") {
    std::vector<Light> lights = goldenLights();
    std::swap(lights[0], lights[1]);
    lightsRefused(lights, "light 1's exportIndex");
}

// ------------------------------------------------ INV-2, one path each

TEST_CASE("read refuses a Bool value byte of 2", "[ubundle][plac]") {
    const Placements placements = withDefault(0, record("bBool", ValueKind::Bool, std::uint8_t{2}));
    readRefuses(fileWith({{"PLAC", placPayload(placements)}}), "Bool byte 2");
}

TEST_CASE("read refuses a resolved byte of 2", "[ubundle][plac]") {
    readRefuses(fileWith({{"PLAC", placPayload(golden(), 2)}}), "resolved byte 2");
}

TEST_CASE("read refuses a light bool byte of 2", "[ubundle][lite]") {
    readRefuses(fileWith({{"LITE", litePayload(goldenLights(), 2)}}), "bool byte 2");
}

TEST_CASE("write refuses a value whose alternative is not its kind's", "[ubundle][plac]") {
    Bundle bundle;
    bundle.placements = withDefault(4, record("Int", ValueKind::Int, std::string("x"), 2));
    writeRefuses(bundle, "does not hold");
}
