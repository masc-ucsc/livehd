//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

  // Control-flow hooks for sticky control taint and runtime-join OR-merge.
  void process_if() override;
  void process_stmts() override;
  void process_stmts_post() override;

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
};

// Plugin registration lives in upass_attributes.cpp.
