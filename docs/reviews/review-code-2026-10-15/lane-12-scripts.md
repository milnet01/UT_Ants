**Line counts (as read):** ccache-windows.sh 29 · check-legs.sh 52 · ci-matrix.sh 226 · ci.sh 387 · class-census.py 483 · docs-only.sh 41 · map-analyser.py 328 · mutation-probe.py 254 · mutations/tilekind.py 39 · mutations/ubundle.py 270 · mutations/ut-paths.py 59 · quarantine-guard.sh 135 · setup-hooks.sh 21 · std-includes.sh 74 · tile-questions.py 147 · windows-gate-reap.sh 43

**What I started with:** the global `~/.claude/CLAUDE.md`, the user's email from the harness, and the review-lane agent prompt. There was no project `CLAUDE.md`, no memory index and no git snapshot. I read UT_Ants' `CLAUDE.md` § Build and test only because the brief named it. This was a depth pass, with no conflict between the brief and the lane file. Every search excluded `tests/**`.

## Critical (0)

## High (0)

## Medium (5)

- **[dim 16] `ci-matrix.sh:54` / `:167`** — `SHA=$(git rev-parse HEAD)` is read before the lock wait at `:59–63`, but `state_before=$(tree_state)` is only taken at `:167`.
  - **Problem:** in the in-place checkout (`ants.gate.inPlace`), a commit or edit made while this gate waits on another one's lock goes undetected.
  - **What happens:** the Linux legs build the new HEAD. The Windows leg checks out the stale `$SHA` from a bundle made of the *current* HEAD (`:115`). The comparison at `:188` then passes.
  - **Contract:** this is exactly the "tree changed mid-run" trap that `CLAUDE.md` says the gate catches.
  - **Fix:** take `state_before` next to `SHA`, before `flock`, and also fail if `git rev-parse HEAD != $SHA` after the legs.
- **[dim 16] `ci-matrix.sh:116–117`** — `scp -q "${alive[@]}" "$bundle" "$WIN_HOST:uta-gate.bundle" || exit 255 # the link, not the build`.
  - **Problem:** every scp failure is reported as a lost link. That includes a full Windows disk, a permissions error, or a missing home directory.
  - **What happens:** the leg reads `NOT RUN (connection lost)` and the push goes through. A broken Windows machine drops MSVC from every push.
  - **Contract:** `:98–100` says a fault that "would drop MSVC from every push in silence … fails the gate".
  - **Fix:** use exit 255 only when scp itself returned 255. Let any other status fall through to RED.
- **[dim 2] `windows-gate-reap.sh:38`** — `awk '... { print $1, $4, substr(...) }'`.
  - **Problem:** the field positions assume `ps -W` rows always start with the PID. As far as I know, Cygwin/MSYS `ps` puts a one-letter status flag first on some rows: `I` (waiting for input), `S` (stopped), `O` (waiting for output).
  - **What happens:** on those rows `$1` is the letter and `$4` is the PGID. A leftover bash blocked reading the dropped ssh's stdin (the very case `:6–7` describes) would then never be reaped.
  - **Doubt:** I could not run `ps -W` here, so this is unconfirmed.
  - **Fix:** strip a leading single-letter field before taking the columns.
- **[dim 7] `map-analyser.py:270–272`** — `subprocess.run([... "ut-shot" ...], input=..., capture_output=True, text=True)`. The return code is thrown away.
  - **Problem:** if ut-shot fails, no views are written, so `read_views` returns no sky.
  - **What happens:** `:283` records `skyPixels: 0, class: "none"` with `why: "measured"`. A tool failure is reported as a measured "no sky" verdict, which then decides that the map gets no sun.
  - **Fix:** on a non-zero return, set `row["error"]` the same way the ut-dump path does.
- **[dim 2] `std-includes.sh:66`** — `grep -oE 'std::[a-z_0-9]+[[:space:]]*[(<]'`.
  - **Problem:** `std::ranges::sort(` and `std::ranges::stable_sort(` never match this pattern. The project uses those calls (the tilekind mutation names `std::ranges::stable_sort` in `src/ubake/Bake.cpp`), and they need `<algorithm>` on MSVC just as much.
  - **What happens:** a missing include of exactly the kind this script exists to catch (`:5–10`) goes through.
  - **Fix:** allow an optional `ranges::` segment and capture the name after it.

## Low / Info

- **[dim 2] `ci.sh:134`** — `if [[ ! -e $resolved ]]` checks the working tree, not git's index.
  - **What happens:** in the in-place gate, a link to an ignored or untracked file (anything under `content/ut99/`, or a `build*/` path) resolves here. GitHub's fresh checkout then fails the same `ci.sh`.
  - **Fix:** test against `git ls-files`, including the prefix of a linked directory.
