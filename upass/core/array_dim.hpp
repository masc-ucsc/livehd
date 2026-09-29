//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "hlop/dlop.hpp"

namespace upass {

// The lane count of an LNAST array type's dim node (the `[N]` const text).
//
// ONE reader for all of upass, because two rules have to hold at the same time
// and each site that reinvented it got only one of them right:
//   * a NAMED dim is NOT a size. `Dlop::from_pyrope("N")` cheerfully returns
//     the character code 78, which silently sized every unfolded `[N]` array
//     at 78 lanes -- so the text must START with a digit to be a literal.
//   * every legal Pyrope integer literal IS a size. A plain base-10 scan
//     rejects `[0x100]`, `[1K]` and `[1_024]`, all of which the grammar
//     accepts, so the parse itself goes through Dlop.
// nullopt means "not a positive integer literal"; the caller decides whether
// that is an error (tolg, where the array must lower) or simply "do not treat
// this as a lane array" (ssa's io view, the roll planner).
[[nodiscard]] inline std::optional<int64_t> array_dim_lanes(std::string_view dim_txt) {
  if (dim_txt.size() >= 2 && dim_txt.front() == '[' && dim_txt.back() == ']') {
    dim_txt = dim_txt.substr(1, dim_txt.size() - 2);
  }
  if (dim_txt.empty() || dim_txt.front() < '0' || dim_txt.front() > '9') {
    return std::nullopt;  // a name, an expression, or empty -- never a literal
  }
  // from_pyrope throws on leading-digit-but-malformed text (the grammar admits
  // `0b102` as a number token), so a bad literal must not take the pass down.
  int64_t n = 0;
  try {
    auto d = Dlop::from_pyrope(dim_txt);
    if (!d || !d->is_just_i64()) {
      return std::nullopt;
    }
    n = d->to_just_i64();
  } catch (...) {
    return std::nullopt;
  }
  return n > 0 ? std::optional<int64_t>{n} : std::nullopt;
}

}  // namespace upass
