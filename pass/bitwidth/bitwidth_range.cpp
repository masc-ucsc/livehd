//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"

#include "bitwidth_range.hpp"

int64_t Bitwidth_range::calc_power2(int64_t x) {
  uint64_t ux = abs(x);

  if (x == 0) {
    return 0;
  }

  // This finds the number of bits in "ux" minus the number of leading 0s.
  uint64_t ux_r = 1ULL << (sizeof(uint64_t) * 8 - __builtin_clzll(ux));

  if (ux == static_cast<uint64_t>(x)) {
    return ux_r - 1;  // I need to subtract one since 4 bits gives us 0 to 15, not 0 to 2^4.
  }

  // FIXME: Above I subtract one. Do I need to do this for negatives? I'd assume not.
  return -ux_r;
}

Bitwidth_range::Bitwidth_range(const Lconst &val) {
  sign_set = val.is_explicit_sign();
  sign     = val.is_negative();

  max_set = true;
  min_set = true;
  if (val.is_i()) {
    overflow = false;
    max      = val.to_i();
    min      = val.to_i();
  } else {
    overflow = true;
    max      = val.get_bits();
    min      = val.get_bits();
  }
}

Bitwidth_range::Bitwidth_range(uint16_t bits, bool _sign) {
  if (_sign)
    set_sbits(bits);
  else
    set_ubits(bits);
}

Bitwidth_range::Bitwidth_range(uint16_t bits) {
  set_ubits(bits);
  sign_set = false;
}


void Bitwidth_range::set_sbits(uint16_t size) {
  sign_set = true;
  sign     = true;

  if (size == 0) {
    overflow = false;
    max_set  = false;
    min_set  = false;
    return;
  }

  max_set = true;
  min_set = true;

  if (size > 63) {
    overflow = true;
    max      = size - 1;     // Use bits in overflow mode
    min      = -(size - 1);  // Use bits
  } else {
    overflow = false;
    max      = (1UL<<(size-1))-1;
    min      = -(1UL<<(size-1));
  }
}

void Bitwidth_range::set_ubits(uint16_t size) {
  sign_set = false;
  sign     = false;

  if (size == 0) {
    overflow = false;
    max_set  = false;
    min_set  = false;
    return;
  }
  assert(size);

  max_set = true;
  min_set = true;
  min     = 0;

  if (size > 63) {
    overflow = true;
    max      = size;  // Use bits in overflow mode
  } else {
    overflow = false;
    max      = (1UL<<size)-1;
  }
}

uint16_t Bitwidth_range::get_bits() const {
  uint16_t bits = 0;
  if (overflow)
    bits = max;
  else
    bits = calc_power2(max);

  if (sign) bits++;

  return bits;
}

bool Bitwidth_range::expand(const Bitwidth_range &range2, bool round2) {
  bool updated = false;

  if (sign & !range2.sign) updated = true;

  sign = sign & range2.sign;

  if (!range2.overflow && overflow) {
    // Nothing to do (!overflow is always smaller than overflow)
  } else if (range2.overflow && !overflow) {
    overflow = true;
    updated  = true;
    max      = range2.max;
    min      = range2.min;
  } else {
    auto tmp = std::max(max, range2.max);
    if (round2 && !overflow) {
      tmp = calc_power2(tmp);
      if (sign) tmp--;
    }
    updated |= (tmp != max);
    max = tmp;

    if (sign) {
      tmp = -max - 1;
    } else {
      tmp = 0;
    }
    if (round2 && !overflow) tmp = calc_power2(tmp);
    updated |= (tmp != min);
    min = tmp;
  }

  return updated;
}

void Bitwidth_range::pick(const Bitwidth_range &range2) {
  if (!range2.overflow && !overflow) {
    if (range2.sign && sign) {
    } else if (range2.sign && !sign) {
      max = max / 2;
      min = -min / 2;
    } else if (!range2.sign && sign) {
      // max = max;
      min = 0;
    }

    if (max > range2.max) max = range2.max;

    if (min < range2.min) min = range2.min;
  } else if (range2.overflow && overflow) {
    if (range2.sign && sign) {
    } else if (range2.sign && !sign) {
      max = max - 1;
      min = min + 1;
    } else if (!range2.sign && sign) {
      // max = max;
      min = 0;
    }

    if (max > range2.max) max = range2.max;

    if (min < range2.min) min = range2.min;
  } else if (range2.overflow && !overflow) {
  } else if (!range2.overflow && overflow) {
    overflow = range2.overflow;
    max      = range2.max;
    min      = range2.min;
  }
}

bool Bitwidth_range::update(const Bitwidth_range &range2) {
  bool min_diff  = min != range2.min;
  bool max_diff  = max != range2.max;
  bool sign_diff = sign != range2.sign;
  bool ovfl_diff = overflow != range2.overflow;

  if (min_diff | max_diff | sign_diff | ovfl_diff) {
    min      = range2.min;
    max      = range2.max;
    sign     = range2.sign;
    overflow = range2.overflow;

    return true;
  }

  return false;
}

bool Bitwidth_range::is_unsigned() const { return !sign_set || (sign_set && !sign); }

void Bitwidth_range::dump() const {
  fmt::print("max{}:{} min{}:{} sign{}:{} {}"
      , max_set  ? "_set" : "", max
      , min_set  ? "_set" : "", min
      , sign_set ? "_set" : "", sign
      , overflow ? "overflow" : ""
      );
}
