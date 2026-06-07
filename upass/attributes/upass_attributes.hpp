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
// Per-attribute semantics plug in via the Handler_registry (see
// upass_attributes_handler.hpp). The pass itself drives the LNAST walk and
// owns the side state every handler reads from / writes to. Concrete
// handlers live in sibling files:
//
//   * upass_attributes_sticky.cpp   — `_*` / `debug` sticky propagation
//   * upass_attributes_wrap_sat.cpp — category-A wrap / saturate / const
//   * upass_attributes_wiring.cpp   — category-B LGraph-wiring attrs
//   * upass_attributes_read.cpp     — `.[attr]` read evaluation
//   * upass_attributes_tuple.cpp    — aggregate (tuple/array) shape tracking
//   * upass_attributes_migrate.cpp  — per-field attr materialization
//
// The pass participates in the normal upass node walk — there is no
// separate fixed-point loop. For every node visited by the runner, this
// pass updates its side state and lets the runner emit / rewrite / drop
// the node.
struct uPass_attributes : public upass::uPass {
public:
  uPass_attributes(std::shared_ptr<upass::Lnast_manager>& lm);
  ~uPass_attributes() override = default;

  void begin_iteration() override;
  void end_run() override;

  // Sticky propagation needs a hook at every assignment-shaped op so it can
  // observe RHS refs and mark the destination.
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
  void process_is() override;
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

  // Type info, range bounds, value-derivation hooks (see
  // upass_attributes_read.cpp).
  void process_type_spec() override;
  // Task 1t — declare(var, TYPE, const(mode)[, value]) unifies the
  // attr_set(type)+attr_set(comptime)+type_spec declaration cluster.
  void process_declare() override;
  void process_range() override;

  // Task 1t — `wrap`/`sat` lower to a `wrap|sat(v=<value>, type=<lhs>)`
  // library call (not an attr_set/policy). Recognize the callee and narrow
  // the value to the lhs's declared type, publishing the result on the call's
  // dst tmp so the following `store(lhs, tmp)` alias-propagates it.
  void process_func_call() override;

  // Aggregate tuple-attribute resolution (see upass_attributes_tuple.cpp).
  // Track tuple shape (field count + field-name list) so .[size] / .[bits] /
  // .[typename] / .[key] and category-D aggregate→field inheritance can
  // resolve before downstream lowering flattens the tuple away.
  void process_tuple_add() override;
  void process_tuple_concat() override;
  void process_tuple_set() override;
  void process_tuple_get() override;

  // Control-flow hooks for sticky control taint and runtime-join OR-merge.
  void process_if() override;
  void process_stmts() override;
  void process_stmts_post() override;

  // Fold the attr_get destination tmp to its computed Const so the runner's
  // emit-time substitution picks it up and downstream passes (verifier
  // cassert, constprop eq/ne via runner_fold_fn fallback) see the resolved
  // value within the same walk. See upass_attributes_read.cpp.
  std::optional<Const> fold_ref(std::string_view name) override;
  bool                 overrides_fold_ref() const override { return true; }

