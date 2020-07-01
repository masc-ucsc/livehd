//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>
#include <deque>

#include "bitwidth_range.hpp"
#include "node_pin.hpp"
#include "pass.hpp"

class Pass_bitwidth : public Pass {
protected:
  int max_iterations;
  bool must_perform_backward;

  absl::flat_hash_map<Node_pin::Compact, Bitwidth_range>  bwmap;
  absl::flat_hash_map<Node::Compact, uint32_t>            outcountmap;

  static void trans(Eprp_var &var);
  void        do_trans(LGraph *orig);

  void        process_const(Node &node);
  void        process_logic(Node &node, XEdge_iterator &inp_edges, bool and_op);

  void        garbage_collect_support_structures(XEdge_iterator &inp_edges);
  void        adjust_dpin_bits(Node_pin &dpin, Bitwidth_range &bw);

  void        bw_pass(LGraph *lg);

public:
  explicit    Pass_bitwidth(const Eprp_var &var);
  static void setup();
};
