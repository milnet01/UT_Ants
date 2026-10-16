#!/usr/bin/env python3
"""List what graphical features each map needs -- UTA-0347. Version 1 read
the sky, for UTA-0346's sun rule and UTA-0350's painted skies; version 2 adds
`needs`, read from the map and its bundle without drawing anything.

    scripts/map-analyser.py [--no-sky] <install> <out dir> <report.json>...

--no-sky skips the views and the sky fields below, keeping `paintedSky` and
`needs`; it takes seconds a map rather than a render.

Each <report.json> is ut-bake's printed report for one map; its `map` is the
map file and its `path` the bundle. For each map this shoots ten upward views
from far-apart navigation nodes into <out dir>, measures the pixels that show
sky, and prints one JSON line:

  map         the map's name
  recipe      the recipe file the bake applied, else null. `needs` reads the
              bundle, so it counts what is left after the recipe: a light the
              recipe switches off is not in `lights`
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
  needs       what else the map holds that a system draws or must draw:
                skyZones        SkyZoneInfo actors: where the sky is drawn from
                backdrop        surfaces with PF_FakeBackdrop: windows onto it
                lights          the bundle's lights
                cylinderLights  LE_Cylinder lights: fill lights flooding an
                                area, as ForbiddenMansion's (UTA-0308)
                wideLights      lights of LightRadius 128 or more
                fogZones, waterZones
                                zones with bFogZone or bWaterZone set
                skippedLiquids  liquid surfaces the bake report skipped
                decorations     {class: actors} with Decoration in their class
                                chain -- meshes urender does not draw yet
                                (UTA-0341)
                translucent     seen BSP surfaces with PF_Translucent: glass,
                                but also beams and force fields (UTA-0272)
                glassNamed      seen BSP surfaces whose texture name holds
                                "glass", whatever their flags
              A map that fails ut-dump prints {"map", "error"} instead.

The last line holds the totals by class, and `paintedSky`, the candidates;
`mapsWith`, how many maps have each need; and the twenty commonest decoration
classes across the maps. A map whose class is clear,
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
import pathlib
import statistics
import struct
import subprocess
import sys

WIDTH, HEIGHT, FOV = 160, 90, 100.0
PF_INVISIBLE, PF_TRANSLUCENT, PF_FAKE_BACKDROP = 0x1, 0x4, 0x80
# Bundle record sizes and byte offsets: src/ubundle/ZoneSection.cpp and
# LightSection.cpp. A light's twelve UT99 bytes start at 28: type, effect, ...
ZONE_SIZE, ZONE_FOG, ZONE_WATER = 29, 3, 12
LIGHT_SIZE, LIGHT_EFFECT, LIGHT_RADIUS = 73, 29, 33
LE_CYLINDER = 17       # src/ubake/LightModel.cpp
WIDE_RADIUS = 128      # LightRadius; AActor::WorldLightRadius makes it 3225 UT units
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


def cameras(package):
    """Ten nav nodes, farthest-point sampled from the first, looking up."""
    nodes = package["nav"]["nodeList"]
    # ut-dump writes null for a node with no saved Location or a non-finite
    # one; neither can place a camera.
    points = [n["location"] for n in nodes if n["location"] is not None]
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
        if not (pathlib.Path(pfm).exists() and pathlib.Path(ppm).exists()):
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


def records(bundle, ident, size):
    """The fixed-size records of one bundle section, which holds a u32 count
    and then the records; [] where the bundle has no such section."""
    data = pathlib.Path(bundle).read_bytes()
    (count,) = struct.unpack_from("<I", data, 12)
    for i in range(count):
        name, offset, length = struct.unpack_from("<4sQQ", data, 16 + 24 * i)
        if name == ident:
            (n,) = struct.unpack_from("<I", data, offset)
            if 4 + n * size != length:
                raise ValueError(f"{bundle}: {ident} holds {length} bytes, not {n} records of {size}")
            return [data[offset + 4 + size * k:offset + 4 + size * (k + 1)] for k in range(n)]
    return []


def bright_zones(zones):
    """ZONE entries whose ambient brightness, their first byte, is 128 or more."""
    return sum(z[0] >= 128 for z in zones)


def painted_sky(surfaces, zones):
    """UTA-0350 SS 4.5: null with a real sky; else the sky-named textures."""
    if any(s["polyFlags"] & PF_FAKE_BACKDROP for s in surfaces):
        return None
    textures = {}
    for s in surfaces:
        name = s.get("texture") or ""
        if "sky" in name.lower():
            textures[name] = textures.get(name, 0) + 1
    return {"textures": dict(sorted(textures.items())), "brightZones": bright_zones(zones)}


def needs(report, package, zones, lights):
    """What the map holds beyond its sky -- see the module's `needs`."""
    surfaces = package["surfaces"]["list"]
    actors = package["wiring"]["actors"]
    decorations = {}
    for a in actors:
        if "Decoration" in a["classChain"]:
            # `class` is the map's own spelling ("lightbox"); the chain's
            # first entry is the class's ("LightBox").
            cls = a["classChain"][0]
            decorations[cls] = decorations.get(cls, 0) + 1
    seen = [s for s in surfaces if not s["polyFlags"] & PF_INVISIBLE]
    return {
        "skyZones": sum("SkyZoneInfo" in a["classChain"] for a in actors),
        "backdrop": sum(bool(s["polyFlags"] & PF_FAKE_BACKDROP) for s in surfaces),
        "lights": len(lights),
        "cylinderLights": sum(l[LIGHT_EFFECT] == LE_CYLINDER for l in lights),
        "wideLights": sum(l[LIGHT_RADIUS] >= WIDE_RADIUS for l in lights),
        "fogZones": sum(z[ZONE_FOG] for z in zones),
        "waterZones": sum(z[ZONE_WATER] for z in zones),
        "skippedLiquids": len(report.get("skippedLiquids", [])),
        "decorations": dict(sorted(decorations.items(), key=lambda kv: -kv[1])),
        "translucent": sum(bool(s["polyFlags"] & PF_TRANSLUCENT) for s in seen),
        "glassNamed": sum("glass" in (s.get("texture") or "").lower() for s in seen),
    }


