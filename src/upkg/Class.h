// A class export: what it descends from, and the values its author set on it.
//
// docs/specs/UTA-0005-class-tables-and-ancestry.md.
//
// ADR-0004 decided this engine understands a custom actor by its ancestry and
// its default properties rather than by running its UnrealScript. This is the
// half that reads. It resolves nothing onto this engine's own classes -- that
// is UTA-0023 -- and it executes no bytecode.
//
// A class's stored defaults are a DIFFERENCE against its parent's, so the
// values an author actually gets come from merging up the chain.
// `effectiveDefaults` is what does that; `readClass` returns the class's own.

#pragma once

#include "core/Error.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace uta::upkg {

struct ClassInfo {
    ObjectReference super;                  ///< the parent; null on Object
    std::uint32_t friendlyName = 0;         ///< name index
    std::uint32_t classFlags = 0;
    std::array<std::byte, 16> classGuid{};
    ObjectReference within;                 ///< version 62 and above
    std::uint32_t configName = 0;           ///< name index, version 62 and above
    std::vector<Property> defaults;         ///< this class's OWN defaults
};

/// Read one class export.
///
/// The parent is reported from `ExportEntry::super` rather than from the
/// `SuperField` inside the object's bytes, and this performs no reference
/// validation of its own: `Package::open` has already validated every
/// reference in both tables (UTA-0003 SS 4.6), so for a package that opened
/// the table column is in range. The in-data field is consumed and unused --
/// nothing has validated it, so a reader taking the parent from there owes a
/// bounds check the container already paid for. INV-2.
///
/// Returns InvalidArgument for an export whose class reference is not null,
/// and for one with no serialised data: a class with no bytes has no parent to
/// report, and an empty ClassInfo would hand the caller a null parent
/// indistinguishable from Object's. INV-10.
[[nodiscard]] Result<ClassInfo> readClass(const Package& package,
                                          const ExportEntry& entry);

/// Open a package by name, or report that it is not available.
///
/// Returns nullptr, with NO error, when the package simply is not present --
/// an install that lacks a mod is the ordinary case, not a malformed one.
///
/// The caller of `readAncestry` owns the lifetime of whatever this returns,
/// the same bargain `Package` already makes with the caller's bytes.
///
/// `readAncestry` folds the name to lower case before calling this, so a
/// resolver may assume folded input and must not do an exact filesystem
/// lookup on it -- otherwise `botpack` misses `BotPack.u` on a case-sensitive
/// filesystem and the walk reports an installed mod as absent, which is the
/// failure INV-7 exists to prevent.
using PackageResolver =
    std::function<Result<const Package*>(std::string_view packageName)>;

struct ResolvedClass {
    const Package* package = nullptr;
    const ExportEntry* entry = nullptr;
};

/// Why the walk stopped. Root is the ordinary end; the other two are
/// successful ends that say which content the install lacks.
enum class AncestryEnd {
    Root,           ///< reached a class with no parent
    PackageMissing, ///< the resolver did not supply the parent's package
    ClassMissing,   ///< a package opened and does not hold the parent class
};

struct Ancestry {
    std::vector<ResolvedClass> chain;   ///< the class itself first, root last
    AncestryEnd end = AncestryEnd::Root;
    std::string missingPackage;         ///< set on PackageMissing only
    std::string missingClass;           ///< set on PackageMissing and ClassMissing
};

/// Walk a class's parent chain, following imports into other packages.
///
/// Two ends are successful and they are different facts. PackageMissing is the
/// resolver declining to supply a package: both `missingPackage` and
/// `missingClass` are set, so a caller can say which content is absent.
/// ClassMissing is a package that opened and does not contain the class:
/// `missingClass` is set and `missingPackage` is EMPTY, because naming a
/// package that is present would report the opposite of what happened.
/// ADR-0004 requires exactly that legibility. INV-7.
///
/// A cycle, or a chain past the depth cap, is MalformedData: the walk
/// terminates on any input. INV-6.
[[nodiscard]] Result<Ancestry> readAncestry(const Package& package,
                                            const ExportEntry& entry,
                                            const PackageResolver& resolver);

/// One merged default property.
struct EffectiveProperty {
    /// The property's name as TEXT, in the spelling of the class nearest the
    /// leaf that set it. Name indices are per-package, so a value read from
    /// one package and a value read from another cannot be compared by index.
    std::string name;
    /// The package this value's name and object indices are relative to. Not
    /// optional bookkeeping: an object-valued default inherited from another
    /// package cannot be resolved without it. INV-9.
    const Package* origin = nullptr;
    Property property;
};

/// Merge a chain's defaults from the root down, each class overriding what it
/// names.
///
/// The merge key is the property's name as text, compared case-insensitively,
/// together with its array index -- never the name index. A name index is a
/// position in one package's name table, so merging by index silently fails to
/// override across a package boundary and the caller gets the base class's
/// value. INV-8.
///
/// An ancestry that ended PackageMissing or ClassMissing still merges, over
/// the part of the chain that resolved. The result is honest but incomplete,
/// and the caller knows which case it is from `Ancestry::end`.
[[nodiscard]] Result<std::vector<EffectiveProperty>> effectiveDefaults(
    const Ancestry& ancestry);

/// Where a class reference leads, and what it is called either way --
/// docs/specs/UTA-0110-lights-and-placements.md SS 4.2.
struct ClassSite {
    std::string package;    ///< folded; the map's own name for a class it exports
    std::string name;       ///< the class's name as spelled where it is referenced
    ResolvedClass resolved; ///< both null when the class was not found
    AncestryEnd end = AncestryEnd::Root; ///< why `resolved` is null; Root when it is not
};

/// The class a reference names, found the way `readAncestry` finds a parent.
///
/// An export of `package` is that export. An import names its outermost
/// package; that name is folded, handed to `resolver`, and the class export of
/// that package whose name matches, compared case-insensitively, is it. A
/// package the resolver does not supply leaves `resolved` null with `end`
/// PackageMissing; a package holding no such class, with ClassMissing. Neither
/// is an error, for ADR-0004's reason. UTA-0110 INV-3.
///
/// `packageName` is the name `package` goes by, used for a class it exports.
/// A null reference names no class and is InvalidArgument; one past its table
/// is MalformedData, since a reference read from object data is unvalidated.
[[nodiscard]] Result<ClassSite> resolveClass(const Package& package,
                                             std::string_view packageName,
                                             ObjectReference classReference,
                                             const PackageResolver& resolver);

} // namespace uta::upkg
