// The error type every module boundary reports through.
//
// docs/design.md § What every part does the same way: std::expected<T, Error>
// across every module boundary; exceptions may be used inside a part and must
// never escape one. This is that type, and Result<T> is that spelling.
//
// An Error carries a reason code and a sentence, and nothing else. No source
// location and no chain of causes -- docs/specs/UTA-0002-core-foundations.md
// § 8 records why both were rejected. Context is added by prefixing as the
// error travels up, which reads outermost-first without a node per link.

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace uta {

/// Why something failed. Deliberately small and general: a part that needs to
/// say more says it in the message, not by adding a code here.
enum class ErrorCode : std::uint16_t {
    Unknown = 0,
    NotFound,
    PermissionDenied,
    AlreadyExists,
    InvalidArgument,
    MalformedData,
    UnsupportedVersion,
    IoFailure,
    OutOfMemory,
    Cancelled,
};

/// The short, stable name of a code -- for logs and test failure output, not
/// for a player. Never empty.
[[nodiscard]] std::string_view errorCodeName(ErrorCode code) noexcept;

/// A failure crossing a module boundary: why it failed, and a sentence saying
/// so. Both are always present -- there is no default constructor, so an
/// Error cannot exist without a caller having chosen both.
class Error {
public:
    Error(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

    /// Ref-qualified, and the rvalue overload is deleted on purpose. The view
    /// aliases message_, and withContext() returns a prvalue -- so the chained
    /// shape this header's own example teaches
    ///     auto m = err.withContext("...").message();
    /// would leave m pointing into an Error destroyed at the end of the full
    /// expression. Measured: it printed nothing, and ASan did not flag it,
    /// because a short message lives inside the object rather than on the
    /// heap. Deleting the overload makes that a compile error instead.
    [[nodiscard]] std::string_view message() const& noexcept { return message_; }
    std::string_view message() const&& = delete;

    /// Prepend context, keeping the code:
    ///   readFile -> Error{NotFound, "no such file: dm-deck16.unr"}
    ///   .withContext("loading DM-Deck16")
    ///   -> "loading DM-Deck16: no such file: dm-deck16.unr"
    ///
    /// The original message survives in full. Replacing it rather than
    /// prefixing loses the innermost reason by the time a caller prints it,
    /// which is INV-2.
    [[nodiscard]] Error withContext(std::string_view context) const;

private:
    ErrorCode code_;
    std::string message_;
};

/// What a fallible function returns. Result<void> is how one that yields
/// nothing reports a failure; there is no second spelling for that case.
template <class T>
using Result = std::expected<T, Error>;

/// Shorthand for the failure arm, so a return reads as one expression.
[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error(code, std::move(message)));
}

}  // namespace uta

// Propagation. std::expected's and_then and transform cover chained
// transformations; the two below cover the imperative case. They are core's
// only PROPAGATION macros -- UTA_LOG is Log.h's.
//
// No statement expressions -- MSVC has none. The temporary is named through
// two levels of indirection, which is what makes the inner macro expand its
// argument before pasting.

#define UTA_DETAIL_CAT_(a, b) a##b
#define UTA_DETAIL_CAT(a, b) UTA_DETAIL_CAT_(a, b)

// __COUNTER__ rather than __LINE__. UTA_TRY declares in the ENCLOSING scope,
// so two on one physical line would redefine one name; and MSVC's
// edit-and-continue build does not expand __LINE__ to a pasteable token, which
// matters here because MSVC is the whole reason this shape exists. GCC, Clang
// and MSVC all provide __COUNTER__; __LINE__ remains the fallback.
#ifdef __COUNTER__
#define UTA_DETAIL_UNIQUE(base) UTA_DETAIL_CAT(base, __COUNTER__)
#else
#define UTA_DETAIL_UNIQUE(base) UTA_DETAIL_CAT(base, __LINE__)
#endif

/// Bind, or return the error to the caller.
///   UTA_TRY(auto bytes, uta::fs::readFile(path));
///
/// This declares in the enclosing scope, so it must sit at block-statement
/// position: brace the body of any if or loop that uses it.
#define UTA_TRY(declaration, expression)                                     \
    UTA_DETAIL_TRY(declaration, expression, UTA_DETAIL_UNIQUE(utaResult_))

#define UTA_DETAIL_TRY(declaration, expression, tmp)                         \
    auto tmp = (expression);                                                 \
    if (!tmp.has_value()) return std::unexpected(std::move(tmp).error());    \
    declaration = *std::move(tmp)

/// Run for effect, or return the error to the caller.
///   UTA_CHECK(uta::fs::writeFileAtomically(path, bytes));
#define UTA_CHECK(expression)                                                \
    do {                                                                     \
        auto utaStatus = (expression);                                       \
        if (!utaStatus.has_value())                                          \
            return std::unexpected(std::move(utaStatus).error());            \
    } while (false)
