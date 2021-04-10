//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "lconst.hpp"

class __attribute__((packed)) Bitwidth_range {
protected:
  static Lconst to_lconst(bool overflow, int64_t val);

  // TODO:
  //  It may be interesting to explore to have unknowns, known zero, know ones
  //  for inside the valid range (when not overflow). Some code like:
  //  https://dougallj.wordpress.com/2020/01/13/bit-twiddling-addition-with-unknown-bits/
  //
  // struct known_bits {
  //   unsigned ones;
  //   unsigned unknowns;
  // };
  //
  // struct known_bits kb_add(struct known_bits a, struct known_bits b) {
  //  struct known_bits result;
  //  unsigned x = a.ones + b.ones;
  //  result.unknowns = a.unknowns | b.unknowns | (x ^ (x + a.unknowns + b.unknowns));
  //  result.ones = x & ~result.unknowns;
  //  return result;
  // }
  //
  //  This could help to find simpler code.
  //
  //  E.g: if the bit 1 is guarantee to be zero:
  //
  //  x = get_mask(x,-3); // 0b111...101
  //
  //  If guaranteed to be one:
  //
  //  x = or(get_mask(x,-3), 0x2)
  //
  //  NOTE: IF lower bits are dropped, the result has less bits (get_max(x,-2)
  //
  //  When translating to mockturtle those bits should be set to zero/one for speed and better optimization
  //
  //  When generating Verilog/Pyrope we could
  //  {x[33:2], 1'b0, x[0]} // verilog

public:
  int64_t max;
  int64_t min;

  bool overflow;

  Bitwidth_range() : max(0), min(0), overflow(false) {}

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
