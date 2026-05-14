//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "const.hpp"
#include "lnast_range.hpp"
#include "upass_core.hpp"

// uPass_bitwidth — LNAST bitwidth / value-range analysis.
//
// Registers as a "bitwidth" uPass_plugin optimizer. For every assignment-shaped
// LNAST statement it computes the min/max value range of the result and stores
// it in an internal map keyed by the LHS variable name.  Ranges persist across
// per-node mark_changed iterations and across outer pass.upass re-invocations.
//
// After the run completes, end_run() flushes the computed ranges into
// lnast->bw_meta() so downstream passes (lnast_to_lgraph) can read them.
//
// Key properties:
//   * Boolean / comparison results always get the signed 1-bit range [-1, 0].
//   * Compiler temps (___*) and port sigils ($, %) participate normally —
//     there is no special exclusion.
//   * fold_ref() returns a concrete Const only when the range collapses to a
//     single constant value, enabling constprop and verifier to fold it.
//   * Arithmetic on unbounded ranges propagates unbounded conservatively.
struct uPass_bitwidth : public upass::uPass {
public:
  explicit uPass_bitwidth(std::shared_ptr<upass::Lnast_manager>& lm);
  ~uPass_bitwidth() override = default;

  // Keep ranges across iterations — only reset the changed flag.
  void begin_iteration() override;
  // Flush computed ranges into lnast->bw_meta().
  void end_run() override;

  // Assignment-shaped ops (LHS = op(rhs…)).
  void process_assign()   override;
  void process_plus()     override;
  void process_minus()    override;
  void process_mult()     override;
  void process_div()      override;
  void process_mod()      override;
  void process_shl()      override;
  void process_sra()      override;
  void process_bit_and()  override;
  void process_bit_or()   override;
  void process_bit_not()  override;
  void process_bit_xor()  override;
  void process_log_and()  override;
  void process_log_or()   override;
  void process_log_not()  override;
  void process_red_or()   override;
  void process_red_and()  override;
  void process_red_xor()  override;
  void process_ne()       override;
  void process_eq()       override;
  void process_lt()       override;
  void process_le()       override;
  void process_gt()       override;
  void process_ge()       override;
  void process_sext()     override;
  void process_get_mask() override;
  void process_set_mask() override;

  // Explicit constraints: `[ubits]`, `[sbits]`, `[max]`, `[min]` narrow the
  // target's inferred range. Conflict reporting is best-effort here (wrap /
  // saturate policy is owned by uPass_attributes today); a hard error will
  // land once the wrap/sat math migrates here per the spec.
  void process_attr_set() override;

  // Returns Const(v) only when the stored range is a single-value constant.
  std::optional<Const> fold_ref(std::string_view name) override;

private:
  // Per-variable range map. Persists across begin_iteration calls.
  std::map<std::string, Lnast_range> range_map_;

  // Read range for `name` — returns unbounded() if not yet known.
  Lnast_range read_range(std::string_view name) const;

  // Store `r` for `name`. Calls mark_changed() iff the range is strictly
  // narrower than what was previously recorded.
  void set_range(std::string_view name, Lnast_range r);

  // Scan the current op node: walk first child (LHS name), remaining children
  // (RHS operands — ref or const), restore cursor on exit.
  struct Op_ranges {
    std::string              lhs;
    std::vector<Lnast_range> rhs;
  };
  Op_ranges scan_op();

  // Like scan_op() but return only the LHS name without consuming children.
  std::string scan_lhs_only();
};
