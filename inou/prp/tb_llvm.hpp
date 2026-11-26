//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// prp_tb -- the LLVM testbench flow of `lhd sim` (todo/livehd/2sl-llvm-only-sim.md).
//
// Where prp_sim prints a C++ driver (every value a `Slop<N>`), prp_tb lowers the `test` blocks of a
// Pyrope file straight to LLVM IR and writes ONE native object next to the DUT kernels:
//
//   <simdir>/drv.tb.o      the tests (one function each), plus per-DUT step/settle/all-dirty functions
//                          that call the pass/lnast_llvm kernels directly
//   <simdir>/drv.tb.json   the tests (parameters, instances, state layout) for the run-time loader
//
// A test's state is ONE flat array of 64-bit words: the DUT instances it declares (`mut acc = Module`)
// laid end to end, each exactly as pass/lnast_llvm laid its own state out. A poke `acc.a = v` is a store
// into the DUT's input word (and its activity-gating marks), a peek `acc.r` a load: no ports are copied
// and no C++ exists. Testbench locals are LLVM values; their width is the allocator's business.
//
// The loader/runner is lhd/lhd_sim_llvm_rt.cpp (`lhd __simrt`). Unsupported constructs are an error that
// names the construct (there is no fallback).

#include <optional>
#include <string>
#include <vector>

#include "prp_sim.hpp"
#include "tb_abi.hpp"

namespace prp_tb {

// Resolve the modules instantiated by test blocks against the compiled definitions.
bool discover_duts(const std::string& file, const std::vector<std::string>& modules, std::vector<std::string>& used, std::string& err);

// Parse `file`, lower every `test` matching `test_sel` (empty = all) against the `<stem>.dut.json`
// files in `simdir`, and write the object + manifest. Returns 0 and fills `tests`, or non-zero with `err`.
// `also_trace`: write `drv.tb.trace.o` as well, the same tests with a per-step trace hook and a stop flag (the observation
// replay tool runs it; the simulation object itself carries no tracing code).
int generate(const std::string& file, const std::string& simdir, const std::string& test_sel,
             std::vector<prp_sim::Test_info>& tests, std::string& err, bool also_trace, bool dump_bitcode, bool eval_counts,
             std::optional<uint64_t> cache_salt);

}  // namespace prp_tb
