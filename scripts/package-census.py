#!/usr/bin/env python3
"""Census the Unreal packages under an install directory.

Reads only the first eight bytes of each file -- signature, package version,
licensee version -- so it is cheap over a large library and copies nothing.

This exists because docs/specs/UTA-0003-package-container.md rests three
design decisions on what a real install actually contains, and a number
transcribed into a document is a number nobody re-derives. Run it instead:

    scripts/package-census.py "/path/to/UnrealTournament"

No content is read beyond the header, and nothing is written anywhere.
"""

import os
import struct
import sys
from collections import Counter

SIGNATURE = 0x9E2A83C1
EXTENSIONS = {".unr", ".utx", ".uax", ".umx", ".u"}


def census(root: str) -> int:
    versions: Counter = Counter()
    licensees: Counter = Counter()
    sizes: list[int] = []
    unreadable = 0
    not_a_package = 0

    for dirpath, _, filenames in os.walk(root):
        for filename in filenames:
            if os.path.splitext(filename)[1].lower() not in EXTENSIONS:
                continue
            path = os.path.join(dirpath, filename)
            try:
                with open(path, "rb") as handle:
                    head = handle.read(8)
                size = os.path.getsize(path)
            except OSError:
                unreadable += 1
                continue
            if len(head) < 8:
                not_a_package += 1
                continue
            signature, version, licensee = struct.unpack("<IHH", head)
            if signature != SIGNATURE:
                not_a_package += 1
                continue
            versions[version] += 1
            licensees[licensee] += 1
            sizes.append(size)

    if not sizes:
        print(f"no Unreal packages found under {root}", file=sys.stderr)
        return 1

    sizes.sort()
    print(f"root:                {root}")
    print(f"packages:            {len(sizes)}")
    print(f"unreadable:          {unreadable}")
    print(f"not a package:       {not_a_package}")
    print(f"package versions:    {dict(sorted(versions.items()))}")
    print(f"licensee versions:   {dict(sorted(licensees.items()))}")
    print(f"below version 64:    {sum(n for v, n in versions.items() if v < 64)}")
    print(f"largest bytes:       {sizes[-1]:,}")
    print(f"median bytes:        {sizes[len(sizes) // 2]:,}")
    return 0


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <unreal-install-directory>", file=sys.stderr)
        return 2
    root = sys.argv[1]
    if not os.path.isdir(root):
        print(f"not a directory: {root}", file=sys.stderr)
        return 2
    return census(root)


if __name__ == "__main__":
    sys.exit(main())
