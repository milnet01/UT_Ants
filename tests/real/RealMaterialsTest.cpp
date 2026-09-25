// The real-asset tier's material census -- UTA-0009 and UTA-0010, split out
// of RealInstallTest.cpp by UTA-0103.

#include "core/FileSystem.h"
#include "core/Jobs.h"
#include "umat/Derive.h"
#include "umat/Enlarge.h"
#include "umat/Fingerprint.h"
#include "umat/Generate.h"
#include "umat/Library.h"
#include "umat/Resolve.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "upkg/Texture.h"
#include "real/RealSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::test::real;

// UTA-0009's census: INV-13, and the figures that spec's SS 2 rests on,
// printed so they are an output of the tree rather than a scratch run.

namespace {

using uta::umat::Image;

/// `in` shrunk by `k` in each axis, each output texel the rounded mean of the
/// k x k block it covers.
Image boxShrink(const Image& in, std::uint32_t k) {
    Image out;
    out.width = in.width / k;
    out.height = in.height / k;
    out.channels = in.channels;
    out.pixels.resize(std::size_t{out.width} * out.height * in.channels);
    for (std::size_t y = 0; y < out.height; ++y)
        for (std::size_t x = 0; x < out.width; ++x)
            for (std::size_t c = 0; c < in.channels; ++c) {
                std::uint32_t sum = 0;
                for (std::size_t dy = 0; dy < k; ++dy)
                    for (std::size_t dx = 0; dx < k; ++dx)
                        sum += std::to_integer<std::uint32_t>(
                            in.pixels[((y * k + dy) * in.width + x * k + dx) * in.channels + c]);
                out.pixels[(y * out.width + x) * in.channels + c] =
                    static_cast<std::byte>((sum + k * k / 2) / (k * k));
            }
    return out;
}

/// A reference enlarger: each output texel copies the source texel it lies in.
Image nearestEnlarge(const Image& in, std::uint32_t k) {
    Image out = in;
    out.width = in.width * k;
    out.height = in.height * k;
    out.pixels.resize(std::size_t{out.width} * out.height * in.channels);
    for (std::size_t y = 0; y < out.height; ++y)
        for (std::size_t x = 0; x < out.width; ++x)
            for (std::size_t c = 0; c < in.channels; ++c)
                out.pixels[(y * out.width + x) * in.channels + c] =
                    in.pixels[((y / k) * in.width + x / k) * in.channels + c];
    return out;
}

/// Keys' cubic convolution kernel, a = -0.5.
double keysCubic(double x) {
    x = std::abs(x);
    const double a = -0.5;
    if (x < 1.0) return ((a + 2.0) * x - (a + 3.0)) * x * x + 1.0;
    if (x < 2.0) return ((a * x - 5.0 * a) * x + 8.0 * a) * x - 4.0 * a;
    return 0.0;
}

/// A reference bicubic enlarger at enlarge()'s own sample positions and
/// wrapping edges, in floating point -- a test may use the maths library.
Image bicubicEnlarge(const Image& in, std::uint32_t k) {
    Image out = nearestEnlarge(in, k);
    const auto texel = [&](std::int64_t x, std::int64_t y, std::size_t c) {
        const std::int64_t w = in.width;
        const std::int64_t h = in.height;
        const auto sx = static_cast<std::size_t>(((x % w) + w) % w);
        const auto sy = static_cast<std::size_t>(((y % h) + h) % h);
        return std::to_integer<int>(in.pixels[(sy * in.width + sx) * in.channels + c]);
    };
    for (std::size_t y = 0; y < out.height; ++y) {
        const double sy = (static_cast<double>(y) + 0.5) / k - 0.5;
        const auto by = static_cast<std::int64_t>(std::floor(sy));
        for (std::size_t x = 0; x < out.width; ++x) {
            const double sx = (static_cast<double>(x) + 0.5) / k - 0.5;
            const auto bx = static_cast<std::int64_t>(std::floor(sx));
            for (std::size_t c = 0; c < in.channels; ++c) {
                double sum = 0.0;
                for (std::int64_t j = -1; j <= 2; ++j)
                    for (std::int64_t i = -1; i <= 2; ++i)
                        sum += keysCubic(sx - static_cast<double>(bx + i))
                               * keysCubic(sy - static_cast<double>(by + j))
                               * texel(bx + i, by + j, c);
                out.pixels[(y * out.width + x) * in.channels + c] =
                    static_cast<std::byte>(std::clamp(std::lround(sum), 0L, 255L));
            }
        }
    }
    return out;
}

/// Peak signal-to-noise ratio over the colour channels, capped at 100 dB for
/// an exact match.
double psnr(const Image& a, const Image& b) {
    double squared = 0.0;
    std::size_t samples = 0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        if (i % a.channels == 3) continue;
        const double d = std::to_integer<int>(a.pixels[i]) - std::to_integer<int>(b.pixels[i]);
        squared += d * d;
        ++samples;
    }
    const double mse = squared / static_cast<double>(samples);
    return mse == 0.0 ? 100.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

TEST_CASE("the enlarger keeps its measured ranking over the install's textures",
          "[real-assets][umat]") {
    // UTA-0009 INV-13: one palettised power-of-two square per texture package,
    // shrunk to a quarter, enlarged back, and scored against the original.
    // The same pass prints SS 2 item 4's index-0 comparison and item 3's count
    // of texture objects carrying a PolyFlags property.
    const fs::path textures = fs::path{UTA_UT_INSTALL_DIR} / "Textures";
    REQUIRE(fs::is_directory(textures));
    uta::JobSystem jobs(std::max(1U, std::thread::hardware_concurrency()));

    double ours = 0.0;
    double bicubic = 0.0;
    double nearest = 0.0;
    long long measured = 0;
    long long textureObjects = 0;
    long long withPolyFlags = 0;
    std::array<double, 2> indexZeroShare{};  // [0] not bMasked, [1] bMasked
    std::array<long long, 2> indexZeroCount{};

    for (const fs::directory_entry& entry : fs::directory_iterator(textures)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".utx") continue;
        const auto read = uta::fs::readFile(entry.path());
        REQUIRE(read.has_value());
        const auto package = uta::upkg::Package::open(std::span<const std::byte>{*read});
        if (!package.has_value()) continue;

        bool measuredOne = false;
        for (const auto& object : package->exports()) {
            const auto className = package->objectName(object.objectClass);
            if (!className.has_value() || *className != "Texture") continue;
            const auto properties = uta::upkg::readProperties(*package, object);
            if (!properties.has_value()) continue;
            ++textureObjects;

            std::optional<uta::upkg::ObjectReference> paletteReference;
            bool masked = false;
            bool polyFlags = false;
            for (const auto& property : *properties) {
                const auto name = package->name(property.nameIndex);
                if (!name.has_value()) continue;
                if (*name == "Palette")
                    if (const auto* reference = std::get_if<uta::upkg::ObjectReference>(&property.value))
                        paletteReference = *reference;
                if (*name == "bMasked")
                    if (const auto* flag = std::get_if<bool>(&property.value)) masked = *flag;
                if (*name == "PolyFlags") polyFlags = true;
            }
            if (polyFlags) ++withPolyFlags;

            const auto texture = uta::upkg::readTexture(*package, object);
            if (!texture.has_value() || texture->mips.empty()) continue;
            const uta::upkg::Mip& base = texture->mips[0];
            const std::size_t texels = std::size_t{base.width} * base.height;
            if (texels == 0 || base.pixels.size() != texels) continue; // not one byte a texel

            std::size_t zeros = 0;
            for (const std::byte index : base.pixels) zeros += index == std::byte{0} ? 1 : 0;
            indexZeroShare[masked ? 1 : 0] += static_cast<double>(zeros) / static_cast<double>(texels);
            ++indexZeroCount[masked ? 1 : 0];

            if (measuredOne || !paletteReference.has_value()
                || paletteReference->kind() != uta::upkg::ObjectReferenceKind::Export)
                continue;
            if (base.width != base.height || !std::has_single_bit(base.width) || base.width < 32)
                continue;
            const auto palette =
                uta::upkg::readPalette(*package, package->exports()[paletteReference->index()]);
            if (!palette.has_value()) continue;
            const auto original = uta::umat::resolve(base, *palette, false);
            if (!original.has_value()) continue;

            const Image small = boxShrink(*original, 4);
            const auto enlarged = uta::umat::enlarge(small, 4, jobs);
            REQUIRE(enlarged.has_value());
            ours += psnr(*enlarged, *original);
            bicubic += psnr(bicubicEnlarge(small, 4), *original);
            nearest += psnr(nearestEnlarge(small, 4), *original);
            ++measured;
            measuredOne = true;
        }
    }

