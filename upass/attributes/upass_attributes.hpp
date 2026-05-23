//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "const.hpp"
#include "upass_attributes_handler.hpp"
#include "upass_core.hpp"

// uPass_attributes — Pyrope attribute pass.
//
// Implements the multi-phase plan described in attribute_todo.md. Phase 1
// (sticky-attribute propagation) is wired here first; the remaining phases
// (3 aggregate reads, 5 category-A consumption, 6 category-B wiring,
// 7 pass-through hints/unknown) plug into the same handler registry as work
// progresses.
//
// Per attribute_todo.md §3, the pass participates in the normal upass node
// walk — there is no separate fixed-point loop. For every node visited by
// the runner, this pass updates its side state and lets the runner emit /
// rewrite / drop the node.
struct uPass_attributes : public upass::uPass {
public:
  uPass_attributes(std::shared_ptr<upass::Lnast_manager>& lm);
  ~uPass_attributes() override = default;

  void begin_iteration() override;
  void end_run() override;

  // Sticky propagation needs a hook at every assignment-shaped op so it can
  // observe RHS refs and mark the destination. Phase 1 only needs assign +
  // expression ops; later phases will extend this list.
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
  void process_ne() override;
  void process_eq() override;
  void process_lt() override;
  void process_le() override;
  void process_gt() override;
  void process_ge() override;
  void process_sext() override;
  void process_get_mask() override;
  void process_set_mask() override;

  // Attribute set/get nodes are dispatched to per-attribute handlers.
  void process_attr_set() override;
  void process_attr_get() override;

  // Phase 2 hooks — track type info, range bounds, value derivations.
  void process_type_spec() override;
  void process_range() override;

  // Phase 3 hooks — aggregate tuple-attribute resolution. Track tuple shape
  // (field count + field-name list) so .[size] / .[bits] / .[typename] / .[key]
  // and category-D aggregate→field inheritance can resolve before Phase 4
  // flattens the tuple away.
  void process_tuple_add() override;
  void process_tuple_concat() override;
  void process_tuple_set() override;
  void process_tuple_get() override;

  // Control-flow hooks for sticky control taint and runtime-join OR-merge.
  void process_if() override;
  void process_stmts() override;
  void process_stmts_post() override;

  // Phase 2 — fold the attr_get destination tmp to its computed Const so
  // the runner's emit-time substitution picks it up and downstream passes
  // (verifier cassert, constprop eq/ne via runner_fold_fn fallback) see the
  // resolved value without an extra iteration.
  std::optional<Const> fold_ref(std::string_view name) override;
  bool                 overrides_fold_ref() const override { return true; }

  // Read-side accessor for tests / cross-handler queries.
  upass::attributes::Handler_registry& registry() { return reg; }

  // Bucket name in flight while a per-name dispatch is active. Handlers can
  // read it inside on_attr_set / on_attr_get so they don't need to re-derive
  // the canonical name from the raw attribute text. Empty when no per-name
  // dispatch is active.
  const std::string& current_dispatch_bucket() const { return dispatch_bucket; }

private:
  upass::attributes::Handler_registry reg;

  // Internal helpers (impl in .cpp).
  // Walk current op-node's children, splitting LHS from RHS refs. Returns
  // the LHS name and a vector of every `ref` operand text on the RHS.
  // is_alias is set true when the op is an `assign` whose single RHS
  // operand is also a ref (direct alias semantics).
  // rhs_refs hold string_views into LNAST node text (mmap_lib::str storage,
  // stable for the pass lifetime). InlinedVector keeps the typical ≤4-operand
  // case off the heap. Avoid storing references to view.lhs in long-lived
  // state — keep it owned.
  struct Op_view {
    std::string                              lhs;
    absl::InlinedVector<std::string_view, 4> rhs_refs;
    bool                                     is_alias{false};
  };
  Op_view scan_op();

  // Dispatch helper: route to every registered handler that claims the
  // attribute name `attr_name`. Phase 1 only the sticky pattern is active.
  void dispatch_attr_set(std::string_view attr_name, std::string_view lhs, std::string_view value_text);
  void dispatch_attr_get(std::string_view attr_name, std::string_view dst, std::string_view base);

