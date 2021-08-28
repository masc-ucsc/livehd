//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "absl/container/flat_hash_map.h"

#include "bundle.hpp"
#include "lconst.hpp"
#include "mmap_str.hpp"

class Symbol_table {
public:


  // Every new variable declared in scope is added
  enum class Scope_type { Function, Always, Conditional };
  struct Scope {
    Scope(Scope_type _type, const mmap_lib::str _func_id, const mmap_lib::str _scope)
      : type(_type)
       ,func_id(_func_id)
       ,scope(_scope) { }

    Scope_type                 type;
    mmap_lib::str              func_id;
    mmap_lib::str              scope;      // 0.0.1 ...
    std::vector<mmap_lib::str> declared;
  };

  absl::flat_hash_map<std::pair<mmap_lib::str, mmap_lib::str>, std::shared_ptr<Bundle>> varmap;  // field, value, path_scope

  std::vector<Scope> stack;

  void                    funcion_scope(mmap_lib::str func_id, std::shared_ptr<Bundle> inp_bundle=nullptr); // input
  void                    always_scope();
  void                    conditional_scope();
  std::shared_ptr<Bundle> leave_scope();  // output bundle only when leaving function scope

  bool var(mmap_lib::str key);

  bool mut(mmap_lib::str key, std::shared_ptr<Bundle> bundle);
  bool mut(mmap_lib::str key, const Lconst &trivial);

  bool set(mmap_lib::str key, std::shared_ptr<Bundle> bundle);
  bool set(mmap_lib::str key, const Lconst &trivial);

  bool let(mmap_lib::str key, std::shared_ptr<Bundle> bundle);
  bool let(mmap_lib::str key, const Lconst &trivial);

  bool has_trivial(mmap_lib::str key) const;
  bool has_bundle(mmap_lib::str key) const;
  bool has_known(mmap_lib::str key) const { return has_trivial(key) || has_bundle(key); }

  // Lconst can be 0sb? or 123 or string or bool or nil or runtime (0sb? and runtime?)
  Lconst                  get_trivial(mmap_lib::str key) const;
  std::shared_ptr<Bundle> get_bundle (mmap_lib::str key) const;

  void dump() const;

private:
  static std::pair<mmap_lib::str,mmap_lib::str> get_var_field(mmap_lib::str key) {
    auto var   = Bundle::get_first_level(key);
    auto field = Bundle::get_all_but_first_level(key);
    if (field.empty())
      field = "0"_str;

    return std::make_pair(var, field);
  }
};
