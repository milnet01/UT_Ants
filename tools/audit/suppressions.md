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
| clang-tidy | `bugprone-unchecked-optional-access` | `src/urender/Frame.cpp`, `src/urender/Probes.cpp`, `src/ubake/Bake.cpp`, `src/ubake/Movers.cpp`, `src/ubundle/Bundle.cpp`, `apps/ut-ants/Launcher.cpp`, `tools/ut-bake/Cli.cpp`, `tools/ut-paths/Cli.cpp`, `tools/ut-paths/Seeds.cpp`, `src/ubake/ShadowMask.cpp`, `src/ubundle/FlameSection.cpp`, `src/ubundle/LampSection.cpp`, `src/ubundle/MaterialSection.cpp`, `src/urender/Lights.cpp`, `tools/ut-bench/Cli.cpp`, `tools/ut-bench/Frame.cpp` | Guarded by an invariant the check cannot see: the renderer's members exist once it is built, and each tool validates its arguments before use. Read site by site on 2026-09-26, and the later paths on 2026-10-16. |
| clang-tidy | `bugprone-signed-bitwise` | `**` | Byte and flag packing for the file formats and the GPU; the operands are never negative. |
| clang-tidy | `bugprone-narrowing-conversions` | `**` | Integer-to-float conversions in pixel, grid and coordinate maths, on values far inside float's exact range. Spot-checked on 2026-09-26. |
| clang-tidy | `bugprone-implicit-widening-of-multiplication-result` | `**` | Size products of small image and grid dimensions. |
| clang-tidy | `bugprone-throwing-static-initialization` | `**` | Static tables and loggers built at start-up. A throw there ends the process, which is correct. |
| clang-tidy | `bugprone-invalid-enum-default-initialization` | `**` | Vulkan info structs zeroed with `{}` before their fields are set, as the Vulkan API expects. |
| ruff | `E501` | `scripts/**`, `tools/ut-compare/**` | Line length. The project has no ruff config and no Python line limit. |
| cppcheck | `comparisonOfFuncReturningBoolError` | `tests/**` | Catch2's `CHECK` on a call returning bool, which cppcheck reads as a comparison. Spot-checked 2026-10-16. |
| clang-tidy | `bugprone-easily-swappable-parameters` | `**` | Geometry and fixture functions take several points or paths by design. The check flags the signature's shape, not a misuse. Spot-checked 2026-10-16. |

Read site by site on 2026-10-16 (`UTA-0374`); each row names the files read.

