//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "hlop/dlop.hpp"
#include "lnast.hpp"

namespace upass {

// The integer value of a Pyrope integer literal (`8`, `-8`, `0x100`, `1_024`),
// nullopt for anything else. A NAMED value is never a literal:
// `Dlop::from_pyrope("N")` cheerfully returns the character code 78, so the
// text must START with a digit (after an optional `-`).
[[nodiscard]] inline std::optional<int64_t> int_literal(std::string_view txt) {
  const auto digits = txt.starts_with('-') ? txt.substr(1) : txt;
  if (digits.empty() || digits.front() < '0' || digits.front() > '9' || digits.find("..") != std::string_view::npos) {
    return std::nullopt;  // a name, an expression, an index range or empty -- never a literal
  }
  // from_pyrope throws on leading-digit-but-malformed text (the grammar admits
  // `0b102` as a number token), so a bad literal must not take the pass down.
  try {
    auto d = Dlop::from_pyrope(txt);
    if (!d || !d->is_just_i64()) {
      return std::nullopt;
    }
    return d->to_just_i64();
  } catch (...) {
    return std::nullopt;
  }
}

// The lane count of an LNAST array type's dim node (the `[N]` const text).
//
// ONE reader for all of upass, because two rules have to hold at the same time
// and each site that reinvented it got only one of them right:
//   * a NAMED dim is NOT a size (int_literal above).
//   * every legal Pyrope integer literal IS a size. A plain base-10 scan
//     rejects `[0x100]`, `[1K]` and `[1_024]`, all of which the grammar
//     accepts, so the parse itself goes through Dlop.
// nullopt means "not a positive integer literal"; the caller decides whether
// that is an error (tolg, where the array must lower) or simply "do not treat
// this as a lane array" (ssa's io view, the roll planner). An index-range or
// enum dimension (array_dim_range below) is NOT a lane count here: the runner
// lowers it to its extent before anything downstream reads the declaration.
[[nodiscard]] inline std::optional<int64_t> array_dim_lanes(std::string_view dim_txt) {
  if (dim_txt.size() >= 2 && dim_txt.front() == '[' && dim_txt.back() == ']') {
    dim_txt = dim_txt.substr(1, dim_txt.size() - 2);
  }
  if (dim_txt.starts_with('-')) {
    return std::nullopt;
  }
  const auto n = int_literal(dim_txt);
  return n && *n > 0 ? n : std::nullopt;
}

// An index-range dimension (08-memories.md "Array index"): `[100..<132]` has
// 32 entries whose first index is 100, `[-8..<7]` takes signed indices,
// `[lo..=hi]` includes `hi` and `[lo..+n]` has `n` entries. `fold` turns one
// bound's text into its value (int_literal, or a comptime-name fold); nullopt
// when the text is not a range, a bound does not fold, or the range is empty.
struct Array_dim_range {
  int64_t lanes = 0;  // entry count
  int64_t lo    = 0;  // index of the first entry
};
template <typename Fold>
[[nodiscard]] std::optional<Array_dim_range> array_dim_range(std::string_view dim_txt, const Fold& fold) {
  if (dim_txt.size() >= 2 && dim_txt.front() == '[' && dim_txt.back() == ']') {
    dim_txt = dim_txt.substr(1, dim_txt.size() - 2);
  }
  const auto op = dim_txt.find("..");
  if (op == std::string_view::npos || op + 2 >= dim_txt.size()) {
    return std::nullopt;
  }
  const char kind = dim_txt[op + 2];
  if (kind != '<' && kind != '=' && kind != '+') {
    return std::nullopt;
  }
  const auto trim = [](std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
      s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
      s.remove_suffix(1);
    }
    return s;
  };
  const std::optional<int64_t> lo = fold(trim(dim_txt.substr(0, op)));
  const std::optional<int64_t> hi = fold(trim(dim_txt.substr(op + 3)));
  if (!lo || !hi) {
    return std::nullopt;
  }
  int64_t lanes = *hi;
  if (kind != '+' && (__builtin_sub_overflow(*hi, *lo, &lanes) || (kind == '=' && __builtin_add_overflow(lanes, 1, &lanes)))) {
    return std::nullopt;
  }
  if (lanes <= 0) {
    return std::nullopt;
  }
  return Array_dim_range{.lanes = lanes, .lo = *lo};
}

// The runtime index of an index-range / enum array rebased to its zero-based
// lane (`i - lo`) is a runner temp named with this prefix; bitwidth words its
// negative-index error for the declared range then.
inline constexpr std::string_view kRebasedIndexPrefix{"%aidx"};

// The dimension node of one `comp_type_array` level: its second child, or the
// lone `[N]` const of an element-less `mut v:[N] = …` (whose level has no
// element type child at all). Invalid when the level has neither.
[[nodiscard]] inline Lnast_nid array_level_dim(const Lnast& ln, const Lnast_nid& level) {
  const auto first = ln.get_first_child(level);
  if (first.is_invalid()) {
    return first;
  }
  if (const auto second = ln.get_sibling_next(first); !second.is_invalid()) {
    return second;
  }
  const bool lone_dim = Lnast_ntype::is_const(ln.get_type(first)) && ln.get_name(first).starts_with('[');
  return lone_dim ? first : Lnast_nid{};
}

}  // namespace upass
