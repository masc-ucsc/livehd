// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "str_tools.hpp"

namespace livehd {
inline bool is_pyrope_keyword(std::string_view s) {
  // clang-format off: the `#include` inside the braced list makes clang-format
  // break the hand-added words one per line, which buries them.
  static const absl::flat_hash_set<std::string_view> kw = {
      // Reserved by the docs or held for future syntax, so absent from the
      // parser's table; over-quoting a non-keyword is harmless.
      "nil", "where", "priority", "defer", "async", "await", "cpp",
#define PRP_KEYWORD(name) #name,
#include "prpparse/prp_keywords.def"
  };
  // clang-format on
  return kw.contains(s);
}

inline bool is_pyrope_reserved_ident(std::string_view s) {
  // The lexer reserves these spellings case-sensitively.
  const bool placeholder = !s.empty() && s.front() == '_' && s.find_first_not_of("0123456789", 1) == std::string_view::npos;
  return placeholder || is_pyrope_keyword(s) || str_tools::is_pyrope_type_word(s);
}

inline bool should_quote_pyrope_ident(std::string_view s) {
  // The writer also quotes keyword-like and old type spellings.
  const auto lower = str_tools::ascii_fold(s);
  return is_pyrope_reserved_ident(s) || is_pyrope_keyword(lower) || lower == "clock" || lower == "reset"
         || !str_tools::renamed_type_spelling(lower).empty();
}

}  // namespace livehd
