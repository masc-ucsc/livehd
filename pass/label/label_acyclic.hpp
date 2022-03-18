// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <vector>

#include "pass.hpp"
#include "cell.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgraphbase.hpp"
#include "lnast.hpp"

#define NO_COLOR 0

class Label_acyclic {
private:
  const bool verbose;
  const bool hier;
  const bool merge_en;
  uint8_t    cutoff;  // currently not being used
  uint8_t    part_id;

  using NodeVector = std::vector<Node::Compact>;
  using NodeSet = absl::flat_hash_set<Node::Compact>;
  using IntSet = absl::flat_hash_set<int>;

  NodeVector                             node_preds;  // predecessors of a node
  NodeSet                                roots;       // potential roots of partitions
  IntSet                                 common_int1;  // use wherever an IntSet is needed
  IntSet                                 common_int2;  // use wherever an IntSet is needed
  NodeSet                                common_node1; // use wherever an NodeSet is needed
  NodeSet                                common_node2; // use wherever an NodeSet is needed

  absl::flat_hash_map<Node::Compact,int> node2id;     // <Node, Partition ID>
  absl::flat_hash_map<int, NodeSet>      id2nodes;    // <Partition ID, Nodes in Partition>
  absl::flat_hash_map<int, NodeSet>      id2inc;      // <Partition ID, Incoming Neighbor Nodes>
  absl::flat_hash_map<int, NodeSet>      id2out;      // <Partition ID, Outgoing Neighbor Nodes>
  absl::flat_hash_map<int, IntSet>       id2incparts; // <Partition ID, Incoming Partition IDs>
  absl::flat_hash_map<int, IntSet>       id2outparts; // <Partition ID, Outgoing Partition IDs>

  bool node_set_cmp(NodeSet a, NodeSet b) const;
  bool int_set_cmp(IntSet a, IntSet b) const;
  void node_set_write(NodeSet &tgt, NodeSet &ref);
  void int_set_write(IntSet &tgt, IntSet &ref);

  void gather_roots(Lgraph *g);
  void grow_partitions(Lgraph *g);
  void gather_inou(Lgraph *g);
  
  void merge_op(int merge_from, int merge_into);
  void merge_partitions_same_parents();
  void merge_partitions_one_parent();


public:
  void label(Lgraph *g);
  Label_acyclic(bool _v, bool _h, uint8_t _c, bool _m);
  void dump(Lgraph *g) const;

};
