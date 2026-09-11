#include "upkg/Class.h"

#include "upkg/ByteReader.h"
#include "upkg/Script.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace uta::upkg {
namespace {

/// The deepest chain in the reference install is 12, so a cap comfortably
/// above that makes a cap that fires evidence of a malformed package rather
/// than of an unusually deep hierarchy. scripts/class-census.py re-derives it.
constexpr std::size_t MAX_ANCESTRY_DEPTH = 64;

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

/// The engine's own package, class and property names are case-insensitive, so
/// every comparison across a package boundary folds first. ASCII only, which
/// is what the format's names are.
std::string fold(std::string_view text) {
    std::string folded;
    folded.reserve(text.size());
    for (const char character : text) {
        folded.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
    }
    return folded;
}

/// A name index read out of an object's bytes, which nothing has validated.
Result<std::uint32_t> checkNameIndex(const Package& package, std::int32_t index,
                                     std::string_view field) {
    if (index < 0 || static_cast<std::size_t>(index) >= package.names().size()) {
        return std::unexpected(malformed(
            "a class's " + std::string(field) + " names index " +
            std::to_string(index) + ", which is outside the name table"));
    }
    return static_cast<std::uint32_t>(index);
}

/// The package an import ultimately lives in: follow its outer chain to the
/// root, whose name is the package's.
Result<std::string_view> importPackageName(const Package& package,
                                           ObjectReference reference) {
    for (std::size_t step = 0; step < MAX_ANCESTRY_DEPTH; ++step) {
        if (reference.kind() != ObjectReferenceKind::Import) {
            return std::unexpected(malformed(
                "an import's outer chain leaves the import table before reaching a root"));
        }
        const ImportEntry& import = package.imports()[reference.index()];
        if (import.outer.kind() == ObjectReferenceKind::Null) {
            return package.name(import.objectName);
        }
        reference = import.outer;
    }
    return std::unexpected(
        malformed("an import's outer chain does not terminate"));
}

/// Find a class export by name within one package, folded.
const ExportEntry* findClassExport(const Package& package, std::string_view wanted) {
    const std::string target = fold(wanted);
    for (const ExportEntry& candidate : package.exports()) {
        if (candidate.objectClass.kind() != ObjectReferenceKind::Null ||
            candidate.serialSize == 0) {
            continue;
        }
        const auto name = package.name(candidate.objectName);
        if (name.has_value() && fold(*name) == target) {
            return &candidate;
        }
    }
    return nullptr;
}

} // namespace

