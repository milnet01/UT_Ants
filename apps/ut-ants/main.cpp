// ut-ants: the client -- UTA-0016.
//
// Checks the install by running ut-bake (docs/design.md rule 16), opens one
// baked map, and flies a camera through it -- or, given no map, opens the map
// launcher (UTA-0170, Launcher.cpp). Everything that needs no window is in
// the files apps/ut-ants/CMakeLists.txt compiles into the unit tests too. This
// file and Launcher.cpp are the SDL half, run by hand: no CI leg has a
// display (docs/specs/UTA-0014-vulkan-draw-path.md SS 4.12).

#include "BuildCommit.h" // generated -- UTA-0200
#include "Capture.h"
#include "Cli.h"
#include "FlyCamera.h"
#include "FrameRate.h"
#include "Launcher.h"
#include "MapList.h"
#include "StandIns.h"
#include "Walker.h"

#include "core/Ascii.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "ubundle/Bundle.h"
#include "ubundle/TileAnswers.h"
#include "ugame/WeaponSet.h"
#include "uinput/Input.h"
#include "urender/Renderer.h"
#include "uworld/BodyCollision.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace uta::client;

/// Whether ut-bake, beside this program, says `install` is usable. ut-bake
/// writes its reasons on standard error, which it shares with this process, so
/// a refusal explains itself. Its standard output, one JSON object, is read and
/// dropped: the exit code decides, as docs/specs/UTA-0011-map-baker.md SS 4.8
/// lets a caller.
bool installIsUsable(const std::filesystem::path& install) {
    const std::string bakerText = besideThisProgram("ut-bake");
    const std::string installText = uta::fs::utf8(install);
    const char* const argv[] = {bakerText.c_str(), "--check", installText.c_str(), nullptr};

    SDL_Process* const process = SDL_CreateProcess(argv, true);
    if (process == nullptr) {
        std::cerr << "ut-ants: could not run " << bakerText << ": " << SDL_GetError() << "\n";
        return false;
    }
    int exitCode = -1;
    void* const output = SDL_ReadProcess(process, nullptr, &exitCode);
    const bool answered = output != nullptr;
    SDL_free(output);
    SDL_DestroyProcess(process);

    if (!answered) {
        std::cerr << "ut-ants: ut-bake gave no answer: " << SDL_GetError() << "\n";
        return false;
    }
    if (exitCode == 0) return true;
    if (exitCode == 1) {
        std::cerr << "ut-ants: " << uta::sanitised(installText) << " is not a usable Unreal Tournament install\n";
    } else {
        std::cerr << "ut-ants: ut-bake could not check the install (exit code " << exitCode << ")\n";
    }
    return false;
}

/// UTA-0357: the degrees one press of [ or ] changes the field of view by.
constexpr double FOV_STEP = 5;

struct Start {
    FlyCamera camera;
    std::string from = "the origin"; ///< for a --frames run's report
};

/// A PlayerStart: where the map puts it, the way it faces, and its name.
struct PlayerStart {
    std::array<float, 3> location{};
    std::array<std::int32_t, 3> rotation{};
    std::string path;
};

/// Every PlayerStart of the map, in its order.
std::vector<PlayerStart> playerStarts(const uta::ubundle::Bundle& bundle) {
    std::vector<PlayerStart> starts;
    if (!bundle.placements.has_value()) return starts;
    const uta::ubundle::Placements& placements = *bundle.placements;
    for (const uta::ubundle::ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size()) continue;
        const uta::ubundle::ActorClass& actorClass = placements.classes[actor.classIndex];
        if (!uta::ubundle::isOrDescendsFrom(actorClass, "engine.playerstart")) continue;

        std::array<float, 3> location{};
        std::array<std::int32_t, 3> rotation{};
        for (const uta::ubundle::PropertyRecord& property : actor.properties) {
            if (property.arrayIndex != 0) continue;
            const std::string name = uta::asciiFolded(property.name); // as spelled in the map
            if (const auto* vector = std::get_if<std::array<float, 3>>(&property.value); vector && name == "location")
                location = *vector;
            if (const auto* rotator = std::get_if<std::array<std::int32_t, 3>>(&property.value);
                rotator && name == "rotation")
                rotation = *rotator;
        }
        starts.push_back({location, rotation, actor.path});
    }
    return starts;
}

