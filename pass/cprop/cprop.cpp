//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>

#include "cprop.hpp"
#include "lbench.hpp"
#include "lgcpp_plugin.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgtuple.hpp"
#include "pass_cprop.hpp"

#define TRACE(x)
//#define TRACE(x) x

Cprop::Cprop (bool _hier, bool _at_gioc) : hier(_hier), at_gioc(_at_gioc) {}

void Cprop::collapse_forward_same_op(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type_op();

  bool all_done = true;
  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type_op() != op) {
      all_done = false;
      continue;
    }

    if (out.driver.get_pid() != out.sink.get_pid()) { //FIXME: maybe separate different op
      all_done = false;
      continue;
    }

    for (auto &inp : inp_edges_ordered) {
      TRACE(fmt::print("cprop same_op pin:{} to pin:{}\n", inp.driver.debug_name(), out.sink.debug_name()));
      if (op == Ntype_op::Xor) {
        // fmt::print("cprop xor pin:{} node.size:{} sink.size:{}\n",inp.driver.debug_name(), node.get_num_edges(),
        // inp.driver.get_node().get_num_edges()); if (out.sink.is_connected(inp.driver)) {
        if (inp.driver.is_connected(out.sink)) {
          out.sink.del_driver(inp.driver);
        } else {
          out.sink.connect_driver(inp.driver);
        }
      } else if (op == Ntype_op::Or || op == Ntype_op::And) {
        // fmt::print("cprop simplified forward or/and pin:{}\n",inp.driver.debug_name());
        out.sink.connect_driver(inp.driver);
      } else {
        I(op != Ntype_op::Sum);  // handled at collapse_forward_sum
        out.sink.connect_driver(inp.driver);
      }
    }

    TRACE(fmt::print("cprop same_op del_edge pin:{} to pin:{}\n", out.driver.debug_name(), out.sink.debug_name()));
    out.del_edge();
  }
  if (all_done) {
    I(!node.has_outputs());
    node.del_node();
  }
}

void Cprop::collapse_forward_sum(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type_op();
  I(op == Ntype_op::Sum);
  bool all_edges_deleted = true;
  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type_op() != Ntype_op::Sum) {
      all_edges_deleted = false;
      continue;
    }

    auto next_sum_node = out.sink.get_node();
    for (auto &inp : inp_edges_ordered) {
      TRACE(fmt::print("cprop same_op pin:{} to pin:{}\n", inp.driver.debug_name(), out.sink.debug_name()));
      //auto sink_name = Ntype::get_sink_name(Ntype_op::Sum, inp.sink.get_pid()); //use get_pin_name or pin_raw
      //auto next_sum_spin = next_sum_node.setup_sink_pin(sink_name);  // Connect same PID
      //
      // Sum(A,Sum(B,C))  = Sum(A+C,B)
      // Sum(Sum(A,B),C)) = Sum(A+C,B)
      if (inp.sink.get_pid() == 0 && out.sink.get_pid() == 0) { // Sum(A+Sum(B)) = Sum(A+B)
        out.sink.connect_driver(inp.driver);
      }else if (inp.sink.get_pid() == 0 && out.sink.get_pid() == 1) { // Sum(A+Sum(,B)) = Sum(A,B)
        out.sink.connect_driver(inp.driver);
      }else if (inp.sink.get_pid() == 1 && out.sink.get_pid() == 0) { // Sum(,A+Sum(B)) = Sum(,A+B)
        next_sum_node.setup_sink_pin("B").connect_driver(inp.driver);
      }else{ // Sum(,A+Sum(,B)) = Sum(B,A)
        next_sum_node.setup_sink_pin("A").connect_driver(inp.driver);
      }
    }
    TRACE(fmt::print("cprop same_op del_edge pin:{} to pin:{}\n", out.driver.debug_name(), out.sink.debug_name()));
    out.del_edge();
  }

  if (all_edges_deleted) {
    bwd_del_node(node);
  }
}

#if 0
void Cprop::collapse_forward_shiftleft(Node &node) {
  I(node.get_type_op()==ShiftLeft_Op);

  // a <<n | b -> join(a,b) if b.bits>=n
  for (auto &out : node.out_edges()) {
    auto sink_node = out.sink.get_node();
    if (sink_node.get_type_op() != Or_Op) continue;

    if (!sink_node.get_driver_pin(0).has_outputs()) continue;

    HERE
  }
}
#endif

