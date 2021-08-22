//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <unordered_map>

#include "bitwidth_range.hpp"
#include "lgedgeiter.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "pass.hpp"

class Bitwidth {
protected:
  int  max_iterations;
  bool hier;
  bool discovered_some_backward_nodes_try_again;

  enum class Attr { Set_other, Set_ubits, Set_sbits, Set_max, Set_min, Set_dp_assign };

  static Attr get_key_attr(const mmap_lib::str &key);

  bool                                                         not_finished;
  absl::flat_hash_map<Node_pin::Compact_class, Bitwidth_range> bwmap;  // bwmap indexing with dpin_compact_class, nid
  // absl::flat_hash_map<Node_pin::Compact_flat , Bitwidth_range> bwmap_flat;  // bwmap indexing with dpin_compact_flat, (lgid, nid)
  // absl::flat_hash_map<Node_pin::Compact      , Bitwidth_range> bwmap_hier;  // bwmap indexing with dpin_compact,      (hidx, nid)

  void set_bw_1bit(Node_pin &&dpin);
  void set_bw_1bit(Node_pin &dpin);
  void adjust_bw(Node_pin &&dpin, const Bitwidth_range &bw);
  void adjust_bw(Node_pin &dpin, const Bitwidth_range &bw);

  void process_const(Node &node);
  void process_not(Node &node, XEdge_iterator &inp_edges);
  void process_flop(Node &node);
  void process_memory(Node &node);
  void process_mux(Node &node, XEdge_iterator &inp_edges);
  void process_sra(Node &node, XEdge_iterator &inp_edges);
  void process_shl(Node &node, XEdge_iterator &inp_edges);
  void process_sum(Node &node, XEdge_iterator &inp_edges);
  void process_mult(Node &node, XEdge_iterator &inp_edges);
  void process_get_mask(Node &node);
  void process_set_mask(Node &node);
  void process_sext(Node &node, XEdge_iterator &inp_edges);
  void process_comparator(Node &node);
  void process_bit_or(Node &node, XEdge_iterator &inp_edges);
  void process_bit_xor(Node &node, XEdge_iterator &inp_edges);
  void process_bit_and(Node &node, XEdge_iterator &inp_edges);
  void process_assignment_or(Node &node, XEdge_iterator &inp_edges);
  void process_ror(Node &node, XEdge_iterator &inp_edges);
  void process_attr_get(Node &node);
  void process_attr_set_dp_assign(Node &node);
  void process_attr_set_new_attr(Node &node, Fwd_edge_iterator::Fwd_iter &fwd_it);
  void process_attr_set_propagate(Node &node);
  void process_attr_set(Node &node, Fwd_edge_iterator::Fwd_iter &fwd_it);
  // void insert_tposs_nodes(Node &node_attr, Bits_t ubits, Fwd_edge_iterator::Fwd_iter &fwd_it, const Bitwidth_range &bw);
  void insert_tposs_nodes(Node &node_attr, Bits_t ubits, Fwd_edge_iterator::Fwd_iter &fwd_it);

  void garbage_collect_support_structures(XEdge_iterator &inp_edges);
  void set_graph_boundary(const Node_pin &dpin, Node_pin &spin);
  void debug_unconstrained_msg(Node &node, Node_pin &d_dpin);
  void try_delete_attr_node(Node &node);
  void set_subgraph_boundary_bw(Node &node);

  void bw_pass(Lgraph *lg);

public:
  Bitwidth(bool hier, int max_iterations);
  void do_trans(Lgraph *orig);
  bool is_finished() const { return !not_finished; }
};
