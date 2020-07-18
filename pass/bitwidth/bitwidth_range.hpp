//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "lconst.hpp"

class __attribute__((packed)) Bitwidth_range {
protected:
  static int64_t round_power2(int64_t x);
  static Lconst  to_lconst(bool overflow, int64_t val);

public:
  int64_t max = 0;
  int64_t min = 0;

  bool overflow = false;

  Bitwidth_range(const Bitwidth_range &i) {
    max = i.max;
    min = i.min;

    overflow = i.overflow;
  };

  constexpr Bitwidth_range &operator=(const Bitwidth_range &r) {
    max      = r.max;
    min      = r.min;
    overflow = r.overflow;

    return *this;
  }

  Bitwidth_range(const Lconst &value);
  Bitwidth_range(const Lconst &min_val, const Lconst &max_val);
  Bitwidth_range(Bits_t bits, bool sign);
  Bitwidth_range(Bits_t bits);

  bool   is_overflow() const { return overflow; };
  void   set_sbits(Bits_t size);
  void   set_ubits(Bits_t size);
  Bits_t get_bits() const;
  Lconst get_max() const { return to_lconst(overflow, max); };
  Lconst get_min() const { return to_lconst(overflow, min); };
  int    get_raw_max() const { return max; };

  bool is_always_negative() const { return max < 0; }
  bool is_always_positive() const { return min >= 0; }
  bool is_2complement() const { return min < 0; }

  void dump() const;
};
