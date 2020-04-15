//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lgraph_to_lnast.hpp"

#include <string>

#include "lbench.hpp"
#include "lgedgeiter.hpp"

void setup_pass_lgraph_to_lnast() { Pass_lgraph_to_lnast::setup(); }

void Pass_lgraph_to_lnast::setup() {
  Eprp_method m1("pass.lgraph_to_lnast", "translates LGraph to LNAST", &Pass_lgraph_to_lnast::trans);
  register_pass(m1);
}

Pass_lgraph_to_lnast::Pass_lgraph_to_lnast(const Eprp_var &var) : Pass("pass.lgraph_to_lnast", var) { }

void Pass_lgraph_to_lnast::trans(Eprp_var &var) {
  Pass_lgraph_to_lnast p(var);

  //std::vector<const LGraph *> lgs;
  for (const auto &l : var.lgs) {
    p.do_trans(l);
  }
}

void Pass_lgraph_to_lnast::do_trans(LGraph *lg) {
  Lbench b("pass.lgraph_to_lnast");

  //pass_setup(lg);
  bool successful = iterate_over_lg(lg);
  if (!successful) {
    error("Could not properly map LGraph to LNAST\n");//FIXME: Maybe later print out why
  }
}

bool Pass_lgraph_to_lnast::iterate_over_lg(LGraph *lg) {
  Lnast lnast;
  lnast.set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, "top")));
  auto idx_stmts = lnast.add_child(lnast.get_root(), Lnast_node::create_stmts("stmts"));

  handle_io(lg, idx_stmts, lnast);

  lnasts.emplace_back(lnast);

  return true;
}

void Pass_lgraph_to_lnast::handle_io(LGraph *lg, Lnast_nid& parent_lnast_node, Lnast& lnast) {
  /* Any input or output that has its bitwidth specified should add info to the LNAST.
   * As an example, if we had an input x that was 7 bits wide, this would be added:
   *     dot             asg
   *   /  |  \         /     \
   *___C0 $x __bits  ___C0  d7    (note that the $ would be % if it was an output)*/

  lg->each_graph_input([this, &lnast, lg, parent_lnast_node](const Node_pin &pin) {
    //I(pin.has_bitwidth());//FIXME: Not necessarily true for subgraphs?
    if(pin.has_bitwidth()) {
      fmt::print("inp: {} {}\n", pin.get_name(), pin.get_bitwidth().e.max);
      //TODO: Add a "dot" node to LNAST here, I just don't know how to get node's name yet.
      auto idx_dot = lnast.add_child(parent_lnast_node, Lnast_node::create_dot("dot"));
      lnast.add_child(idx_dot, Lnast_node::create_ref(""));
      lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(absl::StrCat("$", pin.get_name()))));
      lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

      auto idx_asg = lnast.add_child(parent_lnast_node, Lnast_node::create_assign("asg"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(""));
      if (pin.get_bitwidth().e.overflow) {
        lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(std::to_string(pin.get_bitwidth().e.max))));
      } else {
        lnast.add_child(idx_asg, Lnast_node::create_const(
                      lnast.add_string( "0d" + std::to_string((uint64_t)(floor(log2(pin.get_bitwidth().e.max))+1)) )));
      }

      lnast.dump();
    }
  });

  lg->each_graph_output([this, &lnast, lg, parent_lnast_node](const Node_pin &pin) {
    //I(pin.has_bitwidth());//FIXME: Not necessarily true for subgraphs?
    if(pin.has_bitwidth()) {
      fmt::print("inp: {} {}\n", pin.get_name(), pin.get_bitwidth().e.max);
      //TODO: Add a "dot" node to LNAST here, I just don't know how to get node's name yet.
      auto idx_dot = lnast.add_child(parent_lnast_node, Lnast_node::create_dot("dot"));
      lnast.add_child(idx_dot, Lnast_node::create_ref(""));
      lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(absl::StrCat("$", pin.get_name()))));
      lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

      auto idx_asg = lnast.add_child(parent_lnast_node, Lnast_node::create_assign("asg"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(""));
      if (pin.get_bitwidth().e.overflow) {
        lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(std::to_string(pin.get_bitwidth().e.max))));
      } else {
        lnast.add_child(idx_asg, Lnast_node::create_const(
                      lnast.add_string( "0d" + std::to_string((uint64_t)(floor(log2(pin.get_bitwidth().e.max))+1)) )));
      }

      lnast.dump();
    }
  });
}
