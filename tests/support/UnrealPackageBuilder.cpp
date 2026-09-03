#include "support/UnrealPackageBuilder.h"

#include <cstring>

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

void writeU32At(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[offset + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
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

UnrealPackageBuilder& UnrealPackageBuilder::addName(std::string_view name, std::uint32_t flags) {
    names_.push_back(NameEntry{std::string(name), flags});
    return *this;
}

std::vector<std::uint8_t> UnrealPackageBuilder::build() const {
    std::vector<std::uint8_t> out;

    appendU32(out, SIGNATURE);
    appendU16(out, packageVersion_);
    appendU16(out, licenseeVersion_);
    appendU32(out, packageFlags_);

    appendU32(out, static_cast<std::uint32_t>(names_.size()));
    const std::size_t nameOffsetField = out.size();
    appendU32(out, 0); // patched once the table's position is known

    appendU32(out, 0); // export count
    const std::size_t exportOffsetField = out.size();
    appendU32(out, 0);

    appendU32(out, 0); // import count
    const std::size_t importOffsetField = out.size();
    appendU32(out, 0);

    // Version 68 and up carry a GUID and a generation list where older
    // packages carried a heritage list.
    for (int i = 0; i < 16; ++i) {
        out.push_back(0);
    }
    appendU32(out, 1); // one generation
    appendU32(out, 0); // its export count
    appendU32(out, static_cast<std::uint32_t>(names_.size()));

    // The name table. Each entry is the length as a compact index, the
    // characters, a terminating null, then the entry's flags.
    writeU32At(out, nameOffsetField, static_cast<std::uint32_t>(out.size()));
    for (const NameEntry& entry : names_) {
        const auto length = static_cast<std::int32_t>(entry.name.size() + 1);
        const std::vector<std::uint8_t> encoded = encodeCompactIndex(length);
        out.insert(out.end(), encoded.begin(), encoded.end());
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        out.push_back(0);
        appendU32(out, entry.flags);
    }

    // Both tables are empty, so each offset points at where it would start.
    writeU32At(out, exportOffsetField, static_cast<std::uint32_t>(out.size()));
    writeU32At(out, importOffsetField, static_cast<std::uint32_t>(out.size()));

    return out;
}

} // namespace uta::test