    REQUIRE(measured > 0);
    const double n = static_cast<double>(measured);
    WARN("INV-13 round trip over " << measured << " textures, mean PSNR -- enlarge "
                                   << ours / n << " dB, bicubic " << bicubic / n
                                   << " dB, nearest " << nearest / n << " dB");
    WARN("SS 2 item 4 -- mean index-0 share: bMasked "
         << (indexZeroCount[1] ? indexZeroShare[1] / static_cast<double>(indexZeroCount[1]) : 0.0)
         << " over " << indexZeroCount[1] << " textures, others "
         << (indexZeroCount[0] ? indexZeroShare[0] / static_cast<double>(indexZeroCount[0]) : 0.0)
         << " over " << indexZeroCount[0]);
    WARN("SS 2 item 3 -- Texture objects carrying a PolyFlags property: " << withPolyFlags
                                                                          << " of " << textureObjects);
    CHECK(ours >= bicubic);
    CHECK(ours > nearest);
}

TEST_CASE("surfaces sharing a texture disagree on their flags", "[real-assets][umat]") {
    // UTA-0009 SS 2 item 3, printed rather than asserted: the reason a water or
    // glass tag cannot live on a material. For each map's largest Model, each
    // texture used by two or more surfaces is counted once per flag: as
    // FLAGGED where any of its surfaces sets the flag, and as disagreeing
    // where some do and some do not. Flagged is the denominator that matters
    // -- of the textures used as glass somewhere, how many are not glass
    // elsewhere -- and every shared texture is printed beside it.
    struct Flag {
        const char* name;
        std::uint32_t mask;
        long long shared = 0;
        long long flagged = 0;
        long long disagree = 0;
    };
    std::array<Flag, 6> flags{{{"masked", 0x2},
                               {"translucent", 0x4},
                               {"modulated", 0x40},
                               {"sky", 0x80},
                               {"unlit", 0x400000},
                               {"portal", 0x4000000}}};
    long long mapsRead = 0;

    const fs::path maps = fs::path{UTA_UT_INSTALL_DIR} / "Maps";
    for (const fs::directory_entry& entry : fs::directory_iterator(maps)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".unr") continue;
        const auto read = uta::fs::readFile(entry.path());
        REQUIRE(read.has_value());
        const auto package = uta::upkg::Package::open(std::span<const std::byte>{*read});
        if (!package.has_value()) continue;

        const uta::upkg::ExportEntry* largest = nullptr;
        for (const auto& object : package->exports()) {
            const auto className = package->objectName(object.objectClass);
            if (className.has_value() && *className == "Model"
                && (largest == nullptr || object.serialSize > largest->serialSize))
                largest = &object;
        }
        if (largest == nullptr) continue;
        const auto model = uta::upkg::readModel(*package, *largest);
        if (!model.has_value()) continue;
        ++mapsRead;

        std::map<std::int32_t, std::pair<long long, std::array<long long, 6>>> byTexture;
        for (const auto& surface : model->surfs) {
            if (surface.texture.kind() == uta::upkg::ObjectReferenceKind::Null) continue;
            auto& [uses, set] = byTexture[surface.texture.raw()];
            ++uses;
            for (std::size_t f = 0; f < flags.size(); ++f)
                if ((surface.polyFlags & flags[f].mask) != 0) ++set[f];
        }
        for (const auto& [texture, counts] : byTexture) {
            const auto& [uses, set] = counts;
            if (uses < 2) continue;
            for (std::size_t f = 0; f < flags.size(); ++f) {
                ++flags[f].shared;
                if (set[f] > 0) ++flags[f].flagged;
                if (set[f] > 0 && set[f] < uses) ++flags[f].disagree;
            }
        }
    }

    REQUIRE(mapsRead > 0);
    for (const Flag& flag : flags)
        WARN("SS 2 item 3 -- " << flag.name << ": " << flag.disagree << " of " << flag.flagged
                               << " shared textures that set it on some surface disagree ("
                               << flag.shared << " shared textures, " << mapsRead << " maps)");
}

