// Walks a class's compiled UnrealScript to find where it ends.
//
// docs/specs/UTA-0005-class-tables-and-ancestry.md SS 4.4.
//
// NOTHING IS EXECUTED. Each instruction's operands are read only to learn
// how wide it is, and are then discarded. There is no stack, no evaluation
// and no native function surface -- ADR-0004 decided this engine
// understands a custom actor by its ancestry and its defaults rather than
// by running its script, and this file is a length calculation on the way
// to those defaults. INV-3a is that promise.
//
// Why a walk rather than a skip: ScriptSize, the only length the file
// stores, counts the bytes the script occupies IN MEMORY. On disk an
// object reference or a name is a compact index of one to five bytes,
// while the 1999 compiler that computed ScriptSize counted four for each.
// So a reader that skips ScriptSize bytes lands past the script's end,
// usually inside the default properties -- which then parse as nonsense
// rather than failing. SS 2.1 measured it.

#pragma once

#include "core/Error.h"

#include <cstdint>

namespace uta::upkg {

class ByteReader;
class Package;

/// Advance `reader` past a compiled script of `scriptSize` MEMORY bytes,
/// leaving it on the first byte after the script.
///
/// `scriptSize` is signed all the way in: a negative value is MalformedData
/// and is refused before the walk begins. Narrowing it at the call site is
/// what that forbids -- -1 then arrives as four billion, and is caught by
/// the export's bound rather than by the rule meant to catch it.
///
/// The walk must land on `scriptSize` exactly. A total that steps past it
/// has mis-read an instruction's width and stopped mid-instruction, and
/// nothing downstream can see that: a wrong script end still leaves a
/// property list that parses and ends where it should (SS 2.1).
[[nodiscard]] Result<void> skipScript(const Package& package, ByteReader& reader,
                                      std::int32_t scriptSize);

} // namespace uta::upkg
