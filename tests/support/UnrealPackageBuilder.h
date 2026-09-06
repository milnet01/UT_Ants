// Builds Unreal Engine 1 package bytes for tests.
//
// Why this exists: docs/discovery.md S7 requires that a stranger can clone
// this repository with no Unreal Tournament installed and have the build and
// the test suite both pass. So upkg's tests cannot read real packages -- they
// construct valid ones here, byte by byte, and assert against them.
//
// This deliberately ships an ENCODER and no decoder. The reader upkg grew
// (UTA-0003) writes its own decode path, so the two are independent
// implementations of one format and a misreading on either side shows up as a
// disagreement rather than cancelling out. Exact-byte vectors are what prove
// the encoder, not a round trip through code that shares its assumptions.
//
// Format reference: docs/specs/UTA-0003-package-container.md SS 4.3 to 4.8
// describe every shape encoded here -- the header, the three tables, the
// compact index they are written in, and the tagged property list an object's
// serialised data begins with.

#pragma once

#include <array>
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

/// Encode a property tag's array index, which is NOT a compact index: the
/// marker bits lead and the value's high bits follow them, so this is the one
/// place the format reads most-significant byte first (SS 4.8 step 5). Values
/// below 0x80 take one byte, below 0x4000 two, and the rest four.
[[nodiscard]] std::vector<std::uint8_t> encodeArrayIndex(std::uint32_t value);

/// View built bytes as the span upkg reads. Every test needs this, and
/// spelling the reinterpret_cast at each call site is spelling it wrongly in
/// one of them.
[[nodiscard]] std::span<const std::byte> asBytes(
    const std::vector<std::uint8_t>& bytes) noexcept;

/// One entry of a package's name table.
struct NameEntry {
    std::string name;
    std::uint32_t flags = 0;
};

/// One entry of a package's import table. Indices are into the name table;
/// `outer` is an object reference, written as a raw int32.
struct ImportEntry {
    std::int32_t classPackage = 0;
    std::int32_t className = 0;
    std::int32_t outer = 0;
    std::int32_t objectName = 0;
};

/// The object flag saying an object's serialised data is prefixed by an
/// execution-stack frame (SS 4.8). Common in real maps.
inline constexpr std::uint32_t OBJECT_FLAG_HAS_STACK = 0x02000000u;

/// One entry of a package's export table, with the bytes that entry points at.
///
/// The two overrides exist so a test can build a header that disagrees with
/// the body -- an export claiming more bytes than the file holds, say. A
/// self-consistent builder cannot produce that, and it is exactly what the
/// malformed-input tests need.
struct ExportEntry {
    std::int32_t objectClass = 0;
    std::int32_t super = 0;
    std::int32_t outer = 0;
    std::int32_t objectName = 0;
    std::uint32_t objectFlags = 0;
    std::vector<std::uint8_t> serialData;
    std::optional<std::uint32_t> serialSizeOverride;
    std::optional<std::uint32_t> serialOffsetOverride;
};

/// A tagged property's type, as written in the low four bits of its info byte.
enum class PropertyType : std::uint8_t {
    Byte = 1,
    Int,
    Bool,
    Float,
    Object,
    Name,
    String,
    Class,
    Array,
    Struct,
    Vector,
    Rotator,
    Str,
    Map,
    FixedArray,
};

/// Writes the tagged property list an object's serialised data begins with.
///
/// Every method takes name-table indices rather than strings: the list refers
/// to names by index, and resolving them is the reader's job.
class TaggedPropertyWriter {
public:
    TaggedPropertyWriter& addByte(std::int32_t nameIndex, std::uint8_t value);
    TaggedPropertyWriter& addInt(std::int32_t nameIndex, std::int32_t value);
    TaggedPropertyWriter& addFloat(std::int32_t nameIndex, float value);
    TaggedPropertyWriter& addObject(std::int32_t nameIndex, std::int32_t reference);
    TaggedPropertyWriter& addClass(std::int32_t nameIndex, std::int32_t reference);
    TaggedPropertyWriter& addName(std::int32_t nameIndex, std::int32_t valueNameIndex);

    /// A Str is a length as a compact index then that many bytes including the
    /// terminator; a String is exactly `size` bytes.
    TaggedPropertyWriter& addStr(std::int32_t nameIndex, std::string_view value);
    TaggedPropertyWriter& addString(std::int32_t nameIndex, std::string_view value);

    TaggedPropertyWriter& addVector(std::int32_t nameIndex, float x, float y, float z);
    TaggedPropertyWriter& addRotator(std::int32_t nameIndex, std::int32_t pitch,
                                     std::int32_t yaw, std::int32_t roll);

    /// A Bool's value is bit 7 of its info byte and it consumes no value
    /// bytes -- but its size field is still written, because every Bool tag
    /// measured in real content carries size code 5 and so a trailing size
    /// byte is present (SS 4.8). Writing it is what makes a reader that skips
    /// it desynchronise, which is INV-10's breaking case.
    TaggedPropertyWriter& addBool(std::int32_t nameIndex, bool value);

