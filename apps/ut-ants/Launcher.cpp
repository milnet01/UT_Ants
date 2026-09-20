// The map launcher -- UTA-0170.
//
// One window drawn with SDL's own renderer and its built-in text, since menus
// proper are uui's and later. A picked map is baked by ut-bake, which reuses a
// bake it already made, and flown by a second ut-ants: the viewer is the same
// program given the bundle, and a crash in it leaves the launcher standing to
// record it. Both run on a worker thread, so the window keeps answering while
// a first bake takes its time.

#include "Launcher.h"

#include "MapList.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace uta::client {
namespace {

constexpr float GLYPH = static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
constexpr float ROW = GLYPH + 3.0f; ///< a text row, in unscaled pixels

/// A program beside this one, as ut-ants finds ut-bake (docs/design.md rule 16).
std::string besideThisProgram(const char* name) {
    const char* const base = SDL_GetBasePath();
    std::filesystem::path path = std::filesystem::path(base != nullptr ? base : "") / name;
#ifdef _WIN32
    path += ".exe";
#endif
    return path.string();
}

/// `path` for a status line: the home directory as ~, so the part that says
/// where survives the line's width.
std::string shown(const std::filesystem::path& path) {
    std::string text = path.string();
    for (const char* const variable : {"HOME", "USERPROFILE"}) {
        const char* const home = SDL_getenv(variable);
        if (home == nullptr || *home == '\0') continue;
        const std::string_view prefix(home);
        if (text.starts_with(prefix)) return "~" + text.substr(prefix.size());
    }
    return text;
}

/// One child process run to its end on a worker thread.
class Child {
public:
    Child() = default;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    ~Child() { stop(); }

    /// Start `args`. With `readOutput`, standard output is collected; without,
    /// the child shares this process's.
    void start(std::vector<std::string> args, bool readOutput) {
        stop();
        done_ = false;
        output_.clear();
        exitCode_ = -1;
        worker_ = std::jthread([this, args = std::move(args), readOutput] {
            std::vector<const char*> argv;
            for (const std::string& arg : args) argv.push_back(arg.c_str());
            argv.push_back(nullptr);
            SDL_Process* const process = SDL_CreateProcess(argv.data(), readOutput);
            if (process == nullptr) {
                std::cerr << "ut-ants: could not run " << args.front() << ": " << SDL_GetError() << "\n";
                done_ = true;
                return;
            }
            {
                const std::scoped_lock lock(mutex_);
                process_ = process;
            }
            int exitCode = -1;
            std::string output;
            if (readOutput) {
                std::size_t size = 0;
                if (void* const bytes = SDL_ReadProcess(process, &size, &exitCode)) {
                    output.assign(static_cast<const char*>(bytes), size);
                    SDL_free(bytes);
                }
            } else {
                SDL_WaitProcess(process, true, &exitCode);
            }
            {
                const std::scoped_lock lock(mutex_);
                process_ = nullptr;
                SDL_DestroyProcess(process);
                output_ = std::move(output);
                exitCode_ = exitCode;
            }
            done_ = true;
        });
    }

    [[nodiscard]] bool finished() const { return worker_.joinable() && done_; }

    /// The output and exit code, once finished; the child is then forgotten.
    std::pair<std::string, int> collect() {
        worker_.join();
        const std::scoped_lock lock(mutex_);
        return {std::move(output_), exitCode_};
    }

