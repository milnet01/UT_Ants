"""UTA-0019's mutations, for scripts/mutation-probe.py -- the weapon set's
tick: behaviour from the file, ammo, combos and body cylinders.

Each entry: (invariant, label, project-relative file, search text,
replacement), as ubundle.py's. "" for a rule no invariant states.
"""

SUBJECT = {
    "spec": "docs/specs/UTA-0019-weapon-set.md",
    "target": "uta_unit_tests",
    "binary": "tests/uta_unit_tests",
    "filter": "[game]",
    "mutations": [
        ("INV-4", "instant-hit damage fixed in code", "src/ugame/Fire.cpp",
         "hitBody(tick, hit.body, index, eye, end, hit.fraction, mode.damage * damageScale,\n"
         "                        mode.momentum * damageScale, mode.headshotScale);",
         "hitBody(tick, hit.body, index, eye, end, hit.fraction, 40 * damageScale,\n"
         "                        mode.momentum * damageScale, mode.headshotScale);"),
        ("INV-4", "refire fixed in code", "src/ugame/Game.cpp",
         "body.refireLeft += mode->refire;", "body.refireLeft += 0.13;"),
        ("INV-4", "projectile speed fixed in code", "src/ugame/Fire.cpp",
         "const double speed = tick.set.projectiles[kind].speed;", "const double speed = 840;"),
        ("INV-7", "a charge starts with no ammo", "src/ugame/Game.cpp",
         "} else if (carried.ammo < mode->ammo) {", "} else if (false) {"),
        ("INV-7", "a charged count not cut to the ammo", "src/ugame/Fire.cpp",
         "if (scalesCount) count = std::min(count, carried.ammo / mode.ammo);", ""),
        ("INV-8", "a combo by the ball's centre", "src/ugame/Fire.cpp",
         "const double at = entersCylinder(from, to, shots[i].location, kind.radius, kind.radius);",
         "const double at = entersCylinder(from, to, shots[i].location, 0.001, 0.001);"),
        ("INV-8", "a combo by any margin", "src/ugame/Fire.cpp",
         "const double at = entersCylinder(from, to, shots[i].location, kind.radius, kind.radius);",
         "const double at = entersCylinder(from, to, shots[i].location, kind.radius + 1, kind.radius);"),
        ("INV-9", "a body as a sphere", "src/ugame/Fire.cpp",
         "if (qa == 0) {\n        if (qc > 0) return MISS;",
         "if (px * px + py * py + (from.z - centre.z) * (from.z - centre.z) > radius * radius + 4 * qa) return MISS;\n"
         "    if (qa == 0) {\n        if (qc > 0) return MISS;"),
        ("INV-9", "a body as a box", "src/ugame/Fire.cpp",
         "if (disc < 0) return MISS;", "if (disc < 0 && std::abs(py) > radius) return MISS;"),
        ("INV-9", "no top or bottom", "src/ugame/Fire.cpp",
         "if (from.z < centre.z - halfHeight || from.z > centre.z + halfHeight) return MISS;", ""),
        ("INV-9", "the slab ignored on a sloped shot", "src/ugame/Fire.cpp",
         "        enter = std::max(enter, a);\n        leave = std::min(leave, b);\n", ""),
        ("", "self-push dropped", "src/ugame/Game.cpp",
         "if (uworld::length(momentum) > 0) body.move = uworld::push(body.move, momentum * (1 / MASS));", ""),
    ],
    "expected_survivors": {},
}
