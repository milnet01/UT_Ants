// ut-ants: the client -- UTA-0016.
//
// Checks the install by running ut-bake (docs/design.md rule 16), opens one
// baked map, and flies a camera through it. Everything that needs no window is
// in Cli.cpp and FlyCamera.cpp, which the unit tests compile too. This file is
// the SDL half, and it is run by hand: no CI leg has a display
// (docs/specs/UTA-0014-vulkan-draw-path.md SS 4.12).

#include "Cli.h"
#include "FlyCamera.h"

#include "core/FileSystem.h"
#include "ubundle/Bundle.h"
#include "urender/Renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cctype>
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

int run(SDL_Window* const window, const uta::ubundle::Bundle& bundle, const Options& options) {
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
    SDL_SetWindowRelativeMouseMode(window, true);

    std::uint64_t drawn = 0;
    Uint64 last = SDL_GetTicksNS();
    for (bool running = true; running;) {
        FlyInput input;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
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
        camera.update(input, static_cast<double>(now - last) / 1e9);
        last = now;

        if (const auto result = renderer.draw(bundle, camera.camera()); !result) {
            std::cerr << "ut-ants: a frame did not draw: " << result.error().message() << "\n";
            return EXIT_FAILED;
        }
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
                  << " unshadowed lights\n";
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
    *bytes = {}; // the decoded bundle is all that is drawn from
    if (bundle->header.kind != uta::ubundle::BundleKind::Map) {
        std::cerr << "ut-ants: " << options->bundle.string() << " is not a map\n";
        return EXIT_FAILED;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "ut-ants: SDL did not start: " << SDL_GetError() << "\n";
        return EXIT_FAILED;
    }
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
    const int status = run(window, *bundle, *options);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