/// The first PlayerStart's place and facing, or the origin when the map has none.
Start startingCamera(const uta::ubundle::Bundle& bundle) {
    const std::vector<PlayerStart> starts = playerStarts(bundle);
    if (starts.empty()) return {};
    return {FlyCamera(starts[0].location, starts[0].rotation[0], starts[0].rotation[1]), starts[0].path};
}

// UTA-0018: uinput names keys and buttons by SDL's numbers without including
// SDL; these hold it to that.
static_assert(int(uta::uinput::Key::A) == SDL_SCANCODE_A && int(uta::uinput::Key::D) == SDL_SCANCODE_D &&
              int(uta::uinput::Key::F) == SDL_SCANCODE_F && int(uta::uinput::Key::G) == SDL_SCANCODE_G &&
              int(uta::uinput::Key::H) == SDL_SCANCODE_H && int(uta::uinput::Key::P) == SDL_SCANCODE_P &&
              int(uta::uinput::Key::S) == SDL_SCANCODE_S && int(uta::uinput::Key::W) == SDL_SCANCODE_W &&
              int(uta::uinput::Key::T) == SDL_SCANCODE_T && int(uta::uinput::Key::N1) == SDL_SCANCODE_1 &&
              int(uta::uinput::Key::N2) == SDL_SCANCODE_2 && int(uta::uinput::Key::N3) == SDL_SCANCODE_3 &&
              int(uta::uinput::Key::N4) == SDL_SCANCODE_4 && int(uta::uinput::Key::N5) == SDL_SCANCODE_5 &&
              int(uta::uinput::Key::N6) == SDL_SCANCODE_6 && int(uta::uinput::Key::N7) == SDL_SCANCODE_7 &&
              int(uta::uinput::Key::N8) == SDL_SCANCODE_8 && int(uta::uinput::Key::N9) == SDL_SCANCODE_9 &&
              int(uta::uinput::Key::N0) == SDL_SCANCODE_0 &&
              int(uta::uinput::Key::Escape) == SDL_SCANCODE_ESCAPE &&
              int(uta::uinput::Key::Space) == SDL_SCANCODE_SPACE &&
              int(uta::uinput::Key::LeftBracket) == SDL_SCANCODE_LEFTBRACKET &&
              int(uta::uinput::Key::RightBracket) == SDL_SCANCODE_RIGHTBRACKET &&
              int(uta::uinput::Key::F12) == SDL_SCANCODE_F12 &&
              int(uta::uinput::Key::LeftCtrl) == SDL_SCANCODE_LCTRL &&
              int(uta::uinput::Key::LeftShift) == SDL_SCANCODE_LSHIFT &&
              int(uta::uinput::Key::RightCtrl) == SDL_SCANCODE_RCTRL &&
              int(uta::uinput::Key::RightShift) == SDL_SCANCODE_RSHIFT);
static_assert(int(uta::uinput::MouseButton::Left) == SDL_BUTTON_LEFT &&
              int(uta::uinput::MouseButton::Middle) == SDL_BUTTON_MIDDLE &&
              int(uta::uinput::MouseButton::Right) == SDL_BUTTON_RIGHT);
