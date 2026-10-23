// What every upkg export reader shares: the refusal they all return, and the
// three checks each table, name index and export end goes through. One copy
// each, so a change to a rule or its message reaches every reader.

#pragma once

#include "core/Error.h"
#include "upkg/ByteReader.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace uta::upkg::detail {

/// The version from which a mip's WidthOffset and a sound's NextOffset are
/// present. Measured across the reference install: stock content is not all
/// version 68, so this branch is exercised by real packages rather than being
/// an edge case (UTA-0004 SS 2.1, SS 4.6).
inline constexpr std::uint16_t OFFSET_FIELD_MIN_VERSION = 63;

/// MalformedData carrying `message`.
[[nodiscard]] Error malformed(std::string message);

/// A table's element count comes from the file, so it is checked against
/// what is left before anything is reserved: reserving from an unchecked
/// count is how a four-byte edit asks for gigabytes (UTA-0004 INV-4).
/// `minElementBytes` is the element's SMALLEST encoding, since a compact index
/// is one to five bytes wide; the check bounds the allocation and does not
/// predict where the table ends. Refuses as "<owner> declares <count>
/// <noun>", with what remains when the count is too large.
[[nodiscard]] Result<void> checkCount(const ByteReader& reader, std::int64_t count,
                                      std::size_t minElementBytes, std::string_view owner,
                                      std::string_view noun);

/// A name index read from the file, inside a name table of `nameCount`
/// entries. Refuses as "<names> index <index>, which is outside the name
/// table's <nameCount> entries"; `names` reads like "a Sound names format".
[[nodiscard]] Result<std::uint32_t> checkNameIndex(std::int32_t index, std::size_t nameCount,
                                                   std::string_view names);

/// SS 4.3: a reader that models an export's layout correctly ends exactly at
/// its end. Anywhere else means a field's width is wrong or a branch was
/// missed, and a partial result is never returned (INV-1).
[[nodiscard]] Result<void> expectConsumed(const ByteReader& reader, std::string_view owner);

} // namespace uta::upkg::detail
