//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "bundle.hpp"
#include "const.hpp"

class Symbol_table {
public:
  // Function: hard barrier — name lookup never crosses (callee can't see caller).
  // Block:    transparent — bare-`x = …` walks outward to find the nearest mut.
  // Always / Conditional: legacy categories used by hardware-style scoping;
  //   treated as transparent walk-through, same as Block.
  enum class Scope_type { Function, Block, Always, Conditional };
  struct Scope {
    Scope(Scope_type _type, std::string_view _func_id, std::string_view _scope) : type(_type), func_id(_func_id), scope(_scope) {}

    Scope_type                                                type;
    std::string                                               func_id;
    std::string                                               scope;  // 0.0.1 ...
    std::vector<std::string>                                  declared;
    absl::flat_hash_map<std::string, std::shared_ptr<Bundle>> varmap;  // field, value, path_scope
    Scope*                                                    parent{nullptr};
    // Set by the caller (constprop's process_stmts) when this scope is the
    // body of an if-arm whose condition is not a comptime-known true/false.
    // On leave_scope, every var listed in modified_under_uncertainty is
    // invalidated in its declaring scope so the unknown side-effects of the
    // arm don't leak out. See record_uncertain_modification().
    bool                                                      uncertain_cond{false};
    std::vector<std::string>                                  modified_under_uncertainty;
  };

  static inline Const invalid_lconst{};  // default Dlop is Type::Invalid

  Symbol_table()                               = default;
  Symbol_table(const Symbol_table&)            = delete;
  Symbol_table& operator=(const Symbol_table&) = delete;

  // Active scope chain: innermost at back(). Pointers refer into scope_storage.
  std::vector<Scope*> stack;

  void                    function_scope(std::string_view func_id);
  void                    always_scope();
  void                    conditional_scope();
  // Push a block scope keyed by `key` (e.g. an LNAST nid hash). Re-entering
  // the same `key` on a subsequent iteration restores the same Scope object,
  // so per-iteration state (declarations, values) persists across the upass
  // fixed-point loop. Returns the Scope that was pushed.
  Scope*                  block_scope(uint64_t key);
  std::shared_ptr<Bundle> leave_scope();  // output bundle only when leaving function scope

  // Mark the current (innermost) scope as the body of an uncertain-cond
  // if-arm. Caller is constprop's process_stmts; the runner notifies it via
  // notify_uncertain_arm before descending into the arm. On leave_scope of
  // an uncertain scope, every var modified inside is invalidated in its
  // declaring scope.
  void mark_current_uncertain();

  // True when any active scope is an uncertain if-arm — i.e. the statement
  // being processed is not guaranteed to execute. 2d-reg: a RUNTIME-rhs
  // write under uncertainty must invalidate the lhs's stale comptime value
  // (comptime writes get this via record_uncertain_modification; runtime
  // writes never touch the varmap, so the caller checks this explicitly).
  [[nodiscard]] bool in_uncertain_scope() const {
    for (const auto* s : stack) {
      if (s->uncertain_cond) {
        return true;
      }
    }
    return false;
  }

  bool var(std::string_view key);

  bool mut(std::string_view key, std::shared_ptr<Bundle> bundle);
  bool mut(std::string_view key, const Const& trivial);
  bool mut(std::string_view key, const spool_ptr<Dlop>& trivial) { return mut(key, *trivial); }

  bool set(std::string_view key, std::shared_ptr<Bundle> bundle);
  bool set(std::string_view key, const Const& trivial);
  bool set(std::string_view key, const spool_ptr<Dlop>& trivial) { return set(key, *trivial); }

  bool let(std::string_view key, std::shared_ptr<Bundle> bundle);
  bool let(std::string_view key, const Const& trivial);

  bool has_trivial(std::string_view key) const;
  bool has_bundle(std::string_view key) const;
  bool has_known(std::string_view key) const { return has_trivial(key) || has_bundle(key); }

  // True iff `name` is declared in any reachable scope (walks the parent
  // chain, stopping at and including the nearest Function-typed scope).
  // The eq path uses this to distinguish "never declared anywhere" — which
  // folds to nil — from "declared but unresolved" (stays unfolded).
  bool is_declared(std::string_view key) const;

  /// Returns true iff `name` holds a concrete Const with no unknown bits.
  bool is_known_const(std::string_view name) const;

  // Const can be 0sb? or 123 or string or bool or nil or runtime (0sb? and runtime?)
  const Const&            get_trivial(std::string_view key) const;
  std::shared_ptr<Bundle> get_bundle(std::string_view key) const;
  // COW variant for in-place mutation: un-shares the varmap slot before
  // returning it, so writes don't leak into whole-bundle-assignment aliases
  // (`p2 = p1` stores the same pointer). Bare variable names only.
  std::shared_ptr<Bundle> get_bundle_for_write(std::string_view var);

  void dump() const;

private:
  // Owns every scope ever created. deque keeps emplaced pointers stable so
  // `stack` and `block_scope_index` can hold raw pointers safely.
  std::deque<Scope> scope_storage;

  // Stable mapping from caller-supplied scope key (LNAST nid hash) to the
  // Scope object that represents that block. Re-pushing the same key on a
  // later iteration recovers the same Scope.
  absl::flat_hash_map<uint64_t, Scope*> block_scope_index;

  // Walks the active stack from innermost outward, stopping at and including
  // the nearest Function scope (callee bodies cannot see caller variables).
  // Returns the first scope whose varmap contains `var`, or nullptr.
  Scope*       find_decl_scope(std::string_view var);
  const Scope* find_decl_scope(std::string_view var) const;

  // If any active scope was marked uncertain via mark_current_uncertain,
  // record `name` on the innermost such scope so leave_scope can invalidate
  // it once the arm exits. Tmps (___N) are skipped — they're SSA values
  // anchored at the function scope, not user-visible state.
  void record_uncertain_modification(std::string_view name);

  static std::pair<std::string_view, std::string_view> get_var_field(std::string_view key) {
    auto var   = Bundle::get_first_level(key);
    auto field = Bundle::get_all_but_first_level(key);
    if (field.empty()) {
      field = "0";
    }

    return std::make_pair(var, field);
  }
};
