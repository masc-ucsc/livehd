//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "node_pin.hpp"
#include "node_type_base.hpp"
#include "pass.hpp"

class Pass_bitwidth : public Pass {
  friend class Inou_lnast_dfg;
protected:
  int max_iterations;

  std::vector<Node_pin> pending;
  std::vector<Node_pin> next_pending;

  void mark_all_outputs(const LGraph *lg, Node_pin &pin);

  void iterate_logic(const LGraph *lg, Node_pin &pin, Node_Type_Op op);
  void iterate_arith(const LGraph *lg, Node_pin &pin, Node_Type_Op op);
  void iterate_comparison(const LGraph *lg, Node_pin &pin, Node_Type_Op op);
  void iterate_shift(const LGraph *lg, Node_pin &pin, Node_Type_Op op);
  void iterate_pick(const LGraph *lg, Node_pin &pin, Node_Type_Op op);
  void iterate_join(const LGraph *lg, Node_pin &pin, Node_Type_Op op);
  void iterate_equals(const LGraph *lg, Node_pin &pin, Node_Type_Op op);
  void iterate_mux(const LGraph *lg, Node_pin &pin, Node_Type_Op op);

  void iterate_node(LGraph *lg, Index_ID idx);
  void iterate_driver_pin(LGraph *lg, Node_pin &pin);

  void bw_pass_setup(LGraph *lg);
  void bw_pass_dump(LGraph *lg);
  bool bw_pass_iterate(LGraph *lg);

  static void trans(Eprp_var &var);
  void        do_trans(LGraph *orig);

public:
  Pass_bitwidth(){};
  Pass_bitwidth(const Eprp_var &var);

  static void setup();
};
