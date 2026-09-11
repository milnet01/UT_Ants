#include "BakeFixture.h"

#include "support/UnrealPackageBuilder.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <random>
#include <system_error>
#include <utility>

namespace uta::test::bake {
namespace {

void appendU8(std::vector<std::uint8_t>& into, std::uint8_t value) {
    into.push_back(value);
}

void appendU32(std::vector<std::uint8_t>& into, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        into.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
}

void appendIndex(std::vector<std::uint8_t>& into, std::int32_t value) {
    const std::vector<std::uint8_t> encoded = encodeCompactIndex(value);
    into.insert(into.end(), encoded.begin(), encoded.end());
}

std::int32_t exportRef(std::size_t index) {
    return static_cast<std::int32_t>(index) + 1;
}

std::int32_t importRef(std::size_t index) {
    return -static_cast<std::int32_t>(index) - 1;
}

std::vector<std::uint8_t> emptyProperties() {
    return TaggedPropertyWriter{}.build(0);
}

std::string folded(std::string_view text) {
    std::string out{text};
    for (char& character : out)
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    return out;
}

} // namespace

// -------------------------------------------------------------- properties

PropertySpec byteProperty(std::string name, std::uint8_t value) {
    PropertySpec spec{std::move(name), PropertySpec::Type::Byte};
    spec.value = value;
    return spec;
}

PropertySpec intProperty(std::string name, std::int32_t value) {
    PropertySpec spec{std::move(name), PropertySpec::Type::Int};
    spec.value = value;
    return spec;
}

PropertySpec boolProperty(std::string name, bool value) {
    PropertySpec spec{std::move(name), PropertySpec::Type::Bool};
    spec.value = value ? 1 : 0;
    return spec;
}

PropertySpec floatProperty(std::string name, float value) {
    PropertySpec spec{std::move(name), PropertySpec::Type::Float};
    spec.number = value;
    return spec;
}

PropertySpec vectorProperty(std::string name, float x, float y, float z) {
    PropertySpec spec{std::move(name), PropertySpec::Type::Vector};
    spec.vector = {x, y, z};
    return spec;
}

PropertySpec rotatorProperty(std::string name, std::int32_t pitch, std::int32_t yaw,
                             std::int32_t roll) {
    PropertySpec spec{std::move(name), PropertySpec::Type::Rotator};
    spec.rotator = {pitch, yaw, roll};
    return spec;
}

PropertySpec objectProperty(std::string name, std::int32_t reference) {
    PropertySpec spec{std::move(name), PropertySpec::Type::Object};
    spec.value = reference;
    return spec;
}

PropertySpec nameProperty(std::string name, std::string text) {
    PropertySpec spec{std::move(name), PropertySpec::Type::Name};
    spec.text = std::move(text);
    return spec;
}

PropertySpec scaleProperty(std::string name, float x, float y, float z, float rate,
                           std::int32_t axis) {
    PropertySpec spec{std::move(name), PropertySpec::Type::Scale};
    spec.vector = {x, y, z};
    spec.rate = rate;
    spec.value = axis;
    return spec;
}

Picture picture(std::uint8_t seed) {
    Picture out;
    // Every index 0 to 3 appears, so the masked variant has index-0 texels to
    // cut out and ordinary ones to fill them from.
    for (std::uint32_t texel = 0; texel < out.width * out.height; ++texel)
        out.indices.push_back(static_cast<std::uint8_t>((texel * 3 + seed) % 4));
    for (std::uint32_t colour = 0; colour < 4; ++colour)
        out.palette.push_back({static_cast<std::uint8_t>(seed * 37 + colour * 60),
                               static_cast<std::uint8_t>(200 - colour * 45 + seed),
                               static_cast<std::uint8_t>(colour * 70 + seed * 11), 255});
    return out;
}

// ------------------------------------------------------------------ Packer

Packer::Packer() {
    name("None");
}

std::int32_t Packer::name(std::string_view text) {
    if (const auto found = nameIndex_.find(text); found != nameIndex_.end()) return found->second;
    const auto index = static_cast<std::int32_t>(names_.size());
    names_.emplace_back(text);
    nameIndex_.emplace(std::string(text), index);
    return index;
}

std::int32_t Packer::importObject(std::string_view classPackage, std::string_view className,
                                  std::int32_t outer, std::string_view objectName) {
    const Import wanted{name(classPackage), name(className), outer, name(objectName)};
    for (std::size_t i = 0; i < imports_.size(); ++i) {
        const Import& have = imports_[i];
        if (have.classPackage == wanted.classPackage && have.className == wanted.className
            && have.outer == wanted.outer && have.objectName == wanted.objectName)
            return importRef(i);
    }
    imports_.push_back(wanted);
    return importRef(imports_.size() - 1);
}

std::int32_t Packer::importPackage(std::string_view package) {
    return importObject("Core", "Package", 0, package);
}

std::int32_t Packer::importClass(std::string_view package, std::string_view className) {
    return importObject("Core", "Class", importPackage(package), className);
}

std::int32_t Packer::addExport(std::int32_t objectClass, std::int32_t outer, std::string_view text,
                               std::vector<std::uint8_t> data) {
    exports_.push_back(Export{objectClass, outer, name(text), std::move(data)});
    return exportRef(exports_.size() - 1);
}

std::int32_t Packer::group(std::string_view text) {
    if (const auto found = groups_.find(text); found != groups_.end()) return found->second;
    const std::int32_t reference = addExport(importClass("Core", "Package"), 0, text, {});
    groups_.emplace(std::string(text), reference);
    return reference;
}

std::int32_t Packer::addTexture(const TextureSpec& texture) {
    const std::int32_t outer = texture.group.empty() ? 0 : group(texture.group);

    std::vector<std::uint8_t> paletteData = emptyProperties();
    appendIndex(paletteData, static_cast<std::int32_t>(texture.picture.palette.size()));
    for (const auto& colour : texture.picture.palette)
        paletteData.insert(paletteData.end(), colour.begin(), colour.end());
    const std::int32_t palette = addExport(importClass("Engine", "Palette"), outer,
                                           texture.name + "Pal", std::move(paletteData));

    TaggedPropertyWriter properties;
    properties.addObject(name("Palette"), palette);
    if (texture.format) properties.addByte(name("Format"), 1);
    if (texture.drawScale != 0) properties.addFloat(name("DrawScale"), texture.drawScale);

    std::vector<std::uint8_t> data = properties.build(0);
    appendU8(data, 1); // one mip
    const std::size_t fieldAt = data.size();
    appendU32(data, 0); // WidthOffset, fixed up by build()
    appendIndex(data, static_cast<std::int32_t>(texture.picture.indices.size()));
    data.insert(data.end(), texture.picture.indices.begin(), texture.picture.indices.end());
    const std::size_t dataEnd = data.size();
    appendU32(data, texture.picture.width);
    appendU32(data, texture.picture.height);
    appendU8(data, 2); // bitsWidth: log2 of 4
    appendU8(data, 2);

    const std::int32_t reference =
        addExport(importClass("Engine", "Texture"), outer, texture.name, std::move(data));
    fixups_.push_back(MipFixup{exports_.size() - 1, fieldAt, dataEnd});
    return reference;
}

std::int32_t Packer::addClass(std::string_view className, std::int32_t super,
                              const std::vector<PropertySpec>& defaults) {
    ClassExportWriter writer;
    writer.setSuperField(super).setFriendlyName(name(className)).setDefaults(properties(defaults));
    const std::int32_t reference =
        addExport(0, 0, className, writer.build(68)); // a null class: a class export
    exports_.back().super = super;
    return reference;
}

std::vector<std::uint8_t> Packer::properties(const std::vector<PropertySpec>& specs) {
    TaggedPropertyWriter writer;
    for (const PropertySpec& spec : specs) {
        const std::int32_t key = name(spec.name);
        switch (spec.type) {
        case PropertySpec::Type::Byte: writer.addByte(key, static_cast<std::uint8_t>(spec.value)); break;
        case PropertySpec::Type::Int: writer.addInt(key, spec.value); break;
        case PropertySpec::Type::Bool: writer.addBool(key, spec.value != 0); break;
        case PropertySpec::Type::Vector:
            writer.addVector(key, spec.vector[0], spec.vector[1], spec.vector[2]);
            break;
        case PropertySpec::Type::Rotator:
            writer.addRotator(key, spec.rotator[0], spec.rotator[1], spec.rotator[2]);
            break;
        case PropertySpec::Type::Object: writer.addObject(key, spec.value); break;
        case PropertySpec::Type::Name: writer.addName(key, name(spec.text)); break;
        case PropertySpec::Type::Float: writer.addFloat(key, spec.number); break;
        case PropertySpec::Type::Scale: {
            std::vector<std::uint8_t> raw;
            for (const float part : spec.vector) appendU32(raw, std::bit_cast<std::uint32_t>(part));
            appendU32(raw, std::bit_cast<std::uint32_t>(spec.rate));
            appendU8(raw, static_cast<std::uint8_t>(spec.value));
            writer.addUndecodedStruct(key, name("Scale"), raw);
            break;
        }
        }
    }
    return writer.build(0);
}

std::size_t Packer::exportCount() const noexcept {
    return exports_.size();
}

std::vector<std::uint8_t> Packer::build() const {
    UnrealPackageBuilder builder;
    for (const std::string& text : names_) builder.addName(text);
    for (const Import& import : imports_)
        builder.addImport(
            ImportEntry{import.classPackage, import.className, import.outer, import.objectName});
    for (const Export& object : exports_) {
        ExportEntry entry;
        entry.objectClass = object.objectClass;
        entry.super = object.super;
        entry.outer = object.outer;
        entry.objectName = object.objectName;
        entry.serialData = object.data;
        builder.addExport(entry);
    }
    std::vector<std::uint8_t> bytes = builder.build();
    if (fixups_.empty()) return bytes;

    // Where each export landed, read back rather than recomputed: the builder
    // owns the layout, and a second copy of it here would drift.
    std::vector<std::size_t> offsets;
    {
        const auto package = upkg::Package::open(asBytes(bytes));
        if (!package.has_value()) return bytes; // every case using it then fails on it
        for (const MipFixup& fixup : fixups_)
            offsets.push_back(package->exports()[fixup.exportIndex].serialOffset);
    }
    for (std::size_t i = 0; i < fixups_.size(); ++i) {
        const auto value = static_cast<std::uint32_t>(offsets[i] + fixups_[i].dataEnd);
        for (std::size_t part = 0; part < 4; ++part)
            bytes[offsets[i] + fixups_[i].fieldAt + part] =
                static_cast<std::uint8_t>((value >> (8 * part)) & 0xFFu);
    }
    return bytes;
}

// -------------------------------------------------------------- MapBuilder

MapBuilder::MapBuilder() = default;

std::int32_t MapBuilder::addTexture(const TextureSpec& texture) {
    return packer_.addTexture(texture);
}

std::int32_t MapBuilder::importTexture(std::string_view package, std::string_view group,
                                       std::string_view name) {
    std::int32_t outer = packer_.importPackage(package);
    if (!group.empty()) outer = packer_.importObject("Core", "Package", outer, group);
    return packer_.importObject("Engine", "Texture", outer, name);
}

MapBuilder& MapBuilder::addSurface(std::int32_t texture, std::uint32_t polyFlags) {
    surfaces_.push_back(Surface{texture, polyFlags});
    return *this;
}

std::int32_t MapBuilder::importClass(std::string_view package, std::string_view className) {
    return packer_.importClass(package, className);
}

std::int32_t MapBuilder::addClass(std::string_view className, std::int32_t super,
                                  const std::vector<PropertySpec>& defaults) {
    return packer_.addClass(className, super, defaults);
}

MapBuilder& MapBuilder::addActorOfClass(std::string_view package, std::string_view className,
                                        std::vector<PropertySpec> properties) {
    return addActor(std::string(className) + std::to_string(actors_.size()),
                    packer_.importClass(package, className), std::move(properties));
}

MapBuilder& MapBuilder::addActor(std::string_view name, std::int32_t classReference,
                                 std::vector<PropertySpec> properties) {
    actors_.push_back(Actor{std::string(name), classReference, std::move(properties)});
    return *this;
}

MapBuilder& MapBuilder::addReachSpec(std::size_t from, std::size_t to,
                                     std::int32_t collisionRadius, std::int32_t collisionHeight,
                                     std::int32_t reachFlags) {
    reaches_.push_back(Reach{from, to, collisionRadius, collisionHeight, reachFlags});
    return *this;
}

std::int32_t MapBuilder::addBrushModel(const BrushSpec& brush) {
    ModelExportWriter model;
    std::array<float, 3> low = brush.corners[0];
    std::array<float, 3> high = brush.corners[0];
    for (const auto& corner : brush.corners)
        for (std::size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], corner[axis]);
            high[axis] = std::max(high[axis], corner[axis]);
        }
    model.setBounds(low, high, true);
    // Vectors 0, 1 and 2 are the surface's normal, TextureU and TextureV.
    model.addVector(brush.normal).addVector(brush.textureU).addVector(brush.textureV);
    for (const auto& corner : brush.corners) model.addPoint(corner);
    for (std::int32_t k = 0; k < 4; ++k) model.addVert(k);

    ModelExportWriter::Node square;
    square.normal = brush.normal;
    square.iSurf = brush.iSurf;
    square.iVertPool = 0;
    square.numVertices = 4;
    square.iPlane = brush.iPlane;
    model.addNode(square);

    ModelExportWriter::Surf surf;
    surf.texture = brush.texture;
    surf.polyFlags = brush.polyFlags;
    surf.pBase = 0;
    surf.vNormal = 0;
    surf.vTextureU = 1;
    surf.vTextureV = 2;
    model.addSurf(surf);

    return packer_.addExport(packer_.importClass("Engine", "Model"), 0,
                             "Brush" + std::to_string(brushes_++), model.build());
}

