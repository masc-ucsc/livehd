//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lgraph_to_lnast.hpp"

#include <string>
#include <stack>
#include <queue>

#include "lbench.hpp"
#include "lgedgeiter.hpp"

//Node colors
#define WHITE 0
#define GREY  1
#define BLACK 2

static Pass_plugin sample("pass_lgraph_to_lnast", Pass_lgraph_to_lnast::setup);

void Pass_lgraph_to_lnast::setup() {
  Eprp_method m1("pass.lgraph_to_lnast", "translates LGraph to LNAST", &Pass_lgraph_to_lnast::trans);
  register_pass(m1);
}

Pass_lgraph_to_lnast::Pass_lgraph_to_lnast(const Eprp_var &var) : Pass("pass.lgraph_to_lnast", var) { }

void Pass_lgraph_to_lnast::trans(Eprp_var &var) {
  Lbench b("pass.lgraph_to_lnast");

  Pass_lgraph_to_lnast p(var);

  for (const auto &l : var.lgs) {
    p.do_trans(l, var, l->get_name());
  }
}

void Pass_lgraph_to_lnast::do_trans(LGraph *lg, Eprp_var &var, std::string_view module_name) {
  fmt::print("iterate_over_lg\n");
  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(module_name);
  lnast->set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, lg->get_name())));
  auto idx_stmts = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts(get_new_seq_name(*lnast)));

  handle_io(lg, idx_stmts, *lnast);
  initial_tree_coloring(lg);

  begin_transformation(lg, *lnast, idx_stmts);

  lnast->dump();

  var.add(std::move(lnast));
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

  lg->get_graph_output_node().set_color(BLACK);
  lg->each_graph_output([&](const Node_pin &pin) {//TODO: Make sure I have the capture list correct
    I(pin.get_node().get_color() == BLACK);
    //Note: pin is a driver pin.
    fmt::print("opin: {} pid: {}\n", pin.get_name(), pin.get_pid());
    I(pin.get_node().get_type().op == GraphIO_Op);
    //auto node = pin.get_node();
    auto editable_pin = pin;
    handle_output_node(lg, editable_pin, lnast, ln_node);
  });
}

void Pass_lgraph_to_lnast::handle_output_node(LGraph *lg, Node_pin& pin, Lnast& lnast, Lnast_nid& ln_node) {
  I(pin.has_name()); //Outputs of the graphs should have names, I would think.
  for (const auto &inp : pin.get_node().inp_edges()) {
    auto editable_pin = inp.driver;
    handle_source_node(lg, editable_pin, lnast, ln_node);
    //fmt::print("\tedge: [{} d: {} {} {}] [s: {}]\n", inp.driver.get_node().get_type().op,
    //        inp.driver.get_name(), inp.driver.get_pid(), inp.driver.get_node().get_color(), inp.sink.get_pid());
  }

  attach_output_to_lnast(lnast, ln_node, pin);
}

/* Purpose of this function is to serve as the recursive
 * call we will invoke constantly as we work up the
 * LGraph. At the end, regardless of if any work is
 * needed to be done, return the node's name. */
