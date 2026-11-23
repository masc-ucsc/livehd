// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <limits>
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "pyrope_literal.hpp"

namespace prp_tb {
inline std::optional<llvm::APInt> integer_value(const livehd::Pyrope_integer_spelling& literal) {
  const auto bpd = literal.bits_per_digit();
  if (literal.unknown() || literal.digits.size() > (std::numeric_limits<unsigned>::max() - 1 - literal.scale_bits) / bpd) {
    return std::nullopt;
  }
  const unsigned bits = static_cast<unsigned>(literal.digits.size()) * bpd;
  llvm::APInt    value(bits + 1 + literal.scale_bits, llvm::StringRef(literal.digits), static_cast<uint8_t>(literal.radix));
  if (literal.negative_pattern()) {
    value = value.trunc(bits).sext(bits + 1);
  }
  value <<= literal.scale_bits;
  return literal.negate ? -value : value;
}
}  // namespace prp_tb
