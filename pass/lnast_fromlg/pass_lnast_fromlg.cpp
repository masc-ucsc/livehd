//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnast_fromlg.hpp"

#include <queue>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lbench.hpp"
#include "lgedgeiter.hpp"

// Node colors
#define WHITE 0
#define GREY  1
#define BLACK 2

static Pass_plugin sample("pass_lnast_fromlg", Pass_lnast_fromlg::setup);

void Pass_lnast_fromlg::setup() {
  Eprp_method m1("pass.lnast_fromlg", "translates LGraph to LNAST", &Pass_lnast_fromlg::trans);
  // For bw_in_ln, if __bits are not put in the LNAST then they can be accessed using bw_table in LNAST.
  m1.add_label_optional("bw_in_ln", "true/false: put __ubits or __sbits nodes in LNAST?", "true");
  register_pass(m1);
}

Pass_lnast_fromlg::Pass_lnast_fromlg(const Eprp_var& var) : Pass("pass.lnast_fromlg", var) {}

void Pass_lnast_fromlg::trans(Eprp_var& var) {
  Lbench b("pass.LNAST_FROMLG_trans");

  Pass_lnast_fromlg p(var);

  for (const auto& l : var.lgs) {
    p.do_trans(l, var, l->get_name());  // l->get_name gives the name of the top module (generally same as that of file name)
  }
}

void Pass_lnast_fromlg::do_trans(LGraph* lg, Eprp_var& var, std::string_view module_name) {
  if (var.has_label("bw_in_ln")) {
    if (var.get("bw_in_ln") == "false") {
      put_bw_in_ln = false;
    }
  }
  temp_var_count = 0;
  seq_count      = 0;

  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(module_name);
  lnast->set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, lg->get_name())));
  auto idx_stmts = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts(get_new_seq_name(*lnast)));

  handle_io(lg, idx_stmts, *lnast);
  initial_tree_coloring(lg, *lnast);

  begin_transformation(lg, *lnast, idx_stmts);

  // lnast->dump();

  var.add(std::move(lnast));
}

void Pass_lnast_fromlg::initial_tree_coloring(LGraph* lg, Lnast& lnast) {
  for (const auto& node : lg->fast()) {
    auto node_editable = node;
    node_editable.set_color(WHITE);

    // Look at dpins for each node. If unnamed, give dpin a name.
    for (const auto& dpin : node_editable.out_connected_pins()) {
      auto dpin_editable = dpin;
      auto ntype         = dpin.get_node().get_type_op();

      if (!dpin_editable.has_name() && !((ntype == Ntype_op::IO) || (ntype == Ntype_op::Const))) {
        if (ntype == Ntype_op::Mux) {
          // WARNING: Need to use _._ because it allows SSA (needed for if)
          auto temp_var_name = lnast.add_string(absl::StrCat("_._L", temp_var_count++));
          dpin_set_map_name(dpin_editable, temp_var_name);
        } else {
          dpin_set_map_name(dpin_editable, create_temp_var(lnast));
        }
        if ((ntype == Ntype_op::Sflop) || (ntype == Ntype_op::Aflop) || (ntype == Ntype_op::Fflop) || (ntype == Ntype_op::Latch)) {
          dpin_set_map_name(dpin_editable, lnast.add_string(absl::StrCat("#", dpin_get_name(dpin_editable))));
        }
      } else if (dpin_editable.has_name() && (dpin_editable.get_name()[0] == '#')) {
        if (!((ntype == Ntype_op::Sflop) || (ntype == Ntype_op::Aflop) || (ntype == Ntype_op::Fflop)
              || (ntype == Ntype_op::Latch))) {
          dpin_set_map_name(dpin_editable, lnast.add_string(dpin_get_name(dpin_editable).substr(1)));
        }
      } else if ((ntype == Ntype_op::Sflop) || (ntype == Ntype_op::Aflop) || (ntype == Ntype_op::Fflop)
                 || (ntype == Ntype_op::Latch)) {
        if (dpin_editable.get_name()[0] != '#') {
          dpin_set_map_name(dpin_editable, lnast.add_string(absl::StrCat("#", dpin_get_name(dpin_editable))));
        }
      }
    }
  }

  lg->get_graph_input_node().set_color(WHITE);
  lg->get_graph_output_node().set_color(WHITE);
}

