# Build and test: the push gate and the tools

> **Purpose — the detail behind `CLAUDE.md` § Build and test, read when a
> session needs it rather than at every start.** `CLAUDE.md` keeps the build
> commands and the rules every session needs.

## The push gate

`.githooks/pre-push` runs neither directly. It delegates to
`$ANTS_GLOBAL_HOOKS/pre-push` whenever that variable is set to a
**non-empty** value — the hook uses `${ANTS_GLOBAL_HOOKS:-...}`, so an
empty one falls back exactly as an unset one does — else
`~/.claude/githooks/pre-push`, and the resolved path must be
executable. A set-but-wrong value is not corrected, it just disables the
gate. The machine-wide hook then picks the
gate script, decides documentation-only, and runs it over the pushed
commit. With `ants.gate.inPlace` that is the real checkout when it is clean
at the pushed commit, so the build trees stay warm (`UTA-0238`); otherwise a
detached worktree.

**Five settings drive that. Four are `ants.gate.*` and live only in
`.git/config`, so a clone has none of them. `core.hooksPath` is the
exception** — ~/.gitconfig sets it machine-wide to ~/.claude/githooks, and the
repository value overrides it. So unsetting the repository value
does **not** disable the gate: it falls back to the machine-wide hook,
losing this repository's own hooks rather than the push gate.

`./scripts/setup-hooks.sh` sets all five; run it once in every clone
(`UTA-0231`).

**A green push is not evidence the gate ran.** Four ways it passes having
checked nothing, and only two announce themselves: `NOTHING WAS CHECKED`
(the resolved hook is missing), a line naming a pipeline but no local gate
(`ants.gate.command` unset — the hook's fallback list does not contain
`scripts/ci-matrix.sh`), **no hook output at all**, which on this machine
means `core.hooksPath` naming a directory with no `pre-push` rather than
being unset, and **`.githooks/pre-push` not being executable** — git skips
a non-executable hook in silence.
An unset `docsCommand` is silent too, and falls back to the hook's own
list, which is wider. `scripts/docs-only.sh` is the one list of what counts
as documentation; GitHub reads it too (`UTA-0236`).

**Config alone cannot answer this, because the mode is not config.** Check
both:

```sh
git config --get-regexp 'hooksPath|^ants\.gate\.'
test -x .githooks/pre-push && echo "hook executable" || echo "HOOK NOT EXECUTABLE"
```

**The push gate runs GitHub's three legs; a bare `ci.sh` runs one.** A
bare run uses whatever `CXX` resolves to, and says which at the start and
the end. The gate needs `gcc14-c++` and `clang19` installed, and the SSH
alias `wintest-gate`, which logs in to the Windows machine as a NON-ADMIN
account. Elevated, the Vulkan loader ignores `VK_DRIVER_FILES`, and
INV-5's no-driver tests then find that machine's GPU and fail. **When the
machine is unreachable the gate prints `WINDOWS LEG NOT RUN` and lets the
push go** (user decision, 2026-09-25), so a green push can still lack the
MSVC leg.

**A `cancelled` CI run is not a failure.** `.github/workflows/ci.yml`
sets `cancel-in-progress`, so each push cancels the run still in flight
and its jobs render as ✗. Check the run whose `headSha` is HEAD:

```sh
gh run list --limit 5 --json headSha,conclusion
```

## Tools and sanitizer builds

**`ccache` and `mold` are used if installed and ignored if not**, and change
nothing about the output. ccache needs two settings before it helps across
build directories — untold, it hashes the build path into the key and mostly
misses:

```sh
ccache --set-config base_dir=/
ccache --set-config hash_dir=false
```

**`ut-bench` says which step of a bake took the time** (`UTA-0129`). It bakes
each map several times, never from a cache, and prints each step's smallest,
median and largest time beside the machine and the build:

```sh
build/tools/ut-bench/ut-bench bake --install <install> --scratch <dir> <map>...
```

`--scratch` must be on a real disk; a bundle is large and `/tmp` is memory
here. Compare on the smallest figure. It warns when the build is not Release
or the machine is busy, and it is never a gate.

**`ut-bench frame` times frames with no window**, still at each camera and
moving between them, on this machine's GPU:

```sh
build/tools/ut-bench/ut-bench frame --cameras <file> --tier ultra --size 3840x2160 <bundle>
```

A capture folder's `camera.txt` is one camera line. Compare on the median and
the 99th percentile.

**`ut-compare` puts the original game's frame beside ours, from the same
camera** (`UTA-0306`). It runs the original client headless, then draws ours
with `ut-shot`:

```sh
python3 -I tools/ut-compare/ut-compare.py --install <install> --capture <viewer capture folder> --size 1280x720
```

`--map <name> --cameras <file>` takes `ut-shot` camera lines instead, and
`--help` lists the rest. It needs Xvfb, bwrap and ImageMagick. **It must never
write to the install.** The client runs from a copy in
`~/.cache/ut-ants/compare/`, under bwrap with the install read-only. Wayland
is hidden from it, so no window reaches the desktop. The run fails if a file
in the install's `System` directories changed. The original's lights pulse on
their own clock; ours are pinned at time 0.

**`ut-ref` measures our light against exact light** (`UTA-0292`). It traces
UTA-0112's light model with the probe bake's own code, and scores
`ut-shot`'s light terms against it, per 8×8 block:

```sh
build/tools/ut-bake/ut-bake --install <install> --out <dir> --light-materials light.txt <map>
build/tools/ut-shot/ut-shot --light-terms --light-time 0 --tier ultra <bundle> 320 180 shot < cameras
build/tools/ut-ref/ut-ref trace <bundle> light.txt 320 180 128 4 ref < cameras
build/tools/ut-ref/ut-ref score ref shot 320 180 <views>
```

The material light file must come from the bake that wrote the bundle;
`trace` says how many of the bundle's materials it names. The trace keeps
every core but two busy. `docs/specs/UTA-0292-reference-path-tracer.md`
says what each column means.

Two options worth knowing. `-DUTA_SANITIZE=thread` builds under
ThreadSanitizer, which is how the job system's thread-safety is
measured; the gate runs it as its own step on Linux, and refuses on
MSVC, which has no ThreadSanitizer. `-DUTA_REAL_ASSET_TESTS=ON` with
`-DUTA_UT_INSTALL_DIR=<path>` adds the second test tier, off by default
so a clone with no Unreal Tournament still builds and tests clean —
that separation is what **S7** is measured on. On this machine add
`-DUTA_REFERENCE_INSTALL=ON`: some of the tier's figures were measured on
the reference install and are asserted only there (`UTA-0132`). Off, they
are printed and not asserted.

**There is no `UTA_SANITIZE=address`.** That option takes `''` or
`'thread'` and refuses anything else with a `FATAL_ERROR`, so reach for
the flags directly in a build directory of their own:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined,float-cast-overflow"
```

Address and thread cannot share a binary, which is why this is a separate
directory rather than a flag on the gate. **GCC's `undefined` leaves out
`float-cast-overflow`**, so it must be named (UTA-0217). **A UBSan report
does not fail the run either**: set `UBSAN_OPTIONS=halt_on_error=1`, or a
test prints `runtime error` and still passes.
