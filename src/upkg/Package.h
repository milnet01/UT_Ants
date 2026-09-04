// An opened Unreal Engine 1 package: its header, its three tables, and the
// byte range of any object's serialised data.
//
// docs/specs/UTA-0003-package-container.md SS 4.4 to SS 4.7.
//
// LIFETIME: a Package holds a VIEW of the caller's bytes and copies no
// package data. The caller must keep those bytes alive for the Package's
// lifetime. Copying every package instead was rejected in SS 8 -- section 2.1
// measured how large they get -- and the choice stays with the caller so that
// memory-mapping one later is a caller's decision rather than a rewrite.
//
// Everything reachable from an opened Package is in range. Every name index
// and every object reference is validated as its table is read, so a package
// that opened cannot later hand out an out-of-range index (INV-6). That is
// why validation is not deferred to the accessors.

#pragma once

#include "core/Error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uta::upkg {

struct PackageHeader {
    std::uint16_t packageVersion = 0;
    std::uint16_t licenseeVersion = 0;
    std::uint32_t packageFlags = 0;
    std::array<std::byte, 16> guid{}; // zeroed below version 68
};

enum class ObjectReferenceKind { Null, Export, Import };

/// A reference to an object in this package or in another.
///
/// A positive value is the export at `value - 1`; a negative value is the
/// import at `-value - 1`; zero is null. The off-by-one is the format's, and
/// it is why this is a type rather than a bare std::int32_t: subtracting one
/// at each call site is subtracting it in some of them.
class ObjectReference {
public:
    explicit ObjectReference(std::int32_t raw = 0) noexcept : raw_(raw) {}

    [[nodiscard]] ObjectReferenceKind kind() const noexcept {
        if (raw_ == 0) {
            return ObjectReferenceKind::Null;
        }
        return raw_ > 0 ? ObjectReferenceKind::Export : ObjectReferenceKind::Import;
    }

    /// Zero-based index into the relevant table. Meaningless when kind() is
    /// Null. Total for any reference reachable from an opened Package.
    [[nodiscard]] std::uint32_t index() const noexcept {
        // Negate through a wider type: raw_ can be INT32_MIN, whose magnitude
        // no std::int32_t holds.
        if (raw_ < 0) {
            return static_cast<std::uint32_t>(-static_cast<std::int64_t>(raw_) - 1);
        }
        return static_cast<std::uint32_t>(raw_ > 0 ? raw_ - 1 : 0);
    }

    [[nodiscard]] std::int32_t raw() const noexcept { return raw_; }

private:
    std::int32_t raw_ = 0;
};

struct NameEntry {
    std::string name;
    std::uint32_t flags = 0;
};

struct ImportEntry {
    std::uint32_t classPackage = 0; // name index
    std::uint32_t className = 0;    // name index
    ObjectReference outer;
    std::uint32_t objectName = 0; // name index
};

struct ExportEntry {
    ObjectReference objectClass;
    ObjectReference super;
    ObjectReference outer;
    std::uint32_t objectName = 0; // name index
    std::uint32_t objectFlags = 0;
    std::size_t serialOffset = 0; // both zero when the object has no data
    std::size_t serialSize = 0;
};

/// The object flag saying an object's serialised data is prefixed by an
/// execution-stack frame (SS 4.8).
inline constexpr std::uint32_t OBJECT_FLAG_HAS_STACK = 0x02000000u;

class Package {
public:
    [[nodiscard]] static Result<Package> open(std::span<const std::byte> bytes);

    [[nodiscard]] const PackageHeader& header() const noexcept { return header_; }
    [[nodiscard]] std::span<const NameEntry> names() const noexcept { return names_; }
    [[nodiscard]] std::span<const ImportEntry> imports() const noexcept { return imports_; }
    [[nodiscard]] std::span<const ExportEntry> exports() const noexcept { return exports_; }

    [[nodiscard]] Result<std::string_view> name(std::uint32_t index) const;

    /// "None" for a null reference; otherwise the referenced entry's name.
    [[nodiscard]] Result<std::string_view> objectName(ObjectReference reference) const;

    /// An empty span when the export has no serialised data.
    [[nodiscard]] Result<std::span<const std::byte>> serialBytes(
        const ExportEntry& entry) const;

private:
    Package() = default;

    std::span<const std::byte> bytes_;
    PackageHeader header_;
    std::vector<NameEntry> names_;
    std::vector<ImportEntry> imports_;
    std::vector<ExportEntry> exports_;
};

} // namespace uta::upkg
