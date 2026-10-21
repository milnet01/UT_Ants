// UTA-0338 INV-8, through the draw path: the sun on a level surface through
// its SMSK pair, on movers through the probes' view of it, and its disc in
// the sky.
//
// docs/specs/UTA-0338-baked-sun.md SS 4.5. The sun reaches the renderer as
// the game hands it over, by ubundle::applySun once the bundle is read.
//
// Also UTA-0387 INV-3 and INV-4: the outdoor haze only as far as the probes
// see the sky (docs/specs/UTA-0387-sky-seen-haze.md SS 4.3).

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;
constexpr Rgba GREY{101, 101, 101, 255};
constexpr std::uint16_t CHART_SIZE = 12;
/// Where a square at y = -40 and at y = 40 lands at a depth of 100, as
/// RenderLampsTest's HOLDER_COLUMN puts y = 50.
constexpr std::uint32_t LEFT_COLUMN = 67;
constexpr std::uint32_t RIGHT_COLUMN = 93;

Config linearFrame() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    config.hazeScale = 0;
    config.tier = uta::urender::Tier::Low;
    return config;
}

/// A white sun standing low behind the camera, so it lights a square the
/// camera faces: yaw half a turn (toward -X), pitch 4096.
uta::ubundle::Sun behindTheCamera() {
    return uta::ubundle::Sun{.yaw = 32768, .pitch = 4096, .hue = 0, .saturation = 255, .brightness = 255};
}

uta::ubundle::MaskPair allLit(std::uint32_t light) {
    uta::ubundle::MaskPair pair;
    pair.light = light;
    pair.x = uta::ubundle::MASK_ALL_LIT;
    pair.y = uta::ubundle::MASK_ALL_LIT;
    return pair;
}

/// A grey level square a hundred units ahead, lit by a dim light of its own,
/// its chart holding an all-lit pair for each of `lights` light indices.
uta::ubundle::Bundle maskedSquare(std::uint32_t lights) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, "grey", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "grey", GREY);
    bundle.lights = std::vector{steadyLight({60, -20, 0}, 32, 64)};
    uta::ubundle::ShadowMask mask;
    mask.texelSize = 8;
    mask.width = CHART_SIZE;
    mask.height = CHART_SIZE;
    uta::ubundle::MaskChart chart;
    chart.pairCount = lights;
    chart.width = CHART_SIZE;
    chart.height = CHART_SIZE;
    mask.charts.push_back(chart);
    for (std::uint32_t light = 0; light < lights; ++light) mask.pairs.push_back(allLit(light));
    mask.texels.assign(std::size_t{CHART_SIZE} * CHART_SIZE, 255);
    const std::size_t vertices = bundle.geometry->vertices.size();
    mask.vertexChart.assign(vertices, 0);
    mask.vertexTexel.assign(vertices, {CHART_SIZE / 2.0f, CHART_SIZE / 2.0f});
    bundle.shadowMask = std::move(mask);
    return bundle;
}

/// Two still grey movers a hundred units ahead, at y = -40 and y = 40, over an
/// even field of probes. With `sun`, the probes on the left see it and those on
/// the right do not -- as a wall on the right would leave them.
uta::ubundle::Bundle twoMovers(bool sun) {
    uta::ubundle::Bundle bundle = bundleOf(uta::ubundle::Geometry{});
    addSolidMaterial(bundle, "grey", GREY);
    bundle.movers.emplace();
    for (const float y : {-40.0f, 40.0f}) {
        uta::ubundle::Geometry shape;
        addSquare(shape, 100, y, 0, 15, "grey", 0);
        bundle.movers->push_back(uta::ubundle::MoverShape{static_cast<std::uint32_t>(bundle.movers->size() + 1), {}, {},
                                                          {1, 1, 1}, std::move(shape)});
    }
    addEvenProbes(bundle, {0, -128, -64}, {128, 128, 64}, 0.02f);
    if (sun) {
        bundle.sun = behindTheCamera();
        for (const uta::ubundle::LightProbe& probe : bundle.lightProbes->probes)
            bundle.lightProbes->sunSeen.push_back(probe.cell[1] < 0 ? 1.0f : 0.0f);
    }
    return bundle;
}

