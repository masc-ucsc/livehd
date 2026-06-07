//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <strings.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "lnast.hpp"
#include "str_tools.hpp"

struct eprp_casecmp_str {
  struct nocase_compare {
    bool operator()(const unsigned char& c1, const unsigned char& c2) const { return std::tolower(c1) < std::tolower(c2); }
  };
  bool operator()(const std::string& s1, const std::string& s2) const {
    return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), nocase_compare());
  }
};

class Eprp_var {
public:
  using Eprp_dict   = absl::flat_hash_map<std::string, std::string>;
  using Eprp_graphs = std::vector<std::shared_ptr<hhds::Graph>>;
  using Eprp_lnasts = std::vector<std::shared_ptr<Lnast> >;

  Eprp_dict   dict;
  Eprp_dict   stage_dict;
  Eprp_graphs graphs;
  Eprp_lnasts lnasts;

  // Task 1m — unresolved live imports surfaced by pass.upass when invoked
  // with import_defer:1 (the kernel's iterate-until-converged loop consumes
  // them): (unit that hit the import, import string as written). Empty after
  // a fully-resolved run.
  std::vector<std::pair<std::string, std::string>> unresolved_imports;

  Eprp_var() {
    dict.clear();
    stage_dict.clear();
    graphs.clear();
    lnasts.clear();
  }

  Eprp_var(const Eprp_dict& _dict) : dict(_dict) {}

  void add(const Eprp_dict& _dict);
  void add(const Eprp_var& _var);
  void add(Eprp_lnasts& _var);

  void                           add(const std::shared_ptr<hhds::Graph>& graph);
  void                           add(std::unique_ptr<Lnast> lnast);
  void                           add(const std::shared_ptr<Lnast>& lnast);
  void                           add(std::string_view name, std::string_view value);
  void                           set_stage_labels(const Eprp_dict& _dict) { stage_dict = _dict; }
  [[nodiscard]] bool             has_stage_label(std::string_view name) const { return stage_dict.find(name) != stage_dict.end(); }
  [[nodiscard]] std::string_view get_stage(std::string_view name, std::string_view default_value = "") const;
  void                           replace(const Eprp_var::Eprp_lnasts& lns);
  void                           replace(const std::shared_ptr<Lnast>& lnast_old, std::shared_ptr<Lnast>& lnast_new);

  void delete_label(std::string_view name);

  [[nodiscard]] bool has_label(std::string_view name) const { return dict.find(name) != dict.end(); };

  [[nodiscard]] std::string_view get(std::string_view name, std::string_view default_value = "") const;

  void clear() {
    dict.clear();
    stage_dict.clear();
    graphs.clear();
    lnasts.clear();
  }

  [[nodiscard]] bool empty() const { return dict.empty() && graphs.empty(); }
};
