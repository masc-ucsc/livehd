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

  static Attr get_key_attr(std::string_view key);
  
  //lg_op
  void analysis_lg_const              (Node &node);
  void analysis_lg_attr_set           (Node &node);
  void analysis_lg_attr_set_dp_assign (Node &node);
  void analysis_lg_attr_set_new_attr  (Node &node);
  void analysis_lg_attr_set_propagate (Node &node);
  void analysis_lg_flop               (Node &node);
  void analysis_lg_mux                (Node &node);
  void analysis_fir_ops               (Node &node, std::string_view op);
  //fir_op
  void analysis_fir_add_sub           (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_mul               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_div               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_rem               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_comp              (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_pad               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_as_uint           (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_as_sint           (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_shl               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_shr               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_dshl              (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_dshr              (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_cvt               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_neg               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_not               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_bitwise           (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_bitwire_reduction (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_bits_extract      (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_cat               (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_head              (Node &node, XEdge_iterator &inp_edges);
  void analysis_fir_tail              (Node &node, XEdge_iterator &inp_edges);


  //fir_op->lg_ops 
  void map_fir_ops        (Node &node, std::string_view op, LGraph *new_lg);
  void map_fir_add        (Node &node, LGraph *new_lg);
  void map_fir_sub        (Node &node, LGraph *new_lg);
  void map_fir_mul        (Node &node, LGraph *new_lg);
  void map_fir_div        (Node &node, LGraph *new_lg);
  void map_fir_rem        (Node &node, LGraph *new_lg);
  void map_fir_lt_gt      (Node &node, LGraph *new_lg, std::string_view op);
  void map_fir_leq_geq    (Node &node, LGraph *new_lg, std::string_view op);
  void map_fir_eq         (Node &node, LGraph *new_lg);
  void map_fir_neq        (Node &node, LGraph *new_lg);
  void map_fir_as_uint    (Node &node, LGraph *new_lg);
  void map_fir_as_sint    (Node &node);

  //todo
  void map_fir_pad        (Node &node, LGraph *new_lg);
  void map_fir_shl        (Node &node, LGraph *new_lg);
  void map_fir_shr        (Node &node, LGraph *new_lg);
  void map_fir_dshl       (Node &node, LGraph *new_lg);
  void map_fir_dshr       (Node &node, LGraph *new_lg);
  void map_fir_cvt        (Node &node, LGraph *new_lg);
  void map_fir_neg        (Node &node, LGraph *new_lg);
  void map_fir_not        (Node &node, LGraph *new_lg);
  void map_fir_and_or_xor (Node &node, LGraph *new_lg, std::string_view op);
  void map_fir_andr       (Node &node, LGraph *new_lg);
  void map_fir_orr        (Node &node, LGraph *new_lg);
  void map_fir_xorr       (Node &node, LGraph *new_lg);
  void map_fir_cat        (Node &node, LGraph *new_lg);
  void map_fir_bits       (Node &node, LGraph *new_lg);
  void map_fir_head       (Node &node, LGraph *new_lg);
  void map_fir_tail       (Node &node, LGraph *new_lg);
  void clone_lg_ops       (Node &node, LGraph *new_lg);

public:
  Firmap ();
  void    do_firbits_analysis(LGraph *orig);
  LGraph* do_firrtl_mapping  (LGraph *orig);
};
