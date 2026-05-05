//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lconst.hpp"
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

  // Phase 2 — fold the attr_get destination tmp to its computed Lconst so
  // the runner's emit-time substitution picks it up and downstream passes
  // (verifier cassert, constprop eq/ne via runner_fold_fn fallback) see the
  // resolved value without an extra iteration.
  std::optional<Lconst> fold_ref(std::string_view name) override;

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
  struct Op_view {
    std::string              lhs;
    std::vector<std::string> rhs_refs;
    bool                     is_alias{false};
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
    std::vector<std::string>                       cond_refs;
    std::vector<std::pair<std::string, std::string>> cond_attr_reads;
  };
  std::map<uint64_t, Pending_arm> pending_arms;
  // Stack of stmts nids whose on_if_arm_enter was fired and is awaiting
  // its on_if_arm_exit. Used to balance the enter/exit pair across the
  // recursive process_stmts walk.
  std::vector<uint64_t> active_arm_stack;

  // Strip `%` / `$` IO direction prefixes from a name so handler buckets
  // see the same identifier whether the LNAST happens to carry direction
  // sigils on a particular reference or not.
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
  std::optional<Lconst> lookup_attr_value(std::string_view var, std::string_view attr) const;

  // Type info recorded by process_type_spec + the `type` and `comptime`
  // decl-attrs. Returns nullopt when nothing is known.
  const Type_info* lookup_type_info(std::string_view var) const;

  // Range bounds previously recorded by process_range, keyed by the range
  // op's destination tmp ref (so attr_set with a `range` value pointing to
  // that tmp can lower to max/min).
  std::optional<std::pair<Lconst, Lconst>> lookup_range(std::string_view tmp) const;

  // Ref-text value recorded for an attr_set whose value was a ref (e.g.
  // `attr_set d "range" tmp_1`). Used to chain into range_bounds for
  // max/min derivation.
  std::optional<std::string> lookup_attr_ref(std::string_view var, std::string_view attr) const;

private:
  std::map<std::string, std::map<std::string, Lconst>>      attr_set_values;
  std::map<std::string, std::map<std::string, std::string>> attr_set_refs;  // (var, attr) → ref-text value
  std::map<std::string, Type_info>                          type_info_map;
  std::map<std::string, std::pair<Lconst, Lconst>>          range_bounds;  // start, end keyed by tmp
  std::map<std::string, Lconst>                             tmp_fold;      // attr_get dst → folded Lconst

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
    bool        operator==(const Tuple_field& other) const {
      return positional == other.positional && name == other.name;
    }
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

  bool                                  is_tuple(std::string_view var) const;
  const Tuple_shape*                    lookup_tuple_shape(std::string_view var) const;
  const Get_alias*                      lookup_get_alias(std::string_view tmp) const;
  std::optional<Lconst>                 derive_aggregate_size(std::string_view base) const;
  std::optional<Lconst>                 derive_aggregate_bits(std::string_view base) const;
  std::optional<Lconst>                 derive_aggregate_typename(std::string_view base, std::string_view base_text) const;
  std::optional<Lconst>                 derive_aggregate_key(std::string_view base, std::string_view base_text) const;

  // Cat-D inheritance: walk get-alias and shape-source aliases looking for
  // an explicit attr value, then fall back to the parent aggregate's value.
  std::optional<Lconst> lookup_attr_with_inheritance(std::string_view base, std::string_view attr) const;

  // Built-in attribute names that have dedicated semantics (do NOT participate
  // in generic cat-D aggregate→field inheritance).
  static bool is_builtin_attr(std::string_view name);

private:
  std::map<std::string, Tuple_shape> tuple_shapes;     // var → shape
  std::map<std::string, Get_alias>   tuple_get_alias;  // tmp → resolved alias
  std::map<std::string, std::string> shape_source;     // var → source-tmp from `assign var src` (chained)

  // Phase 2 evaluator: compute the attribute's value when possible, store it
  // in tmp_fold[dst] so downstream reads pick it up. base_text is the raw
  // text of the base ref (before normalize_name); base is its normalized
  // form. attr is the attribute name (lower-case identifier).
  void evaluate_attr_get(std::string_view dst, std::string_view base_text, std::string_view base, std::string_view attr);

  // Helpers used by evaluate_attr_get.
  std::optional<Lconst> derive_max(std::string_view base) const;
  std::optional<Lconst> derive_min(std::string_view base) const;
  std::optional<Lconst> derive_bits(std::string_view base, std::string_view variant) const;  // "bits"/"ubits"/"sbits"
  std::optional<Lconst> derive_comptime(std::string_view base, std::string_view base_text) const;

  // Best-effort comptime value for `var` — consults runner_fold_fn (which
  // aggregates every pass's fold_ref) and our own tmp_fold, returning the
  // first foldable answer.
  std::optional<Lconst> resolve_value(std::string_view var) const;
};

// Plugin registration lives in upass_attributes.cpp.
