#!/usr/bin/env bash
# The pipeline. ONE list of steps, run in two places.
#
# .github/workflows/ci.yml CALLS this file and duplicates none of it, because a
# hand-written mirror of a pipeline is correct on the day it is written and
# drifts from then on -- and a drifted mirror returns green for a pipeline that
# will fail (local-gate.md § 3). The push gate runs the same file too, once per
# compiler, through scripts/ci-matrix.sh, over the commit being pushed.
#
#   scripts/ci.sh          everything
#   scripts/ci.sh --docs   the documentation checks only
#
# WHY --docs EXISTS. Skipping outright is how a prose typo reaches a repository
# whose own suite forbids it; rebuilding and re-testing for a typo is how a
# person learns to reach for --no-verify. So --docs omits the compiler legs and
# NOTHING ELSE: every check that does not need a compiler runs in both modes.
# Both gates pick the mode from one list, scripts/docs-only.sh -- the local
# hook as ants.gate.docsCommand, ci.yml over the push's range (UTA-0236) -- and
# GitHub runs the full gate whenever that range is unknown. A mode cheaper than
# the pipeline is fine; one that checks LESS of what it checks is a green that
# lies.
# The quarantine guard's path checks are in both modes for their own reason --
# a path under content/ can be a .md file and would otherwise ride in on a
# "docs-only" push.
#
# WHERE IT RUNS. Linux and Windows, both first-class. On Windows this is Git
# Bash, which ships with Git for Windows, and the Visual Studio generator is
# used so no developer command prompt is needed. Everything here is POSIX shell
# plus git; a check whose tool is absent SAYS SO and is not silently dropped.
set -Eeuo pipefail

cd "$(git rev-parse --show-toplevel)"

MODE=full
case "${1:-}" in
    --docs) MODE=docs ;;
    --help | -h)
        sed -n '2,20p' "$0"
        exit 0
        ;;
    "") ;;
    *)
        printf 'ci: unknown argument %q (try --help)\n' "$1" >&2
        exit 2
        ;;
esac

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) IS_WINDOWS=true ;;
    *) IS_WINDOWS=false ;;
esac

BUILD_DIR=${UTA_CI_BUILD_DIR:-build-ci}
CONFIG=${UTA_CI_CONFIG:-Release}
# Ninja on Linux, per docs/design.md. On Windows the Visual Studio generator
# finds MSVC by itself; Ninja would need cl.exe already on PATH, which means a
# developer command prompt locally and a third-party action in CI.
if $IS_WINDOWS; then
    GENERATOR=${UTA_CI_GENERATOR:-Visual Studio 17 2022}
else
    GENERATOR=${UTA_CI_GENERATOR:-Ninja}
fi

# Tests run side by side (UTA-0242). Measured 2026-09-28 on 12 threads and
# 31 GB: at -j12 the Release tier's free memory moved under 1 GB and the
# ThreadSanitizer tier's did not drop, so the CPU count is the bound, not
# memory. UTA_CI_TEST_JOBS overrides it. Every temp directory a test makes
# carries a random salt, and --timeout stops a hung test from hanging the gate.
TEST_JOBS=${UTA_CI_TEST_JOBS:-$(nproc 2>/dev/null || echo 2)}
TEST_TIMEOUT=300

