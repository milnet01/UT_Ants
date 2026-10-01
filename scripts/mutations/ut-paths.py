"""ut-paths' mutations, for scripts/mutation-probe.py -- UTA-0121's rules.

Each entry: (invariant, label, project-relative file, search text,
replacement), as scripts/mutations/ubundle.py describes. INV-1 (md5) is
tested under [core], outside this subject's filter, so it carries none here.
"""

SEEDS = "tools/ut-paths/Seeds.cpp"

SUBJECT = {
    "spec": "docs/specs/UTA-0121-bot-path-seeds.md",
    "target": "uta_unit_tests",
    "binary": "tests/uta_unit_tests",
    "filter": "[paths]",
    "mutations": [
        # -- SS 3 decision 10: the edges the network keeps.
        ("INV-11", "a pruned spec is kept", SEEDS,
         "walkable(edge) && edge.pruned == 0 &&",
         "walkable(edge) &&"),
        ("INV-11", "every spec is walkable", SEEDS,
         "    return unav::mayTraverse(edge, BOT);",
         "    (void)edge;\n    return true;"),
        ("INV-16", "a dead-end teleporter's special edge is kept", SEEDS,
         "!((edge.reachFlags & 32) != 0 && deadEnd[edge.from]))",
         "true)"),
        # -- SS 4.6: the start and the exits.
        ("INV-10", "the LAST PlayerStart is the start", SEEDS,
         'if (!started && descendsFrom(actorClass, "engine.playerstart")) {',
         'if (descendsFrom(actorClass, "engine.playerstart")) {'),
        ("INV-12", "the world bound is not tested on Z", SEEDS,
         "std::abs(p.x) >= BOUND || std::abs(p.y) >= BOUND || std::abs(p.z) >= BOUND",
         "std::abs(p.x) >= BOUND || std::abs(p.y) >= BOUND"),
        ("INV-12", "the world bound is not tested on Y", SEEDS,
         "std::abs(p.x) >= BOUND || std::abs(p.y) >= BOUND || std::abs(p.z) >= BOUND",
         "std::abs(p.x) >= BOUND || std::abs(p.z) >= BOUND"),
        # -- SS 4.7: search, routes and nodes.
        ("INV-7", "the start part follows edges backwards", SEEDS,
         "reach(adjacency, *startNode, false)",
         "reach(adjacency, *startNode, true)"),
        ("INV-13", "any node reaching the exit's part is a fallback goal", SEEDS,
         "                if (!touches(scene.network[i], exit)) continue;\n",
         ""),
        ("INV-6", "the first search keeps mover spots", SEEDS,
         "shortestPath(graph, sources, goal, moverSpot); !path.empty()",
         "shortestPath(graph, sources, goal, keepAll); !path.empty()"),
        ("INV-5", "a substituted navigation point skips the hop test", SEEDS,
         "            if (hops.allowed(last, *point)) {",
         "            if (true) {"),
        ("INV-5", "a found path is never searched again round slopes", SEEDS,
         "        if (std::none_of(path.begin(), path.end(), nearSlope)) return path;",
         "        return path;"),
        ("INV-14", "mover comes before teleporter in Why no route", SEEDS,
         "return teleporter ? NoRoute::Teleporter : mover ? NoRoute::Mover : NoRoute::Walled;",
         "return mover ? NoRoute::Mover : teleporter ? NoRoute::Teleporter : NoRoute::Walled;"),
    ],
    # None declared. A survivor here is a finding until a fixture has been
    # tried for it.
    "expected_survivors": {},
}
