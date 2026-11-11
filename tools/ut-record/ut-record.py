#!/usr/bin/env python3
"""ut-record: the original game's player movement, tick by tick.

UTA-0456 (docs/specs/UTA-0456-movement-recorder.md). Runs Unreal Tournament
469 headless and plays scripted runs: a start pose and the keys held over
time. Every tick of every run is logged, at several frame rates, and written
one file a run and rate.

    ut-record.py --install <UT dir> [options]
    ut-record.py --install <UT dir> --calibrate
    ut-record.py --install <UT dir> --starts <map>

Options:
  --runs FILE        the scripted runs (default runs.txt beside this tool)
  --only NAME,...    record only these runs
  --rates R,...      frame-rate caps (default 30,60,85,120,200)
  --out DIR          where recordings go (default ~/.cache/ut-ants/movement/<time>);
                     refused inside this repository
  --work DIR         the install copy (default ~/.cache/ut-ants/record/install)
  --calibrate        hold real keys with xdotool and report K, the value a held
                     key leaves in its axis (spec section 4.6)
  --starts MAP       print the map's player starts, to choose poses from

A run in the runs file:

    run <name> <map> <x> <y> <z> <yaw>
    <seconds> <keys>
    ...
    end <seconds>

keys is a string of F B L R J D W (forward, back, strafe left and right, jump,
duck, walk), or - for none. A line holds from its time until the next.

A weapon run (UTA-0019 section 4.7; weapons.txt) adds, before its first key line:

    aim <pitch>          the view's pitch, 65536 to a turn, up positive
    weapon <class>       in hand before the run starts, with full ammo
    switch <class>       carried too; the key N raises it
    target <distance>    a stand-in pawn this far ahead along the yaw

and the keys P (fire), A (alt fire) and N (switch weapon).

Each launch pins the game as shipped, not this install's settings: dodging on
(DodgeClickTime 0.25), hardcore (TimeDilation 1.1), AirControl 0.35, no bots.
A recording that shows otherwise fails the run.

NOTHING IN THE LIVE INSTALL IS WRITTEN: see tools/common/ut_original.py.
"""

import argparse
import datetime
import itertools
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
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

PACKAGE = "UTARec"
MAX_RUNS = 64   # UTARec's Runs[64]; change both. More runs on a map take more launches.
MAX_STEPS = 32  # UTARec's MaxSteps; change both
KEYS = set("FBLRJDWPAN")
DIRECTIVES = {"aim", "weapon", "switch", "target"}  # a weapon run's, before its keys
TIME_DILATION = 1.1  # hardcore mode (DeathMatchPlus.SetGameSpeed)
# The game as shipped (spec section 4.4, INV-2): what each recording's PAWN line must show.
SHIPPED = {"DodgeClickTime": 0.25, "AirControl": 0.35, "TimeDilation": TIME_DILATION, "Bots": 0}
SKIP_SECONDS = 5  # UTARec's SkipSeconds
SIZE = ("640", "480")


class Run:
    def __init__(self, name, map_name, pose):
        self.name, self.map, self.pose = name, map_name, pose
        self.steps = []  # (seconds, keys)
        self.end = None
        self.directives = {}  # aim, weapon, switch, target: a weapon run's

    def keys_at(self, t):
        keys = "-"
        for start, k in self.steps:
            if start <= t:
                keys = k
        return keys

    def line(self):
        """The run as UTARec.ini holds it."""
        return ";".join([f"{self.name} {' '.join(self.pose)}"] + [f"{k} {v}" for k, v in self.directives.items()]
                        + [f"{t} {k}" for t, k in self.steps] + [f"end {self.end}"])


