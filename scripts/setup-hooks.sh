#!/usr/bin/env bash
# Run once in every clone: ./scripts/setup-hooks.sh
#
# Git keeps these settings in .git/config, and no clone inherits them
# (`local-gate.md` § 2). Without them the hooks in .githooks/ never run,
# and without ants.gate.command the machine-wide hook runs no gate at all:
# its fallback list does not contain scripts/ci-matrix.sh. UTA-0231.
set -Eeuo pipefail
cd "$(dirname "$0")/.."

git config core.hooksPath .githooks
git config ants.gate.command ./scripts/ci-matrix.sh
git config ants.gate.docsMode --docs
git config ants.gate.docsGlob 'docs/*|*.md|LICENSE'
