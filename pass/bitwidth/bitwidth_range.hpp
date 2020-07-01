//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "lconst.hpp"

class __attribute__((packed)) Bitwidth_range {
protected:
  static int64_t calc_power2(int64_t x);

public:
  int64_t max = 0;
  int64_t min = 0;

  bool sign     = false;
  bool overflow = false;
  bool max_set  = false;
  bool min_set  = false;
  bool sign_set = false;

  Bitwidth_range(const Bitwidth_range &i) {
    max      = i.max;
    min      = i.min;
    sign     = i.sign;

    overflow = i.overflow;
    max_set  = i.max_set;
    min_set  = i.min_set;
    sign_set = i.sign_set;
  };

  Bitwidth_range(const Lconst &value);
  Bitwidth_range(uint16_t bits, bool sign);
  Bitwidth_range(uint16_t bits);

  void    set_sbits(uint16_t size);
  void    set_ubits(uint16_t size);
  uint16_t get_bits() const;

  bool    expand(const Bitwidth_range &i, bool round2);
  void    pick(const Bitwidth_range &e);
  bool    update(const Bitwidth_range &i);

  bool    is_unsigned() const;

  void    dump() const;

};
