//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"

#include "bitwidth_range.hpp"

int64_t Bitwidth_range::round_power2(int64_t x) {

  if (x==0 || (x & (x - 1)) == 0) {
    return x; // already power of 2
  }

  uint64_t ux = abs(x);

  // This finds the number of bits in "ux" minus the number of leading 0s.
  uint64_t ux_r = 1ULL << (sizeof(uint64_t) * 8 - __builtin_clzll(ux));

  if (x>0) {
    return ux_r - 1;  // I need to subtract one since 4 bits gives us 0 to 15, not 0 to 2^4.
  }

  return -ux_r;
}

Lconst Bitwidth_range::to_lconst(bool overflow, int64_t val) {
  if (val==0)
    return Lconst(0);

  if (overflow) {
    if (val > 0) {
      return Lconst(1).lsh_op(val) - 1;
    } else {
      return Lconst(0) - (Lconst(1).lsh_op(-val) - 1);
    }
  } else {
    if (val>0)
      return Lconst(val);
    else
      return Lconst(0) - Lconst(-val);
  }
}

Bitwidth_range::Bitwidth_range(const Lconst &val) {
  if (val.is_i()) {
    overflow = false;
    max      = val.to_i();
    min      = val.to_i();
  } else {
    overflow = true;
		auto bits = val.get_bits();

    if (val.is_negative()) {
      max = -bits;
      min = -bits;
    } else {
      max = bits;
      min = bits;
    }
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
}


void Bitwidth_range::set_sbits(uint16_t size) {
  if (size == 0) {
    overflow = true;
    max      = 326768;
    min      = -32768;
    return;
  }

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
  if (size == 0) {
    overflow = true;
    max      = 326768;
    min      = 0;
    return;
  }
  assert(size);

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
  if (overflow) {
    bits = max;
  } else {
    auto abs_max = abs(max);
    bits    = (sizeof(uint64_t) * 8 - __builtin_clzll(abs_max));
  }

	// TODO: In theory, we could have an always negative number optimized to have
	// 1 bit less
	if (min < 0) bits++;

  if (bits>=32768)
    return 0;

	return bits;
}

void Bitwidth_range::expand(const Bitwidth_range &range2, bool round2) {

  if (!range2.overflow && overflow) {
    // Nothing to do (!overflow is always smaller than overflow)
  } else if (range2.overflow && !overflow) {
    overflow = true;
    max      = range2.max;
    min      = range2.min;
  } else {
    if (round2) {
			max = std::max(round_power2(max), round_power2(range2.max));
    } else {
			max = std::max(max, range2.max);
    }

    if (round2) {
			min = std::min(round_power2(min), round_power2(range2.min));
    } else {
			min = std::min(min, range2.min);
    }
  }
}

void Bitwidth_range::and_op(const Bitwidth_range &range2) {

  if (!range2.overflow && overflow) {
		overflow = false;
    max = range2.max;
    min = range2.min;
  } else if (range2.overflow && !overflow) {
		// Keep the smallest
  } else {
    if (min < 0 || range2.min < 0) {
      max = std::max(round_power2(max), round_power2(range2.max));
      min = std::min(round_power2(min), round_power2(range2.min));
    } else {
      max = std::min(round_power2(max), round_power2(range2.max));
      min = 0;
    }
  }
}

void Bitwidth_range::dump() const {
  fmt::print("max:{} min:{} {}"
      , max
      , min
      , overflow ? "overflow" : ""
      );
}

