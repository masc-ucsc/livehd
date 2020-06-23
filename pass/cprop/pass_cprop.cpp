//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_cprop.hpp"

#include <string>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "lgtuple.hpp"

#define TRACE(x)
//#define TRACE(x) x

void setup_pass_cprop() { Pass_cprop::setup(); }

void Pass_cprop::setup() {
  Eprp_method m1("pass.cprop", "in-place copy propagation", &Pass_cprop::optimize);

  register_pass(m1);
}

Pass_cprop::Pass_cprop(const Eprp_var &var) : Pass("pass.cprop", var) {}

void Pass_cprop::optimize(Eprp_var &var) {
  Pass_cprop pass(var);

  for (auto &l : var.lgs) {
    pass.trans(l);
  }
}

void Pass_cprop::collapse_forward_same_op(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type().op;
  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type().op!=op)
      continue;
    if (out.driver.get_pid() != out.sink.get_pid())
      continue;

    for (auto &inp : inp_edges_ordered) {
      TRACE(fmt::print("cprop same_op pin:{} to pin:{}\n",inp.driver.debug_name(), out.sink.debug_name()));
      out.sink.connect_driver(inp.driver);
    }
    TRACE(fmt::print("cprop same_op del_edge pin:{} to pin:{}\n",out.driver.debug_name(), out.sink.debug_name()));
    out.del_edge();
  }
}

#if 0
void Pass_cprop::collapse_forward_shiftleft(Node &node) {
  I(node.get_type().op==ShiftLeft_Op);

  // a <<n | b -> join(a,b) if b.bits>=n
  for (auto &out : node.out_edges()) {
    auto sink_node = out.sink.get_node();
    if (sink_node.get_type().op != Or_Op) continue;

    if (!sink_node.get_driver_pin(0).has_outputs()) continue;

    HERE
  }
}
#endif

// Collase forward single node but only for pid!=0 (not reduction ops)
void Pass_cprop::collapse_forward_always_pin0(Node &node, XEdge_iterator &inp_edges_ordered) {
  bool can_delete = true;

  for (auto &out : node.out_edges()) {
    if (out.driver.get_pid()) {
      can_delete = false;
      continue;
    }

    for (auto &inp : inp_edges_ordered) {
      if (inp.sink.get_pid()) {
        can_delete = false;
        continue;
      }
      TRACE(fmt::print("cprop forward_always pin:{} to pin:{}\n",inp.driver.debug_name(), out.sink.debug_name()));
      out.sink.connect_driver(inp.driver);
    }
  }
  TRACE(fmt::print("cprop forward_always del_node node:{}\n",node.debug_name()));
  if (can_delete)
    node.del_node();
}

void Pass_cprop::collapse_forward_for_pin(Node &node, Node_pin &new_dpin) {
  for (auto &out : node.out_edges()) {
    new_dpin.connect_sink(out.sink);
  }

  node.del_node();
}

void Pass_cprop::try_collapse_forward(Node &node, XEdge_iterator &inp_edges_ordered) {

  // No need to collapse things like const -> join because the Lconst will be forward eval

  auto op = node.get_type().op;

  if (inp_edges_ordered.size()==1) {
    if (op == Sum_Op || op == Mult_Op || op == Div_Op || op == Mod_Op || op == Join_Op || op == And_Op || op == Or_Op ||
        op == Xor_Op) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
      return;
    }
  }

  if (op == Sum_Op || op == Mult_Op) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == And_Op || op == Or_Op || op == Xor_Op) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == Mux_Op) {
    // If all the options are the same. Collapse forward
    auto &a_pin = inp_edges_ordered[1].driver;
    for(auto i=2u;i<inp_edges_ordered.size();++i) {
      if (a_pin != inp_edges_ordered[i].driver)
        return;
    }
    collapse_forward_for_pin(node, a_pin);
  }
}

void Pass_cprop::replace_part_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type().op;
  if (op == Mux_Op) {
    auto s_node = inp_edges_ordered[0].driver.get_node();
    if (!s_node.is_type_const())
      return;

    I(s_node.get_type_const().is_i()); // string with ??? in mux? Give me test case to debug
    size_t sel = s_node.get_type_const().to_i();

    Node_pin a_pin;
    if ((sel+1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
      a_pin = node.get_class_lgraph()->create_node_const(0).get_driver_pin();
    } else {
      a_pin = inp_edges_ordered[sel+1].driver;
    }

    collapse_forward_for_pin(node, a_pin);
  }
}