- **[dim 2] `ci.sh:138`** — `grep -oE '\]\([^)]+\)'`. Reference-style links (`[x]: path`) are never checked, yet `:144` prints "every relative link resolves". Fenced code containing `](` gives false failures.
- **[dim 2] `std-includes.sh:37`** — `add algorithm ... remove remove_if ...`. A `std::remove(path)` call from `<cstdio>` is told it needs `<algorithm>`. This fails closed, so the gate goes red on correct code.
- **[dim 2] `check-legs.sh:36`** — `grep -q '"Windows (MSVC): green"' "$wrapper" && echo msvc`. This proves only that a string literal exists, not that a Windows leg runs. Fix: match the `ssh … ci.sh` invocation instead.
- **[dim 5] `class-census.py:108–110`** — `length = cursor.index()` … `cursor.skip(length)`.
  - **Problem:** a negative compact index moves the cursor backwards, and `skip` checks only the upper bound.
  - **What happens:** a malformed package (community files are untrusted) can loop up to `name_count` (a u32) times without advancing, building an unbounded list. That is a hang or out-of-memory.
  - **Fix:** in `skip`, raise when `count < 0` or `pos < 0`.
- **[dim 7] `map-analyser.py:210`** — `raise ValueError(...)` is not caught in `analyse`/`main`. One bad bundle ends the whole batch, where a failed ut-dump gets an error row. Fix: catch it and emit `{"map","error"}`.
- **[dim 2] `map-analyser.py:269`** — `os.path.exists(f"{prefix}-{len(cams) - 1}.ppm")`. Views from an earlier bake of the same map name are reused silently after a re-bake with a new recipe. The docstring does not mention this cache.
- **[dim 9] `tile-questions.py:142`** — `(out / "answers.txt").write_text(...)`. Re-running into the same out directory overwrites an `answers.txt` the user has already edited but not yet appended. Fix: refuse, or write `answers-<n>.txt`.
- **[dim 9] `mutation-probe.py:153`** — `path.write_bytes(original)` is a truncate-then-write restore. A SIGTERM handled during the `finally` (the handler at `:161` raises `SystemExit` anywhere) can leave a truncated source file. That loses any uncommitted edits in it. Fix: temp file plus `os.replace`.
- **[dim 2] [tool: ruff PTH]** — `class-census.py:358–373` and `map-analyser.py:137` use `os.path` where `python.md` § Idioms prefers `pathlib`.
- **INFO:** `std-includes.sh` scans `*.cpp`/`*.h` only. I did not check whether the tree has `.hpp`/`.cc`/`.inl` files.

## Covered by spec and looks correct

- **`quarantine-guard.sh` check 3:** `tools/ut-origin/Cli.cpp` exits 1 for "derived" and "unreadable", 0 only for authored. The guard therefore meets UTA-0008 § 4.5's direction (refuse anything but authored).
- **Bundle offsets in the Python scripts:** both scripts read the header and section table to UTA-0008 §§ 4.3–4.4. That is the count at 12, 24-byte descriptors from 16, offset at +4 and size at +12.
- **`check-legs.sh`:** its awk parse fits `ci.yml`'s `include:` block (`- name:` / `os:` / `cc:` / `cxx:`).
- **`.gitignore`:** `/build*/` covers every gate build directory, including the `-tsan` ones, so `tree_state` is not polluted.
- **`setup-hooks.sh`:** it sets the five settings `build-and-test.md` names.
- **`ci-matrix.sh` Windows outcomes:** the completion-marker logic fails closed when the status is 0 but no marker arrived.
- **Mutation tags:** every invariant tag in the three mutation lists exists in its spec.
- **`ccache-windows.sh`:** it verifies the SHA-256 before extracting.

## Open questions

- **Answers-file path:** `tile-questions.py:48` ignores a relative `XDG_DATA_HOME`. The C++ `envPath` I saw (`FileSystem.cpp:70–80`) treats only an empty value as unset. I did not read past line 80 to see whether it rejects a relative path. If it does not, the two disagree on where the file lives.
- **Hash case:** `tile-questions.py:61/113` lowercases the file's hashes but not the report's `q["hash"]`. Does ut-bake always print lowercase?
- **Docs-only `*.md` paths:** `docs-only.sh` counts any `*.md` as documentation. Does any test read a `.md` fixture or a spec under `docs/`? If so, a docs-only push skips the test that reads it.
- **CRLF `.gitignore`:** on a CRLF checkout the quarantine markers would not match with `grep -x`. That fails closed (exit 2). Is `.gitignore` pinned to LF by `.gitattributes`?

## 3 items to fix first

1. **The `ci-matrix.sh` SHA and tree-snapshot race.** The gate can approve a commit other than the one being pushed, and that is the gate's core promise.
2. **The `ci-matrix.sh` scp → 255 mapping.** A broken Windows machine silently drops the MSVC leg from every push, against the script's own misconfiguration rule.
3. **The `std-includes.sh` `std::ranges::` gap.** It is the check's own target defect class, and the code already uses ranges calls.
