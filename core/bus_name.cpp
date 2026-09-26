//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bus_name.hpp"

#include <format>

namespace livehd::bus_name {

std::string piece(std::string_view base, int64_t index) { return std::format("{}[{}]", base, index); }

std::optional<Piece> parse_bus_piece(std::string_view name, bool allow_model_suffix, char separator) {
  std::string_view head = name;
  std::string_view suffix;
  if (!head.empty() && head.back() != ']') {
    if (!allow_model_suffix) {
      return std::nullopt;
    }
    // The model segment starts right after the LAST `]`.
    const auto close = head.rfind(']');
    if (close == std::string_view::npos || close + 1 >= head.size() || head[close + 1] != separator) {
      return std::nullopt;
    }
    const auto dot = close + 1;
    suffix         = head.substr(dot + 1);
    if (suffix.empty() || suffix.find_first_of(".[]") != std::string_view::npos) {
      return std::nullopt;
    }
    head = head.substr(0, dot);
  }
  if (head.size() < 4 || head.back() != ']') {  // shortest legal form: `x[0]`
    return std::nullopt;
  }
  const auto open = head.rfind('[');
  if (open == std::string_view::npos || open == 0) {
    return std::nullopt;
  }
  const auto digits = head.substr(open + 1, head.size() - open - 2);
  // Decimal, no sign, no leading zero (so every index has exactly one
  // spelling), bounded so a pathological tail cannot overflow.
  if (digits.empty() || digits.size() > 9 || (digits.size() > 1 && digits.front() == '0')) {
    return std::nullopt;
  }
  int64_t index = 0;
  for (char ch : digits) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    index = index * 10 + (ch - '0');
  }
  return Piece{head.substr(0, open), index, suffix};
}

std::optional<std::string_view> cell_state_owner(std::string_view name) {
  const auto dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= name.size()
      || name.find_first_of("[]", dot + 1) != std::string_view::npos) {
    return std::nullopt;
  }
  auto owner = name.substr(0, dot);
  // cgen's collision uniquifier (Cgen_verilog::get_unique_decl_name).
  constexpr std::string_view uniq = "_cgen";
  if (const auto u = owner.rfind(uniq); u != std::string_view::npos && u > 0 && u + uniq.size() < owner.size()) {
    const auto digits = owner.substr(u + uniq.size());
    if (digits.find_first_not_of("0123456789") == std::string_view::npos && owner[u - 1] != '.') {
      owner = owner.substr(0, u);
    }
  }
  return owner;
}

}  // namespace livehd::bus_name
