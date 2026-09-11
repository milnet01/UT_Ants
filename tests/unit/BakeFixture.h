// Synthetic maps, packages and installs for UTA-0011's tests.
//
// docs/specs/UTA-0011-map-baker.md SS 7: fixtures are synthetic packages built
// with tests/support/UnrealPackageBuilder.h, so every case runs with no Unreal
// Tournament present (S7). Shared by BakeTest, BakeGoldenTest, BakeCliTest and
// BakeInstallTest -- four callers, past coding.md's Rule of Three.
//
// THE STANDARD FIXTURE EXERCISES EVERY ROUTE A BAKE TAKES INTO ANOTHER PACKAGE,
// which is what INV-2 needs: a texture imported from a texture package, and an
// actor whose class lives in a class package.

#pragma once

#include "upkg/Class.h"
#include "upkg/Package.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace uta::test::bake {

/// PF_Masked (UTA-0011 SS 4.6).
inline constexpr std::uint32_t MASKED = 0x00000002u;

/// A 4x4 palettised picture and its four-colour palette. Every texel and
/// colour depends on `seed`, so two seeds give two fingerprints.
struct Picture {
    std::uint32_t width = 4;
    std::uint32_t height = 4;
    std::vector<std::uint8_t> indices;
    std::vector<std::array<std::uint8_t, 4>> palette;
};

[[nodiscard]] Picture picture(std::uint8_t seed);

struct TextureSpec {
    std::string name;
    std::string group;   ///< empty: the texture sits at the package's top level
    Picture picture;
    bool format = false; ///< carries a Format property -- INV-9
    float drawScale = 0; ///< 0: carries no DrawScale property -- UTA-0109 INV-11
};

/// A package under construction: its three tables, kept consistent, and the
/// fixups a texture's WidthOffset needs once the file's layout is known.
class Packer {
public:
    Packer();

    /// A name's index, added the first time it is asked for. `None` is 0.
    std::int32_t name(std::string_view text);

    /// An import naming a package: its outer is null.
    std::int32_t importPackage(std::string_view package);
    /// An import of `<package>.<className>`, a class.
    std::int32_t importClass(std::string_view package, std::string_view className);
    /// Any import. Repeated arguments return the first one's reference.
    std::int32_t importObject(std::string_view classPackage, std::string_view className,
                              std::int32_t outer, std::string_view objectName);

    std::int32_t addExport(std::int32_t objectClass, std::int32_t outer, std::string_view name,
                           std::vector<std::uint8_t> data);

    /// A group export of this package, added the first time it is asked for.
    std::int32_t group(std::string_view name);

    /// A palette export and a texture export using it; the texture's reference.
    std::int32_t addTexture(const TextureSpec& texture);

    /// A root class export named `className`.
    std::int32_t addClass(std::string_view className);

    [[nodiscard]] std::size_t exportCount() const noexcept;

    [[nodiscard]] std::vector<std::uint8_t> build() const;

private:
    struct Import {
        std::int32_t classPackage = 0;
        std::int32_t className = 0;
        std::int32_t outer = 0;
        std::int32_t objectName = 0;
    };
    struct Export {
        std::int32_t objectClass = 0;
        std::int32_t outer = 0;
        std::int32_t objectName = 0;
        std::vector<std::uint8_t> data;
    };
    /// WidthOffset is an offset into the whole FILE (UTA-0004 SS 4.6), so it
    /// is written once the layout is known.
    struct MipFixup {
        std::size_t exportIndex = 0;
        std::size_t fieldAt = 0;   ///< where the u32 sits within the export's data
        std::size_t dataEnd = 0;   ///< where the mip's pixels end within it
    };

    std::vector<std::string> names_;
    std::map<std::string, std::int32_t, std::less<>> nameIndex_;
    std::vector<Import> imports_;
    std::vector<Export> exports_;
    std::map<std::string, std::int32_t, std::less<>> groups_;
    std::vector<MipFixup> fixups_;
};