  // Init-construction window: the runner's synthesized constructor stores
  // (type-defaults bind + ref-self write-back) are not user re-binds, so the
  // const single-bind tally in record_assign pauses while the window is open.
  void notify_init_construction_begin() override { ++init_construction_depth_; }
  void notify_init_construction_end() override { --init_construction_depth_; }

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
    // lhs is a string_view into LNAST's persistent attribute storage; valid
    // for the duration of the per-op process_*. Avoiding the std::string
    // copy here saves one heap allocation per arithmetic statement (and ≈50ns
    // per node on bulk-arithmetic workloads).
    std::string_view                         lhs;
    absl::InlinedVector<std::string_view, 4> rhs_refs;
    bool                                     is_alias{false};
  };
  Op_view scan_op();

  // Dispatch helper: route to every registered handler that claims the
  // attribute name `attr_name`.
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
  std::vector<uint64_t>           active_arm_stack;

  // Canonicalize ref text before using it as a handler key.
  static std::string normalize_name(std::string_view name);

  // ── Attribute-read evaluation (see upass_attributes_read.cpp) ─────────────
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
  enum class Decl_kind : uint8_t { unknown, mut_kind, const_kind, reg_kind, await_kind, type_kind };

  // Numeric type carried by a type_spec node. Width 0 means the declaration
  // omitted the bit count (e.g. `int` / `uint`); concrete widths populate
  // bits via the trailing const child of prim_type_uint/sint.
  enum class Numeric_kind : uint8_t { none, unsigned_int, signed_int, boolean, string };
  struct Type_info {
    Decl_kind            decl{Decl_kind::unknown};
    Numeric_kind         kind{Numeric_kind::none};
    uint32_t             bits{0};
    bool                 is_comptime{false};
    // True when an explicit `:type` annotation was processed for this
    // variable. Distinguishes `const a:int = 3` (annotated but unbounded —
    // size attrs read as nil per the typesystem redesign) from
    // `const b = 3` (no annotation — legacy value-based fallback).
    bool                 has_type_spec{false};
    // Task 1t — the integer `(max,min)` range carried by a `prim_type_int`
    // node (`int(max=,min=)` / `uint(bits=)` type-call). When set, these are
    // the single source of truth for max/min/bits/sign derivation. Either
    // may stay unset (unbounded): `int(max=3)` pins only `max`. uN/sN sugar
    // still flows through `kind`+`bits` (legacy path) until T5 consolidates
    // every integer onto `prim_type_int`.
    std::optional<Const> range_max;
    std::optional<Const> range_min;
  };

  // Look up explicitly-stored attribute values (set by attr_set). Returns
  // nullopt when no entry exists for that (var, attr) pair.
  std::optional<Const> lookup_attr_value(std::string_view var, std::string_view attr) const;

  // Type info recorded by process_type_spec + the `type` and `comptime`
  // decl-attrs. Returns nullopt when nothing is known.
  const Type_info* lookup_type_info(std::string_view var) const;

  // Shared-ST read (1i inliner): hand the runner a declared variable's integer
  // (max,min) range so it can re-type an untyped inlined parameter from the
  // actual at the call site. See uPass::provide_decl_type.
  std::optional<Decl_scalar_type> provide_decl_type(std::string_view name) override;
  bool                            overrides_shared_st() const override { return true; }

  // Task 1k — declared field type of a dotted path (`t1.a`) for the inliner's
  // typed-self `does`-check. Same lookup_type_info chase as provide_decl_type
  // but surfaces the scalar KIND too (and tolerates a missing range, so bool/
  // string fields still report their kind). See uPass::provide_field_type.
  std::optional<Field_decl_type> provide_field_type(std::string_view name) override;

  // Task 1k — declared storage class (mut/const/reg/type) so the inliner can
  // reject const/type bindings as `ref` actuals. See uPass::provide_decl_storage.
  Decl_storage provide_decl_storage(std::string_view name) override;

  // Range bounds previously recorded by process_range, keyed by the range
  // op's destination tmp ref (so attr_set with a `range` value pointing to
  // that tmp can lower to max/min).
  std::optional<std::pair<Const, Const>> lookup_range(std::string_view tmp) const;

  // Ref-text value recorded for an attr_set whose value was a ref (e.g.
  // `attr_set f "clock_pin" my_clk` — a runtime wire ref for LGraph wiring).
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
  // ── Tuple-shape side state (see upass_attributes_tuple.cpp) ──────────────
  //
  // Tracked per tuple-typed variable so aggregate `.[size]` / `.[bits]` /
  // `.[typename]` / `.[key]` reads can resolve before downstream lowering
  // flattens the tuple away. Field list is in source order; `name` is empty
  // for unnamed positional entries.
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

  // ── Per-field attribute migration (see upass_attributes_migrate.cpp) ─────
  //
  // The structural lowering (tuple_add → field-vars, mixed-type split, multi-D
  // flatten) is constprop's job (upass.md §Slice 6). The attribute pass's
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

  // Was `var` assigned a non-nil value at least once? Gates the unsigned
  // first-write coercion in on_assign_like.
  bool was_assigned(std::string_view var) const { return assigned_once.contains(var); }

  // Narrow a value to `type_src`'s declared type using the wrap (modulo) or
  // sat (clamp) rule. `type_src` is the variable whose type envelope to use
  // (the `type=` arg of a wrap/sat call). Returns the input untouched when
  // neither flag is set or type info is missing. Driven by process_func_call.
  Const narrow_for_lhs(std::string_view type_src, const Const& v, bool is_wrap, bool is_sat) const;

  // ── const single-assign tracking ──────────────────────────────────────────
  //
  // A `const` declaration may receive at most one non-nil assignment per
  // cycle. record_assign() bumps the counter for the LHS; the second non-
  // nil assignment to a const-marked var raises upass::error.
  void record_assign(std::string_view lhs, bool rhs_is_nil);

