// Locks INV-1 to INV-6 of docs/specs/UTA-0049-numeric-contract.md. (INV-7,
// the CMake refusal, has no automated test -- see that spec's § 7.)
//
// Every value here is asserted by BIT PATTERN (std::bit_cast to the
// unsigned integer of the same width), never by epsilon: the point of this
// contract is exactness across GCC, Clang and MSVC, and an epsilon
// comparison would hide the one-bit disagreement it exists to catch.
//
// Every "unknown" operand is read through a volatile intermediate, or
// crosses a noinline function boundary, so the optimiser cannot
// constant-fold it away and hide the very transform being checked for --
// verified on this machine (2026-09-04, GCC 16.2.0 and Clang 22.1.8): the
// same expressions written as compile-time constants fold to the
// IEEE-correct answer either way, which would make the test pass whether
// or not -ffast-math was on.
//
// No transcendental (sin/cos/exp/pow/...) is asserted by bit pattern here,
// on purpose -- glibc, LLVM's libm and MSVC's UCRT do not agree
// bit-for-bit, and asserting one would lock in something that is not true
// and red the matrix the moment a transcendental is exercised.
// docs/specs/UTA-0049-numeric-contract.md § 9 says how that half of the
// contract would be covered instead (a lint banning the call, not a
// numeric test).

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

using Bits64 = std::uint64_t;

Bits64 bitsOf(double x) { return std::bit_cast<Bits64>(x); }

std::string hex64(Bits64 b) {
    std::ostringstream os;
    os << "0x" << std::hex << std::setw(16) << std::setfill('0') << b;
    return os.str();
}

// True exactly when `b` is the IEEE-754 binary64 bit PATTERN of a NaN --
// exponent field all-ones, mantissa nonzero -- checked structurally rather
// than by pinning one payload. This project's contract does not promise
// GCC, Clang and MSVC produce the same NaN payload for `x/x`, only that
// none of them folds it to a false, non-NaN answer such as 1.0.
bool isNanBits(Bits64 b) {
    constexpr Bits64 kExponentMask = 0x7ff0000000000000ULL;
    constexpr Bits64 kMantissaMask = 0x000fffffffffffffULL;
    return (b & kExponentMask) == kExponentMask && (b & kMantissaMask) != 0;
}

// noinline so the compiler cannot see through the call and constant-fold
// the reduction itself -- what is under test is whether the LOOP below gets
// reassociated/vectorised, not whether the frontend can precompute a
// compile-time-known array.
#if defined(_MSC_VER)
#define UTA_TEST_NOINLINE __declspec(noinline)
#else
#define UTA_TEST_NOINLINE __attribute__((noinline))
#endif

UTA_TEST_NOINLINE double sumForward(const double* values, std::size_t count) {
    double total = 0.0;
    for (std::size_t i = 0; i < count; ++i) total = total + values[i];
    return total;
}

UTA_TEST_NOINLINE double sumBackward(const double* values, std::size_t count) {
    double total = 0.0;
    for (std::size_t i = count; i-- > 0;) total = total + values[i];
    return total;
}

}  // namespace

