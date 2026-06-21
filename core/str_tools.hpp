//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace str_tools {

[[nodiscard]] inline int to_i(std::string_view str) {  // convert to integer
  if (str.empty() || !std::isdigit(str.front())) {
    return 0;
  }
  int result{};
  std::from_chars(str.data(), str.data() + str.size(), result);
  return result;
}

[[nodiscard]] inline std::string to_s(uint64_t v) {
  std::array<char, 18> str2;
  auto [ptr, ec] = std::to_chars(str2.data(), str2.data() + str2.size(), v, 10);
  (void)ec;
  std::string str(str2.data(), ptr - str2.data());

  return str;
}

[[nodiscard]] inline bool is_string(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  auto ch = str.front();
  if (std::isdigit(ch) || ch == '-') {
    return false;
  }

  return true;
}

[[nodiscard]] inline bool is_i(std::string_view str) {
  if (str.size() == 0 || !(std::isdigit(str.front()) || str.front() == '-')) {
    return false;
  }

  int result{};
  auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
  (void)p;
  if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range) {
    return false;
  }

  return true;
}

[[nodiscard]] inline std::string_view get_str_after_last_if_exists(std::string_view str, const char chr) {
  auto pos = str.rfind(chr);
  if (pos == std::string_view::npos) {
    return str;
  }

  return str.substr(pos + 1);
}

[[nodiscard]] inline std::string_view get_str_before_first(std::string_view str, const char chr) {
  auto pos = str.find(chr);
  if (pos == std::string_view::npos) {
    return str;
  }

  return str.substr(0, pos);
}

[[nodiscard]] inline bool ends_with(std::string_view str, std::string_view end) {
  if (end.size() == str.size()) {
    return str == end;  // faster path
  }
  if (end.size() > str.size()) {
    return false;  // end is larger
  }

  const auto* base_en   = end.data();
  const auto* base_self = str.data() + str.size() - end.size();
  return memcmp(base_self, base_en, end.size()) == 0;
}

// ---------------------------------------------------------------------------
// ASCII case-insensitive name matching (LiveHD/Pyrope names are matched
// case-insensitively while the original spelling is preserved for emission).
// Only 'A'..'Z' fold; digits, '_', '0sb?', backticks, and '.' are untouched.
// ---------------------------------------------------------------------------

[[nodiscard]] inline char ascii_tolower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] inline bool ci_equal(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (ascii_tolower(a[i]) != ascii_tolower(b[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool ci_less(std::string_view a, std::string_view b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    const auto ca = static_cast<unsigned char>(ascii_tolower(a[i]));
    const auto cb = static_cast<unsigned char>(ascii_tolower(b[i]));
    if (ca != cb) {
      return ca < cb;
    }
  }
  return a.size() < b.size();
}

[[nodiscard]] inline bool ci_starts_with(std::string_view str, std::string_view prefix) {
  return prefix.size() <= str.size() && ci_equal(str.substr(0, prefix.size()), prefix);
}

[[nodiscard]] inline bool ci_ends_with(std::string_view str, std::string_view suffix) {
  return suffix.size() <= str.size() && ci_equal(str.substr(str.size() - suffix.size()), suffix);
}

// FNV-1a over ASCII-lowercased bytes: case-insensitive and allocation-free.
[[nodiscard]] inline size_t ci_hash(std::string_view s) {
  uint64_t h = 1469598103934665603ull;
  for (char c : s) {
    h ^= static_cast<unsigned char>(ascii_tolower(c));
    h *= 1099511628211ull;
  }
  return static_cast<size_t>(h);
}

// Transparent functors for case-insensitive std::string-keyed hash and ordered
// containers (string_view lookups work without allocating an std::string).
struct Ci_hash {
  using is_transparent = void;
  [[nodiscard]] size_t operator()(std::string_view s) const { return ci_hash(s); }
};
struct Ci_eq {
  using is_transparent = void;
  [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const { return ci_equal(a, b); }
};
struct Ci_less {
  using is_transparent = void;
  [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const { return ci_less(a, b); }
};

}  // namespace str_tools
