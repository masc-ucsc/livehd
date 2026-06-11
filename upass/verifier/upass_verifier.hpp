//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "hlop/dlop.hpp"
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
  bool                 overrides_classify_statement() const override { return true; }

  // Reset counts for the single walk; these counts are compared in end_run.
  void begin_iteration() override {
    pass_count    = 0;
    fail_count    = 0;
    unknown_count = 0;
    cputs_count   = 0;
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

  // Assignment
  void        process_assign() override { check_unary(); }
  upass::Vote process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    if (src.size() <= 1) {
      process_assign();
    } else {
      process_tuple_set();
    }
    return upass::Vote::keep;
  }

  // Operators
  // - Bitwidth
  upass::Vote process_bit_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_bit_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  // void process_bit_not() override { check_binary(); }
  upass::Vote process_bit_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  // - Bitwidth Insensitive Reduce
  // void process_reduce_or() override { check_binary(); }
  // - Logical
  upass::Vote process_log_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_log_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  // void process_log_not() override { check_binary(); }
  // - Arithmetic
  upass::Vote process_plus(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_minus(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_mult(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_div(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_mod(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  // - Shift
  upass::Vote process_shl(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_sra(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  // - Bit Manipulation
  // void process_sext() override { check_binary(); }
  // void process_set_mask() override { check_binary(); }
  // void process_get_mask() override { check_binary(); }
  // - Comparison
  upass::Vote process_ne(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_eq(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_lt(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_le(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_gt(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }
  upass::Vote process_ge(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    check_binary();
    return upass::Vote::keep;
  }

private:
  std::size_t              pass_count{0};
  std::size_t              fail_count{0};
  std::size_t              unknown_count{0};
  std::size_t              cputs_count{0};
  std::vector<std::string> unknown_operands;

  // Aggregate state shared across every verifier instance in a single
  // pass.upass invocation. -1 sentinels in expected_* mean "not set,
  // don't check".
  static std::size_t                     aggregate_pass_count;
  static std::size_t                     aggregate_fail_count;
  static std::size_t                     aggregate_unknown_count;
  static std::size_t                     aggregate_cputs_count;
  static std::vector<std::string>        aggregate_unknown_operands;
  static int                             aggregate_expected_pass;
  static int                             aggregate_expected_fail;

  // True when the caller asked for cassert count-checking via verifier_pass /
  // verifier_fail. When false (the default elaborate flow) the verifier runs in
  // quiet mode: it still prints cputs and hard-errors on a comptime-false
  // cassert, but emits no count tally and performs no count-mismatch check.
  static bool has_expectations() { return aggregate_expected_pass >= 0 || aggregate_expected_fail >= 0; }
  // Inliner-style dedup so a cputs inside a function body prints once even
  // when constprop walks the same body again via spawned lnasts for each
  // call site.
  static std::unordered_set<std::string> processed_cputs_keys;

  upass::Emit_decision classify_func_call();

  // Emit one non-fatal diagnostic for a comptime-false cassert (the fatal
  // abort still happens once in finalize_aggregate). `msg` is the optional
  // user-supplied message (cassert's 2nd argument); when empty the diag hints
  // at adding one. See upass_verifier.cpp.
  void emit_false_cassert_diag(const Lnast_nid& cassert_nid, const std::string& operand_text, const std::string& value,
                               const std::string& msg);

  // Report a malformed cputs (operand not comptime-known, not a string, or wrong
  // argument count) as a located, non-fatal diagnostic instead of throwing via
  // upass::error — a cputs mistake is a Pyrope-source error, not an internal
  // invariant violation that should unwind the pipeline. See upass_verifier.cpp.
  void emit_cputs_diag(const Lnast_nid& fcall_nid, const std::string& code, const std::string& message,
                       const std::string& hint);

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
