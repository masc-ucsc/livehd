//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "lconst.hpp"

class __attribute__((packed)) Bitwidth_range {
protected:
  static Lconst  to_lconst(bool overflow, int64_t val);

public:
  int64_t max;
  int64_t min;

  bool overflow;

  Bitwidth_range() : max(0), min(0), overflow(false) {
  }

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

  void set_narrower_range(const Lconst &min_val, const Lconst &max_val);
  void set_range(const Lconst &min_val, const Lconst &max_val);

  bool   is_overflow() const { return overflow; };
  void   set_sbits_range(Bits_t size);
  void   set_ubits_range(Bits_t size);
  Bits_t get_sbits() const;
  Lconst get_max() const { return to_lconst(overflow, max); };
  Lconst get_min() const { return to_lconst(overflow, min); };
  int    get_raw_max() const { return max; };

  bool is_always_negative() const { return max < 0; }
  bool is_always_positive() const { return min >= 0; }
  bool is_2complement() const { return min < 0; }

  void dump() const;
};
