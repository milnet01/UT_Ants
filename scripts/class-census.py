#!/usr/bin/env python3
"""Census the class exports under an Unreal Tournament install.

Reports how many class exports there are, how many carry a compiled script
of their own, how many are consumed exactly by the layout
docs/specs/UTA-0005-class-tables-and-ancestry.md SS 4.3 describes, and how
many walk their ancestry to a root class.

    scripts/class-census.py "/path/to/UnrealTournament"

This exists because that spec rests on those numbers, and a number
transcribed into a document is a number nobody re-derives. Nothing is
executed: a compiled script is walked only to learn its length. Nothing is
written anywhere, and no package content is copied.
"""

import glob
import os
import struct
import sys
from collections import Counter

SIGNATURE = 0x9E2A83C1
RF_HAS_STACK = 0x02000000
PTR = 4       # sizeof(UObject*) in the 32-bit compiler that wrote ScriptSize
NAME = 4      # sizeof(FName) likewise
MAX_DEPTH = 64
EXTENSIONS = ("*.u", "*.unr", "*.utx", "*.uax", "*.umx")


class Cursor:
    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def u8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def _unpack(self, fmt, width):
        v = struct.unpack_from(fmt, self.data, self.pos)[0]
        self.pos += width
        return v

    def u16(self): return self._unpack("<H", 2)
    def i16(self): return self._unpack("<h", 2)
    def u32(self): return self._unpack("<I", 4)
    def i32(self): return self._unpack("<i", 4)
    def i64(self): return self._unpack("<q", 8)

    def skip(self, count):
        self.pos += count
        if self.pos > len(self.data):
            raise ValueError("read past end")

    def index(self):
        """The package format's compact index."""
        first = self.u8()
        value = first & 0x3F
        if first & 0x40:
            shift = 6
            for _ in range(4):
                nxt = self.u8()
                value |= (nxt & 0x7F) << shift
                shift += 7
                if not nxt & 0x80:
                    break
            else:
                raise ValueError("compact index too long")
        return -value if first & 0x80 else value


class Package:
    def __init__(self, path):
        with open(path, "rb") as handle:
            self.data = handle.read()
        self.path = path
        cursor = Cursor(self.data)
        if cursor.u32() != SIGNATURE:
            raise ValueError("not a package")
        self.version = cursor.u16()
        cursor.u16()
        cursor.u32()
        name_count, name_offset = cursor.u32(), cursor.u32()
        export_count, export_offset = cursor.u32(), cursor.u32()
        import_count, import_offset = cursor.u32(), cursor.u32()
        self.names = self._read_names(name_count, name_offset)
        self.imports = self._read_imports(import_count, import_offset)
        self.exports = self._read_exports(export_count, export_offset)

    def _read_names(self, count, offset):
        cursor, names = Cursor(self.data, offset), []
        for _ in range(count):
            if self.version < 64:
                raw = bytearray()
                while True:
                    byte = cursor.u8()
                    if byte == 0:
                        break
                    raw.append(byte)
            else:
                length = cursor.index()
                raw = self.data[cursor.pos:cursor.pos + length - 1]
                cursor.skip(length)
            cursor.u32()
            names.append(raw.decode("latin-1"))
        return names

    def _read_imports(self, count, offset):
        cursor, imports = Cursor(self.data, offset), []
        for _ in range(count):
            imports.append((cursor.index(), cursor.index(),
                            cursor.i32(), cursor.index()))
        return imports

    def _read_exports(self, count, offset):
        cursor, exports = Cursor(self.data, offset), []
        for _ in range(count):
            entry = {
                "class": cursor.index(), "super": cursor.index(),
                "outer": cursor.i32(), "name": cursor.index(),
                "flags": cursor.u32(), "size": cursor.index(),
            }
            entry["offset"] = cursor.index() if entry["size"] > 0 else 0
            exports.append(entry)
        return exports

    def name_of(self, entry):
        return self.names[entry["name"]]

    def import_package(self, reference):
        """The package name an import ultimately lives in."""
        for _ in range(MAX_DEPTH):
            if reference >= 0:
                return None
            _, _, outer, object_name = self.imports[-reference - 1]
            if outer == 0:
                return self.names[object_name]
            reference = outer
        return None


# --- the compiled script -----------------------------------------------
#
# Walked, never executed. Each instruction reports the MEMORY bytes it
# occupies, because ScriptSize -- the only length the file stores -- counts
# the memory form while the file holds a narrower encoding.

