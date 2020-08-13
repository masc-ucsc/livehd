//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lgraph_to_lnast.hpp"

#include <queue>
#include <stack>
#include <string>

#include "lbench.hpp"
#include "lgedgeiter.hpp"

// Node colors
#define WHITE 0
#define GREY  1
#define BLACK 2

static Pass_plugin sample("pass_lgraph_to_lnast", Pass_lgraph_to_lnast::setup);

void Pass_lgraph_to_lnast::setup() {
  Eprp_method m1("pass.lgraph_to_lnast", "translates LGraph to LNAST", &Pass_lgraph_to_lnast::trans);
  // For bw_in_ln, if __bits are not put in the LNAST then they can be accessed using bw_table in LNAST.
  m1.add_label_optional("bw_in_ln", "true/false: put __bits nodes in LNAST?", "true");
  register_pass(m1);
}

Pass_lgraph_to_lnast::Pass_lgraph_to_lnast(const Eprp_var& var) : Pass("pass.lgraph_to_lnast", var) {}

void Pass_lgraph_to_lnast::trans(Eprp_var& var) {
  Lbench b("pass.lgraph_to_lnast");

  Pass_lgraph_to_lnast p(var);

  for (const auto& l : var.lgs) {
    p.do_trans(l, var, l->get_name());
  }
}

void Pass_lgraph_to_lnast::do_trans(LGraph* lg, Eprp_var& var, std::string_view module_name) {
  if (var.has_label("bw_in_ln")) {
    if (var.get("bw_in_ln") == "false") {
      put_bw_in_ln = false;
    }
  }

  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(module_name);
  lnast->set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, lg->get_name())));
  auto idx_stmts = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts(get_new_seq_name(*lnast)));

  handle_io(lg, idx_stmts, *lnast);
  initial_tree_coloring(lg, *lnast);

  begin_transformation(lg, *lnast, idx_stmts);

  //lnast->dump();

  var.add(std::move(lnast));
}

void Pass_lgraph_to_lnast::initial_tree_coloring(LGraph* lg, Lnast &lnast) {
  for (const auto& node : lg->fast()) {
    auto node_editable = node;
    node_editable.set_color(WHITE);

    // Look at dpins for each node. If unnamed, give dpin a name.
    for (const auto dpin : node_editable.out_connected_pins()) {
      auto dpin_editable = dpin;
      auto ntype = dpin.get_node().get_type().op;
      if (!dpin_editable.has_name() && !((ntype == GraphIO_Op) || (ntype == Const_Op))) {
        dpin_editable.set_name(create_temp_var(lnast));
        if (dpin_editable.get_node().get_type().op == Mux_Op) {
          dpin_editable.set_name(dpin_editable.get_name().substr(3)); //Remove "___" in front
        } else  if ((ntype == SFlop_Op) || (ntype == AFlop_Op) || (ntype == FFlop_Op) || (ntype == Latch_Op)) {
          dpin_editable.set_name(absl::StrCat("#", dpin_editable.get_name()));
        }
      } else if (dpin_editable.has_name() && (dpin_editable.get_name()[0] == '#')) {
        if(!((ntype == SFlop_Op) || (ntype == AFlop_Op) || (ntype == FFlop_Op) || (ntype == Latch_Op))) {
          // If a pin's name starts with "#" but it isn't a register, remove that "#". (ssa can cause this)
          dpin_editable.set_name(dpin_editable.get_name().substr(1));
        }
      } else  if ((ntype == SFlop_Op) || (ntype == AFlop_Op) || (ntype == FFlop_Op) || (ntype == Latch_Op)) {
        if (dpin_editable.get_name()[0] != '#') {
          dpin_editable.set_name(absl::StrCat("#", dpin_get_name(dpin_editable)));
        }
      }
    }
  }

  lg->get_graph_input_node().set_color(WHITE);
  lg->get_graph_output_node().set_color(WHITE);
}

void Pass_lgraph_to_lnast::begin_transformation(LGraph* lg, Lnast& lnast, Lnast_nid& ln_node) {
  // note: in graph out node, spin_pid == dpin_pid is always true

  auto out_node = lg->get_graph_output_node();
  out_node.set_color(BLACK);
  for (const auto& inp : out_node.inp_edges()) {
    auto gpio_dpin = out_node.get_driver_pin(inp.sink.get_pid());
    I(gpio_dpin.has_name());

    auto edge_dpin = inp.driver;
    I(!edge_dpin.get_node().is_hierarchical());
    handle_source_node(lg, edge_dpin, lnast, ln_node);

    auto asg_node = lnast.add_child(ln_node, Lnast_node::create_dp_assign("out_dpasg"));
    if (gpio_dpin.get_name()[0] == '%') {
      lnast.add_child(asg_node, Lnast_node::create_ref(lnast.add_string(gpio_dpin.get_name())));
    } else {
      lnast.add_child(asg_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("%", gpio_dpin.get_name()))));
    }
    attach_child(lnast, asg_node, inp.driver);
  }
}

/* Purpose of this function is to serve as the recursive
 * call we will invoke constantly as we work up the LGraph */
