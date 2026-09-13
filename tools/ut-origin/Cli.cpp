// The ut-origin command line -- UTA-0013's third quarantine check.

#include "Cli.h"

#include "ubundle/Bundle.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>

namespace uta::origin {

int runCli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    if (args.size() != 1) {
        err << "usage: ut-origin <bundle.utab>\n";
        return 2;
    }
    const std::string name(args[0]);

    // The header only: a whole bundle can be most of a gigabyte, and the origin
    // is in its first sixteen bytes.
    std::ifstream file(std::filesystem::path(name), std::ios::binary);
    if (!file) {
        out << "unreadable\n";
        err << "ut-origin: " << name << ": cannot be opened\n";
        return 1;
    }
    std::array<char, ubundle::HEADER_SIZE> header{};
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    const auto length = static_cast<std::size_t>(file.gcount());

    const auto read =
        ubundle::readHeader(std::as_bytes(std::span<const char>(header.data(), length)));
    if (!read) {
        out << "unreadable\n";
        err << "ut-origin: " << name << ": " << read.error().message() << "\n";
        return 1;
    }
    if (read->origin != ubundle::Origin::Authored) {
        out << "derived\n";
        return 1;
    }
    out << "authored\n";
    return 0;
}

} // namespace uta::origin
