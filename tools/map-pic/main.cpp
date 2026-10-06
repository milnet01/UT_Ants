// map-pic: save the picture a map ships with as a PNG -- UTA-0316.
//
// Everything but argv lives in Cli.cpp, which the unit tests compile too.

#include "Cli.h"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    return uta::mappic::runCli(args, std::cout, std::cerr);
}
