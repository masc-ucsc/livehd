//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop.hpp"

#include <cctype>
#include <deque>
#include <string>

#include "lbench.hpp"
#include "lgcpp_plugin.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgtuple.hpp"
#include "pass_cprop.hpp"

#define TRACE(x)
//#define TRACE(x) x

// Single step CPROP for debugging
//#define TRIVIAL_CPROP

Cprop::Cprop(bool _hier, bool _at_gioc) : hier(_hier), at_gioc(_at_gioc) {}

std::tuple<Node_pin, std::shared_ptr<Lgtuple const> > Cprop::get_value(const Node &node) const {
  I(node.is_type(Ntype_op::TupAdd) || node.is_type(Ntype_op::AttrSet));

  auto value_spin = node.get_sink_pin("value");
  if (value_spin.is_connected()) {
    auto value_dpin = value_spin.get_driver_pin();
    auto tup = find_lgtuple(value_dpin);
    return std::make_tuple(value_dpin, tup);
  }

  return std::make_tuple(invalid_pin, nullptr);
}

void Cprop::collapse_forward_same_op(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type_op();

  bool all_done = true;
  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type_op() != op) {
      all_done = false;
      continue;
    }

    if (out.driver.get_pid() != out.sink.get_pid()) {  // FIXME: maybe separate different op
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
  if (inp_edges_ordered.size() > 32)
    return;  // Do not over flatten

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
      // auto sink_name = Ntype::get_sink_name(Ntype_op::Sum, inp.sink.get_pid()); //use get_pin_name or pin_raw
      // auto next_sum_spin = next_sum_node.setup_sink_pin(sink_name);  // Connect same PID
      //
      // Sum(A,Sum(B,C))  = Sum(A+C,B)
      // Sum(Sum(A,B),C)) = Sum(A+C,B)
      if (inp.sink.get_pid() == 0 && out.sink.get_pid() == 0) {  // Sum(A+Sum(B)) = Sum(A+B)
        out.sink.connect_driver(inp.driver);
      } else if (inp.sink.get_pid() == 0 && out.sink.get_pid() == 1) {  // Sum(A+Sum(,B)) = Sum(A,B)
        out.sink.connect_driver(inp.driver);
      } else if (inp.sink.get_pid() == 1 && out.sink.get_pid() == 0) {  // Sum(,A+Sum(B)) = Sum(,A+B)
        next_sum_node.setup_sink_pin("B").connect_driver(inp.driver);
      } else {  // Sum(,A+Sum(,B)) = Sum(B,A)
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
    (void)lc;
    //    if (lc.is_string())
    //      continue;
    n_inputs_constant++;
  }

  if (n_inputs == n_inputs_constant && n_inputs) {
    replace_all_inputs_const(node, inp_edges_ordered);
    return true;
  } else if (n_inputs && n_inputs_constant >= 1) {  // Some ops like shift can opt with 1 constant input
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
    if (prev_op == Ntype_op::Get_mask) {
      if (op == Ntype_op::Get_mask) {
        // Since this node get_mask has 1 input, value is not set
        collapse_forward_always_pin0(node, inp_edges_ordered);
        return;
      } else if (op == Ntype_op::Ror) {
        auto prev_node      = inp_edges_ordered[0].driver.get_node();
        auto prev_inp_edges = prev_node.inp_edges();
        collapse_forward_always_pin0(prev_node, prev_inp_edges);
      }
    }
  }

  if (op == Ntype_op::Sum) {
    collapse_forward_sum(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mult || op == Ntype_op::Or || op == Ntype_op::And || op == Ntype_op::Xor) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mux) {
    // If all the options are the same. Collapse forward
    if (inp_edges_ordered.size() <= 1) {
      node.del_node();
      return;
    }
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
#ifndef NDEBUG
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
#endif
      a_pin = node.create_const(0).get_driver_pin();
    } else {
      a_pin = inp_edges_ordered[sel + 1].driver;
    }

    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::Sum || op == Ntype_op::Or || op == Ntype_op::And) {
    XEdge first_const_edge;
    int   nconstants = 0;
    int   npending   = 0;

    Lconst result;
    if (op == Ntype_op::And)
      result = Lconst(-1);

    XEdge_iterator edge_it2;
    for (auto &i : inp_edges_ordered) {
      if (!i.driver.get_node().is_type_const()) {
        if (npending == 0)
          edge_it2.push_back(i);
        npending++;
        continue;
      }

      auto c = i.driver.get_node().get_type_const();

      ++nconstants;

      if (op == Ntype_op::Sum) {
        if (i.sink.get_pin_name() == "A") {
          result = result.add_op(c);
        } else {
          I(i.sink.get_pin_name() == "B");
          result = result.sub_op(c);
        }
      } else if (op == Ntype_op::Or) {
        result = result.or_op(c);
      } else {
        I(op == Ntype_op::And);
        result = result.and_op(c);
      }

      if (nconstants == 1)
        first_const_edge = i;
      else
        i.del_edge();
    }

    if (nconstants > 1) {
      first_const_edge.del_edge();
      if (result != 0) {
        auto new_node = node.create_const(result);
        auto dpin     = new_node.get_driver_pin();
        if (result > 0 || op == Ntype_op::Or) {
          node.setup_sink_pin("A").connect_driver(dpin);  // add, Or
        } else {
          node.setup_sink_pin("B").connect_driver(dpin);  // substract
        }
      } else if (npending == 1) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (npending == 0 && nconstants == 1) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
    } else if (npending == 1 && nconstants == 0) {
      if (!(op == Ntype_op::Sum && edge_it2[0].sink.get_pin_name() == "B"))
        collapse_forward_always_pin0(node, edge_it2);
    }
  } else if (op == Ntype_op::SRA || op == Ntype_op::SHL) {
    auto amt_node = inp_edges_ordered[1].driver.get_node();
    I(amt_node == node.get_sink_pin("b").get_driver_node());

    if (amt_node.is_type_const() && amt_node.get_type_const() == 0) {
      collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
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
  } else if (op == Ntype_op::Get_mask) {
    Lconst val       = node.get_sink_pin("a").get_driver_node().get_type_const();
    auto   mask_spin = node.get_sink_pin("mask");
    if (mask_spin.is_connected()) {
      auto mask = mask_spin.get_driver_node().get_type_const();
      replace_node(node, val.get_mask_op(mask));
    } else {
      replace_node(node, val.get_mask_op());  // old tposs
    }
  } else if (op == Ntype_op::Set_mask) {
    Lconst val        = node.get_sink_pin("a").get_driver_node().get_type_const();
    auto   mask_spin  = node.get_sink_pin("mask");
    auto   value_spin = node.get_sink_pin("value");

    if (mask_spin.is_connected() && value_spin.is_connected()) {
      auto mask  = mask_spin.get_driver_node().get_type_const();
      auto value = value_spin.get_driver_node().get_type_const();
      replace_node(node, val.set_mask_op(mask, value));
    } else {
      replace_node(node, val);
    }
  } else if (op == Ntype_op::Sum) {
    Lconst result;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (i.sink.get_pid() == 0) {
        result = result + c;
      } else {  // pid = 1
        result = result - c;
      }
    }

    TRACE(fmt::print("cprop: add node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Ntype_op::Or) {
    Lconst result;
    for (auto &e : inp_edges_ordered) {
      auto c = e.driver.get_node().get_type_const();
      result = result.or_op(c);
    }

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_logic_node(node, result);

  } else if (op == Ntype_op::And) {
    Lconst result("-1");
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      result = result.and_op(c);
    }

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);

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
#ifndef NDEBUG
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
#endif
    } else {
      result = inp_edges_ordered[sel + 1].driver.get_node().get_type_const();
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Mult) {
    Lconst result("1");
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      result = result.mult_op(c);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Div) {
    I(inp_edges_ordered.size() == 2);
    Lconst a = inp_edges_ordered[0].driver.get_type_const();
    Lconst b = inp_edges_ordered[1].driver.get_type_const();

    auto result = a.div_op(b);

    replace_node(node, result);
  } else {
#ifndef NDEBUG
    fmt::print("FIXME: cprop still does not copy prop node:{}\n", node.debug_name());
#endif
  }
}

