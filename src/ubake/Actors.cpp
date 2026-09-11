// The level's placed actors, their classes and their lights --
// docs/specs/UTA-0110-lights-and-placements.md SS 4.5 and SS 4.6.

#include "ubake/Actors.h"

#include "ubake/Install.h"
#include "upkg/Properties.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

namespace uta::ubake {
namespace {

using ubundle::PropertyRecord;
using ubundle::ValueKind;

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

/// The folded name each package goes by, keyed by the package itself.
/// upkg::Package carries no name of its own, so each is recorded as the
/// resolver hands it out (SS 4.3).
using PackageNames = std::map<const upkg::Package*, std::string>;

upkg::PackageResolver recording(const upkg::PackageResolver& resolver, PackageNames& names) {
    return [&resolver, &names](std::string_view name) -> Result<const upkg::Package*> {
        UTA_TRY(const upkg::Package* const package, resolver(name));
        if (package != nullptr) names.emplace(package, detail::fold(name));
        return package;
    };
}

Result<std::string_view> nameOf(const PackageNames& names, const upkg::Package& package) {
    const auto found = names.find(&package);
    if (found == names.end())
        return std::unexpected(malformed("a package was reached without its name being recorded"));
    return std::string_view(found->second);
}

/// SS 4.3: the names from the outermost package down to what `reference`
/// names, joined by '.' and folded; empty for null. `package` goes by
/// `packageName`, which heads the path of each of its exports. A reference
/// read from object data is unvalidated, so each hop is checked, and the
/// walk is bounded by its table.
Result<std::string> objectPath(const upkg::Package& package, std::string_view packageName,
                               upkg::ObjectReference reference) {
    std::vector<std::string_view> names; // what it names first, outermost last
    upkg::ObjectReference current = reference;

    if (reference.kind() == upkg::ObjectReferenceKind::Export) {
        const auto exports = package.exports();
        for (std::size_t hops = 0; current.kind() == upkg::ObjectReferenceKind::Export; ++hops) {
            if (current.index() >= exports.size() || hops >= exports.size())
                return std::unexpected(malformed(
                    "reference " + std::to_string(reference.raw())
                    + " leads outside the export table, or around it"));
            const upkg::ExportEntry& entry = exports[current.index()];
            UTA_TRY(const std::string_view name, package.name(entry.objectName));
            names.push_back(name);
            current = entry.outer;
        }
        names.push_back(packageName);
    } else if (reference.kind() == upkg::ObjectReferenceKind::Import) {
        const auto imports = package.imports();
        for (std::size_t hops = 0; current.kind() == upkg::ObjectReferenceKind::Import; ++hops) {
            if (current.index() >= imports.size() || hops >= imports.size())
                return std::unexpected(malformed(
                    "reference " + std::to_string(reference.raw())
                    + " leads outside the import table, or around it"));
            const upkg::ImportEntry& entry = imports[current.index()];
            UTA_TRY(const std::string_view name, package.name(entry.objectName));
            names.push_back(name);
            current = entry.outer;
        }
    }
    if (current.kind() != upkg::ObjectReferenceKind::Null)
        return std::unexpected(malformed("reference " + std::to_string(reference.raw())
                                         + " has an outer chain crossing from one table to the other"));

    std::string path;
    for (auto name = names.rbegin(); name != names.rend(); ++name) {
        if (!path.empty()) path += '.';
        path += *name;
    }
    return detail::fold(path);
}

/// SS 4.3: one property as a bundle stores it. `package` is the one its name
/// and object indices are relative to, and goes by `packageName`.
Result<PropertyRecord> recordOf(const upkg::Package& package, std::string_view packageName,
                                std::string name, const upkg::Property& property) {
    PropertyRecord record;
    record.name = std::move(name);
    record.arrayIndex = property.arrayIndex;
    const upkg::PropertyValue& value = property.value;

    if (const auto* byte = std::get_if<std::uint8_t>(&value)) {
        record.kind = ValueKind::Byte;
        record.value = *byte;
    } else if (const auto* integer = std::get_if<std::int32_t>(&value)) {
        record.kind = ValueKind::Int;
        record.value = *integer;
    } else if (const auto* flag = std::get_if<bool>(&value)) {
        record.kind = ValueKind::Bool;
        record.value = *flag;
    } else if (const auto* number = std::get_if<float>(&value)) {
        record.kind = ValueKind::Float;
        record.value = *number;
    } else if (const auto* object = std::get_if<upkg::ObjectReference>(&value)) {
        record.kind = property.type == upkg::PropertyType::Class ? ValueKind::Class : ValueKind::Object;
        UTA_TRY(std::string path, objectPath(package, packageName, *object));
        record.value = std::move(path);
    } else if (const auto* nameRef = std::get_if<upkg::NameRef>(&value)) {
        record.kind = ValueKind::Name;
        UTA_TRY(const std::string_view text, package.name(nameRef->index));
        record.value = std::string(text);
    } else if (const auto* text = std::get_if<std::string>(&value)) {
        record.kind = ValueKind::String;
        record.value = *text;
    } else if (const auto* vector = std::get_if<upkg::Vector3>(&value)) {
        record.kind = ValueKind::Vector;
        record.value = std::array<float, 3>{vector->x, vector->y, vector->z};
    } else if (const auto* rotator = std::get_if<upkg::Rotator>(&value)) {
        record.kind = ValueKind::Rotator;
        record.value = std::array<std::int32_t, 3>{rotator->pitch, rotator->yaw, rotator->roll};
    } else if (const auto* bytes = std::get_if<std::span<const std::byte>>(&value)) {
        // Copied as read: an index inside stays relative to `package` (SS 4.3).
        ubundle::RawValue raw;
        raw.type = static_cast<std::uint8_t>(property.type);
        if (property.type == upkg::PropertyType::Struct) {
            UTA_TRY(const std::string_view structName, package.name(property.structNameIndex));
            raw.structName = std::string(structName);
        }
        raw.bytes.assign(bytes->begin(), bytes->end());
        record.kind = ValueKind::Raw;
        record.value = std::move(raw);
    } else {
        return std::unexpected(malformed("property '" + record.name + "' carries no value"));
    }
    return record;
}

ubundle::AncestryEnd endOf(upkg::AncestryEnd end) {
    switch (end) {
    case upkg::AncestryEnd::Root: return ubundle::AncestryEnd::Root;
    case upkg::AncestryEnd::PackageMissing: return ubundle::AncestryEnd::PackageMissing;
    case upkg::AncestryEnd::ClassMissing: return ubundle::AncestryEnd::ClassMissing;
    }
    return ubundle::AncestryEnd::Root;
}

upkg::ObjectReference referenceTo(const upkg::Package& package, const upkg::ExportEntry& entry) {
    return upkg::ObjectReference{static_cast<std::int32_t>(&entry - package.exports().data()) + 1};
}

/// SS 4.5 "For each distinct class". readAncestry and effectiveDefaults run
/// here, once per class rather than once per actor.
Result<ubundle::ActorClass> classEntry(std::string path, const upkg::ClassSite& site,
                                       const upkg::PackageResolver& resolver,
                                       const PackageNames& names) {
    ubundle::ActorClass entry;
    entry.path = std::move(path);

    if (site.resolved.package == nullptr) {
        entry.resolved = false;
        entry.end = endOf(site.end);
        entry.missing = site.end == upkg::AncestryEnd::PackageMissing ? site.package : site.name;
        return entry;
    }

    entry.resolved = true;
    UTA_TRY(const upkg::Ancestry ancestry,
            upkg::readAncestry(*site.resolved.package, *site.resolved.entry, resolver));
    for (std::size_t i = 1; i < ancestry.chain.size(); ++i) {
        const upkg::ResolvedClass& link = ancestry.chain[i];
        UTA_TRY(const std::string_view packageName, nameOf(names, *link.package));
        UTA_TRY(std::string parent,
                objectPath(*link.package, packageName, referenceTo(*link.package, *link.entry)));
        entry.ancestry.push_back(std::move(parent));
    }

    // A class whose parent is missing says so here, with `resolved` still true.
    entry.end = endOf(ancestry.end);
    if (ancestry.end == upkg::AncestryEnd::PackageMissing)
        entry.missing = detail::fold(ancestry.missingPackage);
    else if (ancestry.end == upkg::AncestryEnd::ClassMissing)
        entry.missing = ancestry.missingClass;

    UTA_TRY(const std::vector<upkg::EffectiveProperty> merged, upkg::effectiveDefaults(ancestry));
    for (const upkg::EffectiveProperty& effective : merged) {
        UTA_TRY(const std::string_view packageName, nameOf(names, *effective.origin));
        UTA_TRY(PropertyRecord record,
                recordOf(*effective.origin, packageName, effective.name, effective.property));
        entry.defaults.push_back(std::move(record));
    }
    // Sorted by folded name, then array index. The merge made each pair unique.
    std::sort(entry.defaults.begin(), entry.defaults.end(),
              [](const PropertyRecord& a, const PropertyRecord& b) {
                  return std::make_tuple(detail::fold(a.name), a.arrayIndex)
                         < std::make_tuple(detail::fold(b.name), b.arrayIndex);
              });
    return entry;
}

/// SS 4.6: the actor's own record of `name` at array index 0 and of `kind`,
/// else its class's default of that name and kind, else none. `name` is
/// folded; a record of the right name and the wrong kind is passed over.
const PropertyRecord* lookUp(std::string_view name, ValueKind kind,
                             const std::vector<PropertyRecord>& own,
                             const std::vector<PropertyRecord>& defaults) {
    return detail::resolvedRecord(name, own, defaults,
                                  [kind](const PropertyRecord& record) { return record.kind == kind; });
}

/// SS 4.6: the actor's light, if its resolved LightType is not LT_None. A
/// field neither the actor nor its class sets is Actor.uc's default, zero.
std::optional<ubundle::Light> lightOf(std::uint32_t exportIndex,
                                      const std::vector<PropertyRecord>& own,
                                      const std::vector<PropertyRecord>& defaults) {
    const auto byteOf = [&](std::string_view name) -> std::uint8_t {
        const PropertyRecord* record = lookUp(name, ValueKind::Byte, own, defaults);
        return record == nullptr ? 0 : std::get<std::uint8_t>(record->value);
    };
    const auto flagOf = [&](std::string_view name) {
        const PropertyRecord* record = lookUp(name, ValueKind::Bool, own, defaults);
        return record != nullptr && std::get<bool>(record->value);
    };

    ubundle::Light light;
    light.type = byteOf("lighttype");
    if (light.type == 0) return std::nullopt;

    light.exportIndex = exportIndex;
    if (const PropertyRecord* location = lookUp("location", ValueKind::Vector, own, defaults))
        light.location = std::get<std::array<float, 3>>(location->value);
    if (const PropertyRecord* rotation = lookUp("rotation", ValueKind::Rotator, own, defaults))
        light.rotation = std::get<std::array<std::int32_t, 3>>(rotation->value);
    light.effect = byteOf("lighteffect");
    light.brightness = byteOf("lightbrightness");
    light.hue = byteOf("lighthue");
    light.saturation = byteOf("lightsaturation");
    light.radius = byteOf("lightradius");
    light.period = byteOf("lightperiod");
    light.phase = byteOf("lightphase");
    light.cone = byteOf("lightcone");
    light.volumeBrightness = byteOf("volumebrightness");
    light.volumeRadius = byteOf("volumeradius");
    light.volumeFog = byteOf("volumefog");
    light.specialLit = flagOf("bspeciallit");
    light.actorShadows = flagOf("bactorshadows");
    light.corona = flagOf("bcorona");
    light.lensFlare = flagOf("blensflare");
    return light;
}

} // namespace

namespace detail {

const PropertyRecord* resolvedRecord(std::string_view name, const std::vector<PropertyRecord>& own,
                                     const std::vector<PropertyRecord>& defaults,
                                     const std::function<bool(const PropertyRecord&)>& fits) {
    for (const std::vector<PropertyRecord>* list : {&own, &defaults})
        for (const PropertyRecord& record : *list)
            if (record.arrayIndex == 0 && fold(record.name) == name && fits(record)) return &record;
    return nullptr;
}

} // namespace detail

Result<Actors> buildActors(const upkg::Package& map, std::string_view mapName,
                           const upkg::Level& level, const upkg::PackageResolver& resolver) {
    PackageNames names;
    names.emplace(&map, std::string(mapName));
    const upkg::PackageResolver recorder = recording(resolver, names);

    // Keyed by folded path, so the map's own order is SS 4.4's bytewise one.
    std::map<std::string, ubundle::ActorClass> classes;
    std::map<std::int32_t, std::string> pathOfClass; // by the raw class reference
    std::vector<std::pair<ubundle::ActorPlacement, std::string>> placed;
    std::set<std::uint32_t> seen;
    std::vector<ubundle::Light> lights;

    const auto exports = map.exports();
    for (std::size_t slot = 0; slot < level.actors.size(); ++slot) {
        const upkg::ObjectReference reference = level.actors[slot];
        const std::string where = "actor slot " + std::to_string(slot);

        // Step 1. Level::actors comes from export data, which Package::open
        // did not validate.
        if (reference.kind() != upkg::ObjectReferenceKind::Export || reference.index() >= exports.size())
            return std::unexpected(malformed(where + " holds reference " + std::to_string(reference.raw())
                                             + ", which is not an export of the map"));
        if (!seen.insert(reference.index()).second)
            return std::unexpected(malformed(where + " names export " + std::to_string(reference.index())
                                             + ", which a slot before it already named"));
        const upkg::ExportEntry& entry = exports[reference.index()];
        if (entry.objectClass.kind() == upkg::ObjectReferenceKind::Null)
            return std::unexpected(malformed(where + " names export " + std::to_string(reference.index())
                                             + ", which is a class and not an actor"));

        ubundle::ActorPlacement placement;
        placement.exportIndex = reference.index();
        UTA_TRY(placement.path, objectPath(map, mapName, reference));

        // Step 2.
        auto properties = upkg::readProperties(map, entry);
        if (!properties.has_value())
            return std::unexpected(properties.error().withContext("reading actor " + placement.path));
        for (const upkg::Property& property : *properties) {
            UTA_TRY(const std::string_view name, map.name(property.nameIndex));
            UTA_TRY(PropertyRecord record, recordOf(map, mapName, std::string(name), property));
            placement.properties.push_back(std::move(record));
        }

        // Step 3, and the class's entry the first time it is met.
        std::string classPath;
        if (const auto known = pathOfClass.find(entry.objectClass.raw()); known != pathOfClass.end()) {
            classPath = known->second;
        } else {
            UTA_TRY(const upkg::ClassSite site,
                    upkg::resolveClass(map, mapName, entry.objectClass, recorder));
            UTA_TRY(classPath, objectPath(map, mapName, entry.objectClass));
            if (!classes.contains(classPath)) {
                UTA_TRY(ubundle::ActorClass built, classEntry(classPath, site, recorder, names));
                classes.emplace(classPath, std::move(built));
            }
            pathOfClass.emplace(entry.objectClass.raw(), classPath);
        }

        if (auto light = lightOf(placement.exportIndex, placement.properties,
                                 classes.at(classPath).defaults))
            lights.push_back(*light);
        placed.emplace_back(std::move(placement), std::move(classPath));
    }

    Actors actors;
    std::map<std::string, std::uint32_t> indexOf;
    for (auto& [path, actorClass] : classes) {
        indexOf.emplace(path, static_cast<std::uint32_t>(actors.placements.classes.size()));
        actors.placements.classes.push_back(std::move(actorClass));
    }

    // SS 4.4: actors and lights strictly ascending by export index. The level's
    // own order need not be.
    std::sort(placed.begin(), placed.end(), [](const auto& a, const auto& b) {
        return a.first.exportIndex < b.first.exportIndex;
    });
    for (auto& [placement, classPath] : placed) {
        placement.classIndex = indexOf.at(classPath);
        actors.placements.actors.push_back(std::move(placement));
    }
    std::sort(lights.begin(), lights.end(), [](const ubundle::Light& a, const ubundle::Light& b) {
        return a.exportIndex < b.exportIndex;
    });
    actors.lights = std::move(lights);
    return actors;
}

} // namespace uta::ubake
