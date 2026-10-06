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

NOTHING OUTSIDE --work AND --out IS WRITTEN. The client runs from a copy of
the install made of links, with real copies of its programs, libraries, ini
files and the directories it writes to, and with -nohomedir so it keeps its
settings and logs in the copy rather than in ~/.utpg. It runs under bwrap
with the live install mounted read-only, so a write that still finds its way
back fails rather than lands, and the run fails if anything there changed.

Differences that are the comparison's, not the renderer's: the original's
lights pulse on its own clock while ours are pinned at time 0, and the
original draws its own sky and fog from its settings.
"""

import argparse
import datetime
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
MUTATOR = "UTACam"
WRITTEN_DIRS = {"System64", "Logs", "Cache", "Save"}  # the client writes here
# The engine finds its base directory from where its programs and libraries
# really are, so a linked one would write logs and ini files into the live
# install. These are copied, not linked.
PROGRAMS = {"ucc-bin-amd64", "ut-bin-amd64"}
# What the mutator is compiled against: the stock packages, and nothing a
# third party added, which ucc make would otherwise rebuild into the copy.
STOCK_PACKAGES = ["Core", "Engine", "Editor", "UWindow", "Fire", "IpDrv", "UWeb", "UBrowser", "UnrealShare",
                  "UnrealI", "UMenu", "Botpack"]


def fail(message):
    print(f"ut-compare: {message}", file=sys.stderr)
    sys.exit(1)


def read_capture(folder):
    details = {}
    for line in (folder / "details.txt").read_text().splitlines():
        key, _, value = line.partition(":")
        details[key.strip()] = value.strip()
    cameras = [line for line in (folder / "camera.txt").read_text().splitlines() if line.strip()]
    return details, cameras


def prepare_copy(install, copy):
    """A copy of `install` made of links, except what the client writes."""
    copy.mkdir(parents=True, exist_ok=True)
    for entry in install.iterdir():
        target = copy / entry.name
        if entry.name in WRITTEN_DIRS:
            if target.is_symlink():
                target.unlink()
            target.mkdir(exist_ok=True)
            continue
        if target.is_symlink() or not target.exists():
            if target.is_symlink():
                target.unlink()
            target.symlink_to(entry)
    system = copy / "System64"
    for entry in (install / "System64").iterdir():
        target = system / entry.name
        if entry.suffix.lower() == ".log":
            if target.is_symlink():
                target.unlink()  # the client writes its logs here
        elif entry.suffix.lower() in (".ini", ".so") or entry.name in PROGRAMS:
            if not target.exists() or target.is_symlink():
                if target.is_symlink():
                    target.unlink()
                shutil.copy2(entry, target)  # a real copy: see PROGRAMS, and the client rewrites its ini
        elif not target.exists() and not target.is_symlink():
            target.symlink_to(entry)
    (copy / "home").mkdir(exist_ok=True)
    return system


def set_ini(path, section, values):
    """Set `values` in `section` of a UT ini, adding keys that are missing."""
    lines = path.read_text(errors="replace").splitlines()
    out, inside, done = [], False, set()
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("["):
            if inside:
                out.extend(f"{k}={v}" for k, v in values.items() if k not in done)
            inside = stripped == f"[{section}]"
        elif inside and "=" in stripped and not stripped.startswith(";"):
            key = stripped.split("=", 1)[0]
            if key in values:
                line = f"{key}={values[key]}"
                done.add(key)
        out.append(line)
    if inside:
        out.extend(f"{k}={v}" for k, v in values.items() if k not in done)
    elif f"[{section}]" not in (l.strip() for l in lines):
        out += ["", f"[{section}]"] + [f"{k}={v}" for k, v in values.items()]
    path.write_text("\n".join(out) + "\n")


def headless_env(copy, display=None):
    """The environment the game runs in: never the user's desktop.

    SDL prefers Wayland whenever WAYLAND_DISPLAY is set, whatever DISPLAY
    says, so without this the client opens a window on the user's screen.
    """
    env = {k: v for k, v in os.environ.items() if k != "WAYLAND_DISPLAY"}
    env.update(HOME=str(copy / "home"), SDL_VIDEODRIVER="x11")
    if display:
        env["DISPLAY"] = display
    else:
        env.pop("DISPLAY", None)
    return env


def read_only(install):
    """A prefix that runs a program with the live install mounted read-only.

    The engine finds its way back to the live install by more than one route
    (its programs, its libraries, its ini saves), so the copy alone did not
    keep its writes out. Under this, such a write fails instead of landing.
    """
    prefix = ["bwrap", "--dev-bind", "/", "/", "--ro-bind", str(install), str(install)]
    # And no route to the user's desktop: the Wayland socket reads as empty.
    runtime, wayland = os.environ.get("XDG_RUNTIME_DIR"), os.environ.get("WAYLAND_DISPLAY")
    if runtime and wayland and Path(runtime, wayland).exists():
        prefix += ["--ro-bind", "/dev/null", str(Path(runtime, wayland))]
    return prefix + ["--"]


def compile_mutator(copy, system, install):
    """Build UTACam.u into the copy with the game's own compiler, when stale."""
    source = HERE / MUTATOR / "Classes" / f"{MUTATOR}.uc"
    package = system / f"{MUTATOR}.u"
    if package.exists() and package.stat().st_mtime >= source.stat().st_mtime:
        return
    package.unlink(missing_ok=True)
    classes = copy / MUTATOR / "Classes"
    classes.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, classes / source.name)
    make_ini = system / "UTCompareMake.ini"
    lines = (system / "UnrealTournament.ini").read_text(errors="replace").splitlines()
    first = next(i for i, l in enumerate(lines) if l.startswith("EditPackages="))
    lines = [l for l in lines if not l.startswith("EditPackages=")]
    lines[first:first] = [f"EditPackages={name}" for name in STOCK_PACKAGES + [MUTATOR]]
    make_ini.write_text("\n".join(lines) + "\n")
    result = subprocess.run(read_only(install) + ["./ucc-bin-amd64", "make", f"-ini={make_ini.name}", "-silent", "-nohomedir"], cwd=system,
                            env=headless_env(copy), capture_output=True, text=True)
    if not package.exists():
        fail(f"ucc make did not build {package.name}:\n{result.stdout[-2000:]}{result.stderr[-2000:]}")


def changed_since(install, since):
    """Files in the install's System directories modified after `since`."""
    return [str(p) for d in ("System", "System64") if (install / d).is_dir()
            for p in (install / d).iterdir() if p.is_file() and since < p.stat().st_mtime < time.time() + 60]


def free_display():
    for n in range(120, 200):
        if not Path(f"/tmp/.X11-unix/X{n}").exists() and not Path(f"/tmp/.X{n}-lock").exists():
            return f":{n}"
    fail("no free X display between :120 and :199")


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
        url = f"{map_name}.unr?Game=Botpack.DeathMatchPlus?Mutator={MUTATOR}.{MUTATOR}?SpectatorOnly=1"
        client = subprocess.Popen(
            read_only(install) + ["./ut-bin-amd64", url, "-ini=UnrealTournament.ini", "-userini=User.ini", "-nohomedir", "-nosound",
             f"-log={log}", "-forcelogflush"], cwd=system,
            env=headless_env(copy, display),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
    for line in result.stdout.splitlines():
        if '"path":' in line:
            return line.split('"path": "', 1)[1].split('"', 1)[0]
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
    if not 1 <= len(cameras) <= 64:
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
    compile_mutator(args.work.resolve(), system, install)
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
