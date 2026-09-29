#!/usr/bin/env bash
# The push gate: GitHub's three legs, run here before a push. UTA-0207.
#
#   scripts/ci-matrix.sh          GCC 14, Clang 19 and MSVC
#   scripts/ci-matrix.sh --docs   the documentation checks only, once
#
# WHY IT EXISTS. scripts/ci.sh is the one list of steps, and ci.yml calls it,
# so the STEPS cannot drift. The COMPILERS did: GitHub runs GCC 14, Clang 19
# and MSVC, and a local run used whatever CXX resolved to. UTA-0012 went red on
# MSVC alone, over a test no local leg could see. This runs ci.sh once per leg.
#
# THE WINDOWS LEG runs on the Windows test machine over SSH (host alias from
# UTA_WIN_HOST, default `wintest-gate`). It gets the commit as a git bundle and
# checks it out into a copy it keeps between runs, so its build is incremental.
# It starts first and runs alongside the Linux legs. It gates the push ONLY IF
# IT RAN: an unreachable machine, or an SSH connection lost mid-run, prints
# "WINDOWS LEG NOT RUN" and lets the push go (user decision, 2026-09-25).
# ci.sh failing there fails the gate like any other leg.
#
# The alias must log in as a NON-ADMIN account. Elevated, the Vulkan loader
# ignores VK_DRIVER_FILES, so INV-5's no-driver tests find the machine's real
# GPU and fail. GitHub's runner has no GPU, so it never sees this.
#
# It checks HEAD, not the working tree. The pre-push hook runs it where HEAD is
# the pushed commit: the checkout itself when that is clean at it (UTA-0238),
# otherwise a detached worktree of it.
#
# THE LINUX LEGS run one after the other, with UTA_CI_JOBS parallel jobs
# (default 4), because RAM on this machine is tight. Each has its own build
# directory. Clang is pinned to GCC 14's standard library, which is what the
# CI leg compiles against; left alone it picks the newest GCC installed.
set -Eeuo pipefail

cd "$(git rev-parse --show-toplevel)"

case "${1:-}" in
    --docs) exec ./scripts/ci.sh --docs ;;
    --help | -h)
        sed -n '2,30p' "$0"
        exit 0
        ;;
    "") ;;
    *)
        printf 'ci-matrix: unknown argument %q (try --help)\n' "$1" >&2
        exit 2
        ;;
esac

WIN_HOST=${UTA_WIN_HOST:-wintest-gate}
JOBS=${UTA_CI_JOBS:-4}
GCC14_DIR=/usr/lib64/gcc/x86_64-suse-linux/14
STATE=${XDG_CACHE_HOME:-$HOME/.cache}/uta-gate
mkdir -p "$STATE"
SHA=$(git rev-parse HEAD)

# One gate at a time (UTA-0222). Two share the Windows machine's checkout, so
# a second run's `checkout -f` could land mid-build of the first -- a false
# green -- and in place they share the Linux build trees too. The second waits.
exec 9>"$STATE/gate.lock"
if ! flock -n 9; then
    printf '== another gate is running; waiting for it to finish\n'
    flock 9
fi
WIN_LOG="$STATE/windows-${SHA:0:12}.log"
WIN_DONE=UTA-GATE-DONE # the remote script's completion marker (UTA-0233)

banner() {
    printf '\n\033[1;33m%s\n%s\n%s\033[0m\n' \
        '################################################################' \
        "  $1" \
        '################################################################'
}

for tool in gcc-14 g++-14 clang-19 clang++-19; do
    if ! command -v "$tool" >/dev/null; then
        printf 'ci-matrix: %s is not installed, so its leg cannot run.\n' "$tool" >&2
        printf '  Install gcc14-c++ and clang19, or run scripts/ci.sh for one leg.\n' >&2
        exit 2
    fi
done

# ── Windows: start it first ─────────────────────────────────────────────────

win_pid=
# Ctrl-C or a killed push stops the Windows leg rather than leaving it running
# (UTA-0222); the remote script also clears what an aborted run left.
stop_windows() {
    if [[ -n $win_pid ]]; then
        pkill -TERM -P "$win_pid" 2>/dev/null # its ssh and scp
        kill "$win_pid" 2>/dev/null
    fi
    return 0
}
trap stop_windows EXIT
trap 'stop_windows; exit 130' INT
trap 'stop_windows; exit 143' TERM

# Unreachable is not misconfigured (UTA-0222). A machine that is off or away
# lets the push go (user decision, 2026-09-25); an alias, key or host key that
# is wrong would drop MSVC from every push in silence, so it fails the gate.
probe_err=$(ssh -o BatchMode=yes -o ConnectTimeout=5 "$WIN_HOST" exit 2>&1 >/dev/null) && reachable=true || reachable=false
if ! $reachable && grep -qiE 'permission denied|host key verification failed|could not resolve hostname|no such identity|bad configuration|unknown option' <<<"$probe_err"; then
    banner "WINDOWS LEG MISCONFIGURED: ssh $WIN_HOST refused, not unreachable: $probe_err"
    exit 1
