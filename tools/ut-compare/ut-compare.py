#!/usr/bin/env python3
"""ut-compare: the original game's frame beside ours, from the same camera.

UTA-0306. For each camera, runs Unreal Tournament's own client headless and
silent, takes its frame with the engine's screenshot, draws ours with ut-shot
at the same camera and size, and writes the two side by side with the camera
and the settings under them.

    ut-compare.py --install <UT dir> --capture <viewer capture folder> [options]
    ut-compare.py --install <UT dir> --map <name> --cameras <file> [options]

A camera is one line as ut-shot reads it: x y z pitch yaw roll fov (UT99
units and angles, fov horizontal in degrees). A viewer capture folder
(F12) is named directly: its map, camera, size and tier are used.

Options:
  --size WxH          frame size (default 1920x1080, or the capture's)
  --tier NAME         our tier: low, medium, high, ultra (default ultra)
  --no-volumetric     draw the original without its volumetric lighting
  --fov DEGREES       replace every camera's fov
  --out DIR           where the frames go (default ~/.cache/ut-ants/compare/<map>-<time>)
  --work DIR          where the install copy lives (default ~/.cache/ut-ants/compare/install)
  --build DIR         this project's build tree (default ./build)

NOTHING IN THE LIVE INSTALL IS WRITTEN. The tool writes to --work and --out,
and its bake goes to the bake cache in ~/.cache/ut-ants as any bake's does;
Xvfb takes its display lock in /tmp. The client runs from a copy of
the install made of links, with real copies of its programs, libraries, ini
files and the directories it writes to, and with -nohomedir so it keeps its
settings and logs in the copy rather than in ~/.utpg. It runs under bwrap
with the live install mounted read-only, so a write that still finds its way
back fails rather than lands, and the run fails if a file in its System
directories changed.

Differences that are the comparison's, not the renderer's: the original's
lights pulse on its own clock while ours are pinned at time 0, and the
original draws its own sky and fog from its settings.
"""

import argparse
import datetime
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "common"))
from ut_original import (
    changed_since,
    compile_package,
    fail,
    free_display,
    headless_env,
    prepare_copy,
    read_only,
    set_ini,
)

MUTATOR = "UTACam"


def read_capture(folder):
    details = {}
    for line in (folder / "details.txt").read_text().splitlines():
        key, _, value = line.partition(":")
        details[key.strip()] = value.strip()
    cameras = [line for line in (folder / "camera.txt").read_text().splitlines() if line.strip()]
    return details, cameras


