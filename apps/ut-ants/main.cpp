// ut-ants: the client -- UTA-0016.
//
// Checks the install by running ut-bake (docs/design.md rule 16), opens one
// baked map, and flies a camera through it -- or, given no map, opens the map
// launcher (UTA-0170, Launcher.cpp). Everything that needs no window is
// in Cli.cpp and FlyCamera.cpp, which the unit tests compile too. This file is
// the SDL half, and it is run by hand: no CI leg has a display
// (docs/specs/UTA-0014-vulkan-draw-path.md SS 4.12).

#include "BuildCommit.h" // generated -- UTA-0200
#include "Capture.h"
#include "Cli.h"
#include "FlyCamera.h"
#include "Launcher.h"
#include "MapList.h"

#include "core/FileSystem.h"
#include "ubundle/Bundle.h"
#include "urender/Renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace uta::client;

std::string lowered(std::string_view text) {
    std::string folded(text);
    std::ranges::transform(folded, folded.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return folded;
}

/// Whether ut-bake, beside this program, says `install` is usable. ut-bake
/// writes its reasons on standard error, which it shares with this process, so
/// a refusal explains itself. Its standard output, one JSON object, is read and
/// dropped: the exit code decides, as docs/specs/UTA-0011-map-baker.md SS 4.8
/// lets a caller.
bool installIsUsable(const std::filesystem::path& install) {
    const char* const base = SDL_GetBasePath();
    std::filesystem::path baker = std::filesystem::path(base != nullptr ? base : "") / "ut-bake";
#ifdef _WIN32
    baker += ".exe";
#endif
    const std::string bakerText = baker.string();
    const std::string installText = install.string();
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
        std::cerr << "ut-ants: " << installText << " is not a usable Unreal Tournament install\n";
    } else {
        std::cerr << "ut-ants: ut-bake could not check the install (exit code " << exitCode << ")\n";
    }
    return false;
}

struct Start {
    FlyCamera camera;
    std::string from = "the origin"; ///< for a --frames run's report
};

/// The first PlayerStart's place and facing, or the origin when the map has none.
Start startingCamera(const uta::ubundle::Bundle& bundle) {
    if (!bundle.placements.has_value()) return {};
    const uta::ubundle::Placements& placements = *bundle.placements;
    for (const uta::ubundle::ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size()) continue;
        const uta::ubundle::ActorClass& actorClass = placements.classes[actor.classIndex];
        const std::string_view start = "engine.playerstart";
        if (actorClass.path != start && std::ranges::find(actorClass.ancestry, start) == actorClass.ancestry.end())
            continue;

        std::array<float, 3> location{};
        std::array<std::int32_t, 3> rotation{};
        for (const uta::ubundle::PropertyRecord& property : actor.properties) {
            if (property.arrayIndex != 0) continue;
            const std::string name = lowered(property.name); // as spelled in the map
            if (const auto* vector = std::get_if<std::array<float, 3>>(&property.value); vector && name == "location")
                location = *vector;
            if (const auto* rotator = std::get_if<std::array<std::int32_t, 3>>(&property.value);
                rotator && name == "rotation")
                rotation = *rotator;
        }
        return {FlyCamera(location, rotation[0], rotation[1]), actor.path};
    }
    return {};
}

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

    /// Every open gamepad's sticks and buttons, added to `input`.
    void addTo(FlyInput& input, double seconds) const {
        for (SDL_Gamepad* const pad : pads_) {
            const auto axis = [pad](SDL_GamepadAxis which) {
                return static_cast<float>(SDL_GetGamepadAxis(pad, which)) / 32767.0f;
            };
            const auto held = [pad](SDL_GamepadButton which) {
                return SDL_GetGamepadButton(pad, which) ? 1.0f : 0.0f;
            };
            addPad(input,
                   PadInput{.leftX = axis(SDL_GAMEPAD_AXIS_LEFTX),
                            .leftY = axis(SDL_GAMEPAD_AXIS_LEFTY),
                            .rightX = axis(SDL_GAMEPAD_AXIS_RIGHTX),
                            .rightY = axis(SDL_GAMEPAD_AXIS_RIGHTY),
                            .rise = std::max(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER),
                                             held(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)),
                            .sink = std::max(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER),
                                             held(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)),
                            .fast = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK)},
                   seconds);
        }
    }

    [[nodiscard]] std::size_t count() const noexcept { return pads_.size(); }

