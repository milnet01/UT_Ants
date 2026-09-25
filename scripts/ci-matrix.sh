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
# It checks HEAD, not the working tree. The pre-push hook runs it in a
# worktree of the pushed commit, where the two are the same.
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
WIN_LOG="$STATE/windows-${SHA:0:12}.log"

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
if ssh -o BatchMode=yes -o ConnectTimeout=5 "$WIN_HOST" exit 2>/dev/null; then
    printf '== Windows (MSVC) leg starting on %s; log: %s\n' "$WIN_HOST" "$WIN_LOG"
    (
        set -e
        bundle="$STATE/push.bundle"
        git bundle create "$bundle" HEAD 2>/dev/null
        scp -q "$bundle" "$WIN_HOST:uta-gate.bundle" || exit 255 # the link, not the build
        # Piped on stdin: the machine's SSH shell is cmd.exe, which mangles any
        # quoting a script passed as an argument would need. $SHA expands here,
        # on purpose; nothing else in the script does.
        # shellcheck disable=SC2087
        ssh "$WIN_HOST" '"C:\Program Files\Git\bin\bash.exe" -l -s' <<EOF
set -e
mkdir -p ~/uta-gate
cd ~/uta-gate
[ -d repo/.git ] || git init -q repo
cd repo
git fetch -q ~/uta-gate.bundle $SHA
git checkout -q -f --detach $SHA
git clean -qffdx -e /build-ci/
./scripts/ci.sh
EOF
    ) >"$WIN_LOG" 2>&1 &
    win_pid=$!
else
    banner "WINDOWS LEG NOT RUN: $WIN_HOST is unreachable. GitHub will run it."
fi

# ── Linux: one leg at a time ────────────────────────────────────────────────

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

# ── Windows: collect ────────────────────────────────────────────────────────

win_failed=false
if [[ -n $win_pid ]]; then
    printf '\n== waiting for the Windows (MSVC) leg\n'
    win_status=0
    wait "$win_pid" || win_status=$?
    tr -d '\r' <"$WIN_LOG" | tail -n 25
    case $win_status in
        0) results+=("Windows (MSVC): green") ;;
        255)
            # ssh's own failure code: the connection went, not the build.
            results+=("Windows (MSVC): NOT RUN (connection lost)")
            banner "WINDOWS LEG NOT RUN: the SSH connection failed mid-run. Log: $WIN_LOG"
            ;;
        *)
            results+=("Windows (MSVC): RED (log: $WIN_LOG)")
            win_failed=true
            ;;
    esac
else
    results+=("Windows (MSVC): NOT RUN (unreachable)")
fi

printf '\n\033[1m== matrix\033[0m\n'
printf '   %s\n' "${results[@]}"
if $linux_failed || $win_failed; then
    exit 1
fi
