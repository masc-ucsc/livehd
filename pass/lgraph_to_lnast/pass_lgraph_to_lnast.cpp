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
  Lbench b("pass.lgraph_to_lnast");

  Pass_lgraph_to_lnast p(var);

  for (const auto &l : var.lgs) {
    p.do_trans(l, var, l->get_name());
  }
}

void Pass_lgraph_to_lnast::do_trans(LGraph *lg, Eprp_var &var, std::string_view module_name) {
  fmt::print("iterate_over_lg\n");
  //Lnast lnast("module_name_fixme");//FIXME: Get actual name.
  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(module_name);
  lnast->set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, "top")));
  auto idx_stmts = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts("stmts"));

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
    case StrConst_Op:
      //FIXME: Not sure what to do with this yet. Translate to const value? What about x's?
      fmt::print("StrConst: {}\n", pin.get_node().get_type_const_sview());
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
    if(edge.get_bits() > 0) {
      auto idx_dot = lnast.add_child(parent_lnast_node, Lnast_node::create_dot("dot"));
      auto temp_name = lnast.add_string(absl::StrCat("T", std::to_string(temp_var_count)));
      temp_var_count++;
      lnast.add_child(idx_dot, Lnast_node::create_ref(temp_name));
      lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(absl::StrCat("$", edge.driver.get_name()))));
      lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

      auto idx_asg = lnast.add_child(parent_lnast_node, Lnast_node::create_assign("asg"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(temp_name));
      //FIXME: Is the next line the best way to get driver bitwidth?
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d" + std::to_string(edge.get_bits())))));
    }
  }

  lg->each_graph_output([&](const Node_pin &pin) {//TODO: Make sure I have the capture list correct
    //Note: pin is a driver pin.
    //fmt::print("opin: {} pid: {}\n", pin.get_name(), pin.get_pid());
    I(pin.get_node().get_type().op == GraphIO_Op);
    //auto node = pin.get_node();
    for(const auto edge : pin.get_node().inp_edges()) {
      if(pin.get_pid() == edge.sink.get_pid()) {
        auto idx_dot = lnast.add_child(parent_lnast_node, Lnast_node::create_dot("dot"));
        auto temp_name = lnast.add_string(absl::StrCat("T", std::to_string(temp_var_count)));
        temp_var_count++;
        lnast.add_child(idx_dot, Lnast_node::create_ref(temp_name));
        lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(absl::StrCat("%", pin.get_name()))));
        lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

        auto idx_asg = lnast.add_child(parent_lnast_node, Lnast_node::create_assign("asg"));
        lnast.add_child(idx_asg, Lnast_node::create_ref(temp_name));
        //FIXME: Is the next line the best way to get driver bitwidth?
        lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(absl::StrCat("0d" + std::to_string(edge.get_bits())))));
      }
    }
  });

  /*auto out_io_node = lg->get_graph_output_node();
  for(const auto in_edge : out_io_node.inp_edges()) {
    //FIXME...
    for(const auto out_edge : out_io_node.out_edges()) {

    }
    fmt::print("Output edge: {} {}\n", edge.driver.get_name(), edge.get_bits());
  }*/

  /*lg->each_graph_input([this, &lnast, lg, parent_lnast_node](const Node_pin &pin) {
    //I(pin.has_bitwidth());//FIXME: Not necessarily true for subgraphs?
    if(pin.has_bitwidth()) {
      fmt::print("inp: {} {}\n", pin.get_name(), pin.get_bitwidth().e.max);
      //TODO: Add a "dot" node to LNAST here, I just don't know how to get node's name yet.
    } else {
      fmt::print("input {} has no bitwidth\n", pin.get_name());
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
    }
  });*/
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
  auto pin_name = lnast.add_string(pin.get_name());
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

    auto intermediate_var_name = lnast.add_string(absl::StrCat("T", temp_var_count));
    temp_var_count++;
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
  lnast.add_child(bop_node, Lnast_node::create_ref(lnast.add_string(pin.get_name())));

  //Attach the name of each of the node's inputs to the Lnast operation node we just made.
  attach_children_to_node(lnast, bop_node, pin);
}

