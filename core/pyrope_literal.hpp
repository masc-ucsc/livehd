// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace livehd {

// Parse spelling independently of the consumer's integer representation and
// its policy for unknown bits. Unprefixed and plain hex/binary values are
// magnitudes; 0sb and 0sx are two's-complement bit patterns.
struct Pyrope_integer_spelling {
  std::string digits;
  unsigned    radix          = 10;
  bool        negate         = false;
  bool        signed_pattern = false;

  [[nodiscard]] unsigned bits_per_digit() const { return radix == 2 ? 1 : 4; }
  [[nodiscard]] bool     unknown() const { return digits.find('?') != std::string::npos; }
  [[nodiscard]] bool     negative_pattern() const {
    return signed_pattern && (radix == 2 ? digits.front() == '1' : digits.front() >= '8' && digits.front() != '?');
  }
};

[[nodiscard]] inline std::optional<Pyrope_integer_spelling> parse_pyrope_integer_spelling(std::string_view text) {
  Pyrope_integer_spelling out;
  if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
    out.negate = text.front() == '-';
    text.remove_prefix(1);
  }
  if (text.size() >= 3 && text.front() == '0' && (text[1] == 'u' || text[1] == 's')
      && (text[2] == 'b' || text[2] == 'B' || text[2] == 'x' || text[2] == 'X')) {
    out.signed_pattern = text[1] == 's';
    out.radix          = text[2] == 'b' || text[2] == 'B' ? 2 : 16;
    text.remove_prefix(3);
  } else if (text.starts_with("0b") || text.starts_with("0B") || text.starts_with("0x") || text.starts_with("0X")) {
    out.radix = text[1] == 'b' || text[1] == 'B' ? 2 : 16;
    text.remove_prefix(2);
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