void Pass_lgraph_to_lnast::handle_source_node(LGraph* lg, Node_pin& pin, Lnast& lnast, Lnast_nid& ln_node) {
  // If pin is a driver pin for an already handled node, just return driver pin's name.
  if (pin.get_node().get_color() == BLACK) {
    // Node is already in LNAST, nothing to do.
    return;
  } else if (pin.get_node().get_color() == GREY) {
    /* We only enter here if a logical loop exists. This will be
     * caused by a flop's q value being a part of what determines its
     * din value (ex. x = x - 1). We will have to traverse through
     * the grey nodes and forcibly insert them into the LNAST. (For
     * examples of this, see inou/yosys/tests/loop_in_lg.v and loop2_in_lg2.v)*/
    for (const auto& inp : pin.get_node().inp_edges()) {
      auto editable_pin = inp.driver;
      if (editable_pin.get_node().get_color() == GREY || editable_pin.get_node().get_color() == WHITE) {
        auto ntype = editable_pin.get_node().get_type().op;
        if (ntype == AFlop_Op || ntype == SFlop_Op || ntype == FFlop_Op || ntype == Latch_Op) {
          continue;
        }
        handle_source_node(lg, editable_pin, lnast, ln_node);
        I(editable_pin.get_node().get_color() == BLACK);
      }
    }
    pin.get_node().set_color(BLACK);
    attach_to_lnast(lnast, ln_node, pin);
    return;
  }

  // Node that pin is a driver in has not been visited yet. Handle it.
  I(pin.get_node().get_color() == WHITE);
  pin.get_node().set_color(GREY);

  for (const auto& inp : pin.get_node().inp_edges()) {
    auto editable_pin = inp.driver;
    I(!inp.driver.get_node().is_hierarchical());
    if (editable_pin.get_node().get_color() == WHITE || editable_pin.get_node().get_color() == GREY) {
      handle_source_node(lg, editable_pin, lnast, ln_node);
    }
    I(editable_pin.get_node().get_color() == BLACK);
  }

  if (pin.get_node().get_color() == BLACK) {
    /* A logic loop existed, so this was taken care of in
     * GREY logic above Don't add this node twice to LNAST. */
    return;
  }
  pin.get_node().set_color(BLACK);
  attach_to_lnast(lnast, ln_node, pin);
}



/* TODO:
  Invalid_Op,
  LUT_Op,
  DontCare_Op,
  Memory_Op,
*/
void Pass_lgraph_to_lnast::attach_to_lnast(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // Specify bitwidth in LNAST table (for code gen purposes)
  std::string_view name;
  auto bw = pin.get_bits();
  auto ntype = pin.get_node().get_type().op;
  if (ntype == GraphIO_Op || ntype == Const_Op) {
    // Nothing to do, and don't specify bitwidth.
    return;
  }

  if ((bw > 0) & (pin.get_name().substr(0,3) != "___")) {
    /* NOTE->hunter: I decided to only specify reg and IO bw (not
     * wires). If more is needed just widen below condition. */
    if (ntype == SFlop_Op || ntype == AFlop_Op || ntype == Latch_Op) {
      name = lnast.add_string(pin.get_name());
      lnast.set_bitwidth(name.substr(1), bw);
      if (put_bw_in_ln) {
        add_bw_in_ln(lnast, parent_node, name, bw);
      }
    }
  }

  // Look at pin's node's type, then based off that figure out what type of node to add to LNAST.
  switch (ntype) {
    case And_Op:
    case Or_Op:
    case Xor_Op: attach_binaryop_node(lnast, parent_node, pin); break;
    case Not_Op: attach_not_node(lnast, parent_node, pin); break;
    case Sum_Op: attach_sum_node(lnast, parent_node, pin); break;
    case LessThan_Op:
    case GreaterThan_Op:
    case LessEqualThan_Op:
    case GreaterEqualThan_Op: attach_compar_node(lnast, parent_node, pin); break;
    case Equals_Op:
    case Mult_Op:
    case Div_Op:
    case Mod_Op:
    case LogicShiftRight_Op:
    case ArithShiftRight_Op:
    case DynamicShiftRight_Op:
    case DynamicShiftLeft_Op:
    case ShiftRight_Op:
    case ShiftLeft_Op: attach_simple_node(lnast, parent_node, pin); break;
    case Join_Op: attach_join_node(lnast, parent_node, pin); break;
    case Pick_Op: attach_pick_node(lnast, parent_node, pin); break;
    case Mux_Op: attach_mux_node(lnast, parent_node, pin); break;
    case SFlop_Op:
    case AFlop_Op: attach_flop_node(lnast, parent_node, pin); break;
    case Latch_Op: attach_latch_node(lnast, parent_node, pin); break;
    case SubGraph_Op: attach_subgraph_node(lnast, parent_node, pin); break;
    default: fmt::print("Op not yet supported in attach_to_lnast\n");
  }
}

void Pass_lgraph_to_lnast::add_bw_in_ln(Lnast& lnast, Lnast_nid& parent_node, const std::string_view& pin_name, const uint32_t& bits) {
  auto tmp_var = create_temp_var(lnast);
  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot(""));
  lnast.add_child(idx_dot, Lnast_node::create_ref(tmp_var));
  lnast.add_child(idx_dot, Lnast_node::create_ref(pin_name));
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign(""));
  lnast.add_child(idx_asg, Lnast_node::create_ref(tmp_var));
  lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(std::to_string(bits))));
}