/// Builds a map: textures, a Model over them, a Level naming it, and actors.
class MapBuilder {
public:
    enum class ModelTarget { Model, Null, NotAModel };

    MapBuilder();

    /// A texture the map itself exports; the reference a surface names it by.
    std::int32_t addTexture(const TextureSpec& texture);

    /// A texture the map imports as `<package>.<group>.<name>`, or as
    /// `<package>.<name>` when `group` is empty.
    std::int32_t importTexture(std::string_view package, std::string_view group,
                               std::string_view name);

    MapBuilder& addSurface(std::int32_t texture, std::uint32_t polyFlags = 0);
    MapBuilder& addActorOfClass(std::string_view package, std::string_view className);
    MapBuilder& setLevelCount(int count);
    /// A second, larger Model export the level does not name -- INV-13.
    MapBuilder& addDecoyModel();
    MapBuilder& setModelTarget(ModelTarget target);

    [[nodiscard]] std::vector<std::uint8_t> build() const;

    /// The rooms the level's own Model yields, and the decoy's.
    static constexpr std::size_t ROOMS = 2;
    static constexpr std::size_t DECOY_ROOMS = 3;

private:
    Packer packer_;
    struct Surface {
        std::int32_t texture = 0;
        std::uint32_t polyFlags = 0;
    };
    std::vector<Surface> surfaces_;
    std::vector<std::pair<std::string, std::string>> actorClasses_;
    int levelCount_ = 1;
    bool decoy_ = false;
    ModelTarget target_ = ModelTarget::Model;
};

/// A texture package: each texture and its palette.
[[nodiscard]] std::vector<std::uint8_t> texturePackage(const std::vector<TextureSpec>& textures);

/// A package holding one root class export.
[[nodiscard]] std::vector<std::uint8_t> classPackage(std::string_view className);

/// A package that opens and holds one sizeless export named `exportName`.
[[nodiscard]] std::vector<std::uint8_t> tinyPackage(std::string_view exportName);

/// What most cases bake: a map named `dm-fixture` whose surfaces name a map
/// texture in a group (masked and unmasked), a map texture with no group, and
/// `TexPkg.Metal.Plate`; and whose level holds one actor of `ActorPkg.Lamp`.
struct Fixture {
    MapBuilder map;
    std::vector<TextureSpec> packageTextures; ///< what TexPkg holds
};

[[nodiscard]] Fixture standardFixture();

inline constexpr std::string_view MAP_NAME = "dm-fixture";

/// The material ids the standard fixture makes, ascending.
inline const std::vector<std::string> STANDARD_MATERIALS = {
    "dm-fixture.base.wall",
    "dm-fixture.base.wall#masked",
    "dm-fixture.floor",
    "texpkg.metal.plate",
};

/// Packages held in memory, opened on first use, with a resolver over them
/// -- a stand-in for an Install when no file needs to exist.
class MemoryPackages {
public:
    /// `name` is folded as the resolver expects its input.
    void add(std::string name, std::vector<std::uint8_t> bytes);
    [[nodiscard]] upkg::PackageResolver resolver();

private:
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> bytes_;
    std::map<std::string, upkg::Package, std::less<>> opened_;
};

/// The standard fixture's other packages: TexPkg, ActorPkg, Core and Engine.
[[nodiscard]] MemoryPackages memoryPackagesFor(const Fixture& fixture);

/// Writes `bytes` to `path`, creating its directory.
void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

/// Writes an install holding the standard fixture's packages under `root`:
/// System/Core.u, Engine.u, Botpack.u and ActorPkg.u, Textures/TexPkg.utx,
/// and the map as Maps/DM-Fixture.unr. Returns the map's path.
std::filesystem::path writeInstall(const std::filesystem::path& root, const Fixture& fixture);

/// A directory that removes itself, as CoreFileSystemTest.cpp's does.
class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::vector<std::byte> asByteVector(const std::vector<std::uint8_t>& bytes);

} // namespace uta::test::bake
