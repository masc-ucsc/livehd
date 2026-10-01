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

std::optional<Memory_storage> parse_memory_storage(std::string_view name, char separator) {
  const auto pos = name.find(memory_instance_marker);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto hex_begin = pos + memory_instance_marker.size();
  const auto hex_end   = name.find("_e", hex_begin);
  if (hex_end == std::string_view::npos || hex_end == hex_begin || ((hex_end - hex_begin) & 1U) != 0) {
    return std::nullopt;
  }
  auto rest = name.substr(hex_end + 2);
  if (rest.size() > 1 && rest.front() == '_' && rest[1] >= '0' && rest[1] <= '9') {
    const auto digits_end = rest.find_first_not_of("0123456789", 1);
    rest                  = digits_end == std::string_view::npos ? std::string_view{} : rest.substr(digits_end);
  }
  if (rest.size() != 5 || (rest.front() != separator && rest.front() != '_') || rest.substr(1) != "data") {
    return std::nullopt;
  }

  auto nibble = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
      return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
      return ch - 'A' + 10;
    }
    return -1;
  };
  Memory_storage out{name.substr(0, pos), {}};
  out.source.reserve((hex_end - hex_begin) / 2);
  for (auto i = hex_begin; i < hex_end; i += 2) {
    const int hi = nibble(name[i]);
    const int lo = nibble(name[i + 1]);
    if (hi < 0 || lo < 0) {
      return std::nullopt;
    }
    out.source.push_back(static_cast<char>((hi << 4) | lo));
  }
  return out;
}

}  // namespace livehd::bus_name
