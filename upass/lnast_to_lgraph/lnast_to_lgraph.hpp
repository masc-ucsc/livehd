//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
  std::unordered_set<std::string>           output_names_;
  // 1-based per contract §5: "input tuple field 1 maps to input pin 1; output
  // tuple field 1 maps to output pin 1."
  int next_inp_pos_{1};
  int next_out_pos_{1};

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

  // Returns a nil (0sb?) constant driver pin.
  Node_pin nil_pin();
  // Runs lower_node() on the current cursor position and returns what was
  // bound (assigned) by the subtree, then restores those bindings to their
  // pre-call values.  Auto-promoted graph inputs (added via resolve()) are
  // NOT considered branch writes and are KEPT across branches, so siblings
  // see the same input pin without re-promoting.
  // output_names_ is left updated (output ports discovered inside are kept).
  using WriteMap = std::unordered_map<std::string, Node_pin>;
  WriteMap lower_branch();
  // Active stack of branch-local write trackers populated by bind().  Only
  // user-written names (via assign / op result) end up here, never names
  // that resolve() auto-promotes to graph inputs.
  std::vector<WriteMap> branch_writes_stack_;

  void lower_node();
  void lower_top();
  // Lowers the post-upass layout `top -> [io, stmts]`, reading I/O from
  // the io ref-pair and the named tuple_adds that live in `stmts`.
  // Cursor must be at the `io` child on entry; on exit the cursor is back
  // at `top` (caller drops the parent frame).
  void lower_post_upass_top();
  // Lowers the post-SSA layout `top -> stmts` where I/O info has already
  // been harvested into lnast_->io_meta().  Cursor must be at the `stmts`
  // child on entry; on exit the cursor is still at `stmts` (caller's
  // move_to_parent() returns to `top`).
  void lower_from_io_meta();
  void lower_stmts();
  void lower_assign();
  void lower_if();
  void lower_func_def();
  void lower_attr_set();
  void lower_cassert();
  void lower_infix(Ntype_op op, std::string_view a_pin_name, std::string_view b_pin_name);
  void lower_negated_infix(Ntype_op op, std::string_view a_pin_name, std::string_view b_pin_name);
  void lower_unary(Ntype_op op, std::string_view a_pin_name);
  void lower_set_mask();
  void lower_not();
  Node_pin lower_leaf();
};