/// A sky window filling the view, the sky zone's view at x = 10000 seeing a
/// dark square -- UTA-0281's setup. With `sun`, a sun stands just above the
/// view's centre.
uta::ubundle::Bundle skyWindow(bool sun) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 200, "window", PF_FAKE_BACKDROP);
    addSquare(geometry, 10100, 0, 0, 3000, "cloud", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "window", GREY);
    addSolidMaterial(bundle, "cloud", Rgba{21, 21, 21, 255});
    addEvenProbes(bundle, {10000, -128, -64}, {10128, 128, 64}, 0.05f);
    uta::ubundle::Placements placements;
    placements.classes = {{.path = "engine.skyzoneinfo", .ancestry = {"engine.zoneinfo", "engine.info"}}};
    placements.actors = {{.exportIndex = 1,
                          .classIndex = 0,
                          .properties = {{.name = "Location",
                                          .kind = uta::ubundle::ValueKind::Vector,
                                          .value = std::array<float, 3>{10000, 0, 0}}}}};
    bundle.placements = std::move(placements);
    if (sun) {
        bundle.sun = uta::ubundle::Sun{.yaw = 0, .pitch = 100, .hue = 0, .saturation = 255, .brightness = 255};
        bundle.lightProbes->sunSeen.assign(bundle.lightProbes->probes.size(), 0.0f);
    }
    return bundle;
}

std::vector<std::byte> frameOf(Renderer& renderer, uta::ubundle::Bundle bundle) {
    uta::ubundle::applyAddedLamps(bundle, true);
    uta::ubundle::applySun(bundle);
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

int sum(const Rgba& pixel) { return int(pixel.r) + int(pixel.g) + int(pixel.b); }

} // namespace

TEST_CASE("UTA-0338 INV-8: the sun lights a level surface through its pair", "[device]") {
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle sunny = maskedSquare(2); // LITE's light, then the sun
    sunny.sun = behindTheCamera();
    const std::vector<std::byte> lit = frameOf(renderer, sunny);
    const std::vector<std::byte> plain = frameOf(renderer, maskedSquare(1));
    const Rgba withSun = pixelAt(lit, WIDTH, WIDTH / 2, HEIGHT / 2);
    const Rgba without = pixelAt(plain, WIDTH, WIDTH / 2, HEIGHT / 2);
    CAPTURE(withSun, without);
    CHECK(sum(withSun) > sum(without) + 30);

    // A sun with no pair on the chart lights it not at all: the mask decides.
    uta::ubundle::Bundle unpaired = maskedSquare(1);
    unpaired.sun = behindTheCamera();
    CHECK(pixelAt(frameOf(renderer, unpaired), WIDTH, WIDTH / 2, HEIGHT / 2) == without);
}

TEST_CASE("UTA-0338 INV-8: a still mover takes the sun its probes see", "[device]") {
    Renderer renderer = requireRenderer(linearFrame());
    const std::vector<std::byte> lit = frameOf(renderer, twoMovers(true));
    const std::vector<std::byte> plain = frameOf(renderer, twoMovers(false));
    const Rgba left = pixelAt(lit, WIDTH, LEFT_COLUMN, HEIGHT / 2);
    const Rgba right = pixelAt(lit, WIDTH, RIGHT_COLUMN, HEIGHT / 2);
    const Rgba leftPlain = pixelAt(plain, WIDTH, LEFT_COLUMN, HEIGHT / 2);
    const Rgba rightPlain = pixelAt(plain, WIDTH, RIGHT_COLUMN, HEIGHT / 2);
    CAPTURE(left, right, leftPlain, rightPlain);
    REQUIRE(sum(leftPlain) > 0); // the movers are drawn where the columns look
    CHECK(sum(left) > sum(leftPlain) + 30);
    CHECK(right == rightPlain);
}