std::int32_t MapBuilder::addRawExport(std::string_view package, std::string_view className,
                                      std::string_view name, std::vector<std::uint8_t> data) {
    return packer_.addExport(packer_.importClass(package, className), 0, name, std::move(data));
}

MapBuilder& MapBuilder::setLevelCount(int count) {
    levelCount_ = count;
    return *this;
}

MapBuilder& MapBuilder::addDecoyModel() {
    decoy_ = true;
    return *this;
}

MapBuilder& MapBuilder::repeatActorSlot(std::size_t actor) {
    repeatedSlots_.push_back(actor);
    return *this;
}

MapBuilder& MapBuilder::setModelTarget(ModelTarget target) {
    target_ = target;
    return *this;
}

MapBuilder& MapBuilder::setFloorCoplanar(std::int32_t iPlane) {
    floorCoplanar_ = iPlane;
    return *this;
}

std::vector<std::uint8_t> MapBuilder::build() const {
    Packer packer = packer_;

    std::vector<std::int32_t> actors;
    for (const Actor& actor : actors_)
        actors.push_back(packer.addExport(actor.classReference, 0, actor.name,
                                          packer.properties(actor.properties)));

    // The level's world: one plane with a zone on each side, inside a cube the
    // room builder samples at its default spacing -- RoomBuildTest.cpp's
    // twoZoneModel, as bytes. Zone 0 is the engine's null zone.
    ModelExportWriter model;
    model.setBounds({-64.0F, -64.0F, -64.0F}, {64.0F, 64.0F, 64.0F}, true);
    ModelExportWriter::Node floor;
    floor.normal = {0.0F, 0.0F, 1.0F};
    floor.iLeaf = {1, 0};
    floor.iPlane = floorCoplanar_;
    model.addNode(floor).setZoneCount(3).addLeaf(1).addLeaf(2);

    // Each surface draws a 64-unit square, one above the next, so the golden
    // bake covers GEOM (UTA-0109 SS 7). Its node hangs off no other, so the
    // room builder's descent from node 0 never reaches it. Vectors 0, 1 and 2
    // are every surface's normal, TextureU and TextureV.
    model.addVector({0.0F, 0.0F, 1.0F}).addVector({1.0F, 0.0F, 0.0F}).addVector({0.0F, 1.0F, 0.0F});
    for (std::size_t i = 0; i < surfaces_.size(); ++i) {
        const float z = 8.0F * static_cast<float>(i);
        const auto first = static_cast<std::int32_t>(4 * i);
        for (const auto& [x, y] : {std::pair{0.0F, 0.0F}, {64.0F, 0.0F}, {64.0F, 64.0F}, {0.0F, 64.0F}})
            model.addPoint({x, y, z});
        for (std::int32_t k = 0; k < 4; ++k) model.addVert(first + k);

        ModelExportWriter::Node square;
        square.normal = {0.0F, 0.0F, 1.0F};
        square.w = z;
        square.iSurf = static_cast<std::int32_t>(i);
        square.iVertPool = first;
        square.numVertices = 4;
        model.addNode(square);

        ModelExportWriter::Surf surf;
        surf.texture = surfaces_[i].texture;
        surf.polyFlags = surfaces_[i].polyFlags;
        surf.pBase = first;
        surf.vNormal = 0;
        surf.vTextureU = 1;
        surf.vTextureV = 2;
        model.addSurf(surf);
    }
    const std::int32_t modelRef =
        packer.addExport(packer.importClass("Engine", "Model"), 0, "Model0", model.build());

    if (decoy_) {
        // Larger in every table the baker could rank by -- nodes, leaves,
        // zones and surfaces -- and yielding a different room count, so a
        // baker taking the largest Model shows in ROOM.
        ModelExportWriter bigger;
        bigger.setBounds({-64.0F, -64.0F, -64.0F}, {64.0F, 64.0F, 64.0F}, true);
        ModelExportWriter::Node split;
        split.normal = {0.0F, 0.0F, 1.0F};
        split.iFront = 1;
        split.iLeaf = {-1, 2};
        ModelExportWriter::Node east;
        east.normal = {1.0F, 0.0F, 0.0F};
        east.iLeaf = {0, 1};
        bigger.addNode(split).addNode(east).setZoneCount(4).addLeaf(1).addLeaf(2).addLeaf(3);
        // One undrawn node for each square the level's own Model carries, so
        // the decoy stays the larger in nodes too. Like those squares, none
        // hangs off another node, so the decoy's room count does not move.
        for (std::size_t i = 0; i < surfaces_.size(); ++i) bigger.addNode(ModelExportWriter::Node{});
        for (int copy = 0; copy < 2; ++copy)
            for (const Surface& surface : surfaces_) bigger.addSurf(surface.texture, surface.polyFlags);
        packer.addExport(packer.importClass("Engine", "Model"), 0, "Model1", bigger.build());
    }

    std::int32_t target = modelRef;
    if (target_ == ModelTarget::Null) target = 0;
    if (target_ == ModelTarget::NotAModel) target = actors.empty() ? exportRef(0) : actors.front();

    for (int index = 0; index < levelCount_; ++index) {
        LevelExportWriter level;
        level.setProperties(emptyProperties());
        for (const std::int32_t actor : actors) level.addActor(actor);
        for (const std::size_t repeat : repeatedSlots_) level.addActor(actors.at(repeat));
        level.addActor(0); // a null slot, as a stock map's array is full of
        for (const Reach& reach : reaches_)
            level.addReachSpec(100, actors.at(reach.from), actors.at(reach.to), reach.collisionRadius,
                               reach.collisionHeight, reach.reachFlags, 0);
        level.setModel(target);
        level.setTrailerFloat(1.0F);
        packer.addExport(packer.importClass("Engine", "Level"), 0, "MyLevel", level.build());
    }
    return packer.build();
}