// Collase forward single node but only for pid!=0 (not reduction ops)
void Cprop::collapse_forward_always_pin0(Node &node, XEdge_iterator &inp_edges_ordered) {

  auto op = node.get_type_op();

  for (auto &out : node.out_edges()) {
    for (auto &inp : inp_edges_ordered) {
      TRACE(fmt::print("cprop forward_always pin:{} to pin:{}\n", inp.driver.debug_name(), out.sink.debug_name()));
      if (op == Ntype_op::Xor) {
        if (inp.driver.is_connected(out.sink)) {
          out.sink.del_driver(inp.driver);
        } else {
          out.sink.connect_driver(inp.driver);
        }
      } else {
        out.sink.connect_driver(inp.driver);
      }
    }
  }

  TRACE(fmt::print("cprop forward_always del_node node:{}\n", node.debug_name()));
  bwd_del_node(node);
}

void Cprop::collapse_forward_for_pin(Node &node, Node_pin &new_dpin) {
  for (auto &out : node.out_edges()) {
    new_dpin.connect_sink(out.sink);
  }

  bwd_del_node(node);
}

bool Cprop::try_constant_prop(Node &node, XEdge_iterator &inp_edges_ordered) {
  int n_inputs_constant = 0;
  int n_inputs          = 0;
  for (auto e : inp_edges_ordered) {
    n_inputs++;
    auto drv_node = e.driver.get_node();
    if (!drv_node.is_type_const())
      continue;
    const auto &lc = drv_node.get_type_const();
    if (lc.is_string())
      continue;
    n_inputs_constant++;
  }

  if (n_inputs == n_inputs_constant && n_inputs) {
    replace_all_inputs_const(node, inp_edges_ordered);
    return true;
  } else if (n_inputs && n_inputs_constant>1) {
    replace_part_inputs_const(node, inp_edges_ordered);
    return true;
  }

  return false;
}

void Cprop::try_collapse_forward(Node &node, XEdge_iterator &inp_edges_ordered) {
  // No need to collapse things like const -> join because the Lconst will be forward eval
  auto op = node.get_type_op();

  if (inp_edges_ordered.size() == 1) {
    auto prev_op = inp_edges_ordered[0].driver.get_node().get_type_op();
    if (op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::Div || op == Ntype_op::And || op == Ntype_op::Or
        || op == Ntype_op::Xor) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
      return;
    }
    if (prev_op == Ntype_op::Tposs) {
      if (op == Ntype_op::Tposs) {
        collapse_forward_always_pin0(node, inp_edges_ordered);
        return;
      }else if (op == Ntype_op::Ror) {
        auto prev_node = inp_edges_ordered[0].driver.get_node();
        auto prev_inp_edges = prev_node.inp_edges();
        collapse_forward_always_pin0(prev_node, prev_inp_edges);
      }
    }
  }

  if (op == Ntype_op::Sum) {
    collapse_forward_sum(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mult || op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mux) {
    // If all the options are the same. Collapse forward
    auto &a_pin = inp_edges_ordered[1].driver;
    for (auto i = 2u; i < inp_edges_ordered.size(); ++i) {
      if (a_pin != inp_edges_ordered[i].driver)
        return;
    }
    collapse_forward_for_pin(node, a_pin);
  }
}

void Cprop::replace_part_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type_op();
  if (op == Ntype_op::Mux) {
    auto s_node = inp_edges_ordered[0].driver.get_node();
    if (!s_node.is_type_const())
      return;

    I(s_node.get_type_const().is_i());  // string with ??? in mux? Give me test case to debug
    size_t sel = s_node.get_type_const().to_i();

    Node_pin a_pin;
    if ((sel + 1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
      a_pin = node.get_class_lgraph()->create_node_const(0).get_driver_pin();
    } else {
      a_pin = inp_edges_ordered[sel + 1].driver;
    }

    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::Sum || op == Ntype_op::Or) {
    Lconst result;
    XEdge first_const_edge;
    int nconstants = 0;
    int npending = 0;
    XEdge_iterator edge_it2;
    for (auto &i : inp_edges_ordered) {
      if (!i.driver.get_node().is_type_const()) {
        if (npending==0)
          edge_it2.push_back(i);
        npending++;
        continue;
      }

      auto c = i.driver.get_node().get_type_const();

      if (c == 0) { // zero, just drop
        i.del_edge();
        continue;
      }

      ++nconstants;

      if (op == Ntype_op::Sum) {
        if (i.sink.get_pin_name() == "A") {
          result = result.add_op(c);
        } else {
          I(i.sink.get_pin_name() == "B");
          result = result.sub_op(c);
        }
      }else{
        I(op==Ntype_op::Or);
        result = result.or_op(c);
      }

      if (nconstants==1)
        first_const_edge = i;
      else
        i.del_edge();
    }

    if (nconstants>1) {
      first_const_edge.del_edge();
      if (result!=0) {
        auto new_node = node.get_class_lgraph()->create_node_const(result);
        auto dpin     = new_node.get_driver_pin();
        if (result > 0 || op ==Ntype_op::Or) {
          node.setup_sink_pin("A").connect_driver(dpin);  // add, Or
        } else {
          node.setup_sink_pin("B").connect_driver(dpin);  // substract
        }
      }else if (npending==1) {
				collapse_forward_always_pin0(node, edge_it2);
			}
    }else if (npending==0 && nconstants==1) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
    }else if (npending==1 && nconstants==0) {
      collapse_forward_always_pin0(node, edge_it2);
    }
  }
}

