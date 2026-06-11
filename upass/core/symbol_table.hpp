//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "bundle.hpp"
#include "hlop/dlop.hpp"

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

  static inline Dlop invalid_lconst{};  // default Dlop is Type::Invalid

  Symbol_table()                               = default;
  Symbol_table(const Symbol_table&)            = delete;
  Symbol_table& operator=(const Symbol_table&) = delete;

  // Active scope chain: innermost at back(). Pointers refer into scope_storage.
  std::vector<Scope*> stack;

  void                    function_scope(std::string_view func_id);
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

  // True when `var` is declared in an ENCLOSING scope (not the innermost
  // active one) — i.e. writing it here mutates an outer variable from inside a
  // nested block (`if true { acc = … }`, a loop iteration). Such a write is NOT
  // SSA-versioned (block bodies are copied verbatim), so a RUNTIME-rhs write
  // must invalidate the lhs's stale comptime value just like the uncertain-arm
  // case — otherwise reads after the block fold to the pre-block constant.
  [[nodiscard]] bool is_enclosing_scope_var(std::string_view var) const {
    const auto* decl = find_decl_scope(var);
    return decl != nullptr && !stack.empty() && decl != stack.back();
  }

  bool var(std::string_view key);
  // Declaration pre-step binding: empty bundle, no "0" slot (see impl).
  bool declare_bare(std::string_view var);

  // Declared facts for a DOTTED path whose field entry does not
  // exist yet (`type_spec(inl1_ar.x, u3)` before the argument store). The
  // runner stashes at the bake, applies+erases at the field's first write;
  // readers (lookup_type_info_bundle) consult it for not-yet-applied paths.
  // Fact-only ENTRIES cannot be minted in the varmap — a valueless entry
  // reads as a runtime poison claim downstream — hence this side slot.
  struct Pending_decl {
    upass::Kind kind{upass::Kind::unknown};
    upass::Mode mode{upass::Mode::unknown};
    Dlop       decl_max;
    Dlop       decl_min;
    bool        comptime{false};
  };
  absl::flat_hash_map<std::string, Pending_decl> pending_decl_facts;

  // Extraction origin: tuple_get dst tmp → "src.field" path. The
  // runner's declare/type_spec bake back-flows per-field declared facts to
  // the SOURCE field through this (the typed-tuple-literal lowering types
  // the extraction tmp, not the field). Transient; cleared with the stash.
  absl::flat_hash_map<std::string, std::string> tget_origin;

  // Names whose nil initializer was SYNTHESIZED by the runner's
  // inliner (output seeds, untyped-param prologues). The typecheck rule
  // "an unset/nil scalar destination does not infer a tuple shape from a
  // tuple RHS" exempts these — the prologue legally binds a tuple over its
  // own nil seed. Transient; cleared with the other per-run state.
  absl::flat_hash_set<std::string> nil_seeded;

  // The strict comptime-fold read (ex runner_fold_fn / fold_ref):
  // a concrete (no-unknowns) value on a TRIVIAL-SCALAR binding. A
  // multi-entry tuple or named 1-tuple never inlines (position-0 truncation),
  // and unknown-carrying values never inline (LEC-breaking).
  std::optional<Dlop> known_const_scalar(std::string_view name) const {
    if (name.empty() || !is_known_const(name)) {
      return std::nullopt;
    }
    if (auto b = get_bundle(name); b && !b->is_trivial_scalar()) {
      return std::nullopt;
    }
    return get_trivial(name);
  }

  // Runtime tuple-slot refs (loop-migration Step 1): dst tuple →
  // slot → source ref name, for slots whose value is runtime (the bundle
  // stores null there). The runner rewrites `t[slot]` / `for x in t` into a
  // copy from the ref. Erased wholesale when the dst tuple is rebuilt.
  absl::flat_hash_map<std::string, std::map<std::string, std::string>> tuple_slot_ref;

  bool set(std::string_view key, std::shared_ptr<Bundle> bundle);
  bool set(std::string_view key, const Dlop& trivial);
  bool set(std::string_view key, const spool_ptr<Dlop>& trivial) { return set(key, *trivial); }

  bool has_trivial(std::string_view key) const;
  bool has_bundle(std::string_view key) const;
  bool has_known(std::string_view key) const { return has_trivial(key) || has_bundle(key); }

  /// Returns true iff `name` holds a concrete Dlop with no unknown bits.
  bool is_known_const(std::string_view name) const;

  // Dlop can be 0sb? or 123 or string or bool or nil or runtime (0sb? and runtime?)
  const Dlop&            get_trivial(std::string_view key) const;
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
  // the nearest Function scope. This is the WRITE/anchor variant —
  // a callee body cannot mutate caller variables. Reads use
  // find_decl_scope_read, which crosses the Function barrier (closure
  // capture of outer comptime consts).
  Scope*       find_decl_scope(std::string_view var);
  const Scope* find_decl_scope(std::string_view var) const;
  const Scope* find_decl_scope_read(std::string_view var) const;

public:
  // True when `var` is visible only ACROSS a Function barrier:
  // readable (closure capture) but not writable. A write to such a name is
  // a compile error at the caller (the runner/passes report it).

private:

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
