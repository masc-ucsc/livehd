//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

class __attribute__((packed)) Ann_ssa {
private:
protected:
  uint32_t vid;
  uint16_t subs;
  bool     final = false;
public:
  constexpr Ann_ssa() : vid(0), subs(0){};
  Ann_ssa(uint32_t v, uint16_t s) : vid(v), subs(s){};

  void set_ssa(uint32_t v, uint16_t s) {
    vid = v;
    subs = s;
  }

  void set_final() {
    final = true;
  }

  uint32_t is_final() const { return final;}
  uint32_t get_vid()  const { return vid; }
  uint16_t get_subs() const { return subs; }
};
