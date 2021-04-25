// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgraphbase.hpp"
#include "lnast.hpp"

#include "absl/container/flat_hash_map.h"

class Label_synth {
private:
  const bool verbose;
  const bool hier;

  int last_free_id;

  absl::flat_hash_map<Node::Compact_flat, int> flat_node2id;
  absl::flat_hash_map<int, int> flat_merges;

  int  get_free_id();
  void set_id(const Node &node, int id);
  int collapse_merge(int dst);

  void mark_ids(Lgraph *g);
  void merge_ids();

public:
  void label(Lgraph *g);

  Label_synth(bool _verbose, bool _hier);

  void dump() const;
};
