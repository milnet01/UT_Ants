#!/usr/bin/env bash
# Is a change documentation-only? The ONE list of what counts, read by both
# gates (local-gate.md § 6.1 and § 9): the machine-wide pre-push hook runs it
# as ants.gate.docsCommand, and .github/workflows/ci.yml runs it over a push's
# range. Two copies of the list would drift, and a drifted list lets a code
# change through as "documentation".
#
#   printf '%s\n' <paths> | scripts/docs-only.sh
#
# Exit 0: every path is documentation, so scripts/ci.sh --docs is enough.
# Exit 1: anything else -- including no paths at all, which means the range
# was not known, and unknown runs everything.
set -euo pipefail

# Build files are never documentation, whatever the list below says. The same
# names the machine-wide hook refuses, so GitHub refuses them too.
never='CMakeLists.txt|*.cmake|*.lock|*.pro|*.pri|requirements*.txt|constraints*.txt|Makefile|meson.build'
docs='docs/*|*.md|LICENSE'

IFS='|' read -r -a nevers <<<"$never"
IFS='|' read -r -a globs <<<"$docs"

seen=0
while IFS= read -r path; do
    [[ -z $path ]] && continue
    seen=$((seen + 1))
    for n in "${nevers[@]}"; do
        # shellcheck disable=SC2053  # the right-hand side is a pattern on purpose
        [[ $path == $n || ${path##*/} == $n ]] && exit 1
    done
    matched=false
    for g in "${globs[@]}"; do
        # shellcheck disable=SC2053  # the right-hand side is a pattern on purpose
        [[ $path == $g ]] && {
            matched=true
            break
        }
    done
    $matched || exit 1
done
[[ $seen -gt 0 ]]
