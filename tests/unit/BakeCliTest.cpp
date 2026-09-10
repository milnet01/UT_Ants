// UTA-0011's command-line cases -- INV-15.
//
// docs/specs/UTA-0011-map-baker.md SS 4.8. tools/ut-bake/Cli.cpp is compiled
// into this binary, so every case drives the command line as a function over
// a synthetic install on disk, with no process started.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH. Catch2 treats a
// comma as a filter separator, and parses a leading dash as an option, so ctest
// running such a case by name runs nothing and reports it failed.

#include "BakeFixture.h"

#include "ut-bake/Cli.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using uta::test::bake::standardFixture;
using uta::test::bake::TempDir;
using uta::test::bake::writeInstall;

namespace {

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

Run run(const std::vector<std::string>& args, std::optional<std::uint64_t> budgetBytes = {}) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    Run result;
    result.code = budgetBytes.has_value()
                      ? uta::ubake::detail::runCli(views, out, err, *budgetBytes)
                      : uta::ubake::runCli(views, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

/// Whether `text` is exactly one JSON object and a newline: brackets balance
/// outside strings, and the outermost one closes exactly once, at the end.
bool isOneObject(const std::string& text) {
    if (text.size() < 3 || text.front() != '{' || text.back() != '\n') return false;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            if (--depth < 0) return false;
            if (depth == 0 && i + 2 != text.size()) return false; // something after it
        }
    }
    return depth == 0 && !inString;
}

bool hasKey(const std::string& json, std::string_view key) {
    return json.find("\"" + std::string(key) + "\":") != std::string::npos;
}

bool says(const std::string& json, std::string_view fragment) {
    return json.find(fragment) != std::string::npos;
}

struct Install {
    TempDir dir;
    fs::path install;
    fs::path map;
    fs::path out;

    Install() {
        install = dir.path() / "install";
        map = writeInstall(install, standardFixture());
        out = dir.path() / "out";
    }

    [[nodiscard]] std::vector<std::string> bake(bool force = false) const {
        std::vector<std::string> args = {"--install", install.string(), "--out", out.string()};
        if (force) args.emplace_back("--force");
        args.push_back(map.string());
        return args;
    }
};

std::size_t filesIn(const fs::path& directory) {
    std::size_t count = 0;
    if (!fs::exists(directory)) return 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(directory)) ++count;
    return count;
}

} // namespace

TEST_CASE("the install check prints one object and exits 0 on a good install",
          "[ubake][cli]") {
    const Install fixture;
    const Run result = run({"--check", fixture.install.string()});
    CHECK(result.code == 0);
    CHECK(isOneObject(result.out));
    CHECK(says(result.out, "\"schema\": 1"));
    CHECK(hasKey(result.out, "install"));
    CHECK(says(result.out, "\"ok\": true"));
    CHECK(says(result.out, "\"problems\": []"));
}

TEST_CASE("the install check exits 1 and names each problem on a bad install",
          "[ubake][cli]") {
    const TempDir empty;
    const Run result = run({"--check", empty.path().string()});
    CHECK(result.code == 1);
    CHECK(isOneObject(result.out));
    CHECK(says(result.out, "\"ok\": false"));
    CHECK(says(result.out, "\"what\": \"System/Botpack.u\""));
    CHECK(hasKey(result.out, "why"));
}

TEST_CASE("a bake prints written and then cached and exits 0", "[ubake][cli]") {
    const Install fixture;

    const Run written = run(fixture.bake());
    CHECK(written.code == 0);
    CHECK(isOneObject(written.out));
    CHECK(says(written.out, "\"verdict\": \"written\""));
    for (const std::string_view key : {"schema", "map", "bakerVersion", "name", "path", "rooms",
                                       "budget", "skipped"}) {
        INFO("key: " << key);
        CHECK(hasKey(written.out, key));
    }
    CHECK_FALSE(hasKey(written.out, "error"));
    CHECK(filesIn(fixture.out) == 1);

    const Run cached = run(fixture.bake());
    CHECK(cached.code == 0);
    CHECK(isOneObject(cached.out));
    CHECK(says(cached.out, "\"verdict\": \"cached\""));
    CHECK(hasKey(cached.out, "name"));
    CHECK(hasKey(cached.out, "path"));
    for (const std::string_view key : {"rooms", "budget", "skipped", "error"}) {
        INFO("key: " << key);
        CHECK_FALSE(hasKey(cached.out, key));
    }

    const Run forced = run(fixture.bake(true));
    CHECK(forced.code == 0);
    CHECK(says(forced.out, "\"verdict\": \"written\""));
}

TEST_CASE("a refused bake prints its error and exits 1", "[ubake][cli]") {
    Install fixture;
    fixture.map = fixture.install / "Maps" / "No-Such-Map.unr";
    const Run refused = run(fixture.bake());
    CHECK(refused.code == 1);
    CHECK(isOneObject(refused.out));
    CHECK(says(refused.out, "\"verdict\": \"refused\""));
    CHECK(hasKey(refused.out, "error"));
    for (const std::string_view key : {"name", "path", "rooms", "budget", "skipped"}) {
        INFO("key: " << key);
        CHECK_FALSE(hasKey(refused.out, key));
    }
}

TEST_CASE("an over-budget bake exits 1 and leaves no file", "[ubake][cli]") {
    // INV-15's over-budget row, through the seam with a budget of one byte.
    const Install fixture;
    const Run over = run(fixture.bake(), 1);
    CHECK(over.code == 1);
    CHECK(isOneObject(over.out));
    CHECK(says(over.out, "\"verdict\": \"over-budget\""));
    for (const std::string_view key : {"name", "path", "rooms", "budget", "skipped"}) {
        INFO("key: " << key);
        CHECK(hasKey(over.out, key));
    }
    CHECK(says(over.out, "\"budgetBytes\": 1"));
    CHECK_FALSE(hasKey(over.out, "error"));
    CHECK(filesIn(fixture.out) == 0);
}

TEST_CASE("wrong arguments exit 2 and print nothing on standard output", "[ubake][cli]") {
    const std::vector<std::vector<std::string>> wrong = {
        {},
        {"--check"},
        {"--bogus"},
        {"--install", "i", "--out", "o"},
        {"--install", "i", "map"},
        {"--check", "i", "--force"},
        {"--install", "i", "--out", "o", "first", "second"},
        {"--install", "i", "--install", "j", "--out", "o", "map"},
    };
    for (const std::vector<std::string>& args : wrong) {
        const Run result = run(args);
        CHECK(result.code == 2);
        CHECK(result.out.empty());
        CHECK(says(result.err, "usage:"));
    }

    const Run help = run({"--help"});
    CHECK(help.code == 0);
    CHECK(help.out.empty());
    CHECK(says(help.err, "usage:"));
}
