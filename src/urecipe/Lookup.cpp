// Finding a map's recipe -- docs/specs/UTA-0113-recipe-format.md SS 4.3.

#include "urecipe/Lookup.h"

#include "core/FileSystem.h"

#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace uta::urecipe {
namespace {

/// The parsed recipe at `path`, checked against the map it is for.
Result<Found> load(const std::filesystem::path& path, std::string_view mapName,
                   const std::array<std::byte, 32>& mapDigest) {
    const std::string shown = fs::utf8(path);
    auto bytes = fs::readFile(path);
    if (!bytes.has_value()) return std::unexpected(bytes.error().withContext("reading the recipe " + shown));
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    auto recipe = parse(text);
    if (!recipe.has_value()) return std::unexpected(recipe.error().withContext("the recipe " + shown));

    if (recipe->map != mapName)
        return fail(ErrorCode::InvalidArgument,
                    std::format("the recipe {} is for the map {}, not {}", shown, recipe->map, mapName));
    if (recipe->mapDigest.has_value() && *recipe->mapDigest != mapDigest)
        return fail(ErrorCode::InvalidArgument,
                    std::format("the recipe {} was written for another copy of {}: its sha256 differs", shown,
                                mapName));
    return Found{std::move(*recipe), path};
}

bool isPresent(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::status(path, ec).type() != std::filesystem::file_type::not_found;
}

} // namespace

std::filesystem::path shippedDirectory() {
    return fs::pathFromUtf8(UTA_SHIPPED_RECIPES_DIR);
}

Sources standardSources(std::optional<std::filesystem::path> named) {
    Sources sources;
    sources.named = std::move(named);
    if (const auto data = fs::dataDirectory(); data.has_value()) sources.player = *data / "recipes";
    sources.shipped = shippedDirectory();
    return sources;
}

Result<std::optional<Found>> find(const Sources& sources, std::string_view mapName,
                                  const std::array<std::byte, 32>& mapDigest) {
    if (sources.named.has_value()) {
        UTA_TRY(Found found, load(*sources.named, mapName, mapDigest));
        return std::optional<Found>(std::move(found));
    }
    const std::filesystem::path file = fs::pathFromUtf8(std::string(mapName) + ".recipe");
    for (const std::filesystem::path* directory : {&sources.player, &sources.shipped}) {
        if (directory->empty() || !isPresent(*directory / file)) continue;
        UTA_TRY(Found found, load(*directory / file, mapName, mapDigest));
        return std::optional<Found>(std::move(found));
    }
    return std::optional<Found>();
}

} // namespace uta::urecipe