void Pass_lgraph_to_lnast::handle_source_node(LGraph *lg, Node_pin& pin, Lnast& lnast, Lnast_nid& ln_node) {
  //If pin is a driver pin for an already handled node, just return driver pin's name.
  if(pin.get_node().get_color() == BLACK) {
    if(!pin.has_name()) {
      pin.set_name(create_temp_var(lnast));
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

    //fmt::print("\tedge: [{} d: {} {} {}] [s: {}]\n", inp.driver.get_node().get_type().op,
    //        inp.driver.get_name(), inp.driver.get_pid(), inp.driver.get_node().get_color(), inp.sink.get_pid());
  }

  if(!pin.has_name()) {
    pin.set_name(create_temp_var(lnast));
  }

  pin.get_node().set_color(BLACK);

  attach_to_lnast(lnast, ln_node, pin);
}

/* TODO:
  Invalid_Op,
  Mod_Op,
  // op_class: wire
  Join_Op,
  Pick_Op,
  // op_class: mux
  Mux_Op,  // WARNING: Trivial MUX (not bus muxes) converted to LUT
  // op_class: LUT
  LUT_Op,
  // op_class: wire
  DontCare_Op,
  // op_class: Tuple
  TupAdd_Op,
  TupGet_Op,
  TupRef_Op,
  TupKey_Op,
  //------------------BEGIN PIPELINED (break LOOPS)
  Loop_breaker_begin,
  // op_class: register
  SFlop_Op,  // sync reset flop
  AFlop_Op,  // async reset flop
  Latch_Op,
  FFlop_Op,
  // op_class: memory
  Memory_Op,
  SubGraph_Op,
  // op_class: sub
  SubGraphMin_Op,  // Each subgraph cell starts here
  SubGraphMax_Op = SubGraphMin_Op + ((1ULL << 32) - 1),
  // op_class: value
  U32ConstMin_Op,
  U32ConstMax_Op = U32ConstMin_Op + ((1ULL << 32) - 1),
  // op_class: str
  StrConstMin_Op,
  StrConstMax_Op = StrConstMin_Op + ((1ULL << 32) - 1),
  Loop_breaker_end,
  //------------------END PIPELINED (break LOOPS)
  // op_class: lut
  LUTMin_Op,
  LUTMax_Op = LUTMin_Op + ((1ULL << (1ULL << LUT_input_bits)) - 1)
*/
void Pass_lgraph_to_lnast::attach_to_lnast(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //Look at pin's node's type, then based off that figure out what type of node to add to LNAST.
  fmt::print("LNAST {} :", dpin_get_name(pin));//pin.get_name());
  //note->hunterc: add dpin bits (if set) right here? ---
  lnast.set_bitwidth(dpin_get_name(pin), pin.get_bits());//FIXME?: Do I want all wires in the map + is this best spot to put it in map?
  //------------------------------------------------------
  for(const auto inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    fmt::print(" {}", dpin_get_name(dpin));//dpin.get_name());
  }
  fmt::print("\n");

  switch(pin.get_node().get_type().op) {//Future note to self: when doing src_pins, always check if sources to node are io inputs
    case GraphIO_Op:
    case Const_Op:
      //Skip, nothing to do.
      break;
    case And_Op:
    case Or_Op:
    case Xor_Op:
      attach_binaryop_node(lnast, parent_node, pin);
      break;
    case Not_Op:
      attach_not_node(lnast, parent_node, pin);
      break;
    case Sum_Op:
      attach_sum_node(lnast, parent_node, pin);
      break;
    /*case Mod_Op:
      break;*/
    case LessThan_Op:
    case GreaterThan_Op:
    case LessEqualThan_Op:
    case GreaterEqualThan_Op:
      attach_comparison_node(lnast, parent_node, pin);
      break;
    case Equals_Op:
    case Mult_Op:
    case Div_Op:
    case LogicShiftRight_Op:
    case ArithShiftRight_Op:
    case DynamicShiftRight_Op:
    case DynamicShiftLeft_Op:
    case ShiftRight_Op:
    case ShiftLeft_Op:
      attach_simple_node(lnast, parent_node, pin);
      break;
    case Join_Op:
      attach_join_node(lnast, parent_node, pin);
      break;
    case Pick_Op:
      attach_pick_node(lnast, parent_node, pin);
      break;
    case Mux_Op:
      attach_mux_node(lnast, parent_node, pin);
      break;
    case SFlop_Op:
    case AFlop_Op:
      attach_flop_node(lnast, parent_node, pin);
      break;
    case SubGraph_Op:
      attach_subgraph_node(lnast, parent_node, pin);
      break;
    default: fmt::print("Op not yet supported in attach_to_lnast\n");
  }
}

void Pass_lgraph_to_lnast::handle_io(LGraph *lg, Lnast_nid& parent_lnast_node, Lnast& lnast) {
  /* Any input or output that has its bitwidth specified should add info to the LNAST.
   * As an example, if we had an input x that was 7 bits wide, this would be added:
   *     dot             asg
   *   /  |  \         /     \
   *  T0 $x __bits    T0    0d7    (note that the $ would be % if it was an output)*/

  auto inp_io_node = lg->get_graph_input_node();
  for(const auto edge : inp_io_node.out_edges()) {
    //fmt::print("Out bits:{} name:{}\n", edge.get_bits(), edge.driver.get_name());
    auto bits = edge.get_bits();
    if (bits > 0) {
      // Put input bitwidth info in from_lg_bw_table
      lnast.set_bitwidth(edge.driver.get_name(), bits);
      fmt::print("{} -> {}\n", edge.driver.get_name(), lnast.get_bitwidth(edge.driver.get_name()));

      if (edge.driver.is_signed()) {

      }

      // Create nodes //FIXME: Do I still need this? Table should work
      // note->hunter: The below commented out code creates bw dot nodes for inputs.
      //               It's unnecessary(?) with the existence of from_lgraph_bw_table
      /*auto idx_dot = lnast.add_child(parent_lnast_node, Lnast_node::create_dot("dot"));
      auto temp_name = create_temp_var(lnast);
      lnast.add_child(idx_dot, Lnast_node::create_ref(temp_name));
      lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(absl::StrCat("$", edge.driver.get_name()))));
      lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

      auto idx_asg = lnast.add_child(parent_lnast_node, Lnast_node::create_assign("asg"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(temp_name));
      //FIXME: Is the next line the best way to get driver bitwidth?
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(std::to_string(bits))));*/
    }
  }

  auto out_io_node = lg->get_graph_output_node();
  for(const auto edge : out_io_node.inp_edges()) {
    auto sink_pid    = edge.sink.get_pid();
    auto out_pin     = edge.sink.get_node().get_driver_pin(sink_pid);

    lnast.set_bitwidth(out_pin.get_name(), edge.driver.get_bits());
    fmt::print("{} -> {}\n", out_pin.get_name(), lnast.get_bitwidth(out_pin.get_name()));
  }
}

// -------- How to convert each LGraph node type to LNAST -------------
void Pass_lgraph_to_lnast::attach_output_to_lnast(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &opin) {
  int count = 0;
  for (const auto &inp : opin.get_node().inp_edges()) {
    if(inp.sink.get_pid() == opin.get_pid()) {
      //We have found the sink pin associated with the output pin. Assign the output to the sink pin's edge.
      I(count == 0);
      auto asg_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      lnast.add_child(asg_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("%", opin.get_name()))));
      attach_child(lnast, asg_node, inp.driver);
      count++;
    }
  }

  /*if(count == 0) {
    //There was no inp edge, therefore we set the output to 0.
    auto asg_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(asg_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("%", opin.get_name()))));
    lnast.add_child(asg_node, Lnast_node::create_const("0d0"));
    count++;
  }*/

  I(count <= 1);//There shouldn't be multiple edges leading to a single sink pid on a GraphIO.
}