TEST_CASE("UTA-0338 INV-8: the sky shows the sun's disc where it stands", "[device]") {
    Renderer renderer = requireRenderer(linearFrame());
    const std::vector<std::byte> sunny = frameOf(renderer, skyWindow(true));
    const std::vector<std::byte> plain = frameOf(renderer, skyWindow(false));
    const Rgba disc = pixelAt(sunny, WIDTH, WIDTH / 2, HEIGHT / 2);
    const Rgba skyThere = pixelAt(plain, WIDTH, WIDTH / 2, HEIGHT / 2);
    CAPTURE(disc, skyThere);
    CHECK(sum(disc) > sum(skyThere) + 200);
    // Far from the sun the sky is the map's own; the captured cube took no disc.
    const Rgba corner = pixelAt(sunny, WIDTH, 2, 2);
    const Rgba cornerPlain = pixelAt(plain, WIDTH, 2, 2);
    CAPTURE(corner, cornerPlain);
    CHECK(std::abs(sum(corner) - sum(cornerPlain)) <= 6);
}

/// UTA-0350 SS 4.4: a painted sky whose horizon is red and top blue, with no
/// clouds, so a pixel's colour says which it shows.
uta::ubundle::PaintedSky redToBlue() {
    return uta::ubundle::PaintedSky{.top = {0.05f, 0.05f, 0.8f}, .horizon = {0.6f, 0.05f, 0.05f}, .clouds = 0};
}

TEST_CASE("UTA-0350 INV-7: a backdrop draws the painted sky where there is no sky view", "[device]") {
    Renderer renderer = requireRenderer(linearFrame());
    // skyWindow's window with no sky view: its own grey picture, as before.
    uta::ubundle::Bundle bare = skyWindow(false);
    bare.placements.reset();
    const std::vector<std::byte> picture = frameOf(renderer, bare);
    uta::ubundle::Bundle painted = bare;
    painted.paintedSky = redToBlue();
    const std::vector<std::byte> sky = frameOf(renderer, painted);

    const Rgba centre = pixelAt(sky, WIDTH, WIDTH / 2, HEIGHT / 2);
    const Rgba top = pixelAt(sky, WIDTH, WIDTH / 2, 1);
    const Rgba grey = pixelAt(picture, WIDTH, WIDTH / 2, HEIGHT / 2);
    CAPTURE(centre, top, grey);
    CHECK(std::abs(int(grey.r) - int(grey.b)) <= 2); // the grey picture
    // Level with the eye the horizon shows; higher up the sky is bluer.
    CHECK(int(centre.r) > int(centre.b) + 60);
    CHECK(int(top.b) - int(top.r) > int(centre.b) - int(centre.r));

    // A level with a sky view draws that, whatever PSKY holds. Two draws are
    // compared within the disc case's 6, not byte for byte: GitHub's GCC leg
    // put a shade of 1 where the other draw had 0.
    uta::ubundle::Bundle viewed = skyWindow(false);
    viewed.paintedSky = redToBlue();
    const std::vector<std::byte> withPsky = frameOf(renderer, viewed);
    const std::vector<std::byte> without = frameOf(renderer, skyWindow(false));
    for (const auto& [x, y] : {std::pair{WIDTH / 2, HEIGHT / 2}, std::pair{2u, 2u}}) {
        const Rgba a = pixelAt(withPsky, WIDTH, x, y);
        const Rgba b = pixelAt(without, WIDTH, x, y);
        CAPTURE(x, y, a, b);
        CHECK(std::abs(sum(a) - sum(b)) <= 6);
        CHECK(int(a.r) <= int(a.b) + 6); // not the painted horizon's red
    }
}

