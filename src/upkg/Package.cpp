#include "upkg/Package.h"

#include "upkg/ByteReader.h"

#include <string>

namespace uta::upkg {
namespace {

constexpr std::uint32_t SIGNATURE = 0x9E2A83C1u;
constexpr std::uint16_t MIN_VERSION = 61;
constexpr std::uint16_t MAX_VERSION = 69;

/// The version at which the name table gained a length prefix, and the one at
/// which the heritage list became a GUID and a generation list.
constexpr std::uint16_t VERSION_LENGTH_PREFIXED_NAMES = 64;
constexpr std::uint16_t VERSION_GUID_AND_GENERATIONS = 68;

// The smallest a single table entry can possibly be. These are what stop a
// header claiming millions of entries from causing a large allocation before
// a byte of table is read (INV-2): a count is rejected unless the file is at
// least big enough to hold that many minimal entries.
constexpr std::size_t MIN_NAME_ENTRY_BYTES = 5;   // null terminator + u32 flags
constexpr std::size_t MIN_IMPORT_ENTRY_BYTES = 7; // 3 indices + i32 outer
constexpr std::size_t MIN_EXPORT_ENTRY_BYTES = 12;
constexpr std::size_t GENERATION_BYTES = 8; // export count + name count

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

/// A count is credible only if the bytes after its table's offset could hold
/// that many minimal entries. Checked before any vector is sized.
Result<void> checkTable(const char* what, std::uint32_t count, std::uint32_t offset,
                        std::size_t minimumEntryBytes, std::size_t total) {
    if (count == 0) {
        return {};
    }
    if (offset >= total) {
        return std::unexpected(malformed(
            std::string(what) + " table offset " + std::to_string(offset) +
            " is past the end of a " + std::to_string(total) + "-byte package"));
    }
    const std::size_t available = total - offset;
    if (count > available / minimumEntryBytes) {
        return std::unexpected(malformed(
            std::string(what) + " table claims " + std::to_string(count) +
            " entries, which cannot fit in the " + std::to_string(available) +
            " bytes that follow its offset"));
    }
    return {};
}

Result<std::uint32_t> checkNameIndex(std::int32_t index, std::uint32_t nameCount,
                                     const char* what) {
    if (index < 0 || static_cast<std::uint32_t>(index) >= nameCount) {
        return std::unexpected(malformed(
            std::string(what) + " names index " + std::to_string(index) +
            ", and the name table holds " + std::to_string(nameCount) + " entries"));
    }
    return static_cast<std::uint32_t>(index);
}

/// Validated against the table the reference's SIGN names, which is what makes
/// ObjectReference::index() total afterwards.
Result<ObjectReference> checkReference(std::int32_t raw, std::uint32_t exportCount,
                                       std::uint32_t importCount, const char* what) {
    const ObjectReference reference{raw};
    switch (reference.kind()) {
    case ObjectReferenceKind::Null:
        return reference;
    case ObjectReferenceKind::Export:
        if (reference.index() >= exportCount) {
            return std::unexpected(malformed(
                std::string(what) + " references export " +
                std::to_string(reference.index()) + ", and there are " +
                std::to_string(exportCount)));
        }
        return reference;
    case ObjectReferenceKind::Import:
        if (reference.index() >= importCount) {
            return std::unexpected(malformed(
                std::string(what) + " references import " +
                std::to_string(reference.index()) + ", and there are " +
                std::to_string(importCount)));
        }
        return reference;
    }
    return std::unexpected(malformed(std::string(what) + " has an unreadable reference"));
}

Result<std::string> readName(ByteReader& reader, std::uint16_t version) {
    if (version >= VERSION_LENGTH_PREFIXED_NAMES) {
        UTA_TRY(const std::int32_t length, reader.readIndex());
        if (length <= 0) {
            return std::unexpected(malformed(
                "name table entry declares a length of " + std::to_string(length) +
                "; the terminating null alone makes the minimum one"));
        }
        UTA_TRY(const std::span<const std::byte> chars,
                reader.readBytes(static_cast<std::size_t>(length)));
        // The declared length includes the terminating null.
        std::string name;
        name.reserve(chars.size() - 1);
        for (std::size_t i = 0; i + 1 < chars.size(); ++i) {
            name.push_back(static_cast<char>(chars[i]));
        }
        return name;
    }

    // Below version 64 the length prefix is absent and the null ends the name.
    std::string name;
    for (;;) {
        UTA_TRY(const std::uint8_t byte, reader.readU8());
        if (byte == 0) {
            return name;
        }
        name.push_back(static_cast<char>(byte));
    }
}

} // namespace

Result<Package> Package::open(std::span<const std::byte> bytes) {
    Package package;
    package.bytes_ = bytes;

    ByteReader reader{bytes};

    UTA_TRY(const std::uint32_t signature, reader.readU32());
    if (signature != SIGNATURE) {
        return std::unexpected(malformed(
            "not an Unreal package: signature is 0x" + std::to_string(signature) +
            ", expected 0x9E2A83C1"));
    }

    UTA_TRY(package.header_.packageVersion, reader.readU16());
    UTA_TRY(package.header_.licenseeVersion, reader.readU16());

    // Checked before anything else is read. A version outside the range this
    // reader understands is refused rather than parsed as garbage -- section
    // 2.1 found a version 76 package in the reference install.
    const std::uint16_t version = package.header_.packageVersion;
    if (version < MIN_VERSION || version > MAX_VERSION) {
        return std::unexpected(
            Error(ErrorCode::UnsupportedVersion,
                  "package version " + std::to_string(version) +
                      " is outside the supported range " + std::to_string(MIN_VERSION) +
                      " to " + std::to_string(MAX_VERSION)));
    }

    UTA_TRY(package.header_.packageFlags, reader.readU32());

    UTA_TRY(const std::uint32_t nameCount, reader.readU32());
    UTA_TRY(const std::uint32_t nameOffset, reader.readU32());
    UTA_TRY(const std::uint32_t exportCount, reader.readU32());
    UTA_TRY(const std::uint32_t exportOffset, reader.readU32());
    UTA_TRY(const std::uint32_t importCount, reader.readU32());
    UTA_TRY(const std::uint32_t importOffset, reader.readU32());

    const std::size_t total = bytes.size();
    const std::size_t minimumNameEntry =
        version >= VERSION_LENGTH_PREFIXED_NAMES ? MIN_NAME_ENTRY_BYTES + 1
                                                 : MIN_NAME_ENTRY_BYTES;
    UTA_CHECK(checkTable("name", nameCount, nameOffset, minimumNameEntry, total));
    UTA_CHECK(checkTable("export", exportCount, exportOffset, MIN_EXPORT_ENTRY_BYTES, total));
    UTA_CHECK(checkTable("import", importCount, importOffset, MIN_IMPORT_ENTRY_BYTES, total));

    if (version < VERSION_GUID_AND_GENERATIONS) {
        // A heritage count and offset, which this reader does not use.
        UTA_CHECK(reader.skip(8));
    } else {
        UTA_TRY(const std::span<const std::byte> guid, reader.readBytes(16));
        for (std::size_t i = 0; i < package.header_.guid.size(); ++i) {
            package.header_.guid[i] = guid[i];
        }
        UTA_TRY(const std::uint32_t generations, reader.readU32());
        // Validated before it is used as a length, for the same reason the
        // table counts are.
        if (generations > reader.remaining() / GENERATION_BYTES) {
            return std::unexpected(malformed(
                "package claims " + std::to_string(generations) +
                " generations, which do not fit in the bytes that remain"));
        }
        UTA_CHECK(reader.skip(static_cast<std::size_t>(generations) * GENERATION_BYTES));
    }

    // The name table. Read first, because every other table validates its
    // indices against this one's size.
    package.names_.reserve(nameCount);
    UTA_CHECK(reader.seek(nameOffset));
    for (std::uint32_t i = 0; i < nameCount; ++i) {
        NameEntry entry;
        UTA_TRY(entry.name, readName(reader, version));
        UTA_TRY(entry.flags, reader.readU32());
        package.names_.push_back(std::move(entry));
    }

    // The import table.
    package.imports_.reserve(importCount);
    UTA_CHECK(reader.seek(importOffset));
    for (std::uint32_t i = 0; i < importCount; ++i) {
        ImportEntry entry;
        UTA_TRY(const std::int32_t classPackage, reader.readIndex());
        UTA_TRY(const std::int32_t className, reader.readIndex());
        UTA_TRY(const std::int32_t outer, reader.readI32());
        UTA_TRY(const std::int32_t objectName, reader.readIndex());

        UTA_TRY(entry.classPackage, checkNameIndex(classPackage, nameCount, "an import"));
        UTA_TRY(entry.className, checkNameIndex(className, nameCount, "an import"));
        UTA_TRY(entry.outer, checkReference(outer, exportCount, importCount, "an import"));
        UTA_TRY(entry.objectName, checkNameIndex(objectName, nameCount, "an import"));
        package.imports_.push_back(entry);
    }

    // The export table.
    package.exports_.reserve(exportCount);
    UTA_CHECK(reader.seek(exportOffset));
    for (std::uint32_t i = 0; i < exportCount; ++i) {
        ExportEntry entry;
        UTA_TRY(const std::int32_t objectClass, reader.readIndex());
        UTA_TRY(const std::int32_t super, reader.readIndex());
        UTA_TRY(const std::int32_t outer, reader.readI32());
        UTA_TRY(const std::int32_t objectName, reader.readIndex());
        UTA_TRY(entry.objectFlags, reader.readU32());
        UTA_TRY(const std::int32_t serialSize, reader.readIndex());

        UTA_TRY(entry.objectClass,
                checkReference(objectClass, exportCount, importCount, "an export"));
        UTA_TRY(entry.super, checkReference(super, exportCount, importCount, "an export"));
        UTA_TRY(entry.outer, checkReference(outer, exportCount, importCount, "an export"));
        UTA_TRY(entry.objectName, checkNameIndex(objectName, nameCount, "an export"));

        if (serialSize < 0) {
            return std::unexpected(malformed("an export declares a negative serial size"));
        }
        entry.serialSize = static_cast<std::size_t>(serialSize);

        // The offset is present only when the size is greater than zero.
        // Reading it unconditionally desynchronises the whole table from the
        // first sizeless export onward, and the file still parses (INV-7).
        if (entry.serialSize > 0) {
            UTA_TRY(const std::int32_t serialOffset, reader.readIndex());
            if (serialOffset < 0) {
                return std::unexpected(
                    malformed("an export declares a negative serial offset"));
            }
            entry.serialOffset = static_cast<std::size_t>(serialOffset);

            // Compared by subtraction rather than by adding offset and size,
            // which would wrap (INV-8).
            if (entry.serialOffset > total ||
                entry.serialSize > total - entry.serialOffset) {
                return std::unexpected(malformed(
                    "an export's serial range (" + std::to_string(entry.serialOffset) +
                    " + " + std::to_string(entry.serialSize) +
                    ") runs past the end of a " + std::to_string(total) +
                    "-byte package"));
            }
        }
        package.exports_.push_back(entry);
    }

    return package;
}

Result<std::string_view> Package::name(std::uint32_t index) const {
    if (index >= names_.size()) {
        return std::unexpected(Error(
            ErrorCode::InvalidArgument,
            "name index " + std::to_string(index) + " is past the end of a " +
                std::to_string(names_.size()) + "-entry name table"));
    }
    return std::string_view{names_[index].name};
}

Result<std::string_view> Package::objectName(ObjectReference reference) const {
    switch (reference.kind()) {
    case ObjectReferenceKind::Null:
        // What the format itself calls the absence of an object.
        return std::string_view{"None"};
    case ObjectReferenceKind::Export:
        if (reference.index() >= exports_.size()) {
            return std::unexpected(Error(ErrorCode::InvalidArgument,
                                         "export reference is out of range"));
        }
        return name(exports_[reference.index()].objectName);
    case ObjectReferenceKind::Import:
        if (reference.index() >= imports_.size()) {
            return std::unexpected(Error(ErrorCode::InvalidArgument,
                                         "import reference is out of range"));
        }
        return name(imports_[reference.index()].objectName);
    }
    return std::unexpected(Error(ErrorCode::InvalidArgument, "unreadable object reference"));
}

Result<std::span<const std::byte>> Package::serialBytes(const ExportEntry& entry) const {
    if (entry.serialSize == 0) {
        return std::span<const std::byte>{};
    }
    // Validated at open; re-checked here because the caller supplies the entry
    // and may not have got it from this package.
    if (entry.serialOffset > bytes_.size() ||
        entry.serialSize > bytes_.size() - entry.serialOffset) {
        return std::unexpected(malformed("an export's serial range is outside the package"));
    }
    return bytes_.subspan(entry.serialOffset, entry.serialSize);
}

} // namespace uta::upkg
