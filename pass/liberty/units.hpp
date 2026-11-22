// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <charconv>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace livehd::liberty {

inline std::optional<double> scaled_unit(double value, std::string_view suffix,
                                         std::span<const std::pair<std::string_view, double>> scales) {
  for (const auto& [unit, scale] : scales) {
    if (unit == suffix && value > 0 && std::isfinite(value * scale)) {
      return value * scale;
    }
  }
  return std::nullopt;
}

inline std::optional<double> time_unit_ps(std::string_view text) {
  constexpr std::string_view whitespace = " \t\r\n";
  const auto                 first      = text.find_first_not_of(whitespace);
  if (first == text.npos) {
    return std::nullopt;
  }
  text                    = text.substr(first, text.find_last_not_of(whitespace) - first + 1);
  double value            = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{}) {
    return std::nullopt;
  }
  auto       suffix = text.substr(end - text.data());
  const auto unit   = suffix.find_first_not_of(whitespace);
  if (unit == suffix.npos) {
    return std::nullopt;
  }
  static constexpr std::pair<std::string_view, double> scales[] = {
      {"fs", .001},
      {"ps",    1},
      {"ns", 1000},
      {"us",  1e6},
      {"ms",  1e9},
      { "s", 1e12}
  };
  return scaled_unit(value, suffix.substr(unit), scales);
}

inline std::optional<double> capacitance_unit_ff(double value, std::string_view unit) {
  static constexpr std::pair<std::string_view, double> scales[] = {
      {"af", .001},
      {"ff",    1},
      {"pf", 1000},
      {"nf",  1e6},
      {"uf",  1e9},
      { "f", 1e15}
  };
  return scaled_unit(value, unit, scales);
}

}  // namespace livehd::liberty