TEST_CASE("UTA-0337: a painted sky's clouds drift with the light clock", "[device]") {
    // Half cover, so clouds and gaps both cross the top of the view. Two
    // minutes on, about a cloud's width has passed (Frame.cpp's wind).
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle bundle = skyWindow(false);
    bundle.placements.reset();
    bundle.paintedSky = uta::ubundle::PaintedSky{.top = {0.05f, 0.05f, 0.8f}, .horizon = {0.6f, 0.05f, 0.05f},
                                                 .clouds = 0.5f};
    const auto topRow = [&](double seconds) {
        renderer.pinLightSeconds(seconds);
        const std::vector<std::byte> pixels = frameOf(renderer, bundle);
        std::vector<int> row;
        for (std::uint32_t x = 0; x < WIDTH; ++x) row.push_back(sum(pixelAt(pixels, WIDTH, x, 1)));
        return row;
    };
    const std::vector<int> still = topRow(0);
    const std::vector<int> again = topRow(0);
    const std::vector<int> later = topRow(120);
    int moved = 0;
    for (std::uint32_t x = 0; x < WIDTH; ++x) {
        CHECK(std::abs(still[x] - again[x]) <= 6); // the same moment draws the same sky
        if (std::abs(still[x] - later[x]) > 30) ++moved;
    }
    CAPTURE(moved);
    CHECK(moved > int(WIDTH) / 8);
}

TEST_CASE("UTA-0350 INV-7: a sky view gained or lost is re-uploaded", "[device]") {
    // One bundle edited in place, so only shapeOf's sample of the sky view can
    // tell the renderer it changed. GitHub's GCC leg gave frameOf's by-value
    // bundles one address, and a level that had gained a sky view drew PSKY.
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle bundle = skyWindow(false);
    const auto placements = bundle.placements;
    bundle.placements.reset();
    bundle.paintedSky = redToBlue();
    uta::ubundle::applyAddedLamps(bundle, true);
    uta::ubundle::applySun(bundle);
    const auto centre = [&renderer, &bundle] {
        requireOk(renderer.draw(bundle, Camera{}));
        auto pixels = renderer.readback();
        if (!pixels.has_value()) FAIL(pixels.error().message());
        return pixelAt(*pixels, WIDTH, WIDTH / 2, HEIGHT / 2);
    };
    const Rgba painted = centre();
    bundle.placements = placements;
    const Rgba viewed = centre();
    bundle.placements.reset();
    const Rgba paintedAgain = centre();
    CAPTURE(painted, viewed, paintedAgain);
    CHECK(int(painted.r) > int(painted.b) + 60);
    CHECK(int(viewed.r) <= int(viewed.b) + 6);
    CHECK(int(paintedAgain.r) > int(paintedAgain.b) + 60);
}

TEST_CASE("UTA-0338 INV-8: the sun is in no cluster", "[device]") {
    // The sun's LITE record stands at the origin with radius byte 0, so a
    // cluster test that took it would light a mover within 25 units of there
    // past what its probes see -- here, nothing.
    const auto near = [](bool sun) {
        uta::ubundle::Bundle bundle = bundleOf(uta::ubundle::Geometry{});
        addSolidMaterial(bundle, "grey", GREY);
        uta::ubundle::Geometry shape;
        addSquare(shape, 12, 0, 0, 4, "grey", 0);
        bundle.movers = std::vector{uta::ubundle::MoverShape{1, {}, {}, {1, 1, 1}, std::move(shape)}};
        addEvenProbes(bundle, {-128, -128, -64}, {128, 128, 64}, 0.02f);
        if (sun) {
            bundle.sun = behindTheCamera();
            bundle.lightProbes->sunSeen.assign(bundle.lightProbes->probes.size(), 0.0f);
        }
        return bundle;
    };
    Renderer renderer = requireRenderer(linearFrame());
    const Rgba withSun = pixelAt(frameOf(renderer, near(true)), WIDTH, WIDTH / 2, HEIGHT / 2);
    const Rgba without = pixelAt(frameOf(renderer, near(false)), WIDTH, WIDTH / 2, HEIGHT / 2);
    CAPTURE(withSun, without);
    REQUIRE(sum(without) > 0);
    CHECK(withSun == without);
}

