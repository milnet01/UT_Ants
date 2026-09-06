#include "support/UnrealPackageBuilder.h"

#include <bit>

namespace uta::test {
namespace {

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void appendI32(std::vector<std::uint8_t>& out, std::int32_t value) {
    appendU32(out, static_cast<std::uint32_t>(value));
}

void appendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void appendFloat(std::vector<std::uint8_t>& out, float value) {
    appendU32(out, std::bit_cast<std::uint32_t>(value));
}

void appendIndex(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::vector<std::uint8_t> encoded = encodeCompactIndex(value);
    out.insert(out.end(), encoded.begin(), encoded.end());
}

void appendAll(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void writeU32At(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[offset + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

/// The size code for a property body of `size` bytes, and the extra size field
/// the code implies. Codes 0 to 4 name a fixed width; 5, 6 and 7 mean the size
/// follows as a u8, u16 or u32.
void appendSizeField(std::vector<std::uint8_t>& out, std::uint8_t& sizeCode,
                     std::size_t size) {
    switch (size) {
    case 1: sizeCode = 0; return;
    case 2: sizeCode = 1; return;
    case 4: sizeCode = 2; return;
    case 12: sizeCode = 3; return;
    case 16: sizeCode = 4; return;
    default: break;
    }
    if (size <= 0xFFu) {
        sizeCode = 5;
        out.push_back(static_cast<std::uint8_t>(size));
    } else if (size <= 0xFFFFu) {
        sizeCode = 6;
        appendU16(out, static_cast<std::uint16_t>(size));
    } else {
        sizeCode = 7;
        appendU32(out, static_cast<std::uint32_t>(size));
    }
}

} // namespace

std::vector<std::uint8_t> encodeCompactIndex(std::int32_t value) {
    std::vector<std::uint8_t> out;

    // Negate through int64_t: the magnitude of INT32_MIN does not fit in an
    // int32_t, so negating in place would be undefined.
    const bool negative = value < 0;
    auto magnitude = static_cast<std::uint32_t>(
        negative ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value));

    // First byte: sign in bit 7, continuation in bit 6, six value bits.
    std::uint8_t first = static_cast<std::uint8_t>(magnitude & 0x3Fu);
    if (negative) {
        first |= 0x80u;
    }
    magnitude >>= 6;
    if (magnitude != 0) {
        first |= 0x40u;
    }
    out.push_back(first);

    // Later bytes: continuation in bit 7, seven value bits. A 32-bit magnitude
    // needs at most four of them after the six bits already written.
    while (magnitude != 0) {
        std::uint8_t next = static_cast<std::uint8_t>(magnitude & 0x7Fu);
        magnitude >>= 7;
        if (magnitude != 0) {
            next |= 0x80u;
        }
        out.push_back(next);
    }

    return out;
}

std::vector<std::uint8_t> encodeArrayIndex(std::uint32_t value) {
    // The marker bits lead and the value's high bits follow, so this writes
    // most-significant byte first -- the one place the format is not
    // little-endian.
    if (value < 0x80u) {
        return {static_cast<std::uint8_t>(value)};
    }
    if (value < 0x4000u) {
        // Bit 7 set and bit 6 clear marks the two-byte form.
        return {static_cast<std::uint8_t>(0x80u | (value >> 8)),
                static_cast<std::uint8_t>(value & 0xFFu)};
    }
    // Both marker bits set marks the four-byte form, leaving six value bits in
    // the leading byte.
    return {static_cast<std::uint8_t>(0xC0u | ((value >> 24) & 0x3Fu)),
            static_cast<std::uint8_t>((value >> 16) & 0xFFu),
            static_cast<std::uint8_t>((value >> 8) & 0xFFu),
            static_cast<std::uint8_t>(value & 0xFFu)};
}

std::span<const std::byte> asBytes(const std::vector<std::uint8_t>& bytes) noexcept {
    return {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
}

// --- TaggedPropertyWriter ---------------------------------------------------

TaggedPropertyWriter& TaggedPropertyWriter::addRaw(std::int32_t nameIndex, PropertyType type,
                                                   const std::vector<std::uint8_t>& body) {
    appendIndex(body_, nameIndex);

    std::vector<std::uint8_t> sizeField;
    std::uint8_t sizeCode = 0;
    appendSizeField(sizeField, sizeCode, body.size());

    body_.push_back(static_cast<std::uint8_t>(static_cast<std::uint8_t>(type) |
                                              static_cast<std::uint8_t>(sizeCode << 4)));
    appendAll(body_, sizeField);
    appendAll(body_, body);
    return *this;
}

TaggedPropertyWriter& TaggedPropertyWriter::addByte(std::int32_t nameIndex,
                                                    std::uint8_t value) {
    return addRaw(nameIndex, PropertyType::Byte, {value});
}

TaggedPropertyWriter& TaggedPropertyWriter::addInt(std::int32_t nameIndex,
                                                   std::int32_t value) {
    std::vector<std::uint8_t> body;
    appendI32(body, value);
    return addRaw(nameIndex, PropertyType::Int, body);
}

TaggedPropertyWriter& TaggedPropertyWriter::addFloat(std::int32_t nameIndex, float value) {
    std::vector<std::uint8_t> body;
    appendFloat(body, value);
    return addRaw(nameIndex, PropertyType::Float, body);
}

TaggedPropertyWriter& TaggedPropertyWriter::addObject(std::int32_t nameIndex,
                                                      std::int32_t reference) {
    std::vector<std::uint8_t> body = encodeCompactIndex(reference);
    return addRaw(nameIndex, PropertyType::Object, body);
}

TaggedPropertyWriter& TaggedPropertyWriter::addClass(std::int32_t nameIndex,
                                                     std::int32_t reference) {
    std::vector<std::uint8_t> body = encodeCompactIndex(reference);
    return addRaw(nameIndex, PropertyType::Class, body);
}

TaggedPropertyWriter& TaggedPropertyWriter::addName(std::int32_t nameIndex,
                                                    std::int32_t valueNameIndex) {
    std::vector<std::uint8_t> body = encodeCompactIndex(valueNameIndex);
    return addRaw(nameIndex, PropertyType::Name, body);
}

TaggedPropertyWriter& TaggedPropertyWriter::addStr(std::int32_t nameIndex,
                                                   std::string_view value) {
    std::vector<std::uint8_t> body =
        encodeCompactIndex(static_cast<std::int32_t>(value.size() + 1));
    body.insert(body.end(), value.begin(), value.end());
    body.push_back(0);
    return addRaw(nameIndex, PropertyType::Str, body);
}

TaggedPropertyWriter& TaggedPropertyWriter::addString(std::int32_t nameIndex,
                                                      std::string_view value) {
    std::vector<std::uint8_t> body(value.begin(), value.end());
    body.push_back(0);
    return addRaw(nameIndex, PropertyType::String, body);
}

TaggedPropertyWriter& TaggedPropertyWriter::addVector(std::int32_t nameIndex, float x,
                                                      float y, float z) {
    std::vector<std::uint8_t> body;
    appendFloat(body, x);
    appendFloat(body, y);
    appendFloat(body, z);
    return addRaw(nameIndex, PropertyType::Vector, body);
}

TaggedPropertyWriter& TaggedPropertyWriter::addRotator(std::int32_t nameIndex,
                                                       std::int32_t pitch, std::int32_t yaw,
                                                       std::int32_t roll) {
    std::vector<std::uint8_t> body;
    appendI32(body, pitch);
    appendI32(body, yaw);
    appendI32(body, roll);
    return addRaw(nameIndex, PropertyType::Rotator, body);
}

TaggedPropertyWriter& TaggedPropertyWriter::addBool(std::int32_t nameIndex, bool value) {
    appendIndex(body_, nameIndex);

    // Size code 5 with a declared size of zero: the value is in bit 7 of the
    // info byte and no value bytes follow, but the size byte itself is
    // present, exactly as every Bool tag in real content writes it.
    std::uint8_t info = static_cast<std::uint8_t>(PropertyType::Bool) |
                        static_cast<std::uint8_t>(5u << 4);
    if (value) {
        info |= 0x80u;
    }
    body_.push_back(info);
    body_.push_back(0);
    return *this;
}

TaggedPropertyWriter& TaggedPropertyWriter::addIntAt(std::int32_t nameIndex,
                                                     std::uint32_t arrayIndex,
                                                     std::int32_t value) {
    appendIndex(body_, nameIndex);

    std::vector<std::uint8_t> body;
    appendI32(body, value);

    std::vector<std::uint8_t> sizeField;
    std::uint8_t sizeCode = 0;
    appendSizeField(sizeField, sizeCode, body.size());

    // Bit 7 is the array flag for every type but Bool.
    body_.push_back(static_cast<std::uint8_t>(static_cast<std::uint8_t>(PropertyType::Int) |
                                              static_cast<std::uint8_t>(sizeCode << 4) |
                                              0x80u));
    appendAll(body_, sizeField);
    appendAll(body_, encodeArrayIndex(arrayIndex));
    appendAll(body_, body);
    return *this;
}

TaggedPropertyWriter& TaggedPropertyWriter::addUndecodedStruct(
    std::int32_t nameIndex, std::int32_t structNameIndex,
    const std::vector<std::uint8_t>& raw) {
    appendIndex(body_, nameIndex);

    std::vector<std::uint8_t> sizeField;
    std::uint8_t sizeCode = 0;
    appendSizeField(sizeField, sizeCode, raw.size());

    body_.push_back(static_cast<std::uint8_t>(static_cast<std::uint8_t>(PropertyType::Struct) |
                                              static_cast<std::uint8_t>(sizeCode << 4)));
    // The struct name comes between the info byte and the size field.
    appendIndex(body_, structNameIndex);
    appendAll(body_, sizeField);
    appendAll(body_, raw);
    return *this;
}

TaggedPropertyWriter& TaggedPropertyWriter::setStackFrame(std::int32_t node,
                                                          std::int32_t stateNode,
                                                          std::int64_t probeMask,
                                                          std::int32_t latentAction,
                                                          std::int32_t offset) {
    stackFrame_.clear();
    appendIndex(stackFrame_, node);
    appendIndex(stackFrame_, stateNode);
    appendU64(stackFrame_, static_cast<std::uint64_t>(probeMask));
    appendI32(stackFrame_, latentAction);
    if (node != 0) {
        appendIndex(stackFrame_, offset);
    }
    return *this;
}

std::vector<std::uint8_t> TaggedPropertyWriter::buildWithoutTerminator() const {
    std::vector<std::uint8_t> out = stackFrame_;
    appendAll(out, body_);
    return out;
}

std::vector<std::uint8_t> TaggedPropertyWriter::build(std::int32_t noneNameIndex) const {
    std::vector<std::uint8_t> out = buildWithoutTerminator();
    appendIndex(out, noneNameIndex);
    return out;
}

// --- UnrealPackageBuilder ---------------------------------------------------

UnrealPackageBuilder& UnrealPackageBuilder::setPackageVersion(std::uint16_t version) {
    packageVersion_ = version;
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::setLicenseeVersion(std::uint16_t version) {
    licenseeVersion_ = version;
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::setPackageFlags(std::uint32_t flags) {
    packageFlags_ = flags;
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::addName(std::string_view name,
                                                    std::uint32_t flags) {
    names_.push_back(NameEntry{std::string(name), flags});
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::addImport(ImportEntry entry) {
    imports_.push_back(std::move(entry));
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::addExport(ExportEntry entry) {
    exports_.push_back(std::move(entry));
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::overrideSignature(std::uint32_t value) {
    signatureOverride_ = value;
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::overrideNameCount(std::uint32_t value) {
    nameCountOverride_ = value;
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::overrideNameOffset(std::uint32_t value) {
    nameOffsetOverride_ = value;
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::overrideExportCount(std::uint32_t value) {
    exportCountOverride_ = value;
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::overrideExportOffset(std::uint32_t value) {
    exportOffsetOverride_ = value;
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::overrideImportCount(std::uint32_t value) {
    importCountOverride_ = value;
    return *this;
}

UnrealPackageBuilder& UnrealPackageBuilder::overrideImportOffset(std::uint32_t value) {
    importOffsetOverride_ = value;
    return *this;
}

std::vector<std::uint8_t> UnrealPackageBuilder::build() const {
    std::vector<std::uint8_t> out;

    appendU32(out, signatureOverride_.value_or(SIGNATURE));
    appendU16(out, packageVersion_);
    appendU16(out, licenseeVersion_);
    appendU32(out, packageFlags_);

    appendU32(out, nameCountOverride_.value_or(static_cast<std::uint32_t>(names_.size())));
    const std::size_t nameOffsetField = out.size();
    appendU32(out, 0); // patched once the table's position is known

    appendU32(out,
              exportCountOverride_.value_or(static_cast<std::uint32_t>(exports_.size())));
    const std::size_t exportOffsetField = out.size();
    appendU32(out, 0);

    appendU32(out,
              importCountOverride_.value_or(static_cast<std::uint32_t>(imports_.size())));
    const std::size_t importOffsetField = out.size();
    appendU32(out, 0);

    if (packageVersion_ < 68) {
        // Older packages carry a heritage list where 68 and up carry a GUID
        // and generations. A reader skips it, so an empty one is enough.
        appendU32(out, 0); // heritage count
        appendU32(out, 0); // heritage offset
    } else {
        for (int i = 0; i < 16; ++i) {
            out.push_back(0); // GUID
        }
        appendU32(out, 1); // one generation
        appendU32(out, static_cast<std::uint32_t>(exports_.size()));
        appendU32(out, static_cast<std::uint32_t>(names_.size()));
    }

    // Each export's serialised bytes, before the table that names them, so
    // every offset is known by the time it is written.
    std::vector<std::uint32_t> serialOffsets(exports_.size(), 0);
    for (std::size_t i = 0; i < exports_.size(); ++i) {
        if (exports_[i].serialData.empty()) {
            continue;
        }
        serialOffsets[i] = static_cast<std::uint32_t>(out.size());
        appendAll(out, exports_[i].serialData);
    }

    // The name table. At version 64 and up each entry is its length as a
    // compact index, the characters, a terminating null, then the flags;
    // below 64 the length prefix is absent and the null alone ends the name.
    const auto nameTableOffset = static_cast<std::uint32_t>(out.size());
    for (const NameEntry& entry : names_) {
        if (packageVersion_ >= 64) {
            appendIndex(out, static_cast<std::int32_t>(entry.name.size() + 1));
        }
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        out.push_back(0);
        appendU32(out, entry.flags);
    }
    writeU32At(out, nameOffsetField, nameOffsetOverride_.value_or(nameTableOffset));

    // The export table. Serial offset is written only when serial size is
    // greater than zero -- reading it unconditionally is what desynchronises
    // the table from the first sizeless export onward.
    const auto exportTableOffset = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < exports_.size(); ++i) {
        const ExportEntry& entry = exports_[i];
        appendIndex(out, entry.objectClass);
        appendIndex(out, entry.super);
        appendI32(out, entry.outer);
        appendIndex(out, entry.objectName);
        appendU32(out, entry.objectFlags);

        const std::uint32_t size = entry.serialSizeOverride.value_or(
            static_cast<std::uint32_t>(entry.serialData.size()));
        appendIndex(out, static_cast<std::int32_t>(size));
        if (size > 0) {
            appendIndex(out, static_cast<std::int32_t>(
                                 entry.serialOffsetOverride.value_or(serialOffsets[i])));
        }
    }
    writeU32At(out, exportOffsetField, exportOffsetOverride_.value_or(exportTableOffset));

    // The import table.
    const auto importTableOffset = static_cast<std::uint32_t>(out.size());
    for (const ImportEntry& entry : imports_) {
        appendIndex(out, entry.classPackage);
        appendIndex(out, entry.className);
        appendI32(out, entry.outer);
        appendIndex(out, entry.objectName);
    }
    writeU32At(out, importOffsetField, importOffsetOverride_.value_or(importTableOffset));

    return out;
}

// --- the class export -------------------------------------------------
//
// Field order is docs/specs/UTA-0005-class-tables-and-ancestry.md SS 4.3.
// Written flat, in the order the bytes arrive, because a reader has no use
// for the hierarchy the format inherits these fields through.

ClassExportWriter& ClassExportWriter::setSuperField(std::int32_t reference) {
    superField_ = reference;
    return *this;
}

ClassExportWriter& ClassExportWriter::setNext(std::int32_t reference) {
    next_ = reference;
    return *this;
}

ClassExportWriter& ClassExportWriter::setScriptText(std::int32_t reference) {
    scriptText_ = reference;
    return *this;
}

ClassExportWriter& ClassExportWriter::setChildren(std::int32_t reference) {
    children_ = reference;
    return *this;
}

ClassExportWriter& ClassExportWriter::setFriendlyName(std::int32_t nameIndex) {
    friendlyName_ = nameIndex;
    return *this;
}

ClassExportWriter& ClassExportWriter::setLine(std::int32_t line) {
    line_ = line;
    return *this;
}

ClassExportWriter& ClassExportWriter::setTextPos(std::int32_t textPos) {
    textPos_ = textPos;
    return *this;
}

ClassExportWriter& ClassExportWriter::setScript(std::vector<std::uint8_t> body,
                                                std::int32_t memorySize) {
    script_ = std::move(body);
    scriptSize_ = memorySize;
    return *this;
}

ClassExportWriter& ClassExportWriter::setScriptSizeOverride(std::int32_t scriptSize) {
    scriptSizeOverride_ = scriptSize;
    return *this;
}

ClassExportWriter& ClassExportWriter::setClassFlags(std::uint32_t flags) {
    classFlags_ = flags;
    return *this;
}

ClassExportWriter& ClassExportWriter::setClassGuid(const std::vector<std::uint8_t>& guid) {
    classGuid_ = guid;
    classGuid_.resize(16, 0);
    return *this;
}

ClassExportWriter& ClassExportWriter::addDependency(std::int32_t reference,
                                                    std::int32_t depth,
                                                    std::uint32_t scriptTextCrc) {
    dependencies_.push_back({static_cast<std::int64_t>(reference),
                             static_cast<std::int64_t>(depth),
                             static_cast<std::int64_t>(scriptTextCrc)});
    return *this;
}

ClassExportWriter& ClassExportWriter::addPackageImport(std::int32_t nameIndex) {
    packageImports_.push_back(nameIndex);
    return *this;
}

ClassExportWriter& ClassExportWriter::setWithin(std::int32_t reference) {
    within_ = reference;
    return *this;
}

ClassExportWriter& ClassExportWriter::setConfigName(std::int32_t nameIndex) {
    configName_ = nameIndex;
    return *this;
}

ClassExportWriter& ClassExportWriter::setDefaults(std::vector<std::uint8_t> propertyList) {
    defaults_ = std::move(propertyList);
    return *this;
}

ClassExportWriter& ClassExportWriter::setStackFrame(std::int32_t node,
                                                    std::int32_t stateNode,
                                                    std::int64_t probeMask,
                                                    std::int32_t latentAction,
                                                    std::int32_t offset) {
    stackFrame_.clear();
    appendIndex(stackFrame_, node);
    appendIndex(stackFrame_, stateNode);
    appendU64(stackFrame_, static_cast<std::uint64_t>(probeMask));
    appendI32(stackFrame_, latentAction);
    // The format writes the trailing offset only for a non-null first
    // reference, and the reader keys on the same condition.
    if (node != 0) {
        appendIndex(stackFrame_, offset);
    }
    return *this;
}

std::vector<std::uint8_t> ClassExportWriter::build(std::uint16_t packageVersion) const {
    std::vector<std::uint8_t> out;
    appendAll(out, stackFrame_);

    appendIndex(out, superField_);
    appendIndex(out, next_);
    appendIndex(out, scriptText_);
    appendIndex(out, children_);
    appendIndex(out, friendlyName_);
    appendI32(out, line_);
    appendI32(out, textPos_);

    appendI32(out, scriptSizeOverride_ ? *scriptSizeOverride_ : scriptSize_);
    appendAll(out, script_);

    appendU64(out, 0);            // ProbeMask
    appendU64(out, 0);            // IgnoreMask
    appendU16(out, 0);            // LabelTableOffset
    appendI32(out, 0);            // StateFlags
    appendU32(out, classFlags_);
    appendAll(out, classGuid_);

    appendIndex(out, static_cast<std::int32_t>(dependencies_.size()));
    for (const auto& dependency : dependencies_) {
        appendIndex(out, static_cast<std::int32_t>(dependency[0]));
        appendI32(out, static_cast<std::int32_t>(dependency[1]));
        appendU32(out, static_cast<std::uint32_t>(dependency[2]));
    }

    appendIndex(out, static_cast<std::int32_t>(packageImports_.size()));
    for (const std::int32_t nameIndex : packageImports_) {
        appendIndex(out, nameIndex);
    }

    // Version 62 is the format's own boundary, not this project's. Every class
    // export in the reference install is at 68 or 69, so the false arm here is
    // covered by fixtures alone (SS 7).
    if (packageVersion >= 62) {
        appendIndex(out, within_);
        appendIndex(out, configName_);
    }

    appendAll(out, defaults_);
    return out;
}

LevelExportWriter& LevelExportWriter::setProperties(std::vector<std::uint8_t> propertyList) {
    properties_ = std::move(propertyList);
    return *this;
}

LevelExportWriter& LevelExportWriter::addActor(std::int32_t reference) {
    actors_.push_back(reference);
    return *this;
}

LevelExportWriter& LevelExportWriter::setActorSlotCountOverride(std::int32_t count) {
    actorSlotCountOverride_ = count;
    return *this;
}

LevelExportWriter& LevelExportWriter::setURL(std::string_view protocol, std::string_view host,
                                             std::string_view map, std::string_view portal,
                                             const std::vector<std::string>& options,
                                             std::int32_t port, std::int32_t valid) {
    protocol_ = std::string{protocol};
    host_ = std::string{host};
    map_ = std::string{map};
    portal_ = std::string{portal};
    options_ = options;
    port_ = port;
    valid_ = valid;
    return *this;
}

LevelExportWriter& LevelExportWriter::setModel(std::int32_t reference) {
    model_ = reference;
    return *this;
}

LevelExportWriter& LevelExportWriter::addReachSpec(std::int32_t distance, std::int32_t start,
                                                   std::int32_t end,
                                                   std::int32_t collisionRadius,
                                                   std::int32_t collisionHeight,
                                                   std::int32_t reachFlags,
                                                   std::uint8_t pruned) {
    specs_.push_back(Spec{distance, start, end, collisionRadius, collisionHeight, reachFlags,
                          pruned});
    return *this;
}

LevelExportWriter& LevelExportWriter::setReachSpecCountOverride(std::int32_t count) {
    reachSpecCountOverride_ = count;
    return *this;
}

LevelExportWriter& LevelExportWriter::setTrailerFloat(float value) {
    trailerFloat_ = value;
    return *this;
}

LevelExportWriter& LevelExportWriter::setTrailerIndex(int slot, std::int32_t value) {
    if (slot >= 0 && slot < TRAILER_INDEX_COUNT) {
        trailerIndices_[static_cast<std::size_t>(slot)] = value;
    }
    return *this;
}

LevelExportWriter& LevelExportWriter::addTrailerByte(std::uint8_t value) {
    trailerBytes_.push_back(value);
    return *this;
}

std::vector<std::uint8_t> LevelExportWriter::build() const {
    // A length-prefixed string: the length INCLUDES the terminator, which is
    // how the format writes every string this export carries.
    const auto appendString = [](std::vector<std::uint8_t>& out, const std::string& value) {
        appendIndex(out, static_cast<std::int32_t>(value.size() + 1));
        for (const char character : value) {
            out.push_back(static_cast<std::uint8_t>(character));
        }
        out.push_back(0);
    };

    std::vector<std::uint8_t> out = properties_;

    appendI32(out, actorSlotCountOverride_.value_or(static_cast<std::int32_t>(actors_.size())));
    appendI32(out, static_cast<std::int32_t>(actors_.size())); // Max, unused by the reader
    for (const std::int32_t actor : actors_) {
        appendIndex(out, actor);
    }

    appendString(out, protocol_);
    appendString(out, host_);
    appendString(out, map_);
    appendString(out, portal_);
    appendIndex(out, static_cast<std::int32_t>(options_.size()));
    for (const std::string& option : options_) {
        appendString(out, option);
    }
    appendI32(out, port_);
    appendI32(out, valid_);

    appendIndex(out, model_);
    appendIndex(out, reachSpecCountOverride_.value_or(static_cast<std::int32_t>(specs_.size())));
    for (const Spec& spec : specs_) {
        appendI32(out, spec.distance);
        appendIndex(out, spec.start);
        appendIndex(out, spec.end);
        appendI32(out, spec.collisionRadius);
        appendI32(out, spec.collisionHeight);
        appendI32(out, spec.reachFlags);
        out.push_back(spec.pruned);
    }

    appendFloat(out, trailerFloat_);
    for (const std::int32_t value : trailerIndices_) {
        appendIndex(out, value);
    }
    appendAll(out, trailerBytes_);
    return out;
}

namespace script {

std::vector<std::uint8_t> nothing() {
    return {0x0B};
}

std::vector<std::uint8_t> objectConst(std::int32_t reference) {
    std::vector<std::uint8_t> out{0x20};
    appendIndex(out, reference);
    return out;
}

std::vector<std::uint8_t> intConst(std::int32_t value) {
    std::vector<std::uint8_t> out{0x1D};
    appendI32(out, value);
    return out;
}

std::vector<std::uint8_t> unknownOpcode() {
    // 0x03 sits inside the primary range and the walker's table does not
    // define it, which is exactly the case INV-3 is about.
    return {0x03};
}

} // namespace script

} // namespace uta::test
