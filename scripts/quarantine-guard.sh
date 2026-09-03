#!/usr/bin/env bash
# The quarantine guard (UTA-0013, partial -- see WHAT IS NOT HERE below).
#
# ADR-0003 and design rule 15: nothing derived from the player's Unreal
# Tournament install is committed or published from this repository. This is
# what makes that true in code rather than in prose. One careless commit is
# permanent in a public history, which is why it runs before every push rather
# than in a review afterwards.
#
# WHAT IT LOOKS AT. The INDEX -- tracked and staged paths -- and never the
# working tree. The working tree holds the player's own install under
# content/ut99/, so a guard scanning it would refuse every push on this
# machine. Under the pre-push hook the index is the pushed commit's tree, so
# looking at the index IS looking at what is about to be published.
#
# WHAT IS NOT HERE. UTA-0013's third check -- no tracked .utab outside
# content/ whose origin is not `authored` -- needs the .utab origin field,
# which does not exist yet. It is UTA-0013's, it is not implemented, and this
# file does not pretend otherwise: a check that did not run must not look like
# one that passed.
set -Eeuo pipefail

cd "$(git rev-parse --show-toplevel)"

IGNORE_FILE=.gitignore
BEGIN='# BEGIN-QUARANTINE-EXTENSIONS'
END='# END-QUARANTINE-EXTENSIONS'

# The extension list lives in .gitignore between two markers and is read from
# there rather than copied here. A second copy drifts, and it drifts silently
# in the direction that matters: the guard passing what git was ignoring.
if ! grep -qxF "$BEGIN" "$IGNORE_FILE" || ! grep -qxF "$END" "$IGNORE_FILE"; then
    printf 'quarantine-guard: %s has no %s / %s markers.\n' "$IGNORE_FILE" "$BEGIN" "$END" >&2
    printf 'quarantine-guard: the extension list could not be read, so NOTHING WAS CHECKED.\n' >&2
    exit 2
fi

mapfile -t EXTENSIONS < <(
    sed -n "/^${BEGIN}\$/,/^${END}\$/p" "$IGNORE_FILE" |
        grep -v '^#' | grep -v '^[[:space:]]*$'
)

if [[ ${#EXTENSIONS[@]} -eq 0 ]]; then
    printf 'quarantine-guard: the marker block in %s is empty — NOTHING WAS CHECKED.\n' "$IGNORE_FILE" >&2
    exit 2
fi

mapfile -t TRACKED < <(git ls-files --cached)

violations=()

for path in "${TRACKED[@]}"; do
    # Check 1: nothing under content/. Everything derived from the install
    # lives there (rule 15), so the directory is the quarantine boundary.
    if [[ $path == content/* ]]; then
        violations+=("$path — under content/, which is never committed (design rule 15)")
        continue
    fi

    # Check 2: no Unreal asset extension, anywhere in the tree. Rule 15 puts
    # derived files under content/; this catches one that landed elsewhere.
    for ext in "${EXTENSIONS[@]}"; do
        # shellcheck disable=SC2053  # the right-hand side is a glob on purpose
        if [[ $path == $ext ]]; then
            violations+=("$path — matches $ext, a quarantined format (ADR-0003)")
            break
        fi
    done
done

if [[ ${#violations[@]} -gt 0 ]]; then
    printf 'quarantine-guard: FAILED — %d tracked path(s) must not be in this repository:\n' "${#violations[@]}" >&2
    printf '  %s\n' "${violations[@]}" >&2
    printf 'quarantine-guard: remove them from the index (git rm --cached <path>) before pushing.\n' >&2
    exit 1
fi

printf 'quarantine-guard: %d tracked paths, none quarantined (%d extensions checked).\n' \
    "${#TRACKED[@]}" "${#EXTENSIONS[@]}"
