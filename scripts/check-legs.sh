#!/usr/bin/env bash
# Do the local gate and GitHub test the same compilers? (UTA-0232)
#
# scripts/ci-matrix.sh loops legs over scripts/ci.sh, the script
# .github/workflows/ci.yml calls, so it is a wrapper rather than a mirror --
# but only while a check holds its leg list to the workflow's
# (local-gate.md § 3). This is that check. It compares COMPILERS, not display
# names: each Linux leg as "cc/cxx", the Windows leg as "msvc".
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

workflow=.github/workflows/ci.yml
wrapper=scripts/ci-matrix.sh

# ci.yml: every matrix.include entry, from its "- name:" to the next.
github=$(awk '
    /^ *include:/ { inc = 1; next }
    inc && /^ *steps:/ { inc = 0 }
    !inc { next }
    function flush() {
        if (!seen) return
        if (os ~ /^windows/) print "msvc"; else print cc "/" cxx
    }
    /^ *- name:/ { flush(); seen = 1; os = cc = cxx = ""; next }
    /^ *os:/ { os = $2 }
    /^ *cc:/ { cc = $2 }
    /^ *cxx:/ { cxx = $2 }
    END { flush() }
' "$workflow" | sort)

# ci-matrix.sh: every run_leg line's CC and CXX, and its Windows leg.
local_legs=$(
    {
        grep -E '^run_leg ' "$wrapper" |
            sed -nE 's/.*CC=([^ ]+) CXX=([^ ]+).*/\1\/\2/p'
        grep -q '"Windows (MSVC): green"' "$wrapper" && echo msvc
    } | sort
)

if [[ -z $github || -z $local_legs ]]; then
    # An empty side means a parse that no longer fits the file, not agreement.
    printf 'check-legs: read no legs from %s or %s -- the parse no longer fits.\n' \
        "$workflow" "$wrapper" >&2
    exit 2
fi
if [[ $github != "$local_legs" ]]; then
    printf 'check-legs: the local gate and GitHub test different compilers.\n' >&2
    diff <(printf '%s\n' "$github") <(printf '%s\n' "$local_legs") |
        sed -e 's/^</   GitHub only:/' -e 's/^>/   local only: /' | grep -E 'only' >&2
    exit 1
fi
printf '   the same legs on both sides: %s\n' "$(printf '%s' "$github" | tr '\n' ' ')"