void Pass_lgraph_to_lnast::attach_not_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  auto not_node = lnast.add_child(parent_node, Lnast_node::create_not("not"));
  lnast.add_child(not_node, Lnast_node::create_ref(lnast.add_string(pin.get_name())));

  attach_children_to_node(lnast, not_node, pin);
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
    lnast.add_child(join_node, Lnast_node::create_ref(lnast.add_string(pin.get_name())));
    //Attach each of the inputs to join with the first being added as the most significant and last as least sig.
    for(int i = dpins.size(); i > 0; i--) {
      attach_child(lnast, join_node, dpins.top());
      dpins.pop();
    }
  }
}

void Pass_lgraph_to_lnast::attach_pick_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  // PID: 0 = A, 1 = Offset... Y = A[Offset+(Y_Bitwidth-1) : Offset]
  /* Y = pick(X, 3) in LGraph form turns into Lnast form as:
   *   dot           plus      minus     range_op   bit_select   assign
   *  / | \         / | \      / | \      / | \       / | \      /   \
   * T0 Y __bits   T1 T0 0d3  T2 T1 0d1  T3 T2 0d3   T4 X T3    Y     T4 */
  //FIXME: the above could possibly be wrong if Y's bitwidth was smaller than X's.
  bool have_offset = false;
  bool have_var = false;
  Node_pin offset_pin, var_pin;
  for(const auto inp : pin.get_node().inp_edges()) {
    if(inp.sink.get_pid() == 0) { //A
      I(!have_var);
      have_var = true;
      var_pin = inp.driver;
    } else if(inp.sink.get_pid() == 1) { //Offset
      I(!have_offset);
      have_offset = true;
      offset_pin = inp.driver;
    } else {
      I(false); //Pick pins should only have 1 pick and 1 offset.
    }
  }
  I(have_offset & have_var);

  auto pin_str = lnast.add_string(lnast.add_string(pin.get_name()));
  auto t0_str = lnast.add_string(absl::StrCat("T", temp_var_count));
  auto t1_str = lnast.add_string(absl::StrCat("T", temp_var_count+1));
  auto t2_str = lnast.add_string(absl::StrCat("T", temp_var_count+2));
  auto t3_str = lnast.add_string(absl::StrCat("T", temp_var_count+3));
  auto t4_str = lnast.add_string(absl::StrCat("T", temp_var_count+4));
  temp_var_count += 5;

  auto dot_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_pick"));
  lnast.add_child(dot_node, Lnast_node::create_ref(t0_str));
  lnast.add_child(dot_node, Lnast_node::create_ref(pin_str));
  lnast.add_child(dot_node, Lnast_node::create_ref("__bits"));

  auto plus_node = lnast.add_child(parent_node, Lnast_node::create_plus("plus_pick"));
  lnast.add_child(plus_node, Lnast_node::create_ref(t1_str));
  lnast.add_child(plus_node, Lnast_node::create_ref(t0_str));
  attach_child(lnast, plus_node, offset_pin);

  auto minus_node = lnast.add_child(parent_node, Lnast_node::create_minus("minus_pick"));
  lnast.add_child(minus_node, Lnast_node::create_ref(t2_str));
  lnast.add_child(minus_node, Lnast_node::create_ref(t1_str));
  lnast.add_child(minus_node, Lnast_node::create_const("0d1"));

  auto range_node = lnast.add_child(parent_node, Lnast_node::create_range("range_pick"));
  lnast.add_child(range_node, Lnast_node::create_ref(t3_str));
  lnast.add_child(range_node, Lnast_node::create_ref(t2_str));
  attach_child(lnast, range_node, offset_pin);

  auto bitsel_node = lnast.add_child(parent_node, Lnast_node::create_bit_select("bitsel_pick"));
  lnast.add_child(bitsel_node, Lnast_node::create_ref(t4_str));
  attach_child(lnast, bitsel_node, var_pin);
  lnast.add_child(bitsel_node, Lnast_node::create_ref(t3_str));

  auto asg_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_pick"));
  lnast.add_child(asg_node, Lnast_node::create_ref(pin_str));
  lnast.add_child(asg_node, Lnast_node::create_ref(t4_str));
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
    lnast.add_child(comp_node, Lnast_node::create_ref(lnast.add_string(pin.get_name())));
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
    lnast.add_child(and_node, Lnast_node::create_ref(lnast.add_string(pin.get_name())));
    for(int i = 1; i <= comparisons; i++) {
      lnast.add_child(and_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("T", temp_var_count-i))));
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
  lnast.add_child(simple_node, Lnast_node::create_ref(lnast.add_string(pin.get_name())));

  //Attach the name of each of the node's inputs to the Lnast operation node we just made.
  attach_children_to_node(lnast, simple_node, pin);
}