void Pass_lgraph_to_lnast::handle_io(LGraph* lg, Lnast_nid& parent_lnast_node, Lnast& lnast) {
  /* Any input or output that has its bitwidth specified should add info to the LNAST.
   * As an example, if we had an input x that was 7 bits wide, this would be added:
   *     dot             asg
   *   /  |  \         /     \
   *  T0 $x __bits    T0    0d7    (note that the $ would be % if it was an output)*/

  auto inp_io_node = lg->get_graph_input_node();
  absl::flat_hash_set<std::string_view> inps_visited;
  for (const auto edge : inp_io_node.out_edges()) {
    I(edge.driver.has_name());
    auto pin_name = edge.driver.get_name();
    if (inps_visited.contains(pin_name)) {
      continue;
    }
    inps_visited.insert(pin_name);
    auto bits = edge.get_bits();
    if (bits > 0) {
      // Put input bitwidth info in from_lg_bw_table
      lnast.set_bitwidth(pin_name, bits);
      if (put_bw_in_ln) {
        add_bw_in_ln(lnast, parent_lnast_node,
            lnast.add_string(absl::StrCat("$", pin_name)), bits);
      }
    }

    //if (edge.driver.get_node().get_type().is_input_signed(edge.driver.get_pid())) {
    if (edge.driver.is_signed()) {
      //FIXME: Make sure this has the correct "sign"
      auto temp_var_name = create_temp_var(lnast);

      auto idx_dot = lnast.add_child(parent_lnast_node, Lnast_node::create_dot("sign"));
      lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
      lnast.add_child(idx_dot, Lnast_node::create_ref(pin_name));
      lnast.add_child(idx_dot, Lnast_node::create_ref("__sign"));
      auto idx_asg = lnast.add_child(parent_lnast_node, Lnast_node::create_assign("sign"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
      lnast.add_child(idx_asg, Lnast_node::create_const("true"));
    }
  }

  auto out_io_node = lg->get_graph_output_node();
  for (const auto edge : out_io_node.inp_edges()) {
    auto sink_pid = edge.sink.get_pid();
    auto out_pin = edge.sink.get_node().get_driver_pin(sink_pid);
    I(out_pin.has_name());
    auto pin_name = out_pin.get_name();

    auto bits = edge.get_bits();
    if (bits > 0) {
      // Put output bitwidth info in from_lg_bw_table
      lnast.set_bitwidth(pin_name, bits);
      if (put_bw_in_ln) {
        add_bw_in_ln(lnast, parent_lnast_node,
            lnast.add_string(absl::StrCat("%", pin_name)), bits);
      }
    }
    if (edge.driver.is_signed()) {
      //FIXME: Make sure this has the correct "sign"
      auto temp_var_name = create_temp_var(lnast);

      auto idx_dot = lnast.add_child(parent_lnast_node, Lnast_node::create_dot("sign"));
      lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
      lnast.add_child(idx_dot, Lnast_node::create_ref(pin_name));
      lnast.add_child(idx_dot, Lnast_node::create_ref("__sign"));
      auto idx_asg = lnast.add_child(parent_lnast_node, Lnast_node::create_assign("sign"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
      lnast.add_child(idx_asg, Lnast_node::create_const("true"));
    }
  }
}

// -------- How to convert each LGraph node type to LNAST -------------
void Pass_lgraph_to_lnast::attach_sum_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = AS, 1 = AU, 2 = BS, 3 = BU, 4 = Y... Y = (AS+...+AS+AU+...+AU) - (BS+...+BS+BU+...+BU)
  bool is_add = false;
  bool is_subt = false;
  int  add_count = 0;

  Lnast_nid add_node, subt_node;

  // Determine if we're doing an add, sub, or both.
  auto pin_name = lnast.add_string(dpin_get_name(pin));
  for (const auto inp : pin.get_node().inp_edges()) {
    auto spin = inp.sink;
    if ((spin.get_pid() == 0) || (spin.get_pid() == 1)) {
      add_count++;
      // is_add = true;
    } else {
      is_subt = true;
    }
  }

  if (add_count > 1) {
    is_add = true;
  }

  I(is_add | is_subt);  // If neither, why does this node exist?

  // Now that we know which, create the necessary operation nodes.
  if (is_add & !is_subt) {
    add_node = lnast.add_child(parent_node, Lnast_node::create_plus("plus"));
    lnast.add_child(add_node, Lnast_node::create_ref(pin_name));
  } else if (!is_add & is_subt) {
    subt_node = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));
    /*Note: the next line is a strange workaround but it is important. If we didn't do this, the later
        for loop would try to attach something to "add_node", but we never specified what that was. */
    add_node = subt_node;
    lnast.add_child(subt_node, Lnast_node::create_ref(pin_name));
  } else {
    add_node  = lnast.add_child(parent_node, Lnast_node::create_plus("plus"));
    subt_node = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));

    auto intermediate_var_name = create_temp_var(lnast);
    lnast.add_child(add_node, Lnast_node::create_ref(intermediate_var_name));
    lnast.add_child(subt_node, Lnast_node::create_ref(pin_name));
    lnast.add_child(subt_node, Lnast_node::create_ref(intermediate_var_name));
  }

  // Attach the name of each of the node's inputs to the Lnast operation node we just made.
  for (const auto inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    auto spin = inp.sink;
    // This if statement is used to figure out if the inp_edge is for plus or minus.
    if ((spin.get_pid() == 0) || (spin.get_pid() == 1)) {
      attach_child(lnast, add_node, dpin);
    } else {
      attach_child(lnast, subt_node, dpin);
    }
  }
}

void Pass_lgraph_to_lnast::attach_binaryop_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = A, 0 = Y, 1 = YReduce

  // Check to see if output PID 0 and PID 1 are used.
  bool pid0_used = false;
  bool pid1_used = false;
  Node_pin pid0_pin, pid1_pin;
  //fmt::print("{}\n", pin.get_node().debug_name());
  for (const auto& dpin : pin.get_node().out_connected_pins()) {
    fmt::print("{}\n", dpin.get_pid());
    if (dpin.get_pid() == 0) {
      pid0_used = true;
      pid0_pin = dpin;
    } else {
      pid1_used = true;
      pid1_pin = dpin;
    }

    if (pid0_used && pid1_used) {
      break;
    }
  }

  if (pid0_used) { // Y
    Lnast_nid bop_node;
    switch (pid0_pin.get_node().get_type().op) {
      case And_Op: bop_node = lnast.add_child(parent_node, Lnast_node::create_and("and")); break; //fmt::print("\t{}\n", pid0_pin.get_node().debug_name()); break;
      case Or_Op: bop_node = lnast.add_child(parent_node, Lnast_node::create_or("or")); break;
      case Xor_Op: bop_node = lnast.add_child(parent_node, Lnast_node::create_xor("xor")); break;
      default: fmt::print("Error: attach_binaryop_node doesn't support given node type\n"); I(false);
    }
    lnast.add_child(bop_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pid0_pin))));

    // Attach the name of each of the node's inputs to the Lnast operation node we just made.
    attach_children_to_node(lnast, bop_node, pid0_pin);
  }

  if (pid1_used) { // YReduce
    attach_binary_reduc(lnast, parent_node, pid1_pin);
  }
}

