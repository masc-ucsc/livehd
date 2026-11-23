// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "kind.hpp"
#include "hlop/dlop.hpp"

namespace upass {
// A one-bit wildcard is compatible with both Bool and integer; its Dlop alone
// cannot distinguish that from an integer with unknown bits.
inline Kind literal_kind(std::string_view text, const Dlop& value) {
  if (text == "nil") {
    return Kind::nil;
  }
  if (text == "true" || text == "false") {
    return Kind::boolean;
  }
  if (text == "0sb?" || text == "0ub?") {
    return Kind::unknown;
  }
  if (value.is_string() || text.starts_with('"')) {
    return Kind::string;
  }
  return value.is_integer() ? Kind::integer : Kind::unknown;
}
inline Kind literal_kind(std::string_view text) {
  try {
    return literal_kind(text, Dlop::from_pyrope_cached(text));
  } catch (...) {
    // Non-value LNAST const children (e.g. selector names) carry text.
    return Kind::string;
  }
}
}  // namespace upass
