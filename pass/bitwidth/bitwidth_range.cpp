//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bitwidth_range.hpp"

#include <format>
#include <iostream>

#include "iassert.hpp"
#include "likely.hpp"

Lconst Bitwidth_range::to_lconst(bool overflow, int64_t val) {
  if (!overflow) {
    return Lconst(val);
  }
  if (val == 0) {
    return Lconst(0);
  }

  if (val > 0) {
    return Lconst::get_mask_value(val);
  }

  return Lconst::get_neg_mask_value(-(val + 1));  // Lconst(0) - (Lconst(1).lsh_op(-val));
}

Bitwidth_range::Bitwidth_range(const Lconst &val) {
  if (val.is_i()) {
    overflow = false;
    max      = val.to_i();
    min      = val.to_i();
  } else {
    // val.dump();
    overflow  = true;
    auto bits = val.get_bits();

    if (val.is_negative()) {
      max = 0;
      min = -bits;
    } else {
      max = bits;
      min = 0;
    }
    I(min == 0 || min <= max || max == 0);
  }
}

void Bitwidth_range::set_range(const Lconst &min_val, const Lconst &max_val) {
  I(max_val >= min_val);

  if (max_val.is_i() && min_val.is_i()) {
    overflow = false;
    max      = max_val.to_i();
    min      = min_val.to_i();
    I(max >= min);
  } else {
    overflow = true;
    min      = 0;
    max      = 0;
    if (min_val.is_negative()) {
      min = -(min_val.get_bits());
    }
    if (max_val.is_positive()) {
      max = max_val.get_bits();
    }

    // std::print("min:{} max:{} min_val:{} max_val:{}\n", (int)min, (int)max, min_val.to_pyrope(), max_val.to_pyrope());
    I(min == 0 || min <= max || max == 0);
  }
}

Bitwidth_range::Bitwidth_range(const Lconst &min_val, const Lconst &max_val) { set_range(min_val, max_val); }

void Bitwidth_range::set_narrower_range(const Bitwidth_range &bw) {
  if (likely(!bw.is_overflow() && !is_overflow())) {
    max = std::min(max, bw.max);
    min = std::max(min, bw.min);
    I(max >= min);
    return;
  }

  auto l_max = get_max();
  auto n_max = bw.get_max();
  if (n_max < l_max) {
    l_max = n_max;
  }

  auto l_min = get_min();
  auto n_min = bw.get_min();
  if (n_min > l_min) {
    l_min = n_min;
  }

  set_range(l_min, l_max);
}

void Bitwidth_range::set_wider_range(const Bitwidth_range &bw) {
  if (likely(!bw.is_overflow() && !is_overflow())) {
    max = std::max(max, bw.max);
    min = std::min(min, bw.min);
    I(max >= min);
    return;
  }

  auto l_max = get_max();
  auto n_max = bw.get_max();
  if (n_max > l_max) {
    l_max = n_max;
  }

  auto l_min = get_min();
  auto n_min = bw.get_min();
  if (n_min < l_min) {
    l_min = n_min;
  }

  set_range(l_min, l_max);
}

void Bitwidth_range::set_sbits_range(Bits_t size) {
  I(size < Bits_max);

  if (size == 0) {
    overflow = true;
    max      = 32768;
    min      = -32768;
    return;
  }

  if (size > 63) {
    overflow = true;
    max      = size - 1;                     // Use bits in overflow mode
    min      = -static_cast<int>(size - 1);  // Use bits
  } else {
    overflow = false;
    max      = (1ULL << (size - 1)) - 1;
    min      = -static_cast<int>(max + 1);
  }
  I(max >= min);
}

void Bitwidth_range::set_ubits_range(Bits_t size) {
  I(size < Bits_max);

  if (size == 0) {
    overflow = true;
    max      = 32768;
    min      = 0;
    return;
  }
  assert(size);

  min = 0;

  if (size >= 62) {
    overflow = true;
    max      = size + 1;  // Use bits in overflow mode
  } else {
    overflow = false;
    max      = (1ULL << size) - 1;
  }
}

// we get sbits from the max/min since every thing in lgraph should be initially signed
Bits_t Bitwidth_range::get_sbits() const {
  if (overflow) {
    Bits_t bits = std::max(max, -min);
    I(min == 0 || min <= max || max == 0);
    if (min != 0 && max != 0) {
      bits++;
    }
    if (bits >= Bits_max) {
      return 0;  // To indicate overflow (unable to compute)
    }
    return bits;
  }

  auto a    = Lconst(max).get_bits();  // 15 -> 5sbits
  auto b    = Lconst(min).get_bits();
  auto bits = std::max(a, b);

  I(bits < Bits_max);

  return bits;
}

void Bitwidth_range::dump() const {
  //(max, min, sbis, overflow)
  std::print("({}, {}, {}b) {}\n", max, min, get_sbits(), overflow ? "overflow" : "");
}
