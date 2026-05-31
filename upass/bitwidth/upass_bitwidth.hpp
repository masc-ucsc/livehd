//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "const.hpp"
#include "lnast_range.hpp"
#include "upass_core.hpp"

// uPass_bitwidth — LNAST bitwidth / value-range analysis.
//
// Registers as a "bitwidth" uPass_plugin, but (Goal 1n) it is a standalone
// READ-ONLY FINALIZATION analysis: pass.upass runs it on its own
// (`uPass_runner(lm, {"bitwidth"})`) *after* the main opt runner + SSA, never
// interleaved with constprop, and discards the staging tree. For every
// assignment-shaped LNAST statement it computes the min/max value range of the
// result and stores it in an internal map keyed by the LHS variable name.
// Ranges persist across outer pass.upass re-invocations.
//
// After the run completes, end_run() flushes the computed ranges into
// lnast->bw_meta() so the future lnast_to_lgraph can read them and set
// bits/sign (see upass/upass.md §2 "bitwidth"; the per-node-HHDS-tree-attr +
// Dlop-max/min migration is TODO_livehd 1n phases N3/N5).
//
// Key properties:
//   * It does NOT participate in const-folding (no fold_ref) — constprop owns
//     value folding; this removes the stale-narrow-range bug class.
//   * Boolean / comparison results always get the signed 1-bit range [-1, 0].
//   * Compiler temps (___*) participate normally — there is no special
//     exclusion.
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
  void process_assign() override;
  void process_plus() override;
  void process_minus() override;
  void process_mult() override;
  void process_div() override;
  void process_mod() override;
  void process_shl() override;
  void process_sra() override;
  void process_bit_and() override;
  void process_bit_or() override;
  void process_bit_not() override;
  void process_bit_xor() override;
  void process_log_and() override;
  void process_log_or() override;
  void process_log_not() override;
  void process_red_or() override;
  void process_red_and() override;
  void process_red_xor() override;
  void process_popcount() override;
  void process_ne() override;
  void process_eq() override;
  void process_lt() override;
  void process_le() override;
  void process_gt() override;
  void process_ge() override;
  void process_sext() override;
  void process_get_mask() override;
  void process_set_mask() override;
  void process_func_call() override;

  // Explicit constraints: `[ubits]`, `[sbits]`, `[max]`, `[min]` narrow the
  // target's inferred range. Conflict reporting is best-effort here (wrap /
  // saturate policy is owned by uPass_attributes today); a hard error will
  // land once the wrap/sat math migrates here per the spec.
  void process_attr_set() override;

private:
  // Per-variable range map. Persists across begin_iteration calls.
  // Heterogeneous lookup via string_view avoids std::string allocation on every
  // read/set in hot per-statement paths.
  absl::flat_hash_map<std::string, Lnast_range> range_map_;

  // Read range for `name` — returns unbounded() if not yet known.
  Lnast_range read_range(std::string_view name) const;

  // Store `r` for `name`. Calls mark_changed() iff the range is strictly
  // narrower than what was previously recorded.
  void set_range(std::string_view name, Lnast_range r);

  // Replace `name`'s range with `r` regardless of relative width. Used by
  // process_assign: a mut reassignment must overwrite the prior range so
  // fold_ref doesn't keep returning the stale narrow constant.
  void replace_range(std::string_view name, Lnast_range r);

  // Forget a value range after an opaque mutation point, such as passing a
  // variable as an explicit `ref` actual to a function call.
  void clear_range(std::string_view name);

  // Scan the current op node: walk first child (LHS name), remaining children
  // (RHS operands — ref or const), restore cursor on exit.
  // Most LNAST ops have 1–3 operands; inline storage avoids per-statement heap.
  struct Op_ranges {
    // lhs is a string_view into LNAST's persistent attribute store; no
    // copy required since callers use it only for the lifetime of the
    // per-op process_* call.
    std::string_view                    lhs;
    absl::InlinedVector<Lnast_range, 4> rhs;
  };
  Op_ranges scan_op();

  // Like scan_op() but return only the LHS name without consuming children.
  std::string scan_lhs_only();
};