void Pass_lgraph_to_lnast::attach_binary_reduc(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pid1_pin) {
  // First, concatenate everything together {A0, A1, ...}
  std::queue<Node_pin> dpins;
  auto                 bits_to_shift = 0;
  auto                 total_bits = 0;
  for (auto &inp_edge : pid1_pin.get_node().inp_edges()) {
    dpins.push(inp_edge.driver);
    bits_to_shift += inp_edge.driver.get_bits();
    total_bits += inp_edge.driver.get_bits();
  }

  bool only_one_pin = false;
  std::string_view concat_name;
  if (dpins.size() == 1) {
    // Only one element... No concatenation required.
    only_one_pin = true;

  } else {
    absl::flat_hash_set<std::string_view> interm_names;
    while (dpins.size() > 1) {
      bits_to_shift -= dpins.front().get_bits();
      auto interm_name = create_temp_var(lnast);
      interm_names.insert(interm_name);

      auto idx_sl = lnast.add_child(parent_node, Lnast_node::create_shift_left("yred_sl"));
      lnast.add_child(idx_sl, Lnast_node::create_ref(interm_name));
      attach_child(lnast, idx_sl, dpins.front());
      lnast.add_child(idx_sl, Lnast_node::create_const(lnast.add_string(std::to_string(bits_to_shift))));
      dpins.pop();
    }

    auto temp_or_name = create_temp_var(lnast);
    auto idx_or = lnast.add_child(parent_node, Lnast_node::create_or("yred_or"));
    lnast.add_child(idx_or, Lnast_node::create_ref(temp_or_name));
    for (auto& strv : interm_names) {
      lnast.add_child(idx_or, Lnast_node::create_ref(strv));
    }
    attach_child(lnast, idx_or, dpins.front());

    concat_name = temp_or_name;
  }

  if (pid1_pin.get_node().get_type().op == And_Op) {
    // AndReduc is same as ConcatVal == 2^(bw(ConcatVal)) - 1
    int eq_const = pow(2, total_bits) - 1;
    //FIXME: Doing the above way doesn't work for >63 bits or for signed nums

    auto eq_idx = lnast.add_child(parent_node, Lnast_node::create_same("yred_same"));
    lnast.add_child(eq_idx, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pid1_pin))));
    if (only_one_pin) {
      attach_child(lnast, eq_idx, dpins.front());
    } else {
      lnast.add_child(eq_idx, Lnast_node::create_ref(concat_name));
    }
    lnast.add_child(eq_idx, Lnast_node::create_const(lnast.add_string(std::to_string(eq_const))));

  } else if (pid1_pin.get_node().get_type().op == Or_Op) {
    // OrReduc is same as ConcatVal != 0
    auto temp_eq_name = create_temp_var(lnast);
    auto eq_idx = lnast.add_child(parent_node, Lnast_node::create_same("yred_same"));
    lnast.add_child(eq_idx, Lnast_node::create_ref(temp_eq_name));
    if (only_one_pin) {
      attach_child(lnast, eq_idx, dpins.front());
    } else {
      lnast.add_child(eq_idx, Lnast_node::create_ref(concat_name));
    }
    lnast.add_child(eq_idx, Lnast_node::create_const("0"));

    auto not_idx = lnast.add_child(parent_node, Lnast_node::create_not("yred_not"));
    lnast.add_child(not_idx, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pid1_pin))));
    lnast.add_child(not_idx, Lnast_node::create_ref(temp_eq_name));

  } else if (pid1_pin.get_node().get_type().op == Xor_Op) {
    auto par_idx = lnast.add_child(parent_node, Lnast_node::create_parity("yred_par"));
    lnast.add_child(par_idx, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pid1_pin))));
    if (only_one_pin) {
      attach_child(lnast, par_idx, dpins.front());
    } else {
      lnast.add_child(par_idx, Lnast_node::create_ref(concat_name));
    }
  } else {
    fmt::print("Error: attach_binaryop_node doesn't support given node type\n");
    I(false);
  }
}

void Pass_lgraph_to_lnast::attach_not_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  auto not_node = lnast.add_child(parent_node, Lnast_node::create_not("not"));
  lnast.add_child(not_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));

  attach_children_to_node(lnast, not_node, pin);
}

void Pass_lgraph_to_lnast::attach_join_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  std::stack<Node_pin> dpins;
  auto                 bits_to_shift = 0;
  /*This stack method works because the inp_edges iterator goes from edges w/ lowest sink pid to highest
   *  and the highest sink pid correlates to the most significant part of the concatenation. */
  for (const auto inp : pin.get_node().inp_edges_ordered()) {
    dpins.push(inp.driver);
    bits_to_shift += inp.driver.get_bits();
    //fmt::print("\tjoin -- {} {}\n", inp.sink.get_pid(), bits_to_shift);
  }

  fmt::print("{}\n", pin.get_node().debug_name());
  I(dpins.size() != 0);
  if (dpins.size() < 2) {
    // If this join node only has 1 input, it's really just an assign.
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign(""));
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
    attach_child(lnast, idx_asg, dpins.top());
    return;
  }

  absl::flat_hash_set<std::string_view> interm_names;
  while (dpins.size() > 1) {
    bits_to_shift -= dpins.top().get_bits();
    auto interm_name = create_temp_var(lnast);
    interm_names.insert(interm_name);

    auto idx_sl = lnast.add_child(parent_node, Lnast_node::create_shift_left("join_sl"));
    lnast.add_child(idx_sl, Lnast_node::create_ref(interm_name));
    attach_child(lnast, idx_sl, dpins.top());
    lnast.add_child(idx_sl, Lnast_node::create_const(lnast.add_string(std::to_string(bits_to_shift))));
    dpins.pop();
  }

  auto idx_or = lnast.add_child(parent_node, Lnast_node::create_or("join_or"));
  lnast.add_child(idx_or, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
  for (auto& strv : interm_names) {
    lnast.add_child(idx_or, Lnast_node::create_ref(strv));
  }
  attach_child(lnast, idx_or, dpins.top());
}

