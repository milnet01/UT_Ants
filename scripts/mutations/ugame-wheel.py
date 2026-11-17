"""UTA-0020's mutations, for scripts/mutation-probe.py -- what the weapon
wheel does in ugame: the time scale it opens, what it lists, and a pick.
Their own file, not ugame.py's, because a subject reports against one
spec's invariants and UTA-0019's are numbered alike.

Each entry: (invariant, label, project-relative file, search text,
replacement), as ubundle.py's.
"""

SUBJECT = {
    "spec": "docs/specs/UTA-0020-weapon-wheel.md",
    "target": "uta_unit_tests",
    "binary": "tests/uta_unit_tests",
    "filter": "[game]",
    "mutations": [
        ("INV-1", "every body counted as human", "src/ugame/Game.cpp",
         "        if (!game.bodies[i].human) continue;\n", ""),
        ("INV-1", "two humans still slowed", "src/ugame/Game.cpp",
         "const bool slowed = humans == 1 &&", "const bool slowed = humans >= 1 &&"),
        ("INV-1", "a dead human's bit read", "src/ugame/Game.cpp",
         "human < count && game.bodies[human].alive() && commands[human].wheelOpen",
         "human < count && commands[human].wheelOpen"),
        ("INV-1", "the last body's bit read", "src/ugame/Game.cpp",
         "&& commands[human].wheelOpen;", "&& commands[count - 1].wheelOpen;"),
        ("INV-1", "the scale left as the last tick's", "src/ugame/Game.cpp",
         "game.timeScale = slowed ? WHEEL_TIME_SCALE : 1;",
         "if (slowed) game.timeScale = WHEEL_TIME_SCALE;"),
        ("INV-2", "the open wheel stops fire", "src/ugame/Game.cpp",
         "if (game.bodies[i].alive()) fireBody(tick, static_cast<int>(i), commands[i]);",
         "if (game.bodies[i].alive() && !commands[i].wheelOpen) fireBody(tick, static_cast<int>(i), commands[i]);"),
        ("INV-4", "the time scale left out of the hash", "src/ugame/Game.cpp",
         "    addDouble(game.timeScale);\n", ""),
        ("INV-4", "human left out of the hash", "src/ugame/Game.cpp",
         "        add(body.human ? 1 : 0);\n", ""),
        ("INV-5", "uncarried weapons listed", "src/ugame/Game.cpp",
         "        if (body.weapons[i].has) carried.push_back(static_cast<int>(i));\n    std::ranges::sort",
         "        carried.push_back(static_cast<int>(i));\n    std::ranges::sort"),
        ("INV-5", "the 0 key first", "src/ugame/Game.cpp",
         "return static_cast<int>(slot == 0 ? 10 : slot) * 1000 + weapon;",
         "return static_cast<int>(slot) * 1000 + weapon;"),
        ("INV-6", "an uncarried pick honoured", "src/ugame/Game.cpp",
         "if (std::ranges::find(carried, int{command.weaponPick}) != carried.end()) return command.weaponPick;",
         "if (command.weaponPick >= 0 && command.weaponPick < static_cast<int>(body.weapons.size())) "
         "return command.weaponPick;"),
        ("INV-6", "the pick never read", "src/ugame/Game.cpp",
         "if (std::ranges::find(carried, int{command.weaponPick}) != carried.end()) return command.weaponPick;",
         ""),
        ("INV-6", "a refused pick drops the slot and step", "src/ugame/Game.cpp",
         "if (std::ranges::find(carried, int{command.weaponPick}) != carried.end()) return command.weaponPick;",
         "if (command.weaponPick >= 0) return std::ranges::find(carried, int{command.weaponPick}) != "
         "carried.end() ? int{command.weaponPick} : -1;"),
    ],
    "expected_survivors": {},
}
