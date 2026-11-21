//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

// `lhd sim`: lgraph -> typed LNAST (pass.lg2lnast) -> native LLVM state kernels (lnast_llvm). Per DUT it writes the
// kernel objects and `<stem>.dut.json` (the state layout, the kernel schedule and the activity-gating marks); the
// testbench object inou/prp/tb_llvm lowers calls the kernels directly and reads and writes the port words in place.
// No C++ is generated.
namespace livehd::lnast_llvm {

struct Sim_request {
  std::vector<std::shared_ptr<hhds::Graph>> graphs;  // the compiled design (never modified: lowered from a private copy)
  std::string                               top;     // full graph name of the DUT root, or empty: see `duts`
  // Names the testbench imports (`import("lg:Top")` -> "Top"). Not empty: only the un-instantiated
  // definitions one of them names are simulated; a library usually also holds definitions nothing
  // instantiates any more (inlined or specialized away), and those need no simulator.
  std::vector<std::string>                  duts;
  std::string                               odir;    // the simulator directory (`<workdir>/sim`)
  std::string                               passes;  // LNAST stream passes (pass.lg2lnast.passes)
  bool                                      dump_bitcode = false;
  bool                                      incremental  = true;
  bool                                      unknown_zero = false;  // sim.unknown_zero: `?` bits are 0, else drawn once per run
};

struct Sim_emitted {
  bool                     ok = false;
  std::string              why;    // ok == false: what the flow does not cover (first reason)
  std::vector<std::string> stems;  // native DUT descriptors written (`<stem>.dut.json`)
  size_t                   words        = 0;
  size_t                   instructions = 0;
};

Sim_emitted emit_sim(const Sim_request& request);

}  // namespace livehd::lnast_llvm