void Pass_lgraph_to_lnast::attach_pick_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = A, 1 = Offset... Y = A[Offset+(Y_Bitwidth) : Offset]
  /* Y = pick(X, off) in LGraph form turns into Lnast form as:
   * IMPROVED/FUTURE TRANS:   |||  WORKING TRANS:
   *  range_op   bit_select   |||     shr        and
   *   / | \       / | \      |||    / | \      / | \
   * T0  lo hi    Y  X  T0    |||  T0  X off   Y T0 xyz
   *                          |||
   * where lo = offset,       |||  where xyz = 0b111...1 where
   * hi = offset + y.bits()-1 |||  the # of 1s = y.bits() */

  Node_pin offset_pin, var_pin;
  for (const auto inp : pin.get_node().inp_edges()) {
    if (inp.sink.get_pid() == 0) {
      var_pin = inp.driver;
    } else if (inp.sink.get_pid() == 1) {
      offset_pin = inp.driver;
    } else {
      I(false);  // No other sink pin id should be used.
    }
  }

/* FIXME: Currently range and bit_select nodes are not supported
 * on the LN->LG interface. So for now, we can mimic their
 * functionality by using a shift right and AND node. Once range
 * and bit_select work on that interface, go back to that translation. */
#if 1
  std::string bit_str = "0b";
  for (auto i = 0; i < pin.get_bits(); i++) {
    bit_str = absl::StrCat(bit_str, "1");
  }
  bit_str = absl::StrCat(bit_str, "u");

  auto pin_str = lnast.add_string(lnast.add_string(dpin_get_name(pin)));
  auto t0_str  = create_temp_var(lnast);

  auto shr_idx = lnast.add_child(parent_node, Lnast_node::create_shift_right(""));
  lnast.add_child(shr_idx, Lnast_node::create_ref(t0_str));
  attach_child(lnast, shr_idx, var_pin);
  attach_child(lnast, shr_idx, offset_pin);

  auto and_idx = lnast.add_child(parent_node, Lnast_node::create_and(""));
  lnast.add_child(and_idx, Lnast_node::create_ref(pin_str));
  lnast.add_child(and_idx, Lnast_node::create_ref(t0_str));
  lnast.add_child(and_idx, Lnast_node::create_const(lnast.add_string(bit_str)));
#endif
#if 0
  auto pin_str = lnast.add_string(lnast.add_string(dpin_get_name(pin)));
  auto t0_str  = create_temp_var(lnast);
  auto lo_str  = lnast.add_string(offset_pin.get_node().get_type_const().to_pyrope());
  auto hi_val  = offset_pin.get_node().get_type_const() + Lconst(pin.get_bits() - 1);
  auto hi_str  = lnast.add_string(hi_val.to_pyrope());

  auto range_node = lnast.add_child(parent_node, Lnast_node::create_range("range_pick"));
  lnast.add_child(range_node, Lnast_node::create_ref(t0_str));
  lnast.add_child(range_node, Lnast_node::create_const(lo_str));
  lnast.add_child(range_node, Lnast_node::create_const(hi_str));

  auto bitsel_node = lnast.add_child(parent_node, Lnast_node::create_bit_select("bitsel_pick"));
  lnast.add_child(bitsel_node, Lnast_node::create_ref(pin_str));
  attach_child(lnast, bitsel_node, var_pin);
  lnast.add_child(bitsel_node, Lnast_node::create_ref(t0_str));
#endif
}

void Pass_lgraph_to_lnast::attach_compar_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // Y = (As|Au) [comparator] (Bs|Bu)... Note: the | means one or the other, can't have both.
  // If there are multiple pins like (lessthan A1, B1 B2) then this is the same as A1 < B1 & A1 < B2.

  /* For each A pin, we have to compare that against each B pin.
   * We know which is which based off the inp_edge's sink pin pid. */
  std::vector<Node_pin> a_pins, b_pins;
  for (const auto inp : pin.get_node().inp_edges()) {
    if ((inp.sink.get_pid() == 0) || (inp.sink.get_pid() == 1)) {
      a_pins.push_back(inp.driver);
    } else {
      b_pins.push_back(inp.driver);
    }
  }

  int comparisons = a_pins.size() * b_pins.size();
  if (comparisons == 1) {
    // If only 1 comparison needs to be done, we don't need to do any extra & at the end.
    Lnast_nid comp_node;
    switch (pin.get_node().get_type().op) {
      case LessThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_lt("lt")); break;
      case LessEqualThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_le("lte")); break;
      case GreaterThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_gt("gt")); break;
      case GreaterEqualThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_ge("gte")); break;
      default: fmt::print("Error: invalid node type in attach_compar_node\n"); I(false);
    }
    lnast.add_child(comp_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
    attach_child(lnast, comp_node, a_pins[0]);
    attach_child(lnast, comp_node, b_pins[0]);

  } else {
    /*If there is more than 1 comparison that needs to be done, then we have create each
        separate comparison then & them all together. */
    std::vector<std::string_view> temp_var_list;
    for (const auto apin : a_pins) {
      for (const auto bpin : b_pins) {
        Lnast_nid comp_node;
        switch (pin.get_node().get_type().op) {
          case LessThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_lt("lt_i")); break;
          case LessEqualThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_le("lte_i")); break;
          case GreaterThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_gt("gt_i")); break;
          case GreaterEqualThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_ge("gte_i")); break;
          default: fmt::print("Error: invalid node type in attach_compar_node\n"); I(false);
        }
        auto temp_var_name = create_temp_var(lnast);
        temp_var_list.push_back(temp_var_name);
        lnast.add_child(comp_node, Lnast_node::create_ref(temp_var_name));

        attach_child(lnast, comp_node, apin);
        attach_child(lnast, comp_node, bpin);
      }
    }

    auto and_node = lnast.add_child(parent_node, Lnast_node::create_and("and"));
    lnast.add_child(and_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
    for (const auto& temp_var : temp_var_list) {
      lnast.add_child(and_node, Lnast_node::create_ref(temp_var));
    }
  }
}

