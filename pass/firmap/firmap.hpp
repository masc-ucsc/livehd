//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "node.hpp"
#include "node_pin.hpp"
#include "lgedgeiter.hpp"
#include "pass.hpp"
#include "lgedgeiter.hpp"
#include "struct_firbits.hpp"


class Firmap {
protected:
  bool not_finished;
  absl::flat_hash_map<Node_pin::Compact, Firrtl_bits> fbmap;
  absl::flat_hash_map<Node_pin, Node_pin> o2n_dpin; //old_dpin to new_dpin
  enum class Attr { Set_other, Set_ubits, Set_sbits, Set_max, Set_min, Set_dp_assign };

  void    firbits_analysis(LGraph *lg);
  LGraph* firrtl_mapping(LGraph *lg);

  static Attr get_key_attr(std::string_view key);
  
  void analysis_lg_const(Node &node);
  void analysis_lg_attr_set(Node &node);
  void analysis_lg_attr_set_dp_assign(Node &node);
  void analysis_lg_attr_set_new_attr(Node &node);
  void analysis_lg_attr_set_propagate(Node &node);
  void analysis_lg_flop(Node &node);
  void analysis_lg_mux(Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_ops(Node &node, std::string_view op);
  void analysis_fir_add_sub(Node &node, XEdge_iterator &inp_edges);

  void map_fir_ops(Node &node, std::string_view op, LGraph *new_lg);
  void map_fir_add(Node &node, LGraph *new_lg);
  void map_fir_sub(Node &node, LGraph *new_lg);
  void clone_lg_ops(Node &node, LGraph *new_lg);


public:
  Firmap ();
  void    do_firbits_analysis(LGraph *orig);
  LGraph* do_firrtl_mapping(LGraph *orig);
};
