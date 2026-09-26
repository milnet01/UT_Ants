# Static-analysis classes that are not defects here

`check-code` reads this file through `audit-config.json` beside it, and
moves a finding out of the actionable list when it matches an entry's
tool and rule, within the entry's paths. A finding that matches nothing
here is reported as found.

Each entry was decided on 2026-09-26 by reading the findings against the
source, not by count. A one-off false positive is not listed here: it
goes in `.audit_cache/learned-fp.jsonl` through `audit_dismiss`.

**This file quiets a class. It never switches a check off.** A rule not
listed still reports everywhere, and a listed rule still reports outside
its paths.

| Tool | Rule | Paths | Why it is not a defect here |
|---|---|---|---|
| any | any | `third_party/**` | Vendored code (bc7enc, FSR1). Fixed upstream or not at all. |
| clazy | `clazy-non-pod-global-static` | `**` | A Qt start-up-cost check. This project uses no Qt; its non-POD statics are tables and loggers. |
| cppcheck | `normalCheckLevelMaxBranches` | `**` | cppcheck's own notice that it limited branch analysis, not a finding. |
| cppcheck | `useStlAlgorithm` | `**` | A style suggestion to swap a loop for an algorithm. |
| cppcheck | `shadowFunction` | `**` | A local named like a free function elsewhere. Style; no call is misresolved. |
| cppcheck | `uninitMemberVarNoCtor` | `src/urender/ShaderTypes.h` | GPU mirror structs of the shader layout. Every one is value-initialised with `{}` before it is filled. |
| clang-tidy | `bugprone-unchecked-optional-access` | `tests/**` | Catch2's `REQUIRE` stops the test before the access, which the check cannot see. |
| clang-tidy | `bugprone-unchecked-optional-access` | `src/urender/Frame.cpp`, `src/urender/Probes.cpp`, `src/ubake/Bake.cpp`, `src/ubake/Movers.cpp`, `src/ubundle/Bundle.cpp`, `apps/ut-ants/Launcher.cpp`, `tools/ut-bake/Cli.cpp`, `tools/ut-paths/Cli.cpp`, `tools/ut-paths/Seeds.cpp` | Guarded by an invariant the check cannot see: the renderer's members exist once it is built, and each tool validates its arguments before use. Read site by site on 2026-09-26. |
| clang-tidy | `bugprone-signed-bitwise` | `**` | Byte and flag packing for the file formats and the GPU; the operands are never negative. |
| clang-tidy | `bugprone-narrowing-conversions` | `**` | Integer-to-float conversions in pixel, grid and coordinate maths, on values far inside float's exact range. Spot-checked on 2026-09-26. |
| clang-tidy | `bugprone-implicit-widening-of-multiplication-result` | `**` | Size products of small image and grid dimensions. |
| clang-tidy | `bugprone-throwing-static-initialization` | `**` | Static tables and loggers built at start-up. A throw there ends the process, which is correct. |
| clang-tidy | `bugprone-invalid-enum-default-initialization` | `**` | Vulkan info structs zeroed with `{}` before their fields are set, as the Vulkan API expects. |
| ruff | `E501` | `scripts/**` | Line length. The project has no ruff config and no Python line limit. |