    /// A property at a non-zero array index, which sets bit 7 of the info byte
    /// and appends the index in the leading-marker encoding of SS 4.8 step 5.
    TaggedPropertyWriter& addIntAt(std::int32_t nameIndex, std::uint32_t arrayIndex,
                                   std::int32_t value);

    /// A struct this reader is not expected to decode: its bytes are carried
    /// through with the type and struct name intact (INV-11).
    TaggedPropertyWriter& addUndecodedStruct(std::int32_t nameIndex,
                                             std::int32_t structNameIndex,
                                             const std::vector<std::uint8_t>& raw);

    /// A property of any type with a body this writer does not compose --
    /// the escape hatch for a shape a test needs once.
    TaggedPropertyWriter& addRaw(std::int32_t nameIndex, PropertyType type,
                                 const std::vector<std::uint8_t>& body);

    /// Prefix the list with an execution-stack frame, as an object carrying
    /// OBJECT_FLAG_HAS_STACK does. The trailing offset is written only when
    /// `node` is non-null, which is the format's own rule.
    TaggedPropertyWriter& setStackFrame(std::int32_t node, std::int32_t stateNode,
                                        std::int64_t probeMask, std::int32_t latentAction,
                                        std::int32_t offset);

    /// The list, terminated by the name `None` at `noneNameIndex`.
    [[nodiscard]] std::vector<std::uint8_t> build(std::int32_t noneNameIndex) const;

    /// The same list with no terminator -- INV-9's breaking case, and
    /// something a correct writer would never produce.
    [[nodiscard]] std::vector<std::uint8_t> buildWithoutTerminator() const;

private:
    std::vector<std::uint8_t> body_;
    std::vector<std::uint8_t> stackFrame_;
};

/// Writes the serialised bytes of a CLASS export -- the field order of
/// docs/specs/UTA-0005-class-tables-and-ancestry.md SS 4.3.
///
/// A class export is recognised by a NULL class reference, so the ExportEntry
/// holding these bytes leaves `objectClass` at 0.
///
/// Several setters exist so a test can write what a self-consistent package
/// never would, which is the only way to isolate some of UTA-0005's
/// invariants (SS 4.9):
///
///   - `setSuperField` writes the IN-DATA parent independently of the export
///     table's own `super` column, so the two can be made to disagree (INV-2);
///   - `setScriptSizeOverride` writes a ScriptSize the script body does not
///     sum to, including a negative one (INV-3);
///   - `setPackageVersion` below 62 omits ClassWithin and ClassConfigName,
///     the branch no package in the reference install takes (SS 7).
class ClassExportWriter {
public:
    /// The in-data SuperField. Independent of the export table on purpose.
    ClassExportWriter& setSuperField(std::int32_t reference);
    ClassExportWriter& setNext(std::int32_t reference);
    ClassExportWriter& setScriptText(std::int32_t reference);
    ClassExportWriter& setChildren(std::int32_t reference);
    ClassExportWriter& setFriendlyName(std::int32_t nameIndex);
    ClassExportWriter& setLine(std::int32_t line);
    ClassExportWriter& setTextPos(std::int32_t textPos);

    /// The compiled script. `memorySize` is what ScriptSize will say -- the
    /// bytes the instructions occupy IN MEMORY, which is not `body.size()`.
    ClassExportWriter& setScript(std::vector<std::uint8_t> body, std::int32_t memorySize);

    /// Write a ScriptSize the body does not sum to. Overrides setScript's.
    ClassExportWriter& setScriptSizeOverride(std::int32_t scriptSize);

    ClassExportWriter& setClassFlags(std::uint32_t flags);
    ClassExportWriter& setClassGuid(const std::vector<std::uint8_t>& guid);

    /// One dependency-list entry: an object reference as a compact index, an
    /// int32 and a uint32.
    ClassExportWriter& addDependency(std::int32_t reference, std::int32_t depth,
                                     std::uint32_t scriptTextCrc);
    /// One package-import-list entry: a name index as a compact index.
    ClassExportWriter& addPackageImport(std::int32_t nameIndex);

    /// Written only at package version 62 and above.
    ClassExportWriter& setWithin(std::int32_t reference);
    ClassExportWriter& setConfigName(std::int32_t nameIndex);

    /// The default properties, as built by TaggedPropertyWriter::build.
    ClassExportWriter& setDefaults(std::vector<std::uint8_t> propertyList);

    /// Prefix an execution-stack frame, as an object carrying
    /// OBJECT_FLAG_HAS_STACK does.
    ClassExportWriter& setStackFrame(std::int32_t node, std::int32_t stateNode,
                                     std::int64_t probeMask, std::int32_t latentAction,
                                     std::int32_t offset);