static_assert(int(uta::uinput::PadButton::South) == SDL_GAMEPAD_BUTTON_SOUTH &&
              int(uta::uinput::PadButton::East) == SDL_GAMEPAD_BUTTON_EAST &&
              int(uta::uinput::PadButton::West) == SDL_GAMEPAD_BUTTON_WEST &&
              int(uta::uinput::PadButton::North) == SDL_GAMEPAD_BUTTON_NORTH &&
              int(uta::uinput::PadButton::Back) == SDL_GAMEPAD_BUTTON_BACK &&
              int(uta::uinput::PadButton::Guide) == SDL_GAMEPAD_BUTTON_GUIDE &&
              int(uta::uinput::PadButton::Start) == SDL_GAMEPAD_BUTTON_START &&
              int(uta::uinput::PadButton::LeftStick) == SDL_GAMEPAD_BUTTON_LEFT_STICK &&
              int(uta::uinput::PadButton::RightStick) == SDL_GAMEPAD_BUTTON_RIGHT_STICK &&
              int(uta::uinput::PadButton::LeftShoulder) == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER &&
              int(uta::uinput::PadButton::RightShoulder) == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER &&
              int(uta::uinput::PadButton::DpadUp) == SDL_GAMEPAD_BUTTON_DPAD_UP &&
              int(uta::uinput::PadButton::DpadDown) == SDL_GAMEPAD_BUTTON_DPAD_DOWN &&
              int(uta::uinput::PadButton::DpadLeft) == SDL_GAMEPAD_BUTTON_DPAD_LEFT &&
              int(uta::uinput::PadButton::DpadRight) == SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

/// UTA-0167: the gamepads flying the camera, closed when the run ends.
class Gamepads {
public:
    Gamepads() {
        int count = 0;
        if (SDL_JoystickID* const ids = SDL_GetGamepads(&count)) {
            for (int i = 0; i < count; ++i) open(ids[i]);
            SDL_free(ids);
        }
    }
    Gamepads(const Gamepads&) = delete;
    Gamepads& operator=(const Gamepads&) = delete;
    ~Gamepads() {
        for (SDL_Gamepad* const pad : pads_) SDL_CloseGamepad(pad);
    }

    /// SDL may also announce a gamepad that was plugged in at start.
    void open(SDL_JoystickID id) {
        if (SDL_GetGamepadFromID(id) != nullptr) return;
        if (SDL_Gamepad* const pad = SDL_OpenGamepad(id)) {
            pads_.push_back(pad);
        } else {
            std::cerr << "ut-ants: a gamepad did not open: " << SDL_GetError() << "\n";
        }
    }
    void close(SDL_JoystickID id) {
        if (SDL_Gamepad* const pad = SDL_GetGamepadFromID(id)) {
            std::erase(pads_, pad);
            SDL_CloseGamepad(pad);
        }
    }

    /// Every open gamepad's sticks, triggers and buttons.
    [[nodiscard]] std::vector<uta::uinput::PadState> read() const {
        std::vector<uta::uinput::PadState> states;
        for (SDL_Gamepad* const pad : pads_) {
            const auto axis = [pad](SDL_GamepadAxis which) {
                return static_cast<float>(SDL_GetGamepadAxis(pad, which)) / 32767.0f;
            };
            uta::uinput::PadState& state = states.emplace_back(uta::uinput::PadState{
                .leftX = axis(SDL_GAMEPAD_AXIS_LEFTX),
                .leftY = axis(SDL_GAMEPAD_AXIS_LEFTY),
                .rightX = axis(SDL_GAMEPAD_AXIS_RIGHTX),
                .rightY = axis(SDL_GAMEPAD_AXIS_RIGHTY),
                .leftTrigger = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER),
                .rightTrigger = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
            for (std::size_t button = 0; button < uta::uinput::PAD_BUTTON_COUNT; ++button)
                state.held[button] = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(button));
        }
        return states;
    }

    [[nodiscard]] std::size_t count() const noexcept { return pads_.size(); }

private:
    std::vector<SDL_Gamepad*> pads_;
};

/// What a viewer run keeps between frames: the renderer, the camera, and
/// what the keys change. run() builds one and draws it each frame; F12's
/// capture and each event's action are its two methods.
struct Viewer {
    SDL_Window* const window;
    const uta::ubundle::Bundle& bundle;
    const Options& options;
    const std::string& bundleHash;
    uta::urender::Config config;
    uta::urender::Renderer renderer;
    Start start;
    Gamepads gamepads;
    uta::uinput::Input input; // UTA-0018: every device to one action set
    bool flashlight = false; // UTA-0015 SS 4.5: F toggles it
    bool showFrameRate = true; // UTA-0452: H toggles it
    FrameRate frameRate;

    // UTA-0191. The view the frame ON SCREEN was drawn from, which is not
    // always the camera as it stands: [ or ] earlier in the same batch of
    // events has already changed its field of view. Capturing this instead is
    // what makes the second image the same view as the first, and P writes it
    // for the same reason (UTA-0408).
    std::optional<uta::urender::Camera> shownView;
    decltype(launcherPaths()) paths = launcherPaths();
    // The launcher names the notes file after the map it is opening, so its
    // stem is the map's name (Cli.h). A run given no notes has only the
    // bundle's own name, which is a content hash.
    std::string mapName =
        !options.notes.empty() ? options.notes.stem().string() : options.bundle.stem().string();

    // UTA-0017: walking, toggled with G; the level's bodies are built on the
    // first walk.
    std::optional<uta::uworld::BodyWorld> bodies;
    std::optional<Walker> walker;
    // UTA-0019 SS 4.8: the walker carries the stock weapons, read on the first
    // walk; its shots, explosions and dummies are drawn as stand-in shapes.
    std::optional<uta::ugame::WeaponSet> weapons;
    StandIns standIns;
    std::string overlayShown; ///< the overlay's text as last set

    /// The frame-rate counter, and under it while walking the weapon in hand,
    /// its ammo and the player's health; set only when it changes.
    void showOverlay();

    /// UTA-0191: the shown frame, and the same view redrawn linear, written
    /// to a capture folder.
    void writeCaptureFolder();

    /// UTA-0017: from flying to walking at the camera's place, or back to
    /// flying at the walker's eye.
    void toggleWalking();

    enum class After { Next, Quit, Failed };
    /// One event: a device's goes to `input`, a window's is handled here.
    After handle(const SDL_Event& event);
    /// What the frame's once-a-press actions ask for.
    void act(const uta::uinput::Frame& frame);
};

void Viewer::writeCaptureFolder() {
    if (!shownView) {
        std::cerr << "ut-ants: nothing to capture yet -- no frame has been drawn\n";
        return;
    }
    if (!paths) {
        std::cerr << "ut-ants: no capture folder: " << paths.error().message() << "\n";
        return;
    }
    // The frame already on screen. readback copies the colour target, which
    // the presenting path blits from, so this is what the player is looking
    // at rather than a redraw of it.
    const auto shown = renderer.readback();
    if (!shown) {
        std::cerr << "ut-ants: the frame did not read back: " << shown.error().message() << "\n";
        return;
    }
    const uta::urender::FrameStats stats = renderer.lastFrameStats();

    // The same view again with the output stage off, so it can be measured.
    // The light time is pinned to the frame above, or a pulsing light is at
    // a different phase in the two images and they disagree for a reason
    // that has nothing to do with what is being compared. The scale too:
    // dynamic resolution may have moved it since the shown frame, and the
    // redraw must not steer it (UTA-0372).
    //
    // This draw PRESENTS, so the screen flashes once -- said in the usage
    // text rather than hidden, since a deliberate keypress that flashes
    // reads as feedback where an unexplained one reads as a fault. It also
    // counts as a frame to the renderer, so the next real frame's motion
    // vectors are measured against this one rather than against the frame
    // before the press. Harmless today: UTA-0075's jitter is off until a
    // pass consumes it, and nothing reads the velocity target yet.
    renderer.pinLightSeconds(renderer.lightSeconds());
    renderer.pinRenderScale(stats.renderScale);
    renderer.setLinearOutput(true);
    const auto redrew = renderer.draw(bundle, *shownView);
    const auto linear = redrew ? renderer.readback()
                               : uta::Result<std::vector<std::byte>>(
                                     std::unexpected(redrew.error()));
    renderer.setLinearOutput(false);
    renderer.unpinRenderScale();
    renderer.unpinLightSeconds();
    if (!linear) {
        std::cerr << "ut-ants: the linear view did not draw: " << linear.error().message() << "\n";
        return;
    }

    int windowWidth = 0, windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    const CaptureInfo info{
        .map = mapName,
        .bundleHash = bundleHash,
        .formatVersion = bundle.header.formatVersion,
        .bakerVersion = options.bakerVersion,
        .commit = BUILD_COMMIT,
        .tier = std::string(uta::urender::tierName(stats.tier)),
        .renderWidth = config.width,
        .renderHeight = config.height,
        .renderScale = stats.renderScale,
        .windowWidth = static_cast<std::uint32_t>(std::max(windowWidth, 0)),
        .windowHeight = static_cast<std::uint32_t>(std::max(windowHeight, 0)),
        .lightSeconds = renderer.lightSeconds(),
        .cameraLine = poseLine(*shownView, config.width, config.height),
    };
    const auto folder = writeCapture(paths->captures, info, *shown, *linear,
                                     std::chrono::system_clock::now());
    if (!folder) {
        std::cerr << "ut-ants: the capture did not save: " << uta::sanitised(folder.error().message()) << "\n";
        return;
    }
    std::cerr << "ut-ants: capture written to " << uta::sanitised(folder->string()) << "\n";
}

void Viewer::showOverlay() {
    std::string text = showFrameRate ? frameRate.text() : std::string();
    if (walker) {
        if (!text.empty()) text += '\n';
        text += hudText(walker->body(), walker->set(), walker->lastHit());
    }
    if (text == overlayShown) return;
    renderer.setOverlayText(text);
    overlayShown = std::move(text);
}

void Viewer::toggleWalking() {
    if (walker) {
        const uta::urender::Camera eye = walker->eye(start.camera.fov()).camera(config.width, config.height);
        const double fov = start.camera.fov();
        start.camera = FlyCamera(eye.location, eye.rotation[0], eye.rotation[1]);
        start.camera.setFov(fov);
        walker.reset();
        standIns = {};
        renderer.setShapes({});
        renderer.setCrosshair(false);
        uta::uinput::Settings settings = input.settings();
        settings.triggersFire = false;
        input.setSettings(settings);
        std::cerr << "ut-ants: flying\n";
        return;
    }
    if (!bodies) {
        if (!bundle.collision) {
            std::cerr << "ut-ants: this bundle has no collision to walk on\n";
            return;
        }
        // The level and its movers where the map places them; nothing moves them yet.
        static const std::vector<uta::ubundle::MoverShape> none;
        auto built = uta::uworld::BodyWorld::build(*bundle.collision, bundle.movers ? *bundle.movers : none);
        if (!built) {
            std::cerr << "ut-ants: cannot walk here: " << built.error().message() << "\n";
            return;
        }
        bodies = std::move(*built);
    }
    const uta::urender::Camera view = start.camera.camera(config.width, config.height);
    if (!weapons) {
        const std::filesystem::path file = uta::ugame::shippedDirectory() / "stock.weapons";
        auto loaded = uta::ugame::load(file);
        if (!loaded)
            std::cerr << "ut-ants: walking unarmed: " << uta::sanitised(file.string()) << ": "
                      << loaded.error().message() << "\n";
        weapons = loaded ? std::move(*loaded) : uta::ugame::WeaponSet{};
    }
    std::vector<Walker::Spawn> spawns;
    for (const PlayerStart& at : playerStarts(bundle))
        spawns.push_back({{at.location[0], at.location[1], at.location[2]}, static_cast<double>(at.rotation[1])});
    // The body hangs an eye's height under the camera, lifted clear where
    // that is in the floor, as it is at a PlayerStart.
    walker.emplace(clearOf(*bodies, {view.location[0], view.location[1], view.location[2] - Walker::EYE_HEIGHT}),
                   view.rotation[0], view.rotation[1],
                   bundle.placements ? uta::uworld::Teleporters::fromPlacements(*bundle.placements)
                                     : uta::uworld::Teleporters{},
                   *weapons, std::move(spawns));
    renderer.setCrosshair(true);
    // Walking, the triggers fire; the bottom face button and the shoulders
    // still jump and duck.
    uta::uinput::Settings settings = input.settings();
    settings.triggersFire = true;
    input.setSettings(settings);
    std::cerr << "ut-ants: walking\n";
}

Viewer::After Viewer::handle(const SDL_Event& event) {
    if (event.type == SDL_EVENT_QUIT) {
        return After::Quit;
    } else if (event.type == SDL_EVENT_KEY_DOWN) {
        input.keyDown(static_cast<uta::uinput::Key>(event.key.scancode), event.key.repeat);
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.button < uta::uinput::PAD_BUTTON_COUNT) {
        input.padButtonDown(static_cast<uta::uinput::PadButton>(event.gbutton.button));
    } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        gamepads.open(event.gdevice.which);
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        gamepads.close(event.gdevice.which);
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        input.mouseMoved(event.motion.xrel, event.motion.yrel);
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        input.mouseButton(static_cast<uta::uinput::MouseButton>(event.button.button), event.button.down);
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        input.mouseWheel(event.wheel.y); // away from the player is positive
    } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && event.window.data1 > 0 &&
               event.window.data2 > 0) {
        config.width = static_cast<std::uint32_t>(event.window.data1);
        config.height = static_cast<std::uint32_t>(event.window.data2);
        const auto resized = renderer.resize(config.width, config.height);
        // The targets are rebuilt and not yet drawn, and the shown view
        // was for the old size: a capture now would read both wrong
        // (UTA-0372). The next frame sets it again.
        shownView.reset();
        if (!resized) {
            std::cerr << "ut-ants: the renderer could not resize: " << resized.error().message() << "\n";
            return After::Failed;
        }
    }
    return After::Next;
}

