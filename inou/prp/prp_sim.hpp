//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// prp_sim -- the `test` blocks of a Pyrope file: their dotted names and parameters (`lhd sim --list-tests`, and the
// test table the LLVM testbench flow reports). The simulator is inou/prp/tb_llvm.hpp + lhd/lhd_sim_llvm_rt.cpp; the
// generated-C++ driver this header used to describe was removed (todo/livehd/2sl-llvm-only-sim.md).

#include <string>
#include <vector>

namespace prp_sim {

// One `test name(params)` parameter. `default_text` is the source text of the
// signature default (valid iff !required); a parameter with no default (or
// `=nil`) is `required` and must be supplied at run time.
struct Param_info {
  std::string name;
  bool        required{false};
  std::string default_text;
};

// One `test` block: its dotted selector + its parameters (declaration order).
struct Test_info {
  std::string             name;
  std::vector<Param_info> params;
};

// Parse `file` for its `test` blocks (matching `test_sel`, empty = all) and fill
// `tests` with their dotted names + parameters — without lowering any DUT. On
// success returns 0; on a parse error / no match returns non-zero + sets `err`.
// The caller renders the result (e.g. JSON via tests_to_json, or a human list).
int list_tests(const std::string& file, const std::string& test_sel, std::vector<Test_info>& tests, std::string& err);

// Render `tests` as the canonical single-line `--list-tests` JSON — the SAME
// shape the generated binary's `--list-tests` prints (so `lhd sim --list-tests`
// in JSON mode and the built binary agree byte-for-byte):
//   {"file":"f.prp","tests":[{"name":"a.b","params":[
//       {"name":"n","required":false,"default":"4"}]}]}
std::string tests_to_json(const std::string& file, const std::vector<Test_info>& tests);

}  // namespace prp_sim
