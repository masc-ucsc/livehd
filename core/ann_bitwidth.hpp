//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

class __attribute__((packed)) Ann_bitwidth {
public:
  class __attribute__((packed)) Explicit_range {
  public:
    int64_t max = 0;
    int64_t min = 0;

    bool sign     = false;
    bool overflow = false;
    bool max_set  = false;
    bool min_set  = false;
    bool sign_set = false;

    void dump() const;
    bool is_unsigned() const;
    void set_uconst(uint32_t value);
    void set_sconst(uint32_t value);
    void set_ubits(uint16_t size); //FIXME->sh: why it's only 16 bits
    void set_sbits(uint16_t size);
  };

  class __attribute__((packed)) Implicit_range {
  public:
    int64_t max;
    int64_t min;

    bool sign;
    bool overflow;



    Implicit_range() {
      max      = 0;
      min      = 0;
      sign     = false;
      overflow = false;
    }
    Implicit_range(const Implicit_range &i) {
      max      = i.max;
      min      = i.min;
      sign     = i.sign;
      overflow = i.overflow;
    }
    void    dump() const;
    int64_t round_power2(int64_t x) const;
    bool    expand(const Implicit_range &i, bool round2);
    void    pick(const Explicit_range &e);
    bool    update(const Implicit_range &i);
  };

  Implicit_range i;
  Explicit_range e;
  uint16_t       niters = 0;
  bool           fixed; // var bitwidth is fixed by source code
  bool           dp_flag;  // is a tail subset of another variable, it will be resolved during BW algorithm

  void set_implicit() {
    i.min = e.min;
    i.max = e.max;

    i.sign     = !e.is_unsigned();
    i.overflow = e.overflow;
  }
};
