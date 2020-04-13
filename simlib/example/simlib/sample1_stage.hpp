#pragma once

struct Sample1_stage {
  uint64_t hidx; // lgraph hierarchy index

  UInt<1> to2_aValid;
  UInt<32> to2_a;
  UInt<32> to2_b;

  UInt<1> to3_cValid;
  UInt<32> to3_c;

  UInt<32> tmp;

  Sample1_stage(uint64_t _hidx);

  void reset_cycle();
  void cycle(UInt<32> s3_to1_b, UInt<1> s2_to1_aValid, UInt<32> s2_to1_a);
  void add_signature(Simlib_signature &sign);
};

