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
    [[nodiscard]] std::string_view message() const noexcept { return message_; }

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
// transformations; these two cover the imperative case, and core defines no
// other macros.
//
// No statement expressions -- MSVC has none. The temporary is named from
// __LINE__ through two levels of indirection, which is what makes the inner
// macro expand its argument before pasting.

#define UTA_DETAIL_CAT_(a, b) a##b
#define UTA_DETAIL_CAT(a, b) UTA_DETAIL_CAT_(a, b)

/// Bind, or return the error to the caller.
///   UTA_TRY(auto bytes, uta::fs::readFile(path));
#define UTA_TRY(declaration, expression)                                     \
    auto UTA_DETAIL_CAT(utaResult_, __LINE__) = (expression);                \
    if (!UTA_DETAIL_CAT(utaResult_, __LINE__).has_value())                   \
        return std::unexpected(                                              \
            std::move(UTA_DETAIL_CAT(utaResult_, __LINE__)).error());        \
    declaration = *std::move(UTA_DETAIL_CAT(utaResult_, __LINE__))

/// Run for effect, or return the error to the caller.
///   UTA_CHECK(uta::fs::writeFileAtomically(path, bytes));
#define UTA_CHECK(expression)                                                \
    do {                                                                     \
        auto UTA_DETAIL_CAT(utaStatus_, __LINE__) = (expression);            \
        if (!UTA_DETAIL_CAT(utaStatus_, __LINE__).has_value())               \
            return std::unexpected(                                          \
                std::move(UTA_DETAIL_CAT(utaStatus_, __LINE__)).error());    \
    } while (false)