    /// Kill a running child and wait for its thread.
    void stop() {
        if (!worker_.joinable()) return;
        {
            const std::scoped_lock lock(mutex_);
            if (process_ != nullptr) SDL_KillProcess(process_, true);
        }
        worker_.join();
    }

private:
    std::jthread worker_;
    std::mutex mutex_;
    SDL_Process* process_ = nullptr;
    std::atomic<bool> done_ = false;
    std::string output_;
    int exitCode_ = -1;
};

/// UTA-0179: the maps UT99's own lists offer, by the game types ut-bake reads
/// from the install. When ut-bake gives no answer every map is listed, and
/// `note` says so, since a movie such as CityIntro then opens to black.
std::vector<MapFile> offeredMaps(const std::filesystem::path& install, std::string& note) {
    std::vector<MapFile> maps = listMaps(install);
    const std::string baker = besideThisProgram("ut-bake");
    const std::string installText = install.string();
    const char* const argv[] = {baker.c_str(), "--game-types", installText.c_str(), nullptr};
    std::optional<std::vector<std::string>> prefixes;
    if (SDL_Process* const process = SDL_CreateProcess(argv, true)) {
        std::size_t size = 0;
        int exitCode = -1;
        if (void* const bytes = SDL_ReadProcess(process, &size, &exitCode)) {
            prefixes = readMapPrefixes(std::string_view(static_cast<const char*>(bytes), size), exitCode);
            SDL_free(bytes);
        }
        SDL_DestroyProcess(process);
    }
    if (!prefixes) {
        note = "ut-bake could not list the game types, so every file in Maps is shown. ";
        return maps;
    }
    return playableMaps(std::move(maps), *prefixes);
}

enum class Focus { List, Notes };
enum class Busy { No, Baking, Viewing };

class Launcher {
public:
    Launcher(const Options& options, LauncherPaths paths, SDL_Window* window, SDL_Renderer* renderer)
        : options_(options), paths_(std::move(paths)), window_(window), renderer_(renderer) {
        std::string note;
        maps_ = offeredMaps(options.install, note);
        results_.reserve(maps_.size());
        for (const MapFile& map : maps_) results_.push_back(readResult(paths_.results, map.name));
        refilter();
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, nullptr, &height);
        scale_ = std::clamp(static_cast<int>(height / 360.0 + 0.5), 2, 12); // readable at any desktop size
        status_ = note + "Notes are saved in " + shown(paths_.notes);
    }

    int run() {
        while (!quit_) {
            SDL_Event event;
            if (SDL_WaitEventTimeout(&event, busy_ == Busy::No ? 250 : 50)) {
                handle(event);
                while (SDL_PollEvent(&event)) handle(event);
            }
            if (busy_ != Busy::No && child_.finished()) finishChild();
            if (busy_ != Busy::Viewing) draw();
        }
        child_.stop();
        return EXIT_OK;
    }

private:
    // --- state ---

    [[nodiscard]] const MapFile* selected() const {
        return shown_.empty() ? nullptr : &maps_[shown_[selection_]];
    }

    void refilter() {
        const std::string previous = selected() != nullptr ? selected()->name : std::string();
        shown_ = filterMaps(maps_, filter_);
        selection_ = 0;
        for (std::size_t i = 0; i < shown_.size(); ++i) {
            if (maps_[shown_[i]].name == previous) selection_ = i;
        }
        loadNotes();
    }

    void loadNotes() {
        const MapFile* const map = selected();
        const std::string name = map != nullptr ? map->name : std::string();
        if (name == notesFor_) return;
        notesFor_ = name;
        notes_ = map != nullptr ? readNotes(paths_.notes, name) : std::string();
    }

    void move(long by) {
        if (shown_.empty()) return;
        const long last = static_cast<long>(shown_.size()) - 1;
        selection_ = static_cast<std::size_t>(std::clamp(static_cast<long>(selection_) + by, 0L, last));
        loadNotes();
    }

    void editNotes(std::string text) {
        if (notesFor_.empty()) return;
        notes_ = std::move(text);
        if (const auto saved = writeNotes(paths_.notes, notesFor_, notes_); !saved)
            status_ = "The notes did not save: " + std::string(saved.error().message());
    }

    void record(const std::string& map, const MapResult& result) {
        for (std::size_t i = 0; i < maps_.size(); ++i) {
            if (maps_[i].name == map) results_[i] = result;
        }
        if (const auto written = writeResult(paths_.results, map, result); !written)
            std::cerr << "ut-ants: " << written.error().message() << "\n";
    }

    // --- opening a map ---

    void open() {
        const MapFile* const map = selected();
        if (map == nullptr || busy_ != Busy::No) return;
        opening_ = map->name;
        busy_ = Busy::Baking;
        started_ = SDL_GetTicks();
        child_.start({besideThisProgram("ut-bake"), "--install", options_.install.string(), "--out",
                      paths_.bakes.string(), map->path.string()},
                     true);
    }