// INV-1 -- a*b+c is NOT contracted into a single fused rounding step.
//
// a = 0.1, b = 0.3, c = -0.03. The double literal 0.03 rounds to exactly
// the same bit pattern as 0.1*0.3 computed in double (verified with
// Python's struct module, 2026-09-04), so c is exactly the negation of the
// ROUNDED product a*b. That makes the two paths diverge in a way that is
// worth spelling out:
//   - separately rounded: round(a*b) + c == round(a*b) - round(a*b) == 0.0
//     exactly -- subtracting a value from its own exact negation is always
//     exact in IEEE-754, whatever that value's rounding error was.
//   - fused: fma(a, b, c) rounds a*b and the add together in ONE step, so
//     it sees a*b's UNROUNDED product and returns the rounding error the
//     separate path had already discarded: 1.6653345369377347e-18.
// A build that contracts this expression returns the fused, nonzero result
// for a line of code that asked for the separate one.
//
// KNOWN LIMITATION, not hidden: at this project's default (no -march)
// build target there is no hardware FMA instruction, so this assertion
// passes with or without the CMakeLists.txt fix -- see
// docs/specs/UTA-0049-numeric-contract.md § 5. It is kept because it
// asserts the actual contract (the correct, honest value), and because a
// -march target or a software-contraction path would make the difference
// observable the moment either exists.
TEST_CASE("a*b+c is not fused into a single rounding step", "[build][numeric]") {
    volatile double va = 0.1;
    volatile double vb = 0.3;
    volatile double vc = -0.03;
    const double a = va;
    const double b = vb;
    const double c = vc;

    const double separate = a * b + c;
    const double fused = std::fma(a, b, c);

    INFO("separate = " << hex64(bitsOf(separate)) << ", fused = " << hex64(bitsOf(fused)));
    CHECK(bitsOf(separate) == 0x0000000000000000ULL);
    CHECK(bitsOf(fused) == 0x3c3eb851eb851eb8ULL);
    CHECK(bitsOf(separate) != bitsOf(fused));
}

// INV-2 -- (a+b)+c is evaluated in the order written, not reassociated to
// a+(b+c).
//
// a=1.0, b=1e100, c=-1e100 (Python-verified 2026-09-04): 1.0 is far too
// small to change 1e100's rounded representation, so (a+b) rounds to 1e100
// exactly and (a+b)+c rounds to 0.0. Reassociated as a+(b+c), b+c cancels
// to exactly 0.0 first and the whole expression would return 1.0 instead.
TEST_CASE("(a+b)+c is not reassociated to a+(b+c)", "[build][numeric]") {
    volatile double va = 1.0;
    volatile double vb = 1e100;
    volatile double vc = -1e100;
    const double a = va;
    const double b = vb;
    const double c = vc;

    const double writtenOrder = (a + b) + c;
    const double reassociated = a + (b + c);

    INFO("writtenOrder = " << hex64(bitsOf(writtenOrder))
                            << ", reassociated = " << hex64(bitsOf(reassociated)));
    CHECK(bitsOf(writtenOrder) == bitsOf(0.0));
    CHECK(bitsOf(reassociated) == bitsOf(1.0));
    CHECK(bitsOf(writtenOrder) != bitsOf(reassociated));
}

// INV-3 -- x/x is never assumed to be 1.0. -funsafe-math-optimizations
// (part of -ffast-math) licenses exactly that assumption for both a NaN
// and a zero operand. Measured directly on this machine 2026-09-04 (GCC
// 16.2.0 and Clang 22.1.8): under -ffast-math, x/x for both a NaN and 0.0
// returns 1.0 instead of NaN.
TEST_CASE("x/x is not folded to 1.0 for NaN or zero", "[build][numeric]") {
    volatile Bits64 nanBits = 0x7ff8000000000001ULL;
    const double nanValue = std::bit_cast<double>(static_cast<Bits64>(nanBits));
    volatile double vzero = 0.0;
    const double zero = vzero;

    const double nanOverNan = nanValue / nanValue;
    const double zeroOverZero = zero / zero;

    INFO("nan/nan = " << hex64(bitsOf(nanOverNan)) << ", 0/0 = " << hex64(bitsOf(zeroOverZero)));
    CHECK(isNanBits(bitsOf(nanOverNan)));
    CHECK(bitsOf(nanOverNan) != bitsOf(1.0));
    CHECK(isNanBits(bitsOf(zeroOverZero)));
    CHECK(bitsOf(zeroOverZero) != bitsOf(1.0));
}

// INV-4 -- x == x is never assumed true. -ffinite-math-only (part of
// -ffast-math) licenses assuming no NaN ever occurs, and once assumed,
// `x==x` simplifies to `true` unconditionally. Measured 2026-09-04:
// nan==nan becomes `true` under -ffast-math on both GCC and Clang.
TEST_CASE("NaN does not compare equal to itself", "[build][numeric]") {
    volatile Bits64 nanBits = 0x7ff8000000000001ULL;
    const double nanValue = std::bit_cast<double>(static_cast<Bits64>(nanBits));

    CHECK_FALSE(nanValue == nanValue);
    CHECK(nanValue != nanValue);
}

