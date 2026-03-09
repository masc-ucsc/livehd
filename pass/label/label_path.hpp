// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

class Label_path {
private:
  const bool verbose;
  const bool hier;

  int last_free_id = 0;

  absl::flat_hash_map<Node::Compact, std::vector<int>> node2colors;

  int get_free_id() { return ++last_free_id; }

  void propagate_fwd(const Node &node, int color);
  void propagate_bwd(const Node &node, int color);

public:
  Label_path(bool _verbose, bool _hier) : verbose(_verbose), hier(_hier) {}

  void label(Lgraph *g);
  void dump(Lgraph *g) const;
};