skipped=()
# Each step's wall time, printed as it ends and as a table on exit -- a red
# run's table included -- so a speed-up aims at a measured step (UTA-0234).
timings=()
current_step=
step_start=$SECONDS
end_step() {
    [[ -z $current_step ]] && return 0
    local took=$((SECONDS - step_start))
    printf '   (%ds)\n' "$took"
    timings+=("$(printf '%6ds  %s' "$took" "$current_step")")
    current_step=
}
print_timings() {
    local status=$?
    end_step
    if [[ ${#timings[@]} -gt 0 ]]; then
        printf '\n== step timings (%ds in all)\n' "$SECONDS"
        printf '%s\n' "${timings[@]}"
    fi
    return "$status"
}
trap print_timings EXIT
step() {
    end_step
    current_step=$1
    step_start=$SECONDS
    printf '\n\033[1m== %s\033[0m\n' "$1"
}
skip() {
    printf '   SKIPPED: %s\n' "$1"
    skipped+=("$1")
}

# ── Documentation checks — run in BOTH modes ────────────────────────────────

step "quarantine guard"
./scripts/quarantine-guard.sh
if [[ $MODE == docs ]]; then
    # UTA-0013's third check reads each tracked bundle's header through
    # ut-origin, which this mode does not build. No push can use the gap:
    # scripts/docs-only.sh never classifies a push carrying a .utab as
    # documentation-only, so the full run checks it.
    skip "the quarantine guard's bundle-origin check needs ut-origin, which --docs does not build"
fi

step "documentation: relative links resolve"
link_failures=0
# -z, so a non-ASCII file name is not handed over quoted (review-code 2026-09-26).
while IFS= read -r -d '' file; do
    dir=$(dirname "$file")
    while IFS= read -r target; do
        [[ -z $target ]] && continue
        # Off-tree and in-page links are not this check's business.
        [[ $target =~ ^([a-zA-Z][a-zA-Z0-9+.-]*): ]] && continue
        [[ $target == \#* ]] && continue
        target=${target%%#*} # drop an anchor; the path is what must exist
        target=${target%% *} # drop a "path 'title'" suffix
        [[ -z $target ]] && continue
        # A leading / is the repository's root, as GitHub renders it, never
        # this machine's.
        if [[ $target == /* ]]; then resolved=".$target"; else resolved="$dir/$target"; fi
        if [[ ! -e $resolved ]]; then
            printf '   %s -> %s does not exist\n' "$file" "$target" >&2
            link_failures=$((link_failures + 1))
        fi
    done < <(grep -oE '\]\([^)]+\)' "$file" | sed -E 's/^\]\(//; s/\)$//')
done < <(git ls-files -z '*.md')
if [[ $link_failures -gt 0 ]]; then
    printf 'ci: %d broken relative link(s) in the documentation.\n' "$link_failures" >&2
    exit 1
fi
printf '   %d markdown files, every relative link resolves.\n' "$(git ls-files '*.md' | wc -l)"

# ── Static analysis ─ also BOTH modes ─────────────────────────────
#
# These are here, above the documentation-only exit, because a typo can break
# them too. Anything cheap enough to run on a typo belongs in both modes, or a
# docs-only green means less than a full one. They cost about a second between
# them; the compiler legs are what --docs exists to skip.

step "umap holds no per-player state"
# UTA-0007 INV-7, and design rule 18 is what it protects: a `visited` flag on
# Room would be serialised into the bundle every client holds, which is the
# wallhack that rule exists to prevent. Nothing else in the build would fail.
#
# The comment filter is load-bearing, not decoration. Any header documenting
# why this state is absent must use the very words this hunts, so a pattern
# matching every line would be falsified by the sentence documenting the rule.
# UTA-0004's INV-3 failed exactly that way.
#
# It runs in BOTH modes. A grep costs nothing, and a guard that runs only on
# the compiler legs is one a change can be routed around.
# A grep over a file that is not there exits 2 and reads as clean, so a rename
# of Rooms.h would switch the guard off without a word.
if [[ ! -f src/umap/Rooms.h ]]; then
    printf 'ci: src/umap/Rooms.h is missing -- the UTA-0007 INV-7 guard checked nothing.\n' >&2
    exit 2
fi
if inv7=$(grep -nE '\b(visited|explored|seen|player|team)\b' src/umap/Rooms.h |
    grep -vE ':[[:space:]]*(//|\*)'); then
    printf 'ci: src/umap/Rooms.h carries per-player state, against UTA-0007 INV-7:\n' >&2
    printf '%s\n' "$inv7" >&2
    exit 1
fi
printf '   src/umap/Rooms.h clean.\n'

step "every standard function has its header"
# libstdc++ reaches <bit> and <algorithm> through other headers and MSVC does
# not, so a missing include is green on both Linux legs and red only on
# Windows. When the Windows machine is unreachable this is the local gate's
# only defence against that (runs 36574630875 and 36575289692).
./scripts/std-includes.sh

step "shell scripts"
# Two lists, and the difference is deliberate. shellcheck finds DEFECTS, so it
# reads the git hooks too -- they run on every commit and push, and its first
# run here found a dead case pattern in one. shfmt enforces a house FORMAT, and
# the hooks arrive from ~/.claude/skeleton written in another one; reformatting
# them would be a large diff tracing to nothing and would fork them from the
# skeleton they are maintained in. A format difference is not a defect.
mapfile -t ANALYSE < <(git ls-files 'scripts/*.sh' '.githooks/*')
mapfile -t FORMAT < <(git ls-files 'scripts/*.sh')
if [[ ${#ANALYSE[@]} -eq 0 || ${#FORMAT[@]} -eq 0 ]]; then
    # Not "clean". These files are tracked; an empty list means the checkout is
    # not what this script thinks it is, and passing two linters no arguments
    # would report exactly that as success.
    printf 'ci: no tracked shell scripts found — this is not the repository ci.sh belongs to.\n' >&2
    exit 2
fi
if command -v shellcheck >/dev/null; then
    shellcheck "${ANALYSE[@]}"
    printf '   shellcheck clean (%d files).\n' "${#ANALYSE[@]}"
else
    skip "shellcheck is not installed — the shell scripts were not analysed"
fi
if command -v shfmt >/dev/null; then
    shfmt --diff --indent 4 --case-indent "${FORMAT[@]}"
    printf '   shfmt clean (%d files, the hooks excluded).\n' "${#FORMAT[@]}"
else
    skip "shfmt is not installed — shell formatting was not checked"
fi

step "workflows"
if command -v actionlint >/dev/null; then
    actionlint
    printf '   actionlint clean.\n'
else
    skip "actionlint is not installed — the workflows were not analysed"
fi
if command -v yamllint >/dev/null; then
    yamllint .github/workflows
    printf '   yamllint clean.\n'
else
    skip "yamllint is not installed — the workflow YAML was not linted"
fi

step "the local gate and GitHub test the same compilers"
# UTA-0232: ci-matrix.sh is a wrapper over this script, not a mirror, only
# while its legs match ci.yml's matrix (local-gate.md § 3).
./scripts/check-legs.sh

if [[ $MODE == docs ]]; then
    step "documentation-only run complete"
    [[ ${#skipped[@]} -gt 0 ]] && printf '   %d check(s) skipped, listed above.\n' "${#skipped[@]}"
    # What this mode omits is named rather than left to be inferred: exactly the
    # compiler legs and the bundle-origin check that needs their output, which a
    # change scripts/docs-only.sh accepts cannot reach. Said out loud for the
    # same reason a missing tool is: a green narrower than the pipeline's must
    # not read as the same green.
    printf '   Omitted, and ONLY these: configure, build, bundle origin, test, race detector.\n'
    printf '   Reproduce the full gate with scripts/ci.sh and no argument.\n'
    exit 0
fi

# ── The compiler legs — full mode only ──────────────────────────────────────
#
# The only checks a documentation-only change cannot affect, which is why they
# are the only ones below the exit above.

step "configure ($GENERATOR, $CONFIG)"
# Name the compiler. This script IS the pipeline -- GitHub calls this same
# file -- but GitHub calls it once PER COMPILER, and a developer's machine
# runs it once with whatever CXX resolves to. So the STEPS never drift, and
# the MATRIX does: a local green covers one leg of three, and saying which
# one is the difference between that being understood and being missed.
#
# To run another leg locally, set CC and CXX the way the workflow does:
#   CC=clang-19 CXX=clang++-19 ./scripts/ci.sh
# Named after the configure, from the cache: an unset CXX says nothing about
# which compiler CMake then found.

# UTA_CI_DEPS_DIR: a directory of the fetched libraries' sources, kept between
# GitHub runs so each configure does not clone them again (about 12 s of a
# 29 s fresh configure, measured 2026-09-28). ci.yml keys it on the files
# that pin the libraries' tags, so a new tag starts it empty. Complete, it is
# handed to FetchContent; anything less and FetchContent downloads as usual,
# and the sources it fetched are copied in afterwards. Unset -- every local
# run -- changes nothing.
DEPS=(glm sdl3 catch2)
deps_args=()
if [[ -n ${UTA_CI_DEPS_DIR:-} ]]; then
    # .complete is written last, after every copy landed, so a directory a
    # killed run half-filled is never mistaken for a whole one.
    if [[ -f $UTA_CI_DEPS_DIR/.complete ]]; then
        for dep in "${DEPS[@]}"; do
            deps_args+=("-DFETCHCONTENT_SOURCE_DIR_${dep^^}=$UTA_CI_DEPS_DIR/$dep-src")
        done
        printf '   libraries: the kept sources in %s\n' "$UTA_CI_DEPS_DIR"
    else
        printf '   libraries: %s is incomplete, so they are downloaded\n' "$UTA_CI_DEPS_DIR"
    fi
fi

configure=(-S . -B "$BUILD_DIR" -G "$GENERATOR" "${deps_args[@]}")
case $GENERATOR in
    "Visual Studio"* | Xcode | "Ninja Multi-Config") ;; # multi-config: the config is chosen at build time
    *) configure+=(-DCMAKE_BUILD_TYPE="$CONFIG") ;;
esac
cmake "${configure[@]}"
compiler=$(sed -n 's/^CMAKE_CXX_COMPILER:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null)
compiler=${compiler:-${CXX:-the generator\'s own}} # a Visual Studio generator caches none
printf '   compiler: %s\n' "$compiler"
if [[ -n ${UTA_CI_DEPS_DIR:-} && ${#deps_args[@]} -eq 0 ]]; then
    rm -rf "${UTA_CI_DEPS_DIR:?}"
    mkdir -p "$UTA_CI_DEPS_DIR"
    for dep in "${DEPS[@]}"; do
        cp -R "$BUILD_DIR/_deps/$dep-src" "$UTA_CI_DEPS_DIR/"
    done
    touch "$UTA_CI_DEPS_DIR/.complete"
fi

step "build"
# --parallel: Ninja builds in parallel by default, but the Visual Studio
# generator builds one project at a time without it (UTA-0147).
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel

step "quarantine guard: bundle origin"
# UTA-0013's third check reads each tracked .utab's header through ut-origin,
# the one reader docs/specs/UTA-0008-bundle-container-and-origin.md SS 4.5
# names, so it waits for the build. The path checks ran above, in both modes.
case $GENERATOR in
    "Visual Studio"* | Xcode | "Ninja Multi-Config") origin_tool="$BUILD_DIR/tools/ut-origin/$CONFIG/ut-origin" ;;
    *) origin_tool="$BUILD_DIR/tools/ut-origin/ut-origin" ;;
esac
if $IS_WINDOWS; then origin_tool+=.exe; fi
./scripts/quarantine-guard.sh --origin-tool "$origin_tool"

step "test"
# The default tier only. UTA_REAL_ASSET_TESTS needs an Unreal Tournament
# install, which no runner and no stranger's clone has -- that separation is
# what S7 is measured on, so the gate must never quietly turn it on.
#
# LABELS, PER PLATFORM -- docs/specs/UTA-0014-vulkan-draw-path.md SS 7. The
# `device` tier draws on a real Vulkan device, and the only one a runner has is
# Mesa's CPU driver, installed on the Linux legs alone. So Windows deselects it
# BY LABEL, and says so. It is never deselected by LOOKING for a driver: a gate
# that did would skip the tier on a Linux leg that lost its driver, which is the
# green-over-nothing INV-5 exists to forbid. There the device tests fail.
#
# --no-tests=error, because a label pattern that matched nothing would
# otherwise run nothing and pass.
if $IS_WINDOWS; then
    labels='^(unit|device-absent)$'
    skip "the device tier (label 'device') is registered and was NOT run: this leg installs no Vulkan driver (UTA-0014 SS 9)"
else
    labels='^(unit|device|device-absent)$'
fi
# UTA-0227: the device tier runs under the validation layer's synchronization
# checks too. The default settings missed a race these found at once (50 of 52
# tests, f02396c), and later an unordered clear. Measured on lavapipe: about
# 0.1 s over a 4 s tier. Not a substitute for reading the barriers -- it did
# not flag the compute-to-draw buffer hazards f02396c also fixed.
export VK_VALIDATION_VALIDATE_SYNC=true
ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure --no-tests=error -L "$labels" \
    -j "$TEST_JOBS" --timeout "$TEST_TIMEOUT"

step "race detector"
# The job system is the first threaded code here, and an ordinary test run
# does not see data races -- a racy suite passes green. This is what INV-13 of
# docs/specs/UTA-0002-core-foundations.md is measured with.
#
# A SECOND configure and build, not a re-run of the one above: the
# instrumentation is a compile option, so it cannot be applied to an existing
# build tree.
if $IS_WINDOWS; then
    skip "MSVC has no ThreadSanitizer — the race detector did not run"
else
    TSAN_DIR="${BUILD_DIR}-tsan"
    # Output is NOT swallowed. An earlier version sent configure and build to
    # /dev/null, and when the Clang leg failed here it printed nothing at all
    # -- ninja writes its FAILED lines to stdout, so the one thing needed to
    # diagnose the failure was the thing being discarded. A gate that cannot
    # say why it failed is not a gate.
    # The client is left out: its subject is this project's threads, and the
    # client adds only SDL3, which would be compiled instrumented for nothing.
    cmake -S . -B "$TSAN_DIR" -G "$GENERATOR" "${deps_args[@]}" \
        -DCMAKE_BUILD_TYPE=Debug -DUTA_SANITIZE=thread -DUTA_BUILD_CLIENT=OFF
    cmake --build "$TSAN_DIR"
    # The unit tier alone, which is every test this step ran before the device
    # tier existed. Its subject is this project's own threads; a device test
    # would run Mesa's uninstrumented ones under it too.
    ctest --test-dir "$TSAN_DIR" --output-on-failure --no-tests=error -L '^unit$' \
        -j "$TEST_JOBS" --timeout "$TEST_TIMEOUT"
    printf '   clean under ThreadSanitizer.\n'
fi

step "green"
if [[ ${#skipped[@]} -gt 0 ]]; then
    printf '   ...with %d check(s) skipped:\n' "${#skipped[@]}"
    printf '     - %s\n' "${skipped[@]}"
fi
if [[ -z ${GITHUB_ACTIONS:-} ]]; then
    printf '   This was ONE leg (%s). GitHub runs GCC, Clang and MSVC.\n' \
        "$compiler"
fi
