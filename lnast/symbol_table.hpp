//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "bundle.hpp"
#include "lconst.hpp"

class Symbol_table {
public:
  // Every new variable declared in scope is added
  enum class Scope_type { Function, Always, Conditional };
  struct Scope {
    Scope(Scope_type _type, std::string_view _func_id, std::string_view _scope) : type(_type), func_id(_func_id), scope(_scope) {}

    Scope_type                                                type;
    std::string                                               func_id;
    std::string                                               scope;  // 0.0.1 ...
    std::vector<std::string>                                  declared;
    absl::flat_hash_map<std::string, std::shared_ptr<Bundle>> varmap;  // field, value, path_scope
  };

  static inline Lconst invalid_lconst = Lconst::invalid();

  std::vector<Scope> stack;

  void                    function_scope(std::string_view func_id, std::shared_ptr<Bundle> inp_bundle = nullptr);  // input
  void                    always_scope();
  void                    conditional_scope();
  std::shared_ptr<Bundle> leave_scope();  // output bundle only when leaving function scope

  bool var(std::string_view key);

  bool mut(std::string_view key, std::shared_ptr<Bundle> bundle);
  bool mut(std::string_view key, const Lconst& trivial);

  bool set(std::string_view key, std::shared_ptr<Bundle> bundle);
  bool set(std::string_view key, const Lconst& trivial);

  bool let(std::string_view key, std::shared_ptr<Bundle> bundle);
  bool let(std::string_view key, const Lconst& trivial);

  bool has_trivial(std::string_view key) const;
  bool has_bundle(std::string_view key) const;
  bool has_known(std::string_view key) const { return has_trivial(key) || has_bundle(key); }

  /// Returns true iff `name` holds a concrete Lconst with no unknown bits.
  /// Used by constprop's classify_statement and fold_ref to decide whether a
  /// variable is fully resolved.
  bool is_known_const(std::string_view name) const;

  /// Slice-1 stand-in: true iff `name` starts with '#' (register prefix).
  /// Every call site migrates to a real ST-backed direction lookup once
  /// lnast_todo.md §12 lands; until then, the prefix IS the contract.
  bool is_reg(std::string_view name) const { return !name.empty() && name.front() == '#'; }

  /// Slice-1 stand-in: true iff `name` starts with '$' (input-port prefix).
  bool is_input(std::string_view name) const { return !name.empty() && name.front() == '$'; }

  /// Slice-1 stand-in: true iff `name` starts with '%' (output-port prefix).
  bool is_output(std::string_view name) const { return !name.empty() && name.front() == '%'; }

  // Lconst can be 0sb? or 123 or string or bool or nil or runtime (0sb? and runtime?)
  const Lconst&           get_trivial(std::string_view key) const;
  std::shared_ptr<Bundle> get_bundle(std::string_view key) const;

  void dump() const;

private:
  static std::pair<std::string_view, std::string_view> get_var_field(std::string_view key) {
    auto var   = Bundle::get_first_level(key);
    auto field = Bundle::get_all_but_first_level(key);
    if (field.empty()) {
      field = "0";
    }

    return std::make_pair(var, field);
  }
};
