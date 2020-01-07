#pragma once

#include <cstdint>

struct Sample1_stage {
  bool to2_aValid;
  uint32_t to2_a;
  uint32_t to2_b;

  bool to3_cValid;
  uint32_t to3_c;

  uint32_t tmp;

  void reset_cycle();
#if 0
  void cycle(uint32_t s3_to1_b, bool s2_to1_aValid, uint32_t s2_to1_a) {
    to2_b = s3_to1_b + 1;

    to2_a      = s2_to1_a + s3_to1_b + 2;
    to2_aValid = s2_to1_aValid;

    to3_cValid =  (tmp & 1) != 0;
    to3_c = tmp + s2_to1_a;

    tmp = tmp + 23;
  }
#else
  void cycle(uint32_t s3_to1_b, bool s2_to1_aValid, uint32_t s2_to1_a);
#endif
};

