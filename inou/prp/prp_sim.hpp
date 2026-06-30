//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// prp_sim — generate ONE C++ test-driver for the `test` blocks of a Pyrope
// design so `lhd sim` can run them. The DUT `mod`s are lowered to Slop<N> structs
// by inou.cgen.sim (one <name>.hpp/.cpp each); this walks the `test` blocks in the
// same source and emits a single `drv.cpp` holding a run-function per test, a
// registry, and a `main()` that supports:
//   * `--list-tests`        print the tests + parameters as JSON, then exit
//   * `--test NAME`         run only test NAME (repeatable; default = all)
//   * `--seed N`            hlop PRNG seed
//   * `--<param> N`         bind a `test name(params)` parameter (per test)
//   * `--help` / `-h`       usage
// Values are bound at RUN time (argv), never baked in, so one built binary can be
// re-run with any test selection / parameters / seed.
//
// Supported `test` body: decls + `mut acc = Module` instances driven by a
// `tick N { ... step ... }` loop with poke/peek field access and end-of-loop
// `assert`s. Unsupported constructs return a clear error rather than emitting bad
// C++.

#include <string>
#include <vector>

namespace prp_sim {

// The generated driver's basename (one binary per design): `<simdir>/drv.cpp`.
constexpr const char* kDriverBasename = "drv";

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

// Parse `file` and generate `<simdir>/drv.cpp` for every `test` whose dotted name
// matches `test_sel` (empty = all). The DUT interface is read from the cgen_sim
// `<unit>.hpp` files already written into `simdir`. On success returns 0 and fills
// `tests`; on an unsupported construct or parse error returns non-zero + sets
// `err`. `vcd_dir` (empty = no VCD): each test points its DUT at
// `<vcd_dir>/<test_name>.vcd` so every test dumps its own waveform.
int generate(const std::string& file, const std::string& simdir, const std::string& test_sel, const std::string& vcd_dir,
             std::vector<Test_info>& tests, std::string& err);

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