void Pass_lgraph_to_lnast::attach_simple_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  Lnast_nid simple_node;
  switch (pin.get_node().get_type().op) {
    case Equals_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_same("==")); break;
    case Mult_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_mult("mult")); break;
    case Div_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_div("div")); break;
    case Mod_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_mod("mod")); break;
    case LogicShiftRight_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_logic_shift_right("l_shr")); break;
    case ArithShiftRight_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_arith_shift_right("a_shr")); break;
    case DynamicShiftRight_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_right("d_shr")); break;
    case DynamicShiftLeft_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_left("d_shl")); break;
    case ShiftRight_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr")); break;
    case ShiftLeft_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_shift_left("shl")); break;
    default: fmt::print("Error: attach_simple_node unknown node type provided\n"); I(false);
  }
  lnast.add_child(simple_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));

  // Attach the name of each of the node's inputs to the Lnast operation node we just made.
  attach_children_to_node(lnast, simple_node, pin);
}

void Pass_lgraph_to_lnast::attach_mux_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = S, 1 = A, 2 = B, ...
  // Y = ~SA | SB

  std::queue<XEdge> mux_vals;
  Node_pin sel_pin;
  for (const auto inp : pin.get_node().inp_edges_ordered()) {
    if (inp.sink.get_pid() == 0) {  // If mux selector S, create if's "condition"
      sel_pin = inp.driver;
    } else {
      mux_vals.push(inp);
    }
  }
  I(mux_vals.size() >= 2);

  // Set up each cond value
  std::queue<std::string_view> temp_vars;
  for (long unsigned int i = 0; i < mux_vals.size() - 1; i++) {
    auto temp_var = create_temp_var(lnast);
    temp_vars.push(temp_var);

    auto eq_idx = lnast.add_child(parent_node, Lnast_node::create_same(""));
    lnast.add_child(eq_idx, Lnast_node::create_ref(temp_var));
    attach_child(lnast, eq_idx, sel_pin);
    lnast.add_child(eq_idx, Lnast_node::create_const(lnast.add_string(std::to_string(i))));
  }

  // Specify var being assigned to is in upper scope (not in if-else scope)
  //auto asg_idx_i = lnast.add_child(parent_node, Lnast_node::create_assign(""));
  //lnast.add_child(asg_idx_i, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
  //lnast.add_child(asg_idx_i, Lnast_node::create_const("0b0")); //FIXME: Better to use 0b? but causes problems on FIRRTL interface

  // Specify cond + create stmt for each mux val, except last.
  auto if_node  = lnast.add_child(parent_node, Lnast_node::create_if("mux"));
  while (mux_vals.size() > 1) {
    lnast.add_child(if_node, Lnast_node::create_cond(temp_vars.front()));
    temp_vars.pop();

    auto stmt_idx = lnast.add_child(if_node, Lnast_node::create_stmts(get_new_seq_name(lnast)));

    auto asg_idx = lnast.add_child(stmt_idx, Lnast_node::create_assign(""));
    lnast.add_child(asg_idx, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
    attach_child(lnast, asg_idx, mux_vals.front().driver);
    mux_vals.pop();
  }

  // Attach last mux input, with no condition (since it is "else" case)
  auto stmt_idx = lnast.add_child(if_node, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  auto asg_idx = lnast.add_child(stmt_idx, Lnast_node::create_assign(""));
  lnast.add_child(asg_idx, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
  attach_child(lnast, asg_idx, mux_vals.front().driver);
}

void Pass_lgraph_to_lnast::attach_flop_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = CLK, 1 = Din, 2 = En, 3 = Reset, 4 = Set Val, 5 = Clk Polarity (5 is not used for AFlop)
  bool     has_clk   = false;
  bool     has_din   = false;
  bool     has_en    = false;
  bool     has_reset = false;
  bool     has_set_v = false;
  bool     has_pola  = false;
  Node_pin clk_pin, din_pin, en_pin, reset_pin, set_v_pin, pola_pin;
  for (const auto inp : pin.get_node().inp_edges()) {
    if (inp.sink.get_pid() == 0) {
      I(!has_clk);
      has_clk = true;
      clk_pin = inp.driver;

    } else if (inp.sink.get_pid() == 1) {
      I(!has_din);
      has_din = true;
      din_pin = inp.driver;

    } else if (inp.sink.get_pid() == 2) { //FIXME: Not using en right now.
      I(!has_en);
      has_en = true;
      en_pin = inp.driver;

    } else if (inp.sink.get_pid() == 3) {
      I(!has_reset);
      has_reset = true;
      reset_pin = inp.driver;

    } else if (inp.sink.get_pid() == 4) {
      I(!has_set_v);
      has_set_v = true;
      set_v_pin = inp.driver;

    } else if (inp.sink.get_pid() == 5) {
      I(!has_pola);
      has_pola = true;
      pola_pin = inp.driver;

    } else {
      I(false); // There shouldn't be any other inputs to a flop.
    }
  }
  I(has_din && has_clk);  // A flop at least has to have the input and clock, others are optional/have defaults.

  std::string_view pin_name = lnast.add_string(pin.get_name());

  // Set __fwd = false
  auto temp_var_namefw = create_temp_var(lnast);
  auto dot_fw_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_fwd"));
  lnast.add_child(dot_fw_node, Lnast_node::create_ref(temp_var_namefw));
  lnast.add_child(dot_fw_node, Lnast_node::create_ref(pin_name));
  lnast.add_child(dot_fw_node, Lnast_node::create_ref("__fwd"));
  auto asg_fw_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop_fwd"));
  lnast.add_child(asg_fw_node, Lnast_node::create_ref(temp_var_namefw));
  lnast.add_child(asg_fw_node, Lnast_node::create_const("false"));

  // Specify if async reset
  if (pin.get_node().get_type().op == AFlop_Op) {
    auto temp_var_name = create_temp_var(lnast);

    auto dot_asr_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_async"));
    lnast.add_child(dot_asr_node, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(dot_asr_node, Lnast_node::create_ref(pin_name));
    lnast.add_child(dot_asr_node, Lnast_node::create_ref("__reset_async"));

    auto asg_asr_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop_async"));
    lnast.add_child(asg_asr_node, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(asg_asr_node, Lnast_node::create_const("true"));
  }

  if (has_clk) {
    auto temp_var_name = create_temp_var(lnast);

    auto dot_clk_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_clk"));
    lnast.add_child(dot_clk_node, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(dot_clk_node, Lnast_node::create_ref(pin_name));
    lnast.add_child(dot_clk_node, Lnast_node::create_ref("__clk_pin"));

    auto asg_clk_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop_clk"));
    lnast.add_child(asg_clk_node, Lnast_node::create_ref(temp_var_name));
    attach_child(lnast, asg_clk_node, clk_pin);
  }

  if (has_reset) {
    auto temp_var_name = create_temp_var(lnast);

    auto dot_rst_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_rst"));
    lnast.add_child(dot_rst_node, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(dot_rst_node, Lnast_node::create_ref(pin_name));
    lnast.add_child(dot_rst_node, Lnast_node::create_ref("__reset_pin"));

    auto asg_rst_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop_rst"));
    lnast.add_child(asg_rst_node, Lnast_node::create_ref(temp_var_name));
    attach_child(lnast, asg_rst_node, reset_pin);
  }

  if (has_set_v) {
    // FIXME: Not sure what this is yet. Initial value?
  }

  if (has_pola) {
    I(pin.get_node().get_type().op != AFlop_Op);
    auto temp_var_name = create_temp_var(lnast);
    auto dot_pol       = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_pol"));
    lnast.add_child(dot_pol, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(dot_pol, Lnast_node::create_ref(pin_name));
    lnast.add_child(dot_pol, Lnast_node::create_ref("__posedge"));

    auto asg_pol = lnast.add_child(parent_node, Lnast_node::create_assign("asg_pol"));
    lnast.add_child(asg_pol, Lnast_node::create_ref(temp_var_name));
    if (pola_pin.get_node().get_type_op() == Const_Op) {
      if (pola_pin.get_node().get_type_const().to_firrtl() == "1") {
        lnast.add_child(asg_pol, Lnast_node::create_const("true"));
      } else {
        lnast.add_child(asg_pol, Lnast_node::create_const("false"));
      }
    } else {
      lnast.add_child(asg_pol, Lnast_node::create_const("false"));
    }
  }

  // Perform actual assignment
  Lnast_nid idx_asg;
  if (has_en) {
    auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if("flop"));
    attach_cond_child(lnast, idx_if, en_pin);
    auto idx_stmt = lnast.add_child(idx_if, Lnast_node::create_stmts(""));
    idx_asg = lnast.add_child(idx_stmt, Lnast_node::create_dp_assign(""));
  } else {
    idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("flop"));
  }
  lnast.add_child(idx_asg, Lnast_node::create_ref(pin_name));
  attach_child(lnast, idx_asg, din_pin);
}

void Pass_lgraph_to_lnast::attach_latch_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = Din, 1 = En, 2 = ???
  bool     has_din   = false;
  bool     has_en    = false;
  Node_pin din_pin, en_pin;
  for (const auto inp : pin.get_node().inp_edges()) {
    if (inp.sink.get_pid() == 0) {
      I(!has_din);
      has_din = true;
      din_pin = inp.driver;
    } else if (inp.sink.get_pid() == 1) {
      I(!has_en);
      has_en = true;
      en_pin = inp.driver;
    } else if (inp.sink.get_pid() == 2) {
      // FIXME: What does this pin mean?
    }
  }
  I(has_din && has_en); // A latch at least has to have the din and enable.

  std::string_view pin_name = lnast.add_string(pin.get_name());

  // Set __fwd = false
  auto temp_var_namefw = create_temp_var(lnast);
  auto dot_fw_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_latch_fwd"));
  lnast.add_child(dot_fw_node, Lnast_node::create_ref(temp_var_namefw));
  lnast.add_child(dot_fw_node, Lnast_node::create_ref(pin_name));
  lnast.add_child(dot_fw_node, Lnast_node::create_ref("__fwd"));
  auto asg_fw_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_latch_fwd"));
  lnast.add_child(asg_fw_node, Lnast_node::create_ref(temp_var_namefw));
  lnast.add_child(asg_fw_node, Lnast_node::create_const("false"));

  // Set __latch = true
  auto tmp_var = create_temp_var(lnast);
  auto idx_dotl = lnast.add_child(parent_node, Lnast_node::create_dot(""));
  lnast.add_child(idx_dotl, Lnast_node::create_ref(tmp_var));
  lnast.add_child(idx_dotl, Lnast_node::create_ref(pin_name));
  lnast.add_child(idx_dotl, Lnast_node::create_ref("__latch"));
  auto idx_asgl = lnast.add_child(parent_node, Lnast_node::create_assign(""));
  lnast.add_child(idx_asgl, Lnast_node::create_ref(tmp_var));
  lnast.add_child(idx_asgl, Lnast_node::create_const("true"));

  // if en: set latch val to din
  auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if("latch"));
  attach_cond_child(lnast, idx_if, en_pin);
  auto idx_stmt = lnast.add_child(idx_if, Lnast_node::create_stmts(""));
  auto idx_asg = lnast.add_child(idx_stmt, Lnast_node::create_dp_assign(""));
  lnast.add_child(idx_asg, Lnast_node::create_ref(pin_name));
  attach_child(lnast, idx_asg, din_pin);
}

void Pass_lgraph_to_lnast::attach_subgraph_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  const auto &sub = pin.get_node().get_type_sub_node();
  fmt::print("instance_name:{}, subgraph->get_name():{}\n", pin.get_node().get_name(), sub.get_name());

  // Create tuple names for submodule IO.
  auto inp_tup_name  = lnast.add_string(absl::StrCat("inp_", pin.get_node().get_name()));
  auto out_tup_name = lnast.add_string(absl::StrCat("out_", pin.get_node().get_name()));

  // Create + instantiate input tuple.
  auto args_idx = lnast.add_child(parent_node, Lnast_node::create_tuple("args_tuple"));
  lnast.add_child(args_idx, Lnast_node::create_ref(inp_tup_name));
  for (const auto inp : pin.get_node().inp_edges()) {
    auto port_name = inp.sink.get_type_sub_io_name();
    auto idx_asg = lnast.add_child(args_idx, Lnast_node::create_assign("sb_arg_set"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(port_name));
    attach_child(lnast, idx_asg, inp.driver);
  }

  // Create actual call to submodule.
  auto func_call_node = lnast.add_child(parent_node, Lnast_node::create_func_call("func_call"));
  lnast.add_child(func_call_node, Lnast_node::create_ref(out_tup_name));
  lnast.add_child(func_call_node, Lnast_node::create_ref(sub.get_name()));
  lnast.add_child(func_call_node, Lnast_node::create_ref(inp_tup_name));

  // Create output
  for (const auto dpin : pin.get_node().out_connected_pins()) {
    auto port_name = dpin.get_type_sub_io_name();
    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_dot("sb_out_set"));
    attach_child(lnast, idx_asg, dpin);
    lnast.add_child(idx_asg, Lnast_node::create_ref(out_tup_name));
    lnast.add_child(idx_asg, Lnast_node::create_ref(port_name));
  }
}

//------------- Helper Functions ------------
void Pass_lgraph_to_lnast::attach_children_to_node(Lnast& lnast, Lnast_nid& op_node, const Node_pin& pin) {
  for (const auto inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    attach_child(lnast, op_node, dpin);
  }
}

/* Purpose of this is so that whenever we have to have the RHS of
 * an expression mapped to LNAST, it's possible that it might be
 * a module input. If it is not a module input, just add a
 * node that has the pin's name. If it is a module input,
 * add the "$" in front of it. */
void Pass_lgraph_to_lnast::attach_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin& dpin) {
  // The input "dpin" needs to be a driver pin.
  if (dpin.get_node().is_graph_input()) {
    // If the input to the node is from a GraphIO node (it's a module input), add the $ in front.
    auto dpin_name = dpin_get_name(dpin);
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("$", dpin_name))));
  } else if (dpin.get_node().is_graph_output()) {
    //auto out_driver_name = lnast.add_string(get_driver_of_output(dpin));
    auto out_driver_name = lnast.add_string(absl::StrCat("%", dpin.get_name()));//FIXME: Check up on later. This used to cause problems with Yosys (see above line for what was used instead)
    lnast.add_child(op_node,
                    Lnast_node::create_ref(out_driver_name));  // lnast.add_string(absl::StrCat(prefix, "%", dpin.get_name()))));
  } else if ((dpin.get_node().get_type().op == AFlop_Op) || (dpin.get_node().get_type().op == SFlop_Op)) {
    // dpin_name is already persistent, no need to do add_string but cleaner
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(dpin.get_name())));
  } else if (dpin.get_node().get_type().op == Const_Op) {
    lnast.add_child(op_node, Lnast_node::create_const(lnast.add_string(dpin.get_node().get_type_const().to_pyrope())));
  } else {
    auto dpin_name = dpin_get_name(dpin);
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(dpin_name)));
  }
}