def read_runs(path):
    runs, run = [], None
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        fields = raw.split("#", 1)[0].split()
        if not fields:
            continue
        where = f"{path}:{number}"
        if fields[0] == "run":
            if run:
                fail(f"{where}: run {run.name} has no end")
            if len(fields) != 7:
                fail(f"{where}: a run line is: run <name> <map> <x> <y> <z> <yaw>")
            for value in fields[3:]:
                float(value)
            run = Run(fields[1], fields[2], fields[3:])
        elif run is None:
            fail(f"{where}: a line outside a run")
        elif fields[0] in DIRECTIVES:
            if run.steps:
                fail(f"{where}: {fields[0]} comes before run {run.name}'s first key line")
            if len(fields) != 2 or fields[0] in run.directives:
                fail(f"{where}: one {fields[0]} <value> a run")
            if fields[0] in ("aim", "target"):
                float(fields[1])
            run.directives[fields[0]] = fields[1]
        elif fields[0] == "end":
            run.end = float(fields[1])
            if not run.steps or run.steps[0][0] != 0:
                fail(f"{where}: run {run.name} must start at 0")
            if run.end <= run.steps[-1][0]:
                fail(f"{where}: run {run.name} ends before its last line")
            runs.append(run)
            run = None
        else:
            t, keys = float(fields[0]), fields[1] if len(fields) > 1 else "-"
            if keys != "-" and not set(keys) <= KEYS:
                fail(f"{where}: keys are F B L R J D W P A N, or -")
            if run.steps and t <= run.steps[-1][0]:
                fail(f"{where}: times must increase")
            run.steps.append((t, keys))
            if len(run.steps) > MAX_STEPS:
                fail(f"{where}: run {run.name} has more than {MAX_STEPS} lines")
    if run:
        fail(f"{path}: run {run.name} has no end")
    if len({r.name for r in runs}) != len(runs):
        fail(f"{path}: two runs share a name")
    return runs


def check_intervals(runs, rates):
    """INV-8: a key interval shorter than two ticks at a rate could fall between ticks."""
    for run in runs:
        times = [t for t, _ in run.steps] + [run.end]
        shortest = min(b - a for a, b in itertools.pairwise(times))
        for rate in rates:
            if shortest < 2 * TIME_DILATION / rate:
                fail(f"run {run.name}: a key interval of {shortest} s is under two ticks at {rate} fps "
                     f"({2 * TIME_DILATION / rate:.4f} s of game time)")


def pin_settings(system, rate):
    """The game as shipped (spec section 4.4), at `rate`, drawn small, with no
    game controller: this machine's controller reaches the client and is bound
    to the strafe and forward axes, so it moves the player unasked."""
    ini, user = system / "UnrealTournament.ini", system / "User.ini"
    set_ini(user, "Engine.PlayerPawn", {"DodgeClickTime": "0.25"})
    joy = [line.split("=", 1)[0] for line in user.read_text(errors="replace").splitlines() if line.startswith("Joy")]
    set_ini(user, "Engine.Input", dict.fromkeys(joy, ""))
    set_ini(ini, "SDLDrv.SDLClient", {"UseJoystick": "False"})
    set_ini(ini, "Botpack.DeathMatchPlus", {"bHardCoreMode": "True", "AirControl": "0.35", "MinPlayers": "0",
                                            "InitialBots": "0"})
    set_ini(ini, "Engine.GameInfo", {"GameSpeed": "1.0"})
    set_ini(ini, "Engine.Engine", {"GameRenderDevice": "OpenGLDrv.OpenGLRenderDevice"})
    set_ini(ini, "SDLDrv.SDLClient",
            {"FrameRateLimit": str(rate), "WindowedViewportX": SIZE[0], "WindowedViewportY": SIZE[1],
             "FullscreenViewportX": SIZE[0], "FullscreenViewportY": SIZE[1], "StartupFullscreen": "False"})
    # The render device has a cap of its own; 0 leaves the client's in charge.
    set_ini(ini, "OpenGLDrv.OpenGLRenderDevice", {"FrameRateLimit": "0", "UseVSync": "False"})


