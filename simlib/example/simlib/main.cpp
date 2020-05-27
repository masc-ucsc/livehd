//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "livesim_types.hpp"
#include "simlib_checkpoint.hpp"
#include "sample_stage.hpp"

int main(int argc, char **argv) {
  Simlib_checkpoint<Sample_stage> top("ckpt");
  //top.enable_trace(".");
  if(getenv("SIMLIB_DUMPDIR")) {
    top.enable_trace(getenv("SIMLIB_DUMPDIR"));//to dump the created files in scrap folder so as to not saturate the NFS
  } else {
    top.enable_trace(".");
  }
//  top.advance_clock(100000000);
  top.advance_clock(10000);
  // Replay last cycles:
//  top.load_intermediate_checkpoint(3500000);
 // top.advance_clock(100000000-30000);
  // top.advance_clock(100000000-30000);
  return 0;
}