Result<ClassInfo> readClass(const Package& package, const ExportEntry& entry) {
    // A class export is recognised by a NULL class reference, not by one
    // naming `Class`, which no package writes (UTA-0003 SS 4.8).
    if (entry.objectClass.kind() != ObjectReferenceKind::Null) {
        return std::unexpected(Error(
            ErrorCode::InvalidArgument,
            "this export is not a class: its class reference is not null"));
    }

    UTA_TRY(const std::span<const std::byte> data, package.serialBytes(entry));
    if (data.empty()) {
        return std::unexpected(Error(
            ErrorCode::InvalidArgument,
            "this class export has no serialised data, so it has no parent to "
            "report; an empty result would be indistinguishable from Object's"));
    }

    ByteReader reader{data};
    if ((entry.objectFlags & OBJECT_FLAG_HAS_STACK) != 0) {
        UTA_CHECK(skipExecutionStackFrame(reader));
    }

    ClassInfo info;
    // The parent comes from the export table, which Package::open validated.
    // The in-data SuperField is consumed to reach what follows it, and is not
    // used (INV-2).
    UTA_TRY([[maybe_unused]] const std::int32_t superField, reader.readIndex());
    info.super = entry.super;

    UTA_TRY([[maybe_unused]] const std::int32_t next, reader.readIndex());
    UTA_TRY([[maybe_unused]] const std::int32_t scriptText, reader.readIndex());
    UTA_TRY([[maybe_unused]] const std::int32_t children, reader.readIndex());

    UTA_TRY(const std::int32_t friendlyName, reader.readIndex());
    UTA_TRY(info.friendlyName, checkNameIndex(package, friendlyName, "FriendlyName"));
    UTA_TRY([[maybe_unused]] const std::int32_t line, reader.readI32());
    UTA_TRY([[maybe_unused]] const std::int32_t textPos, reader.readI32());

    // Signed all the way in: skipScript refuses a negative value as its first
    // act, and narrowing it here is what would turn -1 into four billion.
    UTA_TRY(const std::int32_t scriptSize, reader.readI32());
    UTA_CHECK(skipScript(package, reader, scriptSize));

    // The state and class fields the format inherits through its own
    // hierarchy. All consumed, none interpreted -- so the 16-bit field's
    // signedness does not reach this reader.
    UTA_TRY([[maybe_unused]] const std::int64_t probeMask, reader.readI64());
    UTA_TRY([[maybe_unused]] const std::int64_t ignoreMask, reader.readI64());
    UTA_TRY([[maybe_unused]] const std::uint16_t labelTableOffset, reader.readU16());
    UTA_TRY([[maybe_unused]] const std::int32_t stateFlags, reader.readI32());

    UTA_TRY(info.classFlags, reader.readU32());
    UTA_TRY(const std::span<const std::byte> guid, reader.readBytes(16));
    std::copy(guid.begin(), guid.end(), info.classGuid.begin());

    UTA_TRY(const std::int32_t dependencyCount, reader.readIndex());
    if (dependencyCount < 0) {
        return std::unexpected(malformed("a class declares a negative dependency count"));
    }
    for (std::int32_t i = 0; i < dependencyCount; ++i) {
        UTA_TRY([[maybe_unused]] const std::int32_t dependency, reader.readIndex());
        UTA_TRY([[maybe_unused]] const std::int32_t depth, reader.readI32());
        UTA_TRY([[maybe_unused]] const std::uint32_t scriptTextCrc, reader.readU32());
    }

    UTA_TRY(const std::int32_t packageImportCount, reader.readIndex());
    if (packageImportCount < 0) {
        return std::unexpected(
            malformed("a class declares a negative package-import count"));
    }
    for (std::int32_t i = 0; i < packageImportCount; ++i) {
        UTA_TRY([[maybe_unused]] const std::int32_t imported, reader.readIndex());
    }

    // Version 62 is the format's boundary. Every class export in the reference
    // install is at 68 or 69, so the false arm here is covered by fixtures
    // alone (SS 7).
    if (package.header().packageVersion >= 62) {
        UTA_TRY(const std::int32_t within, reader.readIndex());
        info.within = ObjectReference{within};
        UTA_TRY(const std::int32_t configName, reader.readIndex());
        UTA_TRY(info.configName,
                checkNameIndex(package, configName, "ClassConfigName"));
    }

    // The defaults are the LAST thing in the export, which is why this cannot
    // use either entry point that starts at an export's beginning (SS 4.3
    // step 11).
    UTA_TRY(info.defaults, readPropertiesAt(package, reader));

    // Every reader here ends exactly where its export ends (SS 4.8). It is
    // what makes the field order above checkable against content this project
    // did not write.
    if (reader.position() != data.size()) {
        return std::unexpected(malformed(
            "a class export ended at " + std::to_string(reader.position()) +
            " of " + std::to_string(data.size()) + " bytes"));
    }
    return info;
}

Result<Ancestry> readAncestry(const Package& package, const ExportEntry& entry,
                              const PackageResolver& resolver) {
    Ancestry ancestry;
    std::vector<std::pair<const Package*, const ExportEntry*>> visited;

    const Package* currentPackage = &package;
    const ExportEntry* currentEntry = &entry;

    for (std::size_t depth = 0; depth < MAX_ANCESTRY_DEPTH; ++depth) {
        const auto here = std::make_pair(currentPackage, currentEntry);
        if (std::find(visited.begin(), visited.end(), here) != visited.end()) {
            return std::unexpected(malformed(
                "a class's parent chain returns to a class it already visited"));
        }
        visited.push_back(here);
        ancestry.chain.push_back(ResolvedClass{currentPackage, currentEntry});

        UTA_TRY(const ClassInfo info, readClass(*currentPackage, *currentEntry));

        if (info.super.kind() == ObjectReferenceKind::Null) {
            ancestry.end = AncestryEnd::Root;
            return ancestry;
        }

        if (info.super.kind() == ObjectReferenceKind::Export) {
            // In range because Package::open validated it (INV-2).
            currentEntry = &currentPackage->exports()[info.super.index()];
            continue;
        }

        // An import: the class lives in another package.
        const ImportEntry& import = currentPackage->imports()[info.super.index()];
        UTA_TRY(const std::string_view className,
                currentPackage->name(import.objectName));
        UTA_TRY(const std::string_view packageName,
                importPackageName(*currentPackage, info.super));

        // Folded before the resolver sees it, so a resolver may assume folded
        // input rather than each side guessing (INV-7).
        UTA_TRY(const Package* const opened, resolver(fold(packageName)));
        if (opened == nullptr) {
            ancestry.end = AncestryEnd::PackageMissing;
            ancestry.missingPackage = std::string(packageName);
            ancestry.missingClass = std::string(className);
            return ancestry;
        }

        const ExportEntry* const found = findClassExport(*opened, className);
        if (found == nullptr) {
            // missingPackage stays EMPTY: naming a package that is present
            // would report the opposite of what happened.
            ancestry.end = AncestryEnd::ClassMissing;
            ancestry.missingClass = std::string(className);
            return ancestry;
        }

        currentPackage = opened;
        currentEntry = found;
    }

    return std::unexpected(malformed(
        "a class's parent chain is deeper than " +
        std::to_string(MAX_ANCESTRY_DEPTH) + " levels"));
}

