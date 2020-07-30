//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string_view>
#include <utility>

class __attribute__((packed)) Ann_ssa {
private:
protected:
  uint16_t subs;
  bool     final = false;

public:
  constexpr Ann_ssa() : subs(0){};
  Ann_ssa(uint16_t s) : subs(s){};

  void set_ssa(uint16_t s) { subs = s; }

  void set_final() { final = true; }

  uint32_t is_final() const { return final; }
  uint16_t get_subs() const { return subs; }
};
