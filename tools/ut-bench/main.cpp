// ut-bench: bake maps several times and say where the time went -- UTA-0129,
// docs/specs/UTA-0129-benchmark-tool.md.
//
// Everything but argv and what compiled this program lives in Cli.cpp, which
// the unit tests compile too.

#include "BuildCommit.h" // generated -- apps/ut-ants/WriteBuildCommit.cmake
#include "Cli.h"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    uta::bench::BuildInfo build;
    build.compiler = UTA_BENCH_COMPILER;
    build.buildType = UTA_BENCH_BUILD_TYPE;
    if (std::string_view(UTA_BENCH_SANITIZER) != "") build.sanitizer = UTA_BENCH_SANITIZER;
    build.commit = uta::client::BUILD_COMMIT;
    return uta::bench::runCli(args, std::cout, std::cerr, build);
}