void Cprop::replace_node(Node &node, const Lconst &result) {
  auto new_node = node.create_const(result);
  auto dpin     = new_node.get_driver_pin();

  for (auto &out : node.out_edges()) {
    if (dpin.get_bits() == out.driver.get_bits() || out.driver.get_bits() == 0) {
      TRACE(fmt::print("cprop: const:{} to out.driver:{}\n", result.to_pyrope(), out.driver.debug_name()));
      dpin.connect_sink(out.sink);
    } else {
      // create new const node to preserve bits
      auto result2 = result.adjust_bits(out.driver.get_bits());

      auto dpin2 = node.create_const(result2).get_driver_pin();

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

void Cprop::replace_logic_node(Node &node, const Lconst &result) {
  Node_pin dpin_0;

  for (auto &out : node.out_edges()) {
    if (dpin_0.is_invalid()) {
      dpin_0 = node.create_const(result).get_driver_pin();
    }
    dpin_0.connect_sink(out.sink);
  }

  node.del_node();
}

void Cprop::try_connect_tuple_to_sub(Node_pin &dollar_spin, std::shared_ptr<Lgtuple const> tup, Node &sub_node, Node &tup_node) {
  I(sub_node.is_type_sub_present());
  I(tup_node.get_type_op() == Ntype_op::TupAdd);

  const auto &sub = sub_node.get_type_sub_node();

  bool sub_dollar_is_gone = false;

  for (const auto *io_pin : sub.get_input_pins()) {
    if (io_pin->name == "$") {
      if (io_pin->is_invalid()) {
        sub_dollar_is_gone = true;
      }
      continue;
    }

    if (tup->has_dpin(io_pin->name)) {
      auto dpin = tup->get_dpin(io_pin->name);
      sub_node.get_sink_pin(io_pin->name).connect_driver(dpin);
    } else {
      Pass::info("could not find IO {} in graph {}", io_pin->name, sub.get_name());
      tuple_issues = true;
    }
  }

  if (!tuple_issues || sub_dollar_is_gone) {
    for (const auto &it2 : tup->get_map()) {
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
#ifndef NDEBUG
    fmt::print("cprop subgraph:{} is not present, found lgcpp...\n", sub.get_name());
#endif

    std::shared_ptr<Lgtuple> inp;
    std::shared_ptr<Lgtuple> out;
    it->second(node.get_class_lgraph(), inp, out);

    if (!out) {  // no out tuple populated
      return;
    }

#ifndef NDEBUG
    fmt::print("cprop subgraph:{} has out\n", sub.get_name());
    out->dump();
    for (auto dpin : node.out_connected_pins()) {
      fmt::print("dpin:{} pid:{} testing...\n", dpin.debug_name(), dpin.get_pid());
    }
#endif
  }
}

void Cprop::try_connect_sub_inputs(Node &node) {
  I(!hier);

  const auto &sub = node.get_type_sub_node();

  Node_pin dollar_spin;
  for (auto &spin : node.inp_connected_pins()) {
    const auto &io_pin = sub.get_io_pin_from_instance_pid(spin.get_pid());
    if (io_pin.name == "$") {
      dollar_spin = spin;
    } else if (io_pin.dir != Sub_node::Direction::Input) {  // OOPS!!!
      Pass::error("graph {} connects to subgraph {} and the inputs have changed. Recompile {}",
                  node.get_class_lgraph()->get_name(),
                  sub.get_name(),
                  node.get_class_lgraph()->get_name());
    }
  }

  if (!dollar_spin.is_invalid()) {
    auto parent_node = dollar_spin.get_driver_node();
    auto it2         = node2tuple.find(parent_node.get_compact());
    if (it2 != node2tuple.end()) {
      try_connect_tuple_to_sub(dollar_spin, it2->second, node, parent_node);
    }
  }
}

void Cprop::process_subgraph(Node &node, XEdge_iterator &inp_edges_ordered) {
  if (!node.is_type_sub_present()) {
    // Still a blackbox, not much to do
    for (const auto &e : inp_edges_ordered) {
      auto parent_node = e.driver.get_node();
      if (parent_node.is_type_tup() && !node2tuple.contains(e.driver.get_node().get_compact())) {
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

void Cprop::process_flop(Node &node) {
  if (tuple_issues)
    return;

  if (!node.has_outputs())
    return;

  auto din_spin = node.get_sink_pin("din");
  if (!din_spin.is_connected()) {
    Pass::info("flop:{} has no driving din (legal but strange)", node.debug_name());
    return;
  }

  auto din_node = node.get_sink_pin("din").get_driver_node();
  fmt::print("din_node:{}\n", din_node.debug_name());

  auto din_it = node2tuple.find(din_node.get_compact());
  if (din_it == node2tuple.end()) {
    // Unclear if done, It may need a 2nd pass
    if (!tuple_issues) {
      fmt::print("2nd iteration flop:{} still did not have tuple (this may be OK)\n", node.debug_name().c_str());
      return;
    }
    return;  // done or wait for 2nd iteration
  }

  auto node_it = node2tuple.find(node.get_compact());
  if (node_it == node2tuple.end()) {
    auto flop_tup = din_it->second->make_flop(node);
    if (flop_tup) {
      node2tuple[node.get_compact()] = flop_tup;
    }
  }
}

std::tuple<std::string, std::string> Cprop::get_tuple_name_key(Node &node) const {

  std::string key_name;
  if (node.is_sink_connected("position")) {
    auto node2 = node.get_sink_pin("position").get_driver_node();
    if (node2.is_type_const()) {
      key_name = node2.get_type_const().to_string();
    }
  }

  std::string tup_name;
  if (node.setup_driver_pin("Y").has_name()) {
    tup_name = node.setup_driver_pin("Y").get_name();
  }else{
    auto spin = node.get_sink_pin("tuple_name");
    if (spin.is_connected()) {
      auto dpin = spin.get_driver_pin();
      if (dpin.has_name())
        tup_name = dpin.get_name();
    }
  }

  if (tup_name.empty()) {
    tup_name = node.debug_name();
  }

  return std::make_tuple(tup_name, key_name);
}

bool Cprop::process_tuple_get(Node &node) {
  I(node.get_type_op() == Ntype_op::TupGet);

  if (tuple_issues)
    return true;

  auto parent_dpin          = node.get_sink_pin("tuple_name").get_driver_pin();
  auto parent_node          = parent_dpin.get_node();
  auto [tup_name, key_name] = get_tuple_name_key(node);

  std::shared_ptr<Lgtuple const> node_tup;

  auto ptup_it = node2tuple.find(parent_node.get_compact());
  if (ptup_it != node2tuple.end()) {
    node_tup = ptup_it->second;
  }

  if (key_name.empty()) {
    if (node.is_sink_connected("position") && node_tup) {
      auto field_node = node.get_sink_pin("position").get_driver_node();
      auto fieldtup_it = node2tuple.find(field_node.get_compact());
      if (fieldtup_it != node2tuple.end()) {
        auto sub_tup = node_tup->get_sub_tuple(fieldtup_it->second);
        if (sub_tup) {
          if (sub_tup->is_trivial_scalar()) {
            auto dpin = sub_tup->get_dpin();
            collapse_forward_for_pin(node, dpin);
          } else {
            node2tuple[node.get_compact()] = sub_tup;
          }
          return true;
        }
        fieldtup_it->second->dump();
        node.dump();
        Pass::error("FIXME: need to handle runtime tuple index node:{}\n",node.debug_name());
        tuple_issues = true;
        return false;
      }
    }
    Pass::error("tuple_get {} for tuple {} has no way to find field", node.debug_name(), tup_name);
    tuple_issues = true;
    return false;
  }

  if (node_tup) {
    bool is_attr_get = false;
    std::string main_field{key_name};
    if (Lgtuple::is_attribute(key_name)) {
      main_field = Lgtuple::get_all_but_last_level(key_name);
      is_attr_get = true;
    }

    auto sub_tup = node_tup->get_sub_tuple(main_field);
    if (sub_tup->is_trivial_scalar() || is_attr_get) {
      auto out_edges_list = node.out_edges();
      auto new_dpin = expand_data_and_attributes(node, "", out_edges_list, sub_tup);
      I(!node.has_outputs());
      if (is_attr_get) {
        if (new_dpin.is_invalid()) {
          Pass::info("tuple_get {} for key:{} has no data (may be OK)", node.debug_name(), key_name);
        }else{
          node.setup_sink_pin("tuple_name").del();
          node.set_type(Ntype_op::AttrGet);

          for(auto &e:new_dpin.out_edges()) {
            node.setup_driver_pin().connect_sink(e.sink);
            e.del_edge();
          }
          node.setup_sink_pin("name").connect_driver(new_dpin);
        }
      }else{
        std::string non_attr_name;
        {
          auto a_name = sub_tup->get_map()[0].first;
          if (Lgtuple::is_attribute(a_name))
            non_attr_name = Lgtuple::get_all_but_last_level(a_name);
          else
            non_attr_name = a_name;
        }
        if (non_attr_name.empty()) {
          bwd_del_node(node);
        }else{
          node.setup_sink_pin("tuple_name").del();
          node.setup_sink_pin("position").del();

          auto pos_dpin = node.create_const(Lconst::string(non_attr_name)).setup_driver_pin();
          node.setup_sink_pin("position").connect_driver(pos_dpin);

          node.set_type(Ntype_op::TupAdd);

          for(auto &e:new_dpin.out_edges()) {
            node.setup_driver_pin().connect_sink(e.sink);
            e.del_edge();
          }
          node.setup_sink_pin("value").connect_driver(new_dpin);

          node2tuple[node.get_compact()] = sub_tup;
        }
      }
    }else{
      node2tuple[node.get_compact()] = sub_tup;
    }

    return true;
  }

  if (key_name=="0") {
    if (!tuple_issues) {
      auto dpin = node.get_sink_pin("tuple_name").get_driver_pin();
      collapse_forward_for_pin(node, dpin);
    }
    return true;
  }

  if (!Lgtuple::is_root_attribute(key_name)) {
    Pass::info("tuple_get {} for key:{} has no defined tuple (may be OK)", node.debug_name(), key_name);
    return false;
  }

  node.set_type(Ntype_op::AttrGet);
  I(node.get_sink_pin("field").get_driver_pin().get_type_const().to_string() == key_name);

  return true;
}

bool Cprop::process_mux(Node &node, XEdge_iterator &inp_edges_ordered) {

  if (tuple_issues) {
    return false;
  }

  std::vector<std::shared_ptr<Lgtuple const>> tup_list;
  tup_list.resize(inp_edges_ordered.size()-1);

  Node_pin &sel_dpin = inp_edges_ordered[0].driver;

  bool some_tuple_found = false;
  bool some_pending     = false;
  for(auto i=1u;i<inp_edges_ordered.size();++i) {
    auto tup = find_lgtuple(inp_edges_ordered[i].driver);
    tup_list[i-1] = tup;
    if (tup)
      some_tuple_found = true;
    else
      some_pending = true;
  }

  if (!some_tuple_found)
    return false; // nothing to do

  if (some_pending) {
    for(auto i=1u;i<inp_edges_ordered.size();++i) {
      if (tup_list[i-1])
        continue;
      auto tup = std::make_shared<Lgtuple>(""); // scalar
      tup->add(inp_edges_ordered[i].driver);
      tup_list[i-1] = tup;
    }
  }

  auto tup = Lgtuple::make_mux(node, sel_dpin, tup_list);
  if (!tup) {
    return true;  // It was a scalar entry, no need to create tuple
  }

  node2tuple[node.get_compact()] = tup;

  return true; //make_mux can update the node pins
}

void Cprop::process_sext(Node &node, XEdge_iterator &inp_edges_ordered) {
  const auto &pos_dpin = inp_edges_ordered[1].driver;
  if (!pos_dpin.is_type_const()) {
    return;  // not much to do
  }

  const auto &wire_dpin = inp_edges_ordered[0].driver;
  if (wire_dpin.get_type_op() != Ntype_op::Sext)
    return;

  // Sext(Sext(X,a),b) == Sext(X, min(a,b))

  auto parent_pos_dpin = wire_dpin.get_node().get_sink_pin("b").get_driver_pin();
  if (!parent_pos_dpin.is_type_const())
    return;

  auto self_pos   = pos_dpin.get_type_const().to_i();
  auto parent_pos = parent_pos_dpin.get_type_const().to_i();

  auto b = std::min(self_pos, parent_pos);
  if (b != self_pos) {
    auto new_const_node = node.create_const(b);
    inp_edges_ordered[1].del_edge();
    node.setup_sink_pin("b").connect_driver(new_const_node);
  }

  auto parent_wire_dpin = wire_dpin.get_node().get_sink_pin("a").get_driver_pin();
  inp_edges_ordered[0].del_edge();
  node.setup_sink_pin("a").connect_driver(parent_wire_dpin);
}

std::shared_ptr<Lgtuple const> Cprop::find_lgtuple(Node_pin up_dpin) const {
  auto up_node = up_dpin.get_node();

  auto ptup_it = node2tuple.find(up_node.get_compact());
  if (ptup_it != node2tuple.end()) {
    I(!up_node.is_type_const());
    return ptup_it->second;
  }

  return nullptr;
}

std::shared_ptr<Lgtuple const> Cprop::find_lgtuple(Node up_node) const {
  auto ptup_it = node2tuple.find(up_node.get_compact());
  if (ptup_it != node2tuple.end()) {
    I(!up_node.is_type_const());
    return ptup_it->second;
  }

  return nullptr;
}

void Cprop::process_attr_set(Node &node) {
  if (tuple_issues || hier) // hier still not supported
    return;

  // Not much to do, just check for compile error in cases like foo := xxx (were xxx is a tuple)

  auto field_spin = node.get_sink_pin("field");
  if (!field_spin.is_connected()) {
    I(node.is_sink_connected("chain"));  // must be a chain attrset
    return;
  }

  auto field_txt = field_spin.get_driver_pin().get_type_const().to_string();
  I(Lgtuple::is_root_attribute(field_txt)); // AttrSet is only for root fields

  auto attr_field = Lgtuple::get_last_level(field_txt);
  if (attr_field != "__dp_assign")
    return;

  auto value_spin = node.get_sink_pin("value");
  if (!value_spin.is_connected()) {
    node.dump();
    Pass::error("node:{} has := assign without rhs value to assign", node.debug_name());
    return;
  }

  auto name_spin = node.get_sink_pin("name");
  std::shared_ptr<Lgtuple> node_tup;
  if (name_spin.is_connected()) {
    auto name_tup = find_lgtuple(name_spin.get_driver_node());
    if (name_tup) {
      if ( !name_tup->is_scalar()) {
        name_tup->dump();
        node.dump();
        Pass::error("node:{} has := assign with a tuple in lhs, only scalars allowed", node.debug_name());
        return;
      }
      fmt::print("DEBUG2\n");
      node_tup = std::make_shared<Lgtuple>(name_tup->get_name());
      node_tup->add(node.get_driver_pin("Y"));
      for (const auto &e:name_tup->get_map()) {
        if (Lgtuple::is_root_attribute(e.first)) {
          node_tup->add(e.first, e.second);
        }
      }
    }
  }

  fmt::print("DEBUG3\n");
  auto value_tup = find_lgtuple(value_spin.get_driver_node());
  if (!value_tup) {
    if (node_tup) {
      node2tuple[node.get_compact()] = node_tup;
    }
    return; // nothing to propagate
  }
  fmt::print("DEBUG4\n");

  if (!value_tup->is_scalar()) {
    value_tup->dump();
    node.dump();
    Pass::error("node:{} has := assign with a tuple in rhs, only scalars allowed", node.debug_name());
    return;
  }

  // propagate lgtuple, but strip all the "Bitwidth" fields
  for (const auto &e:value_tup->get_map()) {
    // Add update any attr but not the BW fields
    if (Lgtuple::is_attribute(e.first)) {
      auto attr = Lgtuple::get_last_level(e.first);
      if (attr == "__max" || attr == "__min" || attr == "__sbits" || attr == "__ubits")
        continue;
    }
    if (!node_tup) {
      node_tup = std::make_shared<Lgtuple>(value_tup->get_name());
      node_tup->add(node.get_driver_pin("Y"));
    }
    node_tup->add(e.first, e.second);
  }

  if (node_tup) {
    node2tuple[node.get_compact()] = node_tup;
  }
}

void Cprop::process_tuple_add(Node &node) {
  auto [tup_name, key_name]    = get_tuple_name_key(node);

  auto [value_dpin, value_tup] = get_value(node);

  bool                           parent_is_a_sub = false;
  Node_pin                       parent_dpin;
  std::shared_ptr<Lgtuple const> parent_tup;
  {
    auto parent_spin = node.get_sink_pin("tuple_name");
    if (parent_spin.is_connected()) {
      parent_dpin     = parent_spin.get_driver_pin();
      parent_tup      = find_lgtuple(parent_dpin);
      parent_is_a_sub = parent_dpin.get_type_op() == Ntype_op::Sub;
    }
  }

  const bool has_key           = !key_name.empty();
  const bool has_parent_scalar = !parent_dpin.is_invalid() && parent_tup == nullptr;
  const bool has_parent_tup    = parent_tup != nullptr;
  const bool has_value_scalar  = !value_dpin.is_invalid() && value_tup == nullptr;
  const bool has_value_tup     = value_tup != nullptr;

  std::shared_ptr<Lgtuple>       node_tup;

  if (has_key) {
    // When key is provided it is mostly variations of tuple add

    auto v = (has_parent_tup? 0x4:0) + (has_parent_scalar?0x2:0) + (has_value_scalar?0x1:0);

    switch(v) {
      case 0x0: {
        if (!has_value_tup) {
          if (!tuple_issues) {
            node.dump();
            Pass::error("tuple:{} with key:{} no scalar or parent value", node.debug_name(), key_name);
            // NOTE: this could be an empty tuple. Do we want to support this case? Why?
          }
          return;
        }
        node_tup = std::make_shared<Lgtuple>(tup_name);
        node_tup->add(key_name, value_tup);
      }
      break;
      case 0x1: {
        node_tup = std::make_shared<Lgtuple>(tup_name);
        node_tup->add(key_name, value_dpin);
      }
      break;
      case 0x2: {
        if (!has_value_tup) {
          if (!tuple_issues) {
            node.dump();
            Pass::error("tuple:{} with key:{} parent value and no scalar (connect to value)", node.debug_name(), key_name);
          }
          return;
        }
        node_tup = std::make_shared<Lgtuple>(tup_name);
        node_tup->add(key_name, value_tup);
        node_tup->concat(parent_dpin); // Do we generate this case?
      }
      break;
      case 0x3: {
        node_tup = std::make_shared<Lgtuple>(tup_name);
        node_tup->add(parent_dpin);
        node_tup->add(key_name, value_dpin);
      }
      break;
      case 0x4: {
        node_tup = std::make_shared<Lgtuple>(*parent_tup);
        node_tup->add(key_name, value_tup);
      }
      break;
      case 0x5: {
        node_tup = std::make_shared<Lgtuple>(*parent_tup);
        node_tup->add(key_name, value_dpin);
      }
      break;
      default:
        I(false); // impossible cases
    }
  }else{
    // When key is NOT provided it is mostly variations of tuple concat

    if (has_parent_tup) {
      node_tup = std::make_shared<Lgtuple>(*parent_tup);
    }else if (parent_is_a_sub) {

      node_tup = std::make_shared<Lgtuple>(tup_name);

      auto parent_node = node.get_sink_pin("tuple_name").get_driver_node();
      I(parent_node.is_type_sub());

      const auto &sub = parent_node.get_type_sub_node();
      for (const auto *io_pin : sub.get_output_pins()) {
        auto sub_dpin = parent_node.get_driver_pin(io_pin->name);
        if (io_pin->has_io_pos()) {
          auto io_name = absl::StrCat(":", std::to_string(io_pin->get_io_pos()), ":", io_pin->name);
          node_tup->add(io_name, sub_dpin);
        } else {
          node_tup->add(io_pin->name, sub_dpin);
        }
      }
    }else{
      node_tup = std::make_shared<Lgtuple>(tup_name);
    }

    if (has_value_scalar) {
      node_tup->concat(value_dpin);
    }
    if (has_value_tup) {
      node_tup->concat(value_tup);
    }
  }

  node2tuple[node.get_compact()] = node_tup;

  // post handling for graph outputs, sub-graph and register.
  if (hier || tuple_issues)
    return;

  //bool in_tuple_add_chain = false;
  XEdge_iterator pending_out_edges;
  for (auto &e : node.out_edges()) {
    auto sink_type = e.sink.get_type_op();
    if (e.sink.is_graph_output() && e.sink.get_pin_name() == "%") {
      try_create_graph_output(node, node_tup);  // first add outputs
      return;
    } else if (e.sink.get_node().is_type_sub_present() && e.sink.get_pin_name() == "$") {
      auto sub_node = e.sink.get_node();
      try_connect_tuple_to_sub(e.sink, node_tup, sub_node, node);
      return;
    } else if (sink_type == Ntype_op::TupAdd || sink_type == Ntype_op::Mux || sink_type == Ntype_op::TupGet) {
    } else {
      pending_out_edges.emplace_back(e);
    }
  }

  if (!pending_out_edges.empty() && node_tup->is_trivial_scalar()) {
    expand_data_and_attributes(node, "", pending_out_edges, node_tup);
  }
}

Node_pin Cprop::expand_data_and_attributes(Node &node, const std::string &key_name, XEdge_iterator &pending_out_edges, std::shared_ptr<Lgtuple const> node_tup) {
  I(!hier);
  I(!tuple_issues);
  I(node_tup);
  I(node_tup->is_scalar());

  if (pending_out_edges.empty())
    return invalid_pin;

  auto value_dpin = node_tup->get_dpin(key_name);

  bool added_chain = false;

  for (auto it : node_tup->get_level_attributes(key_name)) {
    I(Lgtuple::is_attribute(it.first));
    if (Ntype::is_valid_sink(Ntype_op::Flop, it.first.substr(2)))
      continue; // Do not create attr for flop config (handled in cprop directly)

    added_chain = true;
    auto attr_node = node.create(Ntype_op::AttrSet);
    auto an_spin   = attr_node.setup_sink_pin("name");
    auto af_spin   = attr_node.setup_sink_pin("field");
    auto av_spin   = attr_node.setup_sink_pin("value");

    auto attr_key_node = node.create_const(Lconst::string(it.first));
    auto attr_key_dpin = attr_key_node.setup_driver_pin();
    attr_key_dpin.connect_sink(af_spin);

    av_spin.connect_driver(it.second);
    if (!value_dpin.is_invalid()) // weird but valid case of just attributes no data
      an_spin.connect_driver(value_dpin);

    value_dpin = attr_node.setup_driver_pin("Y"); // to chain all the attributes
    value_dpin.set_name(node_tup->get_name());
  }

  if (value_dpin.is_invalid()) {
    node_tup->dump();
    Pass::error("tuple could not find key:{} or any attribute", key_name);
    return invalid_pin;
  }

  for(auto &e:pending_out_edges) {
    auto sink_node = e.sink.get_node();
    if (sink_node.is_type(Ntype_op::AttrSet) && sink_node.get_driver_pin("Y").get_name() == node_tup->get_name())
      continue; // already connected attribute field
    value_dpin.connect_sink(e.sink);
    e.del_edge();
  }

  if (added_chain) {
    node2tuple[value_dpin.get_node().get_compact()] = node_tup;
  }

  return value_dpin;
}

void Cprop::do_trans(Lgraph *lg) {
  Lbench b("pass.cprop");

  {
    tuple_issues = false;
    auto lgit = lg->forward();
    for (auto fwd_it = lgit.begin(); fwd_it != lgit.end(); ++fwd_it) {
      auto node = *fwd_it;
      fmt::print("fwd node:{}\n", node.debug_name());

      auto op                = node.get_type_op();
      auto inp_edges_ordered = node.inp_edges_ordered();

      // Special cases to handle in cprop
      if (op == Ntype_op::AttrGet) {
        continue;
      } else if (op == Ntype_op::AttrSet) {
        process_attr_set(node);
        continue;
      } else if (op == Ntype_op::Sub) {
        process_subgraph(node, inp_edges_ordered);
        continue;
      } else if (op == Ntype_op::Flop) {
        process_flop(node);
        continue;
      } else if (op == Ntype_op::Latch || op == Ntype_op::Fflop || op == Ntype_op::Memory) {
#ifndef NDEBUG
        fmt::print("cprop skipping node:{}\n", node.debug_name());
#endif
        // FIXME: if flop feeds itself (no update, delete, replace for zero)
        // FIXME: if flop is disconnected *after AttrGet processed*, the flop was not used. Delete
        continue;
      } else if (op == Ntype_op::TupAdd) {
        process_tuple_add(node);
        continue;
      } else if (op == Ntype_op::TupGet) {
        auto ok = process_tuple_get(node);
        if (!ok) {
          if (!tuple_issues)
            Pass::info("cprop could not simplify node:{}", node.debug_name());
          tuple_issues = true;
        }
        continue;
      } else if (op == Ntype_op::Sext) {
        process_sext(node, inp_edges_ordered);
      } else if (op == Ntype_op::Mux) {
        bool updated = process_mux(node, inp_edges_ordered);
        if (updated)
          inp_edges_ordered = node.inp_edges_ordered();
      } else if (!node.has_outputs()) {  // This must be after Mux/Tup because those can insert a flop
        node.del_node();
        continue;
      }

      auto replaced_some = try_constant_prop(node, inp_edges_ordered);

      if (node.is_invalid())
        continue;  // It got deleted

      if (replaced_some) {
        inp_edges_ordered = node.inp_edges_ordered();
      }

      if (op == Ntype_op::Or) {
        // (1) don't cprop the Or at the first cprop
        // (2) and clean the dont_touch table for the second cprop to collapse
        if (dont_touch.find(node.get_compact()) != dont_touch.end()) {
          dont_touch.erase(node.get_compact());
          continue;
        }
      }

      try_collapse_forward(node, inp_edges_ordered);
    }
  }

#ifdef TRIVIAL_CPROP
  //return;
#endif

  // connect register q_pin to sinks
  for (const auto &[reg_name, sink_pins] : reg_name2sink_pins) {
    Node_pin q_pin;
    auto     it = reg_name2qpin.find(reg_name);
    if (it == reg_name2qpin.end()) {
      // I(false);
      break;  // maybe next cprop iteration could solve
    }

    if (sink_pins.empty())
      continue;

    q_pin = it->second;

    for (auto sink_pin : sink_pins) {
      if (sink_pin.is_invalid())
        continue;
      q_pin.connect_sink(sink_pin);
    }
  }

  Node_pin clock_pin;
  Node_pin reset_pin;

  // tuple chain clean up
  for (auto node : lg->fast()) {
    if (!tuple_issues) {
      if (node.is_type_tup()) {
        if (hier) {
          auto it = node2tuple.find(node.get_compact());
          if (it != node2tuple.end()) {
            node2tuple.erase(it);
          }
        }
        if (node.is_type(Ntype_op::TupAdd)) {
          // Some tupleAdd should be converted to AttrSet
          auto pos_spin = node.get_sink_pin("position");
          if (!pos_spin.is_invalid()) {
            auto pos_dpin = pos_spin.get_driver_pin();
            if (pos_dpin.is_type_const()) {
              auto field = pos_dpin.get_type_const().to_string();
              if (Lgtuple::is_root_attribute(field)) {
                if (!Ntype::is_valid_sink(Ntype_op::Flop, field.substr(2))) {
                  node.set_type(Ntype_op::AttrSet);
                  continue;
                }
              }
            }
          }
        }

        node.del_node();
        continue;
      } else if (node.is_type_flop()) {
        {
          auto spin_clock = node.setup_sink_pin("clock");
          if (!spin_clock.is_connected()) {
            if (clock_pin.is_invalid()) {
              if (lg->has_graph_input("clock")) {
                clock_pin = lg->get_graph_input("clock");
              } else {
                clock_pin = lg->add_graph_input("clock", Port_invalid, 1);
              }
            }
            spin_clock.connect_driver(clock_pin);
          }
        }
        {
          auto spin_reset = node.setup_sink_pin("reset");
          if (!spin_reset.is_connected()) {
            if (reset_pin.is_invalid()) {
              if (lg->has_graph_input("reset")) {
                reset_pin = lg->get_graph_input("reset");
              } else {
                reset_pin = lg->add_graph_input("reset", Port_invalid, 1);
              }
            }
            spin_reset.connect_driver(reset_pin);
          }
        }
        continue;
      }
    } // end of (!tuple_issues)

    if (!node.has_outputs()) {
      auto op = node.get_type_op();
      if (op != Ntype_op::Flop && op != Ntype_op::Latch && op != Ntype_op::Fflop && op != Ntype_op::Memory && op != Ntype_op::Sub) {
        //&& op != Ntype_op::AttrSet) {
        // TODO: del_dead_end_nodes(); It can propagate back and keep deleting
        // nodes until it reaches a SubGraph or a driver_pin that has some
        // other outputs. Doing this dead_end_nodes delete iterator can retuce
        // the number of times that cprop needs to be called for deep chains.
        node.del_node();
      }
      continue;
    }
  } // end of lg->fast()

  if (!hier) {
    node2tuple.clear();
  }
 
  if (!tuple_issues && (!hier || at_gioc)) {
    // remove unified input $ if fully resolved
    if (lg->has_graph_input("$")) {
      auto unified_inp = lg->get_graph_input("$");
      if (!unified_inp.has_outputs()) {
        unified_inp.get_non_hierarchical().del();
      }
    }

    // remove unified output % if fully resolved
    if (lg->has_graph_output("%")) {
      auto unified_out = lg->get_graph_output("%");
      if (!unified_out.has_inputs()) {
        unified_out.get_non_hierarchical().del();
      }
    }
  }
} // end of do_trans()

void Cprop::try_create_graph_output(Node &node, std::shared_ptr<Lgtuple> tup) {
  I(!hier);
  I(!tuple_issues);

  auto *lg          = node.get_class_lgraph();
  bool  local_error = false;
  for (const auto &it : tup->get_map()) {
    std::string_view out_name{it.first};
    if (unlikely(out_name.empty())) {
      local_error = true;
      Pass::info("Tuple {} for graph {} without named field (pyrope supports unnamed)", tup->get_name(), lg->get_name());
      continue;
    }
    if (unlikely(it.second.is_invalid())) {
      local_error = true;
      Pass::error("graph {} has output but it has invalid field {}", lg->get_name(), it.first);
      continue;
    }
    if (lg->has_graph_output(out_name))
      continue;

    if (Lgtuple::is_attribute(out_name)) {
      I(Lgtuple::get_last_level(out_name) != "__dp_assign");  // __dp_assign should not create a tuple
      continue;
    }

    Port_ID x   = Port_invalid;
    auto    pos = Lgtuple::get_first_level_pos(out_name);
    if (pos >= 0) {
      x = pos;
    }
    auto flattened_gout = lg->add_graph_output(out_name, x, 0);
    it.second.connect_sink(flattened_gout);
    I(!lg->get_graph_output(out_name).is_invalid());
  }

  if (!local_error) {
    bwd_del_node(node);  // then delete current tup_add

    auto dpin = lg->get_graph_output("%");  // then delete anything left at %
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

  I(!Ntype::is_loop_last(node.get_type_op()));

  absl::flat_hash_set<Node::Compact> potential_set;
  std::deque<Node>                   potential;

  for (auto e : node.inp_edges()) {
    // if (potential_set.contains(node.get_compact()))
    if (potential_set.contains(e.driver.get_node().get_compact()))
      continue;
    potential.emplace_back(e.driver.get_node());
    // potential_set.insert(node.get_compact());
    potential_set.insert(e.driver.get_node().get_compact());
  }

  node.del_node();

  while (!potential.empty()) {
    auto n = potential.front();
    potential.pop_front();

    I(!n.is_invalid());

    if (!n.is_type_loop_last() && !n.has_outputs()) {
      for (auto e : n.inp_edges()) {
        if (e.driver.is_graph_io())
          continue;
        auto d_node = e.driver.get_node();
        if (potential_set.contains(d_node.get_compact()))
          continue;
        potential.emplace_back(e.driver.get_node());
        potential_set.insert(d_node.get_compact());
      }
      n.del_node();
    }
  }
}

void Cprop::dump_node2tuples() const {
  for (const auto &it : node2tuple) {
#ifndef NDEBUG
    fmt::print("node nid:{}\n", it.first.get_nid());
#endif
    it.second->dump();
  }
}
