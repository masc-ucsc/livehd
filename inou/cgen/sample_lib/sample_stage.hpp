#pragma once

#include "sample1_stage.hpp"
#include "sample2_stage.hpp"
#include "sample3_stage.hpp"

struct Sample_stage {
  Sample1_stage s1;
  Sample2_stage s2;
  Sample3_stage s3;

  void reset_cycle();
  void cycle();
};

