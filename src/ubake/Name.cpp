// The baker version and the bake name --
// docs/specs/UTA-0011-map-baker.md SS 4.3 and SS 4.4.

#include "ubake/Name.h"

#include "core/FileSystem.h"
#include "core/Sha256.h"
#include "ubundle/Bundle.h"
#include "umat/Library.h"

#include <algorithm>
#include <format>
#include <set>
#include <utility>

namespace uta::ubake {
namespace {

constexpr std::byte LF{0x0A};

/// Every package `package` imports, folded -- upkg::importedPackages, the
/// supported call (UTA-0070). It yields the set the outer-chain walk this
/// replaced did: every chain that ends does so at an import whose outer is
/// null, and each such import is its own chain's end. So bake names do not
/// move.
std::set<std::string> foldedImports(const upkg::Package& package) {
    std::set<std::string> out;
    if (const auto names = upkg::importedPackages(package); names.has_value())
        for (const std::string_view name : *names) out.insert(detail::fold(name));
    return out;
}

void addText(Sha256& hasher, std::string_view text) {
    hasher.add(std::as_bytes(std::span<const char>(text.data(), text.size())));
}

void addByte(Sha256& hasher, std::byte value) {
    hasher.add(std::span<const std::byte>(&value, 1));
}

} // namespace

std::string bakerVersion() {
    return std::format("r{}-f{}-l{:016x}", BAKER_REVISION, ubundle::FORMAT_VERSION,
                       umat::libraryDigest());
}

Result<std::string> bakeName(const std::filesystem::path& map, Install& install) {
    UTA_TRY(const std::vector<std::byte> bytes, uta::fs::readFile(map));
    return detail::bakeName(bytes, detail::mapNameOf(map), install);
}

namespace detail {

std::string hex(std::span<const std::byte> bytes) {
    static constexpr std::string_view DIGITS = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::byte part : bytes) {
        const auto value = std::to_integer<unsigned>(part);
        out += DIGITS[value >> 4];
        out += DIGITS[value & 0xFU];
    }
    return out;
}

std::string mapNameOf(const std::filesystem::path& map) {
    return fold(utf8(map.stem()));
}

std::string nameOf(const NameInputs& inputs) {
    // SS 4.4's byte string, item by item. Every variable-length field ends in
    // LF, so no two different inputs can run into the same string -- INV-4's
    // {ab, c} against {a, bc}.
    Sha256 hasher;
    addText(hasher, "uta-bake-name-1");
    addByte(hasher, LF);
    addText(hasher, inputs.bakerVersion);
    addByte(hasher, LF);
    addByte(hasher, std::byte{0x00}); // no recipe; UTA-0113 defines what follows 0x01
    addText(hasher, inputs.mapName);
    addByte(hasher, LF);
    hasher.add(inputs.mapDigest);

    std::vector<const ClosureEntry*> sorted;
    for (const ClosureEntry& entry : inputs.closure) sorted.push_back(&entry);
    std::sort(sorted.begin(), sorted.end(),
              [](const ClosureEntry* a, const ClosureEntry* b) { return a->name < b->name; });
    for (const ClosureEntry* entry : sorted) {
        addText(hasher, entry->name);
        addByte(hasher, LF);
        // An absent package still contributes its name, so installing or
        // repairing it later renames the bake (INV-3).
        if (entry->digest.has_value()) {
            addByte(hasher, std::byte{0x01});
            hasher.add(*entry->digest);
        } else {
            addByte(hasher, std::byte{0x00});
        }
    }
    return hex(hasher.finish());
}

Result<std::vector<std::string>> closure(const upkg::Package& map,
                                         const upkg::PackageResolver& resolver) {
    std::set<std::string> seen = foldedImports(map);
    std::vector<std::string> pending(seen.begin(), seen.end());
    for (std::size_t next = 0; next < pending.size(); ++next) {
        UTA_TRY(const upkg::Package* const package, resolver(pending[next]));
        if (package == nullptr) continue;
        for (const std::string& name : foldedImports(*package)) {
            if (seen.insert(name).second) pending.push_back(name);
        }
    }
    return std::vector<std::string>(seen.begin(), seen.end());
}

Result<std::string> bakeName(std::span<const std::byte> mapBytes, std::string_view mapName,
                             Install& install) {
    auto map = upkg::Package::open(mapBytes);
    if (!map.has_value())
        return std::unexpected(map.error().withContext("the map " + std::string(mapName)));

    const upkg::PackageResolver resolver = install.resolver();
    UTA_TRY(const std::vector<std::string> names, closure(*map, resolver));

    NameInputs inputs;
    inputs.bakerVersion = bakerVersion();
    inputs.mapName = std::string(mapName);
    inputs.mapDigest = sha256(mapBytes);
    for (const std::string& name : names) {
        if (name == mapName) continue; // SS 4.4: the map itself is left out
        ClosureEntry entry{name, std::nullopt};
        UTA_TRY(const upkg::Package* const opened, resolver(name));
        if (opened != nullptr) entry.digest = sha256(install.bytesOf(name));
        inputs.closure.push_back(std::move(entry));
    }
    return nameOf(inputs);
}

} // namespace detail

} // namespace uta::ubake