void Pass_lnast_fromlg::begin_transformation(LGraph* lg, Lnast& lnast, Lnast_nid& ln_node) {
  // note: in graph out node, spin_pid == dpin_pid is always true

  auto out_node = lg->get_graph_output_node();
  out_node.set_color(BLACK);
  for (const auto& inp : out_node.inp_edges()) {
    auto gpio_dpin = out_node.get_driver_pin_raw(inp.sink.get_pid());
    I(gpio_dpin.has_name());

    auto edge_dpin = inp.driver;
    I(!edge_dpin.get_node().is_hierarchical());
    handle_source_node(lg, edge_dpin, lnast, ln_node);

    auto asg_node = lnast.add_child(ln_node, Lnast_node::create_assign("out_asg"));
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
void Pass_lnast_fromlg::handle_source_node(LGraph* lg, Node_pin& pin, Lnast& lnast, Lnast_nid& ln_node) {
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
        auto ntype = editable_pin.get_node().get_type_op();
        if (ntype == Ntype_op::Aflop || ntype == Ntype_op::Sflop || ntype == Ntype_op::Fflop || ntype == Ntype_op::Latch) {
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
    // if (editable_pin.get_node().get_color() == WHITE || editable_pin.get_node().get_color() == GREY) {
    if (editable_pin.get_node().get_color() == WHITE) {
      handle_source_node(lg, editable_pin, lnast, ln_node);
    }
    if (editable_pin.get_node().get_color() == GREY) {
      /* if the node is grey, then we have recursively reached here.
       * Need not process that node in another handle_source_node()
       * Hence reached here as a result of loop in lgraph. */
      continue;
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
  Ntype_op::Memory,
*/
void Pass_lnast_fromlg::attach_to_lnast(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // Specify bitwidth in LNAST table (for code gen purposes)
  auto ntype = pin.get_node().get_type_op();
  if (ntype == Ntype_op::IO || ntype == Ntype_op::Const) {
    // Nothing to do, and don't specify bitwidth.
    return;
  }

  std::string_view name;
  auto             bw = pin.get_bits();
  // if ((bw > 0) & (pin.get_name().substr(0,3) != "___")) {
  if ((bw > 0) & (dpin_get_name(pin).substr(0, 3) != "___")) {
    if (ntype == Ntype_op::Sflop || ntype == Ntype_op::Aflop || ntype == Ntype_op::Latch) {
      /* NOTE->hunter: I decided to only specify reg and IO bw (not
       * wires). If more is needed just widen below condition. */
      name = dpin_get_name(pin);
      lnast.set_bitwidth(name.substr(1), bw);
      if (put_bw_in_ln) {
        // add_bw_in_ln(lnast, parent_node, name, bw);
        add_bw_in_ln(lnast, parent_node, pin, name, bw);
      }
    }
  }

  // Look at pin's node's type, then based off that figure out what type of node to add to LNAST.
  switch (ntype) {
    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor: attach_binaryop_node(lnast, parent_node, pin); break;
    case Ntype_op::Not: attach_not_node(lnast, parent_node, pin); break;
    case Ntype_op::Tposs: attach_tposs_node(lnast, parent_node, pin); break;
    case Ntype_op::Sum: attach_sum_node(lnast, parent_node, pin); break;
    case Ntype_op::LT:
    case Ntype_op::GT: attach_compar_node(lnast, parent_node, pin); break;
    case Ntype_op::EQ:
    case Ntype_op::Mult:
    case Ntype_op::Div:
    // case Mod_Op:
    case Ntype_op::SRA:
    case Ntype_op::SHL: attach_simple_node(lnast, parent_node, pin); break;
    case Ntype_op::Mux: attach_mux_node(lnast, parent_node, pin); break;
    case Ntype_op::Sflop:
    case Ntype_op::Aflop: attach_flop_node(lnast, parent_node, pin); break;
    case Ntype_op::Latch: attach_latch_node(lnast, parent_node, pin); break;
    case Ntype_op::Sub: attach_subgraph_node(lnast, parent_node, pin); break;
    case Ntype_op::Memory: attach_memory_node(lnast, parent_node, pin); break;
    default: Pass::error("Found node {} with op not yet supported in attach_to_lnast", pin.get_node().debug_name());
  }
}

void Pass_lnast_fromlg::add_bw_in_ln(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin, const std::string_view& pin_name,
                                     const uint32_t& bits) {
  /*creates subtree in LN for the "dot" and corresponding "assign" to depict bw
   *          dot                    assign
   *     /     |     \               /    \
   * tmp_var pin_name __bits     tmp_var  const(bits)  */
  auto tmp_var = create_temp_var(lnast);
  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot(""));
  lnast.add_child(idx_dot, Lnast_node::create_ref(tmp_var));
  lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(pin_name)));
  if (!pin.is_io_sign()) {
    lnast.add_child(idx_dot, Lnast_node::create_ref("__ubits"));
  } else {
    lnast.add_child(idx_dot, Lnast_node::create_ref("__sbits"));
  }

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign(""));
  lnast.add_child(idx_asg, Lnast_node::create_ref(tmp_var));
  lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(std::to_string(bits))));
}

void Pass_lnast_fromlg::handle_io(LGraph* lg, Lnast_nid& parent_lnast_node, Lnast& lnast) {
  /* Any input or output that has its bitwidth specified should add info to the LNAST.
   * As an example, if we had an input x that was 7 bits wide, this would be added:
   *     dot             asg
   *   /  |  \         /     \
   *  T0 $x __bits    T0    0d7    (note that the $ would be % if it was an output)*/

  auto                                  inp_io_node = lg->get_graph_input_node();
  absl::flat_hash_set<std::string_view> inps_visited;
  for (const auto& edge : inp_io_node.out_edges()) {
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
        if (has_prefix(pin_name)) {
          I(false, "IO in lgraph should not have %/$");
          // add_bw_in_ln(lnast, parent_lnast_node, lnast.add_string(pin_name), bits);
        } else {
          add_bw_in_ln(lnast, parent_lnast_node, edge.driver, lnast.add_string(absl::StrCat("$", pin_name)), bits);
        }
      }
    }
  }

  auto out_io_node = lg->get_graph_output_node();
  for (const auto& edge : out_io_node.inp_edges()) {
    auto sink_pid = edge.sink.get_pid();
    auto out_pin  = edge.sink.get_node().get_driver_pin_raw(sink_pid);
    I(out_pin.has_name());
    auto pin_name = out_pin.get_name();

    auto bits = edge.get_bits();
    if (bits > 0) {
      // Put output bitwidth info in from_lg_bw_table
      lnast.set_bitwidth(pin_name, bits);
      if (put_bw_in_ln) {
        if (has_prefix(pin_name)) {
          I(false, "IO in lgraph should not have %/$");
          // add_bw_in_ln(lnast, parent_lnast_node, lnast.add_string(pin_name), bits);//adds str to lnast->string_pool
        } else {
          add_bw_in_ln(lnast,
                       parent_lnast_node,
                       out_pin,
                       lnast.add_string(absl::StrCat("%", pin_name)),
                       bits);  // adds str to lnast->string_pool
        }
      }
    }
  }
}

// -------- How to convert each LGraph node type to LNAST -------------
void Pass_lnast_fromlg::attach_sum_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = AS, 1 = AU, 2 = BS, 3 = BU, 4 = Y... Y = (AS+...+AS+AU+...+AU) - (BS+...+BS+BU+...+BU)
  // new version: PID: 0=A, 1=B, else invalid ; we are dealing with signed only now.
  bool is_add    = false;
  bool is_subt   = false;
  int  add_count = 0;

  Lnast_nid add_node, subt_node;

  // Determine if we're doing an add, sub, or both.
  auto pin_name = lnast.add_string(dpin_get_name(pin));
  for (const auto& inp : pin.get_node().inp_edges()) {
    auto spin = inp.sink;
    if (spin.get_pid() == 0) {
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
  for (const auto& inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    auto spin = inp.sink;
    // This if statement is used to figure out if the inp_edge is for plus or minus.
    if (spin.get_pid() == 0) {
      attach_child(lnast, add_node, dpin);
    } else {
      attach_child(lnast, subt_node, dpin);
    }
  }
}

void Pass_lnast_fromlg::attach_binaryop_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = A, 0 = Y, 1 = YReduce

  // Check to see if output PID 0 and PID 1 are used.
  bool     pid0_used = false;
  bool     pid1_used = false;
  Node_pin pid0_pin, pid1_pin;
  // fmt::print("{}\n", pin.get_node().debug_name());
  for (const auto& dpin : pin.get_node().out_connected_pins()) {
    // fmt::print("{}\n", dpin.get_pid());
    if (dpin.get_pid() == 0) {
      pid0_used = true;
      pid0_pin  = dpin;
    } else {
      I(false, "SAKSHI: CHECK: what is this for??");
      pid1_used = true;
      pid1_pin  = dpin;
    }

    if (pid0_used && pid1_used) {
      break;
    }
  }

  if (pid0_used) {  // Y
    Lnast_nid bop_node;
    switch (pid0_pin.get_node().get_type_op()) {
      case Ntype_op::And:
        bop_node = lnast.add_child(parent_node, Lnast_node::create_and("and"));
        break;  // fmt::print("\t{}\n", pid0_pin.get_node().debug_name()); break;
      case Ntype_op::Or: bop_node = lnast.add_child(parent_node, Lnast_node::create_or("or")); break;
      case Ntype_op::Xor: bop_node = lnast.add_child(parent_node, Lnast_node::create_xor("xor")); break;
      default: Pass::error("attach_binaryop_node doesn't support given node type");
    }
    lnast.add_child(bop_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pid0_pin))));

    // Attach the name of each of the node's inputs to the Lnast operation node we just made.
    attach_children_to_node(lnast, bop_node, pid0_pin);
  }

  if (pid1_used) {  // YReduce
    attach_binary_reduc(lnast, parent_node, pid1_pin);
  }
}

