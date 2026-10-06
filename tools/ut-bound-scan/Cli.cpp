// The ut-bound-scan command line -- UTA-0328.

#include "Cli.h"

#include "Scan.h"

#include "common/Json.h"
#include "core/FileSystem.h"
#include "core/Md5.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace uta::boundscan {

namespace {

namespace stdfs = std::filesystem;

std::string hex(const std::array<std::byte, 16>& digest) {
    std::string text;
    for (const std::byte b : digest) {
        char pair[3];
        std::snprintf(pair, sizeof pair, "%02x", static_cast<unsigned>(b));
        text += pair;
    }
    return text;
}

/// The name a reference resolves to, or "-" for a null one or one that does
/// not resolve.
std::string nameOf(const upkg::Package& package, upkg::ObjectReference reference) {
    if (reference.kind() == upkg::ObjectReferenceKind::Null) return "-";
    const auto name = package.objectName(reference);
    return name ? std::string(*name) : std::string("-");
}

const char* kindName(Kind kind) {
    switch (kind) {
    case Kind::Miss: return "miss";
    case Kind::Inverted: return "inverted";
    case Kind::Invalid: return "invalid";
    }
    return "miss";
}

/// `{"kind":<kind>,"map":<map>` -- every line's first two fields.
void open(std::ostream& out, std::string_view kind, std::string_view map) {
    out << "{\"kind\":";
    tools::writeJsonString(out, kind);
    out << ",\"map\":";
    tools::writeJsonString(out, map);
}

void refuse(std::ostream& out, std::string_view map, const std::optional<std::string>& md5,
            std::string_view reason) {
    open(out, "refused", map);
    if (md5) {
        out << ",\"md5\":";
        tools::writeJsonString(out, *md5);
    }
    out << ",\"reason\":";
    tools::writeJsonString(out, reason);
    out << "}\n";
}

/// Scans one map; false when it was refused.
bool scanFile(const stdfs::path& file, double tolerance, std::ostream& out) {
    const std::string map = ubake::detail::utf8(file.filename());
    const auto bytes = fs::readFile(file);
    if (!bytes) {
        refuse(out, map, std::nullopt, bytes.error().message());
        return false;
    }
    const std::string md5 = hex(uta::md5(*bytes));
    const auto package = upkg::Package::open(*bytes);
    if (!package) {
        refuse(out, map, md5, package.error().message());
        return false;
    }
    const std::string mapName = ubake::detail::mapNameOf(file);
    const auto levelEntry = ubake::detail::findLevel(*package, mapName);
    if (!levelEntry) {
        refuse(out, map, md5, levelEntry.error().message());
        return false;
    }
    const auto level = upkg::readLevel(*package, **levelEntry);
    if (!level) {
        refuse(out, map, md5, level.error().message());
        return false;
    }
    const auto modelEntry = ubake::detail::findModel(*package, *level, mapName);
    if (!modelEntry) {
        refuse(out, map, md5, modelEntry.error().message());
        return false;
    }
    const auto model = upkg::readModel(*package, **modelEntry);
    if (!model) {
        refuse(out, map, md5, model.error().message());
        return false;
    }

    const Scan scan = scanModel(*model, tolerance);
    for (const Finding& finding : scan.findings) {
        std::string brush = "-";
        std::string texture = "-";
        const upkg::BspNode& node = model->nodes[finding.node];
        if (node.iSurf >= 0 && static_cast<std::size_t>(node.iSurf) < model->surfs.size()) {
            const upkg::BspSurf& surf = model->surfs[static_cast<std::size_t>(node.iSurf)];
            brush = nameOf(*package, surf.actor);
            texture = nameOf(*package, surf.texture);
        }
        open(out, kindName(finding.kind), map);
        out << ",\"md5\":";
        tools::writeJsonString(out, md5);
        out << ",\"node\":" << finding.node << ",\"brush\":";
        tools::writeJsonString(out, brush);
        out << ",\"texture\":";
        tools::writeJsonString(out, texture);
        out << ",\"bound\":" << finding.bound;
        if (finding.kind == Kind::Miss)
            out << ",\"excess\":" << std::llround(finding.excess);
        else
            out << ",\"valid\":" << (finding.valid ? 1 : 0);
        out << "}\n";
    }
    open(out, "summary", map);
    out << ",\"md5\":";
    tools::writeJsonString(out, md5);
    out << ",\"nodes\":" << scan.nodes << ",\"checked\":" << scan.checked
        << ",\"misses\":" << scan.misses << ",\"inverted\":" << scan.inverted
        << ",\"invalid\":" << scan.invalid << ",\"worst\":" << std::llround(scan.worst) << "}\n";
    return true;
}

/// The `.unr` files directly inside `folder`, in name order.
std::vector<stdfs::path> mapsIn(const stdfs::path& folder) {
    std::vector<stdfs::path> maps;
    std::error_code listing;
    for (stdfs::directory_iterator entry(folder, listing), end; !listing && entry != end;
         entry.increment(listing)) {
        std::error_code ignored;
        if (entry->is_regular_file(ignored) &&
            ubake::detail::fold(ubake::detail::utf8(entry->path().extension())) == ".unr")
            maps.push_back(entry->path());
    }
    std::ranges::sort(maps);
    return maps;
}

} // namespace

int runCli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    constexpr std::string_view USAGE =
        "usage: ut-bound-scan [--tolerance <units>] <map.unr | folder>...\n";
    double tolerance = 2;
    std::size_t first = 0;
    if (!args.empty() && args[0] == "--tolerance") {
        if (args.size() < 2) {
            err << USAGE;
            return 2;
        }
        const std::string_view text = args[1];
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), tolerance);
        if (error != std::errc{} || end != text.data() + text.size() || !(tolerance >= 0)) {
            err << "ut-bound-scan: --tolerance wants a number of world units, 0 or more\n";
            return 2;
        }
        first = 2;
    }
    if (first >= args.size()) {
        err << USAGE;
        return 2;
    }

    bool refused = false;
    for (std::size_t a = first; a < args.size(); ++a) {
        const stdfs::path path{std::string(args[a])};
        std::error_code ignored;
        if (stdfs::is_directory(path, ignored)) {
            for (const stdfs::path& map : mapsIn(path))
                refused = !scanFile(map, tolerance, out) || refused;
        } else {
            refused = !scanFile(path, tolerance, out) || refused;
        }
    }
    return refused ? 1 : 0;
}

} // namespace uta::boundscan