def analyse(install, out, report_path, shoot):
    report = json.load(open(report_path))
    map_file, bundle = report["map"], report["path"]
    name = pathlib.Path(map_file).stem
    prefix = str(out / name)
    dump = subprocess.run(["build/tools/ut-dump/ut-dump", "--install", install, "--nav-graph",
                           "--surface-list", "--wiring-graph", map_file],
                          capture_output=True, text=True)
    package = json.loads(dump.stdout)["packages"][0] if dump.returncode == 0 else None
    cams = cameras(package) if package and shoot else []
    row = {"map": name, "recipe": report.get("recipe") and pathlib.Path(report["recipe"]).name}
    if package is None:
        row["error"] = "ut-dump failed: " + dump.stderr.strip()[-200:]
        return row
    # Views are reused only when drawn from this bake: a re-bake under the same
    # map name must not be measured from the old one's pictures (UTA-0371).
    last_view = pathlib.Path(f"{prefix}-{len(cams) - 1}.ppm")
    if cams and not (last_view.exists() and last_view.stat().st_mtime >= pathlib.Path(bundle).stat().st_mtime):
        shot = subprocess.run(["build/tools/ut-shot/ut-shot", "--light-terms", "--light-time", "0",
                               "--tier", "high", bundle, str(WIDTH), str(HEIGHT), prefix],
                              input="\n".join(cams) + "\n", capture_output=True, text=True)
        # A failed draw is an error, not a measured "no sky" (UTA-0371).
        if shot.returncode != 0:
            row["error"] = "ut-shot failed: " + shot.stderr.strip()[-200:]
            return row
    if shoot:
        sky, spots = read_views(prefix, cams)
        if sky:
            row.update(measure(sky))
            row["class"] = classify(row)
        else:
            row.update({"skyPixels": 0, "class": "none"})
        row["why"] = "measured"
        if name in VERDICTS:
            row["class"], row["why"] = VERDICTS[name]
        row["paintedSun"] = painted_sun(spots)
    zones = records(bundle, b"ZONE", ZONE_SIZE)
    row["paintedSky"] = painted_sky(package["surfaces"]["list"], zones)
    row["needs"] = needs(report, package, zones, records(bundle, b"LITE", LIGHT_SIZE))
    return row


def main(argv):
    shoot = "--no-sky" not in argv
    argv = [a for a in argv if a != "--no-sky"]
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    install, out = argv[1], pathlib.Path(argv[2])
    out.mkdir(parents=True, exist_ok=True)
    totals, painted, errors = {}, 0, 0
    maps_with = {k: 0 for k in ("skyZones", "backdrop", "cylinderLights", "wideLights", "fogZones", "waterZones",
                                "skippedLiquids", "decorations", "translucent", "glassNamed")}
    decorations = {}
    for report_path in argv[3:]:
        try:
            row = analyse(install, out, report_path, shoot)
        except ValueError as error:  # a malformed bundle section: one map, not the batch
            row = {"map": pathlib.Path(report_path).stem, "error": str(error)}
        print(json.dumps(row), flush=True)
        if "error" in row:
            errors += 1
            continue
        if shoot:
            totals[row["class"]] = totals.get(row["class"], 0) + 1
        painted += bool(row["paintedSky"] and row["paintedSky"]["textures"])
        for key in maps_with:
            maps_with[key] += bool(row["needs"][key])
        for cls, n in row["needs"]["decorations"].items():
            decorations[cls] = decorations.get(cls, 0) + n
    summary = {"maps": len(argv) - 3, "errors": errors, "paintedSky": painted, "mapsWith": maps_with,
               "decorations": dict(sorted(decorations.items(), key=lambda kv: -kv[1])[:20])}
    if shoot:
        summary.update({"totals": totals, "sun": sum(totals.get(c, 0) for c in SUN_CLASSES)})
    print(json.dumps(summary))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