TEST_CASE("UTA-0338 INV-8: the probes' view of the sun is re-uploaded when it changes", "[device]") {
    // One bundle edited in place, so only shapeOf's sample of sunSeen can tell
    // the renderer the probes changed: a new bundle is a new address.
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle bundle = twoMovers(true);
    uta::ubundle::applyAddedLamps(bundle, true);
    uta::ubundle::applySun(bundle);
    const auto leftOf = [&renderer, &bundle] {
        requireOk(renderer.draw(bundle, Camera{}));
        auto pixels = renderer.readback();
        if (!pixels.has_value()) FAIL(pixels.error().message());
        return pixelAt(*pixels, WIDTH, LEFT_COLUMN, HEIGHT / 2);
    };
    const Rgba seen = leftOf();
    std::ranges::fill(bundle.lightProbes->sunSeen, 0.0f);
    const Rgba unseen = leftOf();
    const Rgba plain = pixelAt(frameOf(renderer, twoMovers(false)), WIDTH, LEFT_COLUMN, HEIGHT / 2);
    CAPTURE(seen, unseen, plain);
    CHECK(sum(seen) > sum(unseen) + 30);
    CHECK(unseen == plain);
}

namespace {

/// UTA-0387: a grey square 3000 units ahead, half 400, before a sky window at
/// 3500 that only paints redToBlue's sky. One zone holds both, so it sees the
/// sky, and its outdoorExtent of 1200 hazes the square nearly whole. Probes
/// stand round the square, each seeing `skySeen` of the sky; with `none`, they
/// stand at the eye instead, so no corner is near the square.
uta::ubundle::Bundle farSquare(float skySeen, bool none = false) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 3000, 0, 0, 400, "grey", 0);
    addSquare(geometry, 3500, 0, 0, 600, "window", PF_FAKE_BACKDROP);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "grey", GREY);
    addSolidMaterial(bundle, "window", GREY);
    bundle.zones = std::vector{uta::ubundle::Zone{}};
    bundle.paintedSky = redToBlue();
    if (none)
        addEvenProbes(bundle, {0, -128, -64}, {128, 128, 64}, 0.02f);
    else
        addEvenProbes(bundle, {2816, -128, -128}, {3072, 128, 128}, 0.02f);
    bundle.lightProbes->skySeen.assign(bundle.lightProbes->probes.size(), skySeen);
    return bundle;
}

} // namespace

TEST_CASE("UTA-0387 INV-3: the outdoor haze reaches only where the sky is seen", "[device]") {
    Renderer renderer = requireRenderer(linearFrame());
    const auto centre = [&renderer](uta::ubundle::Bundle bundle) {
        return pixelAt(frameOf(renderer, std::move(bundle)), WIDTH, WIDTH / 2, HEIGHT / 2);
    };
    const Rgba sealed = centre(farSquare(0.0f));
    const Rgba open = centre(farSquare(0.5f));
    const Rgba wide = centre(farSquare(1.0f));
    const Rgba unprobed = centre(farSquare(0.0f, true));
    CAPTURE(sealed, open, wide, unprobed);
    // No sky seen: the grey square, unhazed.
    CHECK(std::abs(int(sealed.r) - int(sealed.b)) <= 6);
    // Open ground: hazed nearly whole, toward the sky's low ring, which mixes
    // the red horizon with the blue top; more sky adds none.
    CHECK(sum(open) > sum(sealed) + 150);
    CHECK(std::abs(sum(open) - sum(wide)) <= 6);
    // No probe near: hazed as open ground.
    CHECK(std::abs(sum(unprobed) - sum(open)) <= 6);
}

TEST_CASE("UTA-0387 INV-4: the probes' view of the sky is re-uploaded when it changes", "[device]") {
    // One bundle edited in place, as the sunSeen case above.
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle bundle = farSquare(0.0f);
    uta::ubundle::applyAddedLamps(bundle, true);
    uta::ubundle::applySun(bundle);
    const auto centre = [&renderer, &bundle] {
        requireOk(renderer.draw(bundle, Camera{}));
        auto pixels = renderer.readback();
        if (!pixels.has_value()) FAIL(pixels.error().message());
        return pixelAt(*pixels, WIDTH, WIDTH / 2, HEIGHT / 2);
    };
    const Rgba sealed = centre();
    std::ranges::fill(bundle.lightProbes->skySeen, 0.5f);
    const Rgba open = centre();
    CAPTURE(sealed, open);
    CHECK(int(open.r) > int(sealed.r) + 60);
}
