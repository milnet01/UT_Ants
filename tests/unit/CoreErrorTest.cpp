// Locks INV-1 and INV-2 of docs/specs/UTA-0002-core-foundations.md.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "core/Error.h"

using uta::Error;
using uta::ErrorCode;
using uta::Result;

namespace {

/// A stand-in for a fallible core entry point, so the test can exercise the
/// failure arm without reaching the filesystem.
Result<int> failing() {
    return uta::fail(ErrorCode::NotFound, "no such file: dm-deck16.unr");
}

Result<int> succeeding() { return 7; }

/// Calls `failing` through UTA_TRY, which must return the error unchanged.
Result<int> propagates() {
    UTA_TRY(auto value, failing());
    return value + 1;
}

Result<int> propagatesOnSuccess() {
    UTA_TRY(auto value, succeeding());
    return value + 1;
}

}  // namespace

// INV-1 -- every failure carries a non-empty message and a code chosen for
// it, never Unknown as a shortcut.
TEST_CASE("a failure carries a named code and a non-empty message", "[core][error]") {
    const Result<int> result = failing();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::NotFound);
    CHECK(result.error().code() != ErrorCode::Unknown);
    CHECK_FALSE(result.error().message().empty());
}

TEST_CASE("every code has a non-empty name", "[core][error]") {
    // Unknown is a legitimate code -- what INV-1 forbids is reaching for it
    // instead of naming the reason. Its NAME must still be usable in a log.
    CHECK(uta::errorCodeName(ErrorCode::Unknown) == "Unknown");
    CHECK(uta::errorCodeName(ErrorCode::NotFound) == "NotFound");
    CHECK_FALSE(uta::errorCodeName(ErrorCode::Cancelled).empty());
}

// INV-2 -- withContext keeps the code and PREFIXES the message; the original
// survives in full. A replacement would read fine and lose the reason.
TEST_CASE("withContext prefixes and keeps the original message", "[core][error]") {
    const Error inner(ErrorCode::NotFound, "no such file: dm-deck16.unr");
    const Error outer = inner.withContext("loading DM-Deck16");

    CHECK(outer.code() == ErrorCode::NotFound);
    CHECK(outer.message() == "loading DM-Deck16: no such file: dm-deck16.unr");

    // Stated separately from the equality above: the equality would still pass
    // if a future edit changed the separator, and this is the half INV-2 is
    // actually about.
    CHECK(outer.message().find(inner.message()) != std::string_view::npos);
    CHECK_FALSE(inner.message().empty());
}

TEST_CASE("context nests outermost-first", "[core][error]") {
    const Error e = Error(ErrorCode::IoFailure, "disk full")
                        .withContext("writing the bundle")
                        .withContext("baking DM-Deck16");

    CHECK(e.code() == ErrorCode::IoFailure);
    CHECK(e.message() == "baking DM-Deck16: writing the bundle: disk full");
}

TEST_CASE("UTA_TRY returns the error unchanged and binds on success", "[core][error]") {
    const Result<int> failed = propagates();
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code() == ErrorCode::NotFound);
    CHECK(failed.error().message() == "no such file: dm-deck16.unr");

    const Result<int> ok = propagatesOnSuccess();
    REQUIRE(ok.has_value());
    CHECK(*ok == 8);
}
