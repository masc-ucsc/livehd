// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

namespace livehd::liberty {

// The opening quote is at open. Escaped quotes and line continuations are
// part of the payload. Leave unescaping to the caller.
inline size_t quoted_end(std::string_view text, size_t open) {
  for (size_t at = text.find_first_of("\"\\", open + 1); at != text.npos; at = text.find_first_of("\"\\", at + 2)) {
    if (text[at] == '"') {
      return at;
    }
    if (at + 1 == text.size()) {
      break;
    }
  }
  return text.npos;
}

}  // namespace livehd::liberty
