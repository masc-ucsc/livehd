//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

// `lhd sim` through the LNAST flow: lgraph -> typed LNAST (pass.lg2lnast) -> native LLVM state
// kernels (lnast_llvm), with a thin C++ class around the state so the testbench driver that
// inou/prp/prp_sim generates drives it exactly like a class inou.cgen.sim wrote: `__in`/`__out`
// Slop fields, step(), reset_cycle(), checkpoint and observation hooks, and the `<stem>.iface.json`
// manifest. No per-module C++ is generated: the design is the kernels plus one small class.
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
  bool                                      unknown_zero = false;  // sim.unknown_zero: `?` bits are 0, else drawn once per run
  bool                                      vcd          = false;  // sim.vcd: the class writes a VCD of its ports and registers
};

struct Sim_emitted {
  bool                     ok = false;
  std::string              why;    // ok == false: what the flow does not cover (first reason)
  std::vector<std::string> stems;  // file stems written (`<stem>.hpp`, `.cpp`, `.iface.json`)
  size_t                   words        = 0;
  size_t                   instructions = 0;
};

Sim_emitted emit_sim(const Sim_request& request);

}  // namespace livehd::lnast_llvm
