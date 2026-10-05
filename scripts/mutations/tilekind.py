"""UTA-0277's mutations, for scripts/mutation-probe.py -- which pictures may
move, and the player's answers.

Each entry: (invariant, label, project-relative file, search text,
replacement), as ubundle.py's. "" for a rule no invariant states.
"""

SUBJECT = {
    "spec": "docs/specs/UTA-0277-per-tile-variation.md",
    "target": "uta_unit_tests",
    "binary": "tests/uta_unit_tests",
    "filter": "[tilekind]",
    "mutations": [
        ("INV-2", "lines test inverted", "src/ubake/TileKind.cpp",
         "if (scores.lines < limits.linesShuffle && scores.spots < limits.spotsShuffle)",
         "if (scores.lines >= limits.linesShuffle && scores.spots < limits.spotsShuffle)"),
        ("INV-2", "spots never computed", "src/ubake/TileKind.cpp",
         "return TileScores{false, linesOf(hp, variance), spotsOf(hp)};",
         "return TileScores{false, linesOf(hp, variance), 0.0};"),
        ("", "diagonal bands not counted", "src/ubake/TileKind.cpp",
         "return std::max(axes, diagonals) / variance * LINES_SCALE;",
         "return axes / variance * LINES_SCALE;"),
        ("", "the hash ignores the picture's size", "src/ubake/TileKind.cpp",
         "    hash.add(size);\n", ""),
        ("INV-6", "an excluded material takes an answer", "src/ubundle/TileAnswers.cpp",
         "if (record.tileHash == Hash{}) continue;", ""),
        ("INV-8", "two different answers for one picture both kept", "src/ubundle/TileAnswers.cpp",
         "if (!conflicted.contains(hash)) out.answers", "out.answers"),
        ("", "a material no surface spans twice keeps its hash", "src/ubake/Bake.cpp",
         "            record.tileHash = {};\n", ""),
        ("INV-1", "no picture hash made", "src/ubake/Bake.cpp",
         "        tileHash = pictureHash(*resolved);\n", ""),
        ("INV-9", "Fixed materials listed", "src/ubake/Bake.cpp",
         "        if (record.tileKind != ubundle::TileKind::Unsure) continue;\n", ""),
        ("INV-9", "questions not ordered by surfaces", "src/ubake/Bake.cpp",
         "    std::ranges::stable_sort(questions,", "    if (false) std::ranges::stable_sort(questions,"),
    ],
    "expected_survivors": {},
}
