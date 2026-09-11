// ut-paths: propose bot path nodes for UT99's maps -- UTA-0121,
// docs/specs/UTA-0121-bot-path-seeds.md.
//
// Everything but argv lives in Cli.cpp, which the unit tests compile too.

#include "Cli.h"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    return uta::paths::runCli(args, std::cout, std::cerr);
}