def launch(copy, system, install, map_name, log, ini_lines, deadline_seconds, while_running=None):
    """Run the client on a private display until UTAREC DONE, and return the log's UTAREC lines."""
    (system / f"{PACKAGE}.ini").write_text(f"[{PACKAGE}.{PACKAGE}]\n" + "".join(l + "\n" for l in ini_lines))
    log.unlink(missing_ok=True)
    display = free_display()
    xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", f"{SIZE[0]}x{SIZE[1]}x24", "+extension", "GLX"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    client = None
    try:
        time.sleep(2)
        if xvfb.poll() is not None:
            fail(f"Xvfb could not start on {display} (exit {xvfb.returncode})")
        url = (f"{map_name}.unr?Game=Botpack.DeathMatchPlus?Mutator={PACKAGE}.{PACKAGE}"
               f"?Class={PACKAGE}.UTARecPawn")
        client = subprocess.Popen(
            read_only(install) + ["./ut-bin-amd64", url, "-ini=UnrealTournament.ini", "-userini=User.ini",
                                  "-nohomedir", "-nosound", f"-log={log}", "-forcelogflush"],
            cwd=system, env=headless_env(copy, display), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        started = time.time()
        done = False
        while time.time() < started + deadline_seconds and client.poll() is None:
            time.sleep(1)
            text = log.read_text(errors="replace") if log.exists() else ""
            if "UTAREC" not in text and time.time() > started + 90:
                break  # the map never loaded, or the mutator never ran
            if while_running and "UTAREC CAL READY" in text:
                while_running(display)
                while_running = None
            if "UTAREC DONE" in text or "UTAREC NOK" in text:
                done = True
                time.sleep(2)
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
    lines = [l[l.index("UTAREC") + len("UTAREC "):] for l in text.splitlines() if "UTAREC " in l]
    if "NOK" in lines:
        fail(f"K is not set in {PACKAGE}.uc; run --calibrate first (spec section 4.6)")
    if not done:
        fail(f"no UTAREC DONE within {deadline_seconds} s; see {log}")
    return lines


def split_runs(lines):
    """{run name: its lines} from one launch's UTAREC lines, and the skipped runs."""
    runs, skipped, current = {}, [], None
    for line in lines:
        fields = line.split()
        if fields[0] == "SKIP":
            skipped.append(" ".join(fields[1:]))
            runs.pop(current, None)  # a run abandoned part way is not a recording
            current = None
        elif fields[:2] == ["1", "BEGIN"]:
            current = dict(f.split("=", 1) for f in fields[2:] if "=" in f)["run"]
            runs[current] = [line]
        elif current and fields[0] == "1":
            runs[current].append(line)
            if fields[1] == "END":
                current = None
    return runs, skipped


def check_recording(run, lines, rate):
    """INV-2, INV-3, INV-5 and INV-7 on one recording. Returns (summary row, problems)."""
    problems = []
    pawn = next((l.split()[2:] for l in lines if l.split()[1] == "PAWN"), None)
    if pawn is None or lines[-1].split()[1] != "END":
        return None, ["no PAWN or END line"]
    values = dict(f.split("=", 1) for f in pawn)
    if values.get("class") != "UTARecPawn":
        problems.append(f"the player is {values.get('class')}, not UTARecPawn (INV-3)")
    for key, want in SHIPPED.items():
        if key not in values or abs(float(values[key]) - want) > 1e-3:
            problems.append(f"{key}={values.get(key)}, the game as shipped has {want} (INV-2)")
    rows = [l.split()[2:] for l in lines if l.split()[1] == "T"]
    if not rows:
        return None, problems + ["no ticks"]
    times, dts = [float(r[1]) for r in rows], [float(r[2]) for r in rows]
    for r, t in zip(rows, times):
        if r[3] != run.keys_at(t - times[0]):
            problems.append(f"tick {r[0]} holds {r[3]}, the script {run.keys_at(t - times[0])} (INV-5)")
            break
    dilation = float(values.get("TimeDilation", TIME_DILATION))
    real = statistics.median(dt / dilation for dt in dts)
    achieved = 1 / real if real > 0 else 0
    missed = abs(real * rate - 1) > 0.03 or max(dts) > 1.5 * min(dts)
    z0 = float(rows[0][8])
    end = rows[-1][6:9]
    rise = max(float(r[8]) for r in rows) - z0
    speed = max((float(r[9]) ** 2 + float(r[10]) ** 2) ** 0.5 for r in rows)
    row = [run.name, str(rate), f"{achieved:.2f}", f"{min(dts):.6f}", f"{max(dts):.6f}", *end, f"{rise:.3f}",
           f"{speed:.3f}", "missed" if missed else "ok"]
    return row, problems


def record(args, install, copy, system, out):
    runs = read_runs(args.runs)
    if args.only:
        wanted = set(args.only.split(","))
        runs = [r for r in runs if r.name in wanted]
        if wanted - {r.name for r in runs}:
            fail(f"no run named {', '.join(sorted(wanted - {r.name for r in runs}))}")
    if not runs:
        fail("no runs to record")
    check_intervals(runs, args.rates)
    for map_name in {r.map for r in runs}:
        if not (install / "Maps" / f"{map_name}.unr").exists():
            fail(f"no map {map_name}.unr in {install / 'Maps'}")

    summary = [["run", "rate", "achieved", "dt_min", "dt_max", "end_x", "end_y", "end_z", "rise", "max_speed_2d",
                "status"]]
    failed = []
    for map_name in sorted({r.map for r in runs}):
        on_map_all = [r for r in runs if r.map == map_name]
        for rate, first in itertools.product(args.rates, range(0, len(on_map_all), MAX_RUNS)):
            on_map = on_map_all[first:first + MAX_RUNS]
            batch = f"-{first // MAX_RUNS}" if len(on_map_all) > MAX_RUNS else ""
            pin_settings(system, rate)
            # 90 s to load, then each run's settling and length, doubled: a
            # loaded machine runs the game slower than the clock.
            deadline = 90 + 2 * sum(SKIP_SECONDS + r.end + 2 for r in on_map)
            lines = launch(copy, system, install, map_name, out / f"{map_name}-{rate}{batch}.log",
                           [f"MapName={map_name}", f"Rate={rate}"]
                           + [f"Runs[{i}]={r.line()}" for i, r in enumerate(on_map)], deadline)
            recorded, skipped = split_runs(lines)
            failed += [f"{s} at {rate} fps (skipped)" for s in skipped]
            for run in on_map:
                if run.name not in recorded:
                    if not any(s.split()[0] == run.name for s in skipped):
                        failed.append(f"{run.name} at {rate} fps: not recorded")
                    continue
                (out / f"{run.name}-{rate}.utarec").write_text("\n".join(recorded[run.name]) + "\n")
                row, problems = check_recording(run, recorded[run.name], rate)
                if row:
                    summary.append(row)
                failed += [f"{run.name} at {rate} fps: {p}" for p in problems]
            print(f"ut-record: {map_name} at {rate} fps: {len(recorded)} runs", flush=True)
    (out / "summary.tsv").write_text("\n".join("\t".join(r) for r in summary) + "\n")
    return failed


def calibrate(args, install, copy, system, out):
    """Hold forward, then strafe right, with real key events; report K."""
    if not shutil.which("xdotool"):
        fail("xdotool is not installed; calibration needs it")
    measured = {}
    for rate in args.rates:
        pin_settings(system, rate)

        def press(display):
            env = {"DISPLAY": display, "PATH": "/usr/bin:/bin"}
            # A bare X server has no window manager: keys go to the window
            # under the pointer, and the game's window fills the screen.
            moved = subprocess.run(["xdotool", "mousemove", str(int(SIZE[0]) // 2), str(int(SIZE[1]) // 2)],
                                   check=False, env=env, capture_output=True, text=True, timeout=20)
            if moved.returncode != 0:
                fail(f"xdotool cannot reach the display: {moved.stderr.strip()}")
            time.sleep(4)  # past the match's start
            for key in ("w", "d"):
                subprocess.run(["xdotool", "keydown", key], env=env, check=True)
                time.sleep(1)
                subprocess.run(["xdotool", "keyup", key], env=env, check=True)
                time.sleep(0.5)

        lines = launch(copy, system, install, args.map, out / f"calibrate-{rate}.log",
                       ["bCalibrate=True", f"MapName={args.map}", f"Rate={rate}", "K=1"], 120, press)
        rows = [l.split()[1:] for l in lines if l.startswith("CAL ") and l != "CAL READY"]
        held = [float(r[1]) for r in rows if float(r[1]) != 0] + [abs(float(r[2])) for r in rows if float(r[2]) != 0]
        print(f"ut-record: {rate} fps: {len(held)} held ticks, from {min(held, default=0)} to {max(held, default=0)}")
        # The engine's float arithmetic leaves about 1e-7 of noise; a value that
        # follows the frame time differs between ticks by far more.
        if len(held) < rate or max(held) - min(held) > 1e-5 * max(held):
            fail(f"K is not one value on every held tick at {rate} fps (spec section 6); see {out}")
        measured[rate] = statistics.median(held)
    if max(measured.values()) - min(measured.values()) > 1e-5 * max(measured.values()):
        fail(f"K differs between rates: {measured} (spec section 6)")
    print(f"ut-record: K = {round(statistics.median(measured.values()), 3)}; "
          f"set it in {PACKAGE}.uc's defaultproperties")


def starts(args, install, copy, system, out):
    pin_settings(system, 60)
    lines = launch(copy, system, install, args.starts, out / "starts.log",
                   [f"MapName={args.starts}", "Rate=60", "K=1"], 120)
    for line in lines:
        if line.startswith("START "):
            print(line)


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--runs", type=Path, default=HERE / "runs.txt")
    parser.add_argument("--only")
    parser.add_argument("--rates", default="30,60,85,120,200")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--work", type=Path, default=Path.home() / ".cache/ut-ants/record/install")
    parser.add_argument("--calibrate", action="store_true")
    parser.add_argument("--map", default="DM-Deck16][")
    parser.add_argument("--starts")
    parser.add_argument("--help", action="store_true")
    args = parser.parse_args()
    if args.help:
        print(__doc__)
        return
    args.rates = [int(r) for r in args.rates.split(",")]

    install = args.install.resolve()
    for tool in ("Xvfb", "bwrap"):
        if not shutil.which(tool):
            fail(f"{tool} is not installed")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = (args.out or Path.home() / ".cache/ut-ants/movement" / stamp).resolve()
    if out == REPO or REPO in out.parents:  # INV-10: recordings stay out of the repository
        fail(f"--out {out} is inside the repository; recordings are not committed (ADR-0003)")
    out.mkdir(parents=True, exist_ok=True)

    started = time.time()
    copy = args.work.resolve()
    system = prepare_copy(install, copy)
    compile_package(PACKAGE, HERE / PACKAGE, copy, system, install)
    failed = []
    if args.calibrate:
        calibrate(args, install, copy, system, out)
    elif args.starts:
        starts(args, install, copy, system, out)
    else:
        failed = record(args, install, copy, system, out)

    touched = changed_since(install, started)  # INV-1
    if touched:
        fail("the live install changed during the run, which must never happen: " + ", ".join(touched))
    if failed:
        fail("these runs failed:\n  " + "\n  ".join(failed) + f"\nrecordings in {out}")
    print(f"ut-record: done; output in {out}")


if __name__ == "__main__":
    main()
