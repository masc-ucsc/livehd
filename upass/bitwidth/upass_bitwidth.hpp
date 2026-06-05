//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "const.hpp"
#include "diag.hpp"
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

  // Per-run setup hook: seed range_map_ from bw_meta on the first invocation
  // (cross-pass.upass persistence). The runner walks once — no iteration loop.
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

  // Goal 1n N4 — overflow capture. `declare(var, prim_type_int(max,min),
  // const(mode))` / `type_spec(var, prim_type_int(max,min))` record the
  // declared value envelope for `var`; the matching `store`/assign value is
  // range-checked against it at end_run (the value lives in a SEPARATE store
  // node, so the check cannot be node-local). A wrap/sat mode/attr exempts the
  // var (overflow is then intentional truncation/clamp).
  void process_declare() override;
  void process_type_spec() override;

private:
  // Per-variable range map. Persists across begin_iteration calls.
  // Heterogeneous lookup via string_view avoids std::string allocation on every
  // read/set in hot per-statement paths.
  absl::flat_hash_map<std::string, Lnast_range> range_map_;

  // Goal 1n N4 — declared value envelope per variable (from prim_type_int's
  // (max,min) on a `declare`/`type_spec`). Checked against range_map_ at
  // end_run: a bounded value range that provably escapes the envelope (and is
  // not wrap/sat-exempt) is a "does not fit" compile error.
  absl::flat_hash_map<std::string, Lnast_range> decl_envelope_;

  // Variables whose next write carries a wrap/sat policy. A wrap/sat call is
  // emitted immediately before the store to its `type` argument; consume the
  // exemption at that store so later ordinary writes are checked normally.
  absl::flat_hash_set<std::string> wrap_sat_exempt_;

  // Read a `prim_type_int(const max, const min)` envelope assuming the cursor is
  // ON the prim_type_int node; restores the cursor. Returns nullopt unless both
  // bounds are integer consts (a "nil" / non-integer bound = unbounded side =
  // no envelope check). Used by process_declare / process_type_spec.
  std::optional<Lnast_range> read_prim_type_int_envelope();

  // Read range for `name` — returns unbounded() if not yet known.
  Lnast_range read_range(std::string_view name) const;

  // Store `r` for `name`, narrowing monotonically: an existing range is
  // overwritten only when `r` is strictly narrower than what was recorded.
  void set_range(std::string_view name, Lnast_range r);

  // Replace `name`'s range with `r` regardless of relative width. Used by
  // process_assign: a mut reassignment must overwrite the prior range so
  // fold_ref doesn't keep returning the stale narrow constant.
  void replace_range(std::string_view name, Lnast_range r);

  // Forget a value range after an opaque mutation point, such as passing a
  // variable as an explicit `ref` actual to a function call.
  void clear_range(std::string_view name);

  // Check this write against the declared envelope for `name`, if any.
  void check_declared_fit(std::string_view name, const Lnast_range& r);

  // Emit the common diagnostic and remember that this run must fail. Used by
  // per-node checks because the runner catches process_* exceptions.
  void record_overflow(std::string_view name, const Lnast_range& value, const Lnast_range& env);

  // Emit the common "does not fit" diagnostic and abort the pass.
  [[noreturn]] void report_overflow(std::string_view name, const Lnast_range& value, const Lnast_range& env);

  std::optional<std::string> pending_overflow_msg_;

  // Last located write site per base variable (from the store node's loc —
  // the prp2lnast declare/store loc-carry chain). Lets record_overflow point
  // at the offending write even from end_run's deferred envelope check,
  // where no walk cursor exists.
  absl::flat_hash_map<std::string, livehd::diag::Span> write_site_;

  // Record the current node's source span as `name`'s write site (keyed by
  // SSA base name). Call with the cursor ON the store node.
  void note_write_site(std::string_view name);

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