void Pass_cprop::replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {

  // simple constant propagation
  auto op = node.get_type().op;
  if (op == Join_Op) {
    Lconst result;

    for (auto rit = inp_edges_ordered.rbegin(); rit!= inp_edges_ordered.rend(); ++rit) {
      auto &i = *rit;

      result = result << i.driver.get_bits();
      auto c = i.driver.get_node().get_type_const();
      I(c.get_bits()<=i.driver.get_bits());
      result  = result | c;
    }
    result = result.adjust_bits(node.get_driver_pin().get_bits());

    TRACE(fmt::print("cprop: join to {}\n", result.to_pyrope()));

    replace_node(node, result);

  }else if (op == ShiftLeft_Op) {

    Lconst val = node.get_sink_pin("A").get_driver_node().get_type_const();
    Lconst amt = node.get_sink_pin("B").get_driver_node().get_type_const();

    Lconst result = val<<amt;

    TRACE(fmt::print("cprop: shl to {} ({}<<{})\n", result.to_pyrope(), val.to_pyrope(), amt.to_pyrope()));

    replace_node(node, result);
  }else if (op == Sum_Op) {

    Lconst result;
    for(auto &i:inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (i.sink.get_pid()==0 || i.sink.get_pid()==1) {
        result = result + c;
      }else{
        result = result - c;
      }
    }

    TRACE(fmt::print("cprop: add node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  }else if (op == Or_Op) {
		uint16_t max_bits = 0;
		for(auto &i:inp_edges_ordered) {
			auto c = i.driver.get_node().get_type_const();
			if (c.get_bits() > max_bits)
				max_bits = c.get_bits();
		}
		Lconst result(0);
		for(auto &i:inp_edges_ordered) {
			auto c = i.driver.get_node().get_type_const();
			result = result.or_op(c.adjust_bits(max_bits));
		}

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

		Lconst result_reduced = result==0?0:1;

    replace_logic_node(node, result, result_reduced);

  }else if (op == And_Op) {
		uint16_t max_bits = 0;
		for(auto &i:inp_edges_ordered) {
			auto c = i.driver.get_node().get_type_const();
			if (c.get_bits() > max_bits)
				max_bits = c.get_bits();
		}
		Lconst result("-1s");
		for(auto &i:inp_edges_ordered) {
			auto c = i.driver.get_node().get_type_const();
			result = result.and_op(c.adjust_bits(max_bits));
		}

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

		Lconst result_reduced = result==Lconst("-1s")?1:0;

    replace_logic_node(node, result, result_reduced);

  }else if (op == Equals_Op) {
    bool eq=true;
    I(inp_edges_ordered.size()>1);
    auto first = inp_edges_ordered[0].driver.get_node().get_type_const();
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      auto c = inp_edges_ordered[i].driver.get_node().get_type_const();
      eq     = eq && first.equals_op(c);
    }

    Lconst result(eq?1:0);

    TRACE(fmt::print("cprop: eq node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  }else if (op == Mux_Op) {
    auto sel_const = inp_edges_ordered[0].driver.get_node().get_type_const();
    I(sel_const.is_i()); // string with ??? in mux? Give me test case to debug

    size_t sel = sel_const.to_i();

    Lconst result;
    if ((sel+1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
    } else {
      result = inp_edges_ordered[sel + 1].driver.get_node().get_type_const();
    }

    replace_node(node, result);
  }
}

void Pass_cprop::replace_node(Node &node, const Lconst &result) {

  auto new_node = node.get_class_lgraph()->create_node_const(result);
  auto dpin     = new_node.get_driver_pin();

  for(auto &out:node.out_edges()) {
    if (dpin.get_bits() == out.driver.get_bits() || out.driver.get_bits()==0) {
      TRACE(fmt::print("cprop: const:{} to out.driver:{}\n", result.to_pyrope(), out.driver.debug_name()));
      dpin.connect_sink(out.sink);
    }else{
      // create new const node to preserve bits
      auto result2 = result.adjust_bits(out.driver.get_bits());

      auto dpin2   = node.get_class_lgraph()->create_node_const(result2).get_driver_pin();

      TRACE(fmt::print("creating const:{} {}bits {}  from const:{} {}bits\n"
          , result2.to_pyrope(), out.driver.get_bits(), dpin2.get_bits()
          , result.to_pyrope() , dpin.get_bits()));

      dpin2.connect_sink(out.sink);
    }

    //out.del_edge();
  }

  node.del_node();
}

void Pass_cprop::replace_logic_node(Node &node, const Lconst &result, const Lconst &result_reduced) {

	Node_pin dpin_0;
	Node_pin dpin_1;

  for(auto &out:node.out_edges()) {
		if (out.driver.get_pid()) {
			// Reduction
			if (dpin_1.is_invalid()) {
				dpin_1   = node.get_class_lgraph()->create_node_const(result_reduced).get_driver_pin();
			}
			dpin_1.connect_sink(out.sink);
		}else{
			// bitwise op
			if (dpin_0.is_invalid()) {
				dpin_0   = node.get_class_lgraph()->create_node_const(result).get_driver_pin();
			}
			dpin_0.connect_sink(out.sink);
		}
	}

  node.del_node();
}

void Pass_cprop::trans(LGraph *g) {

  for (auto node : g->forward()) {
    fmt::print("node {}\n",node.debug_name());

    if (!node.has_outputs()) {
      if (!node.is_type_sub()) // No subs (inside side-effets
        node.del_node();
      continue;
    }

    int  n_inputs_constant   = 0;
    int  n_inputs            = 0;

    auto inp_edges_ordered = node.inp_edges_ordered();

    for (auto e : inp_edges_ordered) {
      n_inputs++;
      if (e.driver.get_node().is_type_const())
        n_inputs_constant++;
    }

    if (n_inputs == n_inputs_constant && n_inputs) {
      replace_all_inputs_const(node, inp_edges_ordered);
    }else if (n_inputs) {
      replace_part_inputs_const(node, inp_edges_ordered);
    }
    if (node.is_invalid()) {
      continue;  // It got deleted
    }

    // Try again to collapse forward if some inputs were constant
    try_collapse_forward(node, inp_edges_ordered);
  }

  for(auto node:g->fast()) {
    if (!node.has_outputs()) {
      if (!node.is_type_sub()) // No subs (inside side-effets
        node.del_node();
      continue;
    }
  }

  g->sync();
}