void Pass_lnast_fromlg::attach_binary_reduc(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pid1_pin) {
  // First, concatenate everything together {A0, A1, ...}
  std::queue<Node_pin> dpins;
  auto                 bits_to_shift = 0;
  uint32_t             total_bits    = 0;
  for (auto& inp_edge : pid1_pin.get_node().inp_edges()) {
    dpins.push(inp_edge.driver);
    bits_to_shift += inp_edge.driver.get_bits();
    total_bits += inp_edge.driver.get_bits();
  }

  bool             only_one_pin = false;
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
    auto idx_or       = lnast.add_child(parent_node, Lnast_node::create_or("yred_or"));
    lnast.add_child(idx_or, Lnast_node::create_ref(temp_or_name));
    for (auto& strv : interm_names) {
      lnast.add_child(idx_or, Lnast_node::create_ref(strv));
    }
    attach_child(lnast, idx_or, dpins.front());

    concat_name = temp_or_name;
  }

  auto ntype = pid1_pin.get_node().get_type_op();
  if (ntype == Ntype_op::And) {
    // AndReduc is same as ConcatVal == 2^(bw(ConcatVal)) - 1
    std::string rhs_2pow = "0b";
    for (uint32_t i = 0; i < total_bits; i++) {
      rhs_2pow = absl::StrCat(rhs_2pow, "1");
    }

    auto eq_idx = lnast.add_child(parent_node, Lnast_node::create_same("yred_same"));
    lnast.add_child(eq_idx, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pid1_pin))));
    if (only_one_pin) {
      attach_child(lnast, eq_idx, dpins.front());
    } else {
      lnast.add_child(eq_idx, Lnast_node::create_ref(concat_name));
    }
    lnast.add_child(eq_idx, Lnast_node::create_const(lnast.add_string(rhs_2pow)));

  } else if (ntype == Ntype_op::Or) {
    // OrReduc is same as ConcatVal != 0
    auto temp_eq_name = create_temp_var(lnast);
    auto eq_idx       = lnast.add_child(parent_node, Lnast_node::create_same("yred_same"));
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

  } else if (ntype == Ntype_op::Xor) {
    auto par_idx = lnast.add_child(parent_node, Lnast_node::create_parity("yred_par"));
    lnast.add_child(par_idx, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pid1_pin))));
    if (only_one_pin) {
      attach_child(lnast, par_idx, dpins.front());
    } else {
      lnast.add_child(par_idx, Lnast_node::create_ref(concat_name));
    }
  } else {
    Pass::error("attach_binaryop_node doesn't support given node: {}", pid1_pin.get_node().debug_name());
  }
}

void Pass_lnast_fromlg::attach_not_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  auto not_node = lnast.add_child(parent_node, Lnast_node::create_not("not"));
  lnast.add_child(not_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));

  attach_children_to_node(lnast, not_node, pin);
}

void Pass_lnast_fromlg::attach_tposs_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  auto tposs_node = lnast.add_child(parent_node, Lnast_node::create_tposs("Tposs"));
  lnast.add_child(tposs_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));

  attach_children_to_node(lnast, tposs_node, pin);
}

// void Pass_lnast_fromlg::attach_join_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
//  std::stack<Node_pin> dpins;
//  auto                 bits_to_shift = 0;
//  /* This stack method works because the inp_edges iterator goes from edges w/ lowest sink pid to highest
//   * and the highest sink pid correlates to the most significant part of the concatenation. */
//  for (const auto inp : pin.get_node().inp_edges_ordered()) {
//    dpins.push(inp.driver);
//    bits_to_shift += inp.driver.get_bits();
//  }
//
//  I(dpins.size() != 0);
//  if (dpins.size() < 2) {
//    // If this join node only has 1 input, it's really just an assign.
//    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign(""));
//    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
//    attach_child(lnast, idx_asg, dpins.top());
//    return;
//  }
//
//  absl::flat_hash_set<std::string_view> interm_names;
//  while (dpins.size() > 1) {
//    bits_to_shift -= dpins.top().get_bits();
//    auto interm_name = create_temp_var(lnast);
//    interm_names.insert(interm_name);
//
//    auto idx_sl = lnast.add_child(parent_node, Lnast_node::create_shift_left("join_sl"));
//    lnast.add_child(idx_sl, Lnast_node::create_ref(interm_name));
//    attach_child(lnast, idx_sl, dpins.top());
//    lnast.add_child(idx_sl, Lnast_node::create_const(lnast.add_string(std::to_string(bits_to_shift))));
//    dpins.pop();
//  }
//
//  auto idx_or = lnast.add_child(parent_node, Lnast_node::create_or("join_or"));
//  lnast.add_child(idx_or, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
//  for (auto& strv : interm_names) {
//    lnast.add_child(idx_or, Lnast_node::create_ref(strv));
//  }
//  attach_child(lnast, idx_or, dpins.top());
//}

// void Pass_lnast_fromlg::attach_pick_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
//  // PID: 0 = A, 1 = Offset... Y = A[Offset+(Y_Bitwidth) : Offset]
/* Y = pick(X, off) in LGraph form turns into Lnast form as:
 * IMPROVED/FUTURE TRANS:   |||  WORKING TRANS:
 *  range_op   bit_select   |||     shr        and
 *   / | \       / | \      |||    / | \      / | \
 * T0  lo hi    Y  X  T0    |||  T0  X off   Y T0 xyz
 *                          |||
 * where lo = offset,       |||  where xyz = 0b111...1 where
 * hi = offset + y.bits()-1 |||  the # of 1s = y.bits() */
