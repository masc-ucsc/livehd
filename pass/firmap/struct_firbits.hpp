//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <iostream>
#include <cstdint>

#include <format>
#include "lconst.hpp"

class __attribute__((packed)) Firrtl_bits {
protected:
  Bits_t bits;
  bool   sign;

public:
  Firrtl_bits() : bits(0), sign(false) {}

  Firrtl_bits(const Firrtl_bits &i) {
    I(i.bits >= 0);
    bits = i.bits;
    sign = i.sign;
  };

  Firrtl_bits(const Bits_t _bits, const bool _sign) {
    // I(bits >= 0); //FIXME: ???
    bits = _bits;
    sign = _sign;
  };

  Firrtl_bits(const Bits_t _bits) {
    // I(bits >= 0); //FIXME: ???
    bits = _bits;
    sign = false;
  };

  constexpr Firrtl_bits &operator=(const Firrtl_bits &r) {
    bits = r.bits;
    sign = r.sign;
    return *this;
  }

  void set_bits(Bits_t _bits) { bits = _bits; }

  Bits_t get_bits() const { return bits; };

  void set_sign(bool _sign) { sign = _sign; };

  void set_bits_sign(Bits_t _bits, bool _sign) {
    bits = _bits;
    sign = _sign;
  }

  bool get_sign() const { return sign; };

  void dump() const { std::print("{}{}b\n", sign ? "S" : "U", bits); }
};
