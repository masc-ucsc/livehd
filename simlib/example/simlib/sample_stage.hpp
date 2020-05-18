#pragma once
#include <string>
#include "sample1_stage.hpp"
#include "sample2_stage.hpp"
#include "sample3_stage.hpp"
#include "vcd_writer.hpp"
#include <time.h>

struct Sample_stage {
  uint64_t hidx;

  Sample1_stage s1;
  Sample2_stage s2;
  Sample3_stage s3;

  Sample_stage(uint64_t _hidx);

  void reset_cycle();
#ifndef SIMLIB_VCD
  void cycle();
#endif

#ifdef SIMLIB_VCD
  //to avoid NFS saturation:
  void initialize_vcd_writer();
  void vcd_cycle();
#endif
#ifdef SIMLIB_TRACE
  void add_signature(Simlib_signature &sign);
#endif
};
