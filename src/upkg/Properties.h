// The tagged property list an object's serialised data begins with.
//
// docs/specs/UTA-0003-package-container.md SS 4.8.
//
// What this reader decodes and what it carries through is a deliberate line.
// A struct's members are serialised WITHOUT tags, so its layout is knowable
// only from the class table -- which is UTA-0005. So any struct this file does
// not name is returned with its type, its struct name and its raw bytes, and
// parsing continues. The tag's size field is what makes that safe, and it is
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
