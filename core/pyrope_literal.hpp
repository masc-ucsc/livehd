// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace livehd {

// Parse spelling independently of the consumer's integer representation and
// its policy for unknown bits. Signed binary, octal and hexadecimal literals
// are two's-complement bit patterns.
struct Pyrope_integer_spelling {
  std::string digits;
  unsigned    radix          = 10;
  unsigned    scale_bits     = 0;  // K/M/G/T on decimal magnitudes
  bool        negate         = false;
  bool        signed_pattern = false;

  [[nodiscard]] unsigned bits_per_digit() const { return radix == 2 ? 1 : radix == 8 ? 3 : 4; }
  [[nodiscard]] bool     unknown() const { return digits.find('?') != std::string::npos; }
  [[nodiscard]] bool     negative_pattern() const {
    return signed_pattern
           && (radix == 2 ? digits.front() == '1' : digits.front() >= (radix == 8 ? '4' : '8') && digits.front() != '?');
  }
};

[[nodiscard]] inline std::optional<Pyrope_integer_spelling> parse_pyrope_integer_spelling(std::string_view text) {
  Pyrope_integer_spelling out;
  if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
    out.negate = text.front() == '-';
    text.remove_prefix(1);
  }
  if (text.size() > 1 && text[0] == '0') {
    const bool   sign = text[1] == 's' || text[1] == 'u';
    const size_t pos  = sign ? 2 : 1;
    const char   base = pos < text.size() ? text[pos] : '\0';
    switch (base) {
      case 'b':
      case 'B':
        if (!sign) {
          return std::nullopt;
        }
        out.radix = 2;
        break;
      case 'o':
      case 'O': out.radix = 8; break;
      case 'd':
      case 'D': out.radix = 10; break;
      case 'x':
      case 'X': out.radix = 16; break;
      default:
        if (sign) {
          return std::nullopt;
        }
    }
    if (out.radix != 10 || base == 'd' || base == 'D') {
      out.signed_pattern = sign && text[1] == 's' && out.radix != 10;
      text.remove_prefix(pos + 1);
    }
  }
  if (out.radix == 10 && !text.empty()) {
    switch (text.back()) {
      case 'K':
      case 'k': out.scale_bits = 10; break;
      case 'M':
      case 'm': out.scale_bits = 20; break;
      case 'G':
      case 'g': out.scale_bits = 30; break;
      case 'T':
      case 't': out.scale_bits = 40; break;
    }
    if (out.scale_bits) {
      text.remove_suffix(1);
    }
  }
  out.digits.reserve(text.size());
  for (char c : text) {
    if (c == '_') {
      continue;
    }
    if (c >= 'A' && c <= 'Z') {
      c += 'a' - 'A';
    }
    if (out.radix != 10 && (c == '?' || c == 'x' || c == 'z')) {
      out.digits.push_back('?');
      continue;
    }
    const auto digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (digit < 0 || static_cast<unsigned>(digit) >= out.radix) {
      return std::nullopt;
    }
    out.digits.push_back(c);
  }
  if (out.digits.empty()) {
    return std::nullopt;
  }
  return out;
}

}  // namespace livehd
