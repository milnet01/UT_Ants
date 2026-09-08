#!/usr/bin/env python3
"""Ask, of each rule a spec names, whether any test actually grades it.

    scripts/mutation-probe.py ubundle
    scripts/mutation-probe.py ubundle --asan

WHY THIS EXISTS. A green test proves nothing about the rule it names. On
UTA-0007 three of four tier-1 cases passed on first writing and each was
decided by something OTHER than the rule in its title -- a table entry that
already returned the refusal, a range check that subsumed the guard, and a
loop bound that rejected the fixture early. None of that is visible from
reading the test. CLAUDE.md SS Build and test records it, and this script is
how the same question gets asked mechanically.

On UTA-0008 it asked 57 times and found FOUR more: a string-overrun case in a
section too short for the node count that came before it, an unknown-id case
whose payload was too short to decode either way, a table-bound case whose bad
entry sat where a different rule caught it first, and a graph case that broke
two rules at once. Every one passed. Every one read correctly.

HOW TO READ THE RESULT.

  KILLED       removing the rule turned some test red. The rule is graded.
  SURVIVED     removing the rule changed nothing. Either the fixture grades
               something else -- suspect this FIRST, per CLAUDE.md -- or the
               rule is genuinely redundant, in which case declare it in
               EXPECTED_SURVIVORS with the reason.
  NOT-APPLIED  the search text no longer matches. The mutation is stale and
               is telling you nothing; fix it or drop it.
  COMPILE-FAIL the mutation does not build. Same -- it is not a result.

Exit status is 0 when the survivors are exactly the declared ones. A NEW
survivor exits 1: it means an edit made some rule untested and no other check
in this repository would have said so.

THREE TRAPS, ALL PAID FOR ONCE ALREADY.

  1. Restore by rewriting and touching. `mv` restores an older mtime, ninja
     skips the rebuild, and the NEXT mutation is graded against the previous
     one's binary.
  2. Baseline the filter, not just the suite. A Catch2 tag that matches no
     tests exits non-zero, under which every mutation reads as KILLED. The
     baseline below asserts a non-zero case count.
  3. Never put `ulimit -v` around a sanitizer binary. ASan reserves tens of
     terabytes of address space for its shadow map; a cap makes it die at
     startup, before any test runs, and that also reads as KILLED. Under
     --asan the bound is ASAN_OPTIONS and a timeout instead.
"""

