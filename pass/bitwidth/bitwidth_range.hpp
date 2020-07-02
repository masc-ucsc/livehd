//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "lconst.hpp"

class __attribute__((packed)) Bitwidth_range {
protected:
  static int64_t round_power2(int64_t x);

public:
  int64_t max = 0;
  int64_t min = 0;

  bool overflow = false;

  Bitwidth_range(const Bitwidth_range &i) {
    max      = i.max;
    min      = i.min;

    overflow = i.overflow;
  };

  Bitwidth_range(const Lconst &value);
  Bitwidth_range(uint16_t bits, bool sign);
  Bitwidth_range(uint16_t bits);

  void    set_sbits(uint16_t size);
  void    set_ubits(uint16_t size);
  uint16_t get_bits() const;

  void    expand(const Bitwidth_range &i, bool round2);
	void    and_op(const Bitwidth_range &range2);

  bool    is_always_negative() const { return max <0; }
  bool    is_always_positive() const { return min >= 0; }
  bool    is_2complement() const { return min < 0; }

  void    dump() const;
};
