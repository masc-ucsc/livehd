//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "absl/container/flat_hash_map.h"
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

  // Record `mut`-declared names (mirrors constprop's process_declare). A
  // comptime-known write to a `mut` var is NOT dropped by constprop's
  // classify_statement (the task-2u guard keeps it), so the coalescer must
  // own its dead-store elimination: park comptime mut writes too, drop the
  // superseded ones, and keep (never comptime-drop) the surviving last write.
  void process_declare() override;

  // Drop-candidate op-nodes that the coalescer parks. Mirrors the A_OP set in
  // the runner's switch, restricted to scalar-result ops where parking is
  // safe. Tuple/attr/func ops carry shape/side-effect semantics and are
  // intentionally left to the runner's normal emit path.
  void        process_assign() override { handle_op(); }
  upass::Vote process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    if (src.size() <= 1) {
      process_assign();
    } else {
      process_tuple_set();
    }
    return consume_park_vote();
  }
  upass::Vote process_plus(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_minus(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_mult(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_div(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_mod(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_shl(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_sra(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_bit_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_bit_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_bit_not(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_bit_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_log_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_log_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_log_not(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_red_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_red_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_red_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_popcount(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_sext(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_get_mask(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_set_mask(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_ne(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_eq(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_lt(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_le(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_gt(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  upass::Vote process_ge(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }
  // `is` is a pure scalar (bool) type comparison — a drop-candidate in the
  // runner switch, no side effect, no bundle/tuple state. Park it like the
  // other scalar comparisons so a dead or DSE'd `is` result is dropped at
  // flush (const-known) instead of emitted-then-swept (1d hybrid B).
  upass::Vote process_is(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    handle_op();
    return consume_park_vote();
  }

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
  // 1i: flush parked writes before an inline source-swap so their src nids
  // are still relative to the active read tree (see uPass::flush_deferred).
  void flush_deferred() override { flush_all(); }

  // Cassert reads its operand and is treated as a barrier — flush so the
  // operand reflects whatever value the parked producer would have emitted.
  void process_cassert() override { flush_all(); }

  // Verbatim ops that touch tuple state — flush as a barrier for the same
  // reason. Bundle aliasing is hard to reason about across deferred writes.
  void process_tuple_set() override { flush_all(); }
  upass::Vote process_tuple_add(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    flush_all();
    return upass::Vote::keep;
  }
  void process_tuple_get() override { flush_all(); }
  upass::Vote process_tuple_concat(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    (void)src;
    flush_all();
    return upass::Vote::keep;
  }
  void process_attr_set() override { flush_all(); }
  void process_attr_get() override { flush_all(); }

  // The park decision rides the push VOTE (one-shot flag set by
  // handle_op, consumed by the dispatching hook's return value).
  upass::Vote consume_park_vote() {
    if (parked_current_stmt) {
      parked_current_stmt = false;
      return upass::Vote::drop;
    }
    return upass::Vote::keep;
  }

  void set_options(const upass::Options_map& opts) override;

  // Counters surfaced for tests / diagnostics.
  std::size_t pending_count() const { return pending.size(); }
  std::size_t parked_total() const { return stat_parked; }
  std::size_t dse_dropped_total() const { return stat_dse_dropped; }
  std::size_t flushed_total() const { return stat_flushed; }

private:
  // Source LNAST nid of a parked stmt, keyed by LHS name. flush_all sorts
  // keys before flushing so end-of-scope flush has a stable, deterministic
  // order across runs even though the hash map's own iteration is
  // unspecified. flat_hash_map with absl's transparent string_hash gives us
  // heterogeneous lookup so the hot handle_op path can probe with a
  // string_view (no per-find std::string allocation).
  absl::flat_hash_map<std::string, Lnast_nid> pending;

  // `mut`-declared names seen so far (see process_declare). A write to one of
  // these is parked / DSE'd even when its value is comptime-known, and its
  // surviving store is emitted (not comptime-dropped) at flush.
  std::unordered_set<std::string> mut_decl_names;

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

  // True when `name` resolves to a fully-known compile-time constant via the
  // runner's fold seam (concrete Dlop, no unknowns/invalid). Such a read is
  // served by fold_ref at the consumer, so a parked store need not be flushed.
  bool is_comptime(std::string_view name) const;

  // Boundary metadata is structural; textual ref prefixes are not recognized.
  static bool is_boundary(std::string_view name) {
    (void)name;
    return false;
  }

  static std::string_view strip_io_prefix(std::string_view name) { return name; }
};

// Plugin registration lives in upass_coalescer.cpp.
