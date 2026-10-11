#!/usr/bin/env python3
"""List what graphical features each map needs -- UTA-0347. Version 1 reads
the sky only, for UTA-0346's sun rule and UTA-0350's painted skies.

    scripts/map-analyser.py <install> <out dir> <report.json>...

Each <report.json> is ut-bake's printed report for one map; its `map` is the
map file and its `path` the bundle. For each map this shoots ten upward views
from far-apart navigation nodes into <out dir>, measures the pixels that show
sky, and prints one JSON line:

  map         the map's name
  skyPixels   how many sky pixels the ten views hold
  luma        their mean brightness, 0-255
  blue, purple, cloud, warm
              the share of sky pixels that are blue, purple, white or grey
              (cloud), and red-orange (warm)
  class       none, night, clear, fewClouds, warmClear or other
  why         "measured", or the reason a checked verdict overrides it
  paintedSun  [yaw, pitch] in UT units where the sky paints a sun -- a small
              bright spot seen at one direction from two views -- else null
  paintedSky  null where the map has a real sky (a PF_FakeBackdrop surface);
              else {"textures": {name: surfaces}, "brightZones": n} -- the
              textures whose name holds "sky", each with how many surfaces
              wear it, and how many ZONE entries in the bundle have an
              ambient brightness of 128 or more (UTA-0350 SS 4.5). A
              non-empty texture list marks a candidate for a [sky] recipe.

The last line holds the totals by class, and `paintedSky`, the candidates. A map whose class is clear,
fewClouds or warmClear gets a sun (UTA-0346; warmClear added by the user,
2026-10-11). `other` is any sky the rules do not name: overcast, purple,
orange, alien.

The measured class is a guide. Brightness and colour alone mistake some skies:
an overcast or moonlit sky can be as blue as a clear one. VERDICTS holds the
maps a session checked by eye, and wins.

Needs ut-dump and ut-shot under build/. Run from the repository root.
"""

import json
import math
import os
import pathlib
import statistics
import struct
import subprocess
import sys

WIDTH, HEIGHT, FOV = 160, 90, 100.0
PF_FAKE_BACKDROP = 0x80
UP_PITCH = 14000  # UT units; nearly straight up, as UTA-0334's survey shot
SUN_CLASSES = ("clear", "fewClouds", "warmClear")

# Maps checked by eye against wider views where the measured class is wrong:
# map -> (class, why). UTA-0346, 2026-10-11.
VERDICTS = {
    "DM-Siberia][": ("fewClouds", "a deep day blue with clouds and a painted sun; darker than the bar"),
    "DM-Traction": ("other", "its only sky is small skylights of pinkish cloud"),
    "AS-Frigate": ("other", "a pale, near-white haze: overcast, though faintly blue"),
    "MH-()mG-TheBoat-V2mini-BP": ("clear", "clear light blue in the user's capture (UTA-0344); the views stand below deck"),
}


def cameras(nav_json):
    """Ten nav nodes, farthest-point sampled from the first, looking up."""
    nodes = json.loads(nav_json)["packages"][0]["nav"]["nodeList"]
    points = [n["location"] for n in nodes]
    if not points:
        return []
    d2 = lambda a, b: sum((x - y) ** 2 for x, y in zip(a, b))
    chosen = [0]
    while len(chosen) < min(10, len(points)):
        chosen.append(max(range(len(points)),
                          key=lambda i: min(d2(points[i], points[c]) for c in chosen)))
    lines = []
    for i in chosen:
        p = points[i]
        far = max(points, key=lambda q: d2(p, q))
        yaw = round(math.atan2(far[1] - p[1], far[0] - p[0]) * 32768 / math.pi) % 65536
        lines.append(f"{p[0]:.1f} {p[1]:.1f} {p[2] + 20:.1f} {UP_PITCH} {yaw} 0 {FOV:g}")
    return lines


def direction(px, py, pitch, yaw):
    """The unit vector through pixel (px, py) of a view at pitch and yaw."""
    th = math.tan(math.radians(FOV / 2))
    tv = th * HEIGHT / WIDTH
    x, y, z = 1.0, ((px + .5) / WIDTH * 2 - 1) * th, (1 - (py + .5) / HEIGHT * 2) * tv
    p, w = pitch * math.pi / 32768, yaw * math.pi / 32768
    x, z = x * math.cos(p) - z * math.sin(p), x * math.sin(p) + z * math.cos(p)
    x, y = x * math.cos(w) - y * math.sin(w), x * math.sin(w) + y * math.cos(w)
    length = math.sqrt(x * x + y * y + z * z)
    return x / length, y / length, z / length


def read_views(prefix, cams):
    """Sky pixels over every view, and the direction of each view's bright spot.
    A pixel is sky where ut-shot's light terms hold no light, direct or
    indirect, and it is not black."""
    sky, spots = [], []
    for v, cam in enumerate(cams):
        pfm, ppm = f"{prefix}-{v}-light.pfm", f"{prefix}-{v}.ppm"
        if not (os.path.exists(pfm) and os.path.exists(ppm)):
            continue
        light = struct.unpack(f"<{WIDTH * HEIGHT * 3}f",
                              open(pfm, "rb").read().split(b"\n", 3)[3][:WIDTH * HEIGHT * 12])
        image = open(ppm, "rb").read().split(b"\n", 3)[3]
        hot = []
        for y in range(HEIGHT):
            for x in range(WIDTH):
                i = (HEIGHT - 1 - y) * WIDTH + x  # the PFM's rows run bottom up
                if light[3 * i] == 0 and light[3 * i + 1] == 0:
                    r, g, b = image[3 * (y * WIDTH + x):3 * (y * WIDTH + x) + 3]
                    if r + g + b > 30:
                        sky.append((r, g, b))
                        if min(r, g, b) > 235:
                            hot.append((x, y))
        if hot and len(hot) < 0.02 * WIDTH * HEIGHT:  # a small spot, not a white sky
            cx = sum(h[0] for h in hot) / len(hot)
            cy = sum(h[1] for h in hot) / len(hot)
            fields = cam.split()
            spots.append(direction(cx, cy, int(fields[3]), int(fields[4])))
    return sky, spots