| Tool | Rule | Paths | Why it is not a defect here |
|---|---|---|---|
| cppcheck | `returnDanglingLifetime` | `src/upkg/Class.cpp`, `src/ubake/Bake.cpp`, `tools/ut-dump/Cli.cpp`, `tests/**` | A pointer into `Package::exports()`, a span over the package's own storage. It lives as long as the package. |
| cppcheck | `uninitMemberVarNoCtor` | `src/umat/Library.h`, `src/umat/Flames.h`, `src/ubake/Geometry.cpp`, `src/urender/Device.cpp`, `src/urender/Materials.cpp`, `src/urender/Pipelines.cpp`, `src/upkg/Package.h`, `tools/ut-paths/Seeds.cpp`, `tools/ut-bench/Cli.cpp`, `tests/**` | Aggregates built only from full brace initialisers, or members already given a default. |
| cppcheck | `passedByValue` | `tests/**` | Small test values; constants pinned to UT99's on purpose; `x / x` tested on purpose; a case struct uploaded to a shader as bytes. |
| cppcheck | `useInitializationList` | `tests/**` | Small test values; constants pinned to UT99's on purpose; `x / x` tested on purpose; a case struct uploaded to a shader as bytes. |
| cppcheck | `knownConditionTrueFalse` | `tests/**` | Small test values; constants pinned to UT99's on purpose; `x / x` tested on purpose; a case struct uploaded to a shader as bytes. |
| cppcheck | `duplicateExpression` | `tests/**` | Small test values; constants pinned to UT99's on purpose; `x / x` tested on purpose; a case struct uploaded to a shader as bytes. |
| cppcheck | `unusedStructMember` | `tests/**` | Small test values; constants pinned to UT99's on purpose; `x / x` tested on purpose; a case struct uploaded to a shader as bytes. |
| cppcheck | `constParameterReference` | `src/ubundle/TileAnswers.cpp` | The bundle is written through. |
| cppcheck | `constVariableReference` | `src/urender/Materials.cpp` | An output argument. |
| cppcheck | `returnByReference` | `src/urender/Renderer.h` | A small value returned by copy. |
| cppcheck | `shadowMember` | `src/urender/Frame.cpp` | Locals of a different type from the members they shadow; a mix-up would not compile. |
| cppcheck | `constParameterCallback` | `apps/ut-ants/Png.cpp` | stb's callback signature. |
| cppcheck | `redundantInitialization` | `apps/ut-ants/Launcher.cpp` | A defensive default. |
| cppcheck | `containerOutOfBounds` | `tools/ut-bound-scan/Cli.cpp` | An index the loop bound keeps in range. |
| clang-tidy | `bugprone-float-loop-counter` | `src/ufire/Fire.cpp`, `tools/ut-paths/Seeds.cpp`, `tests/**` | Steps that are exact binary fractions, or sweeps whose exact count does not matter. |
| clang-tidy | `clang-analyzer-security.FloatLoopCounter` | `src/ufire/Fire.cpp`, `tools/ut-paths/Seeds.cpp`, `tests/**` | Steps that are exact binary fractions, or sweeps whose exact count does not matter. |
| clang-tidy | `bugprone-misplaced-widening-cast` | `src/ufire/Fire.cpp` | The product is already `size_t`; the rounded value is a heat from 0 to 255, never negative. |
| clang-tidy | `bugprone-incorrect-roundings` | `src/ufire/Fire.cpp` | The product is already `size_t`; the rounded value is a heat from 0 to 255, never negative. |
| clang-tidy | `clang-analyzer-core.CallAndMessage` | `src/ubake/Flames.cpp` | The point that set the lowest height always passes the filter, so both edge pointers are set. |
| clang-tidy | `bugprone-branch-clone` | `src/urender/Tiers.h` | One case per enumerator and no default, on purpose, so a new feature warns. |
| clang-tidy | `clang-analyzer-optin.performance.Padding` | `src/urender/Frame.cpp` | `Renderer::Impl`, one per renderer. |
| clang-tidy | `bugprone-assignment-in-if-condition` | `tools/ut-bench/**` | Assign-and-test, marked by double parentheses. |
| clang-tidy | `bugprone-too-small-loop-variable` | `tests/**` | A bound fixed at 29; out-of-range enum values fed on purpose to test a refusal. |
| clang-tidy | `clang-analyzer-optin.core.EnumCastOutOfRange` | `tests/**` | A bound fixed at 29; out-of-range enum values fed on purpose to test a refusal. |
| ruff | `E731` | `scripts/**`, `tools/ut-compare/**` | Style; argument lists with no shell; zips of 3-vectors; `S108` reads Xvfb's fixed lock paths and creates nothing. |
| ruff | `E741` | `scripts/**`, `tools/ut-compare/**` | Style; argument lists with no shell; zips of 3-vectors; `S108` reads Xvfb's fixed lock paths and creates nothing. |
| ruff | `B905` | `scripts/**`, `tools/ut-compare/**` | Style; argument lists with no shell; zips of 3-vectors; `S108` reads Xvfb's fixed lock paths and creates nothing. |
| ruff | `S603` | `scripts/**`, `tools/ut-compare/**` | Style; argument lists with no shell; zips of 3-vectors; `S108` reads Xvfb's fixed lock paths and creates nothing. |
| ruff | `S607` | `scripts/**`, `tools/ut-compare/**` | Style; argument lists with no shell; zips of 3-vectors; `S108` reads Xvfb's fixed lock paths and creates nothing. |
| ruff | `S108` | `scripts/**`, `tools/ut-compare/**` | Style; argument lists with no shell; zips of 3-vectors; `S108` reads Xvfb's fixed lock paths and creates nothing. |
| bandit | `B108` | `tools/ut-compare/**` | As ruff's `S108`. |
| semgrep | `python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args` | `scripts/**`, `tools/ut-compare/**` | A list-form `subprocess.run` of a fixed binary, no shell. |

**cppcheck needs `-D__has_cpp_attribute(x)=0`**, set in
`audit-config.json`. Without it, Catch2's headers stop analysis of every
test file at a syntax error (`UTA-0374`).

**glslangValidator cannot read `fsr_easu.frag` or `fsr_rcas.frag`**: they
use `#include` without `GL_GOOGLE_include_directive`, which the build's
`glslc` enables by default. Check them with `glslc` instead.

