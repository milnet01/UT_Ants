# stb — vendored

`stb_image_write.h`, the PNG encoder `uta_core`'s `core/Png.h` wraps.
`UTA-0191` needs the viewer's capture folder to hold a picture the user can
open, and nothing in this tree wrote PNG before it.
`docs/standards/dependency-acquisition.md` § 2 question 4 is why it is
vendored rather than fetched — stb ships no build system at all, so there is
nothing whose build could produce a library to link. That is the same branch
`third_party/bc7enc/` and Dear ImGui take.

Only `stb_image_write.h` is vendored. The rest of stb — the loader, the
truetype rasteriser, the rest of the collection — is not copied, because
nothing here reads an image or rasterises a glyph.

## Upstream

- **Repository:** <https://github.com/nothings/stb>
- **Commit:** `2c980bb59875b0d32144a71867fbdebb2f77cd20` (2026-08-02)
- **Header version:** `stb_image_write - v1.16`
- **Retrieved:** 2026-09-20

A vendored copy *is* its own pin, so there is no version to bump; this file
is what says which copy it is. Nothing in the build reads it — neither
`scripts/ci.sh` nor `.githooks/pre-push` mentions `third_party` — so whether
this copy has gone stale is a question somebody asks by hand.
`docs/standards/dependency-acquisition.md` § 4 admits that in as many words.

### What landed, and its checksum

    cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05  stb_image_write.h
    bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63  LICENSE

## Licence

MIT **or** the Unlicense, at our choice — either of which this project's
GPL-3.0 `LICENSE` absorbs. The upstream text is in `LICENSE` beside this
file, and the header repeats the same dual grant at its own end.

## How it is compiled, and what is switched off

`src/core/Png.cpp` is the single translation unit that defines
`STB_IMAGE_WRITE_IMPLEMENTATION`. It also defines:

- `STBI_WRITE_NO_STDIO`, so the copy declares no `FILE`-taking entry point.
  Every write in this project goes through `core/FileSystem.h`, which reports
  an `Error` rather than setting `errno`, and a second file-writing path
  would report failures a different way.
- `STBIW_ASSERT(x) ((void)0)`, so a malformed call cannot `abort()` the
  client. `writePng` validates its own arguments and returns an `Error`
  instead.

## The maths, and why it does not reach `ADR-0002`

`docs/design.md`'s Determinism bullet rules out *"no platform maths library
in the simulation or the baker"*. This copy is in neither: it is called by
the viewer's capture path, which writes a debug folder and contributes
nothing to a bundle. So `ADR-0002`'s ground — one map, recipe and baker
version hashing to one bundle on any machine — is not in question here.

**It is worth stating anyway, because the answer is stronger than the rule
needs.** Measured over the vendored copy at the commit above, the only libm
names appearing anywhere in the file are `ceilf` and `floorf`, and both
occurrences sit inside a **commented-out** line of the JPEG path, beside
upstream's own note that they are *"not needed here anyway"*. So the
compiled copy calls no maths library function at all, and the PNG path —
the only path this project uses — is integer throughout.