import argparse
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Each entry: (label, project-relative file, search text, replacement).
# The search text must be unique in the file -- a replacement count of one is
# what makes a mutation one rule rather than several.
SUBJECTS = {
    "ubundle": {
        "target": "uta_unit_tests",
        "binary": "tests/uta_unit_tests",
        "filter": "[ubundle]",
        "cases": 21,
        "mutations": [
            # -- SS 4.6 to SS 4.8: two adjacent fields of one width and type,
            # transposed. INV-6 and INV-7. A round trip cannot see these; only
            # the hand-authored golden array can.
            ("swap Node iLeaf[0] and iLeaf[1]", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(node.iLeaf[0], cursor.readI32());\n    UTA_TRY(node.iLeaf[1], cursor.readI32());",
             "    UTA_TRY(node.iLeaf[1], cursor.readI32());\n    UTA_TRY(node.iLeaf[0], cursor.readI32());"),
            ("swap Node normal.x and normal.y", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(node.normal.x, cursor.readF32());\n    UTA_TRY(node.normal.y, cursor.readF32());",
             "    UTA_TRY(node.normal.y, cursor.readF32());\n    UTA_TRY(node.normal.x, cursor.readF32());"),
            ("swap Node normal.z and w", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(node.normal.z, cursor.readF32());\n    UTA_TRY(node.w, cursor.readF32());",
             "    UTA_TRY(node.w, cursor.readF32());\n    UTA_TRY(node.normal.z, cursor.readF32());"),
            ("swap Node iZone[0] and iZone[1]", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(node.iZone[0], cursor.readU8());\n    UTA_TRY(node.iZone[1], cursor.readU8());",
             "    UTA_TRY(node.iZone[1], cursor.readU8());\n    UTA_TRY(node.iZone[0], cursor.readU8());"),
            ("swap Room minZ and maxZ", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(room.minZ, cursor.readF32());\n    UTA_TRY(room.maxZ, cursor.readF32());",
             "    UTA_TRY(room.maxZ, cursor.readF32());\n    UTA_TRY(room.minZ, cursor.readF32());"),
            ("swap Point2 x and y", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(point.x, cursor.readF32());\n    UTA_TRY(point.y, cursor.readF32());",
             "    UTA_TRY(point.y, cursor.readF32());\n    UTA_TRY(point.x, cursor.readF32());"),
            ("swap NavNode firstEdge and edgeCount", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(node.firstEdge, cursor.readU32());\n    UTA_TRY(node.edgeCount, cursor.readU32());",
             "    UTA_TRY(node.edgeCount, cursor.readU32());\n    UTA_TRY(node.firstEdge, cursor.readU32());"),
            ("swap NavEdge distance and collisionRadius", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(edge.distance, cursor.readI32());\n    UTA_TRY(edge.collisionRadius, cursor.readI32());",
             "    UTA_TRY(edge.collisionRadius, cursor.readI32());\n    UTA_TRY(edge.distance, cursor.readI32());"),
            ("swap NavEdge collisionHeight and reachFlags", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(edge.collisionHeight, cursor.readI32());\n    UTA_TRY(edge.reachFlags, cursor.readI32());",
             "    UTA_TRY(edge.reachFlags, cursor.readI32());\n    UTA_TRY(edge.collisionHeight, cursor.readI32());"),
            ("swap WiringNode firstIncoming and incomingCount", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(node.firstIncoming, cursor.readU32());\n    UTA_TRY(node.incomingCount, cursor.readU32());",
             "    UTA_TRY(node.incomingCount, cursor.readU32());\n    UTA_TRY(node.firstIncoming, cursor.readU32());"),
            ("swap WiringNode firstOutgoing and outgoingCount", "src/ubundle/Bundle.cpp",
             "    UTA_TRY(node.firstOutgoing, cursor.readU32());\n    UTA_TRY(node.outgoingCount, cursor.readU32());",
             "    UTA_TRY(node.outgoingCount, cursor.readU32());\n    UTA_TRY(node.firstOutgoing, cursor.readU32());"),
            ("swap the RoomMap bands and roomForZone order", "src/ubundle/Bundle.cpp",
             '    UTA_TRY(map.bands, readVector<float>(cursor, MIN_F32, "bands", readF32Element));',
             '    UTA_TRY(map.roomForZone,\n            readVector<std::uint32_t>(cursor, MIN_U32, "x", readU32Element));\n'
             '    UTA_TRY(map.bands, readVector<float>(cursor, MIN_F32, "bands", readF32Element));'),
            ("the WRITER swaps iFront and iBack", "src/ubundle/Bundle.cpp",
             "    sink.putI32(node.iFront);\n    sink.putI32(node.iBack);",
             "    sink.putI32(node.iBack);\n    sink.putI32(node.iFront);"),

            # -- SS 4.2 rule 1: the allocation bound. INV-1 and INV-2.
            ("the count bound multiplies instead of dividing", "src/ubundle/Bundle.cpp",
             "    if (count > cursor.remaining() / minElement)",
             "    if (static_cast<std::uint32_t>(count * minElement) > cursor.remaining())"),
            ("the count bound is removed", "src/ubundle/Bundle.cpp",
             "    if (count > cursor.remaining() / minElement)",
             "    if (count > 0xFFFFFFFFU)"),
            ("readBytes' own bound is removed", "src/ubundle/Bundle.cpp",
             '        if (remaining() < count) return shortRead("byte run");',
             "        // removed"),

            # -- SS 4.3: the header. INV-4, INV-5, INV-12.
            ("the version check becomes a lower bound", "src/ubundle/Bundle.cpp",
             "    if (raw.header.formatVersion != FORMAT_VERSION)",
             "    if (raw.header.formatVersion < FORMAT_VERSION)"),
            ("the magic check is removed", "src/ubundle/Bundle.cpp",
             "        if (actual != expected)\n"
             '            return fail(ErrorCode::MalformedData, "not a .utab bundle: the magic is wrong");',
             "        (void)actual;"),
            ("the origin range check is removed", "src/ubundle/Bundle.cpp",
             "    if (origin > static_cast<std::uint8_t>(Origin::Authored))\n"
             "        return fail(ErrorCode::MalformedData,\n"
             '                    "origin byte " + std::to_string(origin) + " is not 0 or 1");',
             "    // removed"),
            ("the kind range check is removed", "src/ubundle/Bundle.cpp",
             "    if (kind > static_cast<std::uint8_t>(BundleKind::Character))\n"
             "        return fail(ErrorCode::MalformedData,\n"
             '                    "kind byte " + std::to_string(kind) + " is not 0 or 1");',
             "    // removed"),
            ("the header's reserved check is removed", "src/ubundle/Bundle.cpp",
             "    if (reserved != 0)\n"
             '        return fail(ErrorCode::MalformedData, "the header\'s reserved field is not zero");',
             "    // removed"),

            # -- SS 4.4: the section table. INV-11.
            ("the compression check is removed", "src/ubundle/Bundle.cpp",
             "        if (compression != 0)", "        if (compression == 0xFFU)"),
            ("a descriptor's reserved check is removed", "src/ubundle/Bundle.cpp",
             "            if (reserved != 0)\n"
             "                return fail(ErrorCode::MalformedData,\n"
             '                            "a section descriptor\'s reserved field is not zero");',
             "            (void)reserved;"),
            ("an unknown section id is reinterpreted rather than refused", "src/ubundle/Bundle.cpp",
             "        if (!knownId(descriptor.id))\n"
             '            return fail(ErrorCode::MalformedData, "a section id is not defined in this version");',
             "        if (!knownId(descriptor.id)) { descriptor.id = ID_WIRG; }"),
            ("the duplicate-id check is removed", "src/ubundle/Bundle.cpp",
             "            if (descriptors[i].id == descriptors[j].id)\n"
             '                return fail(ErrorCode::MalformedData, "a section id appears twice");',
             "            (void)j;"),
            ("the ascending-offset check is removed", "src/ubundle/Bundle.cpp",
             "        if (i > 0 && descriptors[i].offset < descriptors[i - 1].offset)\n"
             "            return fail(ErrorCode::MalformedData,\n"
             '                        "section descriptors are not in ascending offset order");',
             "        // removed"),
            ("the tiling check is removed", "src/ubundle/Bundle.cpp",
             "        if (descriptor.offset != expected)\n"
             "            return fail(ErrorCode::MalformedData,\n"
             '                        "the sections do not tile the file: a gap or an overlap");',
             "        // removed"),
            ("the trailing-bytes check is removed", "src/ubundle/Bundle.cpp",
             "    if (expected != fileSize)\n"
             '        return fail(ErrorCode::MalformedData, "bytes trail the last section");',
             "    // removed"),
            ("the sectionCount bound multiplies instead of dividing", "src/ubundle/Bundle.cpp",
             "    if (raw.sectionCount > (fileSize - HEADER_SIZE) / SECTION_DESCRIPTOR_SIZE)",
             "    if (HEADER_SIZE + static_cast<std::uint32_t>(raw.sectionCount * SECTION_DESCRIPTOR_SIZE) > fileSize)"),
            ("the section-ends-unread check is removed", "src/ubundle/Bundle.cpp",
             "        if (payload.remaining() != 0)\n"
             '            return fail(ErrorCode::MalformedData, "a section ends with bytes unread");',
             "        // removed"),

            # -- SS 4.9: structural validation of ROOM. INV-3.
            ("the roomForZone entry bound is removed", "src/ubundle/Bundle.cpp",
             "        if (room != umap::NO_ROOM && room >= roomCount)\n"
             '            return fail(code, "ROOM: a roomForZone entry names no room");',
             "        (void)room;"),
            ("the roomForZone[0] rule is removed", "src/ubundle/Bundle.cpp",
             "    if (!map.roomForZone.empty() && map.roomForZone[0] != umap::NO_ROOM)\n"
             '        return fail(code, "ROOM: roomForZone[0] is not NO_ROOM");',
             "    // removed"),
            ("the zone-zero rule is removed", "src/ubundle/Bundle.cpp",
             '        if (room.zoneIndex == 0) return fail(code, "ROOM: a room names zone 0");',
             "        // removed"),
            ("the zoneIndex bound is removed", "src/ubundle/Bundle.cpp",
             "        if (room.zoneIndex >= zoneCount)\n"
             '            return fail(code, "ROOM: a room\'s zoneIndex is outside roomForZone");',
             "        if (room.zoneIndex >= zoneCount) continue;"),
            ("the two-table agreement rule is removed", "src/ubundle/Bundle.cpp",
             "        if (static_cast<std::uint64_t>(map.roomForZone[room.zoneIndex]) != position)\n"
             '            return fail(code, "ROOM: roomForZone and rooms disagree about which room owns a zone");',
             "        // removed"),
            ("the empty-floors rule is removed", "src/ubundle/Bundle.cpp",
             '        if (room.floors.empty()) return fail(code, "ROOM: a room\'s floors is empty");',
             "        // removed"),
            ("the floor-band bound is removed", "src/ubundle/Bundle.cpp",
             "            if (floor >= map.bands.size())\n"
             '                return fail(code, "ROOM: a room names a floor band that does not exist");',
             "            (void)floor;"),
            ("the bands-ascending rule is removed", "src/ubundle/Bundle.cpp",
             "        if (!(map.bands[i] >= map.bands[i - 1]))\n"
             '            return fail(code, "ROOM: bands are not in ascending order");',
             "        (void)i;"),
            ("the leafZone bound is removed", "src/ubundle/Bundle.cpp",
             "        if (zone != umap::ZONE_REFUSED && zone >= zoneCount)\n"
             '            return fail(code, "ROOM: a leafZone entry is outside roomForZone");',
             "        (void)zone;"),
            ("the node child-index bound is removed", "src/ubundle/Bundle.cpp",
             "        if (!indexOrNone(node.iFront, map.nodes.size())\n"
             "            || !indexOrNone(node.iBack, map.nodes.size()))\n"
             '            return fail(code, "ROOM: a node\'s child index is out of range");',
             "        // removed"),
            ("the node leaf-index bound is removed", "src/ubundle/Bundle.cpp",
             "        if (!indexOrNone(node.iLeaf[0], map.leafZone.size())\n"
             "            || !indexOrNone(node.iLeaf[1], map.leafZone.size()))\n"
             '            return fail(code, "ROOM: a node\'s leaf index is out of range");',
             "        // removed"),

            # -- SS 4.9: NAVG.
            ("the NAVG ascending-exportIndex rule is removed", "src/ubundle/Bundle.cpp",
             "        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)\n"
             '            return fail(code, "NAVG: nodes are not in strictly ascending exportIndex order");',
             "        // removed"),
            ("the NAVG ascending rule becomes non-strict", "src/ubundle/Bundle.cpp",
             "        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)\n"
             '            return fail(code, "NAVG: nodes are not in strictly ascending exportIndex order");',
             "        if (graph.nodes[i].exportIndex < graph.nodes[i - 1].exportIndex)\n"
             '            return fail(code, "NAVG: nodes are not in ascending exportIndex order");'),
            ("the NAVG run bound is removed", "src/ubundle/Bundle.cpp",
             "        if (!runWithin(node.firstEdge, node.edgeCount, graph.edges.size()))\n"
             '            return fail(code, "NAVG: a node\'s edge run reaches past the edge table");',
             "        if (!runWithin(node.firstEdge, node.edgeCount, graph.edges.size())) continue;"),
            ("the NAVG run-membership rule is removed", "src/ubundle/Bundle.cpp",
             "            if (graph.edges[static_cast<std::size_t>(node.firstEdge) + j].from != i)\n"
             '                return fail(code, "NAVG: an edge in a node\'s run does not name that node");',
             "            (void)j;"),
            ("the NAVG endpoint bound is removed", "src/ubundle/Bundle.cpp",
             "        if (edge.from >= graph.nodes.size() || edge.to >= graph.nodes.size())\n"
             '            return fail(code, "NAVG: an edge endpoint names no node");',
             "        // removed"),

            # -- SS 4.9: WIRG.
            ("the WIRG ascending-exportIndex rule is removed", "src/ubundle/Bundle.cpp",
             "        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)\n"
             '            return fail(code, "WIRG: nodes are not in strictly ascending exportIndex order");',
             "        // removed"),
            ("the WIRG incoming/edges size equality is removed", "src/ubundle/Bundle.cpp",
             "    if (graph.incoming.size() != graph.edges.size())\n"
             '        return fail(code, "WIRG: incoming and edges hold different numbers of edges");',
             "    // removed"),
            ("the WIRG outgoing run bound is removed", "src/ubundle/Bundle.cpp",
             "        if (!runWithin(node.firstOutgoing, node.outgoingCount, graph.edges.size()))\n"
             '            return fail(code, "WIRG: a node\'s outgoing run reaches past the edge table");',
             "        if (!runWithin(node.firstOutgoing, node.outgoingCount, graph.edges.size())) continue;"),
            ("the WIRG outgoing membership rule is removed", "src/ubundle/Bundle.cpp",
             "            if (graph.edges[static_cast<std::size_t>(node.firstOutgoing) + j].from != i)\n"
             '                return fail(code, "WIRG: an edge in a node\'s outgoing run does not name that node");',
             "            (void)j;"),
            ("the WIRG incoming run bound is removed", "src/ubundle/Bundle.cpp",
             "        if (!runWithin(node.firstIncoming, node.incomingCount, graph.incoming.size()))\n"
             '            return fail(code, "WIRG: a node\'s incoming run reaches past the incoming table");',
             "        if (!runWithin(node.firstIncoming, node.incomingCount, graph.incoming.size())) continue;"),
            ("the WIRG incoming membership rule is removed", "src/ubundle/Bundle.cpp",
             "            if (graph.incoming[static_cast<std::size_t>(node.firstIncoming) + j].to != i)\n"
             '                return fail(code, "WIRG: an edge in a node\'s incoming run does not name that node");',
             "            (void)j;"),
            ("the WIRG dangling bound is removed", "src/ubundle/Bundle.cpp",
             "        if (dangling.from >= graph.nodes.size())\n"
             '            return fail(code, "WIRG: a dangling event names no node");',
             "        // removed"),

            # -- SS 4.2 and SS 4.10: floats and the writer. INV-9, INV-8.
            ("a float is moved through a wider type", "src/ubundle/Bundle.cpp",
             "        UTA_TRY(const std::uint32_t bits, readU32());\n        return std::bit_cast<float>(bits);",
             "        UTA_TRY(const std::uint32_t bits, readU32());\n"
             "        const double wide = static_cast<double>(std::bit_cast<float>(bits));\n"
             "        return wide == 0.0 ? 0.0F : static_cast<float>(wide);"),
            ("write no longer refuses an invalid bundle", "src/ubundle/Bundle.cpp",
             "    if (bundle.nav) UTA_CHECK(validateNavGraph(*bundle.nav, ErrorCode::InvalidArgument));",
             "    // removed"),
            ("write emits NAVG before ROOM", "src/ubundle/Bundle.cpp",
             "    if (bundle.rooms) sections.emplace_back(ID_ROOM, encodeRoomMap(*bundle.rooms));\n"
             "    if (bundle.nav) sections.emplace_back(ID_NAVG, encodeNavGraph(*bundle.nav));",
             "    if (bundle.nav) sections.emplace_back(ID_NAVG, encodeNavGraph(*bundle.nav));\n"
             "    if (bundle.rooms) sections.emplace_back(ID_ROOM, encodeRoomMap(*bundle.rooms));"),

            # -- SS 4.5: combine, which lives in the header.
            ("combine becomes a maximum", "src/ubundle/Bundle.h",
             "    return (a == Origin::Authored && b == Origin::Authored) ? Origin::Authored\n"
             "                                                            : Origin::Derived;",
             "    return static_cast<std::uint8_t>(a) > static_cast<std::uint8_t>(b) ? a : b;"),
        ],
        # Survivors that are REDUNDANCY rather than a gap. Each was chased to
        # the point of proving no fixture can isolate it. Adding a line here is
        # a claim; make it only after trying to build the fixture.
        "expected_survivors": {
            "the ascending-offset check is removed":
                "Subsumed by the tiling rule: a table that is out of order cannot also "
                "have each section beginning where the last ended. SS 4.4 names both.",
            "the zone-zero rule is removed":
                "Subsumed by the roomForZone[0] rule plus two-table agreement. A room "
                "naming zone 0 needs roomForZone[0] to be its own position, which the "
                "index-0 rule forbids, so no fixture reaches it. SS 4.9 names both.",
            "readBytes' own bound is removed":
                "Undefined behaviour rather than a wrong answer, so the Release leg "
                "cannot see it. Run with --asan, where it reports a heap-buffer-overflow.",
        },
    },
}