//
//  Node_pin offset_pin, var_pin;
//  for (const auto inp : pin.get_node().inp_edges()) {
//    if (inp.sink.get_pid() == 0) {
//      var_pin = inp.driver;
//    } else if (inp.sink.get_pid() == 1) {
//      offset_pin = inp.driver;
//    } else {
//      I(false);  // No other sink pin id should be used.
//    }
//  }
//
///* FIXME: Currently range and bit_select nodes are not supported
// * on the LN->LG interface. So for now, we can mimic their
// * functionality by using a shift right and AND node. Once range
// * and bit_select work on that interface, go back to that translation. */
//#if 1
//  std::string bit_str = "0b";
//  for (uint32_t i = 0; i < pin.get_bits(); i++) {
//    bit_str = absl::StrCat(bit_str, "1");
//  }
//  bit_str = absl::StrCat(bit_str, "u");
//
//  auto pin_str = lnast.add_string(dpin_get_name(pin));
//  auto t0_str  = create_temp_var(lnast);
//
//  auto shr_idx = lnast.add_child(parent_node, Lnast_node::create_shift_right(""));
//  lnast.add_child(shr_idx, Lnast_node::create_ref(t0_str));
//  attach_child(lnast, shr_idx, var_pin);
//  attach_child(lnast, shr_idx, offset_pin);
//
//  auto and_idx = lnast.add_child(parent_node, Lnast_node::create_and(""));
//  lnast.add_child(and_idx, Lnast_node::create_ref(pin_str));
//  lnast.add_child(and_idx, Lnast_node::create_ref(t0_str));
//  lnast.add_child(and_idx, Lnast_node::create_const(lnast.add_string(bit_str)));
//#endif
//#if 0
//  auto pin_str = dpin_get_name(pin);
//  auto t0_str  = create_temp_var(lnast);
//  auto lo_str  = lnast.add_string(offset_pin.get_node().get_type_const().to_pyrope());
//  auto hi_val  = offset_pin.get_node().get_type_const() + Lconst(pin.get_bits() - 1);
//  auto hi_str  = lnast.add_string(hi_val.to_pyrope());
//
//  auto range_node = lnast.add_child(parent_node, Lnast_node::create_range("range_pick"));
//  lnast.add_child(range_node, Lnast_node::create_ref(t0_str));
//  lnast.add_child(range_node, Lnast_node::create_const(lo_str));
//  lnast.add_child(range_node, Lnast_node::create_const(hi_str));
//
//  auto bitsel_node = lnast.add_child(parent_node, Lnast_node::create_bit_select("bitsel_pick"));
//  lnast.add_child(bitsel_node, Lnast_node::create_ref(pin_str));
//  attach_child(lnast, bitsel_node, var_pin);
//  lnast.add_child(bitsel_node, Lnast_node::create_ref(t0_str));
//#endif
//}

