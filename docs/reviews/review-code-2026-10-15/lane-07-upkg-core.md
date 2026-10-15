Lines read (native `Read`, whole files): FileSystem.cpp 596 · FileSystem.h 120 · Jobs.cpp 206 · Log.cpp 169 · Log.h 170 · Md5.cpp 130 · Sha256.cpp 139 · Timing.cpp 65 · Timing.h 77 · Geometry.cpp 491 · Package.cpp 329 · Properties.cpp 334 · Texture.cpp 232 · Texture.h 92.

**Already in my context when I started:** the global `~/.claude/CLAUDE.md` (it describes rule 14, review skills and the Ants verbs), the review-lane agent prompt, and the shared brief at `/home/ants/.cache/uta-scratch/u329-review/shared-context.md`. I had no git snapshot and no project `CLAUDE.md`. Nothing in my brief disagreed with the lane file. I did not read the C++ language standard named in the lane's notes (see INFO).

## Critical (0)

## High (1)

- [dim 4] `FileSystem.cpp:284` — `return fail(ErrorCode::NotFound, "no such file: " + path.string());` — **On Windows, the error messages in this file can throw instead of returning an error.**
  - The file's own comment at `:92-95` says `path::string()` "can also throw on an unconvertible path, which would escape uta::fs against INV-3". The UTA-0229 fix changed the file-opening code (`openNative`, the temp-file name at `:462`) and `discard()`'s `utf8(temporary)`. It did not change the error messages.
  - Every failure message still calls `path.string()`. Sites: `:178` (`relative.string()`, `root.string()`), `:284-320`, `:329-347`, `:398-437`, `:469-473`, `:499`, `:504`, `:522-581`, and `Log.cpp:153` (`fileSink`).
  - Example: a Windows user with a non-ASCII profile name, first run, config file absent. `readFile` reaches `:398`, the message throws `std::system_error`, and the exception leaves `uta::fs`. That breaks UTA-0002 INV-3.
  - The same happens when `resolveUnder` refuses a non-ASCII name sent by a remote server: it throws instead of returning `InvalidArgument`.
  - Uncertainty: this turns on MSVC's `path::string()` throwing on characters the code page cannot hold. I believe it does ("No mapping for the Unicode character exists…"), and the file's comment says so. I did not test it.
  - fix: build every message with `utf8(path)`, as `discard()` already does.

## Medium (2)

- [dim 2] `Properties.cpp:286` — `UTA_TRY(property.value, readValue(reader, package, decodeAs, size));` — **A decoded value can include bytes from the next property.**
  - The decode is not limited to the size the tag declares. `Str` reads `length` bytes (`:157`), and `Vector`/`Rotator` read 12 (`:166-178`), whatever `size` says. Only the rewind-and-skip at `:290-291` uses `size`.
  - So a Vector struct tagged with size 4 returns a Vector3 built partly from the following property's bytes, and reports success. The cursor stays correct, so INV-10's byte count holds, but the value comes from outside the property's own bytes.
  - The reverse case: an over-long `Str` whose read runs past the export's end is refused, even though the declared body fits.
  - fix: decode inside a sub-reader over exactly `readBytes(size)`. Treat a decode that does not fill it, or overruns it, as `MalformedData`, or fall back to returning the raw bytes.
- [dim 3] `Log.cpp:31` — `} else if (c == 0xc2 && next >= 0x80 && next <= 0x9f) {` — **Some terminal control bytes from package names reach the log unescaped.**
  - Only the UTF-8 *spelling* of C1 controls (`0xC2 0x80`–`0xC2 0x9F`) is escaped. A lone byte `0x80`–`0x9F` passes through.
  - Package names are copied byte for byte as Latin-1 (`Package.cpp:107`, `:119`). So a name holding `0x9B` (CSI, a control sequence introducer) reaches stderr and the log file raw.
  - `Log.h:44` promises "each control byte becomes `\xNN`". An 8-bit-C1 terminal, or a legacy Windows console code page, will obey that byte.
  - fix: escape any byte `0x80`–`0x9F` that is not part of a valid UTF-8 sequence.

## Low / Info

