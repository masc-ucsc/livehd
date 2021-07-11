//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "absl/container/node_hash_map.h"
#include "lgedgeiter.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "pass.hpp"
#include "struct_firbits.hpp"

using FBMap   = absl::flat_hash_map<Node_pin::Compact_class_driver, Firrtl_bits>;  // pin->firrtl bits
using PinMap  = absl::flat_hash_map<Node_pin, Node_pin>;                           // old_pin to new_pin for both dpin and spin
using XorrMap = absl::flat_hash_map<Node_pin, std::vector<Node_pin>>;  // special case for xorr one old spin -> multi newspin

class Firmap {
protected:
  bool firbits_issues    = false;
  bool firmap_issues     = false;
  bool firbits_wait_flop = false;

  absl::node_hash_map<Lgraph *, FBMap> &  fbmaps;   // firbits maps center
  absl::node_hash_map<Lgraph *, PinMap> & pinmaps;  // pin maps center
  absl::node_hash_map<Lgraph *, XorrMap> &spinmaps_xorr;
  // absl::flat_hash_map<Node_pin, Node_pin>                  pinmap;       // old_pin to new_pin for both dpin and spin
  // absl::flat_hash_map<Node_pin, std::vector<Node_pin>>     spinmap_xorr;
  enum class Attr { Set_other, Set_ubits, Set_sbits, Set_max, Set_min, Set_dp_assign };

  static Attr     get_key_attr(std::string_view key);
  FBMap::iterator get_fbits_from_hierarchy(XEdge &e);

  // lg_op
  void analysis_lg_const(Node &node, FBMap &fbmap);
  void analysis_lg_attr_set(Node &node, FBMap &fbmap);
  void analysis_lg_attr_set_dp_assign(Node &node, FBMap &fbmap);
  void analysis_lg_attr_set_new_attr(Node &node, FBMap &fbmap);
  void analysis_lg_attr_set_propagate(Node &node, FBMap &fbmap);
  void analysis_lg_flop(Node &node, FBMap &fbmap);
  void analysis_lg_mux(Node &node, FBMap &fbmap);
  void analysis_fir_ops(Node &node, std::string_view op, FBMap &fbmap);
  // fir_op
  void analysis_fir_const(Node &node, FBMap &fbmap);
  void analysis_fir_add_sub(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_mul(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_div(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_rem(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_comp(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_pad(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_as_uint(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_as_sint(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_as_clock(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_shl(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_shr(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_dshl(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_dshr(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_cvt(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_neg(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_not(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_bitwise(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_bitwire_reduction(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_bits_extract(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_cat(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_head(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);
  void analysis_fir_tail(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap);

  // fir_op->lg_ops
  void map_node_fir_ops(Node &node, std::string_view op, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap, XorrMap &spinmap_xorr);
  void map_node_fir_const(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_add(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_sub(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_mul(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_div(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_rem(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_lt_gt(Node &node, Lgraph *new_lg, std::string_view op, PinMap &pinmap);
  void map_node_fir_leq_geq(Node &node, Lgraph *new_lg, std::string_view op, PinMap &pinmap);
  void map_node_fir_eq(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_neq(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_as_uint(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_as_sint(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_as_clock(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_pad(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_shl(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_shr(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_dshl(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_dshr(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_cvt(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_neg(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_and_or_xor(Node &node, Lgraph *new_lg, std::string_view op, PinMap &pinmap);
  void map_node_fir_orr(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_bits(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void map_node_fir_not(Node &node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap);
  void map_node_fir_andr(Node &node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap);
  void map_node_fir_xorr(Node &node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap, XorrMap &spinmap_xorr);
  void map_node_fir_cat(Node &node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap);
  void map_node_fir_head(Node &node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap);
  void map_node_fir_tail(Node &node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap);

  void clone_lg_ops_node(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void clone_subgraph_node(Node &node, Lgraph *new_lg, PinMap &pinmap);
  void clone_edges(Node &node, PinMap &pinmap);
  void clone_edges_fir_xorr(Node &node, PinMap &pinmap, XorrMap &spinmap_xorr);

public:
  Firmap(absl::node_hash_map<Lgraph *, FBMap> &_fbmaps, absl::node_hash_map<Lgraph *, PinMap> &_pinmaps,
         absl::node_hash_map<Lgraph *, XorrMap> &_spinmaps_xorr);
  void    do_firbits_analysis(Lgraph *orig);
  Lgraph *do_firrtl_mapping(Lgraph *orig);

  void add_map_entry(Lgraph *lg);

  void dump() const;
};