void Pass_lgraph_to_lnast::attach_sum_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //PID: 0 = AS, 1 = AU, 2 = BS, 3 = BU, 4 = Y... Y = (AS+...+AS+AU+...+AU) - (BS+...+BS+BU+...+BU)
  bool is_add, is_subt = false;
  int add_count = 0;

  Lnast_nid add_node, subt_node;

  //Determine if we're doing an add, sub, or both.
  auto pin_name = lnast.add_string(dpin_get_name(pin));
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
    lnast.add_child(add_node, Lnast_node::create_ref(pin_name));
  } else if(!is_add & is_subt) {
    subt_node = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));
    /*Note: the next line is a strange workaround but it is important. If we didn't do this, the later
        for loop would try to attach something to "add_node", but we never specified what that was. */
    add_node = subt_node;
    lnast.add_child(subt_node, Lnast_node::create_ref(pin_name));
  } else {
    add_node = lnast.add_child(parent_node, Lnast_node::create_plus("plus"));
    subt_node = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));

    auto intermediate_var_name = create_temp_var(lnast);
    lnast.add_child(add_node, Lnast_node::create_ref(intermediate_var_name));
    lnast.add_child(subt_node, Lnast_node::create_ref(pin_name));
    lnast.add_child(subt_node, Lnast_node::create_ref(intermediate_var_name));
  }

  //Attach the name of each of the node's inputs to the Lnast operation node we just made.
  for(const auto inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    auto spin = inp.sink;
    //This if statement is used to figure out if the inp_edge is for plus or minus.
    if((spin.get_pid() == 0) || (spin.get_pid() == 1)) {
      attach_child(lnast, add_node, dpin);
    } else {
      attach_child(lnast, subt_node, dpin);
    }
  }
}

