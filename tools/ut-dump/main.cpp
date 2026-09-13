// ut-dump: inspect an Unreal Engine 1 package from the command line -- UTA-0012.
//
// Everything but argv lives in Cli.cpp, which the unit tests compile too.

#include "Cli.h"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    return uta::dump::runCli(args, std::cout, std::cerr);
}
