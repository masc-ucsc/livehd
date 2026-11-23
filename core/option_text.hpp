// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace str_tools {

// Strict, case-sensitive CLI option vocabulary.
[[nodiscard]] constexpr std::optional<bool> parse_bool(std::string_view text) {
  if (text == "true" || text == "1" || text == "on") {
    return true;
  }
  if (text == "false" || text == "0" || text == "off") {
    return false;
  }
  return std::nullopt;
}

// Formal name lists ignore empty entries and preserve order, duplicates and whitespace.
inline void append_csv_names(std::vector<std::string>& names, std::string_view text) {
  while (!text.empty()) {
    const auto comma = text.find(',');
    const auto name  = text.substr(0, comma);
    if (!name.empty()) {
      names.emplace_back(name);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    text.remove_prefix(comma + 1);
  }
}

}  // namespace str_tools
