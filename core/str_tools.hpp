//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
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

[[nodiscard]] inline std::string to_hex(uint64_t v) {
  std::array<char, 18> str2;
  auto [ptr, ec] = std::to_chars(str2.data(), str2.data() + str2.size(), v, 16);
  (void)ec;
  std::string str(str2.data(), ptr - str2.data());

  return str;
}

[[nodiscard]] inline std::string to_s(uint64_t v) {
  std::array<char, 18> str2;
  auto [ptr, ec] = std::to_chars(str2.data(), str2.data() + str2.size(), v, 10);
  (void)ec;
  std::string str(str2.data(), ptr - str2.data());

  return str;
}

[[nodiscard]] inline int to_u64_from_hex(std::string_view str) {  // convert to integer from hexa
  if (str.empty()) {
    return 0;
  }
  int result{};
  std::from_chars(str.data(), str.data() + str.size(), result, 16);
  return result;
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

[[nodiscard]] inline std::string_view get_str_after_last(std::string_view str, const char chr) {
  auto pos = str.rfind(chr);
  if (pos == std::string_view::npos) {
    return {};
  }

  return str.substr(pos + 1);
}

[[nodiscard]] inline std::string_view get_str_after_last_if_exists(std::string_view str, const char chr) {
  auto pos = str.rfind(chr);
  if (pos == std::string_view::npos) {
    return str;
  }

  return str.substr(pos + 1);
}

[[nodiscard]] inline std::string_view get_str_after_first(std::string_view str, const char chr) {
  auto pos = str.find(chr);
  if (pos == std::string_view::npos) {
    return {};
  }

  return str.substr(pos + 1);
}

[[nodiscard]] inline std::string_view get_str_after_first_if_exists(std::string_view str, const char chr) {
  auto pos = str.find(chr);
  if (pos == std::string_view::npos) {
    return str;
  }

  return str.substr(pos + 1);
}

[[nodiscard]] inline std::string_view get_str_before_last(std::string_view str, const char chr) {
  auto pos = str.rfind(chr);
  if (pos == std::string_view::npos) {
    return str;
  }

  return str.substr(0, pos);
}

[[nodiscard]] inline std::string_view get_str_before_first(std::string_view str, const char chr) {
  auto pos = str.find(chr);
  if (pos == std::string_view::npos) {
    return str;
  }

  return str.substr(0, pos);
}

[[nodiscard]] inline std::string to_lower(std::string_view str) {
  std::string s(str);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return std::tolower(ch); });
  return s;
}

[[nodiscard]] inline bool starts_with(std::string_view str, std::string_view start) {
  if (str.size() < start.size()) {
    return false;
  }

  return str.substr(0, start.size()) == start;
}

[[nodiscard]] inline bool ends_with(std::string_view str, std::string_view end) {
  if (end.size() == str.size()) {
    return str == end;  // faster path
  }
  if (end.size() > str.size()) {
    return false;  // end is larger
  }

  const auto *base_en   = end.data();
  const auto *base_self = str.data() + str.size() - end.size();
  return memcmp(base_self, base_en, end.size()) == 0;
}

[[nodiscard]] inline size_t ends_with_count(std::string_view str1, std::string_view str2) {
  auto str1_pos = std::mismatch(str1.rbegin(), str1.rend(), str2.rbegin()).first;

  return str1_pos - str1.rbegin();
}

[[nodiscard]] inline bool contains(std::string_view str, std::string_view start) {
  return str.find(start) != std::string_view::npos;
}

[[nodiscard]] inline bool contains(std::string_view str, char start) { return str.find(start) != std::string_view::npos; }

}  // namespace str_tools