void Pass_lgraph_to_lnast::attach_binaryop_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //PID: 0 = A, 0 = Y, 1 = YReduce
  //FIXME: Not yet sure to handle YReduce portion of this

  Lnast_nid bop_node;
  switch(pin.get_node().get_type().op) {
    case And_Op:
      bop_node = lnast.add_child(parent_node, Lnast_node::create_and("and"));
      break;
    case Or_Op:
      bop_node = lnast.add_child(parent_node, Lnast_node::create_or("or"));
      break;
    case Xor_Op:
      bop_node = lnast.add_child(parent_node, Lnast_node::create_xor("xor"));
      break;
    default:
      fmt::print("Error: attach_binaryop_node doesn't support given node type\n");
      I(false);
  }
  lnast.add_child(bop_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));

  //Attach the name of each of the node's inputs to the Lnast operation node we just made.
  attach_children_to_node(lnast, bop_node, pin);
}

void Pass_lgraph_to_lnast::attach_not_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  auto not_node = lnast.add_child(parent_node, Lnast_node::create_not("not"));
  lnast.add_child(not_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));

  attach_children_to_node(lnast, not_node, pin);
}

void Pass_lgraph_to_lnast::attach_join_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  std::stack<Node_pin> dpins;
  auto bits_to_shift = 0;
  /*This stack method works because the inp_edges iterator goes from edges w/ lowest sink pid to highest
   *  and the highest sink pid correlates to the most significant part of the concatenation. */
  for(const auto inp : pin.get_node().inp_edges()) {
    dpins.push(inp.driver);
    bits_to_shift += inp.driver.get_bits();
    fmt::print("\tjoin -- {} {}\n", inp.sink.get_pid(), bits_to_shift);
  }
  I(dpins.size() >= 2);

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
  for (auto &strv : interm_names) {
    lnast.add_child(idx_or, Lnast_node::create_ref(strv));
  }
  attach_child(lnast, idx_or, dpins.top());

  //FIXME (BIG!): This is a temporary fix. This node type should be not "assign" but some concatenation node type.
  //  One currently does not exist in LNAST.
  /*auto join_node = lnast.add_child(parent_node, Lnast_node::create_assign("join"));
  lnast.add_child(join_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
  //Attach each of the inputs to join with the first being added as the most significant and last as least sig.
  for(int i = dpins.size(); i > 0; i--) {
    attach_child(lnast, join_node, dpins.top());
    dpins.pop();
  }*/
}