/* This is the same as above, but instead of making ref/const nodes,
 * we instead make cond nodes. */
void Pass_lgraph_to_lnast::attach_cond_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin& dpin) {
  // The input "dpin" needs to be a driver pin.
  if (dpin.get_node().is_graph_input()) {
    // If the input to the node is from a GraphIO node (it's a module input), add the $ in front.
    auto dpin_name = dpin_get_name(dpin);
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(absl::StrCat("$", dpin_name))));
  } else if (dpin.get_node().is_graph_output()) {
    auto dpin_name = dpin_get_name(dpin);
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(absl::StrCat("%", dpin_name))));
  } else if ((dpin.get_node().get_type().op == AFlop_Op) || (dpin.get_node().get_type().op == SFlop_Op)) {
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(dpin.get_name())));
  } else if (dpin.get_node().get_type().op == Const_Op) {
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(dpin.get_node().get_type_const().to_pyrope())));
  } else {
    auto dpin_name = dpin_get_name(dpin);
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(dpin_name)));
  }
}

/* Since having statements like "%x = %y + a" causes problems in
 * Yosys Verilog generation, we instead return the name of the
 * thing that drives %y. So in the case where some %y = T0, the
 * above statement becomes "%x = T0 + a". This function returns
 * the name of the driver of %y in this case (T0). */