// UTA-0010's census: INV-8, INV-9, and the figures that spec's SS 2 item 2 and
// SS 4.2 rest on, printed so they are an output of the tree.

namespace {

using uta::umat::CuratedEntry;
using uta::umat::CuratedOverride;
using uta::umat::CurationSource;

/// SS 4.7's image check: at least one texel in this many is bright.
constexpr std::size_t SEED_BRIGHT_SHARE = 100;

constexpr std::array<std::string_view, 5> METAL_SOUND_WORDS{"stepmetal", "hitmetal", "fs_metal",
                                                            "metalstep", "metwalk"};
constexpr std::array<std::string_view, 3> METAL_GROUPS{"metal", "metals", "steel"};
constexpr std::array<std::string_view, 15> GLOW_GROUPS{
    "light",       "lights", "lamp", "lamps",  "lightbox", "baselight",   "carlights", "gothiclight",
    "glowpanels",  "lava",   "neon", "screen", "screens",  "panelscreen", "monitors"};

bool isOneOf(std::string_view value, std::span<const std::string_view> set) {
    return std::ranges::find(set, value) != set.end();
}

/// A `Texture` export carrying no Format property whose base level has a
/// fingerprint: SS 4.7's population, in whatever package holds it.
struct CensusTexture {
    std::uint64_t fingerprint = 0;
    std::size_t secondHash = 0; // INV-9's independent hash of the same bytes
    std::string name;           // folded
    std::string group;          // the export `outer` names, folded
    std::vector<std::string> sounds; // FootstepSound and HitSound object names, folded
    std::string note;           // materialId's form
    bool bright = false;        // SS 4.7's image check; run only in a glow group
};

struct FormatFigures {
    long long carrying = 0;
    long long oneByteATexel = 0;
};

/// materialId's `path`: each group holding `entry`, outermost first, then its
/// name. Bounded by the export count, so a cyclic outer chain cannot hang it.
std::string exportPath(const uta::upkg::Package& package, const uta::upkg::ExportEntry& entry) {
    std::string path{package.name(entry.objectName).value_or("")};
    uta::upkg::ObjectReference outer = entry.outer;
    for (std::size_t depth = 0; outer.kind() == uta::upkg::ObjectReferenceKind::Export
                                && depth < package.exports().size();
         ++depth) {
        const uta::upkg::ExportEntry& group = package.exports()[outer.index()];
        path = std::string{package.name(group.objectName).value_or("")} + "." + path;
        outer = group.outer;
    }
    return path;
}

/// Every census texture in one package, adding its Format figures to `format`.
std::vector<CensusTexture> censusTextures(const uta::upkg::Package& package,
                                          std::string_view packageName, FormatFigures& format) {
    std::vector<CensusTexture> out;
    for (const auto& object : package.exports()) {
        const auto className = package.objectName(object.objectClass);
        if (!className.has_value() || *className != "Texture") continue;
        const auto properties = uta::upkg::readProperties(package, object);
        if (!properties.has_value()) continue;

        CensusTexture texture;
        std::optional<uta::upkg::ObjectReference> paletteReference;
        bool hasFormat = false;
        for (const auto& property : *properties) {
            const auto name = package.name(property.nameIndex);
            if (!name.has_value()) continue;
            const auto* reference = std::get_if<uta::upkg::ObjectReference>(&property.value);
            if (*name == "Format") hasFormat = true;
            if (*name == "Palette" && reference != nullptr) paletteReference = *reference;
            if ((*name == "FootstepSound" || *name == "HitSound") && reference != nullptr
                && reference->kind() != uta::upkg::ObjectReferenceKind::Null)
                if (const auto sound = package.objectName(*reference); sound.has_value())
                    texture.sounds.push_back(foldCase(*sound));
        }

        const auto read = uta::upkg::readTexture(package, object);
        if (!read.has_value() || read->mips.empty()) continue;
        const uta::upkg::Mip& base = read->mips[0];
        if (hasFormat) {
            ++format.carrying;
            if (base.pixels.size() == std::size_t{base.width} * base.height) ++format.oneByteATexel;
            continue;
        }
        if (!paletteReference.has_value()
            || paletteReference->kind() != uta::upkg::ObjectReferenceKind::Export)
            continue;
        const auto palette =
            uta::upkg::readPalette(package, package.exports()[paletteReference->index()]);
        if (!palette.has_value()) continue;
        const auto fingerprint = uta::umat::pictureFingerprint(base, *palette);
        if (!fingerprint.has_value()) continue;

        // The fingerprint's input, written out again here and hashed by a
        // different algorithm, so a narrowed fingerprint shows as a collision.
        std::string picture;
        for (const std::uint32_t dimension : {base.width, base.height})
            for (int shift = 0; shift < 32; shift += 8)
                picture.push_back(static_cast<char>(dimension >> shift));
        for (const std::byte index : base.pixels) picture.push_back(static_cast<char>(index));
        for (const auto& entry : palette->entries)
            picture.append({static_cast<char>(entry.r), static_cast<char>(entry.g),
                            static_cast<char>(entry.b)});

        texture.fingerprint = *fingerprint;
        texture.secondHash = std::hash<std::string>{}(picture);
        texture.name = foldCase(package.name(object.objectName).value_or(""));
        texture.group = foldCase(package.objectName(object.outer).value_or(""));
        texture.note = uta::umat::materialId(packageName, exportPath(package, object), false);
        if (isOneOf(texture.group, GLOW_GROUPS)) {
            const auto rgba = uta::umat::resolve(base, *palette, false);
            if (rgba.has_value()) {
                const Image luma = uta::umat::heightOf(*rgba);
                const std::uint8_t threshold = uta::umat::MaterialSettings{}.emissiveThreshold;
                const auto bright = std::ranges::count_if(luma.pixels, [threshold](std::byte l) {
                    return std::to_integer<std::uint8_t>(l) >= threshold;
                });
                texture.bright =
                    static_cast<std::size_t>(bright) * SEED_BRIGHT_SHARE >= luma.pixels.size();
            }
        }
        out.push_back(std::move(texture));
    }
    return out;
}

/// One row of CuratedMaterials.cpp's table.
std::string seedRow(std::uint64_t fingerprint, std::string_view note,
                    const CuratedOverride& settings, CurationSource source) {
    CuratedOverride metal;
    metal.metallic = true;
    CuratedOverride glow;
    glow.emissive = true;
    CuratedOverride both = metal;
    both.emissive = true;
    const char* shape = settings == metal  ? "METAL"
                        : settings == glow ? "GLOW"
                        : settings == both ? "METAL_GLOW"
                                           : "/* not a seed shape */";
    const char* why = source == CurationSource::MetalSound  ? "MetalSound"
                      : source == CurationSource::GroupName ? "GroupName"
                                                            : "Play";
    // Some install files are named with a Windows path, `Textures\X.utx`, and
    // that backslash reaches the note.
    std::string literal;
    for (const char character : note) {
        if (character == '\\' || character == '"') literal.push_back('\\');
        literal.push_back(character);
    }
    return std::format("    {{0x{:016x}ULL, \"{}\", CurationSource::{}, {}}},", fingerprint,
                       literal, why, shape);
}

} // namespace

