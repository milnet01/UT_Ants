"""ubundle's mutations, for scripts/mutation-probe.py -- UTA-0008's rules.

Each entry: (invariant, label, project-relative file, search text,
replacement). The invariant is the one the mutation breaks, as the spec names
it; "" for a rule no invariant states. The search text must be unique in the
file -- a replacement count of one is what makes a mutation one rule rather
than several.
"""

SUBJECT = {
    "spec": "docs/specs/UTA-0008-bundle-container-and-origin.md",
    "target": "uta_unit_tests",
    "binary": "tests/uta_unit_tests",
    "filter": "[ubundle]",
    "mutations": [
        # -- SS 4.6 to SS 4.8: two adjacent fields of one width and type,
        # transposed. INV-6 and INV-7. A round trip cannot see these; only
        # the hand-authored golden array can.
        ("INV-6", "swap Node iLeaf[0] and iLeaf[1]", "src/ubundle/RoomSection.cpp",
         "    UTA_TRY(node.iLeaf[0], cursor.readI32());\n    UTA_TRY(node.iLeaf[1], cursor.readI32());",
         "    UTA_TRY(node.iLeaf[1], cursor.readI32());\n    UTA_TRY(node.iLeaf[0], cursor.readI32());"),
        ("INV-6", "swap Node normal.x and normal.y", "src/ubundle/RoomSection.cpp",
         "    UTA_TRY(node.normal.x, cursor.readF32());\n    UTA_TRY(node.normal.y, cursor.readF32());",
         "    UTA_TRY(node.normal.y, cursor.readF32());\n    UTA_TRY(node.normal.x, cursor.readF32());"),
        ("INV-6", "swap Node normal.z and w", "src/ubundle/RoomSection.cpp",
         "    UTA_TRY(node.normal.z, cursor.readF32());\n    UTA_TRY(node.w, cursor.readF32());",
         "    UTA_TRY(node.w, cursor.readF32());\n    UTA_TRY(node.normal.z, cursor.readF32());"),
        ("INV-6", "swap Node iZone[0] and iZone[1]", "src/ubundle/RoomSection.cpp",
         "    UTA_TRY(node.iZone[0], cursor.readU8());\n    UTA_TRY(node.iZone[1], cursor.readU8());",
         "    UTA_TRY(node.iZone[1], cursor.readU8());\n    UTA_TRY(node.iZone[0], cursor.readU8());"),
        ("INV-6", "swap Room minZ and maxZ", "src/ubundle/RoomSection.cpp",
         "    UTA_TRY(room.minZ, cursor.readF32());\n    UTA_TRY(room.maxZ, cursor.readF32());",
         "    UTA_TRY(room.maxZ, cursor.readF32());\n    UTA_TRY(room.minZ, cursor.readF32());"),
        ("INV-6", "swap Point2 x and y", "src/ubundle/RoomSection.cpp",
         "    UTA_TRY(point.x, cursor.readF32());\n    UTA_TRY(point.y, cursor.readF32());",
         "    UTA_TRY(point.y, cursor.readF32());\n    UTA_TRY(point.x, cursor.readF32());"),
        ("INV-6", "swap NavNode firstEdge and edgeCount", "src/ubundle/NavSection.cpp",
         "    UTA_TRY(node.firstEdge, cursor.readU32());\n    UTA_TRY(node.edgeCount, cursor.readU32());",
         "    UTA_TRY(node.edgeCount, cursor.readU32());\n    UTA_TRY(node.firstEdge, cursor.readU32());"),
        ("INV-6", "swap NavEdge distance and collisionRadius", "src/ubundle/NavSection.cpp",
         "    UTA_TRY(edge.distance, cursor.readI32());\n    UTA_TRY(edge.collisionRadius, cursor.readI32());",
         "    UTA_TRY(edge.collisionRadius, cursor.readI32());\n    UTA_TRY(edge.distance, cursor.readI32());"),
        ("INV-6", "swap NavEdge collisionHeight and reachFlags", "src/ubundle/NavSection.cpp",
         "    UTA_TRY(edge.collisionHeight, cursor.readI32());\n    UTA_TRY(edge.reachFlags, cursor.readI32());",
         "    UTA_TRY(edge.reachFlags, cursor.readI32());\n    UTA_TRY(edge.collisionHeight, cursor.readI32());"),
        ("INV-6", "swap WiringNode firstIncoming and incomingCount", "src/ubundle/WiringSection.cpp",
         "    UTA_TRY(node.firstIncoming, cursor.readU32());\n    UTA_TRY(node.incomingCount, cursor.readU32());",
         "    UTA_TRY(node.incomingCount, cursor.readU32());\n    UTA_TRY(node.firstIncoming, cursor.readU32());"),
        ("INV-6", "swap WiringNode firstOutgoing and outgoingCount", "src/ubundle/WiringSection.cpp",
         "    UTA_TRY(node.firstOutgoing, cursor.readU32());\n    UTA_TRY(node.outgoingCount, cursor.readU32());",
         "    UTA_TRY(node.outgoingCount, cursor.readU32());\n    UTA_TRY(node.firstOutgoing, cursor.readU32());"),
        ("INV-6", "swap the RoomMap bands and roomForZone order", "src/ubundle/RoomSection.cpp",
         '    UTA_TRY(map.bands, readVector<float>(cursor, MIN_F32, "bands", readF32Element));',
         '    UTA_TRY(map.roomForZone,\n            readVector<std::uint32_t>(cursor, MIN_U32, "x", readU32Element));\n'
         '    UTA_TRY(map.bands, readVector<float>(cursor, MIN_F32, "bands", readF32Element));'),
        ("INV-7", "the WRITER swaps iFront and iBack", "src/ubundle/RoomSection.cpp",
         "    sink.putI32(node.iFront);\n    sink.putI32(node.iBack);",
         "    sink.putI32(node.iBack);\n    sink.putI32(node.iFront);"),

        # -- SS 4.2 rule 1: the allocation bound. INV-1 and INV-2.
        ("INV-2", "the count bound multiplies instead of dividing", "src/ubundle/Codec.h",
         "    if (count > cursor.remaining() / minElement)",
         "    if (static_cast<std::uint32_t>(count * minElement) > cursor.remaining())"),
        ("INV-2", "the count bound is removed", "src/ubundle/Codec.h",
         "    if (count > cursor.remaining() / minElement)",
         "    if (count > 0xFFFFFFFFU)"),
        ("INV-1", "readBytes' own bound is removed", "src/ubundle/Codec.h",
         '        if (remaining() < count) return shortRead("byte run");',
         "        // removed"),

        # -- SS 4.3: the header. INV-4, INV-5, INV-12.
        ("INV-4", "the version check becomes a lower bound", "src/ubundle/Bundle.cpp",
         "    if (raw.header.formatVersion != FORMAT_VERSION)",
         "    if (raw.header.formatVersion < FORMAT_VERSION)"),
        ("INV-4", "the magic check is removed", "src/ubundle/Bundle.cpp",
         "        if (actual != expected)\n"
         '            return fail(ErrorCode::MalformedData, "not a .utab bundle: the magic is wrong");',
         "        (void)actual;"),
        ("INV-5", "the origin range check is removed", "src/ubundle/Bundle.cpp",
         "    if (origin > static_cast<std::uint8_t>(Origin::Authored))\n"
         "        return fail(ErrorCode::MalformedData,\n"
         '                    "origin byte " + std::to_string(origin) + " is not 0 or 1");',
         "    // removed"),
        ("INV-12", "the kind range check is removed", "src/ubundle/Bundle.cpp",
         "    if (kind > static_cast<std::uint8_t>(BundleKind::Character))\n"
         "        return fail(ErrorCode::MalformedData,\n"
         '                    "kind byte " + std::to_string(kind) + " is not 0 or 1");',
         "    // removed"),
        ("INV-12", "the header's reserved check is removed", "src/ubundle/Bundle.cpp",
         "    if (reserved != 0)\n"
         '        return fail(ErrorCode::MalformedData, "the header\'s reserved field is not zero");',
         "    // removed"),

        # -- SS 4.4: the section table. INV-11.
        ("INV-12", "the compression check is removed", "src/ubundle/Bundle.cpp",
         "        if (compression != 0)", "        if (compression == 0xFFU)"),
        ("INV-11", "a descriptor's reserved check is removed", "src/ubundle/Bundle.cpp",
         "            if (reserved != 0)\n"
         "                return fail(ErrorCode::MalformedData,\n"
         '                            "a section descriptor\'s reserved field is not zero");',
         "            (void)reserved;"),
        ("INV-11", "an unknown section id is reinterpreted rather than refused", "src/ubundle/Bundle.cpp",
         "        if (!knownId(descriptor.id))\n"
         '            return fail(ErrorCode::MalformedData, "a section id is not defined in this version");',
         "        if (!knownId(descriptor.id)) { descriptor.id = ID_WIRG; }"),
        ("INV-11", "the duplicate-id check is removed", "src/ubundle/Bundle.cpp",
         "            if (descriptors[i].id == descriptors[j].id)\n"
         '                return fail(ErrorCode::MalformedData, "a section id appears twice");',
         "            (void)j;"),
        ("INV-11", "the ascending-offset check is removed", "src/ubundle/Bundle.cpp",
         "        if (i > 0 && descriptors[i].offset < descriptors[i - 1].offset)\n"
         "            return fail(ErrorCode::MalformedData,\n"
         '                        "section descriptors are not in ascending offset order");',
         "        // removed"),
        ("INV-11", "the tiling check is removed", "src/ubundle/Bundle.cpp",
         "        if (descriptor.offset != expected)\n"
         "            return fail(ErrorCode::MalformedData,\n"
         '                        "the sections do not tile the file: a gap or an overlap");',
         "        // removed"),
        ("INV-11", "the trailing-bytes check is removed", "src/ubundle/Bundle.cpp",
         "    if (expected != fileSize)\n"
         '        return fail(ErrorCode::MalformedData, "bytes trail the last section");',
         "    // removed"),
        ("INV-2", "the sectionCount bound multiplies instead of dividing", "src/ubundle/Bundle.cpp",
         "    if (raw.sectionCount > (fileSize - HEADER_SIZE) / SECTION_DESCRIPTOR_SIZE)",
         "    if (HEADER_SIZE + static_cast<std::uint32_t>(raw.sectionCount * SECTION_DESCRIPTOR_SIZE) > fileSize)"),
        ("INV-11", "the section-ends-unread check is removed", "src/ubundle/Bundle.cpp",
         "        if (payload.remaining() != 0)\n"
         '            return fail(ErrorCode::MalformedData, "a section ends with bytes unread");',
         "        // removed"),

        # -- SS 4.9: structural validation of ROOM. INV-3.
        ("INV-3", "the roomForZone entry bound is removed", "src/ubundle/RoomSection.cpp",
         "        if (room != umap::NO_ROOM && room >= roomCount)\n"
         '            return fail(code, "ROOM: a roomForZone entry names no room");',
         "        (void)room;"),
        ("INV-3", "the roomForZone[0] rule is removed", "src/ubundle/RoomSection.cpp",
         "    if (!map.roomForZone.empty() && map.roomForZone[0] != umap::NO_ROOM)\n"
         '        return fail(code, "ROOM: roomForZone[0] is not NO_ROOM");',
         "    // removed"),
        ("INV-3", "the zone-zero rule is removed", "src/ubundle/RoomSection.cpp",
         '        if (room.zoneIndex == 0) return fail(code, "ROOM: a room names zone 0");',
         "        // removed"),
        ("INV-3", "the zoneIndex bound is removed", "src/ubundle/RoomSection.cpp",
         "        if (room.zoneIndex >= zoneCount)\n"
         '            return fail(code, "ROOM: a room\'s zoneIndex is outside roomForZone");',
         "        if (room.zoneIndex >= zoneCount) continue;"),
        ("INV-3", "the two-table agreement rule is removed", "src/ubundle/RoomSection.cpp",
         "        if (static_cast<std::uint64_t>(map.roomForZone[room.zoneIndex]) != position)\n"
         '            return fail(code, "ROOM: roomForZone and rooms disagree about which room owns a zone");',
         "        // removed"),
        ("INV-3", "the empty-floors rule is removed", "src/ubundle/RoomSection.cpp",
         '        if (room.floors.empty()) return fail(code, "ROOM: a room\'s floors is empty");',
         "        // removed"),
        ("INV-3", "the floor-band bound is removed", "src/ubundle/RoomSection.cpp",
         "            if (floor >= map.bands.size())\n"
         '                return fail(code, "ROOM: a room names a floor band that does not exist");',
         "            (void)floor;"),
        ("INV-3", "the bands-ascending rule is removed", "src/ubundle/RoomSection.cpp",
         "        if (!(map.bands[i] >= map.bands[i - 1]))\n"
         '            return fail(code, "ROOM: bands are not in ascending order");',
         "        (void)i;"),
        ("INV-3", "the leafZone bound is removed", "src/ubundle/RoomSection.cpp",
         "        if (zone != umap::ZONE_REFUSED && zone >= zoneCount)\n"
         '            return fail(code, "ROOM: a leafZone entry is outside roomForZone");',
         "        (void)zone;"),
        ("INV-3", "the node child-index bound is removed", "src/ubundle/RoomSection.cpp",
         "        if (!indexOrNone(node.iFront, map.nodes.size())\n"
         "            || !indexOrNone(node.iBack, map.nodes.size()))\n"
         '            return fail(code, "ROOM: a node\'s child index is out of range");',
         "        // removed"),
        ("INV-3", "the node leaf-index bound is removed", "src/ubundle/RoomSection.cpp",
         "        if (!indexOrNone(node.iLeaf[0], map.leafZone.size())\n"
         "            || !indexOrNone(node.iLeaf[1], map.leafZone.size()))\n"
         '            return fail(code, "ROOM: a node\'s leaf index is out of range");',
         "        // removed"),

        # -- SS 4.9: NAVG.
        ("INV-3", "the NAVG ascending-exportIndex rule is removed", "src/ubundle/NavSection.cpp",
         "        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)\n"
         '            return fail(code, "NAVG: nodes are not in strictly ascending exportIndex order");',
         "        // removed"),
        ("INV-3", "the NAVG ascending rule becomes non-strict", "src/ubundle/NavSection.cpp",
         "        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)\n"
         '            return fail(code, "NAVG: nodes are not in strictly ascending exportIndex order");',
         "        if (graph.nodes[i].exportIndex < graph.nodes[i - 1].exportIndex)\n"
         '            return fail(code, "NAVG: nodes are not in ascending exportIndex order");'),
        ("INV-3", "the NAVG run bound is removed", "src/ubundle/NavSection.cpp",
         "        if (!runWithin(node.firstEdge, node.edgeCount, graph.edges.size()))\n"
         '            return fail(code, "NAVG: a node\'s edge run reaches past the edge table");',
         "        if (!runWithin(node.firstEdge, node.edgeCount, graph.edges.size())) continue;"),
        ("INV-3", "the NAVG run-membership rule is removed", "src/ubundle/NavSection.cpp",
         "            if (graph.edges[static_cast<std::size_t>(node.firstEdge) + j].from != i)\n"
         '                return fail(code, "NAVG: an edge in a node\'s run does not name that node");',
         "            (void)j;"),
        ("INV-3", "the NAVG endpoint bound is removed", "src/ubundle/NavSection.cpp",
         "        if (edge.from >= graph.nodes.size() || edge.to >= graph.nodes.size())\n"
         '            return fail(code, "NAVG: an edge endpoint names no node");',
         "        // removed"),

        # -- SS 4.9: WIRG.
        ("INV-3", "the WIRG ascending-exportIndex rule is removed", "src/ubundle/WiringSection.cpp",
         "        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)\n"
         '            return fail(code, "WIRG: nodes are not in strictly ascending exportIndex order");',
         "        // removed"),
        ("INV-3", "the WIRG incoming/edges size equality is removed", "src/ubundle/WiringSection.cpp",
         "    if (graph.incoming.size() != graph.edges.size())\n"
         '        return fail(code, "WIRG: incoming and edges hold different numbers of edges");',
         "    // removed"),
        ("INV-3", "the WIRG outgoing run bound is removed", "src/ubundle/WiringSection.cpp",
         "        if (!runWithin(node.firstOutgoing, node.outgoingCount, graph.edges.size()))\n"
         '            return fail(code, "WIRG: a node\'s outgoing run reaches past the edge table");',
         "        if (!runWithin(node.firstOutgoing, node.outgoingCount, graph.edges.size())) continue;"),
        ("INV-3", "the WIRG outgoing membership rule is removed", "src/ubundle/WiringSection.cpp",
         "            if (graph.edges[static_cast<std::size_t>(node.firstOutgoing) + j].from != i)\n"
         '                return fail(code, "WIRG: an edge in a node\'s outgoing run does not name that node");',
         "            (void)j;"),
        ("INV-3", "the WIRG incoming run bound is removed", "src/ubundle/WiringSection.cpp",
         "        if (!runWithin(node.firstIncoming, node.incomingCount, graph.incoming.size()))\n"
         '            return fail(code, "WIRG: a node\'s incoming run reaches past the incoming table");',
         "        if (!runWithin(node.firstIncoming, node.incomingCount, graph.incoming.size())) continue;"),
        ("INV-3", "the WIRG incoming membership rule is removed", "src/ubundle/WiringSection.cpp",
         "            if (graph.incoming[static_cast<std::size_t>(node.firstIncoming) + j].to != i)\n"
         '                return fail(code, "WIRG: an edge in a node\'s incoming run does not name that node");',
         "            (void)j;"),
        ("INV-3", "the WIRG dangling bound is removed", "src/ubundle/WiringSection.cpp",
         "        if (dangling.from >= graph.nodes.size())\n"
         '            return fail(code, "WIRG: a dangling event names no node");',
         "        // removed"),

        # -- SS 4.2 and SS 4.10: floats and the writer. INV-9, INV-8.
        ("INV-9", "a float is moved through a wider type", "src/ubundle/Codec.h",
         "        UTA_TRY(const std::uint32_t bits, readU32());\n        return std::bit_cast<float>(bits);",
         "        UTA_TRY(const std::uint32_t bits, readU32());\n"
         "        const double wide = static_cast<double>(std::bit_cast<float>(bits));\n"
         "        return wide == 0.0 ? 0.0F : static_cast<float>(wide);"),
        ("", "write no longer refuses an invalid bundle", "src/ubundle/Bundle.cpp",
         "    if (bundle.nav) UTA_CHECK(validateNavGraph(*bundle.nav, ErrorCode::InvalidArgument));",
         "    // removed"),
        ("INV-7", "write emits NAVG before ROOM", "src/ubundle/Bundle.cpp",
         "    if (bundle.rooms) encoded(ID_ROOM, encodeRoomMap(*bundle.rooms));\n"
         "    if (bundle.nav) encoded(ID_NAVG, encodeNavGraph(*bundle.nav));",
         "    if (bundle.nav) encoded(ID_NAVG, encodeNavGraph(*bundle.nav));\n"
         "    if (bundle.rooms) encoded(ID_ROOM, encodeRoomMap(*bundle.rooms));"),

        # -- SS 4.5: combine, which lives in the header.
        ("INV-12", "combine becomes a maximum", "src/ubundle/Bundle.h",
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
    # Declared above for the Release leg only. --asan exists to grade these,
    # so a survival there is unexplained (review-code 2026-09-26).
    "killed_under_asan": ["readBytes' own bound is removed"],
}
