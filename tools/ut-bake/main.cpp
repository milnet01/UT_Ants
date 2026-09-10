// ut-bake: bake a map, and check an install -- UTA-0011,
// docs/specs/UTA-0011-map-baker.md.
//
// Everything but argv lives in Cli.cpp, which the unit tests compile too.

#include "Cli.h"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    return uta::ubake::runCli(args, std::cout, std::cerr);
}
