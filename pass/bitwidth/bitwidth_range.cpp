//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"

#include "iassert.hpp"
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

Bitwidth_range::Bitwidth_range(const Lconst &min_val, const Lconst &max_val) {
	I(max_val >= min_val);

	if (max_val.is_i() && min_val.is_i()) {
		overflow = false;
		max      = max_val.to_i();
		min      = min_val.to_i();
	} else {
		overflow = true;
		if (max_val == 0) {
			max = 0;
		}else{
			auto bits = max_val.get_bits();
			if (max_val.is_negative()) {
				max = -bits;
			} else {
				max = bits;
			}
		}

		if (min_val == 0) {
			min = 0;
		}else{
			auto bits = min_val.get_bits();
			if (min_val.is_negative()) {
				min = -bits;
			} else {
				min = bits;
			}
		}
	}
}

Bitwidth_range::Bitwidth_range(Bits_t bits, bool _sign) {
  if (_sign)
    set_sbits(bits);
  else
    set_ubits(bits);
}

Bitwidth_range::Bitwidth_range(Bits_t bits) {
  set_ubits(bits);
}


void Bitwidth_range::set_sbits(Bits_t size) {
  I(size<=(1UL<<Bits_bits)-1);

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

void Bitwidth_range::set_ubits(Bits_t size) {
  I(size<=(1UL<<Bits_bits)-1);

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

Bits_t Bitwidth_range::get_bits() const {
  if (overflow) {
    Bits_t bits = max;
    if (min < 0) bits++;
    if(bits>=(1UL<<Bits_bits)-1) // Limit in bits
      return 0; // To indicate overflow (unable to compute)
    return bits;
  }

  auto abs_max = abs(max);
  Bits_t bits    = (sizeof(uint64_t) * 8 - __builtin_clzll(abs_max));
	if (min < 0) bits++;

  I(bits<=(1UL<<Bits_bits)-1);

	return bits;
}

void Bitwidth_range::dump() const {
  fmt::print("max:{} min:{} {}"
      , max
      , min
      , overflow ? "overflow" : ""
      );
}