def capture_original(copy, system, install, map_name, cameras, width, height, volumetric, out):
    set_ini(system / "UnrealTournament.ini", "Engine.Engine", {"GameRenderDevice": "OpenGLDrv.OpenGLRenderDevice"})
    set_ini(system / "UnrealTournament.ini", "SDLDrv.SDLClient",
            {"WindowedViewportX": width, "WindowedViewportY": height, "FullscreenViewportX": width,
             "FullscreenViewportY": height, "StartupFullscreen": "False"})
    set_ini(system / "UnrealTournament.ini", "OpenGLDrv.OpenGLRenderDevice",
            {"VolumetricLighting": str(volumetric), "GammaCorrectScreenshots": "True"})
    (system / f"{MUTATOR}.ini").write_text(
        f"[{MUTATOR}.{MUTATOR}]\n" + "".join(f"Cameras[{i}]={c}\n" for i, c in enumerate(cameras)))

    log = out / "original.log"
    stamp = time.time()
    time.sleep(1)  # so a shot's mtime is after the stamp
    display = free_display()
    xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", f"{width}x{height}x24", "+extension", "GLX"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    client = None
    try:
        time.sleep(2)
        # Another program may have taken the display after free_display chose
        # it; say so rather than report 0 frames from the original (UTA-0372).
        if xvfb.poll() is not None:
            fail(f"Xvfb could not start on {display} (exit {xvfb.returncode})")
        url = f"{map_name}.unr?Game=Botpack.DeathMatchPlus?Mutator={MUTATOR}.{MUTATOR}?SpectatorOnly=1"
        client = subprocess.Popen(
            read_only(install) + ["./ut-bin-amd64", url, "-ini=UnrealTournament.ini", "-userini=User.ini", "-nohomedir", "-nosound",
             f"-log={log}", "-forcelogflush"], cwd=system,
            env=headless_env(copy, display),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # 90 s for the client to start and load the map, then 10 a camera.
        # UTACam's defaultproperties spend StartSeconds (6) once, then
        # HoldSeconds (4) and ShotSeconds (2) a camera; lengthen these with them.
        deadline = time.time() + 90 + 10 * len(cameras)
        while time.time() < deadline and client.poll() is None:
            time.sleep(1)
            if log.exists() and "UTACAM DONE" in log.read_text(errors="replace"):
                time.sleep(3)
                break
    finally:
        for process in (client, xvfb):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(5)
                except subprocess.TimeoutExpired:
                    process.kill()

    text = log.read_text(errors="replace") if log.exists() else ""
    poses = [l[l.index("UTACAM"):] for l in text.splitlines() if "UTACAM POSE" in l]
    shots = sorted((p for root in (copy / "home", system) for p in root.rglob("*.png")
                    if p.stat().st_mtime > stamp), key=lambda p: p.stat().st_mtime)
    if len(shots) != len(cameras):
        fail(f"the original gave {len(shots)} frames for {len(cameras)} cameras; see {log}")
    for i, shot in enumerate(shots):
        shutil.move(str(shot), out / f"original-{i}.png")
    return poses


def bake(build, install, map_name):
    result = subprocess.run([str(build / "tools/ut-bake/ut-bake"), "--install", str(install), "--out",
                             str(Path.home() / ".cache/ut-ants/content/bakes"), "--texture-cache",
                             str(Path.home() / ".cache/ut-ants/textures"), str(install / "Maps" / f"{map_name}.unr")],
                            capture_output=True, text=True)
    # Parsed, not split: the path is a JSON string, so a quote or a backslash
    # in it arrives escaped (UTA-0410).
    try:
        return json.loads(result.stdout)["path"]
    except (ValueError, KeyError, TypeError):
        fail(f"ut-bake gave no bundle:\n{result.stdout[-2000:]}{result.stderr[-2000:]}")


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--map")
    parser.add_argument("--cameras", type=Path)
    parser.add_argument("--size")
    parser.add_argument("--tier")
    parser.add_argument("--no-volumetric", action="store_true")
    parser.add_argument("--fov", type=float)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--work", type=Path, default=Path.home() / ".cache/ut-ants/compare/install")
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--help", action="store_true")
    args = parser.parse_args()
    if args.help:
        print(__doc__)
        return

    size, tier = args.size, args.tier
    if args.capture:
        details, cameras = read_capture(args.capture)
        map_name = details["map"]
        size = size or details.get("render-size")
        tier = tier or details.get("tier")
    elif args.map and args.cameras:
        map_name = args.map
        cameras = [l for l in args.cameras.read_text().splitlines() if l.strip()]
    else:
        fail("name --capture, or --map with --cameras")
    if not 1 <= len(cameras) <= 64:  # UTACam's Cameras[64]; change both together
        fail(f"{len(cameras)} cameras; 1 to 64 are taken")
    if args.fov:
        cameras = [" ".join(c.split()[:6] + [str(args.fov)]) for c in cameras]
    width, height = (size or "1920x1080").split("x")
    tier = tier or "ultra"
    install = args.install.resolve()
    if not (install / "Maps" / f"{map_name}.unr").exists():
        fail(f"no map {map_name}.unr in {install / 'Maps'}")
    for tool in ("Xvfb", "magick", "bwrap"):
        if not shutil.which(tool):
            fail(f"{tool} is not installed")

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = (args.out or Path.home() / ".cache/ut-ants/compare" / f"{map_name}-{stamp}").resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / "camera.txt").write_text("\n".join(cameras) + "\n")

    started = time.time()
    system = prepare_copy(install, args.work.resolve())
    compile_package(MUTATOR, HERE / MUTATOR, args.work.resolve(), system, install)
    poses = capture_original(args.work.resolve(), system, install, map_name, cameras, width, height,
                             not args.no_volumetric, out)

    touched = changed_since(install, started)
    if touched:
        fail("the live install changed during the run, which must never happen: " + ", ".join(touched))

    bundle = bake(args.build, install, map_name)
    subprocess.run([str(args.build / "tools/ut-shot/ut-shot"), "--tier", tier, "--light-time", "0", bundle,
                    width, height, str(out / "ours")], input="\n".join(cameras) + "\n", text=True, check=True,
                   stdout=subprocess.DEVNULL)

    settings = (f"map {map_name}  size {width}x{height}  our tier {tier}  "
                f"original volumetric {'on' if not args.no_volumetric else 'off'}")
    (out / "settings.txt").write_text(settings + "\n" + "\n".join(poses) + "\n")
    point = max(16, int(height) // 30)
    for i, camera in enumerate(cameras):
        subprocess.run(["magick", str(out / f"original-{i}.png"), str(out / f"ours-{i}.ppm"),
                        "-resize", f"{width}x{height}!", "+append", "-background", "black", "-fill", "white",
                        "-pointsize", str(point), f"label:original (left)   ours (right)\ncamera {camera}\n{settings}",
                        "-append", str(out / f"pair-{i}.png")], check=True)
    print(f"ut-compare: {len(cameras)} pairs in {out}")


if __name__ == "__main__":
    main()
