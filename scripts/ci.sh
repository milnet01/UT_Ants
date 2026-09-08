#!/usr/bin/env bash
# The pipeline. ONE list of steps, run in two places.
#
# .github/workflows/ci.yml CALLS this file and duplicates none of it, because a
# hand-written mirror of a pipeline is correct on the day it is written and
# drifts from then on -- and a drifted mirror returns green for a pipeline that
# will fail (local-gate.md § 3). .githooks/pre-push runs the same file, over the
# commits being pushed, before they go.
#
#   scripts/ci.sh          everything
#   scripts/ci.sh --docs   the documentation checks only
#
# WHY --docs EXISTS. Skipping outright is how a prose typo reaches a repository
# whose own suite forbids it; rebuilding and re-testing for a typo is how a
# person learns to reach for --no-verify. So --docs omits the compiler legs and
# NOTHING ELSE: every check that does not need a compiler runs in both modes,
# because ci.yml calls this script with no argument and GitHub therefore applies
# the full run to a documentation-only push. A mode cheaper than the pipeline is
# fine; one that checks LESS of what the pipeline checks is a green that lies.
# The quarantine guard is in both modes for its own reason -- a path under
# content/ can be a .md file and would otherwise ride in on a "docs-only" push.
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

skipped=()
step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
skip() {
    printf '   SKIPPED: %s\n' "$1"
    skipped+=("$1")
}

# ── Documentation checks — run in BOTH modes ────────────────────────────────

step "quarantine guard"
./scripts/quarantine-guard.sh

step "documentation: relative links resolve"
link_failures=0
while IFS= read -r file; do
    dir=$(dirname "$file")
    while IFS= read -r target; do
        [[ -z $target ]] && continue
        # Off-tree and in-page links are not this check's business.
        [[ $target =~ ^([a-zA-Z][a-zA-Z0-9+.-]*): ]] && continue
        [[ $target == \#* ]] && continue
        target=${target%%#*} # drop an anchor; the path is what must exist
        target=${target%% *} # drop a "path 'title'" suffix
        [[ -z $target ]] && continue
        resolved=$target
        [[ $target != /* ]] && resolved="$dir/$target"
        if [[ ! -e $resolved ]]; then
            printf '   %s -> %s does not exist\n' "$file" "$target" >&2
            link_failures=$((link_failures + 1))
        fi
    done < <(grep -oE '\]\([^)]+\)' "$file" | sed -E 's/^\]\(//; s/\)$//')
done < <(git ls-files '*.md')
if [[ $link_failures -gt 0 ]]; then
    printf 'ci: %d broken relative link(s) in the documentation.\n' "$link_failures" >&2
    exit 1
fi
printf '   %d markdown files, every relative link resolves.\n' "$(git ls-files '*.md' | wc -l)"

# ── Static analysis ─ also BOTH modes ─────────────────────────────
#
# These are here, above the documentation-only exit, because GitHub runs them
# on a documentation-only push: ci.yml calls this script with NO argument, so
# there is no docs mode on that side. Anything cheap enough to run on a typo
# therefore belongs in both modes, or a local green means less than the green
# the pipeline will apply. They cost about a second between them; the compiler
# legs are what --docs exists to skip.

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
if inv7=$(grep -nE '\b(visited|explored|seen|player|team)\b' src/umap/Rooms.h |
    grep -vE ':[[:space:]]*(//|\*)'); then
    printf 'ci: src/umap/Rooms.h carries per-player state, against UTA-0007 INV-7:\n' >&2
    printf '%s\n' "$inv7" >&2
    exit 1
fi
printf '   src/umap/Rooms.h clean.\n'

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

if [[ $MODE == docs ]]; then
    step "documentation-only run complete"
    [[ ${#skipped[@]} -gt 0 ]] && printf '   %d check(s) skipped, listed above.\n' "${#skipped[@]}"
    # GITHUB DOES NOT TAKE THIS MODE -- ci.yml calls this script with no
    # argument -- so what this mode omits is worth naming rather than leaving to
    # be inferred. It is now exactly the compiler legs, and a change matching the
    # documentation glob cannot reach them. Said out loud for the same reason a
    # missing tool is: a green narrower than the pipeline's must not read as the
    # same green.
    printf '   Omitted, and ONLY these: configure, build, test, race detector.\n'
    printf '   GitHub runs the full gate on every push. Reproduce it with\n'
    printf '   scripts/ci.sh and no argument.\n'
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
printf '   compiler: %s\n' "${CXX:-the CMake default}"

configure=(-S . -B "$BUILD_DIR" -G "$GENERATOR")
case $GENERATOR in
    "Visual Studio"* | Xcode | "Ninja Multi-Config") ;; # multi-config: the config is chosen at build time
    *) configure+=(-DCMAKE_BUILD_TYPE="$CONFIG") ;;
esac
cmake "${configure[@]}"

step "build"
cmake --build "$BUILD_DIR" --config "$CONFIG"

step "test"
# The default tier only. UTA_REAL_ASSET_TESTS needs an Unreal Tournament
# install, which no runner and no stranger's clone has -- that separation is
# what S7 is measured on, so the gate must never quietly turn it on.
ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure

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
    cmake -S . -B "$TSAN_DIR" -G "$GENERATOR" \
        -DCMAKE_BUILD_TYPE=Debug -DUTA_SANITIZE=thread
    cmake --build "$TSAN_DIR"
    ctest --test-dir "$TSAN_DIR" --output-on-failure
    printf '   clean under ThreadSanitizer.\n'
fi

step "green"
if [[ ${#skipped[@]} -gt 0 ]]; then
    printf '   ...with %d check(s) skipped:\n' "${#skipped[@]}"
    printf '     - %s\n' "${skipped[@]}"
fi
if [[ -z ${GITHUB_ACTIONS:-} ]]; then
    printf '   This was ONE leg (%s). GitHub runs GCC, Clang and MSVC.\n' \
        "${CXX:-the CMake default}"
fi
