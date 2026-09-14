# AMD FidelityFX Super Resolution 1 — vendored

FSR 1's shader headers: `ffx_a.h`, AMD's portability layer, and `ffx_fsr1.h`,
which holds EASU (the upscale) and RCAS (the sharpen). urender's
`fsr_easu.frag` and `fsr_rcas.frag` include them, and glslc compiles them into
the renderer at build time — `ROADMAP UTA-0154`.
`docs/standards/dependency-acquisition.md` § 2 question 4 is why this is
vendored rather than fetched: upstream ships headers and a sample
application, and no build system producing anything a target links.

## Upstream

- **Repository:** <https://github.com/GPUOpen-Effects/FidelityFX-FSR>
- **Release:** `v1.0.2`, at commit `a21ffb8f6c13233ba336352bdff293894c706575`
- **Retrieved:** 2026-09-14, from `ffx-fsr/` and the repository root

A vendored copy is its own pin, so there is no version to bump. This file
says which copy it is. Nothing in the build reads it — neither
`scripts/ci.sh` nor `.githooks/pre-push` mentions `third_party` — so whether
this copy has gone stale is asked by hand, as § 4 of that standard says.

### What landed, and its checksum

    f60e2722fcd13989523b9164d776ab382b3692791767f3bf8bb19967f763f3fb  ffx_a.h
    93c3922362ea7fc99cbcc698ca30c98de4f8c246d1fbb0b09e015ddef38ce3a5  ffx_fsr1.h
    db089274ce766da70f5b7d791029c3486f9f9e27c8c79c652689603d3192e802  license.txt

The files are byte-identical to upstream. The sample application is not
vendored: nothing here runs it.

## Licence

MIT, in `license.txt` beside this file, which this project's GPL-3.0
`LICENSE` absorbs.
