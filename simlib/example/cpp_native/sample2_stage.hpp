#pragma once

#include <cstdint>

struct Sample2_stage {
  bool     to1_aValid;
  uint32_t to1_a;

  bool     to2_eValid;
  uint32_t to2_e;

  bool     to3_dValid;
  uint32_t to3_d;

  uint32_t tmp;

  void reset_cycle();
  void cycle(bool s1_to2_aValid, uint32_t s1_to2_a, uint32_t s1_to2_b) {
    to3_dValid =  (tmp & 1) == 0;
    to3_d = tmp+s1_to2_b;

    to2_eValid =  (tmp & 1) == 1 && s1_to2_aValid && to1_aValid;
    to2_e = tmp+s1_to2_a + to1_a;

    to1_aValid =  (tmp & 2) == 2;
    to1_a = tmp+3;

    tmp = tmp + 13;
  }
};