    /// `packageVersion` decides whether ClassWithin and ClassConfigName are
    /// written; it must match the package these bytes go into.
    [[nodiscard]] std::vector<std::uint8_t> build(std::uint16_t packageVersion) const;

private:
    std::vector<std::uint8_t> stackFrame_;
    std::int32_t superField_ = 0;
    std::int32_t next_ = 0;
    std::int32_t scriptText_ = 0;
    std::int32_t children_ = 0;
    std::int32_t friendlyName_ = 0;
    std::int32_t line_ = 0;
    std::int32_t textPos_ = 0;
    std::vector<std::uint8_t> script_;
    std::int32_t scriptSize_ = 0;
    std::optional<std::int32_t> scriptSizeOverride_;
    std::uint32_t classFlags_ = 0;
    std::vector<std::uint8_t> classGuid_ = std::vector<std::uint8_t>(16, 0);
    std::vector<std::array<std::int64_t, 3>> dependencies_;
    std::vector<std::int32_t> packageImports_;
    std::int32_t within_ = 0;
    std::int32_t configName_ = 0;
    std::vector<std::uint8_t> defaults_;
};

/// Compiled-script instructions, for the walker's fixtures. Each returns the
/// DISK bytes; the memory cost each contributes is named beside it, because
/// ScriptSize counts the memory form and a test has to state both.
namespace script {

/// EX_Nothing. 1 disk byte, 1 memory byte.
[[nodiscard]] std::vector<std::uint8_t> nothing();
/// EX_ObjectConst plus a compact-index reference. 1 memory byte for the
/// opcode plus 4 for the reference, whatever the index encodes to on disk --
/// which is what INV-4 turns on.
[[nodiscard]] std::vector<std::uint8_t> objectConst(std::int32_t reference);
/// EX_IntConst plus a 32-bit literal. 5 disk bytes, 5 memory bytes.
[[nodiscard]] std::vector<std::uint8_t> intConst(std::int32_t value);
/// An opcode the walker's table does not define (INV-3).
[[nodiscard]] std::vector<std::uint8_t> unknownOpcode();

} // namespace script

/// Assembles a package whose header, name table, import table and export table
/// are internally consistent -- offsets and counts included, which is the part
/// a hand-written fixture gets wrong.
///
/// Layout: header, then each export's serialised bytes, then the name table,
/// the export table and the import table. Any layout whose offsets are honest
/// is a valid package; this one is chosen because it fixes every serial offset
/// before the export table that names them is written.
class UnrealPackageBuilder {
public:
    /// UT99's own packages are version 68. At 68 and above the header carries
    /// a GUID and a generation list; below it, a heritage list. Below 64 the
    /// name table is null-terminated rather than length-prefixed. All three
    /// shapes are built, because SS 2.1 found real content at each.
    UnrealPackageBuilder& setPackageVersion(std::uint16_t version);
    UnrealPackageBuilder& setLicenseeVersion(std::uint16_t version);
    UnrealPackageBuilder& setPackageFlags(std::uint32_t flags);
    UnrealPackageBuilder& addName(std::string_view name, std::uint32_t flags = 0);
    UnrealPackageBuilder& addImport(ImportEntry entry);
    UnrealPackageBuilder& addExport(ExportEntry entry);

    // Deliberate lies. Each replaces one header field after the honest value
    // has been computed, so the rest of the package stays consistent and the
    // test isolates the one falsehood it is about.
    UnrealPackageBuilder& overrideSignature(std::uint32_t value);
    UnrealPackageBuilder& overrideNameCount(std::uint32_t value);
    UnrealPackageBuilder& overrideNameOffset(std::uint32_t value);
    UnrealPackageBuilder& overrideExportCount(std::uint32_t value);
    UnrealPackageBuilder& overrideExportOffset(std::uint32_t value);
    UnrealPackageBuilder& overrideImportCount(std::uint32_t value);
    UnrealPackageBuilder& overrideImportOffset(std::uint32_t value);

    [[nodiscard]] std::vector<std::uint8_t> build() const;

    /// The signature every Unreal package opens with.
    static constexpr std::uint32_t SIGNATURE = 0x9E2A83C1u;

private:
    std::uint16_t packageVersion_ = 68;
    std::uint16_t licenseeVersion_ = 0;
    std::uint32_t packageFlags_ = 0;
    std::vector<NameEntry> names_;
    std::vector<ImportEntry> imports_;
    std::vector<ExportEntry> exports_;

    std::optional<std::uint32_t> signatureOverride_;
    std::optional<std::uint32_t> nameCountOverride_;
    std::optional<std::uint32_t> nameOffsetOverride_;
    std::optional<std::uint32_t> exportCountOverride_;
    std::optional<std::uint32_t> exportOffsetOverride_;
    std::optional<std::uint32_t> importCountOverride_;
    std::optional<std::uint32_t> importOffsetOverride_;
};

} // namespace uta::test
