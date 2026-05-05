//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

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
  void notify_uncertain_arm_begin() override;
  void notify_uncertain_arm_end() override;

  // Tuple ops are touched by Phase 3/4; for Phase 1 they're no-ops.
  // Stubs left in for future expansion.
  // void process_tuple_set() override;
  // void process_tuple_get() override;
  // void process_tuple_add() override;
  // void process_tuple_concat() override;

  // Read-side accessor for tests / cross-handler queries.
  upass::attributes::Handler_registry& registry() { return reg; }

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
  void dispatch_attr_get(std::string_view attr_name, std::string_view lhs);

  // Convenience hooks invoked by every assignment-shaped op.
  void on_assign_like(bool is_expression);
};

// Plugin registration lives in upass_attributes.cpp.
