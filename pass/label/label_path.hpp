// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

class Label_path {
private:
  const bool                     verbose;
  const bool                     hier;
  const std::vector<std::string> instance_names;

  int last_free_id = 0;

  absl::flat_hash_map<Node::Compact, std::vector<int>> node2colors;
  absl::flat_hash_map<int, std::string>                color2instance;

  int get_free_id() { return ++last_free_id; }

  bool should_stop_fwd(const Node& node, int color) const;
  void propagate_fwd(const Node& node, int color);
  void propagate_bwd(const Node& node, int color);

public:
  Label_path(bool _verbose, bool _hier, std::string_view _instance_name = {})
      : verbose(_verbose), hier(_hier), instance_names(parse_instance_names(_instance_name)) {}

  void label(Lgraph* g);
  void dump(Lgraph* g) const;

  static std::vector<std::string> parse_instance_names(std::string_view instance_csv);
};
