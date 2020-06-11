//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string_view>
#include <utility>

class __attribute__((packed)) Ann_ssa {
private:
protected:
  std::string_view vname;  // variable name without ssa
  uint16_t         subs;
  bool             final = false;

public:
  constexpr Ann_ssa() : vname(""), subs(0){};
  Ann_ssa(std::string_view n, uint16_t s) : vname(n), subs(s){};

  void set_ssa(std::string_view n, uint16_t s) {
    vname = n;
    subs  = s;
  }

  void set_final() { final = true; }

  uint32_t         is_final() const { return final; }
  std::string_view get_vname() const { return vname; }
  uint16_t         get_subs() const { return subs; }
};
