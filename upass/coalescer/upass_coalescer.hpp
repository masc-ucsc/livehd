//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "upass_core.hpp"

// uPass_coalescer — deferred-emit / dead-store-elimination pass.
//
// Runs immediately after constprop in the default order. For every "drop
// candidate" op-node (assign, plus, …) whose LHS is a pure non-boundary local
// and not already a comptime-known constant, the coalescer:
//
//   1. Records the source LNAST nid as a *pending* write keyed by LHS name
//      and votes drop in classify_statement. The runner does not emit the
//      stmt at this position.
//   2. On a later read of any pending name (in the RHS of another op), the
//      coalescer flushes that pending — invokes the runner's emit_op_with_fold
//      replay so the parked stmt lands in the staging tree right before the
//      reading op. After flush, the slot is cleared.
//   3. On a later write to the same name with no intervening read, the older
//      pending is *discarded* — pure dead-store elimination. The new stmt is
//      parked in its place.
//   4. On a self-write (RHS reads its own LHS), the prior pending is flushed
//      first (step 2), then the new stmt is parked. v1 does not coalesce
//      successive `r += k1; r += k2` into a single accumulator stmt — that's
//      Tier B and gated on a small structural rewrite that this slice
//      intentionally omits.
//   5. At every scope/control-flow boundary (process_if, process_stmts_pre_pop)
//      all pendings are flushed in source-nid order so cross-scope reorder is
//      impossible.
//
// Disable with `pass.upass coalescer:0` — the pass is removed from the
// default order entirely; no flag-check overhead in the hot path.
struct uPass_coalescer : public upass::uPass {
public:
  using uPass::uPass;

  // Drop-candidate op-nodes that the coalescer parks. Mirrors the A_OP set in
  // the runner's switch, restricted to scalar-result ops where parking is
  // safe. Tuple/attr/func ops carry shape/side-effect semantics and are
  // intentionally left to the runner's normal emit path.
  void process_assign() override { handle_op(); }
  void process_plus() override { handle_op(); }
  void process_minus() override { handle_op(); }
  void process_mult() override { handle_op(); }
  void process_div() override { handle_op(); }
  void process_mod() override { handle_op(); }
  void process_shl() override { handle_op(); }
  void process_sra() override { handle_op(); }
  void process_bit_and() override { handle_op(); }
  void process_bit_or() override { handle_op(); }
  void process_bit_not() override { handle_op(); }
  void process_bit_xor() override { handle_op(); }
  void process_log_and() override { handle_op(); }
  void process_log_or() override { handle_op(); }
  void process_log_not() override { handle_op(); }
  void process_red_or() override { handle_op(); }
  void process_red_and() override { handle_op(); }
  void process_red_xor() override { handle_op(); }
  void process_sext() override { handle_op(); }
  void process_get_mask() override { handle_op(); }
  void process_set_mask() override { handle_op(); }
  void process_ne() override { handle_op(); }
  void process_eq() override { handle_op(); }
  void process_lt() override { handle_op(); }
  void process_le() override { handle_op(); }
  void process_gt() override { handle_op(); }
  void process_ge() override { handle_op(); }

  // Scope / control-flow boundaries — flush everything to keep emission order
  // anchored to the original lexical position of each parked stmt. v1 is
  // conservative; refining to per-name liveness across boundaries is a
  // follow-up.
  void process_if() override { flush_all(); }
  void process_for() override { flush_all(); }
  void process_while() override { flush_all(); }
  void process_uif() override { flush_all(); }
  void process_func_def() override { flush_all(); }
  void process_func_call() override { flush_all(); }
  void process_stmts_pre_pop() override { flush_all(); }
  void notify_uncertain_arm_begin() override { flush_all(); }
  void notify_uncertain_arm_end() override { flush_all(); }

  // Cassert reads its operand and is treated as a barrier — flush so the
  // operand reflects whatever value the parked producer would have emitted.
  void process_cassert() override { flush_all(); }

  // Verbatim ops that touch tuple state — flush as a barrier for the same
  // reason. Bundle aliasing is hard to reason about across deferred writes.
  void process_tuple_set() override { flush_all(); }
  void process_tuple_add() override { flush_all(); }
  void process_tuple_get() override { flush_all(); }
  void process_tuple_concat() override { flush_all(); }
  void process_attr_set() override { flush_all(); }
  void process_attr_get() override { flush_all(); }

  bool                 overrides_classify_statement() const override { return true; }
  upass::Emit_decision classify_statement() override {
    if (parked_current_stmt) {
      parked_current_stmt = false;
      return upass::Emit_decision::drop();
    }
    return upass::Emit_decision::emit_node();
  }

  void set_options(const upass::Options_map& opts) override;

  // Counters surfaced for tests / diagnostics.
  std::size_t pending_count() const { return pending.size(); }
  std::size_t parked_total() const { return stat_parked; }
  std::size_t dse_dropped_total() const { return stat_dse_dropped; }
  std::size_t flushed_total() const { return stat_flushed; }

private:
  // Source LNAST nid of a parked stmt, keyed by LHS name. Ordered by name so
  // end-of-scope flush has a stable order. Source-nid ordering would also
  // work; nid order isn't lexicographically meaningful as a sort key.
  std::map<std::string, Lnast_nid> pending;

  // Set by handle_op when it parks the current stmt; consumed by
  // classify_statement to vote drop. One-shot per stmt.
  bool parked_current_stmt{false};

  // coalescer:0 disables parking — pass becomes inert (still flushes empty
  // dict on boundaries; cheap).
  bool enabled{true};

  std::size_t stat_parked{0};
  std::size_t stat_dse_dropped{0};
  std::size_t stat_flushed{0};

  // Top-level handler: scan the current op's children, flush any pending
  // names read by RHS, then decide whether to park (DSE-replace or fresh
  // park) or let the runner emit normally.
  void handle_op();

  // Flush every pending stmt at the current staging cursor in name order.
  // Pendings whose LHS is now comptime-known by some other pass (constprop)
  // are dropped instead of emitted — they would have been dropped by
  // constprop's own classify_statement on the original emit path.
  void flush_all();

  // Flush a single pending entry. Returns true if it emitted, false if it
  // was dropped because the LHS is comptime-known.
  bool flush_one(const std::string& name);

  // Park the current op-node as a pending write to `lhs`. If a pending for
  // `lhs` already exists, it is discarded (DSE).
  void park_current(const std::string& lhs);

  // Boundary metadata is structural; textual ref prefixes are not recognized.
  static bool is_boundary(std::string_view name) {
    (void)name;
    return false;
  }

  static std::string_view strip_io_prefix(std::string_view name) { return name; }
};

// Plugin registration lives in upass_coalescer.cpp.