void Pass_lgraph_to_lnast::attach_pick_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  // PID: 0 = A, 1 = Offset... Y = A[Offset+(Y_Bitwidth) : Offset]
  /* Y = pick(X, off) in LGraph form turns into Lnast form as:
   *   range_op   bit_select
   *   / | \       / | \
   * T0  lo hi    Y  X  T0
   * where lo = offset, hi = offset + y.bits() - 1 */
  Node_pin offset_pin, var_pin;
  for(const auto inp : pin.get_node().inp_edges()) {
    if(inp.sink.get_pid() == 0) {
      var_pin = inp.driver;
    } else if(inp.sink.get_pid() == 1) {
      offset_pin = inp.driver;
    } else {
      I(false); //No other sink pin id should be used.
    }
  }

  auto pin_str = lnast.add_string(lnast.add_string(dpin_get_name(pin)));
  auto t0_str = create_temp_var(lnast);
  auto lo_str = lnast.add_string(offset_pin.get_node().get_type_const().to_pyrope());
  auto hi_val = offset_pin.get_node().get_type_const() + Lconst(pin.get_bits() - 1);
  auto hi_str = lnast.add_string(hi_val.to_pyrope());

  auto range_node = lnast.add_child(parent_node, Lnast_node::create_range("range_pick"));
  lnast.add_child(range_node, Lnast_node::create_ref(t0_str));
  lnast.add_child(range_node, Lnast_node::create_const(lo_str));
  lnast.add_child(range_node, Lnast_node::create_const(hi_str));

  auto bitsel_node = lnast.add_child(parent_node, Lnast_node::create_bit_select("bitsel_pick"));
  lnast.add_child(bitsel_node, Lnast_node::create_ref(pin_str));
  attach_child(lnast, bitsel_node, var_pin);
  lnast.add_child(bitsel_node, Lnast_node::create_ref(t0_str));
}

void Pass_lgraph_to_lnast::attach_comparison_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //Y = (As|Au) [comparator] (Bs|Bu)... Note: the | means one or the other, can't have both.
  //If there are multiple pins like (lessthan A1, B1 B2) then this is the same as A1 < B1 & A1 < B2.

  /* For each A pin, we have to compare that against each B pin.
   * We know which is which based off the inp_edge's sink pin pid.
   * FIXME: This is n^2. Is there a clean O(n) way to do this?*/
  std::vector<Node_pin> a_pins, b_pins;
  for(const auto inp : pin.get_node().inp_edges()) {
    if((inp.sink.get_pid() == 0) || (inp.sink.get_pid() == 1)) {
      a_pins.push_back(inp.driver);
    } else {
      b_pins.push_back(inp.driver);
    }
  }

  int comparisons = a_pins.size() * b_pins.size();
  if(comparisons == 1) {
    //If only 1 comparison needs to be done, we don't need to do any extra & at the end.
    Lnast_nid comp_node;
    switch(pin.get_node().get_type().op) {
      case LessThan_Op:
        comp_node = lnast.add_child(parent_node, Lnast_node::create_lt("lt"));
        break;
      case LessEqualThan_Op:
        comp_node = lnast.add_child(parent_node, Lnast_node::create_le("lte"));
        break;
      case GreaterThan_Op:
        comp_node = lnast.add_child(parent_node, Lnast_node::create_gt("gt"));
        break;
      case GreaterEqualThan_Op:
        comp_node = lnast.add_child(parent_node, Lnast_node::create_ge("gte"));
        break;
      default:
        fmt::print("Error: invalid node type in attach_comparison_node\n");
        I(false);
    }
    lnast.add_child(comp_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
    attach_child(lnast, comp_node, a_pins[0]);
    attach_child(lnast, comp_node, b_pins[0]);

  } else {
    /*If there is more than 1 comparison that needs to be done, then we have create each
        separate comparison then & them all together. */
    for(const auto apin : a_pins) {
      for(const auto bpin : b_pins) {
        Lnast_nid comp_node;
        switch(pin.get_node().get_type().op) {
          case LessThan_Op:
            comp_node = lnast.add_child(parent_node, Lnast_node::create_lt("lt_i"));
            break;
          case LessEqualThan_Op:
            comp_node = lnast.add_child(parent_node, Lnast_node::create_le("lte_i"));
            break;
          case GreaterThan_Op:
            comp_node = lnast.add_child(parent_node, Lnast_node::create_gt("gt_i"));
            break;
          case GreaterEqualThan_Op:
            comp_node = lnast.add_child(parent_node, Lnast_node::create_ge("gte_i"));
            break;
          default:
            fmt::print("Error: invalid node type in attach_comparison_node\n");
            I(false);
        }
        lnast.add_child(comp_node, Lnast_node::create_ref(create_temp_var(lnast)));
        attach_child(lnast, comp_node, apin);
        attach_child(lnast, comp_node, bpin);
      }
    }

    auto and_node = lnast.add_child(parent_node, Lnast_node::create_and("and"));
    lnast.add_child(and_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));
    for(int i = 1; i <= comparisons; i++) {
      //FIXME: Find a better way to do this (maybe create list of temp vars used?)
      lnast.add_child(and_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("___L", temp_var_count-i))));
    }
  }
}

