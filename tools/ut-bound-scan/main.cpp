// ut-bound-scan: render bounds that miss the geometry they guard -- UTA-0328.
//
// Everything but argv lives in Cli.cpp, which the unit tests compile too.

#include "Cli.h"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    return uta::boundscan::runCli(args, std::cout, std::cerr);
}