private:
  std::map<std::string, Tuple_shape> tuple_shapes;     // var → shape
  std::map<std::string, Get_alias>   tuple_get_alias;  // tmp → resolved alias
  std::map<std::string, std::string> shape_source;     // var → source-tmp from `assign var src` (chained)
  std::map<std::string, std::string> direct_alias;     // lhs → rhs for direct-ref `assign` aliases (migrate.cpp)

  // Assignment tracking (wrap_sat.cpp). flat_hash_* gives heterogeneous
  // string_view lookup and O(1) inserts; assigned_once grows to ~one entry per
  // assignment on bulk workloads, so the std::set's O(log N) per-insert
  // allocator pressure was a visible bottleneck.
  absl::flat_hash_set<std::string>      assigned_once;       // any non-nil assign happened
  absl::flat_hash_map<std::string, int> const_assign_count;  // for const single-assign check
  // >0 while the runner's init-construction window is open (see the
  // notify_init_construction_* overrides) — record_assign skips the const
  // single-bind tally for the synthesized constructor stores.
  int init_construction_depth_ = 0;

  // Read evaluator (read.cpp): compute the attribute's value when possible, store it
  // in tmp_fold[dst] so downstream reads pick it up. base_text is the raw
  // text of the base ref (before normalize_name); base is its normalized
  // form. attr is the attribute name (lower-case identifier).
  void evaluate_attr_get(std::string_view dst, std::string_view base_text, std::string_view base, std::string_view attr);

  // Helpers used by evaluate_attr_get.
  std::optional<Const> derive_max(std::string_view base) const;
  std::optional<Const> derive_min(std::string_view base) const;
  std::optional<Const> derive_bits(std::string_view base, std::string_view variant) const;  // "bits"/"ubits"/"sbits"
  std::optional<Const> derive_comptime(std::string_view base, std::string_view base_text) const;

  // Task 1t — read a scalar TYPE node at the cursor (prim_type_int/uint/sint/
  // boolean/string), filling kind/bits and the (max,min) range. The cursor is
  // expected to be positioned AT the type node and is left there (balanced).
  // `is_real_type` is set true for a concrete numeric/string type (so the
  // caller marks has_type_spec), false for none/unknown. Shared by
  // process_type_spec and process_declare.
  void read_scalar_type_at_cursor(Numeric_kind& kind, uint32_t& bits, std::optional<Const>& range_max,
                                  std::optional<Const>& range_min, bool& is_real_type);

  // Best-effort comptime value for `var` — consults runner_fold_fn (which
  // aggregates every pass's fold_ref) and our own tmp_fold, returning the
  // first foldable answer.
  std::optional<Const> resolve_value(std::string_view var) const;
};

// Plugin registration lives in upass_attributes.cpp.