std::string_view Pass_lgraph_to_lnast::get_driver_of_output(const Node_pin dpin) {
  //auto out_pin = get_driver_pin(sink_pid);

  //FIXME: using get_name in here was causing a failure on async.v
  for (const auto inp : dpin.get_node().inp_edges()) {
    if (inp.sink.get_pid() == dpin.get_pid()) {
      return inp.driver.get_name();
    }
  }

  I(false);   // There should always be some driver.
  return "";  // Here just so no warnings in compiler.
  // FIXME: Perhaps change this instead to just "0d0" (though the place this calls would have to be const not ref).
}

/* If a driver pin's name includes a "%" and is not an output of the
 * design, then it's an SSA variable. Thus, if it is an SSA variable
 * I need to remove the "%". */
std::string_view Pass_lgraph_to_lnast::dpin_get_name(const Node_pin dpin) {
  if (dpin.get_name().substr(0, 1) == "%") {
    return dpin.get_name().substr(1);
  } else {
    return dpin.get_name();
  }
}

std::string_view Pass_lgraph_to_lnast::get_new_seq_name(Lnast& lnast) {
  auto seq_name = lnast.add_string(absl::StrCat("SEQ", seq_count));
  seq_count++;
  return seq_name;
}

std::string_view Pass_lgraph_to_lnast::create_temp_var(Lnast& lnast) {
  auto temp_var_name = lnast.add_string(absl::StrCat("___L", temp_var_count));
  temp_var_count++;
  return temp_var_name;
}
