//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Test support for the rule DSL (testonly). The generated aeg_fuzz_<group>_smoke and
// aeg_prove_<group>_smoke call Rule_check; rulegen's own tests call the functions below directly
// with inline rule text.
//
//   fuzz_equal    LHS and RHS evaluated on random values with the engine's own Dlop folds
//                 (aegraph::eval_op), negatives and values up to ~130 bits included. A rule with a
//                 `?` literal on the left is checked in REFINEMENT mode (every known LHS bit is
//                 kept, no new `?` appears); a choice arm with its assignment substituted.
//   example_fires the rule's :example term, built as a micro-LGraph, makes the engine apply the
//                 rule when it is the only rule enabled (check=full).
//   prove_bv      the bv proof tier: micro-LGraphs of the instantiated LHS and RHS proven equal
//                 by lec::prove_equal.
//
// Seeds: AEG_FUZZ_SEED (default fixed), AEG_FUZZ_SAMPLES (default 1000), AEG_PROVE_FULL=1 for
// every instance instead of at most 8 per rule.

#include <cstddef>
#include <cstdint>
#include <string>

#include "rulegen/rules.hpp"

namespace livehd::aegraph::rulegen::testing {

struct Fuzz_report {
  size_t      samples = 0;  // samples compared
  size_t      skipped = 0;  // samples with no legal assignment (a guard never held, an op had no value)
  std::string failure;      // empty = ok
};

struct Prove_report {
  size_t      instances   = 0;
  size_t      proven      = 0;
  size_t      undecided   = 0;  // timeout / unknown: a pass
  size_t      unsupported = 0;
  size_t      skipped     = 0;  // beyond the group budget, or no legal instance
  std::string refuted;          // empty = ok; else the instance and the counterexample
  std::string note;             // why the first instance was undecided or unsupported (for the log)
};

[[nodiscard]] Fuzz_report  fuzz_equal(const Rule& rule, const Analyses& analyses, uint64_t seed, size_t samples);
// Empty when the example makes the rule fire; else why not.
[[nodiscard]] std::string  example_fires(const Rules_file& file, const Rule& rule);
// `limit_seconds` bounds the whole call (instances beyond it are skipped).
[[nodiscard]] Prove_report prove_bv(const Rule& rule, const Analyses& analyses, bool full, double limit_seconds);

struct Parsed {
  Rules_file file;
  Analyses   analyses;
  Diags      diags;
};
[[nodiscard]] Parsed parse_all(const char* path, const char* rules, const char* analyses);

// gtest drivers for the generated tests.
struct Rule_check {
  static void parses(const char* path, const char* rules, const char* analyses);
  static void fuzz_equal(const char* path, const char* rules, const char* analyses, const char* rule);
  static void example_fires(const char* path, const char* rules, const char* analyses, const char* rule);
  static void prove_bv(const char* path, const char* rules, const char* analyses, const char* rule);
  // Asserts the group's tests so far stayed inside the wall budget (25 s opt, 90 s dbg).
  static void wall_time(const char* what);
};

}  // namespace livehd::aegraph::rulegen::testing