void Pass_lnast_fromlg::attach_compar_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // Y = (As|Au) [comparator] (Bs|Bu)... Note: the | means one or the other, can't have both.
  // If there are multiple pins like (lessthan A1, B1 B2) then this is the same as A1 < B1 & A1 < B2.

  /* For each A pin, we have to compare that against each B pin.
   * We know which is which based off the inp_edge's sink pin pid. */
  std::vector<Node_pin> a_pins, b_pins;
  for (const auto& inp : pin.get_node().inp_edges()) {
    if (inp.sink.get_pid() == 0) {
      a_pins.push_back(inp.driver);
    } else {
      b_pins.push_back(inp.driver);
    }
  }

  int comparisons = a_pins.size() * b_pins.size();
  if (comparisons == 1) {
    // If only 1 comparison needs to be done, we don't need to do any extra & at the end.
    Lnast_nid comp_node;
    switch (pin.get_node().get_type_op()) {
      case Ntype_op::LT: comp_node = lnast.add_child(parent_node, Lnast_node::create_lt("lt")); break;
      // case LessEqualThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_le("lte")); break;
      case Ntype_op::GT: comp_node = lnast.add_child(parent_node, Lnast_node::create_gt("gt")); break;
      // case GreaterEqualThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_ge("gte")); break;
      default: Pass::error("Error: invalid node type in attach_compar_node");
    }
    lnast.add_child(comp_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
    attach_child(lnast, comp_node, a_pins[0]);
    attach_child(lnast, comp_node, b_pins[0]);

  } else {
    /*If there is more than 1 comparison that needs to be done, then we have create each
        separate comparison then & them all together. */
    std::vector<std::string_view> temp_var_list;
    for (const auto& apin : a_pins) {
      for (const auto& bpin : b_pins) {
        Lnast_nid comp_node;
        switch (pin.get_node().get_type_op()) {
          case Ntype_op::LT: comp_node = lnast.add_child(parent_node, Lnast_node::create_lt("lt_i")); break;
          // case LessEqualThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_le("lte_i")); break;
          case Ntype_op::GT: comp_node = lnast.add_child(parent_node, Lnast_node::create_gt("gt_i")); break;
          // case GreaterEqualThan_Op: comp_node = lnast.add_child(parent_node, Lnast_node::create_ge("gte_i")); break;
          default: Pass::error("Error: invalid node type in attach_compar_node");
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

void Pass_lnast_fromlg::attach_simple_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  Lnast_nid simple_node;
  switch (pin.get_node().get_type_op()) {
    case Ntype_op::EQ: simple_node = lnast.add_child(parent_node, Lnast_node::create_same("==")); break;
    case Ntype_op::Mult: simple_node = lnast.add_child(parent_node, Lnast_node::create_mult("mult")); break;
    case Ntype_op::Div: simple_node = lnast.add_child(parent_node, Lnast_node::create_div("div")); break;
    // case Mod_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_mod("mod")); break;
    // case LogicShiftRight_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_logic_shift_right("l_shr")); break;
    case Ntype_op::SRA: simple_node = lnast.add_child(parent_node, Lnast_node::create_arith_shift_right("a_shr")); break;
    // case DynamicShiftRight_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_right("d_shr"));
    // break; case Ntype_op::SHL: simple_node = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_left("d_shl")); break;
    // case ShiftRight_Op: simple_node = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr")); break;
    case Ntype_op::SHL: simple_node = lnast.add_child(parent_node, Lnast_node::create_shift_left("shl")); break;
    default: Pass::error("Error: attach_simple_node unknown node type provided");
  }
  lnast.add_child(simple_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));

  // Attach the name of each of the node's inputs to the Lnast operation node we just made.
  attach_children_to_node(lnast, simple_node, pin);
}

void Pass_lnast_fromlg::attach_mux_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = S, 1 = A, 2 = B, ...
  // Y = ~SA | SB

  std::vector<Node_pin> mux_vals;
  Node_pin              sel_pin;
  for (const auto& inp : pin.get_node().inp_connected_pins()) {
    if (inp.get_pid() == 0) {  // If mux selector S, create if's "condition"
      sel_pin = inp.get_driver_pin();
    } else {
      mux_vals.emplace_back(inp);
    }
  }
  I(mux_vals.size() >= 2);

  // Set up each cond value
  std::vector<std::string_view> temp_vars;
  for (long unsigned int i = 0; i < mux_vals.size() - 1; i++) {
    auto temp_var = create_temp_var(lnast);
    temp_vars.emplace_back(temp_var);

    auto eq_idx = lnast.add_child(parent_node, Lnast_node::create_same(""));
    lnast.add_child(eq_idx, Lnast_node::create_ref(temp_var));
    attach_child(lnast, eq_idx, sel_pin);
    lnast.add_child(eq_idx, Lnast_node::create_const(lnast.add_string(std::to_string(i))));
  }

  // Specify var being assigned to is in upper scope (not in if-else scope)
  auto asg_idx_i = lnast.add_child(parent_node, Lnast_node::create_assign(""));
  auto pin_name  = lnast.add_string(dpin_get_name(pin));  // it should be with _._
  lnast.add_child(asg_idx_i, Lnast_node::create_ref(pin_name));
  auto bits      = pin.get_bits();
  auto const_str = pin.get_bits() == 1 ? "0u1bit" : absl::StrCat("0u", bits, "bits");
  //  auto const_str = dpin_get_name(pin);//fails 3 tests
  lnast.add_child(asg_idx_i, Lnast_node::create_const(lnast.add_string(const_str)));

  // Specify cond + create stmt for each mux val, except last.
  auto if_node = lnast.add_child(parent_node, Lnast_node::create_if("mux"));
  while (mux_vals.size() > 1) {
    lnast.add_child(if_node, Lnast_node::create_cond(temp_vars.front()));
    temp_vars.erase(temp_vars.begin());

    auto stmt_idx = lnast.add_child(if_node, Lnast_node::create_stmts(get_new_seq_name(lnast)));

    auto asg_idx = lnast.add_child(stmt_idx, Lnast_node::create_assign(""));
    lnast.add_child(asg_idx, Lnast_node::create_ref(pin_name));
    attach_child(lnast, asg_idx, mux_vals.front().get_driver_pin());
    mux_vals.erase(mux_vals.begin());
  }

  // Attach last mux input, with no condition (since it is "else" case)
  auto stmt_idx = lnast.add_child(if_node, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  auto asg_idx = lnast.add_child(stmt_idx, Lnast_node::create_assign(""));
  lnast.add_child(asg_idx, Lnast_node::create_ref(pin_name));
  attach_child(lnast, asg_idx, mux_vals.front().get_driver_pin());
}

void Pass_lnast_fromlg::attach_flop_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = CLK, 1 = Din, 2 = En, 3 = Reset, 4 = Set Val, 5 = Clk Polarity (5 is not used for AFlop)//-->OLD
  // new config: PID: 0=reset, 1=initial(reset value), 2=clock, 3=din, 4=enable, 5=posclk, 6=negreset
  bool     has_clk   = false;
  bool     has_din   = false;
  bool     has_en    = false;
  bool     has_reset = false;
  bool     has_set_v = false;
  bool     has_pola  = false;
  bool     has_init  = false;
  Node_pin clk_pin, din_pin, en_pin, reset_pin, set_v_pin, pola_pin, init_pin;
  for (const auto& inp : pin.get_node().inp_edges()) {
    if (inp.sink.get_pid() == 2) {
      I(!has_clk);
      has_clk = true;
      clk_pin = inp.driver;

    } else if (inp.sink.get_pid() == 3) {
      I(!has_din);
      has_din = true;
      din_pin = inp.driver;

    } else if (inp.sink.get_pid() == 4) {
      I(!has_en);
      has_en = true;
      en_pin = inp.driver;

    } else if (inp.sink.get_pid() == 0) {
      I(!has_reset);
      has_reset = true;
      reset_pin = inp.driver;

    } else if (inp.sink.get_pid() == 6) {
      I(!has_set_v);
      has_set_v = true;
      set_v_pin = inp.driver;

    } else if (inp.sink.get_pid() == 5) {  // clock polarity
      I(!has_pola);
      has_pola = true;
      pola_pin = inp.driver;

    } else if (inp.sink.get_pid() == 1) {  //"initial" // reset value
      I(!has_init);
      has_init = true;
      init_pin = inp.driver;
    } else {
      I(false);  // There shouldn't be any other inputs to a flop.
    }
  }
  I(has_din && has_clk);  // A flop at least has to have the input and clock, others are optional/have defaults.

  // std::string_view pin_name = lnast.add_string(pin.get_name());
  std::string_view pin_name = lnast.add_string(dpin_get_name(pin));

  // Set __clk_pin
  /* FIXME: Currently, this is commented out since LN->LG does not support __clk_pin attribute.
   * (It always set clk pin to "clock". Once implemented on LN->LG, this code snippet needs to be put back in. */
#if 0
  auto temp_var_c = create_temp_var(lnast);
  auto dot_clk_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_clk"));
  lnast.add_child(dot_clk_node, Lnast_node::create_ref(temp_var_c));
  lnast.add_child(dot_clk_node, Lnast_node::create_ref(pin_name));
  lnast.add_child(dot_clk_node, Lnast_node::create_ref("__clk_pin"));

  auto asg_clk_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop_clk"));
  lnast.add_child(asg_clk_node, Lnast_node::create_ref(temp_var_c));
  attach_child(lnast, asg_clk_node, clk_pin);
#endif

  // Specify if async reset
  if (pin.get_node().get_type_op() == Ntype_op::Aflop) {
    auto temp_var_name = create_temp_var(lnast);

    auto dot_asr_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_async"));
    lnast.add_child(dot_asr_node, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(dot_asr_node, Lnast_node::create_ref(pin_name));
    lnast.add_child(dot_asr_node, Lnast_node::create_ref("__reset_async"));

    auto asg_asr_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop_async"));
    lnast.add_child(asg_asr_node, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(asg_asr_node, Lnast_node::create_const("true"));
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

  if (has_init) {
    auto temp_var_name = create_temp_var(lnast);

    auto dot_init_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_init"));
    lnast.add_child(dot_init_node, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(dot_init_node, Lnast_node::create_ref(pin_name));
    lnast.add_child(dot_init_node, Lnast_node::create_ref("__reset"));

    auto asg_init_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop_init"));
    lnast.add_child(asg_init_node, Lnast_node::create_ref(temp_var_name));
    attach_child(lnast, asg_init_node, init_pin);
  }

  if (has_set_v) {
    // FIXME: Not sure what this is yet. Initial value?
  }

  if (has_pola) {
    I(pin.get_node().get_type_op() != Ntype_op::Aflop);
    auto temp_var_name = create_temp_var(lnast);
    auto dot_pol       = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_pol"));
    lnast.add_child(dot_pol, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(dot_pol, Lnast_node::create_ref(pin_name));
    lnast.add_child(dot_pol, Lnast_node::create_ref("__posedge"));

    auto asg_pol = lnast.add_child(parent_node, Lnast_node::create_assign("asg_pol"));
    lnast.add_child(asg_pol, Lnast_node::create_ref(temp_var_name));
    if (pola_pin.get_node().get_type_op() == Ntype_op::Const) {
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
    idx_asg       = lnast.add_child(idx_stmt, Lnast_node::create_dp_assign(""));
  } else {
    idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("flop"));
  }
  lnast.add_child(idx_asg, Lnast_node::create_ref(pin_name));
  attach_child(lnast, idx_asg, din_pin);

  /* Create a dot node that points to reg's qpin. Then change name of reg pin in
   * LGraph to match the LHS of that dot node (so all future references to that
   * register are actually referencing the __q_pin).
   * FIXME: In the future, it may just be better to set reg __fwd = false and not do this. */
  auto tmp_var_q = create_temp_var(lnast);
  auto idx_dot_q = lnast.add_child(parent_node, Lnast_node::create_dot(""));
  lnast.add_child(idx_dot_q, Lnast_node::create_ref(tmp_var_q));
  lnast.add_child(idx_dot_q, Lnast_node::create_ref(pin_name));
  lnast.add_child(idx_dot_q, Lnast_node::create_ref("__q_pin"));

  auto editable_pin = pin;
  //	dpin_get_name(editable_pin);
  dpin_set_map_name(editable_pin, tmp_var_q);
  // editable_pin.set_name(tmp_var_q);
}

void Pass_lnast_fromlg::attach_latch_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  // PID: 0 = Din, 1 = En, 2 = ???
  // new PID: 0=posclk, 3=din, 4=enable, default=invalid
  bool     has_din = false;
  bool     has_en  = false;
  Node_pin din_pin, en_pin;
  for (const auto& inp : pin.get_node().inp_edges()) {
    if (inp.sink.get_pid() == 3) {
      I(!has_din);
      has_din = true;
      din_pin = inp.driver;
    } else if (inp.sink.get_pid() == 4) {
      I(!has_en);
      has_en = true;
      en_pin = inp.driver;
    } else if (inp.sink.get_pid() == 0) {
      // FIXME: What does this pin mean?
    }
  }
  I(has_din && has_en);  // A latch at least has to have the din and enable.

  std::string_view pin_name = lnast.add_string(dpin_get_name(pin));  // lnast.add_string(pin.get_name());

  // Set __latch = true
  auto tmp_var  = create_temp_var(lnast);
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
  auto idx_asg  = lnast.add_child(idx_stmt, Lnast_node::create_dp_assign(""));
  lnast.add_child(idx_asg, Lnast_node::create_ref(pin_name));
  attach_child(lnast, idx_asg, din_pin);

  /* Create a dot node that points to reg's qpin. Then change name of reg pin in
   * LGraph to match the LHS of that dot node (so all future references to that
   * register are actually referencing the __q_pin).
   * FIXME: In the future, it may just be better to set reg __fwd = false and not do this. */
  auto tmp_var_q = create_temp_var(lnast);
  auto idx_dot_q = lnast.add_child(parent_node, Lnast_node::create_dot(""));
  lnast.add_child(idx_dot_q, Lnast_node::create_ref(tmp_var_q));
  lnast.add_child(idx_dot_q, Lnast_node::create_ref(pin_name));
  lnast.add_child(idx_dot_q, Lnast_node::create_ref("__q_pin"));

  auto editable_pin = pin;
  dpin_set_map_name(editable_pin, tmp_var_q);
}

void Pass_lnast_fromlg::attach_subgraph_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  const auto& sub = pin.get_node().get_type_sub_node();

  // Create tuple names for submodule IO.
  std::string_view out_tup_name;
  if (!pin.get_node().has_name()) {
    I(false, "\n\nERROR: for debug; not expecting to enter this code-part\n\n");
    // 15-9 out_tup_name = dpin_get_name(pin);//TODO: check the type_op and assign prefix of "out"
    dpin_set_map_name(pin, create_temp_var(lnast));
    out_tup_name = lnast.add_string(absl::StrCat("out", dpin_get_name(pin)));
    // pin.get_node().set_name(create_temp_var(lnast));
    // out_tup_name = lnast.add_string(absl::StrCat("out", pin.get_node().get_name()));
  } else {
    std::vector<std::string_view> out_node_name
        = absl::StrSplit(pin.get_node().get_name(), ":");  // du to commit: embed return variable name to subgraph node name
    out_tup_name = lnast.add_string(out_node_name[0]);
  }
  auto inp_tup_name = create_temp_var(lnast);
  fmt::print("instance_name:{}, subgraph->get_name():{}\n", pin.get_node().get_name(), sub.get_name());

  // auto out_tup_name = lnast.add_string(pin.get_node().get_name());

  // Create + instantiate input tuple.
  auto args_idx = lnast.add_child(parent_node, Lnast_node::create_tuple("args_tuple"));
  lnast.add_child(args_idx, Lnast_node::create_ref(inp_tup_name));
  // attach_child(lnast, args_idx, const Node_pin &dpin)
  for (const auto& inp : pin.get_node().inp_edges()) {
    auto port_name = inp.sink.get_type_sub_pin_name();
    auto idx_asg   = lnast.add_child(args_idx, Lnast_node::create_assign("sb_arg_set"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(port_name)));
    attach_child(lnast, idx_asg, inp.driver);
  }

  // Create actual call to submodule.
  auto func_call_node = lnast.add_child(parent_node, Lnast_node::create_func_call("func_call"));
  lnast.add_child(func_call_node, Lnast_node::create_ref(out_tup_name));
  lnast.add_child(func_call_node, Lnast_node::create_ref(lnast.add_string(sub.get_name())));
  lnast.add_child(func_call_node, Lnast_node::create_ref(inp_tup_name));

  // Create output
  for (const auto& dpin : pin.get_node().out_connected_pins()) {
    auto port_name  = dpin.get_type_sub_pin_name();
    auto idx_dotasg = lnast.add_child(parent_node, Lnast_node::create_dot("sb_out_set"));
    attach_child(lnast, idx_dotasg, dpin);
    lnast.add_child(idx_dotasg, Lnast_node::create_ref(out_tup_name));
    lnast.add_child(idx_dotasg, Lnast_node::create_ref(lnast.add_string(port_name)));

    // Specify output's bw -- NOTE: shouldn't be necessary, but useful for bw pass for now
    // --commented due to dummy dot problem--
#if 0
    if (put_bw_in_ln) {
     auto port_bw   = dpin.get_bits();
     auto temp_var_bw = create_temp_var(lnast);
     auto idx_dot_bw = lnast.add_child(parent_node, Lnast_node::create_dot("sb_out_bwd"));
     lnast.add_child(idx_dot_bw, Lnast_node::create_ref(temp_var_bw));
     lnast.add_child(idx_dot_bw, Lnast_node::create_ref(out_tup_name));
     lnast.add_child(idx_dot_bw, Lnast_node::create_ref(port_name));
     auto idx_asg_bw = lnast.add_child(parent_node, Lnast_node::create_assign("sb_out_bwa"));
     lnast.add_child(idx_asg_bw, Lnast_node::create_ref(temp_var_bw));
     lnast.add_child(idx_asg_bw, Lnast_node::create_const(lnast.add_string(std::to_string(port_bw))));
    }
#endif
  }
}

// FIXME: NOT WORKING, IN PROGRESS
void Pass_lnast_fromlg::attach_memory_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin) {
  std::queue<Node_pin> addr_q, clk_q, din_q, en_q, fwd_q, lat_q, wmask_q, pose_q, wmode_q;
  bool is_one_addr = false, is_one_clk = false, is_one_din = false, is_one_en = false, is_one_fwd = false, is_one_lat = false,
       is_one_wmask = false, is_one_pose = false, is_one_wmode = false;

  (void)is_one_din;
  (void)is_one_wmode;

  Node_pin size_dpin, bits_dpin;
  for (const auto& inp : pin.get_node().inp_edges_ordered()) {
    switch (inp.sink.get_pid()) {
      case 0:  // addr
        addr_q.push(inp.driver);
        break;
      case 1:  // bits
        // Do I need to specify the .__bits per port?
        bits_dpin = inp.driver;
        break;
      case 2:  // clock
        clk_q.push(inp.driver);
        break;
      case 3:  // data in
        din_q.push(inp.driver);
        break;
      case 4:  // enable
        en_q.push(inp.driver);
        break;
      case 5:  // fwd
        fwd_q.push(inp.driver);
        break;
      case 7:  // latency
        lat_q.push(inp.driver);
        break;
      case 8:  // wmask
        wmask_q.push(inp.driver);
        break;
      case 6:  // posedge
        pose_q.push(inp.driver);
        break;
      case 9:  // size
        size_dpin = inp.driver;
        break;
      case 10:  // wmode
        wmode_q.push(inp.driver);
        break;
      default: Pass::error("bad input edge into memory node {} with sink pid {}", pin.get_node().debug_name(), inp.sink.get_pid());
    }
  }
  if (addr_q.size() == 1)
    is_one_addr = true;
  if (clk_q.size() == 1)
    is_one_clk = true;
  if (din_q.size() == 1)
    is_one_din = true;
  if (en_q.size() == 1)
    is_one_en = true;
  if (fwd_q.size() == 1)
    is_one_fwd = true;
  if (lat_q.size() == 1)
    is_one_lat = true;
  if (wmask_q.size() == 1)
    is_one_wmask = true;
  if (pose_q.size() == 1)
    is_one_pose = true;
  if (wmode_q.size() == 1)
    is_one_wmode = true;

  auto port_count = std::max(
      {addr_q.size(), clk_q.size(), din_q.size(), fwd_q.size(), lat_q.size(), wmask_q.size(), pose_q.size(), wmode_q.size()});

  // Create a tuple for each memory port.
  absl::flat_hash_set<std::pair<std::string_view, std::string_view>> port_temp_name_list;
  for (uint64_t i = 0; i < port_count; i++) {
    /* FIXME: This tuple having the name "memory[1/2/3]" is important to the LN->FIR interface,
     * specifically to help with identifying things related to memory. This is hacky... */
    auto idx_tuple     = lnast.add_child(parent_node, Lnast_node::create_tuple("memory1"));
    auto temp_var_name = create_temp_var(lnast);
    port_temp_name_list.insert({"FIXME:GET_DPIN_NAME", temp_var_name});
    lnast.add_child(idx_tuple, Lnast_node::create_ref(temp_var_name));  // FIXME: how to get port name?

    auto idx_asg_addr = lnast.add_child(idx_tuple, Lnast_node::create_assign(""));
    lnast.add_child(idx_asg_addr, Lnast_node::create_ref("__addr"));
    attach_child(lnast, idx_asg_addr, addr_q.front());
    if (!is_one_addr)
      addr_q.pop();

    auto idx_asg_clk = lnast.add_child(idx_tuple, Lnast_node::create_assign(""));
    lnast.add_child(idx_asg_clk, Lnast_node::create_ref("__clk_pin"));
    attach_child(lnast, idx_asg_clk, clk_q.front());
    if (!is_one_clk)
      clk_q.pop();

    // FIXME: How to handle __data? (din)

    auto idx_asg_en = lnast.add_child(idx_tuple, Lnast_node::create_assign(""));
    lnast.add_child(idx_asg_en, Lnast_node::create_ref("__enable"));
    attach_child(lnast, idx_asg_en, en_q.front());
    if (!is_one_en)
      en_q.pop();

    auto idx_asg_fwd = lnast.add_child(idx_tuple, Lnast_node::create_assign(""));
    lnast.add_child(idx_asg_fwd, Lnast_node::create_ref("__fwd"));
    attach_child(lnast, idx_asg_fwd, fwd_q.front());
    if (!is_one_fwd)
      fwd_q.pop();

    auto idx_asg_lat = lnast.add_child(idx_tuple, Lnast_node::create_assign(""));
    lnast.add_child(idx_asg_lat, Lnast_node::create_ref("__latency"));
    attach_child(lnast, idx_asg_lat, lat_q.front());
    if (!is_one_lat)
      lat_q.pop();

    auto idx_asg_wmask = lnast.add_child(idx_tuple, Lnast_node::create_assign(""));
    lnast.add_child(idx_asg_wmask, Lnast_node::create_ref("__wrmask"));
    attach_child(lnast, idx_asg_wmask, wmask_q.front());
    if (!is_one_wmask)
      wmask_q.pop();

    auto idx_asg_pose = lnast.add_child(idx_tuple, Lnast_node::create_assign(""));
    lnast.add_child(idx_asg_pose, Lnast_node::create_ref("__posedge"));
    attach_child(lnast, idx_asg_pose, pose_q.front());
    if (!is_one_pose)
      pose_q.pop();

    // FIXME: How to handle wmode?
  }

  // Create a single tuple with each memory port instantiated in.
  auto idx_port_tuple = lnast.add_child(parent_node, Lnast_node::create_tuple("memory2"));
  auto temp_var_name  = create_temp_var(lnast);
  lnast.add_child(idx_port_tuple, Lnast_node::create_ref(temp_var_name));
  for (const auto& it : port_temp_name_list) {
    auto idx_asg = lnast.add_child(idx_port_tuple, Lnast_node::create_assign(""));
    // Note->hunter: this translation is changed to not have port names, need to change to FIRRTL interface
    lnast.add_child(idx_asg, Lnast_node::create_ref(it.first));
    lnast.add_child(idx_asg, Lnast_node::create_ref(it.second));
  }

  // Specify all the attributes of this memory (.__port, .__size, ...)
  auto idx_mem_tuple = lnast.add_child(parent_node, Lnast_node::create_tuple("memory3"));
  lnast.add_child(idx_mem_tuple, Lnast_node::create_ref("#FIXME:MEM_NAME"));

  auto idx_asg_port = lnast.add_child(idx_mem_tuple, Lnast_node::create_assign(""));
  lnast.add_child(idx_asg_port, Lnast_node::create_ref("__port"));
  lnast.add_child(idx_asg_port, Lnast_node::create_ref(temp_var_name));

  auto idx_asg_size = lnast.add_child(idx_mem_tuple, Lnast_node::create_assign(""));
  lnast.add_child(idx_asg_size, Lnast_node::create_ref("__size"));
  attach_child(lnast, idx_asg_size, size_dpin);

  // FIXME: Should specify __bits?

  // Need to now handle data out ports
  // FIXME: Unclear how to do this. Need to have port name then do: dpin_name = mem.{port_name}
}

//------------- Helper Functions ------------
void Pass_lnast_fromlg::attach_children_to_node(Lnast& lnast, Lnast_nid& op_node, const Node_pin& pin) {
  for (const auto& inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    attach_child(lnast, op_node, dpin);
  }
}

/* Purpose of this is so that whenever we have to have the RHS of
 * an expression mapped to LNAST, it's possible that it might be
 * a module input. If it is not a module input, just add a
 * node that has the pin's name. If it is a module input,
 * add the "$" in front of it. */
void Pass_lnast_fromlg::attach_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin& dpin) {
  // The input "dpin" needs to be a driver pin.
  if (dpin.get_node().is_graph_input()) {
    // If the input to the node is from a GraphIO node (it's a module input), add the $ in front.
    auto dpin_name = lnast.add_string(dpin_get_name(dpin));
    if (has_prefix(dpin_name)) {
      I(false, "IO in lgraph should not have %/$");
      // lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(dpin_name)));
    } else {
      lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("$", dpin_name))));
    }
  } else if (dpin.get_node().is_graph_output()) {
    auto name = dpin_get_name(dpin);
    if (name[0] != '%') {
      name = absl::StrCat("%", name);
    }
    auto out_driver_name = lnast.add_string(name);
    lnast.add_child(op_node,
                    Lnast_node::create_ref(out_driver_name));  // lnast.add_string(absl::StrCat(prefix, "%", dpin.get_name()))));
  } else if ((dpin.get_node().get_type_op() == Ntype_op::Aflop) || (dpin.get_node().get_type_op() == Ntype_op::Sflop)) {
    // dpin_name is already persistent, no need to do add_string but cleaner
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(dpin))));
  } else if (dpin.get_node().get_type_op() == Ntype_op::Const) {
    lnast.add_child(op_node, Lnast_node::create_const(lnast.add_string(dpin.get_node().get_type_const().to_pyrope())));
  } else {
    auto dpin_name = lnast.add_string(dpin_get_name(dpin));
    lnast.add_child(op_node, Lnast_node::create_ref(dpin_name));
  }
}

