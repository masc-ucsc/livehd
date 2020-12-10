//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "node.hpp"
#include "node_pin.hpp"
#include "lgedgeiter.hpp"
#include "pass.hpp"
#include "lgedgeiter.hpp"
#include "firbits.hpp"


class Firmap {
protected:
  bool hier;
  absl::flat_hash_map<Node_pin::Compact, Firrtl_bits> fbmap;

  enum class Attr { Set_other, Set_ubits, Set_sbits, Set_max, Set_min, Set_dp_assign };
  void firmap_pass(LGraph *lg);

  static Attr get_key_attr(std::string_view key);
  
  void process_lg_const(Node &node);
  void process_lg_attr_get(Node &node);
  void process_lg_attr_set(Node &node);
  void process_lg_attr_set_dp_assign(Node &node);
  void process_lg_attr_set_new_attr(Node &node);
  void process_lg_attr_set_propagate(Node &node);
  void process_lg_flop(Node &node);
  void process_lg_mux(Node &node, XEdge_iterator &inp_edges);
  void process_fir_ops(Node &node);
  void process_fir_add_sub(Node &node, XEdge_iterator &inp_edges);

public:
  Firmap (bool hier);
  void do_trans(LGraph *orig);
};