void Pass_lgraph_to_lnast::attach_simple_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  Lnast_nid simple_node;
  switch(pin.get_node().get_type().op) {
    case Equals_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_same("=="));
      break;
    case Mult_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_mult("mult"));
      break;
    case Div_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_div("div"));
      break;
    case LogicShiftRight_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_logic_shift_right("l_shr"));
      break;
    case ArithShiftRight_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_arith_shift_right("a_shr"));
      break;
    case DynamicShiftRight_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_right("d_shr"));
      break;
    case DynamicShiftLeft_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_dynamic_shift_left("d_shl"));
      break;
    case ShiftRight_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr"));
      break;
    case ShiftLeft_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_shift_left("shl"));
      break;
    default:
      fmt::print("Error: attach_simple_node unknown node type provided\n");
      I(false);
  }
  lnast.add_child(simple_node, Lnast_node::create_ref(lnast.add_string(dpin_get_name(pin))));

  //Attach the name of each of the node's inputs to the Lnast operation node we just made.
  attach_children_to_node(lnast, simple_node, pin);
}

void Pass_lgraph_to_lnast::attach_mux_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //FIXME: Currently, this will only support a mux that has 1 cond + 2 vals. Eventually handle more.
  // PID: 0 = S, 1 = A, 2 = B, ...
  // Y = ~SA | SB

  auto if_node = lnast.add_child(parent_node, Lnast_node::create_if("mux"));
  auto cst_node = lnast.add_child(if_node, Lnast_node::create_cstmts(""));

  std::vector<XEdge> mux_vals;
  for(const auto inp : pin.get_node().inp_edges()) {
    if(inp.sink.get_pid() == 0) { //If mux selector S, create if's "condition"
      attach_cond_child(lnast, if_node, inp.driver);
    } else {
      mux_vals.push_back(inp);
    }
  }
  I(mux_vals.size() >= 2);

  Node_pin dpins[mux_vals.size() + 1];
   //dpins[1] = driver of A, dpins[2] driver of B, etc... dpins[0] leave empty
  for(const auto edge : mux_vals) {
    dpins[edge.sink.get_pid()] = edge.driver;
  }

  auto pin_name = lnast.add_string(dpin_get_name(pin));

  auto if_true_stmt_node = lnast.add_child(if_node, Lnast_node::create_stmts(get_new_seq_name(lnast)));
  auto if_false_stmt_node = lnast.add_child(if_node, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  auto asg_node_false = lnast.add_child(if_false_stmt_node, Lnast_node::create_assign("assign_false"));
  lnast.add_child(asg_node_false, Lnast_node::create_ref(pin_name));
  attach_child(lnast, asg_node_false, dpins[1]);

  auto asg_node_true = lnast.add_child(if_true_stmt_node, Lnast_node::create_assign("assign_true"));
  lnast.add_child(asg_node_true, Lnast_node::create_ref(pin_name));
  attach_child(lnast, asg_node_true, dpins[2]);
}

