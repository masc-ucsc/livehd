#pragma once
#include <string>
#include "sample1_stage.hpp"
#include "sample2_stage.hpp"
#include "sample3_stage.hpp"
#include "vcd_writer.hpp"

struct Sample_stage {
  uint64_t hidx;

  Sample1_stage s1;
  Sample2_stage s2;
  Sample3_stage s3;

#ifdef SIMLIB_VCD
  Sample_stage(uint64_t _hidx, vcd::VCDWriter &vcd_writer);
#else
  Sample_stage(uint64_t _hidx);
#endif

  void reset_cycle();

#ifdef SIMLIB_VCD
  void vcd_cycle();
#else
  void cycle();
#endif
#ifdef SIMLIB_TRACE
  void add_signature(Simlib_signature &sign);
#endif
};
