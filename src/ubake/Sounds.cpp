// The sounds a map's actors name, and the weapon set's sound bundle --
// docs/specs/UTA-0478-baked-sounds.md SS 4.3 and SS 4.4.

#include "ubake/Sounds.h"

#include "core/FileSystem.h"
#include "core/Sha256.h"
#include "ubake/Actors.h"
#include "upkg/Sound.h"

#include <algorithm>
#include <map>
#include <span>
#include <system_error>
#include <utility>
#include <variant>

namespace uta::ubake {
namespace {

using ubundle::PropertyRecord;
using ubundle::ValueKind;

constexpr std::byte LF{0x0A};

/// The part of a folded path before its first dot: the package.
std::string packageOf(const std::string& path) { return path.substr(0, path.find('.')); }

/// Finds the Sound export a folded path names. Each package's sounds are
/// listed once, by the path PLAC would give a reference to them, so a lookup
/// compares whole paths and never guesses at groups.
class SoundFinder {
public:
    /// `map` may be null: the sound set has none.
    SoundFinder(const upkg::Package* map, std::string_view mapName, const upkg::PackageResolver& resolver)
        : map_(map), mapName_(mapName), resolver_(resolver) {}

    struct Site {
        const upkg::Package* holder = nullptr;
        const upkg::ExportEntry* entry = nullptr;
    };

    /// `holder` null where `path` names no Sound: another class's object, an
    /// object that is not there, or a package the install lacks.
    [[nodiscard]] Result<Site> find(const std::string& path) {
        const std::string package = packageOf(path);
        auto listed = sounds_.find(package);
        if (listed == sounds_.end()) {
            UTA_TRY(Listing listing, list(package));
            listed = sounds_.emplace(package, std::move(listing)).first;
        }
        const auto found = listed->second.byPath.find(path);
        if (found == listed->second.byPath.end()) return Site{};
        return Site{listed->second.holder, found->second};
    }

private:
    struct Listing {
        const upkg::Package* holder = nullptr;
        std::map<std::string, const upkg::ExportEntry*> byPath;
    };

    [[nodiscard]] Result<Listing> list(const std::string& package) {
        Listing listing;
        if (map_ != nullptr && package == mapName_) {
            listing.holder = map_;
        } else {
            UTA_TRY(listing.holder, resolver_(package));
        }
        if (listing.holder == nullptr) return listing;
        for (const upkg::ExportEntry& entry : listing.holder->exports()) {
            if (entry.objectClass.kind() == upkg::ObjectReferenceKind::Null) continue; // a class
            const auto className = listing.holder->objectName(entry.objectClass);
            if (!className.has_value() || detail::fold(*className) != "sound") continue;
            // An export whose outer chain does not walk is not a sound anyone
            // can name; it is passed over, not a refusal.
            auto path = detail::pathOfExport(*listing.holder, package, entry);
            if (path.has_value()) listing.byPath.emplace(std::move(*path), &entry);
        }
        return listing;
    }