// INV-5 -- 0.0 - (+0.0) keeps its sign: the correctly-rounded IEEE-754
// result is +0.0, never -0.0. -fno-signed-zeros (part of -ffast-math)
// licenses treating `0.0 - x` as `-x`, which for x = +0.0 gives -0.0
// instead. Measured 2026-09-04: this flips from bit pattern
// 0x0000000000000000 to 0x8000000000000000 under -ffast-math on both GCC
// and Clang, for the identical expression compiled with -O3.
TEST_CASE("subtracting positive zero from zero keeps the sign", "[build][numeric]") {
    volatile double posZero = 0.0;
    const double result = 0.0 - posZero;

    INFO("result = " << hex64(bitsOf(result)));
    CHECK(bitsOf(result) == 0x0000000000000000ULL);
}

// INV-6a -- forward and backward accumulation of the same multiset give
// different, specific bit patterns -- not a common reassociated answer.
//
// {1, 1e16, -1e16, 1, 1e16, -1e16, 3}, summed forward by ordinary
// `total = total + v`, gives 3.0 (Python-verified 2026-09-04, full running
// total printed at each step): 1e16's representable numbers are 2.0 apart,
// so `1.0 + 1e16` rounds to exactly 1e16 -- each leading 1.0 is absorbed
// without trace, each pair then cancels to exactly 0.0, and only the
// trailing 3.0 survives. Summed backward the running total is never 0
// between steps, so the SAME rounding-to-a-multiple-of-2 instead loses part
// of the 3.0 and 1.0 terms as they combine with a live ±1e16 total, and the
// sequence resolves to 5.0 rather than 3.0. Both totals were computed by
// literally running this reduction, not derived by hand -- the mechanism is
// stated for the curious reader, the bit patterns below are what is
// actually asserted.
TEST_CASE("forward and backward accumulation give the order's own answer", "[build][numeric]") {
    const std::array<double, 7> values{1.0, 1e16, -1e16, 1.0, 1e16, -1e16, 3.0};

    const double forward = sumForward(values.data(), values.size());
    const double backward = sumBackward(values.data(), values.size());

    INFO("forward = " << hex64(bitsOf(forward)) << ", backward = " << hex64(bitsOf(backward)));
    CHECK(bitsOf(forward) == bitsOf(3.0));
    CHECK(bitsOf(backward) == bitsOf(5.0));
    CHECK(bitsOf(forward) != bitsOf(backward));
}

// INV-6b -- a long, exactly-cancelling sum stays exactly zero.
//
// One 1e16, sixty-two 1.0s, one -1e16, summed forward by ordinary
// sequential `+`. Under strict IEEE-754 arithmetic the running total is
// 1e16 after the first element (every following 1.0 is too small to move
// it) and lands on exactly 0.0 once the trailing -1e16 cancels it --
// verified both in Python and by compiling this exact loop shape on this
// machine 2026-09-04. It is also the genuinely LIVE discriminator among
// the assertions in this file at this project's actual (no -march) build
// target: compiled with -ffast-math, GCC 16.2.0 returns 32.0 and Clang
// 22.1.8 returns 48.0 for this same loop -- both wrong, and wrong
// DIFFERENTLY, which is its own argument for the contract existing at all.
TEST_CASE("a long cancelling sum stays exactly zero", "[build][numeric]") {
    std::array<double, 64> values{};
    values.front() = 1e16;
    values.back() = -1e16;
    for (std::size_t i = 1; i + 1 < values.size(); ++i) values[i] = 1.0;

    const double total = sumForward(values.data(), values.size());

    INFO("total = " << hex64(bitsOf(total)));
    CHECK(bitsOf(total) == bitsOf(0.0));
}