  // Convenience hooks invoked by every assignment-shaped op.
  void on_assign_like(bool is_assign_node);

  // Per-name dispatch context (see current_dispatch_bucket()).
  std::string dispatch_bucket;

  // Pending if-arm conds, keyed by the stmts node's class index. Populated
  // in process_if before the runner descends into the body, consumed by
  // process_stmts when each arm's stmts node is entered. Stored as owned
  // strings because the LNAST text references can move/invalidate as the
  // runner mutates the staging tree, and the arm body walk happens after
  // process_if returns.
  struct Pending_arm {
    std::vector<std::string>                         cond_refs;
    std::vector<std::pair<std::string, std::string>> cond_attr_reads;
  };
  std::map<uint64_t, Pending_arm> pending_arms;
  // Stack of stmts nids whose on_if_arm_enter was fired and is awaiting
  // its on_if_arm_exit. Used to balance the enter/exit pair across the
  // recursive process_stmts walk.
  std::vector<uint64_t> active_arm_stack;

  // Canonicalize ref text before using it as a handler key.
  static std::string normalize_name(std::string_view name);

  // ── Phase 2 — attribute-read evaluation ────────────────────────────────────
  //
  // Side state owned by the pass (rather than a single handler) because
  // multiple kinds of dispatch read/write the same maps:
  //   * process_attr_set populates attr_set_values (and the type / range /
  //     comptime sub-maps that drive derived reads).
  //   * process_type_spec / process_range populate the structural data the
  //     attr_get evaluator needs before any read fires.
  //   * fold_ref reads tmp_fold to substitute the attr_get destination at
  //     every downstream consumer (cassert via runner_fold_fn, eq/ne via
  //     constprop's runner_fold_fn fallback).
public:
  // Storage classification recorded by the `type` decl-attr that prp2lnast
  // emits (`mut`/`const`/`reg`/`await`).
  enum class Decl_kind : uint8_t { unknown, mut_kind, const_kind, reg_kind, await_kind };

  // Numeric type carried by a type_spec node. Width 0 means the declaration
  // omitted the bit count (e.g. `int` / `uint`); concrete widths populate
  // bits via the trailing const child of prim_type_uint/sint.
  enum class Numeric_kind : uint8_t { none, unsigned_int, signed_int, boolean, string };
  struct Type_info {
    Decl_kind    decl{Decl_kind::unknown};
    Numeric_kind kind{Numeric_kind::none};
    uint32_t     bits{0};
    bool         is_comptime{false};
  };

  // Look up explicitly-stored attribute values (set by attr_set). Returns
  // nullopt when no entry exists for that (var, attr) pair.
  std::optional<Const> lookup_attr_value(std::string_view var, std::string_view attr) const;

  // Type info recorded by process_type_spec + the `type` and `comptime`
  // decl-attrs. Returns nullopt when nothing is known.
  const Type_info* lookup_type_info(std::string_view var) const;

  // Range bounds previously recorded by process_range, keyed by the range
  // op's destination tmp ref (so attr_set with a `range` value pointing to
  // that tmp can lower to max/min).
  std::optional<std::pair<Const, Const>> lookup_range(std::string_view tmp) const;