void Pass_lgraph_to_lnast::attach_mux_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {
  //FIXME: Currently, this will only support a mux that has 1 cond + 2 vals. Eventually handle more.
  // PID: 0 = S, 1 = A, 2 = B, ...
  // Y = ~SA | SB

  auto if_node = lnast.add_child(parent_node, Lnast_node::create_if("mux"));

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

  auto pin_name = lnast.add_string(pin.get_name());

  auto if_true_stmt_node = lnast.add_child(if_node, Lnast_node::create_stmts("mux_stmt_true"));
  auto if_false_stmt_node = lnast.add_child(if_node, Lnast_node::create_stmts("mux_stmt_false"));

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
  I(has_din); //A flop at least has to have the input, others are optional/have defaults.

  std::string_view pin_name;
  pin_name = lnast.add_string(absl::StrCat("#", pin.get_name()));

  auto asg_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop"));
  lnast.add_child(asg_node, Lnast_node::create_ref(pin_name));
  attach_child(lnast, asg_node, din_pin);

  if (has_clk) {
    auto temp_var_name = lnast.add_string(absl::StrCat("T", temp_var_count));
    temp_var_count++;

    auto dot_clk_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot_flop_clk"));
    lnast.add_child(dot_clk_node, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(dot_clk_node, Lnast_node::create_ref(pin_name));
    lnast.add_child(dot_clk_node, Lnast_node::create_ref("__clk_pin"));

    auto asg_clk_node = lnast.add_child(parent_node, Lnast_node::create_assign("asg_flop_clk"));
    lnast.add_child(asg_clk_node, Lnast_node::create_ref(temp_var_name));
    attach_child(lnast, asg_clk_node, clk_pin, "@"); //FIXME: Might change reference symbol to '\'
  }

  if (has_reset) {
    //FIXME: Add reset logic.
  }

  if (has_set_v) {
    //FIXME: Not sure what this is yet. Initial value?
  }

  if (has_pola) {
    //FIXME: Add in how to handle polarity. Would need dot node using __posedge (true/false)
    I(pin.get_node().get_type().op == AFlop_Op);
  }
}

void Pass_lgraph_to_lnast::attach_subgraph_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin &pin) {

  //Create a tuple that contains all arguments.
  auto args_tup_node = lnast.add_child(parent_node, Lnast_node::create_tuple("args_tuple"));
  //Tuple name
  auto tuple_temp_holder = temp_var_count;
  lnast.add_child(args_tup_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("T", temp_var_count))));
  temp_var_count++;
  //Set up each key-value of the arg tuple (key = name in submodule | null, value = name in calling module)
  for(const auto inp : pin.get_node().inp_edges()) {
    auto key_value_asg_node = lnast.add_child(args_tup_node, Lnast_node::create_assign("assign"));
    lnast.add_child(key_value_asg_node, Lnast_node::create_ref("null"));
    attach_child(lnast, key_value_asg_node, inp.driver);
  }

  auto func_call_node = lnast.add_child(parent_node, Lnast_node::create_func_call("func_call"));
  //LHS
  auto func_temp_holder = temp_var_count;
  lnast.add_child(func_call_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("T", temp_var_count))));
  temp_var_count++;
  //func_name
  lnast.add_child(func_call_node, Lnast_node::create_ref(lnast.add_string(pin.get_node().debug_name())));//"FIXME_FNAME"));
  //arguments (just use tuple created above)
  //NOTE: Below, we do not use temp_var_count, we use tuple_temp_holder (so we can reference tuple name).
  lnast.add_child(func_call_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("T", tuple_temp_holder))));

  //FIXME: Need a way to do the dot stuff. Below is incomplete but serves as proof of idea.
  for(const auto out : pin.get_node().out_edges()) {
    auto dot_node = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    attach_child(lnast, dot_node, out.driver);
    //NOTE: Below, we do not use temp_var_count, we use tuple_temp_holder (so we can reference tuple name).
    lnast.add_child(dot_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("T", func_temp_holder))));
    //FIXME: Now how do I get the output pin's name from the submodule?
  }
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
  attach_child(lnast, op_node, dpin, "");
}

