//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "ann_bitwidth.hpp"
#include "lgraph.hpp"

void Ann_bitwidth::Explicit_range::dump() const {
  fmt::print("max{}:{} min{}:{} sign{}:{} {}", max_set ? "_set" : "", max, min_set ? "_set" : "", min, sign_set ? "_set" : "", sign,
             overflow ? "overflow" : "");
}

bool Ann_bitwidth::Explicit_range::is_unsigned() const { return !sign_set || (sign_set && !sign); }

void Ann_bitwidth::Explicit_range::set_sbits(uint16_t size) {
  sign_set = true;
  sign     = true;

  if (size == 0) {
    overflow = false;
    max_set  = false;
    min_set  = false;
    return;
  }
  assert(size);

  max_set = true;
  min_set = true;

  if (size > 63) {
    overflow = true;
    max      = size - 1;     // Use bits in overflow mode
    min      = -(size - 1);  // Use bits
  } else {
    overflow = false;
    max      = pow(2, size - 1) - 1;
    min      = -pow(2, size - 1);
  }
}

void Ann_bitwidth::Explicit_range::set_ubits(uint16_t size) {
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
    max      = pow(2, size) - 1;
  }
}
void Ann_bitwidth::Explicit_range::set_uconst(uint32_t val) {
  sign_set = true;
  sign     = false;

  max_set = true;
  min_set = true;
  max     = val;
  min     = val;
}

void Ann_bitwidth::Explicit_range::set_sconst(uint32_t val) {
  sign_set = true;
  sign     = true;

  max_set = true;
  min_set = true;
  max     = static_cast<int32_t>(val);  // calculate 2's complement, ex. B = 1011 = -5
  min     = static_cast<int32_t>(val);
}

void Ann_bitwidth::Implicit_range::dump() const {
  fmt::print("max:{} min:{} sign:{} {}", max, min, sign, overflow ? "overflow" : "");
}

int64_t Ann_bitwidth::Implicit_range::round_power2(int64_t x) const {
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

bool Ann_bitwidth::Implicit_range::expand(const Implicit_range &imp_range, bool round2) {
  bool updated = false;

  if (sign & !imp_range.sign) updated = true;

  sign = sign & imp_range.sign;

  if (!imp_range.overflow && overflow) {
    // Nothing to do (!overflow is always smaller than overflow)
  } else if (imp_range.overflow && !overflow) {
    overflow = true;
    updated  = true;
    max      = imp_range.max;
    min      = imp_range.min;
  } else {
    auto tmp = std::max(max, imp_range.max);
    if (round2 && !overflow) {
      tmp = round_power2(tmp);
      if (sign) tmp--;
    }
    updated |= (tmp != max);
    max = tmp;

    if (sign) {
      tmp = -max - 1;
    } else {
      tmp = 0;
    }
    if (round2 && !overflow) tmp = round_power2(tmp);
    updated |= (tmp != min);
    min = tmp;
  }

  return updated;
}

void Ann_bitwidth::Implicit_range::pick(const Explicit_range &exp_range) {
  if (!exp_range.overflow && !overflow) {
    if (exp_range.sign && sign) {
    } else if (exp_range.sign && !sign) {
      max = max / 2;
      min = -min / 2;
    } else if (!exp_range.sign && sign) {
      // max = max;
      min = 0;
    }

    if (max > exp_range.max) max = exp_range.max;

    if (min < exp_range.min) min = exp_range.min;
  } else if (exp_range.overflow && overflow) {
    if (exp_range.sign && sign) {
    } else if (exp_range.sign && !sign) {
      max = max - 1;
      min = min + 1;
    } else if (!exp_range.sign && sign) {
      // max = max;
      min = 0;
    }

    if (max > exp_range.max) max = exp_range.max;

    if (min < exp_range.min) min = exp_range.min;
  } else if (exp_range.overflow && !overflow) {
  } else if (!exp_range.overflow && overflow) {
    overflow = exp_range.overflow;
    max      = exp_range.max;
    min      = exp_range.min;
  }
}

