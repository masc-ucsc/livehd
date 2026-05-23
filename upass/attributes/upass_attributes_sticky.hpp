//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "upass_attributes_handler.hpp"

// Phase 1 — sticky attribute propagation (see attribute_todo.md §Phase 1).
//
// Tracks `debug` / `_debug` and user `_*` attrs as monotonic per-name state.
// Once a sticky attr is acquired by a variable it cannot be cleared. Flows:
//   - direct alias: full attr set carries (sticky + non-sticky).
//   - expression result: sticky-only carries; non-sticky drop.
//   - control dependency: sticky condition taints assignments inside the arm.
//   - runtime if/else join: OR-merge across the live branches.
//   - comptime if/else: only the live branch contributes (Phase 2 folds the
//     condition; this handler sees only the surviving arm).
//
// Each sticky name is tracked in its own bucket. `debug` and `_debug` share a
// bucket per the spec.

namespace upass {
namespace attributes {

class Sticky_handler : public Attribute_handler {
public:
  Sticky_handler() = default;

  void begin_iteration(uPass_attributes& owner) override;
  void end_run(uPass_attributes& owner) override;

  void on_attr_set(uPass_attributes& owner, std::string_view lhs, std::string_view value_text) override;
  void on_attr_get(uPass_attributes& owner, std::string_view dst, std::string_view base) override;

  void on_alias_assign(uPass_attributes& owner, std::string_view lhs, std::string_view rhs) override;
  void on_expr_assign(uPass_attributes& owner, std::string_view lhs, std::span<const std::string_view> rhs_refs) override;

  void on_if_arm_enter(uPass_attributes& owner, std::span<const std::string_view> cond_refs,
                       std::span<const std::pair<std::string_view, std::string_view>> cond_attr_reads) override;
  void on_if_arm_exit(uPass_attributes& owner) override;

  // Pattern check: returns true for `debug`, `_debug`, and any name starting
  // with `_` (user sticky attr). Used by the registry to dispatch.
  static bool is_sticky_name(std::string_view name);

  // Canonicalize attr name for bucket lookup. `debug` and `_debug` collapse
  // to a shared bucket; user `_foo` keeps its own bucket.
  static std::string canonical_bucket(std::string_view name);

  // Test/diagnostic accessors. True iff `var` has acquired the sticky attr
  // identified by `bucket` at the current point of the walk.
  bool has_sticky(std::string_view var, std::string_view bucket) const;

  // Direct mark from the per-name dispatcher. Visible to uPass_attributes so
  // attr_set on a sticky bucket can mark the target without re-routing
  // through on_attr_set's value parsing.
  void mark(std::string_view var, std::string_view bucket);

  // True iff no sticky state is in scope — no var has acquired a bucket and
  // no control taint is active. Lets the on_assign_like fast-path elide its
  // operand walk when the pass is functionally inert (the common case on
  // bulk-arithmetic workloads with no `_*` attrs in flight).
  bool is_inert() const { return acquired.empty() && control_taint_stack.empty(); }

private:
  // Per-variable acquired sticky attrs. Monotonic — entries are added but
  // never removed (later clean assignments cannot clear a sticky).
  // flat_hash_map supports heterogeneous string_view lookup so the hot
  // merge_from / mark / has_sticky paths skip per-call std::string temps.
  absl::flat_hash_map<std::string, std::set<std::string>> acquired;

  // Per-arm control taint stack: each entry is the set of sticky bucket
  // names the current arm carries because its condition referenced sticky
  // state. Pushed by on_if_arm_enter, popped by on_if_arm_exit.
  std::vector<std::set<std::string>> control_taint_stack;

  void                  merge_from(std::string_view dst, std::string_view src);
  std::set<std::string> active_control_taint() const;  // OR of stack entries
};

}  // namespace attributes
}  // namespace upass
