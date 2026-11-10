// A map's level collision tree, read from the install -- what UTA-0457's and
// UTA-0017's recording cases both stand their bodies in. Header-only.

#pragma once

#include "core/FileSystem.h"
#include "ubake/Collision.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace uta::test::real {

/// The level's collision tree from the map at `mapPath`, built as the baker
/// builds it; the case fails where any step does.
inline uta::ubundle::CollisionTree levelTreeOf(const std::filesystem::path& mapPath) {
    const auto bytes = uta::fs::readFile(mapPath);
    REQUIRE(bytes.has_value());
    const auto map = uta::upkg::Package::open(*bytes);
    REQUIRE(map.has_value());
    const auto exports = map->exports();
    const uta::upkg::ExportEntry* levelExport = nullptr;
    for (const auto& entry : exports)
        if (entry.objectClass.kind() != uta::upkg::ObjectReferenceKind::Null
            && uta::ubake::detail::fold(std::string(map->objectName(entry.objectClass).value_or(""))) == "level")
            levelExport = &entry;
    REQUIRE(levelExport != nullptr);
    const auto level = uta::upkg::readLevel(*map, *levelExport);
    REQUIRE(level.has_value());
    REQUIRE(level->model.kind() == uta::upkg::ObjectReferenceKind::Export);
    const auto model = uta::upkg::readModel(*map, exports[level->model.index()]);
    REQUIRE(model.has_value());
    auto tree = uta::ubake::buildCollision(*model);
    REQUIRE(tree.has_value());
    return std::move(*tree);
}

} // namespace uta::test::real