/* This is the same as above, but instead of making ref/const nodes,
 * we instead make cond nodes. */
void Pass_lnast_fromlg::attach_cond_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin& dpin) {
  // The input "dpin" needs to be a driver pin.
  if (dpin.get_node().is_graph_input()) {
    // If the input to the node is from a GraphIO node (it's a module input), add the $ in front.
    auto dpin_name = lnast.add_string(dpin_get_name(dpin));
    if (has_prefix(dpin_name)) {
      I(false, "IO in lgraph should not have %/$");
      // lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(dpin_name)));
    } else {
      lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(absl::StrCat("$", dpin_name))));
    }
  } else if (dpin.get_node().is_graph_output()) {
    auto dpin_name = lnast.add_string(dpin_get_name(dpin));
    if (has_prefix(dpin_name)) {
      I(false, "IO in lgraph should not have %/$");
      // lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(dpin_name)));
    } else {
      lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(absl::StrCat("%", dpin_name))));
    }
  } else if ((dpin.get_node().get_type_op() == Ntype_op::Aflop) || (dpin.get_node().get_type_op() == Ntype_op::Sflop)) {
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(dpin.get_name())));
  } else if (dpin.get_node().get_type_op() == Ntype_op::Const) {
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(dpin.get_node().get_type_const().to_pyrope())));
  } else {
    auto dpin_name = lnast.add_string(dpin_get_name(dpin));
    lnast.add_child(op_node, Lnast_node::create_cond(dpin_name));
  }
}

