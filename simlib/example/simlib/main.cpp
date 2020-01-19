//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "livesim_types.hpp"
#include "simlib_checkpoint.hpp"
#include "sample_stage.hpp"

int main(int argc, char **argv) {

  Simlib_checkpoint<Sample_stage> top("check");

  top.enable_trace(".");

  top.advance_clock(100000000);

  // Replay last cycles
  top.load_checkpoint(4891136);
  //top.advance_clock(100000000-4891136);

  //auto v = top.find_previous_checkpoint(3000000);
  //printf("previous_check = %lld\n", (long)v);

  return 0;
}

