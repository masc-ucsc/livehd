//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "hlop/dlop.hpp"
#include "diag.hpp"
#include "lnast_range.hpp"
#include "upass_core.hpp"

// uPass_bitwidth — LNAST bitwidth / value-range analysis.
//
// Registers as a "bitwidth" uPass_plugin, but it is a standalone
// READ-ONLY FINALIZATION analysis: pass.upass runs it on its own
// (`uPass_runner(lm, {"bitwidth"})`) *after* the main opt runner + SSA, never
// interleaved with constprop, and discards the staging tree.
//
// MIGRATED to push-based dispatch. The per-variable facts live on the
// runner symbol table's bundles:
//   * value range  → Entry.bw_max / bw_min on the dst bundle (was range_map_),
//     written through to lnast->bw_meta() so lnast_to_lgraph and the LSP read
//     them (and so ranges persist across pass.upass invocations — reads fall
//     back to bw_meta for names not bound in this walk).
//   * declared envelope → Entry.decl_max / decl_min on the BASE name's
//     binding (the runner's declare pre-step bakes them; this pass also bakes
//     standalone type_spec targets in its own runner — was decl_envelope_).
// The does-not-fit check happens AT THE STORE/OP NODE: the diagnostic
// carries the node's own source span; an error-severity diag already fails
// the compile, so there is no end_run throw, no write_site_, and no
// pending_overflow_msg_.
//
// Key properties:
//   * It does NOT participate in const-folding (no fold_ref) — constprop owns
//     value folding; this removes the stale-narrow-range bug class.
//   * Boolean / comparison results always get the signed 1-bit range [-1, 0].
//   * Compiler temps (___*) participate normally.
//   * Arithmetic on unbounded ranges propagates unbounded conservatively.
struct uPass_bitwidth : public upass::uPass {
public:
  explicit uPass_bitwidth(std::shared_ptr<upass::Lnast_manager>& lm);
  ~uPass_bitwidth() override = default;

  using Vote = upass::Vote;

  // Assignment-shaped ops (LHS = op(rhs…)) — push form.
  Vote process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  Vote process_plus(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_minus(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_mult(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_div(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_mod(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_shl(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_sra(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_and(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_or(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_not(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_xor(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_log_and(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_log_or(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_log_not(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_red_or(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_red_and(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_red_xor(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_popcount(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_ne(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_eq(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_lt(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_le(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_gt(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_ge(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_sext(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_get_mask(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_set_mask(std::string_view, Bundle&, upass::Src_span) override;

  // Nullary hooks (these nodes are not push-dispatched):
  // func_call — clear ranges through opaque `ref` actuals; note a wrap/sat
  // narrowing call's target (exempts its next write from the fit check).
  void process_func_call() override;
  // declare / type_spec — in this pass's standalone runner the declare bake
  // already wrote the envelope (Entry.decl_max/min); a bare type_spec target
  // may be unbound here (no constprop), so this pass ensures the binding.
  void process_type_spec() override;

private:
  // Variables whose next write carries a wrap/sat policy. A wrap/sat call is
  // emitted immediately before the store to its `type` argument; consume the
  // exemption at that store so later ordinary writes are checked normally.
  // (Deliberately transient walk state, like the coalescer's pending set: a
  // one-shot per-write policy handshake, not a per-name fact.)
  absl::flat_hash_set<std::string> wrap_sat_exempt_;

  // ── Lnast_range ↔ bundle-Entry conversion ─────────────────────────────────
  static std::optional<int64_t> const_to_i64(const Dlop& v);
  static Lnast_range            range_from_entry(const Dlop& maxc, const Dlop& minc);
  // Range of a pushed operand: comptime point value first (const literals /
  // folded scalars), then the "0" Entry's bw facts, then bw_meta (cross-
  // invocation persistence), else unbounded.
  Lnast_range range_of_operand(const upass::Operand& o) const;
  // Range of a NAME through the table + bw_meta (nullary func_call path).
  Lnast_range read_range(std::string_view name) const;

  // Write `r` as dst's value range (Entry.bw_max/min + bw_meta write-through).
  // `replace=false` narrows monotonically (iterative refinement of one
  // producer's tmp); `replace=true` overwrites (mut reassignment must not
  // keep a stale narrow range). Also runs the declared-envelope fit check.
  void write_bw(std::string_view name, Bundle& dst, Lnast_range r, bool replace);

  // Forget a value range after an opaque mutation point (ref actual).
  void clear_range(std::string_view name);

  // Declared envelope for `name` (SSA-base resolved) off the base binding's
  // Entry.decl_max/min. nullopt = no (complete) envelope.
  std::optional<Lnast_range> decl_envelope_of(std::string_view name) const;

  // Check this write against the declared envelope, if any. Emits the
  // does-not-fit diagnostic AT the current node.
  void check_declared_fit(std::string_view name, const Lnast_range& r);

  // Check an element store against a declared array's ELEMENT envelope
  // (the root bundle's internal __elem_max/__elem_min attrs, baked by the
  // runner's declare pre-step). No-op for non-array bindings.
  void check_array_elem_fit(std::string_view name, const Lnast_range& r);

  // Emit the common diagnostic at the current node's span.
  void record_overflow(std::string_view name, const Lnast_range& value, const Lnast_range& env);

  // Declared envelope of an operand by name (unbounded for literals/temps):
  // the shift-amount check's fallback when no value range was derived.
  Lnast_range envelope_of_operand(const upass::Operand& o) const;

  // Shift-amount sanity for shl/sra (negative-shift): a hardware shift count
  // must be >= 0, judged on the amount's derived range — error when the range
  // is entirely negative (max < 0), warning when it merely allows negatives
  // (min < 0 <= max). Replaces the old constprop comptime-only diagnostic:
  // the range view also covers folded constants and runtime amounts.
  void check_shift_amount(const Lnast_range& amt);

  // Common body for the value-op hooks.
  Vote stamp(std::string_view dst_name, Bundle& dst, Lnast_range r) {
    // REPLACE semantics: each stamp is a fresh derivation
    // of the value this node just produced. The old narrow-only gate
    // (sound only for a flat accumulating map) refused non-narrowing
    // recomputes, leaving REASSIGNED/loop-rebound bindings with ranges that
    // no longer CONTAIN their value (`mut hit = 0; hit = 30` kept [0,0]).
    write_bw(dst_name, dst, r, /*replace=*/true);
    return Vote::keep;
  }
};
