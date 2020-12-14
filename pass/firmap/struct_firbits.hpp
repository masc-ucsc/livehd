//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "fmt/format.h"
#include "lconst.hpp"

class __attribute__((packed)) Firrtl_bits {
protected:
  Bits_t bits;
  bool   signedness;

public:
  Firrtl_bits() : bits(0), signedness(false) {}

  Firrtl_bits(const Firrtl_bits &i) {
    bits = i.bits;
    signedness = i.signedness;
  };

  Firrtl_bits(const Bits_t _bits, const bool _signedness) {
    bits = _bits;
    signedness = _signedness;
  };

  Firrtl_bits(const Bits_t _bits) {
    bits = _bits;
    signedness = false;
  };

  constexpr Firrtl_bits &operator=(const Firrtl_bits &r) {
    bits       = r.bits;
    signedness = r.signedness;
    return *this;
  }


  void set_bits(Bits_t _bits) { 
    bits = _bits;
  }
  
  Bits_t get_bits() const { 
    return bits; 
  };

  void set_signedness(bool _signedness) {
    signedness = _signedness;
  };

  void set_bits_signedness(Bits_t _bits, bool _signedness) {
    bits = _bits;
    signedness = _signedness;
  }

  bool get_signedness() const {
    return signedness;
  };

  void dump() const {
    fmt::print("{}{}b\n", signedness ? "S" : "U", bits);
  }
};
