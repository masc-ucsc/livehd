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

  // std::vector<Node_pin> pending;
  std::deque<Node_pin> pending;
  // std::vector<Node_pin> next_pending;
  std::deque<Node_pin> next_pending;
  // std::vector<Node_pin> initial_imp_unset;

  void mark_all_outputs  (Node_pin &pin);
  void iterate_logic     (Node_pin &pin);
  void iterate_arith     (Node_pin &pin);
  void iterate_comparison(Node_pin &pin);
  void iterate_shift     (Node_pin &pin);
  void iterate_pick      (Node_pin &pin);
  void iterate_join      (Node_pin &pin);
  void iterate_equals    (Node_pin &pin);
  void iterate_mux       (Node_pin &pin);
  void iterate_flop      (Node_pin &pin);

  void iterate_driver_pin        (Node_pin &pin);

  void bw_pass_setup             (LGraph *lg);
  static void bw_pass_dump              (LGraph *lg);
  static void bw_implicit_range_to_bits (LGraph *lg);
  bool bw_pass_iterate           ();
  void bw_settle_graph_outputs   (LGraph *lg);

  static void trans(Eprp_var &var);
  void        do_trans(LGraph *orig);

public:
  explicit Pass_bitwidth(const Eprp_var &var);

  static void setup();
};
