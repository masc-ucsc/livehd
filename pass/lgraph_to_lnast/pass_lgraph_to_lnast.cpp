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

  lnast.dump();

  //lnasts.emplace_back(lnast);//FIXME: Need to reinsert this at some point.

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
      pin.set_name(absl::StrCat("T", std::to_string(temp_var_count)));//FIXME: Will this ever collide with any var names?
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

    //fmt::print("\tedge: [{} d: {} {} {}] [s: {}]\n", inp.driver.get_node().get_type().op,
    //        inp.driver.get_name(), inp.driver.get_pid(), inp.driver.get_node().get_color(), inp.sink.get_pid());
  }

  if(!pin.has_name()) {
    pin.set_name(absl::StrCat("T", std::to_string(temp_var_count)));
    temp_var_count++;
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
  U32Const_Op,
  StrConst_Op,
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
  fmt::print("LNAST {} :", pin.get_name());
  for(const auto inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    fmt::print(" {}", dpin.get_name());
  }
  fmt::print("\n");
  switch(pin.get_node().get_type().op) {//Future note to self: when doing src_pins, always check if sources to node are io inputs
    case GraphIO_Op:
    case U32Const_Op:
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
    /*case Pick_Op:
      break;*/
    case Mux_Op:
      attach_mux_node(lnast, parent_node, pin);
      break;
    case StrConst_Op:
      //FIXME: I'm encounter strconst of "x". What does this mean?
      fmt::print("StrConst: {}\n", pin.get_node().get_type_const_sview());
      break;
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

  if(count == 0) {
    //There was no inp edge, therefore we set the output to 0.
    auto asg_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(asg_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("%", opin.get_name()))));
    lnast.add_child(asg_node, Lnast_node::create_const("0"));
    count++;
  }

  I(count == 1);//There shouldn't be multiple edges leading to a single sink pid on a GraphIO.
}

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
  lnast.add_child(bop_node, Lnast_node::create_ref(pin.get_name()));

  //Attach the name of each of the node's inputs to the Lnast operation node we just made.
  attach_children_to_node(lnast, bop_node, pin);
}

void Pass_lgraph_to_lnast::attach_not_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  auto asg_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
  lnast.add_child(asg_node, Lnast_node::create_ref(pin.get_name()));

  int inp_count = 0;
  for(const auto inp : pin.get_node().inp_edges()) {
    auto dpin = inp.driver;
    attach_child(lnast, asg_node, dpin);
    inp_count++;
  }
  I(inp_count == 1);
}

void Pass_lgraph_to_lnast::attach_join_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  int inp_count = 0;
  std::stack<Node_pin> dpins;
  /*This stack method works because the inp_edges iterator goes from edges w/ lowest sink pid to highest
   *  and the highest sink pid correlates to the most significant part of the concatenation. */
  for(const auto inp : pin.get_node().inp_edges()) {
    inp_count++;
    dpins.push(inp.driver);
    fmt::print("\tjoin -- {}\n", inp.sink.get_pid());
  }
  if(inp_count == 0) {
    pin.get_node().del_node();
  } else {
    //FIXME (BIG!): This is a temporary fix. This node type should be not "assign" but some concatenation node type.
    //  One currently does not exist in LNAST.
    auto join_node = lnast.add_child(parent_node, Lnast_node::create_assign("join"));
    lnast.add_child(join_node, Lnast_node::create_ref(pin.get_name()));
    //Attach each of the inputs to join with the first being added as the most significant and last as least sig.
    for(int i = dpins.size(); i > 0; i--) {
      attach_child(lnast, join_node, dpins.top());
      dpins.pop();
    }
  }
}

//void Pass_lgraph_to_lnast::attach_pick_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  // PID: 0 = A, 1 = Offset... Y = A[Offset+Y_Bitwidth-1 : Offset]
  /* Y = pick(X, 4) in LGraph form turns into Lnast form as:
   *   dot         minus     range_op  bit_select  assign
   *  / | \        / | \      / | \      / | \     /   \
   * T0 X __bits  T1 T2 0d1  T3 T1 0d4  T4 X T3   Y     T4 */
  //FIXME: the above could possibly be wrong if Y's bitwidth was smaller than X's.

//}

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
    lnast.add_child(comp_node, Lnast_node::create_ref(pin.get_name()));
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
        lnast.add_child(comp_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("T", temp_var_count))));
        temp_var_count++;
        attach_child(lnast, comp_node, apin);
        attach_child(lnast, comp_node, bpin);
      }
    }

    auto and_node = lnast.add_child(parent_node, Lnast_node::create_and("and"));
    lnast.add_child(and_node, Lnast_node::create_ref(pin.get_name()));
    for(int i = 1; i <= comparisons; i++) {
      lnast.add_child(and_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("T", temp_var_count-i))));
    }
  }
}

void Pass_lgraph_to_lnast::attach_simple_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  Lnast_nid simple_node;
  switch(pin.get_node().get_type().op) {
    case Equals_Op:
      simple_node = lnast.add_child(parent_node, Lnast_node::create_eq("=="));
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
  lnast.add_child(simple_node, Lnast_node::create_ref(pin.get_name()));

  //Attach the name of each of the node's inputs to the Lnast operation node we just made.
  attach_children_to_node(lnast, simple_node, pin);
}

void Pass_lgraph_to_lnast::attach_mux_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //FIXME: Currently, this will only support a mux that has 1 condition, like below. Eventually handle more.
  // Y = SA | ~SB

  auto if_node = lnast.add_child(parent_node, Lnast_node::create_if("mux"));
  //FIXME: If I need to add cstmt stuff, it would go right where this comment is.
  std::queue<Node_pin> dpins;
  for(const auto inp : pin.get_node().inp_edges()) {
    if(inp.sink.get_pid() == 0) { //If mux selector S, create if's "condition"
      attach_child(lnast, if_node, inp.driver);
    } else {
      dpins.push(inp.driver);
    }
  }
  I(dpins.size() >= 2);

  auto if_true_stmt_node = lnast.add_child(if_node, Lnast_node::create_stmts("mux_stmt_true"));
  auto asg_node_true = lnast.add_child(if_true_stmt_node, Lnast_node::create_assign("assign_true"));
  lnast.add_child(asg_node_true, Lnast_node::create_ref(pin.get_name()));
  attach_child(lnast, asg_node_true, dpins.front());
  dpins.pop();

  auto if_false_stmt_node = lnast.add_child(if_node, Lnast_node::create_stmts("mux_stmt_false"));
  auto asg_node_false = lnast.add_child(if_false_stmt_node, Lnast_node::create_assign("assign_false"));
  lnast.add_child(asg_node_false, Lnast_node::create_ref(pin.get_name()));
  attach_child(lnast, asg_node_false, dpins.front());
  dpins.pop();
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
  if(dpin.get_node().is_graph_io()) {
    //If the input to the node is from a GraphIO node (it's a module input), add the $ in front.
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("$", dpin.get_name()))));
  } else if(dpin.get_node().get_type().op == U32Const_Op) {
    lnast.add_child(op_node, Lnast_node::create_const(lnast.add_string(std::to_string(dpin.get_node().get_type_const_value()))));
  } else {
    lnast.add_child(op_node, Lnast_node::create_ref(dpin.get_name()));
  }
}
