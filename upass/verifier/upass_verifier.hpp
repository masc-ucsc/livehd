//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "lconst.hpp"
#include "lnast_ntype.hpp"
#include "upass_core.hpp"

struct uPass_verifier : public upass::uPass {
public:
  using uPass::uPass;

  // Cassert disposition at comptime. Uses the runner-backed fold callback
  // to resolve the operand. See upass.md §3 Slice 2.
  //   known-true  → drop (assertion discharged), increments pass_count
  //   known-false → upass::error, increments fail_count
  //   unknown     → emit, increments unknown_count
  upass::Emit_decision classify_statement() override;

  // Reset counts so a re-entered iteration starts fresh. The final
  // iteration's counts are the ones compared in end_run.
  void begin_iteration() override {
    upass::uPass::begin_iteration();
    pass_count    = 0;
    fail_count    = 0;
    unknown_count = 0;
    unknown_operands.clear();
  }

  // Prints a one-line count summary for the current lnast and adds the
  // local tallies to the test-level aggregate. Per-lnast counts are
  // informational only — the actual expected_pass / expected_fail check
  // runs once across all lnasts via finalize_aggregate(), since a test's
  // verifier_pass / verifier_fail describes the whole program (top + any
  // lnasts spawned by func_extract), not just the entry-point lnast.
  void end_run() override;

  // Test-level aggregate counters. pass.upass calls reset_aggregate() /
  // set_aggregate_expected() before processing the first lnast and
  // finalize_aggregate() after the last one finishes. Each verifier
  // instance contributes via end_run().
  static void reset_aggregate();
  static void set_aggregate_expected(int expected_pass, int expected_fail);
  static void finalize_aggregate();

  // Inliner-driven cassert tally. When constprop's try_eval_comb_call proves
  // a function-body cassert at a call site (with concrete actuals), it
  // records the result here keyed by the cassert's source location. The
  // dedup ensures the body cassert counts once even when the function is
  // called from multiple sites, and the spawned-lnast verifier consults the
  // same set in classify_statement to skip already-counted casserts (avoids
  // double-counting via the spawned-lnast walk where the operand is
  // unresolvable because the parameters are templates).
  //
  // First mark wins for pass/fail. We could refine to "any fail demotes a
  // prior pass" if that ever matters; today's tests don't need it.
  static bool already_counted_cassert(std::string_view key);
  static void mark_inlined_cassert_pass(std::string_view key);
  static void mark_inlined_cassert_fail(std::string_view key);

  // Assignment
  void process_assign() override { check_unary(); }

  // Operators
  // - Bitwidth
  void process_bit_and() override { check_binary(); }
  void process_bit_or() override { check_binary(); }
  // void process_bit_not() override { check_binary(); }
  void process_bit_xor() override { check_binary(); }
  // - Bitwidth Insensitive Reduce
  // void process_reduce_or() override { check_binary(); }
  // - Logical
  void process_log_and() override { check_binary(); }
  void process_log_or() override { check_binary(); }
  // void process_log_not() override { check_binary(); }
  // - Arithmetic
  void process_plus() override { check_binary(); }
  void process_minus() override { check_binary(); }
  void process_mult() override { check_binary(); }
  void process_div() override { check_binary(); }
  void process_mod() override { check_binary(); }
  // - Shift
  void process_shl() override { check_binary(); }
  void process_sra() override { check_binary(); }
  // - Bit Manipulation
  // void process_sext() override { check_binary(); }
  // void process_set_mask() override { check_binary(); }
  // void process_get_mask() override { check_binary(); }
  // void process_mask_and() override { check_binary(); }
  // void process_mask_popcount() override { check_binary(); }
  // void process_mask_xor() override { check_binary(); }
  // - Comparison
  void process_ne() override { check_binary(); }
  void process_eq() override { check_binary(); }
  void process_lt() override { check_binary(); }
  void process_le() override { check_binary(); }
  void process_gt() override { check_binary(); }
  void process_ge() override { check_binary(); }

private:
  std::size_t              pass_count{0};
  std::size_t              fail_count{0};
  std::size_t              unknown_count{0};
  std::vector<std::string> unknown_operands;

  // Aggregate state shared across every verifier instance in a single
  // pass.upass invocation. -1 sentinels in expected_* mean "not set,
  // don't check".
  static std::size_t                     aggregate_pass_count;
  static std::size_t                     aggregate_fail_count;
  static std::size_t                     aggregate_unknown_count;
  static std::vector<std::string>        aggregate_unknown_operands;
  static int                             aggregate_expected_pass;
  static int                             aggregate_expected_fail;
  static std::unordered_set<std::string> processed_cassert_keys;

  void check_binary() {
    move_to_child();

    check_type(Lnast_ntype::Lnast_ntype_ref);
    move_to_sibling();
    check_type(Lnast_ntype::Lnast_ntype_ref, Lnast_ntype::Lnast_ntype_const);
    move_to_sibling();
    check_type(Lnast_ntype::Lnast_ntype_ref, Lnast_ntype::Lnast_ntype_const);
    end_of_siblings();

    move_to_parent();
  }

  void check_unary() {
    move_to_child();
    check_type(Lnast_ntype::Lnast_ntype_ref);
    move_to_sibling();
    check_type(Lnast_ntype::Lnast_ntype_ref, Lnast_ntype::Lnast_ntype_const);
    end_of_siblings();
    move_to_parent();
  }

  void end_of_siblings() const {
    if (!is_last_child()) {
      upass::error("");
    }
  }

  template <class... Lnast_ntype_int>
  void check_type(Lnast_ntype_int... ty) const {
    if (is_invalid()) {
      upass::error("invalid\n");
      return;
    }
    auto       actual  = get_raw_ntype();
    const bool type_ok = ((actual == ty) || ...);
    if (type_ok) {
      return;  // OK
    }
    upass::error("failed\n");
  }

  template <class T, class... Targs>
  void print_types(T ty, Targs... tys) const {
    std::cout << Lnast_ntype::debug_name(ty) << " ";
    print_types(tys...);
  }

  void print_types() const {}
};

// Plugin registration lives in upass_verifier.cpp to avoid duplicate
// construction when multiple TUs include this header.