def run(cmd, timeout=900, env=None):
    return subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True,
                          text=True, errors="replace", timeout=timeout, env=env)


def probe(build_dir, subject, label, rel, old, new, test_cmd, env):
    path = ROOT / rel
    original = path.read_text()
    if old not in original:
        return "NOT-APPLIED"
    if original.count(old) != 1:
        return "NOT-UNIQUE"
    path.write_text(original.replace(old, new, 1))
    # Rewrite and touch. A restore that leaves an older mtime lets ninja skip
    # the rebuild, and the next mutation is graded against this one's binary.
    os.utime(path, None)
    try:
        if run(f"cmake --build {build_dir} --target {subject['target']}").returncode != 0:
            return "COMPILE-FAIL"
        try:
            return "KILLED" if run(test_cmd, timeout=300, env=env).returncode != 0 else "SURVIVED"
        except subprocess.TimeoutExpired:
            return "KILLED"
    finally:
        path.write_text(original)
        os.utime(path, None)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("subject", choices=sorted(SUBJECTS))
    parser.add_argument("--asan", action="store_true",
                        help="probe in build-asan/ instead of build/, for rules whose "
                             "removal is undefined behaviour rather than a wrong answer")
    parser.add_argument("--only", help="probe only mutations whose label contains this")
    args = parser.parse_args()

    subject = SUBJECTS[args.subject]
    build_dir = "build-asan" if args.asan else "build"
    if not (ROOT / build_dir).is_dir():
        sys.exit(f"no {build_dir}/ -- CLAUDE.md SS Build and test says how to configure it")

    env = dict(os.environ)
    if args.asan:
        # NOT a ulimit. ASan reserves tens of terabytes of address space for
        # its shadow map, so a virtual-memory cap kills it at startup, before
        # any test runs -- and that reads as a mutation KILLED.
        env["ASAN_OPTIONS"] = "allocator_may_return_null=0:max_allocation_size_mb=2048"
    test_cmd = f"./{build_dir}/{subject['binary']} '{subject['filter']}'"

    if run(f"cmake --build {build_dir} --target {subject['target']}").returncode != 0:
        sys.exit("the unmutated tree does not build")
    base = run(test_cmd, env=env)
    if base.returncode != 0:
        sys.exit("the unmutated tree is already red -- fix that before probing")
    # A filter matching NO tests exits non-zero, under which every mutation
    # reads as KILLED. Assert the count the subject expects.
    if f"{subject['cases']} test case" not in base.stdout:
        sys.exit(f"filter {subject['filter']} did not match {subject['cases']} cases -- "
                 "a tag typo would make every mutation read as killed")
    print(f"baseline green in {build_dir}/, {subject['cases']} cases matched\n")

    results = []
    for label, rel, old, new in subject["mutations"]:
        if args.only and args.only not in label:
            continue
        state = probe(build_dir, subject, label, rel, old, new, test_cmd, env)
        results.append((state, label))
        print(f"{state:>13}  {label}", flush=True)

    expected = subject["expected_survivors"]
    survived = [l for s, l in results if s == "SURVIVED"]
    broken = [(s, l) for s, l in results if s in ("NOT-APPLIED", "NOT-UNIQUE", "COMPILE-FAIL")]
    unexplained = [l for l in survived if l not in expected]

    print(f"\n=== {args.subject} in {build_dir}/ ===")
    print(f"killed {sum(1 for s, _ in results if s == 'KILLED')} of {len(results)}")
    for label in survived:
        note = expected.get(label, "NOT DECLARED -- no test grades this rule")
        print(f"  survived: {label}\n            {note}")
    for state, label in broken:
        print(f"  {state}: {label}")

    if unexplained or broken:
        print("\nA new survivor means some rule is no longer graded. Suspect the FIXTURE "
              "first: ask which rule makes it fail, and whether that is the rule it names.")
        sys.exit(1)
    print("\nsurvivors are exactly the declared ones")


if __name__ == "__main__":
    main()
