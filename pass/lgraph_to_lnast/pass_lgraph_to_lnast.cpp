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

  begin_transformation(lg, lnast, idx_stmts);

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
    //TODO: Make this so it only invokes once, since all outputs share same node.
    pin.get_node().set_color(WHITE);
  });

}

void Pass_lgraph_to_lnast::begin_transformation(LGraph *lg, Lnast& lnast, Lnast_nid& ln_node) {
  //note: in graph out node, spin_pid == dpin_pid is always true

  lg->each_graph_output([&](const Node_pin &pin) {//TODO: Make sure I have the capture list correct
    //Note: pin is a driver pin.
    fmt::print("opin: {} pid: {}\n", pin.get_name(), pin.get_pid());
    I(pin.get_node().get_type().op == GraphIO_Op);
    //auto node = pin.get_node();
    auto editable_pin = pin;
    handle_output_node(lg, editable_pin, lnast, ln_node);
    //final result should be an assignment like: pin.get_name() = temp
    fmt::print("End - node color: {}\n", pin.get_node().get_color());
  });
}

void Pass_lgraph_to_lnast::handle_output_node(LGraph *lg, Node_pin& pin, Lnast& lnast, Lnast_nid& ln_node) {
  I(pin.has_name()); //Outputs of the graphs should have names, I would think.
  for (const auto &inp : pin.get_node().inp_edges()) {
    auto editable_pin = inp.driver;
    handle_source_node(lg, editable_pin, lnast, ln_node);
    fmt::print("\tedge: [{} d: {} {} {}] [s: {}]\n", inp.driver.get_node().get_type().op,
            inp.driver.get_name(), inp.driver.get_pid(), inp.driver.get_node().get_color(), inp.sink.get_pid());
  }

  attach_to_lnast(lnast, ln_node, pin);
}

/* Purpose of this function is to serve as the recursive
 * call we will invoke constantly as we work up the
 * LGraph. At the end, regardless of if any work is
 * needed to be done, return the node's name. */
void Pass_lgraph_to_lnast::handle_source_node(LGraph *lg, Node_pin& pin, Lnast& lnast, Lnast_nid& ln_node) {
  //If pin is a driver pin for an already handled node, just return driver pin's name.
  if(pin.get_node().get_color() == BLACK) {
    if(!pin.has_name()) {
      pin.set_name(absl::StrCat("T", std::to_string(temp_var_count)));//FIXME: Will this ever collide with any var names?
      fmt::print("AssigningE {}\n", pin.get_name());
      temp_var_count++;
    }
    return;
  }

  //Node that pin is a driver in has not been visited yet. Handle it.
  I(pin.get_node().get_color() == WHITE);
  pin.get_node().set_color(GREY);

  for (const auto &inp : pin.get_node().inp_edges()) {
    auto editable_pin = inp.driver;
    if (editable_pin.get_node().get_color() == WHITE) {
      handle_source_node(lg, editable_pin, lnast, ln_node);
    }
    I(editable_pin.get_node().get_color() == BLACK);

    fmt::print("\tedge: [{} d: {} {} {}] [s: {}]\n", inp.driver.get_node().get_type().op,
            inp.driver.get_name(), inp.driver.get_pid(), inp.driver.get_node().get_color(), inp.sink.get_pid());
  }

  if(!pin.has_name()) {
    pin.set_name(absl::StrCat("T", std::to_string(temp_var_count)));
    fmt::print("Assigning {}\n", pin.get_name());
    temp_var_count++;
  }

  pin.get_node().set_color(BLACK);

  attach_to_lnast(lnast, ln_node, pin);
}

void Pass_lgraph_to_lnast::attach_to_lnast(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //Look at pin's node's type, then based off that figure out what type of node to add to LNAST.
  fmt::print("LNAST {} :", pin.get_name());
  for(const auto inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    fmt::print(" {}", dpin.get_name());
  }
  fmt::print("\n");
  switch(pin.get_node().get_type().op) {//Future note to self: when doing src_pins, always check if sources to node are io inputs
    case GraphIO_Op:
      break;
    /*case And_Op:
      break;
    case Or_Op:
      break;
    case Xor_Op:
      break;
    case Not_Op:
      break;*/
    case Sum_Op:
      attach_sum_node(lnast, parent_node, pin);
      lnast.dump();
      break;
    /*case Mult_Op:
      break;
    case Div_Op:
      break;
    case Mod_Op:
      break;
    case LessThan_Op:
      break;
    case GreaterThan_Op:
      break;
    case LessEqualThan_Op:
      break;
    case GreaterEqualThan_Op:
      break;
    case Equals_Op:
      break;
    case ShiftLeft_Op:
      break;
    case ShiftRight_Op:
      break;
    case Join_Op:
      break;
    case Pick_Op:
      break;
    case U32Const_Op:
      break;
    case Mux_Op:
      break;*/
    default: fmt::print("Op not yet supported in attach_to_lnast\n");
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

// -------- How to convert each LGraph node type to LNAST -------------

void Pass_lgraph_to_lnast::attach_sum_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //PID: 0 = AS, 1 = AU, 2 = BS, 3 = BU, 4 = Y... Y = (AS+...+AS+AU+...+AU) - (BS+...+BS+BU+...+BU)
  bool is_add, is_subt = false;
  int add_count = 0;

  Lnast_nid add_node, subt_node;

  //Determine if we're doing an add, sub, or both.
  for(const auto inp : pin.get_node().inp_edges()) {
    auto spin = inp.sink;
    if((spin.get_pid() == 0) || (spin.get_pid() == 1)) {
      add_count++;
      //is_add = true;
    } else {
      is_subt = true;
    }
  }

  if(add_count > 1) {
    is_add = true;
  }

  I(is_add | is_subt); //If neither, why does this node exist?

  //Now that we know which, create the necessary operation nodes.
  if(is_add & !is_subt) {
    add_node = lnast.add_child(parent_node, Lnast_node::create_plus("plus"));
    lnast.add_child(add_node, Lnast_node::create_ref(pin.get_name()));
  } else if(!is_add & is_subt) {
    subt_node = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));
    /*Note: the next line is a strange workaround but it is important. If we didn't do this, the later
        for loop would try to attach something to "add_node", but we never specified what that was. */
    add_node = subt_node;
    lnast.add_child(subt_node, Lnast_node::create_ref(pin.get_name()));
  } else {
    add_node = lnast.add_child(parent_node, Lnast_node::create_plus("plus"));
    subt_node = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));

    std::string_view intermediate_var_name = absl::StrCat("T", temp_var_count);
    temp_var_count++;
    lnast.add_child(add_node, Lnast_node::create_ref( lnast.add_string(intermediate_var_name)));
    lnast.add_child(subt_node, Lnast_node::create_ref(pin.get_name()));
    lnast.add_child(subt_node, Lnast_node::create_ref( lnast.add_string(intermediate_var_name)));
  }

  //Attach the name of each of the node's inputs to the Lnast operation node we just made.
  for(const auto inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    auto spin = inp.sink;
    //FIXME: This will only work for var/wire names. This will mess up for constants.
    if((spin.get_pid() == 0) || (spin.get_pid() == 1)) {
      lnast.add_child(add_node, Lnast_node::create_ref(dpin.get_name()));
    } else {
      lnast.add_child(subt_node, Lnast_node::create_ref(dpin.get_name()));
    }
  }
}