  // Ref-text value recorded for an attr_set whose value was a ref (e.g.
  // `attr_set d "range" tmp_1`). Used to chain into range_bounds for
  // max/min derivation.
  std::optional<std::string> lookup_attr_ref(std::string_view var, std::string_view attr) const;

private:
  std::map<std::string, std::map<std::string, Const>>       attr_set_values;
  std::map<std::string, std::map<std::string, std::string>> attr_set_refs;  // (var, attr) → ref-text value
  // type_info_map is on the hot record_assign() path — flat_hash_map gives
  // heterogeneous string_view lookup so the per-op probe doesn't allocate.
  // (The other maps remain std::map because their iteration order is
  // observable via tuple shape / aggregate inheritance code paths.)
  absl::flat_hash_map<std::string, Type_info>               type_info_map;
  std::map<std::string, std::pair<Const, Const>>            range_bounds;  // start, end keyed by tmp
  std::map<std::string, Const>                              tmp_fold;      // attr_get dst → folded Const

public:
  // ── Phase 3 — tuple-shape side state ────────────────────────────────────
  //
  // Tracked per tuple-typed variable so aggregate `.[size]` / `.[bits]` /
  // `.[typename]` / `.[key]` reads can resolve before Phase 4 flattens the
  // tuple away. Field list is in source order; `name` is empty for unnamed
  // positional entries.
  struct Tuple_field {
    std::string positional;  // "0", "1", ...
    std::string name;        // "a", "xx", or empty for unnamed positional
    bool        operator==(const Tuple_field& other) const { return positional == other.positional && name == other.name; }
  };
  struct Tuple_shape {
    std::vector<Tuple_field> fields;
    bool                     from_range{false};  // size derived from range_bounds[var]
  };

  // tuple_get destination tmp → resolution info. Lets later attr_get see what
  // the tmp logically points at so cat-D inheritance and `.[key]` work.
  struct Get_alias {
    std::string base;        // canonical aggregate var (e.g. "t" or "___1")
    std::string field_key;   // "0", "xx", ...
    std::string field_name;  // for `.[key]` reads — the source name at this slot
  };

  bool                 is_tuple(std::string_view var) const;
  const Tuple_shape*   lookup_tuple_shape(std::string_view var) const;
  const Get_alias*     lookup_get_alias(std::string_view tmp) const;
  std::optional<Const> derive_aggregate_size(std::string_view base) const;
  std::optional<Const> derive_aggregate_bits(std::string_view base) const;
  std::optional<Const> derive_aggregate_typename(std::string_view base, std::string_view base_text) const;
  std::optional<Const> derive_aggregate_key(std::string_view base, std::string_view base_text) const;

  // Cat-D inheritance: walk get-alias and shape-source aliases looking for
  // an explicit attr value, then fall back to the parent aggregate's value.
  std::optional<Const> lookup_attr_with_inheritance(std::string_view base, std::string_view attr) const;

  // Built-in attribute names that have dedicated semantics (do NOT participate
  // in generic cat-D aggregate→field inheritance).
  static bool is_builtin_attr(std::string_view name);

  // ── Phase 4 — tuple expansion / array lowering ─────────────────────────
  //
  // The structural lowering (tuple_add → field-vars, mixed-type split, multi-D
  // flatten) is constprop's job (upass.md §Slice 6). Phase 4's *attribute*
  // responsibility is "user-attr migration" — when fields/aliases come into
  // existence, the cat-D aggregate attrs must follow them so that LGraph
  // generation still sees the annotation after the tuple shape is gone, and
  // comptime reads on the new aliased name continue to resolve.
  //
  // Two materialization triggers:
  //   1. process_tuple_add / on_assign_like (alias branch): when a name
  //      acquires a tuple shape, walk its known cat-D attrs and write each to
  //      every field's flattened name (`base.fieldname`) unless the field
  //      already has an explicit override.
  //   2. on_assign_like (alias branch) with rhs carrying a tuple_get_alias:
  //      copy the alias to the lhs so a later `lhs.[attr]` chains through to
  //      the underlying aggregate's inheritance lookup. This is the
  //      `const v = t.a` case: v inherits poison from t.a → t.

  // Public so phase4 helpers (and future tests) can drive materialization.
  void migrate_aggregate_attrs_to_fields(std::string_view base);
  void migrate_alias(std::string_view lhs, std::string_view rhs);

