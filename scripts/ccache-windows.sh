#!/usr/bin/env bash
# Fetch the pinned ccache for Windows into <dir> and print its directory.
# ONE copy of the version and checksum, used by both Windows legs (UTA-0235):
# GitHub's (.github/workflows/ci.yml) and the local gate's on the Windows test
# machine (scripts/ci-matrix.sh). With ccache on PATH, CMakeLists.txt routes
# MSBuild through it and turns precompiled headers off, so a leg WITHOUT it
# builds differently -- which is how 5b23ec5 passed locally and failed on
# GitHub. Keep the legs on the same route by calling this in both.
#
#   dir=$(scripts/ccache-windows.sh "$HOME/.cache/uta-ccache")
#
# Idempotent: an existing, matching download is reused. Git Bash on Windows.
set -euo pipefail

VERSION=4.14.1
SHA256=6219f3865ca59aec41ee4b678df171d5d35855ecb2b6dbbbd20690b3a68af7b4

root=${1:?usage: ccache-windows.sh <install-dir>}
name="ccache-$VERSION-windows-x86_64"
if [[ ! -x $root/$name/ccache.exe ]]; then
    mkdir -p "$root"
    curl -fsSL -o "$root/$name.zip" \
        "https://github.com/ccache/ccache/releases/download/v$VERSION/$name.zip"
    echo "$SHA256  $root/$name.zip" | sha256sum -c - >&2
    rm -rf "${root:?}/$name"
    unzip -q "$root/$name.zip" -d "$root"
    rm -f "$root/$name.zip"
fi
printf '%s\n' "$root/$name"
