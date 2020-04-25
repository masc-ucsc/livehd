//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <strings.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <memory>

#include "absl/container/flat_hash_map.h"

#include "lnast.hpp"

struct eprp_casecmp_str : public std::binary_function<const std::string, const std::string, bool> {
  bool operator()(const std::string &lhs, const std::string &rhs) const { return strcasecmp(lhs.c_str(), rhs.c_str()) < 0; }
};

class LGraph;

class Eprp_var {
public:
  using Eprp_dict   = absl::flat_hash_map<const std::string, std::string>;
  using Eprp_lgs    = std::vector<LGraph *>;
  using Eprp_lnasts = std::vector<std::shared_ptr<Lnast> >;

  Eprp_dict   dict;
  Eprp_lgs    lgs;
  Eprp_lnasts lnasts;

  Eprp_var() {
    dict.clear();
    lgs.clear();
    lnasts.clear();
  }

  Eprp_var(const Eprp_dict &_dict) : dict(_dict) { }
  Eprp_var(const Eprp_lgs &_lgs) : lgs(_lgs) { }

  void add(const Eprp_dict &_dict);
  void add(const Eprp_lgs &_lgs);
  void add(const Eprp_var &_var);

  void add(LGraph *lg);
  void add(std::unique_ptr<Lnast> lnast);
  void add(const std::string &name, std::string_view value);
  template<typename Str>
    std::enable_if_t<std::is_convertible_v<std::string_view, Str>, void> add(const Str &name, std::string_view value) {
      add(std::string(name), value);
  }

  void delete_label(const std::string &name);

  bool             has_label(const std::string &name) const { return dict.find(name) != dict.end(); };
  std::string_view get(const std::string &name) const;
  // const std::string get(const std::string &name, const std::string &def_val) const;

  void clear() {
    dict.clear();
    lgs.clear();
    lnasts.clear();
  }

  bool empty() const { return dict.empty() && lgs.empty(); }
};