def painted_sun(spots):
    """Two spots within ten degrees of each other are one painted sun."""
    for spot in spots:
        near = [s for s in spots if sum(a * b for a, b in zip(s, spot)) > math.cos(math.radians(10))]
        if len(near) >= 2:
            d = [sum(s[k] for s in near) / len(near) for k in range(3)]
            length = math.sqrt(sum(c * c for c in d))
            return [round(math.atan2(d[1], d[0]) * 32768 / math.pi) % 65536,
                    round(math.asin(d[2] / length) * 32768 / math.pi)]
    return None


def measure(sky):
    n = len(sky)
    share = lambda test: round(sum(1 for p in sky if test(*p)) / n, 2)
    return {
        "skyPixels": n,
        "luma": round(statistics.mean(.2126 * r + .7152 * g + .0722 * b for r, g, b in sky)),
        "blue": share(lambda r, g, b: b > r + 10 and b >= g - 5 and r <= g + 15),
        "purple": share(lambda r, g, b: r > g + 20 and b > g + 20),
        "cloud": share(lambda r, g, b: max(r, g, b) - min(r, g, b) < 40 and max(r, g, b) > 150),
        "warm": share(lambda r, g, b: r > b + 20 and r > g + 10),
    }


def classify(m):
    """Thresholds read off UTA-0346's eye-checked maps, 2026-10-11."""
    if m["skyPixels"] < 2000:
        return "none"
    if m["luma"] < 90:
        return "night"
    if m["blue"] >= 0.9 and m["cloud"] < 0.1 and m["luma"] >= 140:
        return "clear"
    if m["blue"] >= 0.5 and m["luma"] >= 140:
        return "fewClouds"
    if m["warm"] >= 0.9 and m["cloud"] < 0.3 and m["luma"] >= 180:
        return "warmClear"
    return "other"


def bright_zones(bundle):
    """ZONE entries whose ambient brightness is 128 or more. A ZONE entry is
    29 bytes, brightness first, after a u32 count (UTA-0156 SS 4.1)."""
    data = pathlib.Path(bundle).read_bytes()
    (count,) = struct.unpack_from("<I", data, 12)
    for i in range(count):
        ident, offset, _size = struct.unpack_from("<4sQQ", data, 16 + 24 * i)
        if ident == b"ZONE":
            (zones,) = struct.unpack_from("<I", data, offset)
            return sum(data[offset + 4 + 29 * z] >= 128 for z in range(zones))
    return 0


def painted_sky(install, map_file, bundle):
    """UTA-0350 SS 4.5: null with a real sky; else the sky-named textures."""
    dump = subprocess.run(["build/tools/ut-dump/ut-dump", "--install", install, "--surface-list", map_file],
                          capture_output=True, text=True, check=True)
    surfaces = json.loads(dump.stdout)["packages"][0]["surfaces"]["list"]
    if any(s["polyFlags"] & PF_FAKE_BACKDROP for s in surfaces):
        return None
    textures = {}
    for s in surfaces:
        name = s.get("texture") or ""
        if "sky" in name.lower():
            textures[name] = textures.get(name, 0) + 1
    return {"textures": dict(sorted(textures.items())), "brightZones": bright_zones(bundle)}


def analyse(install, out, report_path):
    report = json.load(open(report_path))
    map_file, bundle = report["map"], report["path"]
    name = pathlib.Path(map_file).stem
    prefix = str(out / name)
    nav = subprocess.run(["build/tools/ut-dump/ut-dump", "--install", install, "--nav-graph", map_file],
                         capture_output=True, text=True)
    cams = cameras(nav.stdout) if nav.returncode == 0 else []
    if cams and not os.path.exists(f"{prefix}-{len(cams) - 1}.ppm"):
        subprocess.run(["build/tools/ut-shot/ut-shot", "--light-terms", "--light-time", "0",
                        "--tier", "high", bundle, str(WIDTH), str(HEIGHT), prefix],
                       input="\n".join(cams) + "\n", capture_output=True, text=True)
    sky, spots = read_views(prefix, cams)
    row = {"map": name}
    if sky:
        row.update(measure(sky))
        row["class"] = classify(row)
    else:
        row.update({"skyPixels": 0, "class": "none"})
    row["why"] = "measured"
    if name in VERDICTS:
        row["class"], row["why"] = VERDICTS[name]
    row["paintedSun"] = painted_sun(spots)
    row["paintedSky"] = painted_sky(install, map_file, bundle)
    return row


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    install, out = argv[1], pathlib.Path(argv[2])
    out.mkdir(parents=True, exist_ok=True)
    totals = {}
    painted = 0
    for report_path in argv[3:]:
        row = analyse(install, out, report_path)
        totals[row["class"]] = totals.get(row["class"], 0) + 1
        painted += bool(row["paintedSky"] and row["paintedSky"]["textures"])
        print(json.dumps(row), flush=True)
    print(json.dumps({"totals": totals, "sun": sum(totals.get(c, 0) for c in SUN_CLASSES), "paintedSky": painted}))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