  // ── Phase 5 — overflow-policy state ───────────────────────────────────
  //
  // wrap / saturate as declaration attributes set a persistent narrowing
  // policy on the target variable; subsequent assignments are narrowed in
  // place via tmp_fold so consumers (verifier cassert, constprop's eq/ne
  // fallback) see the post-narrowed value through runner_fold_fn. The
  // statement-level form (`wrap x = ...` / `sat x = ...`) lowers to the
  // same attr_set but is emitted *after* the assign — distinguishing
  // declaration vs. statement is "did this var already have an assign?"
  // The statement-level form narrows the in-flight value and leaves no
  // sticky attribute on the variable.
  bool has_wrap_policy(std::string_view var) const { return wrap_policy.contains(var); }
  bool has_sat_policy(std::string_view var) const { return sat_policy.contains(var); }
  bool was_assigned(std::string_view var) const { return assigned_once.contains(var); }
  void set_wrap_policy(std::string_view var) { wrap_policy.emplace(var); }
  void set_sat_policy(std::string_view var) { sat_policy.emplace(var); }

  // Resolve the value being narrowed (either the LHS's last-stored value if
  // it's already been assigned, or the RHS we are currently emitting) and
  // overwrite tmp_fold[lhs] with the policy-narrowed result.
  void apply_narrowing(std::string_view lhs, bool is_wrap, bool is_sat);

  // Erase a stored attr value (used by the wrap/sat handler to drop the
  // per-statement attr — process_attr_set always pre-records, but
  // statement-level wrap/sat must not leave a sticky entry).
  void erase_attr_value(std::string_view var, std::string_view attr) {
    auto it = attr_set_values.find(std::string{var});
    if (it == attr_set_values.end()) {
      return;
    }
    it->second.erase(std::string{attr});
    if (it->second.empty()) {
      attr_set_values.erase(it);
    }
  }

  // Fold a value through the wrap/sat policy on `lhs` if any. Returns the
  // possibly-narrowed value; returns the input untouched when no policy is
  // active or type info is missing.
  Const narrow_for_lhs(std::string_view lhs, const Const& v) const;

  // ── Phase 5 — const single-assign tracking ─────────────────────────────
  //
  // A `const` declaration may receive at most one non-nil assignment per
  // cycle. record_assign() bumps the counter for the LHS; the second non-
  // nil assignment to a const-marked var raises upass::error.
  void record_assign(std::string_view lhs, bool rhs_is_nil);

private:
  std::map<std::string, Tuple_shape> tuple_shapes;     // var → shape
  std::map<std::string, Get_alias>   tuple_get_alias;  // tmp → resolved alias
  std::map<std::string, std::string> shape_source;     // var → source-tmp from `assign var src` (chained)
  std::map<std::string, std::string> direct_alias;     // Phase 4 — lhs → rhs for direct-ref `assign` aliases

  // Phase 5 — overflow policy + assignment tracking. flat_hash_* gives
  // heterogeneous string_view lookup and O(1) inserts; assigned_once grows
  // to ~one entry per assignment on bulk workloads, so the std::set's
  // O(log N) per-insert allocator pressure was a visible bottleneck.
  absl::flat_hash_set<std::string>      wrap_policy;
  absl::flat_hash_set<std::string>      sat_policy;
  absl::flat_hash_set<std::string>      assigned_once;       // any non-nil assign happened
  absl::flat_hash_map<std::string, int> const_assign_count;  // for const single-assign check

  // Phase 2 evaluator: compute the attribute's value when possible, store it
  // in tmp_fold[dst] so downstream reads pick it up. base_text is the raw
  // text of the base ref (before normalize_name); base is its normalized
  // form. attr is the attribute name (lower-case identifier).
  void evaluate_attr_get(std::string_view dst, std::string_view base_text, std::string_view base, std::string_view attr);

  // Helpers used by evaluate_attr_get.
  std::optional<Const> derive_max(std::string_view base) const;
  std::optional<Const> derive_min(std::string_view base) const;
  std::optional<Const> derive_bits(std::string_view base, std::string_view variant) const;  // "bits"/"ubits"/"sbits"
  std::optional<Const> derive_comptime(std::string_view base, std::string_view base_text) const;

  // Best-effort comptime value for `var` — consults runner_fold_fn (which
  // aggregates every pass's fold_ref) and our own tmp_fold, returning the
  // first foldable answer.
  std::optional<Const> resolve_value(std::string_view var) const;
};

// Plugin registration lives in upass_attributes.cpp.
