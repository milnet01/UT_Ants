#!/usr/bin/env bash
# UTA-0013: the quarantine guard's third check, run for real -- a throwaway git
# repository holding a copy of the guard and its .gitignore block, bundles
# staged into its index, and the ut-origin binary the build produced.
#
# Usage: quarantine-guard-test.sh <source-dir> <ut-origin>
set -Eeuo pipefail

SRC=$1
TOOL=$2
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Read from the header that defines it, so this test is not a second copy of
# the format version that goes stale at the next bump.
VERSION=$(sed -nE 's/^inline constexpr std::uint32_t FORMAT_VERSION = ([0-9]+);$/\1/p' \
    "$SRC/src/ubundle/Bundle.h")
if [[ -z $VERSION ]]; then
    printf 'could not read FORMAT_VERSION from src/ubundle/Bundle.h\n' >&2
    exit 2
fi

byte() { printf '%b' "\\x$(printf %02x "$1")"; }

# A sixteen-byte header -- docs/specs/UTA-0008-bundle-container-and-origin.md
# SS 4.3: magic, version (u32 little-endian), origin, kind, reserved, and a
# section count of zero. $1 is the origin byte.
header() {
    printf 'UTAB'
    byte $((VERSION & 255))
    byte $(((VERSION >> 8) & 255))
    byte 0
    byte 0
    byte "$1"
    for _ in 1 2 3 4 5 6 7; do byte 0; done
}

repo=$WORK/repo
mkdir -p "$repo/scripts"
cp "$SRC/scripts/quarantine-guard.sh" "$repo/scripts/"
cp "$SRC/.gitignore" "$repo/.gitignore"
git -C "$repo" init -q

# $1 a path; the bytes arrive on standard input.
stage() {
    mkdir -p "$repo/$(dirname "$1")"
    cat >"$repo/$1"
    git -C "$repo" add -f -- "$1"
}
unstage() {
    git -C "$repo" rm -q --cached -f -- "$1"
    rm -f "$repo/$1"
}

failures=0
# $1 the exit status wanted, $2 what the case shows; the rest go to the guard.
expect() {
    local want=$1 what=$2 got=0
    shift 2
    (cd "$repo" && ./scripts/quarantine-guard.sh "$@") >"$WORK/out" 2>&1 || got=$?
    if [[ $got -eq $want ]]; then
        printf 'ok: %s\n' "$what"
    else
        printf 'FAIL: %s -- exit %s, wanted %s\n' "$what" "$got" "$want" >&2
        sed 's/^/  | /' "$WORK/out" >&2
        failures=$((failures + 1))
    fi
}

expect 0 "no bundle is tracked" --origin-tool "$TOOL"

header 1 | stage maps/authored.utab
expect 0 "an authored bundle outside content/ passes" --origin-tool "$TOOL"

header 0 | stage maps/derived.utab
expect 1 "a derived bundle outside content/ is refused" --origin-tool "$TOOL"
unstage maps/derived.utab

printf 'UTAB' | stage maps/short.utab
expect 1 "a bundle whose header cannot be read is refused" --origin-tool "$TOOL"
unstage maps/short.utab

# The INDEX is what is published, not the working tree.
header 0 >"$repo/maps/authored.utab"
expect 0 "the staged bytes are read and not the working tree's" --origin-tool "$TOOL"
header 1 >"$repo/maps/authored.utab"

expect 0 "without ut-origin the check does not run and the guard says so"
if ! grep -q 'NOT RUN' "$WORK/out"; then
    printf 'FAIL: the guard did not say the origin check was not run\n' >&2
    failures=$((failures + 1))
fi

expect 2 "an ut-origin that does not exist checks nothing and fails" \
    --origin-tool "$WORK/no-such-ut-origin"

if [[ $failures -gt 0 ]]; then
    printf '%d case(s) failed\n' "$failures" >&2
    exit 1
fi