// ------------------------------------------------------------ the packages

std::vector<std::uint8_t> texturePackage(const std::vector<TextureSpec>& textures) {
    Packer packer;
    for (const TextureSpec& texture : textures) packer.addTexture(texture);
    return packer.build();
}

std::vector<std::uint8_t> classPackage(std::string_view className) {
    Packer packer;
    const std::int32_t base =
        packer.addClass(std::string(className) + "Base", 0,
                        {byteProperty("LightType", 1), byteProperty("LightBrightness", 64)});
    packer.addClass(className, base);
    return packer.build();
}

std::vector<std::uint8_t> enginePackage() {
    Packer packer;
    const std::int32_t brush = packer.addClass("Brush", 0, {boolProperty("bStatic", true)});
    packer.addClass("Mover", brush, {boolProperty("bStatic", false)});
    packer.addExport(0, 0, "Actor", {});
    return packer.build();
}

std::vector<std::uint8_t> tinyPackage(std::string_view exportName) {
    Packer packer;
    packer.addExport(0, 0, exportName, {});
    return packer.build();
}

Fixture standardFixture() {
    Fixture fixture;
    fixture.packageTextures = {TextureSpec{"Plate", "Metal", picture(3), false}};
    MapBuilder& map = fixture.map;
    const std::int32_t wall = map.addTexture(TextureSpec{"Wall", "Base", picture(1), false});
    const std::int32_t floor = map.addTexture(TextureSpec{"Floor", "", picture(2), false});
    const std::int32_t plate = map.importTexture("TexPkg", "Metal", "Plate");
    map.addSurface(wall).addSurface(wall, MASKED).addSurface(floor).addSurface(plate);
    map.addActorOfClass("ActorPkg", "Lamp",
                        {vectorProperty("Location", 16.0F, 32.0F, 48.0F), byteProperty("LightHue", 40)});

    // A mover wearing a texture no level surface wears, so the golden bake
    // covers MOVR and a material only a mover makes (UTA-0119 SS 7).
    fixture.packageTextures.push_back(TextureSpec{"Door", "Metal", picture(4), false});
    BrushSpec door;
    door.corners = {{{0, 0, 0}, {64, 0, 0}, {64, 0, 64}, {0, 0, 64}}};
    door.normal = {0, -1, 0};
    door.textureV = {0, 0, 1};
    door.texture = map.importTexture("TexPkg", "Metal", "Door");
    map.addActor("Door0", map.importClass("Engine", "Mover"),
                 {objectProperty("Brush", map.addBrushModel(door)),
                  vectorProperty("Location", 128.0F, 0.0F, 0.0F)});
    return fixture;
}