    void finishChild() {
        auto [output, exitCode] = child_.collect();
        if (busy_ == Busy::Baking) {
            const BakeAnswer answer = readBakeAnswer(output, exitCode);
            if (!answer.baked) {
                record(opening_, {.failed = true, .failure = answer.failure});
                status_ = opening_ + " did not bake.";
                busy_ = Busy::No;
                return;
            }
            record(opening_, {});
            std::vector<std::string> args{besideThisProgram("ut-ants")};
            if (options_.windowed) args.emplace_back("--windowed");
            if (options_.validation) args.emplace_back("--validation");
            if (options_.tier.has_value()) {
                args.emplace_back("--tier");
                args.emplace_back(urender::tierName(*options_.tier));
            }
            args.emplace_back("--notes"); // UTA-0190: P writes the camera there
            args.push_back(notesFile(paths_.notes, opening_).string());
            // UTA-0191: the report is the only place the baker version appears,
            // and it is gone once this answer is dropped. A cached verdict
            // carries it too, so a map opened a second time is described as
            // fully as the first.
            if (!answer.bakerVersion.empty()) {
                args.emplace_back("--baker-version");
                args.push_back(answer.bakerVersion);
            }
            args.push_back(options_.install.string());
            args.push_back(answer.path.string());
            busy_ = Busy::Viewing;
            SDL_HideWindow(window_);
            child_.start(std::move(args), false);
            return;
        }

        // Back from the viewer: straight to the notes, which is what it was for.
        if (exitCode != 0) {
            record(opening_, {.failed = true,
                              .failure = "it baked, but the viewer stopped with exit code " +
                                         std::to_string(exitCode)});
        }
        busy_ = Busy::No;
        // UTA-0190: the viewer may have added to the notes, so read them again
        // rather than keep the copy a later keystroke would save over them.
        notesFor_.clear();
        loadNotes();
        focus_ = Focus::Notes;
        status_ = "Back from " + opening_ + ". Notes are saved in " + shown(paths_.notes);
        SDL_ShowWindow(window_);
        SDL_RaiseWindow(window_);
    }

    // --- input ---

