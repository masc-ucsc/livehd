//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// Central whitelist of built-in callee names.
//
// A Pyrope call `foo(...)` whose callee is NOT a user-defined `comb`/`mod`/
// `pipe` (nor a function-valued variable / parameter) must be one of these
// compiler-provided built-ins. The undefined-call check (prp2lnast
// check_undefined_reads) consults this table so a call to a real built-in is
// never reported as "undefined", while a typo'd / never-defined callee is.
//
// This is the single source of truth: previously the names were recognized
// ad-hoc at scattered sites (assert family in prp2lnast, casts in
// is_prim_type_token, the `built-in cast` note in upass.tolg). Add new
// built-ins HERE.
// ─────────────────────────────────────────────────────────────────────────────
namespace prp_builtins {

// Plain built-in functions (verification, debug output, compilation, timing).
inline bool is_builtin_function(std::string_view name) {
  // Linear scan over a tiny array — faster than a hash at this size and keeps
  // the header dependency-free.
  // `__`-prefixed names are compiler cell intrinsics (the @graph/cell.hpp cell
  // ops: __sum, __mux, __hotmux, __memory, __and, …) emitted by the cellmap /
  // lowering libraries — always callable.
  if (name.size() >= 2 && name[0] == '_' && name[1] == '_') {
    return true;
  }
  static constexpr std::string_view names[] = {
      // verification (requires/ensures = lambda pre/postconditions -> assume/assert)
      "assert", "cassert", "assume", "assert_always", "requires", "ensures",
      // debug / string output
      "cputs", "puts", "print", "format",
      // compilation / directives
      "import", "optimize",
      // overflow policies (also usable as `wrap x = …` statements)
      "wrap", "sat", "saturate",
      // concurrency / timing
      "spawn", "defer",
      // testbench string-path probes (sim-only; handled by prp_sim, not tolg)
      "peek", "poke",
  };
  for (const auto n : names) {
    if (n == name) {
      return true;
    }
  }
  return false;
}

// True iff `name` is a REMOVED scalar cast/type keyword — `int`/`integer`/
// `uint`. These were dropped in favor of `signed`/`unsigned` (explicit sign
// intent); a use must report a tailored "removed" diagnostic, not be treated as
// a valid built-in. NOT a member of is_type_cast_callee below.
inline bool is_removed_int_keyword(std::string_view t) { return t == "int" || t == "integer" || t == "uint"; }

// Built-in scalar type constructors usable as casts: `unsigned`/`signed`/
// `bool`/`boolean`/`string` and the sized forms `uN`/`sN`/`iN`. (`int`/`uint`/
// `integer` were REMOVED — see is_removed_int_keyword.) Mirrors
// Prp2lnast::is_prim_type_token, kept here so the whitelist is whole.
inline bool is_type_cast_callee(std::string_view t) {
  if (t == "unsigned" || t == "signed" || t == "bool" || t == "boolean" || t == "string") {
    return true;
  }
  return t.size() >= 2 && (t[0] == 'u' || t[0] == 's' || t[0] == 'i')
         && std::all_of(t.begin() + 1, t.end(), [](unsigned char ch) { return std::isdigit(ch); });
}

// Temporal-library builtin `past[N](x)` — `x` delayed by N clock cycles (an
// N-deep flop shift register). The grammar parses `past[2](a)` as a call whose
// callee text is the whole `past[2]` member-selection, so the `[N]` rides along
// in the callee name. Returns the delay N when `name` matches `past[<N>]` with N
// a positive decimal literal, else 0 ("not a past builtin"). prp2lnast rewrites
// such a call into an equivalent `stage[N]` shift register; recognizing it here
// also keeps the undefined-call check from flagging the synthetic callee.
inline uint64_t past_builtin_delay(std::string_view name) {
  constexpr std::string_view prefix = "past[";
  if (name.size() < prefix.size() + 2 || name.substr(0, prefix.size()) != prefix || name.back() != ']') {
    return 0;
  }
  const std::string_view digits = name.substr(prefix.size(), name.size() - prefix.size() - 1);
  if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    return 0;
  }
  uint64_t n = 0;
  for (const char c : digits) {
    n = n * 10 + static_cast<uint64_t>(c - '0');
    if (n > 4096) {  // absurd depth guard — a shift register this deep is a mistake
      return 0;
    }
  }
  return n;  // N==0 (`past[0]`) is not a delay: reported as an undefined call
}

// True iff `name` is any compiler-provided callee (function, type cast, or the
// `past[N]` temporal builtin).
inline bool is_builtin_callee(std::string_view name) {
  return is_builtin_function(name) || is_type_cast_callee(name) || past_builtin_delay(name) != 0;
}

}  // namespace prp_builtins