Result<std::vector<EffectiveProperty>> effectiveDefaults(const Ancestry& ancestry) {
    std::vector<EffectiveProperty> merged;

    // From the root down, so each class overrides what it names. The chain
    // holds the class itself first, so it is walked backwards.
    for (auto member = ancestry.chain.rbegin(); member != ancestry.chain.rend();
         ++member) {
        const Package& owner = *member->package;
        UTA_TRY(const ClassInfo info, readClass(owner, *member->entry));

        for (const Property& property : info.defaults) {
            UTA_TRY(const std::string_view name, owner.name(property.nameIndex));
            const std::string key = fold(name);

            // Keyed on the name as TEXT and the array index, never the name
            // index: the same property is a different number in every package,
            // so an index key silently fails to override across a boundary and
            // the caller gets the base class's value (INV-8).
            const auto existing = std::find_if(
                merged.begin(), merged.end(), [&](const EffectiveProperty& candidate) {
                    return candidate.property.arrayIndex == property.arrayIndex &&
                           fold(candidate.name) == key;
                });

            if (existing == merged.end()) {
                merged.push_back(EffectiveProperty{std::string(name), &owner, property});
                continue;
            }
            // The spelling of the class nearest the leaf that set the value is
            // the one an author last wrote, so it replaces the parent's.
            existing->name = std::string(name);
            existing->origin = &owner;
            existing->property = property;
        }
    }

    return merged;
}

Result<ClassSite> resolveClass(const Package& package, std::string_view packageName,
                               ObjectReference classReference,
                               const PackageResolver& resolver) {
    ClassSite site;
    switch (classReference.kind()) {
    case ObjectReferenceKind::Null:
        return std::unexpected(
            Error(ErrorCode::InvalidArgument, "a null class reference names no class"));

    case ObjectReferenceKind::Export: {
        if (classReference.index() >= package.exports().size()) {
            return std::unexpected(malformed(
                "a class reference names export " + std::to_string(classReference.index()) +
                ", past the export table"));
        }
        const ExportEntry& entry = package.exports()[classReference.index()];
        UTA_TRY(const std::string_view name, package.name(entry.objectName));
        site.package = fold(packageName);
        site.name = std::string(name);
        site.resolved = ResolvedClass{&package, &entry};
        return site;
    }

    case ObjectReferenceKind::Import: {
        if (classReference.index() >= package.imports().size()) {
            return std::unexpected(malformed(
                "a class reference names import " + std::to_string(classReference.index()) +
                ", past the import table"));
        }
        const ImportEntry& import = package.imports()[classReference.index()];
        UTA_TRY(const std::string_view name, package.name(import.objectName));
        UTA_TRY(const std::string_view home, importPackageName(package, classReference));
        site.package = fold(home);
        site.name = std::string(name);

        // Folded before the resolver sees it, as readAncestry's own calls are.
        UTA_TRY(const Package* const opened, resolver(site.package));
        if (opened == nullptr) {
            site.end = AncestryEnd::PackageMissing;
            return site;
        }
        const ExportEntry* const found = findClassExport(*opened, name);
        if (found == nullptr) {
            site.end = AncestryEnd::ClassMissing;
            return site;
        }
        site.resolved = ResolvedClass{opened, found};
        return site;
    }
    }
    return std::unexpected(malformed("a class reference of no known kind"));
}

} // namespace uta::upkg
