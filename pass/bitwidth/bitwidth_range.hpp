//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "const.hpp"
#include "graph_sizing.hpp"

class __attribute__((packed)) Bitwidth_range {
protected:
  static Const to_lconst(bool overflow, int64_t val);

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

  Bitwidth_range(const Bitwidth_range& i) {
    max = i.max;
    min = i.min;

    overflow = i.overflow;
  };

  constexpr Bitwidth_range& operator=(const Bitwidth_range& r) {
    max      = r.max;
    min      = r.min;
    overflow = r.overflow;

    return *this;
  }

  Bitwidth_range(const Const& value);
  Bitwidth_range(const Const& min_val, const Const& max_val);
  // Convenience overloads accepting transient spool_ptr<Dlop> op results.
  Bitwidth_range(const spool_ptr<Dlop>& value) : Bitwidth_range(*value) {}
  Bitwidth_range(const spool_ptr<Dlop>& min_val, const spool_ptr<Dlop>& max_val) : Bitwidth_range(*min_val, *max_val) {}
  Bitwidth_range(const Const& min_val, const spool_ptr<Dlop>& max_val) : Bitwidth_range(min_val, *max_val) {}
  Bitwidth_range(const spool_ptr<Dlop>& min_val, const Const& max_val) : Bitwidth_range(*min_val, max_val) {}

  Bitwidth_range(const int64_t min_val, const int64_t max_val) {
    I(min_val <= max_val);
    min      = min_val;
    max      = max_val;
    overflow = false;
  }

  void set_narrower_range(const Bitwidth_range& bw);
  void set_narrower_range(const Const& min_val, const Const& max_val) { set_narrower_range(Bitwidth_range(min_val, max_val)); }
  void set_narrower_range(const spool_ptr<Dlop>& min_val, const spool_ptr<Dlop>& max_val) {
    set_narrower_range(*min_val, *max_val);
  }
  void set_narrower_range(const Const& min_val, const spool_ptr<Dlop>& max_val) { set_narrower_range(min_val, *max_val); }
  void set_narrower_range(const spool_ptr<Dlop>& min_val, const Const& max_val) { set_narrower_range(*min_val, max_val); }

  void set_wider_range(const Bitwidth_range& bw);
  void set_wider_range(const Const& min_val, const Const& max_val) { set_wider_range(Bitwidth_range(min_val, max_val)); }
  void set_wider_range(const spool_ptr<Dlop>& min_val, const spool_ptr<Dlop>& max_val) { set_wider_range(*min_val, *max_val); }
  void set_wider_range(const Const& min_val, const spool_ptr<Dlop>& max_val) { set_wider_range(min_val, *max_val); }
  void set_wider_range(const spool_ptr<Dlop>& min_val, const Const& max_val) { set_wider_range(*min_val, max_val); }

  void set_range(const Const& min_val, const Const& max_val);
  void set_range(const spool_ptr<Dlop>& min_val, const Const& max_val) { set_range(*min_val, max_val); }
  void set_range(const Const& min_val, const spool_ptr<Dlop>& max_val) { set_range(min_val, *max_val); }
  void set_range(const spool_ptr<Dlop>& min_val, const spool_ptr<Dlop>& max_val) { set_range(*min_val, *max_val); }
  void set_range(int64_t min_val, int64_t max_val) { set_range(*Dlop::create_integer(min_val), *Dlop::create_integer(max_val)); }

  bool   is_overflow() const { return overflow; };
  void   set_sbits_range(Bits_t size);
  void   set_ubits_range(Bits_t size);
  Bits_t get_sbits() const;
  Const  get_range() const {
    if (overflow) {
      return *Dlop::create_integer(1)->shl_op(*Dlop::create_integer(get_sbits()));
    }
    return *Dlop::create_integer(max - min + 1);
  }
  Const get_max() const { return to_lconst(overflow, max); };
  Const get_min() const { return to_lconst(overflow, min); };
  int   get_raw_max() const { return max; };

  bool is_always_negative() const { return max < 0; }
  bool is_always_positive() const { return min >= 0; }
  bool is_2complement() const { return min < 0; }

  void dump() const;
};