/* If a driver pin's name includes a "%" and is not an output of the
 * design, then it's an SSA variable. Thus, if it is an SSA variable
 * I need to remove the "%". */
std::string_view Pass_lnast_fromlg::dpin_get_name(const Node_pin dpin) {
  auto ccd = dpin.get_compact_class_driver();
  auto it  = dpin_name_map.find(ccd);

  if (it != dpin_name_map.end()) {
    // it is present in map
    return it->second;
  } else if (dpin.has_name() && dpin.get_name().substr(0, 1) == "%") {
    return dpin.get_name().substr(1);
  } else if (dpin.has_name()) {
    return dpin.get_name();
  } else {
    I(false, "\n\nERROR: trying to fetch a name that is not present in dpin_name_map ans well as in lgraph as the dpin name\n\n");
    return "ERROR";
  }
}

/* if (dpin doesn't have name) assign a temp var to pin and keep in map
 * else (dpin has name) modulate the name as per need and assing to the map */
void Pass_lnast_fromlg::dpin_set_map_name(const Node_pin dpin, std::string_view name_part) {
  auto ccd  = dpin.get_compact_class_driver();
  auto name = name_part;
  if (dpin_name_map.find(ccd) != dpin_name_map.end()) {
    // if (key present) overwrite the value
    dpin_name_map[ccd] = name_part;
  } else {                                              // insert in map
    I(dpin_name_map.find(ccd) == dpin_name_map.end());  // assert that the pin entry is not already in map.
    dpin_name_map.emplace(ccd, name);                   // emplace does not reset; if it is already set, it just returns the val
  }
  // dpin_name_map.emplace(ccd, name);//emplace does not reset; if it is already set, it just returns the val
}

std::string_view Pass_lnast_fromlg::get_new_seq_name(Lnast& lnast) {
  auto seq_name = lnast.add_string(absl::StrCat("SEQ", seq_count));
  seq_count++;
  return seq_name;
}

std::string_view Pass_lnast_fromlg::create_temp_var(Lnast& lnast, std::string_view str_prefix) {
  auto temp_var_name = lnast.add_string(absl::StrCat(str_prefix, "L", temp_var_count));
  temp_var_count++;
  return temp_var_name;
}

bool Pass_lnast_fromlg::has_prefix(std::string_view test_string) {
  return (test_string.find("$") == 0 || test_string.find("#") == 0 || test_string.find("%") == 0);
}
bool Pass_lnast_fromlg::has_prefix(std::string test_string) {
  return (test_string.find("$") == 0 || test_string.find("#") == 0 || test_string.find("%") == 0);
}
