#!/usr/bin/env python3
"""Make the pictures for the textures a bake could not judge -- UTA-0277 SS 4.5.

    scripts/tile-questions.py <report.json> <bundle.utab> <out dir>

<report.json> is ut-bake's printed report; its tileQuestions are the textures
the bake could not tell natural from structured. For each one not already in
the player's answers file this writes, into <out dir>:

  <n>-texture.png  the texture's base level as the bundle stores it, scaled up
                   to at least 512 texels across, nearest-neighbour
  <n>-view.png     ut-shot's picture of the largest surface wearing it, from
                   out along its normal: 1.5 times its size, but never past
                   VIEW_MAX units, so a big floor is seen from inside its room

and answers.txt, one line a question with "?" where the answer goes. Show the
user both pictures -- this is asking what a thing IS, not judging a look -- then
write "shuffle" (rock, dirt, plaster: no lines or repeating features) or
"fixed" (bricks, planks, panels, trims, signs) over each "?" and append the
lines to the answers file, which the viewer reads at load. An unedited "?" line
is malformed, so appending one by mistake answers nothing.

Needs Pillow, which decodes the bundle's BC7. Run from the repository root
with the build at build/; ut-shot is build/tools/ut-shot/ut-shot.
"""

import json
import math
import os
import pathlib
import struct
import subprocess
import sys

from PIL import Image

UT_SHOT = pathlib.Path("build/tools/ut-shot/ut-shot")
VIEW_SIZE = (1280, 720)
VIEW_FOV = 90.0
VIEW_DISTANCE = 1.5  # times the surface's extent
VIEW_MAX = 192.0     # world units: further, and a floor's view leaves the level
SMALLEST_SIDE = 512
BC7 = 2  # ubundle::BlockFormat


def answers_path():
    """The answers file ubundle::tileAnswersPath names, on Linux."""
    data = os.environ.get("XDG_DATA_HOME", "")
    base = pathlib.Path(data) if data.startswith("/") else pathlib.Path.home() / ".local" / "share"
    return base / "ut-ants" / "tile-kinds.txt"


def answered():
    """Hashes the answers file already answers."""
    path = answers_path()
    if not path.exists():
        return set()
    out = set()
    for line in path.read_text(errors="replace").splitlines():
        words = line.split("#", 1)[0].split()
        if len(words) == 2 and len(words[0]) == 64 and words[1] in ("shuffle", "fixed"):
            out.add(words[0].lower())
    return out


def base_levels(bundle):
    """Each texture's base level from the bundle's TEXS section, by name: (width,
    height, BC7 block bytes). Other formats are not pictures a player names."""
    data = bundle.read_bytes()
    count = struct.unpack_from("<I", data, 12)[0]
    for i in range(count):
        at = 16 + 24 * i
        if data[at:at + 4] == b"TEXS":
            p = struct.unpack_from("<Q", data, at + 4)[0]
            break
    else:
        sys.exit(f"tile-questions: {bundle} has no TEXS section")
    textures = {}
    (n,) = struct.unpack_from("<I", data, p)
    p += 4
    for _ in range(n):
        (length,) = struct.unpack_from("<I", data, p)
        name = data[p + 4:p + 4 + length].decode()
        p += 4 + length
        fmt, width, height = data[p], *struct.unpack_from("<HH", data, p + 1)
        p += 1 + 8 + 1  # format, four sides, mip count
        (size,) = struct.unpack_from("<I", data, p)
        p += 4
        if fmt == BC7:
            level = math.ceil(width / 4) * math.ceil(height / 4) * 16
            textures[name] = (width, height, data[p:p + level])
        p += size
    return textures


def camera_line(view):
    """ut-shot's camera: on the surface's normal, looking back at its centre."""
    at, normal, extent = view["at"], view["normal"], view["extent"]
    distance = min(VIEW_DISTANCE * extent, VIEW_MAX)
    eye = [a + n * distance for a, n in zip(at, normal)]
    look = [-n for n in normal]
    yaw = math.atan2(look[1], look[0])
    pitch = math.atan2(look[2], math.hypot(look[0], look[1]))
    turn = 65536 / (2 * math.pi)
    return f"{eye[0]} {eye[1]} {eye[2]} {round(pitch * turn)} {round(yaw * turn) % 65536} 0 {VIEW_FOV}\n"


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip().splitlines()[2].strip())
    report, bundle, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    questions = json.loads(report.read_text()).get("tileQuestions", [])
    done = answered()
    asked = [q for q in questions if q["hash"] not in done]
    print(f"{len(questions)} question(s) in the report, {len(questions) - len(asked)} already answered")
    if not asked:
        return
    out.mkdir(parents=True, exist_ok=True)
    pictures = base_levels(bundle)

    cameras = "".join(camera_line(q["view"]) for q in asked)
    shot = subprocess.run([str(UT_SHOT), "--light-time", "0", str(bundle), str(VIEW_SIZE[0]), str(VIEW_SIZE[1]),
                           str(out / "view")], input=cameras, text=True, capture_output=True)
    lines = []
    for n, q in enumerate(asked):
        width, height, blocks = pictures.get(q["material"] + ":base", (0, 0, b""))
        if blocks:
            picture = Image.frombytes("RGBA", (math.ceil(width / 4) * 4, math.ceil(height / 4) * 4), blocks, "bcn", 7)
            picture = picture.crop((0, 0, width, height)).convert("RGB")
            scale = max(1, math.ceil(SMALLEST_SIDE / min(width, height)))
            picture.resize((width * scale, height * scale), Image.NEAREST).save(out / f"{n}-texture.png")
        else:
            print(f"{n}: {q['material']} has no BC7 base level in the bundle; no texture picture")
        view = out / f"view-{n}.ppm"
        if shot.returncode == 0 and view.exists():
            Image.open(view).save(out / f"{n}-view.png")
            view.unlink()
        else:
            print(f"{n}: ut-shot could not draw the view; show the texture picture alone")
        lines.append(f"{q['hash']} ?  # {n}: {q['material']} on {q['surfaces']} surface(s)\n")
    if shot.returncode != 0:
        print("ut-shot:", shot.stderr.strip().splitlines()[-1] if shot.stderr.strip() else f"exit {shot.returncode}")
    (out / "answers.txt").write_text("".join(lines))
    print(f"wrote {len(asked)} question(s) to {out}; the answers file is {answers_path()}")


if __name__ == "__main__":
    main()
