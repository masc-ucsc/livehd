//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <strings.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "lnast.hpp"

struct eprp_casecmp_str : public std::binary_function<const mmap_lib::str, const mmap_lib::str, bool> {
  bool operator()(const mmap_lib::str &lhs, const mmap_lib::str &rhs) const { return lhs.to_lower() < rhs.to_lower(); }
};

class Lgraph;

class Eprp_var {
public:
  using Eprp_dict   = absl::flat_hash_map<mmap_lib::str, mmap_lib::str>;
  using Eprp_lgs    = std::vector<Lgraph *>;
  using Eprp_lnasts = std::vector<std::shared_ptr<Lnast> >;

  Eprp_dict   dict;
  Eprp_lgs    lgs;
  Eprp_lnasts lnasts;

  Eprp_var() {
    dict.clear();
    lgs.clear();
    lnasts.clear();
  }

  Eprp_var(const Eprp_dict &_dict) : dict(_dict) {}
  Eprp_var(const Eprp_lgs &_lgs) : lgs(_lgs) {}

  void add(const Eprp_dict &_dict);
  void add(const Eprp_lgs &_lgs);
  void add(const Eprp_var &_var);
  void add(Eprp_lnasts &_var);

  void add(Lgraph *lg);
  void add(std::unique_ptr<Lnast> lnast);
  void add(const std::shared_ptr<Lnast> &lnast);
  void add(const mmap_lib::str &name, const mmap_lib::str &value);
  void replace(const std::shared_ptr<Lnast> &lnast_old, std::shared_ptr<Lnast> &lnast_new);

#if 0
  template <typename Str>
  std::enable_if_t<std::is_convertible_v<mmap_lib::str_view, Str>, void> add(const Str &name, mmap_lib::str_view value) {
    add(mmap_lib::str(name), value);
  }
#endif

  void delete_label(const mmap_lib::str &name);

  bool             has_label(const mmap_lib::str &name) const { return dict.find(name) != dict.end(); };
  mmap_lib::str    get(const mmap_lib::str &name) const;

  void clear() {
    dict.clear();
    lgs.clear();
    lnasts.clear();
  }

  bool empty() const { return dict.empty() && lgs.empty(); }
};
