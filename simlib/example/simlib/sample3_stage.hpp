#pragma once

struct Sample3_stage {
  uint64_t hidx;

  UInt<32> to1_b;

  std::array<UInt<32>, 256> memory;

  uint8_t reset_iterator;
  UInt<32> tmp;
  UInt<32> tmp2;
#ifdef SIMLIB_VCD
  VarPtr vcd_to1_b;

  VarPtr vcd_tmp;
  VarPtr vcd_tmp2;

  void vcd_cycle(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d);
#endif

  #ifdef SIMLIB_VCD
    VarPtr vcd_to1_b;
    VarPtr vcd_tmp;
    VarPtr vcd_tmp2;
    void vcd_cycle(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d);
  #endif

    Sample3_stage(uint64_t _hidx);

  void reset_cycle();
  void cycle(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d);
#ifdef SIMLIB_TRACE
  void add_signature(Simlib_signature &sign);
#endif
};