fi
if $reachable; then
    printf '== Windows (MSVC) leg starting on %s; log: %s\n' "$WIN_HOST" "$WIN_LOG"
    # Keep-alives, so a link that dies without a reset (the machine sleeps, Wi-Fi
    # drops) ends in about a minute as WINDOWS LEG NOT RUN instead of hanging the
    # push (review-code 2026-09-26).
    alive=(-o ServerAliveInterval=15 -o ServerAliveCountMax=4)
    (
        set -e
        bundle="$STATE/push-${SHA:0:12}.bundle"
        git bundle create "$bundle" HEAD 2>/dev/null
        scp -q "${alive[@]}" "$bundle" "$WIN_HOST:uta-gate.bundle" || exit 255 # the link, not the build
        scp -q "${alive[@]}" scripts/windows-gate-reap.sh "$WIN_HOST:uta-gate-reap.sh" || exit 255
        rm -f "$bundle"
        # Piped on stdin: the machine's SSH shell is cmd.exe, which mangles any
        # quoting a script passed as an argument would need. $SHA expands here,
        # on purpose; nothing else in the script does.
        #
        # The trap prints a completion marker carrying the remote status. ssh
        # passes that status through, so a ci.sh exiting 255 is otherwise
        # indistinguishable from ssh's own 255 for a lost link (UTA-0233).
        # A dropped connection cannot print the marker; a finished run always
        # does, whatever ended it.
        # shellcheck disable=SC2087
        ssh "${alive[@]}" "$WIN_HOST" '"C:\Program Files\Git\bin\bash.exe" -l -s' <<EOF
trap 'echo "$WIN_DONE rc=\$?"' EXIT
set -e
mkdir -p ~/uta-gate
cd ~/uta-gate
[ -d repo/.git ] || git init -q repo
cd repo
# A dropped connection does NOT end the remote script (measured), so what the
# last aborted run left is stopped before this one touches the checkout
# (UTA-0222).
bash ~/uta-gate-reap.sh
git fetch -q ~/uta-gate.bundle $SHA
git checkout -q -f --detach $SHA
# MSYS_NO_PATHCONV: Git Bash rewrites an argument starting with / into a path
# under its install directory, so "-e /build-ci/" excluded nothing and every
# run deleted the warm build tree -- a cold 528 s MSVC build each push.
MSYS_NO_PATHCONV=1 git clean -qffdx -e /build-ci/
# The pinned ccache GitHub's Windows leg uses, so this leg takes the same
# MSBuild route (UTA-0235) -- without it, 5b23ec5 passed here and failed there.
ccache_bin=\$(./scripts/ccache-windows.sh ~/uta-gate/ccache-bin)
export PATH="\$ccache_bin:\$PATH"
export CCACHE_DIR="\$(cygpath -w ~/uta-gate/ccache)" CCACHE_BASEDIR="\$(cygpath -w "\$PWD")"
export CCACHE_NOHASHDIR=true CCACHE_MAXSIZE=1G
./scripts/ci.sh
EOF
    ) >"$WIN_LOG" 2>&1 &
    win_pid=$!
else
    banner "WINDOWS LEG NOT RUN: $WIN_HOST is unreachable. GitHub will run it."
fi

# ── Linux: one leg at a time ────────────────────────────────────────────────

# The pre-push hook may run this in the real checkout (ants.gate.inPlace,
# UTA-0238), where an edit made mid-run would be built in place of the pushed
# commit. So the tree's state is snapshotted here and compared after the legs;
# any difference fails the gate. In a fresh worktree nothing changes it.
tree_state() { git rev-parse HEAD && git status --porcelain --untracked-files=all; }
state_before=$(tree_state)

results=()
linux_failed=false
run_leg() {
    local name=$1
    shift
    printf '\n\033[1m######## %s ########\033[0m\n' "$name"
    if env "$@" CMAKE_BUILD_PARALLEL_LEVEL="$JOBS" ./scripts/ci.sh; then
        results+=("$name: green")
    else
        results+=("$name: RED")
        linux_failed=true
    fi
}

run_leg "Linux (GCC 14)" CC=gcc-14 CXX=g++-14 UTA_CI_BUILD_DIR=build-ci-gcc14
run_leg "Linux (Clang 19)" CC=clang-19 CXX=clang++-19 UTA_CI_BUILD_DIR=build-ci-clang19 \
    CFLAGS="--gcc-install-dir=$GCC14_DIR" CXXFLAGS="--gcc-install-dir=$GCC14_DIR" \
    LDFLAGS="--gcc-install-dir=$GCC14_DIR"

if [[ $(tree_state) != "$state_before" ]]; then
    results+=("Linux: RED (the working tree changed while the legs ran)")
    banner "THE TREE CHANGED DURING THE GATE: the Linux legs may have built edits, not ${SHA:0:12}. Push again."
    linux_failed=true
fi

# ── Windows: collect ────────────────────────────────────────────────────────

win_failed=false
if [[ -n $win_pid ]]; then
    printf '\n== waiting for the Windows (MSVC) leg\n'
    win_status=0
    wait "$win_pid" || win_status=$?
    win_pid= # collected: nothing left for the EXIT trap to stop
    tr -d '\r' <"$WIN_LOG" | tail -n 25
    # The remote script's own status, if it finished; empty if it never did.
    win_rc=$(tr -d '\r' <"$WIN_LOG" | sed -n "s/^$WIN_DONE rc=\([0-9]*\)\$/\1/p" | tail -n 1)
    if [[ $win_rc == 0 ]]; then
        results+=("Windows (MSVC): green")
    elif [[ -z $win_rc && $win_status == 255 ]]; then
        # No marker and ssh's own failure code: the connection went, not the
        # build.
        results+=("Windows (MSVC): NOT RUN (connection lost)")
        banner "WINDOWS LEG NOT RUN: the SSH connection failed mid-run. Log: $WIN_LOG"
    else
        # A finished run that failed, or anything unexplained -- including a
        # zero status with no marker, which fails closed.
        results+=("Windows (MSVC): RED (log: $WIN_LOG)")
        win_failed=true
    fi
else
    results+=("Windows (MSVC): NOT RUN (unreachable)")
fi

printf '\n\033[1m== matrix\033[0m\n'
printf '   %s\n' "${results[@]}"
if $linux_failed || $win_failed; then
    exit 1
fi
