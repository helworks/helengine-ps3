#pragma once

/// Supplies the C99 math functions this toolchain's C++ library expects but newlib does
/// not provide, so generated Core can use the std-qualified names it emits.
///
/// Two separate gaps are involved, and both are properties of the toolchain rather than of
/// the generated code:
///
/// 1. libstdc++ hides its C99 math overloads behind `_GLIBCXX_USE_C99_MATH_TR1`, which is
///    baked in when the library is built and is off in this cross toolchain. The build
///    defines it, which brings 28 functions such as `std::log2` and `std::round` into
///    scope, because newlib does declare them at global scope.
///
/// 2. With that macro on, `<cmath>` also imports five long-double entry points that newlib
///    genuinely lacks. They are defined here, before `<cmath>` is ever read, so its
///    `using ::log2l;` declarations resolve.
///
/// This header is force-included ahead of every translation unit in Core mode. It is a
/// build-level shim precisely so that generated output is never edited, which the project
/// rules forbid.
///
/// Precision note: the long-double forms delegate to their double counterparts. Nothing in
/// the generated tree performs long-double math, and PowerPC long double is not a format
/// newlib supports here, so the narrowing is a deliberate accommodation rather than a
/// silent loss.
///
/// No math header is included below, deliberately. Including one here would pull in
/// <cmath> before these definitions exist, and its own `using ::log2l;` declarations would
/// then fail on the very names this header supplies. Compiler builtins are used instead,
/// which need no declarations and lower to the same calls.

/// Base-2 logarithm for long double, delegating to the double implementation.
inline long double log2l(long double value) {
    return static_cast<long double>(__builtin_log2(static_cast<double>(value)));
}

/// Radix-independent exponent for long double, delegating to the double implementation.
inline long double logbl(long double value) {
    return static_cast<long double>(__builtin_logb(static_cast<double>(value)));
}

/// Next representable double after `from` in the direction of `to`.
inline double nexttoward(double from, long double to) {
    return __builtin_nextafter(from, static_cast<double>(to));
}

/// Next representable float after `from` in the direction of `to`.
inline float nexttowardf(float from, long double to) {
    return __builtin_nextafterf(from, static_cast<float>(to));
}

/// Next representable long double after `from` in the direction of `to`.
inline long double nexttowardl(long double from, long double to) {
    return static_cast<long double>(__builtin_nextafter(static_cast<double>(from), static_cast<double>(to)));
}
