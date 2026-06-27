//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// prp_sim — generate C++ test-driver(s) for `test` blocks so `lhd sim` can run
// them. The DUT `mod`s are lowered to Slop<N> structs by inou.cgen.sim (one
// <name>.hpp/.cpp each); this walks the `test` blocks in the same source and
// emits a driver main() per test that instantiates the DUT Slop, runs the
// `tick` loop calling cycle(), and turns each `assert` into a runtime check.
//
// MVP scope (extended incrementally): a `test` whose body is decls + one
// `tick N { ... }` driving a single DUT + end-of-sim `assert`s. Unsupported
// constructs return a clear error rather than emitting bad C++.

#include <string>
#include <vector>

namespace prp_sim {

struct Driver {
  std::string test_name;  // dotted selector, e.g. "counter.held_high"
  std::string basename;   // generated driver file basename (no .cpp), e.g. "drv_counter_held_high"
};

// Parse `file`, generate a driver .cpp into `simdir` for every `test` whose
// dotted name matches `test_sel` (empty = all). The DUT interface is read from
// the cgen_sim `<unit>.hpp` files already written into `simdir`. On success
// returns 0 and fills `out`; on an unsupported construct or parse error returns
// non-zero and sets `err`.
// `vcd_dir` (empty = no VCD): when set, each driver points its DUT at
// `<vcd_dir>/<test_name>.vcd` so every test dumps its own waveform.
// `test name(params)` parameters are NOT baked in: each becomes a `--<name>`
// flag on the generated driver (alongside `--seed` and `--help`), bound at run
// time. `lhd sim --arg key=value` is forwarded to the driver as `--key value`.
int generate(const std::string& file, const std::string& simdir, const std::string& test_sel, const std::string& vcd_dir,
             std::vector<Driver>& out, std::string& err);

}  // namespace prp_sim
