//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>

#include "cell.hpp"
#include "lgraph.hpp"
#include "lnast.hpp"

class Lnast_to_lgraph {
public:
  explicit Lnast_to_lgraph(Lgraph* lg, std::shared_ptr<Lnast> lnast);
  void lower();

private:
  Lgraph*                lg_;
  std::shared_ptr<Lnast> lnast_;
  std::stack<Lnast_nid> nid_stack_;
  Lnast_nid             cur_;
  std::unordered_map<std::string, Node_pin> pin_map_;
  int next_inp_pos_{0};
  int next_out_pos_{0};

  bool             move_to_child();
  bool             move_to_sibling();
  void             move_to_parent();
  bool             is_last_child() const;
  std::string_view current_text() const;
  Lnast_ntype::Lnast_ntype_int current_ntype() const;

  static std::string_view strip_prefix(std::string_view name);
  static bool             is_output_port(std::string_view name);
  static bool             is_input_port(std::string_view name);

  Node_pin resolve(std::string_view name);
  void     bind(std::string_view name, Node_pin drv);
  void     wire_outputs();

  void lower_node();
  void lower_top();
  void lower_stmts();
  void lower_assign();
  void lower_attr_set();
  void lower_cassert();
  void lower_infix(Ntype_op op, std::string_view a_pin_name, std::string_view b_pin_name);
  void lower_not();
  Node_pin lower_leaf();
};