// --------------------------------------------------------- MemoryPackages

void MemoryPackages::add(std::string name, std::vector<std::uint8_t> bytes) {
    bytes_[folded(name)] = std::move(bytes);
}

upkg::PackageResolver MemoryPackages::resolver() {
    return [this](std::string_view name) -> Result<const upkg::Package*> {
        const std::string key = folded(name);
        if (const auto opened = opened_.find(key); opened != opened_.end()) return &opened->second;
        const auto bytes = bytes_.find(key);
        if (bytes == bytes_.end()) return nullptr;
        auto package = upkg::Package::open(asBytes(bytes->second));
        if (!package.has_value()) return nullptr;
        return &opened_.emplace(key, std::move(*package)).first->second;
    };
}

MemoryPackages memoryPackagesFor(const Fixture& fixture) {
    MemoryPackages packages;
    packages.add("texpkg", texturePackage(fixture.packageTextures));
    packages.add("actorpkg", classPackage("Lamp"));
    packages.add("core", tinyPackage("Object"));
    packages.add("engine", enginePackage());
    return packages;
}

// ------------------------------------------------------------------ disk

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::filesystem::path writeInstall(const std::filesystem::path& root, const Fixture& fixture) {
    writeFile(root / "System" / "Core.u", tinyPackage("Object"));
    writeFile(root / "System" / "Engine.u", enginePackage());
    writeFile(root / "System" / "Botpack.u", tinyPackage("DeathMatchPlus"));
    writeFile(root / "System" / "ActorPkg.u", classPackage("Lamp"));
    writeFile(root / "Textures" / "TexPkg.utx", texturePackage(fixture.packageTextures));
    const std::filesystem::path map = root / "Maps" / "DM-Fixture.unr";
    writeFile(map, fixture.map.build());
    return map;
}

TempDir::TempDir() {
    // Not getpid(): Windows is a first-class target and does not have it.
    static const unsigned long long salt = std::random_device{}();
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path()
            / ("uta-bake-test-" + std::to_string(salt) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(path_);
}

TempDir::~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

std::vector<std::byte> asByteVector(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::byte> out(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) out[i] = static_cast<std::byte>(bytes[i]);
    return out;
}

} // namespace uta::test::bake