NO_OPERANDS = {0x08, 0x0B, 0x16, 0x17, 0x25, 0x26, 0x27, 0x28, 0x2A, 0x30, 0x31}
ONE_EXPRESSION = {0x04, 0x0D, 0x0E, 0x2D}
OBJECT_OPERAND = {0x00, 0x01, 0x02, 0x20, 0x29}
TWO_EXPRESSIONS = {0x0F, 0x10, 0x14, 0x1A}
CAST_OPERAND = {0x13, 0x2E, 0x36}
WORD_THEN_EXPRESSION = {0x07, 0x09, 0x18}
END_OF_PARAMETERS = 0x16


def skip_expression(package, cursor, depth=0):
    if depth > MAX_DEPTH:
        raise ValueError("expression nested too deeply")
    op = cursor.u8()
    if op >= 0x70:
        return 1 + skip_parameters(package, cursor, depth)
    if op >= 0x60:
        cursor.u8()
        return 2 + skip_parameters(package, cursor, depth)
    nested = lambda: skip_expression(package, cursor, depth + 1)
    if op in OBJECT_OPERAND:
        cursor.index()
        return 1 + PTR
    if op == 0x21:
        cursor.index()
        return 1 + NAME
    if op in NO_OPERANDS:
        return 1
    if op in ONE_EXPRESSION:
        return 1 + nested()
    if op == 0x05:
        cursor.u8()
        return 2 + nested()
    if op == 0x06:
        cursor.u16()
        return 3
    if op in WORD_THEN_EXPRESSION:
        cursor.u16()
        return 3 + nested()
    if op == 0x0A:
        return 3 + (nested() if cursor.u16() != 0xFFFF else 0)
    if op == 0x0C:
        size = 0
        while True:
            entry = cursor.index()
            cursor.u32()
            size += NAME + 4
            if package.names[entry] == "None":
                return 1 + size
    if op in TWO_EXPRESSIONS:
        return 1 + nested() + nested()
    if op == 0x11:
        return 1 + nested() + nested() + nested() + nested()
    if op in (0x12, 0x19):
        first = nested()
        cursor.u16()
        cursor.u8()
        return 4 + first + nested()
    if op in CAST_OPERAND:
        cursor.index()
        return 1 + PTR + nested()
    if op == 0x1B:
        cursor.index()
        return 1 + NAME + skip_parameters(package, cursor, depth)
    if op in (0x1C, 0x38):
        cursor.index()
        return 1 + PTR + skip_parameters(package, cursor, depth)
    if op in (0x1D, 0x1E):
        cursor.skip(4)
        return 5
    if op == 0x1F:
        length = 0
        while cursor.u8() != 0:
            length += 1
        return 1 + length + 1
    if op == 0x34:
        length = 0
        while cursor.u16() != 0:
            length += 2
        return 1 + length + 2
    if op in (0x22, 0x23):
        cursor.skip(12)
        return 13
    if op in (0x24, 0x2C):
        cursor.u8()
        return 2
    if op == 0x2F:
        first = nested()
        cursor.u16()
        return 3 + first
    if op in (0x32, 0x33):
        cursor.index()
        return 1 + PTR + nested() + nested()
    if 0x39 <= op <= 0x5F:
        return 1 + nested()
    raise ValueError(f"unknown opcode 0x{op:02X}")


def skip_parameters(package, cursor, depth):
    total = 0
    while True:
        at = cursor.pos
        total += skip_expression(package, cursor, depth + 1)
        if cursor.data[at] == END_OF_PARAMETERS:
            return total


def skip_script(package, cursor, script_size):
    walked = 0
    while walked < script_size:
        walked += skip_expression(package, cursor)
    return walked


# --- the class ----------------------------------------------------------

FIXED_SIZES = {0: 1, 1: 2, 2: 4, 3: 12, 4: 16}


def skip_properties(package, cursor, end):
    """The tagged property list, in the form UTA-0003 SS 4.8 reads."""
    count = 0
    while True:
        if cursor.pos >= end:
            raise ValueError("property list ran past the export")
        if package.names[cursor.index()] == "None":
            return count
        info = cursor.u8()
        kind = info & 0x0F
        code = (info >> 4) & 0x07
        if kind == 10:
            cursor.index()
        if code in FIXED_SIZES:
            size = FIXED_SIZES[code]
        elif code == 5:
            size = cursor.u8()
        elif code == 6:
            size = cursor.u16()
        else:
            size = cursor.u32()
        if info & 0x80 and kind != 3:
            first = cursor.u8()
            if first >= 0x80:
                cursor.skip(1 if first & 0x40 == 0 else 3)
        if kind != 3:
            cursor.skip(size)
        count += 1