void Pass_lgraph_to_lnast::attach_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin &dpin, std::string prefix) {
  //The input "dpin" needs to be a driver pin.

  //FIXME: This will only work for var/wire names. This will mess up for constants, I think.
  if(dpin.get_node().is_graph_input()) {
    //If the input to the node is from a GraphIO node (it's a module input), add the $ in front.
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat(prefix, "$", dpin.get_name()))));
  } else if(dpin.get_node().is_graph_output()) {
    auto out_driver_name = lnast.add_string(get_driver_of_output(dpin));
    lnast.add_child(op_node, Lnast_node::create_ref(out_driver_name));//lnast.add_string(absl::StrCat(prefix, "%", dpin.get_name()))));
  } else if((dpin.get_node().get_type().op == AFlop_Op) || (dpin.get_node().get_type().op == SFlop_Op)) {
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat(prefix, "#", dpin.get_name()))));
  } else if(dpin.get_node().get_type().op == U32Const_Op) {
    lnast.add_child(op_node, Lnast_node::create_const(
                              lnast.add_string(absl::StrCat("0d", dpin.get_node().get_type_const_value()))));
  } else {
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat(prefix, dpin.get_name()))));
  }
}

/* This is the same as above, but instead of making ref/const nodes,
 * we instead make cond nodes. */
void Pass_lgraph_to_lnast::attach_cond_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin &dpin) {
  //The input "dpin" needs to be a driver pin.

  //FIXME: This will only work for var/wire names. This will mess up for constants, I think.
  if(dpin.get_node().is_graph_input()) {
    //If the input to the node is from a GraphIO node (it's a module input), add the $ in front.
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(absl::StrCat("$", dpin.get_name()))));
  } else if(dpin.get_node().is_graph_output()) {
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(absl::StrCat("%", dpin.get_name()))));
  } else if((dpin.get_node().get_type().op == AFlop_Op) || (dpin.get_node().get_type().op == SFlop_Op)) {
    lnast.add_child(op_node, Lnast_node::create_ref(lnast.add_string(absl::StrCat("#", dpin.get_name()))));
  } else if(dpin.get_node().get_type().op == U32Const_Op) {
    lnast.add_child(op_node, Lnast_node::create_cond(
                              lnast.add_string(absl::StrCat("0d", dpin.get_node().get_type_const_value()))));
  } else {
    lnast.add_child(op_node, Lnast_node::create_cond(lnast.add_string(dpin.get_name())));
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
  //FIXME: Perhaps change this instead to just "0d0" (though the place this calls would have to be const not ref).
}
