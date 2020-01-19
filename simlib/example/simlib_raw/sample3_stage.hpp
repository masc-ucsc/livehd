#pragma once

struct Sample3_stage {
  UInt<32> to1_b;

  std::array<UInt<32>, 256> memory;

  uint8_t reset_iterator;
  UInt<32> tmp;
  UInt<32> tmp2;

  void reset_cycle();
  void cycle(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d);
};

