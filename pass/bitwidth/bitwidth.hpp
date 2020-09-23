//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "bitwidth_range.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "pass.hpp"

class Bitwidth {
protected:
  int  max_iterations;
  bool hier;
  bool must_perform_backward;

  enum class Attr { Set_other, Set_bits, Set_max, Set_min, Set_dp_assign };

  static Attr get_key_attr(std::string_view key);

  bool not_finished;

  absl::flat_hash_map<Node_pin::Compact, Bitwidth_range> bwmap;
  absl::flat_hash_map<Node::Compact, uint32_t>           outcountmap;

  void process_const(Node &node);
  void process_not(Node &node, XEdge_iterator &inp_edges);
  void process_flop(Node &node);
  void process_mux(Node &node, XEdge_iterator &inp_edges);
  void process_shr(Node &node, XEdge_iterator &inp_edges);
  void process_sum(Node &node, XEdge_iterator &inp_edges);
  void process_pick(Node &node);
  void process_comparator(Node &node);
  void process_logic(Node &node, XEdge_iterator &inp_edges);
  void process_logic_and(Node &node, XEdge_iterator &inp_edges);
  void process_attr_get(Node &node);
  void process_attr_set_dp_assign(Node &node);
  void process_attr_set_new_attr(Node &node);
  void process_attr_set_propagate(Node &node);
  void process_attr_set(Node &node);

  void garbage_collect_support_structures(XEdge_iterator &inp_edges);
  void forward_adjust_dpin(Node_pin &dpin, Bitwidth_range &bw);
  void set_graph_boundary(Node_pin &dpin, Node_pin &spin);

  void bw_pass(LGraph *lg);

public:
  Bitwidth (bool hier, int max_iterations);
  void do_trans(LGraph *orig);
  bool is_finished() const { return !not_finished; }
};