void Pass_lgraph_to_lnast::attach_flop_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //PID: 0 = CLK, 1 = Din, 2 = En, 3 = Reset, 4 = Set Val, 5 = Clk Polarity (5 is not used for AFlop)
  bool has_clk   = false;
  bool has_din   = false;
  bool has_en    = false;
  bool has_reset = false;
  bool has_set_v = false;
  bool has_pola  = false;
  Node_pin clk_pin, din_pin, en_pin, reset_pin, set_v_pin, pola_pin;
  for(const auto inp : pin.get_node().inp_edges()) {
    if (inp.sink.get_pid() == 0) {
      I(!has_clk);
      has_clk = true;
      clk_pin = inp.driver;

    } else if (inp.sink.get_pid() == 1) {
      I(!has_din);
      has_din = true;
      din_pin = inp.driver;

    } else if (inp.sink.get_pid() == 2) {
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
      I(false); //There shouldn't be any other inputs to a flop.
    }
  }
  I(has_din && has_clk); //A flop at least has to have the input and clock, others are optional/have defaults.

  std::string_view pin_name;
  if(pin.get_name().substr(0,1) == "#") {
    pin_name = lnast.add_string(pin.get_name());
  } else {
    pin_name = lnast.add_string(absl::StrCat("#", dpin_get_name(pin)));
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
    //FIXME: Add reset logic.
  }

  if (has_set_v) {
    //FIXME: Not sure what this is yet. Initial value?
  }

  if (has_pola) {
    I(pin.get_node().get_type().op != AFlop_Op);
    auto temp_var_name = create_temp_var(lnast);
    auto dot_pol = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_pol"));
    lnast.add_child(dot_pol, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(dot_pol, Lnast_node::create_ref(pin_name));
    lnast.add_child(dot_pol, Lnast_node::create_ref("__posedge"));

    auto asg_pol = lnast.add_child(parent_node, Lnast_node::create_assign("asg_pol"));
    lnast.add_child(asg_pol, Lnast_node::create_ref(temp_var_name));
    if (pola_pin.get_node().get_type_op() == Const_Op) {
      if (pola_pin.get_node().get_type_const().to_firrtl() == "1") {
        lnast.add_child(asg_pol, Lnast_node::create_ref("true"));
      } else {
        lnast.add_child(asg_pol, Lnast_node::create_ref("false"));
      }
    } else {
      lnast.add_child(asg_pol, Lnast_node::create_ref("false"));
    }
  }

  // Perform actual assignment
  auto asg_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop"));
  lnast.add_child(asg_node, Lnast_node::create_ref(pin_name));
  attach_child(lnast, asg_node, din_pin);
}

void Pass_lgraph_to_lnast::attach_subgraph_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {

  //Create a tuple that contains all arguments.
  auto args_tup_node = lnast.add_child(parent_node, Lnast_node::create_tuple("args_tuple"));
  auto inp_tup_name = lnast.add_string(absl::StrCat("inp_submodule", temp_var_count));
  lnast.add_child(args_tup_node, Lnast_node::create_ref(inp_tup_name));

  //Create name for tuple that will hold outputs
  auto out_tup_name = lnast.add_string(absl::StrCat("out_submodule", temp_var_count));
  temp_var_count++;

  //Set up each key-value of the arg tuple (key = name in submodule | null, value = name in calling module)
  for(const auto inp : pin.get_node().inp_edges()) {
    auto key_value_asg_node = lnast.add_child(args_tup_node, Lnast_node::create_assign("assign"));
    lnast.add_child(key_value_asg_node, Lnast_node::create_ref("null"));
    attach_child(lnast, key_value_asg_node, inp.driver);
    fmt::print("inp: {} {}\n", inp.driver.get_name(), inp.sink.get_pid());
  }

  auto func_call_node = lnast.add_child(parent_node, Lnast_node::create_func_call("func_call"));
  //LHS
  lnast.add_child(func_call_node, Lnast_node::create_ref(out_tup_name));
  //func_name
  lnast.add_child(func_call_node, Lnast_node::create_ref(lnast.add_string(pin.get_node().debug_name())));//FIXME: Is this the right name?
  //arguments (just use tuple created above)
  lnast.add_child(func_call_node, Lnast_node::create_ref(inp_tup_name));

  //FIXME: Need a way to do the dot stuff + outputs. Below is incomplete but serves as proof of idea.
  /*for(const auto out : pin.get_node().out_edges()) {
    auto dot_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    attach_child(lnast, dot_node, out.driver);
    //NOTE: Below, we do not use temp_var_count, we use tuple_temp_holder (so we can reference tuple name).
    lnast.add_child(dot_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("T", func_temp_holder))));
    //FIXME: Now how do I get the output pin's name from the submodule?
    fmt::print("out: {} {}\n", out.driver.get_name(), out.driver.get_pid());
  }*/
  lnast.dump();
  I(false);
}

