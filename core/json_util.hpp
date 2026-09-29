// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string>
#include <string_view>

namespace livehd::json_util {

inline void escape_append(std::string& out, std::string_view text) {
  constexpr char hex[] = "0123456789abcdef";
  for (char ch : text) {
    switch (ch) {
      case '"' : out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default  : {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20) {
          out += "\\u00";
          out += hex[byte >> 4U];
          out += hex[byte & 0x0fU];
        } else {
          out += ch;
        }
      }
    }
  }
}

[[nodiscard]] inline std::string escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  escape_append(out, text);
  return out;
}

}  // namespace livehd::json_util