    const upkg::Package* map_;
    std::string mapName_;
    const upkg::PackageResolver& resolver_;
    std::map<std::string, Listing> sounds_;
};

/// SS 4.3 items 2 to 4, over a set so each path is read once and the result
/// is ascending.
Result<FoundSounds> soundsAt(const std::set<std::string>& paths, SoundFinder& finder) {
    FoundSounds found;
    for (const std::string& path : paths) {
        UTA_TRY(const SoundFinder::Site site, finder.find(path));
        if (site.holder == nullptr) continue;
        const auto sound = upkg::readSound(*site.holder, *site.entry);
        if (!sound.has_value()) {
            found.skipped.push_back({path, std::string(sound.error().message())});
        } else if (sound->data.empty()) {
            found.skipped.push_back({path, "the sound holds no bytes"});
        } else if (found.sounds.size() == ubundle::SOUND_LIMIT) {
            found.skipped.push_back({path, "past the limit of " + std::to_string(ubundle::SOUND_LIMIT) + " sounds"});
        } else {
            found.sounds.push_back({path, std::vector<std::byte>(sound->data.begin(), sound->data.end())});
        }
    }
    return found;
}

/// The non-empty paths `records` carry under `kind`, added to `into`.
void collect(const std::vector<PropertyRecord>& records, ValueKind kind, std::set<std::string>& into) {
    for (const PropertyRecord& record : records) {
        if (record.kind != kind) continue;
        const std::string* const path = std::get_if<std::string>(&record.value);
        if (path != nullptr && !path->empty()) into.insert(*path);
    }
}

std::set<std::string> foldedSet(const std::vector<std::string>& classes) {
    std::set<std::string> out;
    for (const std::string& name : classes) out.insert(detail::fold(name));
    return out;
}

void addText(Sha256& hasher, std::string_view text) {
    hasher.add(std::as_bytes(std::span<const char>(text.data(), text.size())));
}

void addByte(Sha256& hasher, std::byte value) { hasher.add(std::span<const std::byte>(&value, 1)); }

} // namespace

Result<FoundSounds> mapSounds(const ubundle::Placements& placements, const upkg::Package& map,
                              std::string_view mapName, const upkg::PackageResolver& resolver) {
    std::set<std::string> paths;
    for (const ubundle::ActorPlacement& actor : placements.actors) collect(actor.properties, ValueKind::Object, paths);
    for (const ubundle::ActorClass& actorClass : placements.classes)
        collect(actorClass.defaults, ValueKind::Object, paths);
    SoundFinder finder(&map, mapName, resolver);
    return soundsAt(paths, finder);
}

namespace detail {

Result<SoundSet> soundSetOf(const std::vector<std::string>& classes, const upkg::PackageResolver& resolver) {
    SoundSet set;
    const std::set<std::string> listed = foldedSet(classes);
    std::set<std::string> paths;
    std::set<std::string> named; // by a listed class's class-valued defaults
    for (const std::string& path : listed) {
        UTA_TRY(const ubundle::ActorClass record, classRecord(path, resolver));
        if (!record.resolved) {
            set.unresolved.push_back(path);
            continue;
        }
        collect(record.defaults, ValueKind::Object, paths);
        collect(record.defaults, ValueKind::Class, named);
    }
    // One step and no further (SS 3 decision 4): a named class gives its
    // sounds, and the classes IT names are not followed.
    for (const std::string& path : named) {
        if (listed.contains(path)) continue;
        UTA_TRY(const ubundle::ActorClass record, classRecord(path, resolver));
        if (record.resolved) collect(record.defaults, ValueKind::Object, paths);
    }
    SoundFinder finder(nullptr, {}, resolver);
    UTA_TRY(set.found, soundsAt(paths, finder));
    return set;
}

std::string soundSetNameOf(std::string_view bakerVersion, const std::vector<std::string>& classes,
                           const std::vector<ClosureEntry>& closure) {
    // Every variable-length field ends in LF, as bakeNameOf's do.
    Sha256 hasher;
    addText(hasher, "uta-sound-set-name-1");
    addByte(hasher, LF);
    addText(hasher, bakerVersion);
    addByte(hasher, LF);
    for (const std::string& path : foldedSet(classes)) {
        addText(hasher, path);
        addByte(hasher, LF);
    }
    std::vector<const ClosureEntry*> sorted;
    for (const ClosureEntry& entry : closure) sorted.push_back(&entry);
    std::sort(sorted.begin(), sorted.end(),
              [](const ClosureEntry* a, const ClosureEntry* b) { return a->name < b->name; });
    for (const ClosureEntry* entry : sorted) {
        addText(hasher, entry->name);
        addByte(hasher, LF);
        if (entry->digest.has_value()) {
            addByte(hasher, std::byte{0x01});
            hasher.add(*entry->digest);
        } else {
            addByte(hasher, std::byte{0x00});
        }
    }
    return hex(hasher.finish());
}

Result<std::string> soundSetName(const std::vector<std::string>& classes, Install& install) {
    const upkg::PackageResolver resolver = install.resolver();
    std::set<std::string> packages;
    for (const std::string& path : foldedSet(classes)) {
        const std::string root = packageOf(path);
        if (!packages.insert(root).second) continue;
        UTA_TRY(const upkg::Package* const opened, resolver(root));
        if (opened == nullptr) continue;
        UTA_TRY(const std::vector<std::string> reached, closure(*opened, resolver));
        packages.insert(reached.begin(), reached.end());
    }
    std::vector<ClosureEntry> entries;
    for (const std::string& name : packages) {
        ClosureEntry entry{name, std::nullopt};
        UTA_TRY(const upkg::Package* const opened, resolver(name));
        if (opened != nullptr) entry.digest = sha256(install.bytesOf(name));
        entries.push_back(std::move(entry));
    }
    return soundSetNameOf(bakerVersion(), classes, entries);
}

} // namespace detail

Result<SoundSetOutcome> bakeSoundSet(const SoundSetRequest& request) {
    UTA_TRY(Install install, Install::open(request.install));
    SoundSetOutcome outcome;
    UTA_TRY(outcome.name, detail::soundSetName(request.classes, install));
    outcome.path = request.outDir / (outcome.name + ".utab");

    std::error_code ec;
    std::filesystem::create_directories(request.outDir, ec);
    if (ec)
        return fail(ErrorCode::IoFailure, "cannot create " + detail::utf8(request.outDir) + ": " + ec.message());

    if (!request.force && detail::isCachedBake(outcome.path)) {
        outcome.verdict = Verdict::Cached;
        return outcome;
    }

    UTA_TRY(detail::SoundSet set, detail::soundSetOf(request.classes, install.resolver()));
    ubundle::Bundle bundle;
    bundle.header.origin = ubundle::Origin::Derived;
    bundle.header.kind = ubundle::BundleKind::Sounds;
    bundle.baker = bakerVersion();
    outcome.soundCount = set.found.sounds.size();
    for (const ubundle::BakedSound& sound : set.found.sounds) outcome.soundBytes += sound.data.size();
    bundle.sounds = std::move(set.found.sounds);
    outcome.unresolved = std::move(set.unresolved);
    outcome.skippedSounds = std::move(set.found.skipped);

    UTA_TRY(const std::vector<std::byte> bytes, ubundle::write(bundle));
    UTA_CHECK(uta::fs::writeFileAtomically(outcome.path, bytes));
    outcome.verdict = Verdict::Written;
    return outcome;
}

} // namespace uta::ubake