//------------- Helper Functions ------------
void Pass_lgraph_to_lnast::attach_children_to_node(Lnast& lnast, Lnast_nid& op_node, const Node_pin &pin) {
  for(const auto inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    attach_child(lnast, op_node, dpin);
  }
}

/* Purpose of this is so that whenever we have to have the RHS of
 * an expression mapped to LNAST, it's possible that it might be
 * a module input. If it is not a module input, just add a
 * node that has the pin's name. If it is a module input,
 * add the "$" in front of it. */
void Pass_lgraph_to_lnast::attach_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin &dpin) {
  //The input "dpin" needs to be a driver pin.

  //FIXME: This will only work for var/wire names. This will mess up for constants, I think.
  if(dpin.get_node().is_graph_input()) {
    //If the input to the node is from a GraphIO node (it's a module input), add the $ in front.
    auto dpin_name = dpin_get_name(dpin);
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("$", dpin_name))));
  } else if(dpin.get_node().is_graph_output()) {
    auto out_driver_name = lnast.add_string(get_driver_of_output(dpin));
    lnast.add_child(op_node, Lnast_node::create_ref(out_driver_name));//lnast.add_string(absl::StrCat(prefix, "%", dpin.get_name()))));
  } else if((dpin.get_node().get_type().op == AFlop_Op) || (dpin.get_node().get_type().op == SFlop_Op)) {
    auto dpin_name = dpin_get_name(dpin);
    if(dpin_name.substr(0,1) == "#") {
      // dpin_name is already persistent, no need to do add_string but cleaner
      lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(dpin_name)));
    } else {
      lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("#", dpin_name))));
    }
  } else if(dpin.get_node().get_type().op == Const_Op) {
    lnast.add_child(op_node, Lnast_node::create_const(lnast.add_string(dpin.get_node().get_type_const().to_pyrope())));
  } else {
    auto dpin_name = dpin_get_name(dpin);
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(dpin_name)));
  }
}

/* This is the same as above, but instead of making ref/const nodes,
 * we instead make cond nodes. */
void Pass_lgraph_to_lnast::attach_cond_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin &dpin) {
  //The input "dpin" needs to be a driver pin.

  //FIXME: This will only work for var/wire names. This will mess up for constants, I think.
  if(dpin.get_node().is_graph_input()) {
    //If the input to the node is from a GraphIO node (it's a module input), add the $ in front.
    auto dpin_name = dpin_get_name(dpin);
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(absl::StrCat("$", dpin_name))));
  } else if(dpin.get_node().is_graph_output()) {
    auto dpin_name = dpin_get_name(dpin);
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(absl::StrCat("%", dpin_name))));
  } else if((dpin.get_node().get_type().op == AFlop_Op) || (dpin.get_node().get_type().op == SFlop_Op)) {
    auto dpin_name = dpin_get_name(dpin);
    if(dpin_name.substr(0,1) == "#") {
      lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(dpin_name)));
    } else {
      lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("#", dpin_name))));
    }
  } else if(dpin.get_node().get_type().op == Const_Op) {
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
  for(const auto inp : dpin.get_node().inp_edges()) {
    if(inp.sink.get_pid() == dpin.get_pid()) {
      return inp.driver.get_name();
    }
  }

  I(false);//There should always be some driver.
  return ""; //Here just so no warnings in compiler.
  //FIXME: Perhaps change this instead to just "0d0" (though the place this calls would have to be const not ref).
}

/* If a driver pin's name includes a "%" and is not an output of the
 * design, then it's an SSA variable. Thus, if it is an SSA variable
 * I need to remove the "%". */
std::string_view Pass_lgraph_to_lnast::dpin_get_name(const Node_pin dpin) {
  if(dpin.get_name().substr(0,1) == "%") {
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
