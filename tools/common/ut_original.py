"""Running the original game headless, from a copy, never writing the live install.

Shared by ut-compare (UTA-0306) and ut-record (UTA-0456).

NOTHING IN THE LIVE INSTALL IS WRITTEN. The client runs from a copy of the
install made of links, with real copies of its programs, libraries, ini files
and the directories it writes to, and with -nohomedir so it keeps its settings
and logs in the copy rather than in ~/.utpg. It runs under bwrap with the live
install mounted read-only, so a write that still finds its way back fails
rather than lands; a tool checks changed_since after its run.
"""

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

WRITTEN_DIRS = {"System64", "Logs", "Cache", "Save"}  # the client writes here
# The engine finds its base directory from where its programs and libraries
# really are, so a linked one would write logs and ini files into the live
# install. These are copied, not linked.
PROGRAMS = {"ucc-bin-amd64", "ut-bin-amd64"}
# What a package is compiled against: the stock packages, and nothing a third
# party added, which ucc make would otherwise rebuild into the copy.
STOCK_PACKAGES = ["Core", "Engine", "Editor", "UWindow", "Fire", "IpDrv", "UWeb", "UBrowser", "UnrealShare",
                  "UnrealI", "UMenu", "Botpack"]


def fail(message):
    """Print `<tool>: message` and exit 1; the tool is the running script's name."""
    print(f"{Path(sys.argv[0]).stem}: {message}", file=sys.stderr)
    sys.exit(1)


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


def compile_package(name, source, copy, system, install):
    """Build `<name>.u` from `source/Classes/*.uc` into the copy with the game's
    own compiler, when any source is newer than the package."""
    sources = sorted((source / "Classes").glob("*.uc"))
    package = system / f"{name}.u"
    if package.exists() and package.stat().st_mtime >= max(s.stat().st_mtime for s in sources):
        return
    package.unlink(missing_ok=True)
    classes = copy / name / "Classes"
    if classes.exists():
        shutil.rmtree(classes)  # a class deleted from the source must not be compiled
    classes.mkdir(parents=True)
    for s in sources:
        shutil.copy2(s, classes / s.name)
    make_ini = system / f"Make{name}.ini"
    lines = (system / "UnrealTournament.ini").read_text(errors="replace").splitlines()
    first = next(i for i, l in enumerate(lines) if l.startswith("EditPackages="))
    lines = [l for l in lines if not l.startswith("EditPackages=")]
    lines[first:first] = [f"EditPackages={p}" for p in STOCK_PACKAGES + [name]]
    make_ini.write_text("\n".join(lines) + "\n")
    result = subprocess.run(read_only(install) + ["./ucc-bin-amd64", "make", f"-ini={make_ini.name}", "-silent",
                                                  "-nohomedir"], check=False, cwd=system,
                            env=headless_env(copy), capture_output=True, text=True)
    if not package.exists():
        fail(f"ucc make did not build {package.name}:\n{result.stdout[-2000:]}{result.stderr[-2000:]}")


def changed_since(install, since):
    """Files in the install's System directories modified after `since`.

    A file stamped more than a minute in the future is left out: its stamp
    says nothing about this run, and it would fail every run."""
    return [str(p) for d in ("System", "System64") if (install / d).is_dir()
            for p in (install / d).iterdir() if p.is_file() and since < p.stat().st_mtime < time.time() + 60]


def free_display():
    for n in range(120, 200):
        if not Path(f"/tmp/.X11-unix/X{n}").exists() and not Path(f"/tmp/.X{n}-lock").exists():
            return f":{n}"
    fail("no free X display between :120 and :199")