void Viewer::act(const uta::uinput::Frame& frame) {
    using uta::uinput::Action;
    if (frame.wasPressed(Action::Flashlight)) flashlight = !flashlight;
    if (frame.wasPressed(Action::ToggleWalking)) toggleWalking(); // UTA-0017
    if (frame.wasPressed(Action::FrameRate)) showFrameRate = !showFrameRate; // UTA-0452; showOverlay draws it
    // UTA-0357: until the settings menu's slider (UTA-0297), the brackets
    // narrow and widen the view, held down to sweep it.
    if (frame.wasPressed(Action::FovNarrower) != frame.wasPressed(Action::FovWider)) {
        start.camera.setFov(start.camera.fov() + (frame.wasPressed(Action::FovWider) ? FOV_STEP : -FOV_STEP));
        std::cerr << "ut-ants: field of view " << start.camera.fov() << "\n";
    }
    if (frame.wasPressed(Action::SaveCamera)) {
        // UTA-0190: where the camera is, so a finding can be drawn again
        // with ut-shot. Before the first frame there is no shown view, so the
        // camera as it stands.
        const uta::urender::Camera view = shownView ? *shownView : start.camera.camera(config.width, config.height);
        const std::string line = "Camera (ut-shot): " + poseLine(view, config.width, config.height);
        std::cerr << line << "\n";
        if (!options.notes.empty()) {
            if (const auto added = appendNote(options.notes, line); !added)
                std::cerr << "ut-ants: the camera did not save: " << uta::sanitised(added.error().message())
                          << "\n";
        }
    }
    if (frame.wasPressed(Action::Capture)) writeCaptureFolder(); // UTA-0191
}

