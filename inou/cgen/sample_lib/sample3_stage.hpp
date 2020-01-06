#pragma once

#include <cstdint>
#include <array>

struct Sample3_stage {
  uint32_t to1_b;

  std::array<uint32_t, 256> memory;

  uint8_t reset_iterator;
  uint32_t tmp;
  uint32_t tmp2;

  void reset_cycle();
  void cycle(bool s1_to3_cValid, uint32_t s1_to3_c, bool s2_to3_dValid, uint32_t s2_to3_d);
};

