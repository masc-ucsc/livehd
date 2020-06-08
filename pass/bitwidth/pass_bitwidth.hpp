//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>
#include <deque>

#include "node_pin.hpp"
#include "node_type_base.hpp"
#include "pass.hpp"

class Pass_bitwidth : public Pass {
protected:
  int max_iterations {};

  std::deque<Node_pin> pending;
  std::deque<Node_pin> next_pending;

  absl::flat_hash_set<Node_pin> dp_flagged_dpins;
  absl::flat_hash_map<Node_pin, Node_pin> dp_followed_by_table; // foo_N-1 -> foo_N, foo_N-1's bitwidth should be followed by foo_N
  absl::flat_hash_map<std::string_view, std::vector<Node_pin>> vname2dpins; // a smaller searching subset instead of whole LG

  void mark_all_affected_dpins  (const Node_pin &dpin, bool ini_setup = false);
  void iterate_logic     (Node_pin &dpin);
  void iterate_arith     (Node_pin &dpin);
  void iterate_comparison(Node_pin &dpin);
  void iterate_shift     (Node_pin &dpin);
  void iterate_pick      (Node_pin &dpin);
  void iterate_join      (Node_pin &dpin);
  void iterate_equals    (Node_pin &dpin);
  void iterate_mux       (Node_pin &dpin);
  void iterate_flop      (Node_pin &dpin);
  void iterate_driver_pin        (Node_pin &dpin);


  void        bw_pass_setup              (LGraph *lg);
  void        dp_assign_initialization   (LGraph *lg);
  bool        bw_pass_iterate            ();
  void        bw_pass_dump               (LGraph *lg);
  void        bw_settle_graph_outputs    (LGraph *lg);
  void        bw_bits_extension_by_join  (LGraph *lg);
  void        bw_replace_dp_node_by_pick (LGraph *lg);
  static void bw_implicit_range_to_bits  (LGraph *lg);
  static void trans(Eprp_var &var);
  void        do_trans(LGraph *orig);

public:
  explicit    Pass_bitwidth(const Eprp_var &var);
  static void setup();
};
