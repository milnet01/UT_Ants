// Builds Unreal Engine 1 package bytes for tests.
//
// Why this exists: docs/discovery.md S7 requires that a stranger can clone
// this repository with no Unreal Tournament installed and have the build and
// the test suite both pass. So upkg's tests cannot read real packages -- they
// construct valid ones here, byte by byte, and assert against them.
//
// This deliberately ships an ENCODER and no decoder. The reader upkg will
// grow (UTA-0003) writes its own decode path, so the two are independent
// implementations of one format and a misreading on either side shows up as a
// disagreement rather than cancelling out. Exact-byte vectors are what prove
// the encoder, not a round trip through code that shares its assumptions.
//
// Format reference: the Unreal package format is community-documented; the
// shapes encoded here are the header, the name table, and the compact index
// those two are written in.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uta::test {

/// Encode one value in Unreal's compact index -- a variable-length signed
/// integer where the first byte carries a sign bit, a continuation bit and six
/// value bits, and each later byte carries a continuation bit and seven.
[[nodiscard]] std::vector<std::uint8_t> encodeCompactIndex(std::int32_t value);

/// One entry of a package's name table.
struct NameEntry {
    std::string name;
    std::uint32_t flags = 0;
};

/// Assembles a package whose header, name table and (empty) import and export
/// tables are internally consistent -- offsets and counts included, which is
/// the part a hand-written fixture gets wrong.
class UnrealPackageBuilder {
public:
    /// UT99's own packages are version 68. Below 64 the name table is
    /// null-terminated rather than length-prefixed, which this does not build.
    UnrealPackageBuilder& setPackageVersion(std::uint16_t version);
    UnrealPackageBuilder& setLicenseeVersion(std::uint16_t version);
    UnrealPackageBuilder& setPackageFlags(std::uint32_t flags);
    UnrealPackageBuilder& addName(std::string_view name, std::uint32_t flags = 0);

    [[nodiscard]] std::vector<std::uint8_t> build() const;

    /// The signature every Unreal package opens with.
    static constexpr std::uint32_t SIGNATURE = 0x9E2A83C1u;

private:
    std::uint16_t packageVersion_ = 68;
    std::uint16_t licenseeVersion_ = 0;
    std::uint32_t packageFlags_ = 0;
    std::vector<NameEntry> names_;
};

} // namespace uta::test