TEST_CASE("the curated seed is what its rules derive over the install", "[real-assets][umat]") {
    // UTA-0010 INV-8 and INV-9, and the figures SS 2 item 2 and SS 4.2 rest
    // on. Every seed entry the table lacks is printed to stdout as a row
    // CuratedMaterials.cpp can take as it stands.
    struct Packaged {
        std::string note; // the first copy in sorted file order
        std::set<std::string> names;
        bool metalSound = false;
        bool metalGroup = false;
        bool glowGroup = false;
        bool glow = false; // a glow group AND the image check
    };
    const fs::path root{UTA_UT_INSTALL_DIR};
    FormatFigures format;
    std::map<std::uint64_t, std::size_t> secondHashes;
    long long collisions = 0;
    const auto recordHash = [&](const CensusTexture& texture) {
        const auto [at, inserted] = secondHashes.try_emplace(texture.fingerprint, texture.secondHash);
        if (inserted || at->second == texture.secondHash) return;
        ++collisions;
        WARN("INV-9 -- fingerprint " << std::format("{:016x}", texture.fingerprint)
                                     << " carries two pictures, one of them " << texture.note);
    };

    std::map<std::uint64_t, Packaged> packaged;
    long long population = 0;
    for (const fs::path& path : sortedPackages(root / "Textures", ".utx")) {
        const auto read = uta::fs::readFile(path);
        REQUIRE(read.has_value());
        const auto package = uta::upkg::Package::open(std::span<const std::byte>{*read});
        if (!package.has_value()) continue;
        for (const CensusTexture& texture : censusTextures(*package, path.stem().string(), format)) {
            ++population;
            recordHash(texture);
            Packaged& picture = packaged[texture.fingerprint];
            if (picture.note.empty()) picture.note = texture.note;
            picture.names.insert(texture.name);
            for (const std::string& sound : texture.sounds)
                for (const std::string_view word : METAL_SOUND_WORDS)
                    if (sound.find(word) != std::string::npos) picture.metalSound = true;
            if (isOneOf(texture.group, METAL_GROUPS)) picture.metalGroup = true;
            if (isOneOf(texture.group, GLOW_GROUPS)) {
                picture.glowGroup = true;
                if (texture.bright) picture.glow = true;
            }
        }
    }

    long long embedded = 0;
    long long copies = 0;
    long long renamed = 0;
    long long mapsRead = 0;
    long long mapsHoldingACopy = 0;
    for (const fs::path& path : sortedPackages(root / "Maps", ".unr")) {
        const auto read = uta::fs::readFile(path);
        REQUIRE(read.has_value());
        const auto package = uta::upkg::Package::open(std::span<const std::byte>{*read});
        if (!package.has_value()) continue;
        ++mapsRead;
        bool holdsACopy = false;
        for (const CensusTexture& texture : censusTextures(*package, path.stem().string(), format)) {
            ++embedded;
            recordHash(texture);
            const auto found = packaged.find(texture.fingerprint);
            if (found == packaged.end()) continue;
            ++copies;
            holdsACopy = true;
            if (!found->second.names.contains(texture.name)) ++renamed;
        }
        if (holdsACopy) ++mapsHoldingACopy;
    }

    // SS 4.7's seed, derived; then both sides with every Play fingerprint removed.
    std::map<std::uint64_t, std::pair<CuratedOverride, CurationSource>> derived;
    long long metalBySound = 0;
    long long metalByGroup = 0;
    long long glowCandidates = 0;
    long long glowing = 0;
    for (const auto& [fingerprint, picture] : packaged) {
        metalBySound += picture.metalSound ? 1 : 0;
        metalByGroup += picture.metalGroup ? 1 : 0;
        glowCandidates += picture.glowGroup ? 1 : 0;
        glowing += picture.glow ? 1 : 0;
        if (!picture.metalSound && !picture.metalGroup && !picture.glow) continue;
        CuratedOverride settings;
        if (picture.metalSound || picture.metalGroup) settings.metallic = true;
        if (picture.glow) settings.emissive = true;
        derived.emplace(fingerprint,
                        std::pair{settings, picture.metalSound ? CurationSource::MetalSound
                                                               : CurationSource::GroupName});
    }
    std::map<std::uint64_t, const CuratedEntry*> table;
    std::set<std::uint64_t> play;
    for (const CuratedEntry& entry : uta::umat::curatedLibrary()) {
        if (entry.source == CurationSource::Play) play.insert(entry.fingerprint);
        else table.emplace(entry.fingerprint, &entry);
    }
    for (const std::uint64_t fingerprint : play) {
        derived.erase(fingerprint);
        table.erase(fingerprint);
    }

    long long missing = 0;
    long long extra = 0;
    for (const auto& [fingerprint, want] : derived) {
        const auto found = table.find(fingerprint);
        if (found != table.end() && found->second->settings == want.first
            && found->second->source == want.second)
            continue;
        ++missing;
        std::cout << seedRow(fingerprint, packaged.at(fingerprint).note, want.first, want.second)
                  << '\n';
    }
    for (const auto& [fingerprint, entry] : table)
        if (!derived.contains(fingerprint)) {
            ++extra;
            std::cout << "extra: "
                      << seedRow(fingerprint, entry->note, entry->settings, entry->source) << '\n';
        }

    WARN("SS 4.7 population -- " << population << " textures, " << packaged.size()
                                 << " pictures; metal by sound " << metalBySound
                                 << ", metal by group " << metalByGroup << ", glow group "
                                 << glowCandidates << " of which " << glowing
                                 << " pass the image check");
    WARN("SS 2 item 2 -- embedded textures copying a packaged picture: "
         << copies << " of " << embedded << ", renamed " << renamed << ", in "
         << mapsHoldingACopy << " of " << mapsRead << " maps");
    WARN("SS 4.2 -- textures carrying a Format property: " << format.carrying
                                                           << ", of those one byte a texel: "
                                                           << format.oneByteATexel);
    WARN("INV-8 -- seed entries missing " << missing << ", extra " << extra << "; Play entries "
                                          << play.size());

    REQUIRE(population > 0);
    CHECK(collisions == 0);
    // UTA-0132: the seed table was derived from the reference install, so it
    // matches that install and no other. Elsewhere the counts are printed above.
    if (UTA_REFERENCE_INSTALL) {
        CHECK(missing == 0);
        CHECK(extra == 0);
    } else {
        WARN("not the reference install (UTA_REFERENCE_INSTALL is OFF): the seed table was not compared");
    }
}