def read_class(package, entry):
    """Returns (super_reference, bytes_past_the_end, had_script)."""
    end = entry["offset"] + entry["size"]
    cursor = Cursor(package.data, entry["offset"])
    if entry["flags"] & RF_HAS_STACK:
        node = cursor.index()
        cursor.index()
        cursor.i64()
        cursor.i32()
        if node != 0:
            cursor.index()
    super_reference = cursor.index()
    for _ in range(3):
        cursor.index()
    cursor.index()
    cursor.i32()
    cursor.i32()
    script_size = cursor.i32()
    if script_size:
        skip_script(package, cursor, script_size)
    cursor.i64()
    cursor.i64()
    cursor.i16()
    cursor.i32()
    cursor.u32()
    cursor.skip(16)
    for _ in range(cursor.index()):
        cursor.index()
        cursor.i32()
        cursor.u32()
    for _ in range(cursor.index()):
        cursor.index()
    if package.version >= 62:
        cursor.index()
        cursor.index()
    skip_properties(package, cursor, end)
    return super_reference, cursor.pos - end, script_size > 0


def is_class_export(entry):
    return entry["class"] == 0 and entry["size"] > 0


# --- ancestry -----------------------------------------------------------

class Installation:
    """Opens packages by name, the way SS 4.6's resolver is expected to."""

    def __init__(self, root):
        self.index = {}
        for directory in ("System", "Maps", "Textures", "Sounds", "Music"):
            for pattern in EXTENSIONS:
                for path in glob.glob(os.path.join(root, directory, pattern)):
                    key = os.path.basename(path).rsplit(".", 1)[0].lower()
                    self.index.setdefault(key, path)
        self.opened = {}

    def package(self, name):
        key = name.lower()
        if key not in self.opened:
            path = self.index.get(key)
            try:
                self.opened[key] = Package(path) if path else None
            except (ValueError, OSError, IndexError, struct.error):
                self.opened[key] = None
        return self.opened[key]

    def find_class(self, package_name, class_name):
        package = self.package(package_name)
        if package is None:
            return None
        wanted = class_name.lower()
        for entry in package.exports:
            if is_class_export(entry) and package.name_of(entry).lower() == wanted:
                return package, entry
        return None


def walk_ancestry(installation, package, entry):
    """Returns 'root', 'missing', 'cycle' or 'malformed'."""
    seen = set()
    for _ in range(MAX_DEPTH):
        key = (package.path, entry["name"])
        if key in seen:
            return "cycle"
        seen.add(key)
        try:
            reference, _, _ = read_class(package, entry)
        except (ValueError, IndexError, struct.error):
            return "malformed"
        if reference == 0:
            return "root"
        if reference > 0:
            if reference - 1 >= len(package.exports):
                return "malformed"
            entry = package.exports[reference - 1]
            continue
        if -reference - 1 >= len(package.imports):
            return "malformed"
        _, _, _, object_name = package.imports[-reference - 1]
        owner = package.import_package(reference)
        found = installation.find_class(owner, package.names[object_name]) if owner else None
        if found is None:
            return "missing"
        package, entry = found
    return "cycle"


def census(root):
    installation = Installation(root)
    tally = Counter()
    packages = 0
    for path in sorted(set(installation.index.values())):
        try:
            package = Package(path)
        except (ValueError, OSError, IndexError, struct.error):
            tally["packages unreadable"] += 1
            continue
        packages += 1
        for entry in package.exports:
            if not is_class_export(entry):
                continue
            tally["class exports"] += 1
            try:
                _, past_end, had_script = read_class(package, entry)
            except (ValueError, IndexError, struct.error):
                tally["  layout: failed to read"] += 1
                continue
            tally["  layout: consumed exactly" if past_end == 0
                  else "  layout: WRONG LENGTH"] += 1
            if had_script:
                tally["  carrying a script of their own"] += 1
            tally["  ancestry: " + walk_ancestry(installation, package, entry)] += 1

    print(f"install: {root}")
    print(f"packages read: {packages}")
    for key in sorted(tally):
        print(f"  {key}: {tally[key]}")
    exact = tally["  layout: consumed exactly"]
    total = tally["class exports"]
    if total:
        print(f"consumed exactly: {exact} of {total} ({100.0 * exact / total:.2f}%)")
    return 0 if total and exact == total else 1


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    if not os.path.isdir(argv[1]):
        print(f"not a directory: {argv[1]}", file=sys.stderr)
        return 2
    return census(argv[1])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