private:
    std::vector<SDL_Gamepad*> pads_;
};

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
    config.dynamicResolution = true; // UTA-0051 SS 4.5

    auto created = uta::urender::Renderer::create(config);
    if (!created) {
        std::cerr << "ut-ants: the renderer did not start: " << created.error().message() << "\n";
        return EXIT_FAILED;
    }
    uta::urender::Renderer renderer = std::move(*created);
    Start start = startingCamera(bundle);
    FlyCamera& camera = start.camera;
    // UTA-0158: the camera stops at the level's walls, as UT99's spectator does.
    const uta::ubundle::CollisionTree* const level = bundle.collision ? &bundle.collision->level : nullptr;
    SDL_SetWindowRelativeMouseMode(window, true);
    Gamepads gamepads;

    std::uint64_t drawn = 0;
    bool flashlight = false; // UTA-0015 SS 4.5: F toggles it

    // UTA-0191. The view the frame ON SCREEN was drawn from, which is not the
    // camera as it stands: a mouse motion earlier in the same batch of events
    // has already moved that. Capturing this instead is what makes the second
    // image the same view as the first.
    std::optional<uta::urender::Camera> shownView;
    const auto paths = launcherPaths();
    // The launcher names the notes file after the map it is opening, so its
    // stem is the map's name (Cli.h). A run given no notes has only the
    // bundle's own name, which is a content hash.
    const std::string mapName =
        !options.notes.empty() ? options.notes.stem().string() : options.bundle.stem().string();

    const auto writeCaptureFolder = [&] {
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
        // that has nothing to do with what is being compared.
        //
        // This draw PRESENTS, so the screen flashes once -- said in the usage
        // text rather than hidden, since a deliberate keypress that flashes
        // reads as feedback where an unexplained one reads as a fault. It also
        // counts as a frame to the renderer, so the next real frame's motion
        // vectors are measured against this one rather than against the frame
        // before the press. Harmless today: UTA-0075's jitter is off until a
        // pass consumes it, and nothing reads the velocity target yet.
        renderer.pinLightSeconds(renderer.lightSeconds());
        renderer.setLinearOutput(true);
        const auto redrew = renderer.draw(bundle, *shownView);
        const auto linear = redrew ? renderer.readback()
                                   : uta::Result<std::vector<std::byte>>(
                                         std::unexpected(redrew.error()));
        renderer.setLinearOutput(false);
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
            std::cerr << "ut-ants: the capture did not save: " << folder.error().message() << "\n";
            return;
        }
        std::cerr << "ut-ants: capture written to " << folder->string() << "\n";
    };

    Uint64 last = SDL_GetTicksNS();
    for (bool running = true; running;) {
        FlyInput input;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                       event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                running = false; // UTA-0183: Options on a PlayStation pad, Menu on an Xbox one
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F && !event.key.repeat) {
                flashlight = !flashlight;
            } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                       event.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                flashlight = !flashlight; // Triangle on a PlayStation pad, Y on an Xbox one
            } else if ((event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_P && !event.key.repeat) ||
                       (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                        event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
                // UTA-0190: where the camera is, so a finding can be drawn again
                // with ut-shot. Share or Create on a PlayStation pad, View on an Xbox one.
                const std::string line = "Camera (ut-shot): " + poseLine(camera.camera(), config.width, config.height);
                std::cerr << line << "\n";
                if (!options.notes.empty()) {
                    if (const auto added = appendNote(options.notes, line); !added)
                        std::cerr << "ut-ants: the camera did not save: " << added.error().message() << "\n";
                }
            } else if ((event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F12 &&
                        !event.key.repeat) ||
                       (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                        event.gbutton.button == SDL_GAMEPAD_BUTTON_WEST)) {
                // UTA-0191: everything needed to find and redraw this view.
                // Square on a PlayStation pad, X on an Xbox one -- the one face
                // button the viewer does not already use.
                writeCaptureFolder();
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                gamepads.open(event.gdevice.which);
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                gamepads.close(event.gdevice.which);
            } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                input.lookRight += event.motion.xrel;
                input.lookUp -= event.motion.yrel; // SDL's y grows downward
            } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && event.window.data1 > 0 &&
                       event.window.data2 > 0) {
                config.width = static_cast<std::uint32_t>(event.window.data1);
                config.height = static_cast<std::uint32_t>(event.window.data2);
                const auto resized = renderer.resize(config.width, config.height);
                if (!resized) {
                    std::cerr << "ut-ants: the renderer could not resize: " << resized.error().message() << "\n";
                    return EXIT_FAILED;
                }
            }
        }
        if (!running) break;
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
            SDL_Delay(10);
            last = SDL_GetTicksNS();
            continue;
        }

        const bool* const keys = SDL_GetKeyboardState(nullptr);
        const auto axis = [keys](SDL_Scancode positive, SDL_Scancode negative) {
            return static_cast<float>(keys[positive]) - static_cast<float>(keys[negative]);
        };
        input.forward = axis(SDL_SCANCODE_W, SDL_SCANCODE_S);
        input.right = axis(SDL_SCANCODE_D, SDL_SCANCODE_A);
        input.up = axis(SDL_SCANCODE_SPACE, SDL_SCANCODE_LCTRL);
        input.fast = keys[SDL_SCANCODE_LSHIFT];

        const Uint64 now = SDL_GetTicksNS();
        const double seconds = static_cast<double>(now - last) / 1e9;
        last = now;
        gamepads.addTo(input, seconds);
        camera.update(input, seconds, level);

        uta::urender::Camera view = camera.camera();
        view.flashlight = flashlight;
        if (const auto result = renderer.draw(bundle, view); !result) {
            std::cerr << "ut-ants: a frame did not draw: " << result.error().message() << "\n";
            return EXIT_FAILED;
        }
        shownView = view; // UTA-0191: what a capture redraws
        ++drawn;
        if (options.frames.has_value() && drawn >= *options.frames) break;
    }

    if (options.frames.has_value()) {
        const uta::urender::FrameStats stats = renderer.lastFrameStats();
        std::cout << "ut-ants: started at " << start.from << "; tier " << uta::urender::tierName(stats.tier)
                  << "; drew " << drawn << " of " << *options.frames << " frames at " << config.width << "x"
                  << config.height << " pixels; the last was drawn at scale "
                  << stats.renderScale << " in " << stats.frameMilliseconds << " ms and had "
                  << stats.overflowedClusters << " overflowed clusters and " << stats.unshadowedLights
                  << " unshadowed lights; " << gamepads.count() << " gamepads open\n";
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
    const auto bundle = uta::ubundle::read(*bytes);
    if (!bundle) {
        std::cerr << "ut-ants: " << options->bundle.string() << " did not read: " << bundle.error().message()
                  << "\n";
        return EXIT_FAILED;
    }
    // UTA-0191: the bundle's identity, taken before the bytes go. A capture
    // records it so a frame can be tied to the exact bundle that drew it.
    const std::string bundleHash = uta::client::bundleHashHex(*bytes);
    *bytes = {}; // the decoded bundle is all that is drawn from
    if (bundle->header.kind != uta::ubundle::BundleKind::Map) {
        std::cerr << "ut-ants: " << options->bundle.string() << " is not a map\n";
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
