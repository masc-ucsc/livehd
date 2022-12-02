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
#include "str_tools.hpp"

// struct eprp_casecmp_str : public std::binary_function<const std::string &, const std::string &, bool> {
// bool operator()(std::string_view lhs, std::string_view rhs) const { return str_tools::to_lower(lhs) < str_tools::to_lower(rhs); }
//};

struct eprp_casecmp_str {
  struct nocase_compare {
    bool operator()(const unsigned char &c1, const unsigned char &c2) const { return std::tolower(c1) < std::tolower(c2); }
  };
  bool operator()(const std::string &s1, const std::string &s2) const {
    return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), nocase_compare());
  }
};

class Lgraph;

class Eprp_var {
public:
  using Eprp_dict   = absl::flat_hash_map<std::string, std::string>;
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
  void add(std::string_view name, std::string_view value);
  void replace(const Eprp_var::Eprp_lnasts &lns);
  void replace(const std::shared_ptr<Lnast> &lnast_old, std::shared_ptr<Lnast> &lnast_new);

  void delete_label(std::string_view name);

  [[nodiscard]] bool has_label(std::string_view name) const { return dict.find(name) != dict.end(); };

  [[nodiscard]] std::string_view get(std::string_view name, std::string_view default_value = "") const;

  void clear() {
    dict.clear();
    lgs.clear();
    lnasts.clear();
  }

  [[nodiscard]] bool empty() const { return dict.empty() && lgs.empty(); }
};
