// The tagged property list an object's serialised data begins with.
//
// docs/specs/UTA-0003-package-container.md SS 4.8.
//
// What this reader decodes and what it carries through is a deliberate line.
// A struct's members are serialised WITHOUT tags, so its layout is knowable
// only from a class's Children chain -- which nothing reads yet. UTA-0005
// reads the class table and deliberately does NOT decode struct values; it
// measured the question and scoped it out, so this carry-through is the whole
// answer rather than a placeholder. Any struct this file does not name is
// returned with its type, its struct name and its raw bytes, and parsing
// continues. The tag's size field is what makes that safe, and it is
// the whole reason the size is read even for types whose width is fixed.

#pragma once

#include "core/Error.h"
#include "upkg/Package.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace uta::upkg {

class ByteReader;

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

/// An index into the package's name table, distinguished from a plain integer
/// so a Name property cannot be mistaken for an Int one.
struct NameRef {
    std::uint32_t index = 0;
};

struct Vector3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

struct Rotator {
    std::int32_t pitch = 0;
    std::int32_t yaw = 0;
    std::int32_t roll = 0;
};

using PropertyValue = std::variant<std::monostate,      // nothing read yet
                                   std::uint8_t,        // Byte
                                   std::int32_t,        // Int
                                   bool,                // Bool
                                   float,               // Float
                                   ObjectReference,     // Object, Class
                                   NameRef,             // Name
                                   std::string,         // String, Str
                                   Vector3, Rotator,
                                   std::span<const std::byte>>; // not decoded

struct Property {
    std::uint32_t nameIndex = 0;
    PropertyType type = PropertyType::Byte;
    std::uint32_t structNameIndex = 0; // meaningful only when type is Struct
    std::uint32_t arrayIndex = 0;
    PropertyValue value;
};

/// A property list, and where it ended.
///
/// `nativeOffset` is the offset WITHIN the export's serialised bytes at which
/// the object's native data begins -- the byte after the list's `None`
/// terminator. Every typed reader in UTA-0004 needs it, and none of them can
/// recompute it without re-parsing the list, which would be a second decoder
/// of the one format this file owns.
struct PropertyList {
    std::vector<Property> properties;
    std::size_t nativeOffset = 0;
};

/// Read a tagged property list at the cursor's current position, leaving the
/// cursor on the byte after the list's `None` terminator.
///
/// This is the one decoder of this format, and the two entry points below are
/// written in terms of it. Both of those start at the BEGINNING of an
/// export's serialised bytes, because that is where the list is for every
/// object UTA-0003 and UTA-0004 read. A class's defaults are the LAST thing
/// in its export, ten fields and a compiled script downstream, so UTA-0005
/// needs a cursor the caller already holds -- and a second decoder is what
/// this file's own header comment exists to prevent.
/// docs/specs/UTA-0005-class-tables-and-ancestry.md SS 4.3 step 11, INV-12.
///
/// The list's extent is bounded by the reader's own span, so a caller that
/// built one over a single export cannot read beyond it.
[[nodiscard]] Result<std::vector<Property>> readPropertiesAt(const Package& package,
                                                             ByteReader& reader);

/// Skip the execution-stack frame an object carrying OBJECT_FLAG_HAS_STACK is
/// prefixed by, leaving the cursor on the byte after it.
///
/// Exposed for the same reason as `readPropertiesAt`: a class export carries
/// this frame too (UTA-0005 SS 4.3 step 1), and a second copy of the shape
/// would be a second decoder of it. The frame is read and discarded, but
/// skipping it is not optional -- ignore it and the field after it is read out
/// of the frame's bytes, which usually decodes as something plausible rather
/// than failing.
[[nodiscard]] Result<void> skipExecutionStackFrame(ByteReader& reader);

/// As `readProperties`, but also reporting where the list ended.
[[nodiscard]] Result<PropertyList> readPropertyList(const Package& package,
                                                    const ExportEntry& entry);

/// Read the tagged property list an export's serialised data begins with,
/// skipping the execution-stack frame where the object carries one.
///
/// An export with no serialised data has no list: this succeeds with an empty
/// vector. A CLASS export has no list either, and is recognised by a NULL
/// class reference rather than by one naming `Class`, which no package writes;
/// this refuses it with InvalidArgument rather than returning nonsense.
[[nodiscard]] Result<std::vector<Property>> readProperties(const Package& package,
                                                           const ExportEntry& entry);

} // namespace uta::upkg
