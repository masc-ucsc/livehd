//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The constant-expression builtins of the rule DSL (`:let`, `:when`, `(k EXPR)`).
//
// ONE table serves every consumer: rulegen type-checks a rule against it, the
// generated rule code calls the functions below directly, and the generated
// fuzz/prove tests interpret the same expressions through `call`, so a guard
// means the same thing in the engine and in the tests that judge it.
//
// Values are int64 with checked arithmetic: an overflow, a negative shift
// amount or any other undefined result is `!ok`, and an `!ok` guard or `:let`
// simply does not match (the rule stays out of the way). `(k c)` binds only
// constants that fit an int64, so nothing here needs more.

#include <array>
#include <bit>
#include <cstdint>
#include <climits>
#include <span>
#include <string_view>

namespace livehd::aegraph::rt::ex {

struct Num {
  int64_t v  = 0;
  bool    ok = false;
};

[[nodiscard]] constexpr Num num(int64_t v) { return {v, true}; }
[[nodiscard]] constexpr Num flag(bool b) { return {b ? 1 : 0, true}; }
[[nodiscard]] constexpr bool holds(Num n) { return n.ok && n.v != 0; }

[[nodiscard]] constexpr Num add(Num a, Num b) {
  int64_t r = 0;
  return (a.ok && b.ok && !__builtin_add_overflow(a.v, b.v, &r)) ? num(r) : Num{};
}
[[nodiscard]] constexpr Num sub(Num a, Num b) {
  int64_t r = 0;
  return (a.ok && b.ok && !__builtin_sub_overflow(a.v, b.v, &r)) ? num(r) : Num{};
}
[[nodiscard]] constexpr Num mul(Num a, Num b) {
  int64_t r = 0;
  return (a.ok && b.ok && !__builtin_mul_overflow(a.v, b.v, &r)) ? num(r) : Num{};
}
[[nodiscard]] constexpr Num neg(Num a) { return sub(num(0), a); }
[[nodiscard]] constexpr Num min(Num a, Num b) { return (a.ok && b.ok) ? num(a.v < b.v ? a.v : b.v) : Num{}; }
[[nodiscard]] constexpr Num max(Num a, Num b) { return (a.ok && b.ok) ? num(a.v > b.v ? a.v : b.v) : Num{}; }
// floor(log2 a) for a > 0.
[[nodiscard]] constexpr Num log2_floor(Num a) {
  return (a.ok && a.v > 0) ? num(63 - std::countl_zero(static_cast<uint64_t>(a.v))) : Num{};
}
// 1 when a is a power of two (a > 0), else 0.
[[nodiscard]] constexpr Num pow2p(Num a) { return a.ok ? flag(a.v > 0 && (a.v & (a.v - 1)) == 0) : Num{}; }
// Number of set bits of a non-negative a.
[[nodiscard]] constexpr Num popcount(Num a) { return (a.ok && a.v >= 0) ? num(std::popcount(static_cast<uint64_t>(a.v))) : Num{}; }
// 2^a - 1 for 0 <= a <= 62.
[[nodiscard]] constexpr Num mask_of(Num a) { return (a.ok && a.v >= 0 && a.v <= 62) ? num((int64_t{1} << a.v) - 1) : Num{}; }
// 2^a for 0 <= a <= 62.
[[nodiscard]] constexpr Num pow2(Num a) { return (a.ok && a.v >= 0 && a.v <= 62) ? num(int64_t{1} << a.v) : Num{}; }
// ceil(log2 a) for a >= 1.
[[nodiscard]] constexpr Num clog2(Num a) {
  return (a.ok && a.v >= 1) ? num(a.v == 1 ? 0 : 64 - std::countl_zero(static_cast<uint64_t>(a.v - 1))) : Num{};
}
// Bitwise and of the two's complement values.
[[nodiscard]] constexpr Num band(Num a, Num b) { return (a.ok && b.ok) ? num(a.v & b.v) : Num{}; }
// a / b truncated toward zero (b != 0).
[[nodiscard]] constexpr Num div(Num a, Num b) {
  return (a.ok && b.ok && b.v != 0 && !(a.v == INT64_MIN && b.v == -1)) ? num(a.v / b.v) : Num{};
}
[[nodiscard]] constexpr Num lt(Num a, Num b) { return (a.ok && b.ok) ? flag(a.v < b.v) : Num{}; }
[[nodiscard]] constexpr Num le(Num a, Num b) { return (a.ok && b.ok) ? flag(a.v <= b.v) : Num{}; }
[[nodiscard]] constexpr Num gt(Num a, Num b) { return (a.ok && b.ok) ? flag(a.v > b.v) : Num{}; }
[[nodiscard]] constexpr Num ge(Num a, Num b) { return (a.ok && b.ok) ? flag(a.v >= b.v) : Num{}; }
[[nodiscard]] constexpr Num eq(Num a, Num b) { return (a.ok && b.ok) ? flag(a.v == b.v) : Num{}; }
[[nodiscard]] constexpr Num land(Num a, Num b) { return (a.ok && b.ok) ? flag(a.v != 0 && b.v != 0) : Num{}; }
[[nodiscard]] constexpr Num lor(Num a, Num b) { return (a.ok && b.ok) ? flag(a.v != 0 || b.v != 0) : Num{}; }
[[nodiscard]] constexpr Num lnot(Num a) { return a.ok ? flag(a.v == 0) : Num{}; }

// The builtin table. `fn1`/`fn2` are the unary/binary implementations; an
// n-ary builtin (`+ * min max and or`) folds pairwise from the left, and `-`
// with one argument negates.
struct Builtin {
  std::string_view name;
  int              min_args;
  int              max_args;  // -1 = unbounded
  bool             boolean;   // the result is a truth value
  std::string_view cxx;       // the function above, for the generated code
  std::string_view cxx_unary; // `-` only: the one-argument spelling
};

inline constexpr std::array<Builtin, 22> kBuiltins{{
    {"+", 2, -1, false, "add", ""},
    {"-", 1, 2, false, "sub", "neg"},
    {"*", 2, -1, false, "mul", ""},
    {"min", 2, -1, false, "min", ""},
    {"max", 2, -1, false, "max", ""},
    {"log2", 1, 1, false, "log2_floor", ""},
    {"pow2?", 1, 1, true, "pow2p", ""},
    {"popcount", 1, 1, false, "popcount", ""},
    {"mask_of", 1, 1, false, "mask_of", ""},
    {"<", 2, 2, true, "lt", ""},
    {"<=", 2, 2, true, "le", ""},
    {">", 2, 2, true, "gt", ""},
    {">=", 2, 2, true, "ge", ""},
    {"=", 2, 2, true, "eq", ""},
    {"and", 2, -1, true, "land", ""},
    {"or", 2, -1, true, "lor", ""},
    {"not", 1, 1, true, "lnot", ""},
    {"abs", 1, 1, false, "abs_", ""},
    {"pow2", 1, 1, false, "pow2", ""},
    {"clog2", 1, 1, false, "clog2", ""},
    {"band", 2, 2, false, "band", ""},
    {"div", 2, 2, false, "div", ""},
}};

[[nodiscard]] constexpr Num abs_(Num a) { return a.ok ? (a.v < 0 ? neg(a) : a) : Num{}; }

[[nodiscard]] constexpr const Builtin* find_builtin(std::string_view name) {
  for (const auto& b : kBuiltins) {
    if (b.name == name) {
      return &b;
    }
  }
  return nullptr;
}

// Interpreter entry: apply builtin `name` to `args` (the generated tests).
[[nodiscard]] constexpr Num call(std::string_view name, std::span<const Num> args) {
  const auto fold2 = [&](Num (*f)(Num, Num)) {
    Num acc = args[0];
    for (size_t i = 1; i < args.size(); ++i) {
      acc = f(acc, args[i]);
    }
    return acc;
  };
  if (name == "+") {
    return fold2(add);
  }
  if (name == "-") {
    return args.size() == 1 ? neg(args[0]) : sub(args[0], args[1]);
  }
  if (name == "*") {
    return fold2(mul);
  }
  if (name == "min") {
    return fold2(min);
  }
  if (name == "max") {
    return fold2(max);
  }
  if (name == "log2") {
    return log2_floor(args[0]);
  }
  if (name == "pow2?") {
    return pow2p(args[0]);
  }
  if (name == "popcount") {
    return popcount(args[0]);
  }
  if (name == "mask_of") {
    return mask_of(args[0]);
  }
  if (name == "<") {
    return lt(args[0], args[1]);
  }
  if (name == "<=") {
    return le(args[0], args[1]);
  }
  if (name == ">") {
    return gt(args[0], args[1]);
  }
  if (name == ">=") {
    return ge(args[0], args[1]);
  }
  if (name == "=") {
    return eq(args[0], args[1]);
  }
  if (name == "and") {
    return fold2(land);
  }
  if (name == "or") {
    return fold2(lor);
  }
  if (name == "not") {
    return lnot(args[0]);
  }
  if (name == "abs") {
    return abs_(args[0]);
  }
  if (name == "pow2") {
    return pow2(args[0]);
  }
  if (name == "clog2") {
    return clog2(args[0]);
  }
  if (name == "band") {
    return band(args[0], args[1]);
  }
  if (name == "div") {
    return div(args[0], args[1]);
  }
  return Num{};
}

}  // namespace livehd::aegraph::rt::ex