    void handle(const SDL_Event& event) {
        if (event.type == SDL_EVENT_QUIT) {
            quit_ = true;
            return;
        }
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            if (SDL_GetGamepadFromID(event.gdevice.which) == nullptr) SDL_OpenGamepad(event.gdevice.which);
            return;
        }
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            if (SDL_Gamepad* const pad = SDL_GetGamepadFromID(event.gdevice.which)) SDL_CloseGamepad(pad);
            return;
        }
        if (busy_ != Busy::No) return; // nothing is picked while a map is baking or open

        switch (event.type) {
        case SDL_EVENT_KEY_DOWN: key(event.key); break;
        case SDL_EVENT_TEXT_INPUT:
            if ((SDL_GetModState() & SDL_KMOD_CTRL) != 0) break;
            if (focus_ == Focus::Notes) {
                editNotes(notes_ + event.text.text);
            } else {
                filter_ += event.text.text;
                refilter();
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL: move(event.wheel.y > 0 ? -3 : 3); break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            focus_ = Focus::List;
            if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) move(-1);
            if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) move(1);
            if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) move(-pageRows());
            if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) move(pageRows());
            if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) open();
            break;
        default: break;
        }
    }

    void key(const SDL_KeyboardEvent& key) {
        if ((key.mod & SDL_KMOD_CTRL) != 0) {
            if (key.key == SDLK_EQUALS || key.key == SDLK_PLUS || key.key == SDLK_KP_PLUS) scale_ = std::min(scale_ + 1, 12);
            if (key.key == SDLK_MINUS || key.key == SDLK_KP_MINUS) scale_ = std::max(scale_ - 1, 1);
            return;
        }
        if (key.key == SDLK_TAB) {
            focus_ = focus_ == Focus::List ? Focus::Notes : Focus::List;
            return;
        }
        if (focus_ == Focus::Notes) {
            if (key.key == SDLK_ESCAPE) focus_ = Focus::List;
            if (key.key == SDLK_BACKSPACE) editNotes(withoutLastCharacter(notes_));
            if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) editNotes(notes_ + "\n");
            return;
        }
        switch (key.key) {
        case SDLK_ESCAPE: quit_ = true; break;
        case SDLK_UP: move(-1); break;
        case SDLK_DOWN: move(1); break;
        case SDLK_PAGEUP: move(-pageRows()); break;
        case SDLK_PAGEDOWN: move(pageRows()); break;
        case SDLK_HOME: move(-static_cast<long>(shown_.size())); break;
        case SDLK_END: move(static_cast<long>(shown_.size())); break;
        case SDLK_BACKSPACE:
            if (!filter_.empty()) {
                filter_ = withoutLastCharacter(filter_);
                refilter();
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: open(); break;
        default: break;
        }
    }

    // --- drawing ---

    struct Grid {
        int columns = 0;
        int rows = 0;
    };

    [[nodiscard]] Grid grid() const {
        int width = 0, height = 0;
        SDL_GetRenderOutputSize(renderer_, &width, &height);
        return {static_cast<int>(width / (GLYPH * static_cast<float>(scale_))),
                static_cast<int>(height / (ROW * static_cast<float>(scale_)))};
    }

    [[nodiscard]] long pageRows() const { return std::max(1, grid().rows - 6); }

    void colour(Uint8 r, Uint8 g, Uint8 b) { SDL_SetRenderDrawColor(renderer_, r, g, b, SDL_ALPHA_OPAQUE); }

    void text(int column, int row, std::string_view line) {
        const std::string owned(line);
        SDL_RenderDebugText(renderer_, static_cast<float>(column) * GLYPH, static_cast<float>(row) * ROW + 1.0f,
                            owned.c_str());
    }

    void box(int column, int row, int columns, int rows) {
        const SDL_FRect rect{static_cast<float>(column) * GLYPH - 3.0f, static_cast<float>(row) * ROW - 2.0f,
                             static_cast<float>(columns) * GLYPH + 6.0f, static_cast<float>(rows) * ROW + 3.0f};
        SDL_RenderRect(renderer_, &rect);
    }

    static std::string clipped(std::string_view line, int width) {
        if (width <= 0) return {};
        if (line.size() <= static_cast<std::size_t>(width)) return std::string(line);
        return std::string(line.substr(0, static_cast<std::size_t>(std::max(width - 3, 0)))) + "...";
    }

    void draw() {
        SDL_SetRenderScale(renderer_, static_cast<float>(scale_), static_cast<float>(scale_));
        colour(18, 18, 24);
        SDL_RenderClear(renderer_);
        const Grid g = grid();

        colour(235, 235, 235);
        text(1, 0,
             clipped("UT_Ants maps - " + std::to_string(shown_.size()) + " of " + std::to_string(maps_.size()) +
                         " shown.  Filter: " + filter_ + (focus_ == Focus::List ? "_" : ""),
                     g.columns - 2));
        colour(170, 170, 185);
        text(1, 1,
             clipped(busy_ == Busy::Baking ? "Baking - please wait."
                     : focus_ == Focus::List
                         ? "Up/Down: pick  Type: filter  Enter: open  Tab: notes  Ctrl+/-: size  Esc: quit"
                         : "Type your notes; they save as you type. Tab or Esc: back to the list.",
                     g.columns - 2));

        const int listWidth = std::clamp(g.columns / 2, 20, 46);
        const int top = 3;
        const int visible = std::max(1, g.rows - top - 2);
        drawList(1, top, listWidth, visible);
        drawDetails(listWidth + 3, top, g.columns - listWidth - 4, visible);

        colour(170, 170, 185);
        std::string bottom = status_;
        if (busy_ == Busy::Baking) {
            bottom = "Baking " + opening_ + ": " + std::to_string((SDL_GetTicks() - started_) / 1000) +
                     " s. A first bake can take a minute.";
        }
        text(1, g.rows - 1, clipped(bottom, g.columns - 2));
        SDL_RenderPresent(renderer_);
    }

    void drawList(int column, int row, int width, int rows) {
        if (focus_ == Focus::List) colour(120, 150, 220);
        else colour(70, 70, 85);
        box(column, row, width, rows);
        if (shown_.empty()) {
            colour(170, 170, 185);
            text(column, row, maps_.empty() ? "No maps in this install." : "No map matches the filter.");
            return;
        }
        if (selection_ < listTop_) listTop_ = selection_;
        if (selection_ >= listTop_ + static_cast<std::size_t>(rows)) listTop_ = selection_ + 1 - rows;
        listTop_ = std::min(listTop_, shown_.size() > static_cast<std::size_t>(rows) ? shown_.size() - rows : 0);

        for (int i = 0; i < rows && listTop_ + i < shown_.size(); ++i) {
            const std::size_t index = shown_[listTop_ + i];
            const bool isSelected = listTop_ + i == selection_;
            if (isSelected) {
                colour(50, 80, 140);
                const SDL_FRect bar{static_cast<float>(column) * GLYPH - 2.0f,
                                    static_cast<float>(row + i) * ROW - 1.0f,
                                    static_cast<float>(width) * GLYPH + 4.0f, ROW};
                SDL_RenderFillRect(renderer_, &bar);
            }
            const std::optional<MapResult>& result = results_[index];
            const std::string_view tag = !result ? "" : result->failed ? "FAILED" : "baked";
            colour(235, 235, 235);
            text(column, row + i, clipped(maps_[index].name, width - 7));
            if (!tag.empty()) {
                if (result->failed) colour(245, 120, 110);
                else colour(130, 210, 130);
                text(column + width - static_cast<int>(tag.size()), row + i, tag);
            }
        }
    }

    void drawDetails(int column, int row, int width, int rows) {
        const MapFile* const map = selected();
        if (map == nullptr || width < 10) return;
        const std::optional<MapResult>& result = results_[shown_[selection_]];

        colour(235, 235, 235);
        text(column, row, clipped(map->name, width));
        int line = row + 1;
        const auto paragraph = [&](const std::string& words) {
            for (const std::string& part : wrapText(words, static_cast<std::size_t>(width))) {
                if (line >= row + rows) return;
                text(column, line++, part);
            }
        };
        if (!result) {
            colour(170, 170, 185);
            paragraph("Not baked yet. Enter bakes it, which can take a minute, then opens it.");
        } else if (result->failed) {
            colour(245, 120, 110);
            paragraph("Failed: " + result->failure);
        } else {
            colour(130, 210, 130);
            paragraph("Baked. Enter opens it.");
        }
        line += 1;

        colour(235, 235, 235);
        text(column, line++, "Notes:");
        const int notesTop = line;
        const int notesRows = row + rows - notesTop;
        if (notesRows < 1) return;
        if (focus_ == Focus::Notes) colour(120, 150, 220);
        else colour(70, 70, 85);
        box(column, notesTop, width, notesRows);

        std::vector<std::string> lines = wrapText(notes_ + (focus_ == Focus::Notes ? "_" : ""),
                                                  static_cast<std::size_t>(width));
        const std::size_t first = lines.size() > static_cast<std::size_t>(notesRows) ? lines.size() - notesRows : 0;
        colour(235, 235, 235);
        for (std::size_t i = first; i < lines.size(); ++i) text(column, notesTop + static_cast<int>(i - first), lines[i]);
    }

    const Options& options_;
    const LauncherPaths paths_;
    SDL_Window* const window_;
    SDL_Renderer* const renderer_;

    std::vector<MapFile> maps_; // set once, by the constructor
    std::vector<std::optional<MapResult>> results_;
    std::vector<std::size_t> shown_;
    std::size_t selection_ = 0;
    std::size_t listTop_ = 0;
    std::string filter_;
    std::string notesFor_;
    std::string notes_;
    Focus focus_ = Focus::List;
    int scale_ = 2;
    std::string status_;
    bool quit_ = false;

    Busy busy_ = Busy::No;
    std::string opening_;
    Uint64 started_ = 0;
    Child child_;
};

} // namespace

int runLauncher(const Options& options) {
    const auto paths = launcherPaths();
    if (!paths) {
        std::cerr << "ut-ants: the launcher has nowhere to keep bakes and notes: " << paths.error().message() << "\n";
        return EXIT_FAILED;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "ut-ants: SDL did not start: " << SDL_GetError() << "\n";
        return EXIT_FAILED;
    }
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) std::cerr << "ut-ants: gamepads are off: " << SDL_GetError() << "\n";

    int width = 1280, height = 720;
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (!options.windowed) {
        flags |= SDL_WINDOW_FULLSCREEN;
        if (const SDL_DisplayMode* const desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())) {
            width = desktop->w;
            height = desktop->h;
        }
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("UT_Ants maps", width, height, flags, &window, &renderer)) {
        std::cerr << "ut-ants: SDL could not open the launcher's window: " << SDL_GetError() << "\n";
        SDL_Quit();
        return EXIT_FAILED;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_StartTextInput(window);
    SDL_SyncWindow(window);

    int status = EXIT_FAILED;
    {
        Launcher launcher(options, *paths, window, renderer);
        status = launcher.run();
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}

} // namespace uta::client
