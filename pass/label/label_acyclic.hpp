// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <vector>
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgraphbase.hpp"
#include "lnast.hpp"

class Label_acyclic {
private:
  const bool verbose;
  const bool hier;
  uint8_t    cutoff;
  uint8_t    part_id;

  using NodeVector = std::vector<Node:;Compact>;
  NodeVector                             node_preds; // predecessors of a node
  absl::flat_hash_set<Node::Compact>     roots;      // potential roots of partitions
  absl::flat_hash_map<Node::Compact,int> node2id;    // <Node, Partition ID>
  absl::flat_hash_map<int, NodeVector>   id2nodes;   // <Partition ID, Nodes in Partition>


public:
  void label(Lgraph *g);

  Label_acyclic(bool _verbose, bool _hier, uint8_t _cutoff);

  void dump() const;

};
