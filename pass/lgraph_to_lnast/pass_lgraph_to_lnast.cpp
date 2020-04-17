//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lgraph_to_lnast.hpp"

#include <string>

#include "lbench.hpp"
#include "lgedgeiter.hpp"

//Node colors
#define WHITE 0
#define GREY  1
#define BLACK 2

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
  fmt::print("iterate_over_lg\n");
  Lnast lnast;
  lnast.set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, "top")));
  auto idx_stmts = lnast.add_child(lnast.get_root(), Lnast_node::create_stmts("stmts"));

  //handle_io(lg, idx_stmts, lnast);
  initial_tree_coloring(lg);

  begin_transformation(lg);

  lnasts.emplace_back(lnast);

  return true;
}

void Pass_lgraph_to_lnast::initial_tree_coloring(LGraph *lg) {
  lg->each_graph_input([&](const Node_pin &pin) {
    //TODO: Make this so it only invokes once, since all inputs share same node.
    //  Also, we could just consider starting this with it being black, but I will see if I should later in dev.
    pin.get_node().set_color(WHITE);
  });

  for (const auto &node: lg->fast()) {
    auto node_editable = node;
    node_editable.set_color(WHITE);
  }

  lg->each_graph_output([&](const Node_pin &pin) {
    //TODO: Make this so it only invokes once, since all inputs share same node.
    pin.get_node().set_color(WHITE);
  });
}

void Pass_lgraph_to_lnast::begin_transformation(LGraph *lg) {
  lg->each_graph_output([&](const Node_pin &pin) {//TODO: Make sure I have the capture list correct
    //Note: pin is a driver pin.
    fmt::print("opin: {} pid: {}\n", pin.get_name(), pin.get_pid());
    I(pin.get_node().get_type().op == GraphIO_Op);
    auto node = pin.get_node();
    std::string_view temp = handle_source_node(lg, node);
    fmt::print("End - node color: {}\n", node.get_color());
  });

  lg->each_graph_output([&](const Node_pin &pin) {//TODO: Make sure I have the capture list correct
    //Note: pin is a driver pin.
    fmt::print("opin: {} pid: {}\n", pin.get_name(), pin.get_pid());
    I(pin.get_node().get_type().op == GraphIO_Op);
    auto node = pin.get_node();
    fmt::print("End - node color: {}\n", node.get_color());
  });
}

/* Purpose of this function is to serve as the recursive
 * call we will invoke constantly as we work up the
 * LGraph. At the end, regardless of if any work is
 * needed to be done, return the node's name. */
std::string_view Pass_lgraph_to_lnast::handle_source_node(LGraph *lg, Node& node) {
  node.set_color(GREY);
  for (const auto &inp : node.inp_edges()) {
    if (inp.driver.has_name()) {
      fmt::print("\tedge: [d: {} {} {}] [s: ]\n", inp.driver.get_name(), inp.driver.get_pid(), inp.driver.get_node().get_color());
    } else {
      fmt::print("\tedge: [d: {} {} {}] [s: ]\n", "tester", inp.driver.get_pid(), inp.driver.get_node().get_color());
    }

    if (inp.driver.get_node().get_color() == WHITE) {
    //if (!inp.driver.get_node().has_color()) {
      auto nd = inp.driver.get_node();
      std::string_view temp2 = handle_source_node(lg, nd);
      I(nd.get_color() == BLACK);
    }
  }
  node.set_color(BLACK);
  if (node.has_name()) {
    return node.get_name();
  } else {
    //TODO: Generate a random name, set node's name to this, then return that name.
    return "fixme";
  }
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