int run(SDL_Window* const window, const uta::ubundle::Bundle& bundle, const Options& options,
        const std::string& bundleHash) {
    uta::urender::Config config;
    Uint32 count = 0;
    const char* const* const names = SDL_Vulkan_GetInstanceExtensions(&count);
    for (Uint32 i = 0; i < count; ++i) config.instanceExtensions.emplace_back(names[i]);
    config.createSurface = [window](std::uint64_t instance) -> std::uint64_t {
        VkSurfaceKHR surface = nullptr;
        const auto vkInstance = reinterpret_cast<VkInstance>(static_cast<std::uintptr_t>(instance));
        if (!SDL_Vulkan_CreateSurface(window, vkInstance, nullptr, &surface)) {
            std::cerr << "ut-ants: SDL could not make a Vulkan surface: " << SDL_GetError() << "\n";
            return 0;
        }
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(surface));
    };
    SDL_SyncWindow(window); // a fullscreen request may not have landed yet
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    config.width = static_cast<std::uint32_t>(width);
    config.height = static_cast<std::uint32_t>(height);
    config.validation = options.validation;
    config.showMissingMaterials = options.showMissing;
    config.tier = options.tier;
    config.dynamicResolution = true; // UTA-0051 SS 4.5 always asks; SS 4.4 says what it does

    auto created = uta::urender::Renderer::create(config);
    if (!created) {
        std::cerr << "ut-ants: the renderer did not start: " << created.error().message() << "\n";
        return EXIT_FAILED;
    }
    SDL_SetWindowRelativeMouseMode(window, true);
    Viewer viewer{.window = window,
                  .bundle = bundle,
                  .options = options,
                  .bundleHash = bundleHash,
                  .config = std::move(config),
                  .renderer = std::move(*created),
                  .start = startingCamera(bundle)};
    FlyCamera& camera = viewer.start.camera;
    if (options.fov.has_value()) camera.setFov(*options.fov); // UTA-0357
    // UTA-0158: the camera stops at the level's walls, as UT99's spectator does.
    const uta::ubundle::CollisionTree* const level = bundle.collision ? &bundle.collision->level : nullptr;

    std::uint64_t drawn = 0;
    Uint64 last = SDL_GetTicksNS();
    for (bool running = true; running;) {
        SDL_Event event;
        // Every waiting event is handled, even after a quit.
        while (SDL_PollEvent(&event)) {
            const Viewer::After after = viewer.handle(event);
            if (after == Viewer::After::Failed) return EXIT_FAILED;
            if (after == Viewer::After::Quit) running = false;
        }
        const Uint64 now = SDL_GetTicksNS();
        const double seconds = static_cast<double>(now - last) / 1e9;
        last = now;
        int keyCount = 0;
        const bool* const keys = SDL_GetKeyboardState(&keyCount);
        const uta::uinput::Frame input = viewer.input.frame(
            std::span<const bool>(keys, static_cast<std::size_t>(keyCount)), viewer.gamepads.read(), seconds);
        if (!running || input.wasPressed(uta::uinput::Action::Quit)) break;
        viewer.act(input);
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
            SDL_Delay(10);
            last = SDL_GetTicksNS();
            continue;
        }

        // UTA-0452: start to start, so the count includes waiting for the display.
        viewer.frameRate.add(seconds);
        uta::urender::Camera view;
        if (viewer.walker) {
            const std::size_t dummies = viewer.walker->dummies();
            viewer.walker->update(input, seconds, *viewer.bodies);
            if (input.wasPressed(uta::uinput::Action::DropDummy))
                std::cerr << (viewer.walker->dummies() > dummies ? "ut-ants: a dummy stands ahead\n"
                                                                 : "ut-ants: no room for a dummy here\n");
            view = viewer.walker->view(camera.fov(), viewer.config.width, viewer.config.height);
            viewer.standIns.advance(seconds, viewer.walker->events());
            viewer.renderer.setShapes(viewer.standIns.shapes(viewer.walker->game(), viewer.walker->set()));
        } else {
            camera.update(input, seconds, level);
            view = camera.camera(viewer.config.width, viewer.config.height);
        }
        view.flashlight = viewer.flashlight;
        viewer.showOverlay();
        if (const auto result = viewer.renderer.draw(bundle, view); !result) {
            std::cerr << "ut-ants: a frame did not draw: " << result.error().message() << "\n";
            return EXIT_FAILED;
        }
        viewer.shownView = view; // UTA-0191: what a capture redraws
        ++drawn;
        if (options.frames.has_value() && drawn >= *options.frames) break;
    }

    if (options.frames.has_value()) {
        const uta::urender::FrameStats stats = viewer.renderer.lastFrameStats();
        std::cout << "ut-ants: started at " << viewer.start.from << "; tier " << uta::urender::tierName(stats.tier)
                  << "; drew " << drawn << " of " << *options.frames << " frames at " << viewer.config.width << "x"
                  << viewer.config.height << " pixels; the last was drawn at scale "
                  << stats.renderScale << " in " << stats.frameMilliseconds << " ms and had "
                  << stats.overflowedClusters << " overflowed clusters and " << stats.unshadowedLights
                  << " unshadowed lights; " << viewer.gamepads.count() << " gamepads open\n";
        if (drawn < *options.frames) return EXIT_FAILED;
    }
    return EXIT_OK;
} // the Renderer, and the surface it owns, go before the window

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    const auto options = parseArguments(args, std::cerr);
    if (!options) {
        usage(std::cerr);
        return EXIT_USAGE;
    }
    if (options->help) {
        usage(std::cerr);
        return EXIT_OK;
    }

    if (!installIsUsable(options->install)) return EXIT_FAILED;
    if (options->bundle.empty()) return runLauncher(*options);

    auto bytes = uta::fs::readFile(options->bundle);
    if (!bytes) {
        std::cerr << "ut-ants: " << bytes.error().message() << "\n";
        return EXIT_FAILED;
    }
    auto bundle = uta::ubundle::read(*bytes);
    if (!bundle) {
        std::cerr << "ut-ants: " << uta::sanitised(options->bundle.string()) << " did not read: " << bundle.error().message()
                  << "\n";
        return EXIT_FAILED;
    }
    // UTA-0277 SS 4.5: the player's answers about which pictures may move.
    for (const std::string& warning : uta::ubundle::applyTileAnswersFile(*bundle))
        std::cerr << "ut-ants: " << warning << "\n";
    // UTA-0256 SS 4.4: the recipe's added lamps, or the map as it was made.
    uta::ubundle::applyAddedLamps(*bundle, options->addedLamps);
    uta::ubundle::applySun(*bundle); // UTA-0338 SS 4.2
    uta::ubundle::applyProps(*bundle); // UTA-0341 SS 4.6
    // UTA-0191: the bundle's identity, taken before the bytes go. A capture
    // records it so a frame can be tied to the exact bundle that drew it.
    const std::string bundleHash = uta::client::bundleHashHex(*bytes);
    *bytes = {}; // the decoded bundle is all that is drawn from
    if (bundle->header.kind != uta::ubundle::BundleKind::Map) {
        std::cerr << "ut-ants: " << uta::sanitised(options->bundle.string()) << " is not a map\n";
        return EXIT_FAILED;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "ut-ants: SDL did not start: " << SDL_GetError() << "\n";
        return EXIT_FAILED;
    }
    // UTA-0167: without gamepads the keyboard and mouse still fly the camera.
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        std::cerr << "ut-ants: gamepads are off: " << SDL_GetError() << "\n";
    // UTA-0153. A fullscreen window with no display mode set is borderless at
    // the desktop's resolution (SDL_SetWindowFullscreenMode's NULL case), and
    // high pixel density keeps a scaled desktop's real pixels.
    int width = 1280, height = 720;
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (!options->windowed) {
        flags |= SDL_WINDOW_FULLSCREEN;
        if (const SDL_DisplayMode* const desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())) {
            width = desktop->w;
            height = desktop->h;
        }
    }
    SDL_Window* const window = SDL_CreateWindow("UT_Ants", width, height, flags);
    if (window == nullptr) {
        std::cerr << "ut-ants: SDL could not open a window: " << SDL_GetError() << "\n";
        SDL_Quit();
        return EXIT_FAILED;
    }
    const int status = run(window, *bundle, *options, bundleHash);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