void Cprop::replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {
  // simple constant propagation
  auto op = node.get_type_op();
  if (op == Ntype_op::SHL) {
    Lconst val = node.get_sink_pin("a").get_driver_node().get_type_const();
    Lconst amt = node.get_sink_pin("b").get_driver_node().get_type_const();

    Lconst result = val << amt;

    TRACE(fmt::print("cprop: shl to {} ({}<<{})\n", result.to_pyrope(), val.to_pyrope(), amt.to_pyrope()));

    replace_node(node, result);
  } else if (op == Ntype_op::Tposs) {
    Lconst val = node.get_sink_pin("a").get_driver_node().get_type_const();
    replace_node(node, val.tposs_op());
  } else if (op == Ntype_op::Sum) {
    Lconst result;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (i.sink.get_pid() == 0) {
        result = result + c;
      } else { //pid = 1
        result = result - c;
      }
    }

    TRACE(fmt::print("cprop: add node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Ntype_op::Or) {
    Bits_t max_bits = 0;
    for (auto &e : inp_edges_ordered) {
      auto c = e.driver.get_node().get_type_const();
      if (c.get_bits() > max_bits)
        max_bits = c.get_bits();
    }
    Lconst result(0);
    for (auto &e : inp_edges_ordered) {
      auto c = e.driver.get_node().get_type_const();
      result = result.or_op(c.adjust_bits(max_bits));
    }

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    Lconst result_reduced = result == 0 ? 0 : 1;
    fmt::print("node {}, result {}, result_reduced {}\n", node.debug_name(), result.to_i(), result_reduced.to_i());
    replace_logic_node(node, result, result_reduced);

  } else if (op == Ntype_op::And) {
    Bits_t max_bits = 0;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (c.get_bits() > max_bits)
        max_bits = c.get_bits();
    }
    Lconst result("-1");
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      result = result.and_op(c.adjust_bits(max_bits));
    }

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    Lconst result_reduced = result == Lconst("-1") ? 1 : 0;

    replace_logic_node(node, result, result_reduced);

  } else if (op == Ntype_op::EQ) {
    bool eq = true;
    I(inp_edges_ordered.size() > 1);
    auto first = inp_edges_ordered[0].driver.get_node().get_type_const();
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      auto c = inp_edges_ordered[i].driver.get_node().get_type_const();
      eq     = eq && first.eq_op(c);
    }

    Lconst result(eq ? 1 : 0);

    TRACE(fmt::print("cprop: eq node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Ntype_op::Mux) {
    auto sel_const = inp_edges_ordered[0].driver.get_node().get_type_const();
    I(sel_const.is_i());  // string with ??? in mux? Give me test case to debug

    size_t sel = sel_const.to_i();

    Lconst result;
    if ((sel + 1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
    } else {
      result = inp_edges_ordered[sel + 1].driver.get_node().get_type_const();
    }

    replace_node(node, result);
  } else {
    fmt::print("FIXME: cprop still does not copy prop node:{}\n", node.debug_name());
  }
}

void Cprop::replace_node(Node &node, const Lconst &result) {
  auto new_node = node.get_class_lgraph()->create_node_const(result);
  auto dpin     = new_node.get_driver_pin();

  for (auto &out : node.out_edges()) {
    if (dpin.get_bits() == out.driver.get_bits() || out.driver.get_bits() == 0) {
      TRACE(fmt::print("cprop: const:{} to out.driver:{}\n", result.to_pyrope(), out.driver.debug_name()));
      dpin.connect_sink(out.sink);
    } else {
      // create new const node to preserve bits
      auto result2 = result.adjust_bits(out.driver.get_bits());

      auto dpin2 = node.get_class_lgraph()->create_node_const(result2).get_driver_pin();

      TRACE(fmt::print("creating const:{} {}bits {}  from const:{} {}bits\n",
                       result2.to_pyrope(),
                       out.driver.get_bits(),
                       dpin2.get_bits(),
                       result.to_pyrope(),
                       dpin.get_bits()));

      dpin2.connect_sink(out.sink);
    }
  }

  node.del_node();
}

// FIXME: not sure
void Cprop::replace_logic_node(Node &node, const Lconst &result, const Lconst &result_reduced) {
  Node_pin dpin_0;
  (void) result_reduced; // no useage for now

  for (auto &out : node.out_edges()) {
    if (dpin_0.is_invalid()) {
      dpin_0 = node.get_class_lgraph()->create_node_const(result).get_driver_pin();
    }
    dpin_0.connect_sink(out.sink);
  }

  node.del_node();
}

void Cprop::try_connect_tuple_to_sub(Node_pin &dollar_spin, std::shared_ptr<Lgtuple> tup, Node &sub_node, Node &tup_node) {
  I(sub_node.is_type_sub_present());
  I(tup_node.get_type_op() == Ntype_op::TupAdd);

  const auto &sub = sub_node.get_type_sub_node();

	bool sub_dollar_is_gone = false;

  for(const auto *io_pin:sub.get_input_pins()) {
    if (io_pin->name == "$") {
			if (io_pin->is_invalid()) {
				sub_dollar_is_gone = true;
			}
      continue;
		}

    if (tup->has_dpin(io_pin->name)) {
      auto dpin = tup->get_dpin(io_pin->name);
      sub_node.get_sink_pin(io_pin->name).connect_driver(dpin);
    }else{
      Pass::info("could not find IO {} in graph {}", io_pin->name, sub.get_name());
      tuple_issues = true;
    }
  }

  if (!tuple_issues || sub_dollar_is_gone) {
    for(const auto &it2:tup->get_map()) {
      if (!sub.is_input(it2.first)) {
        Pass::info("potential issue, field {} unused by the sub {} at node {}", it2.first, sub.get_name(), sub_node.debug_name());
      }
    }
    dollar_spin.del();
    if (!tup_node.has_outputs())
      bwd_del_node(tup_node);
  }
}

void Cprop::try_connect_lgcpp(Node &node) {

  const auto &sub = node.get_type_sub_node();
  const auto &reg = Lgcpp_plugin::get_registry();

  auto it = reg.find(sub.get_name());
  if (it != reg.end()) {
    fmt::print("cprop subgraph:{} is not present, found lgcpp...\n", sub.get_name());

    std::shared_ptr<Lgtuple> inp;
    std::shared_ptr<Lgtuple> out;
    it->second(node.get_class_lgraph(), inp, out);

    if (!out) { // no out tuple populated
      return;
    }
    fmt::print("cprop subgraph:{} has out\n", sub.get_name());
    out->dump();

    for (auto dpin : node.out_connected_pins()) {
      fmt::print("dpin:{} pid:{} testing...\n", dpin.debug_name(), dpin.get_pid());
      if (dpin.has_name()) {
        if (out->has_dpin(dpin.get_name())) {
          fmt::print("replace dpin:{}\n", dpin.get_name());
        } else {
          fmt::print("dpin:{} disconnected. name Remove\n", dpin.get_name());
        }
      } else {
        if (out->has_dpin(dpin.get_pid())) {
          fmt::print("replace dpin:{} pid:{}\n", dpin.debug_name(), dpin.get_pid());
        } else {
          fmt::print("dpin:{} disconnected. pos Remove\n", dpin.debug_name());
        }
      }
    }
  }
}

void Cprop::try_connect_sub_inputs(Node &node) {

  I(!hier);

  const auto &sub = node.get_type_sub_node();

  Node_pin dollar_spin;
  for(auto &spin:node.inp_connected_pins()) {
    const auto &io_pin = sub.get_io_pin_from_instance_pid(spin.get_pid());
    if (io_pin.name == "$") {
      dollar_spin = spin;
    }else if (io_pin.dir != Sub_node::Direction::Input) { // OOPS!!!
      Pass::error("graph {} connects to subgraph {} and the inputs have changed. Recompile {}", node.get_class_lgraph()->get_name(), sub.get_name(), node.get_class_lgraph()->get_name());
    }
  }

  if (!dollar_spin.is_invalid()) {
    auto parent_node = dollar_spin.get_driver_node();
    auto it2 = node2tuple.find(parent_node.get_compact());
    if (it2 != node2tuple.end()) {
      try_connect_tuple_to_sub(dollar_spin, it2->second, node, parent_node);
    }
  }
}

void Cprop::process_subgraph(Node &node) {

  if (!node.is_type_sub_present()) {
    // Still a blackbox, not much to do
    for(const auto &e:node.inp_edges()) {
      if (node2tuple.contains(e.driver.get_node().get_compact())) {
        tuple_issues = true;
        return;
      }
    }

    try_connect_lgcpp(node);
    return;
  }

  if (hier)
    return;

  try_connect_sub_inputs(node);
}

void Cprop::process_attr_q_pin(Node &node, Node_pin &parent_dpin) {
  // if some attribute are set on the flop, you will get that dpin instead of the real flop qpin
  if (parent_dpin.get_node().get_type_op() == Ntype_op::AttrSet) {
    collapse_forward_for_pin(node, parent_dpin);
    return;
  }

  // Get variable name
  auto driver_wname = parent_dpin.get_name();

  // remove the SSA from name
  auto pos   = driver_wname.find_last_of('_');
  auto wname = driver_wname.substr(0, pos);

  if (wname == driver_wname) {
    collapse_forward_for_pin(node, parent_dpin);
  } else {
    // Find flop
    auto target_ff_qpin = Node_pin::find_driver_pin(node.get_class_lgraph(), wname);
    collapse_forward_for_pin(node, target_ff_qpin);
  }
}

bool Cprop::process_attr_get(Node &node) {
  I(node.get_type_op() == Ntype_op::AttrGet);

  if (!node.is_sink_connected("name") || !node.is_sink_connected("field"))
    return false;

  // Either pos or name
  auto parent_dpin   = node.get_sink_pin("name").get_driver_pin();
  auto key_name_dpin = node.get_sink_pin("field").get_driver_pin();

  I(key_name_dpin.get_node().get_type_op() == Ntype_op::TupKey);
  I(key_name_dpin.has_name());
  auto key_name = key_name_dpin.get_name();
  if (key_name.substr(0, 2) == "__") { //FIXME->sh: why not merge?
    if (key_name.substr(0, 7) == "__q_pin") {
      fmt::print("process_attr_q_pin parent_node:{} node:{}\n", parent_dpin.get_node().debug_name(), node.debug_name());
      process_attr_q_pin(node, parent_dpin);
      return true;
    }
  }
  return false;
}

std::tuple<std::string_view, std::string_view, int> Cprop::get_tuple_name_key(Node &node) {
  std::string_view tup_name;
  std::string_view key_name;
  int              key_pos = -1;
  if (node.is_sink_connected("field")) {
    auto node2 = node.get_sink_pin("field").get_driver_node();
    if (node2.get_type_op() == Ntype_op::TupKey)
      key_name = node2.get_driver_pin().get_name();
  }

  for(const auto &dpin : node.setup_sink_pin("tuple_name").inp_driver()) {
    if (dpin.has_name()) {
      tup_name = dpin.get_name();
      break;
    }
  }

  if (node.is_sink_connected("position")) {
    auto node2 = node.get_sink_pin("position").get_driver_node();
    if (node2.is_type_const())
      key_pos = node2.get_type_const().to_i();
  }

  // I(!key_name.empty() || key_pos != -1);  // At least one defined // FIXME->sh: not necessarily true, could be resolved at later
  // TupAdd merge step

  if (tup_name.size() && key_name.size() && tup_name[0] == '%' && key_name[0] == '%') {
    fmt::print("FIXME: the tupple is {}, the key/field {} should not include the % (fixing it)\n", tup_name, key_name);
    key_name = key_name.substr(1);
  } if (key_name.size() && key_name[0] == '%') {
    fmt::print("FIXME: the tupple is {}, the key/field {} should not include the % (fixing it)\n", tup_name, key_name);
    key_name = key_name.substr(1);
    // What about tup_name, it should have a '%' as the only name
  }

  return std::make_tuple(tup_name, key_name, key_pos);
}

bool Cprop::process_tuple_get(Node &node) {

  I(node.get_type_op() == Ntype_op::TupGet);

  auto parent_dpin  = node.get_sink_pin("tuple_name").get_driver_pin();
  auto parent_node  = parent_dpin.get_node();
  auto [tup_name, key_name, key_pos] = get_tuple_name_key(node);

  // this attr comes from tail of TG chain where the TG tail has been transformed into an AttrSet node.
  if (parent_node.get_type_op() == Ntype_op::AttrSet) {
    auto attr_val_dpin = parent_node.get_sink_pin("value").get_driver_pin();
    collapse_forward_for_pin(node, attr_val_dpin);
    return true;
  }

  auto ptup_it = node2tuple.find(parent_node.get_compact());
  if (ptup_it == node2tuple.end()) {
    if (key_pos == 0 && !parent_dpin.is_invalid()) {
      collapse_forward_for_pin(node, parent_dpin);
      return true;
		}

		Pass::info("tuple_get {} could not decide the field {}!", node.debug_name(), key_name);
    return false;
  }

  const auto node_tup = ptup_it->second;
  auto val_dpin = node_tup->get_dpin(key_pos, key_name);
  if (!val_dpin.is_invalid()) {

    int conta=0;
    for(auto it : node_tup->get_level_attributes(key_pos, key_name)) {
      auto attr_key_node = node.get_lg()->create_node(Ntype_op::TupKey);
      auto attr_key_dpin = attr_key_node.setup_driver_pin();
      attr_key_dpin.set_name(it.first);

      if (conta==0) {

        fmt::print("cprop: changing node:{} to AttrSet node for attr:{} from pin:{}\n",node.debug_name(), it.first, it.second.debug_name());
        // Reuse current node. First delete input edges
        for(auto e:node.inp_edges()) {
          e.del_edge();
        }

        node.set_type(Ntype_op::AttrSet); // Replace TupGet for AttrSet
        node.setup_sink_pin("name").connect_driver(val_dpin);
        node.setup_sink_pin("field").connect_driver(attr_key_dpin);
        node.setup_sink_pin("value").connect_driver(it.second);
      } else {
        I(false); // FIXME: TODO handle multiple attr set (create node)
      }
      conta++;
    }

    if (conta==0) { // No attributes
      collapse_forward_for_pin(node, val_dpin);
    }
    return true;
  }

  auto sub_tup = node_tup->get_sub_tuple(key_pos, key_name);
  if (!sub_tup) {
		Pass::info("tuple_get {} could not decide the field {}!", node.debug_name(), key_name);
    return false; // Could not resolve (maybe compile error, maybe hierarchical needed)
  }

  node2tuple[node.get_compact()] = sub_tup;
  return true;
}

void Cprop::process_mux(Node &node) {


  std::vector<std::shared_ptr<Lgtuple const>> tup_list;

  Node_pin sel_dpin;

  for(auto e:node.inp_edges_ordered()) { // Mux needs the edges ordered
    if (e.sink.get_pid() == 0) {
      I(e.sink.get_pin_name() == "0");

      sel_dpin = e.driver;
    }else{
      auto tup = find_lgtuple(e.driver);
      if (tup == nullptr) {
        tup_list.clear();
        break; // All have to have
      }

      tup_list.emplace_back(tup);
    }
  }

  if (!tup_list.empty()) {
    auto tup = Lgtuple::make_merge(sel_dpin, tup_list);
    if (!tup) {
      tuple_issues = true; // could not merge
      return;
    }
    node2tuple[node.get_compact()] = tup;
  }

}

std::shared_ptr<Lgtuple const> Cprop::find_lgtuple(Node_pin up_dpin) {

  auto up_node = up_dpin.get_node();
  auto ptup_it = node2tuple.find(up_node.get_compact());
  if (ptup_it == node2tuple.end()) {
    return nullptr;
  }

  I(up_node.get_type_op() == Ntype_op::TupAdd || up_node.get_type_op() == Ntype_op::TupGet ||
    up_node.get_type_op() == Ntype_op::TupRef || up_node.get_type_op() == Ntype_op::Mux);

  return ptup_it->second;
}

void Cprop::process_tuple_add(Node &node) {

  auto [tup_name, key_name, key_pos] = get_tuple_name_key(node);

  Node_pin                       parent_dpin;
  std::shared_ptr<Lgtuple const> parent_tup;
  std::shared_ptr<Lgtuple const> value_tup;
  bool parent_is_a_sub = false;
  std::shared_ptr<Lgtuple> node_tup;

  { // Try to get parent tuple or value tuple
    if(node.is_sink_connected("value")) {
      value_tup = find_lgtuple(node.get_sink_pin("value").get_driver_pin());
    }

    if (node.get_sink_pin("tuple_name").is_connected()) {
      parent_dpin = node.get_sink_pin("tuple_name").get_driver_pin();
      parent_tup = find_lgtuple(parent_dpin);
      parent_is_a_sub = parent_dpin.get_type_op() == Ntype_op::Sub;
      if (parent_tup) {
        node_tup = std::make_shared<Lgtuple>(*parent_tup);
      }
    }
  }

  if (!node_tup) {
    node_tup = std::make_shared<Lgtuple>(tup_name); // new tuple if not already created
    if (!value_tup && !parent_dpin.is_invalid()) {
      if (parent_dpin.get_node().get_type_op() != Ntype_op::TupRef)
        node_tup->add(0, parent_dpin); // the chain was a constant
    }
  }

  if (value_tup) {
    if (key_pos<0 && key_name.empty()) { // Tuple Concatenation
      bool ok = node_tup->concat(value_tup);
      if (!ok) {
        tuple_issues = true;
        return;
      }
    } else {
      node_tup->add(key_pos, key_name, value_tup);
    }
  } else if (node.is_sink_connected("value")) {

    auto val_dpin = node.get_sink_pin("value").get_driver_pin();
    I(val_dpin.get_node().get_type_op() != Ntype_op::TupAdd); // value_tup should be true otherwise

    node_tup->add(key_pos, key_name, val_dpin);
  } else if (parent_is_a_sub) {
    auto parent_node = node.get_sink_pin("tuple_name").get_driver_node();
    I(parent_node.is_type_sub());

    const auto &sub = parent_node.get_type_sub_node();
    for(const auto *io_pin:sub.get_output_pins()) {
      auto sub_dpin = parent_node.get_driver_pin(io_pin->name);
      if (io_pin->has_io_pos())
        node_tup->add(io_pin->get_io_pos(), io_pin->name, sub_dpin);
      else
        node_tup->add(-1, io_pin->name, sub_dpin);
    }
  }else{
    I(parent_tup); // tup1 = tup2 can have no sink("value")
  }

  node2tuple[node.get_compact()] = node_tup;

  if (!hier && !tuple_issues) {
    for(auto &e:node.out_edges()) {
      if (e.sink.is_graph_output() && e.sink.get_pin_name() == "%") {
        try_create_graph_output(node, node_tup); // first add outputs
      }else if (e.sink.get_node().is_type_sub_present() && e.sink.get_pin_name() == "$") {
        auto sub_node = e.sink.get_node();
        try_connect_tuple_to_sub(e.sink, node_tup, sub_node, node);
      }
    }
  }
}

void Cprop::do_trans(LGraph *lg) {
  /* Lbench b("pass.cprop"); */
  /* bool tup_get_left = false; */

  tuple_issues = false;

  for (auto node : lg->forward()) {
    //fmt::print("{}\n", node.debug_name());
    auto op = node.get_type_op();

    // Special cases to handle in cprop
    if (op == Ntype_op::AttrGet) {
      process_attr_get(node);
      continue;
    } else if (op == Ntype_op::AttrSet) {
      continue;  // Nothing to do in cprop
    } else if (op == Ntype_op::Sub) {
      process_subgraph(node);
      continue;
    } else if (op == Ntype_op::Sflop || op == Ntype_op::Aflop || op == Ntype_op::Latch || op == Ntype_op::Fflop || op == Ntype_op::Memory || op == Ntype_op::Sub) {
      fmt::print("cprop skipping node:{}\n", node.debug_name());
      // FIXME: if flop feeds itself (no update, delete, replace for zero)
      // FIXME: if flop is disconnected *after AttrGet processed*, the flop was not used. Delete
      continue;
    } else if (!node.has_outputs()) {
      node.del_node();
      continue;
    } else if (op == Ntype_op::TupAdd) {
      process_tuple_add(node);
      continue;
    } else if (op == Ntype_op::TupGet) {
      auto ok = process_tuple_get(node);
      if (!ok) {
        Pass::info("cprop could not simplify node:{}",node.debug_name());
        tuple_issues = true;
      }
      continue;
    } else if (op == Ntype_op::Mux) {
      process_mux(node);
    }

    // Normal copy prop and strength reduction
    auto inp_edges_ordered = node.inp_edges_ordered();
    auto replaced_some = try_constant_prop(node, inp_edges_ordered);

    if (node.is_invalid())
      continue;  // It got deleted

    if (replaced_some) {
      inp_edges_ordered = node.inp_edges_ordered();
    }
    try_collapse_forward(node, inp_edges_ordered);
  }

  // FIXME: due to strange bug?? I move this function to the end of process_tuple_add
  /* auto last_ta = lg->get_graph_output("%").get_driver_node(); */
  /* fmt::print("last_ta:{}\n", last_ta.debug_name()); */
  /* auto tup = node2tuple[last_ta.get_compact()]; */
  /* tup->dump(); */
  /* try_create_graph_output(lg, tup); */


  for (auto node : lg->fast()) {
    if (!tuple_issues && node.is_type_tup()) {
      if (hier) {
        auto it = node2tuple.find(node.get_compact());
        if (it != node2tuple.end()) {
          node2tuple.erase(it);
        }
      }
      node.del_node();
      continue;
    }

    if (!node.has_outputs()) {
      auto op = node.get_type_op();
      if (op != Ntype_op::Sflop && op != Ntype_op::Aflop  && op != Ntype_op::Latch &&
          op != Ntype_op::Fflop && op != Ntype_op::Memory && op != Ntype_op::Sub   && op != Ntype_op::AttrSet) {
        // TODO: del_dead_end_nodes(); It can propagate back and keep deleting
        // nodes until it reaches a SubGraph or a driver_pin that has some
        // other outputs. Doing this dead_end_nodes delete iterator can retuce
        // the number of times that cprop needs to be called for deep chains.
        node.del_node();
      }
      continue;
    }
  }

  if (!hier) {
    node2tuple.clear();
  }

  if (!tuple_issues && (!hier || at_gioc)) {
    //remove unified input $ if fully resolved
    if (lg->has_graph_input("$")) {
      auto unified_inp = lg->get_graph_input("$");
      if (!unified_inp.has_outputs()) {
        unified_inp.get_non_hierarchical().del();
      }
    }

    //remove unified output % if fully resolved
    if (lg->has_graph_output("%")) {
      auto unified_out = lg->get_graph_output("%");
      if (!unified_out.has_inputs()) {
        unified_out.get_non_hierarchical().del();
      }
    }
  }
}

void Cprop::try_create_graph_output(Node &node, std::shared_ptr<Lgtuple> tup) {

  I(!hier);
  I(!tuple_issues);

  auto *g = node.get_class_lgraph();
  bool local_error = false;
  for (const auto &it : tup->get_map()) {
    auto out_name = it.first;
    if (unlikely(out_name.empty())) {
      local_error = true;
      Pass::info("Tuple {} for graph {} without named field (pyrope supports unnamed)", tup->get_name(), g->get_name());
    }
    if (out_name.find(".__") != std::string::npos)
      continue; // do not populate attributes to the IOs

		if (unlikely(it.second.is_invalid())) {
      local_error = true;
      Pass::error("graph {} has output but it has invalid field {}", g->get_name(), it.first);
			continue;
		}

    if (!g->has_graph_output(out_name)) {
      int pos = tup->get_pos(out_name);

      Port_ID x = Port_invalid;
      if (pos>=0) {
        x = pos;
      }
      auto flattened_gout = g->add_graph_output(out_name, x, 0);
      it.second.connect_sink(flattened_gout);
      I(!g->get_graph_output(out_name).is_invalid());
    }
  }

  if (!local_error) {
    bwd_del_node(node); // then delete current tup_add

    auto dpin = g->get_graph_output("%"); // then delete anything left at %
    dpin.get_non_hierarchical().del();
  }
}

void Cprop::bwd_del_node(Node &node) {
  // a more aggressive del_node that avoids iterations
  //
  // WARNING: call it only if all the extra edges added or it can delete nodes
  // that you may want to keep

  if (hier)
    return;

  I(!node.is_type_loop_breaker());

  std::vector<Node> potential;

  for(auto e:node.inp_edges()) {
    potential.emplace_back(e.driver.get_node());
  }

  node.del_node();

  while(!potential.empty()) {
    auto n = potential.back();
    potential.pop_back();

    if (!n.is_invalid() && !n.has_outputs() && !n.is_type_loop_breaker()) {
      for(auto e:n.inp_edges()) {
        potential.emplace_back(e.driver.get_node());
      }
      n.del_node();
    }

  }
}

void Cprop::dump_node2tuples() const {
  for(const auto &it:node2tuple) {
    fmt::print("node nid:{}\n",it.first.get_nid());
    it.second->dump();
  }
}