- [dim 7] `Package.cpp:134` — `"not an Unreal package: signature is 0x" + std::to_string(signature)` — the signature prints in decimal after a `0x` prefix, so the message misreports what the file holds. fix: `std::format("0x{:08X}", signature)`.
- [dim 2] `Package.cpp:192` / `:202` / `:219` — `UTA_CHECK(reader.seek(nameOffset));` — the seek runs even when the table's count is 0. `checkTable` deliberately accepts any offset for a zero count (`:37-39`), but `seek` then refuses an offset past the end. So a package with an empty import table and a stray offset is refused outright. fix: seek only when `count != 0`. *Whether real packages do this: unknown.*
- [dim 3] `FileSystem.cpp:290` then `:326` — the regular-file check is made on `status()` before `::open`. The `fstat` result at `:332` is never re-checked with `S_ISREG`, and `open` has no `O_NONBLOCK`. A FIFO swapped in between the two blocks the open forever. fix: open with `O_NONBLOCK`, then check `S_ISREG(info.st_mode)`.
- [dim 16] `FileSystem.cpp:405` / `:431` — `std::fread(bytes.data(), 1, bytes.size(), file)` — the size is read once, before the open. A file that grows in between returns its first part as a complete success. fix: after the read, confirm `fgetc(file) == EOF`, or size the file from the open handle.
- [dim 7] `Log.cpp:108` — `const std::lock_guard lock(mutex_);` inside `noexcept` `write()` — `mutex::lock` may throw `std::system_error`, which here means `std::terminate`. That is rare, but INV-3 covers Logger entry points.
- INFO: The C++ language standard named in my lane notes (`~/.claude/standards/languages/cpp.md`) was never opened. No finding was checked against it, so this lane's coverage of that standard is zero.
- INFO: `ByteReader` was opened only for `seek`. I took `readIndex`, `readBytes` and `skip` to be bounds-checked as UTA-0003 INV-1, INV-3 and INV-4 promise, without checking.
- INFO, other dimensions:
  - **2b:** nothing found. `MappedFile`, `isMaskedTexture`, `PhaseTimes::adopt`, `importedPackages`, `md5` and `pathFromUtf8` each have callers outside the tests (in `src/` or `tools/`).
  - **5:** nothing found. `readTable` checks every count before it reserves. The reservation can be up to about 4× the bytes behind it (`Vert`: 2-byte minimum entry against an 8-byte struct), which INV-2 as written allows.
  - **8:** read `Jobs.cpp` and `Timing.cpp` fully; nothing found.
  - **9:** read the atomic-write path; see the permissions question below.
  - **13:** nothing found; `upperAscii` avoids locale rules.
  - **10, 12:** N/A, because my lane notes name no observability contract and no accessibility standard.
  - **15:** nothing leaves the machine in these files.
  - **17:** N/A, because no state file has a schema in these files.

## Covered by spec and looks correct

- `Md5.cpp` and `Sha256.cpp`: padding arithmetic (the buffer is never overrun), byte order, and the compress rounds, checked against the RFC 1321 and FIPS 180-4 sections they cite.
- `Package.cpp`: minimum entry sizes (5 or 6, 7, 12), the subtraction-based serial-range check (INV-8), and the offset read only when size > 0 (INV-7).
- `Geometry.cpp`: every `*_MIN_BYTES` constant, re-summed from the reader functions (43, 16, 17, 30, 25, 11, 62). Every count is checked before reserving (UTA-0069 INV-2).
- `Properties.cpp`: Int/Float size clamp (INV-10), Bool consuming no value bytes, ends only at `None` (INV-9).
- `Texture.cpp`: the WidthOffset cross-check (INV-5) and every count checked before allocation.
- `resolveUnder`: the component check runs before any filesystem call (INV-15), and containment is compared by path element.

## Open questions

- **Name case.** UT names are case-insensitive. Exact-spelling compares go beyond property names: `"None"` (`Properties.cpp:217`), struct names (`:274`), texture class names (`Texture.cpp:31`, `:151`), and `importedPackages`' de-duplication (`Package.cpp:322`). Your known-traps list covers property names, so I have not re-filed these. Does it reach the others?
- **Permissions.** `writeFileAtomically` (`:466`) creates the temporary with the default mode, so the rename drops any mode the user set on the destination. Do any callers write files whose mode matters?
- **Windows device names.** Is `"CON .txt"` (a space before the dot) a device on Windows? If it is, `isReservedDeviceName` (`:148`) misses it.
- **Truncated mapped files.** `MappedFile` leaves "the file must not change while mapped" to its callers. If a mapped file is truncated, the process crashes with SIGBUS. Whether `ubake/Install.cpp:148` meets that rule is outside this lane.

## 3 items to fix first

1. **The `path.string()` messages (High).** These are a contract breach (INV-3) on a common Windows path: a non-ASCII profile name on a first run. They also sit at the trust boundary, `resolveUnder`. The fix is mechanical.
2. **Decoding properties outside their declared bytes (`Properties.cpp:286`).** Untrusted community files can feed wrong values into the bake without any error, and the fix is local.
3. **Lone C1 bytes in `sanitised` (`Log.cpp:31`).** Package names are Latin-1 and attacker-chosen, and this function is the one barrier that protects the user's terminal from them.
