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

Cprop::Cprop(bool _hier, bool _at_gioc) : hier(_hier), at_gioc(_at_gioc) {}

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
      a_pin = node.get_class_lgraph()->create_node_const(0).get_driver_pin();
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
        auto new_node = node.get_class_lgraph()->create_node_const(result);
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
#if 0
    Bits_t max_bits = 0;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (c.get_bits() > max_bits)
        max_bits = c.get_bits();
    }
#endif
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

void Cprop::replace_logic_node(Node &node, const Lconst &result) {
  Node_pin dpin_0;

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
    // node.del_node();
#ifndef NDEBUG
    fmt::print("delete flop\n");
#endif
    return;
  }

  auto din_node = node.get_sink_pin("din").get_driver_node();

  auto din_it = node2tuple.find(din_node.get_compact());
  if (din_it == node2tuple.end()) {
    auto op = din_node.get_type_op();
    if (din_node.is_type_tup() || op == Ntype_op::Mux || op == Ntype_op::Flop) {  // TODO: Any node that could generate a LGTUPLE
      // Not done. 2nd pass needed
      if (flop_needs_2nd_iteration && !tuple_issues) {
        fmt::print("2nd iteration flop:{} still did not have tuple (this may be OK)\n", node.debug_name().c_str());
        return;
      }
      if (!tuple_issues) {
        flop_needs_2nd_iteration = true;
        tuple_issues             = true;
      }
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
  std::string tup_name;

  if (node.is_sink_connected("tuple_name")) {
    auto dpin = node.get_sink_pin("tuple_name").get_driver_pin();
    if (dpin.is_type_const()) {
      auto v   = dpin.get_node().get_type_const();
      tup_name = v.to_string();
    } else if (dpin.has_name()) {
      tup_name = dpin.get_name();
    }
  }

  std::string key_name;

  // FIXME: We can get rid of the "position" pin, Use the :num:label
  if (node.is_sink_connected("position")) {
    auto node2 = node.get_sink_pin("position").get_driver_node();
    if (node2.is_type_const()) {
      key_name = node2.get_type_const().to_string();
    }
    const auto node2_it = node2tuple.find(node2.get_compact());
    if (node2_it != node2tuple.end() && node2_it->second->is_scalar()) {
      const auto &d2 = node2_it->second->get_dpin();
      if (!d2.is_invalid() && d2.is_type_const()) {
        key_name = d2.get_node().get_type_const().to_string();
      }
    }
  }

  return std::make_tuple(tup_name, key_name);
}

void Cprop::split_hier_name(std::string_view full_name, std::vector<std::string_view> &subnames) {
  auto start = 0u;
  auto end   = full_name.find('.');
  while (end != std::string_view::npos) {
    std::string_view token = full_name.substr(start, end - start);
    subnames.emplace_back(token);
    start = end + 1;  //
    end   = full_name.find('.', start);
  }
  std::string_view token = full_name.substr(start, end - start);
  subnames.emplace_back(token);
}

bool Cprop::reg_q_pin_access_preparation(Node &tg_parent_node, Node_pin &ori_tgq_dpin) {
  auto        cur_node = tg_parent_node;
  std::string hier_reg_name;
  while (true) {
    Node_pin parent_dpin;
    if (cur_node.is_type_tup()) {
      auto [tup_name, key_name] = get_tuple_name_key(cur_node);
      if (hier_reg_name.empty()) {
        hier_reg_name = key_name;
      } else {
        hier_reg_name = absl::StrCat(key_name, ".", hier_reg_name);
      }

      parent_dpin      = cur_node.get_sink_pin("tuple_name").get_driver_pin();
      auto parent_node = parent_dpin.get_node();
      auto ptype       = parent_node.get_type_op();
      if (ptype == Ntype_op::TupGet) {
        cur_node = parent_node;
        continue;
      }

      if (ptype != Ntype_op::TupAdd && ptype != Ntype_op::Mux && ptype != Ntype_op::AttrSet)
        return false;
    } else {
      parent_dpin = cur_node.get_driver_pin();
    }

    I(parent_dpin.has_name());
    auto reg_root_ssa_name = parent_dpin.get_name();
    auto pos               = reg_root_ssa_name.find_last_of("_");
    auto reg_root_name     = reg_root_ssa_name.substr(0, pos);
    if (hier_reg_name.empty())
      hier_reg_name = reg_root_name;
    else
      hier_reg_name = absl::StrCat(reg_root_name, ".", hier_reg_name);

    // hier_reg_name collection finished!
    for (auto &e : ori_tgq_dpin.out_edges()) {
      auto sink_node  = e.sink.get_node();
      auto sink_ntype = sink_node.get_type_op();
      if (sink_ntype == Ntype_op::TupAdd && e.sink.get_pin_name() == "value") {
        // note: sometimes the tg(__q_pin) will drive value_dpin of graph output TA chain,
        // but this % TA chain will be deleted before the registers is created, so suddenly
        // the TA sink_pin("value") will disappear. Here I insert an dummy assignment node to
        // make sure there will always a valid spin for the future reg_q_pin to connect to.
        auto new_asg_node = cur_node.get_class_lgraph()->create_node(Ntype_op::Or);
        auto asg_dpin     = new_asg_node.setup_driver_pin();
        asg_dpin.connect_sink(e.sink);
        auto asg_spin = new_asg_node.setup_sink_pin("A");
        reg_name2sink_pins[hier_reg_name].emplace_back(asg_spin);
        e.del_edge();
      } else if (sink_ntype == Ntype_op::Or && sink_node.inp_edges().size() == 1) {
        // originally, if it's an assignment_OR driven by TG(__q_pin), it will be deleted by cprop,
        // and it will cause the sink_pin floating. Here I put the asg_OR to dont_touch table at the first
        // cprop. This dont_touch table only needed at the first cprop as at the time of second cprop,
        // the TG(__q_pin) should be resolved and th asg_or could be cprop-ed at that time.
        dont_touch.insert(sink_node.get_compact());
        reg_name2sink_pins[hier_reg_name].emplace_back(e.sink);
      } else {
        reg_name2sink_pins[hier_reg_name].emplace_back(e.sink);
      }
    }
    return true;
  }
  return true;
}

bool Cprop::process_tuple_get(Node &node, XEdge_iterator &inp_edges_ordered) {
  I(node.get_type_op() == Ntype_op::TupGet);
  auto parent_dpin          = node.get_sink_pin("tuple_name").get_driver_pin();
  auto parent_node          = parent_dpin.get_node();
  auto [tup_name, key_name] = get_tuple_name_key(node);
  if (key_name == "__q_pin") {
    auto tg_dpin = node.setup_driver_pin();
    return reg_q_pin_access_preparation(parent_node, tg_dpin);  // start from parent tg to collect the hierarchical reg name
  }

#if 0
  // this attr comes from tail of TG chain where the TG tail has been transformed into an AttrSet node.
  if (parent_node.get_type_op() == Ntype_op::AttrSet) {
    auto attr_val_dpin = parent_node.get_sink_pin("value").get_driver_pin();
    collapse_forward_for_pin(node, attr_val_dpin);
    return true;
  }
#endif

  // note: if child is TG(__q_pin), don't change cur_tg into AttrSet
  bool child_is_tg_qpin_fetch = false;
  for (auto &e : node.out_edges()) {
    auto sink_node = e.sink.get_node();
    if (sink_node.get_type_op() == Ntype_op::TupGet) {
      auto [child_tup_name, child_key_name] = get_tuple_name_key(sink_node);
      if (child_key_name == "__q_pin")
        child_is_tg_qpin_fetch = true;
    }
  }

  auto ptup_it = node2tuple.find(parent_node.get_compact());
  if (ptup_it == node2tuple.end()) {
    if (tuple_issues) {
      return false;
    }
    if (!parent_dpin.is_invalid() && !child_is_tg_qpin_fetch) {
      collapse_forward_for_pin(node, parent_dpin);
      return true;
    }

    auto wname = absl::StrCat(tup_name, ".", key_name);

    auto dpin = Node_pin::find_driver_pin(node.get_class_lgraph(), wname);
    if (!dpin.is_invalid()) {
      auto flop_node = dpin.get_node();
      if (flop_node.is_type_flop()) {
        collapse_forward_for_pin(node, dpin);
        return true;
      }
    }

    Pass::info("tuple_get {} could not decide the field {} (1)", node.debug_name(), key_name);
    return false;
  }

  const auto &node_tup = ptup_it->second;

  if (key_name.empty()) {
    if (node.is_sink_connected("position")) {
      auto field_node = node.get_sink_pin("position").get_driver_node();

      auto fieldtup_it = node2tuple.find(field_node.get_compact());
      if (fieldtup_it != node2tuple.end()) {
        auto sub_tup = node_tup->get_sub_tuple(fieldtup_it->second);
        if (sub_tup) {
          if (sub_tup->is_scalar() && !child_is_tg_qpin_fetch) {
            auto dpin = sub_tup->get_dpin();
            collapse_forward_for_pin(node, dpin);
          } else {
            node2tuple[node.get_compact()] = sub_tup;
          }
          return true;
        }
      }
    }

    Pass::info("tuple_get {} for tuple {} has no way to find field", node.debug_name(), tup_name);
    return false;
  }

  auto val_dpin = node_tup->get_dpin(key_name);
  if (!val_dpin.is_invalid() && !child_is_tg_qpin_fetch) {
    int conta = 0;
    for (auto it : node_tup->get_level_attributes(key_name)) {
      auto attr_key_node = node.get_lg()->create_node(Ntype_op::TupKey);
      auto attr_key_dpin = attr_key_node.setup_driver_pin();
      attr_key_dpin.set_name(it.first);

      if (conta == 0) {
#ifndef NDEBUG
        fmt::print("cprop: changing node:{} to AttrSet node for attr:{} from pin:{}\n",
                   node.debug_name(),
                   it.first,
                   it.second.debug_name());
#endif
        // Reuse current node. First delete input edges
        for (auto &e : inp_edges_ordered) {
          e.del_edge();
        }

        node.set_type(Ntype_op::AttrSet);  // Replace TupGet for AttrSet
        node.setup_sink_pin("name").connect_driver(val_dpin);
        node.setup_sink_pin("field").connect_driver(attr_key_dpin);
        node.setup_sink_pin("value").connect_driver(it.second);
      } else {
        I(false);  // FIXME: TODO handle multiple attr set (create node)
      }
      conta++;
    }

    if (conta == 0) {  // No attributes
      collapse_forward_for_pin(node, val_dpin);
    }
    return true;
  }

  I(!key_name.empty());
  auto sub_tup = node_tup->get_sub_tuple(key_name);
  if (sub_tup) {
    if (sub_tup->is_scalar() && !child_is_tg_qpin_fetch) {
      auto dpin = sub_tup->get_dpin();
      collapse_forward_for_pin(node, dpin);
    } else {
      node2tuple[node.get_compact()] = sub_tup;
    }
    return true;
  }

  if (child_is_tg_qpin_fetch)
    return true;

  Pass::info("tuple_get {} could not decide the field {} (2)", node.debug_name(), key_name);
  return false;  // Could not resolve (maybe compile error, maybe hierarchical needed)
}

void Cprop::process_mux(Node &node, XEdge_iterator &inp_edges_ordered) {
  std::vector<std::shared_ptr<Lgtuple const>> tup_list;

  Node_pin sel_dpin;

  for (auto &e : inp_edges_ordered) {  // Mux needs the edges ordered
    if (e.sink.get_pid() == 0) {
      I(e.sink.get_pin_name() == "0");

      sel_dpin = e.driver;
    } else {
      auto tup = find_lgtuple(e.driver);
      if (tup) {
        tup_list.emplace_back(tup);
      }
    }
  }

  std::shared_ptr<Lgtuple> tup;
  if (!tuple_issues && !tup_list.empty()) {
    if (tup_list.size() == 1) {
      std::string non_attr_key;
      Node_pin    non_attr_dpin;
      if ((tup_list.size() + 1) != inp_edges_ordered.size()) {
        for (auto tup2 : tup_list) {
          // OK not to have all the inputs as tuples if each input is a scalar
          if (!tup2->is_scalar()) {
            tuple_issues = true;
            break;
          }
          for (const auto &e : tup2->get_map()) {
            if (Lgtuple::is_attribute(e.first))
              continue;

            if (non_attr_dpin.is_invalid()) {
              non_attr_key  = e.first;
              non_attr_dpin = e.second;
            } else {
              if (non_attr_key != e.first) {
                tuple_issues = true;
                break;
              }
            }
          }
        }
      }
      if (non_attr_dpin.is_invalid())
        return;  // no tuple to create

      tup = std::make_shared<Lgtuple>(tup_list[0]->get_name());
      tup->add(non_attr_key, non_attr_dpin);
    } else {
      tup = Lgtuple::make_mux(node, sel_dpin, tup_list);
      if (!tup) {
        return;  // It was a scalar entry, no need to create tuple
      }
      // WARNING: new flops are not added to the iterator. Call them now, but
      // not much besides code OPT (they have no tuples for sure)
    }
    node2tuple[node.get_compact()] = tup;
    // tup->dump();
  }

  // create registers if mux is tuple-reg tail
  bool is_tail = true;
  if (tup && !hier && !tuple_issues) {
    for (auto &e : node.out_edges()) {
      auto sink_type = e.sink.get_type_op();
      if (sink_type == Ntype_op::TupAdd || sink_type == Ntype_op::Mux) {
        is_tail = false;
      }
    }

    if (is_tail) {
      try_create_register(node, tup);
    }
  }
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
    auto new_const_node = node.get_class_lgraph()->create_node_const(b);
    inp_edges_ordered[1].del_edge();
    node.setup_sink_pin("b").connect_driver(new_const_node);
  }

  auto parent_wire_dpin = wire_dpin.get_node().get_sink_pin("a").get_driver_pin();
  inp_edges_ordered[0].del_edge();
  node.setup_sink_pin("a").connect_driver(parent_wire_dpin);
}

std::shared_ptr<Lgtuple const> Cprop::find_lgtuple(Node_pin up_dpin) {
  auto up_node = up_dpin.get_node();
  if (up_node.is_type_const())
    return nullptr;

  auto ptup_it = node2tuple.find(up_node.get_compact());
  if (ptup_it == node2tuple.end()) {
    return nullptr;
  }

  I(up_node.get_type_op() == Ntype_op::TupAdd || up_node.get_type_op() == Ntype_op::TupGet
    || up_node.get_type_op() == Ntype_op::Flop || up_node.get_type_op() == Ntype_op::Mux
    || up_node.get_type_op() == Ntype_op::AttrSet);

  return ptup_it->second;
}

void Cprop::process_attr_get(Node &node) {
  if (tuple_issues)
    return;

  auto field_spin = node.get_sink_pin("field");
  I(field_spin.is_connected() && field_spin.get_driver_node().is_type(Ntype_op::TupKey));
  auto field_txt = field_spin.get_driver_pin().get_name();

  if (field_txt.substr(0, 2) == "__")
    return;  // done

  std::shared_ptr<Lgtuple> node_tup;

  auto name_spin = node.get_sink_pin("name");
  if (name_spin.is_connected()) {
    std::shared_ptr<Lgtuple const> name_tup = find_lgtuple(name_spin.get_driver_pin());
    if (name_tup)
      node_tup = std::make_shared<Lgtuple>(*name_tup);
  }

  if (!node_tup)
    return;  // Not much to do (or working on a scalar)

  auto dpin_name = Lgtuple::get_all_but_last_level(field_txt);

  I(name_spin.is_connected());

  auto d = name_spin.get_driver_pin();
  name_spin.del_driver(d);

  Node_pin new_dpin;
  if (!node_tup->has_dpin(dpin_name)) {
    new_dpin = node.get_lg()->create_node_const(0).setup_driver_pin();
  } else {
    new_dpin = node_tup->get_dpin(dpin_name);
  }
  name_spin.connect_driver(new_dpin);

  // NOTE: do not fix field name (may be useful to provide better error/warnings)
}

void Cprop::process_attr_set(Node &node) {
  auto field_spin = node.get_sink_pin("field");
  if (!field_spin.is_connected()) {
    I(node.is_sink_connected("chain"));  // must be a chain attrset
    return;
  }

  I(field_spin.is_connected() && field_spin.get_driver_node().is_type(Ntype_op::TupKey));
  auto field_txt = field_spin.get_driver_pin().get_name();

  if (Lgtuple::is_root_attribute(field_txt)) {
    if (field_txt != "__dp_assign")
      return;  // following stages will have to deal with this
  }

  std::shared_ptr<Lgtuple> node_tup;

  auto name_spin = node.get_sink_pin("name");
  if (name_spin.is_connected()) {
    std::shared_ptr<Lgtuple const> name_tup = find_lgtuple(name_spin.get_driver_pin());
    if (name_tup)
      node_tup = std::make_shared<Lgtuple>(*name_tup);
  }

  if (!node_tup) {
    auto dpin = node.get_driver_pin("Y");
    if (dpin.has_name())
      node_tup = std::make_shared<Lgtuple>(dpin.get_name());
    else
      node_tup = std::make_shared<Lgtuple>("unknown");
  }

  auto attr_field = Lgtuple::get_last_level(field_txt);
  if (attr_field == "__dp_assign") {
    // WARNING: __dp_assign sets itself as driver. Bitwidth expect this.
    // Maybe a nicer way will be to chain here all the attributes and attr set
#ifndef NDEBUG
    for (const auto &it : node_tup->get_map()) {
      if (!Lgtuple::is_attribute(it.first))
        continue;
      fmt::print("dp_assign to {} with attr:{}\n", node_tup->get_name(), it.first);
    }
#endif
    auto no_attr_field = Lgtuple::get_all_but_last_level(field_txt);
    node_tup->add(no_attr_field, node.setup_driver_pin("Y"));
  } else {
    auto value_dpin = node.get_sink_pin("value").get_driver_pin();
    node_tup->add(field_txt, value_dpin);
  }

  node2tuple[node.get_compact()] = node_tup;
}

void Cprop::process_tuple_add(Node &node) {
  auto [tup_name, key_name] = get_tuple_name_key(node);

  Node_pin                       parent_dpin;
  std::shared_ptr<Lgtuple const> parent_tup;
  std::shared_ptr<Lgtuple const> value_tup;
  bool                           parent_is_a_sub = false;
  std::shared_ptr<Lgtuple>       node_tup;

  // Try to get parent tuple or value tuple
  if (node.is_sink_connected("value")) {
    value_tup = find_lgtuple(node.get_sink_pin("value").get_driver_pin());
  }

  if (node.get_sink_pin("tuple_name").is_connected()) {
    parent_dpin     = node.get_sink_pin("tuple_name").get_driver_pin();
    parent_tup      = find_lgtuple(parent_dpin);
    parent_is_a_sub = parent_dpin.get_type_op() == Ntype_op::Sub;
    if (parent_tup) {
      node_tup = std::make_shared<Lgtuple>(*parent_tup);
    }
  }

  if (!node_tup) {
    I(!parent_tup);
    node_tup = std::make_shared<Lgtuple>(tup_name);  // new tuple if not already created
    if (!value_tup && !parent_dpin.is_invalid()) {
      if (key_name.empty())
        node_tup->add("", parent_dpin);  // the chain was a constant
    }
  }

  if (value_tup) {
    if (key_name.empty()) {  // Tuple Concatenation
      bool ok = node_tup->append_tuple(value_tup);
      if (!ok) {
        tuple_issues = true;
        return;
      }
    } else {
      node_tup->add(key_name, value_tup);
    }
  } else if (node.is_sink_connected("value")) {
    auto val_dpin = node.get_sink_pin("value").get_driver_pin();
    I(val_dpin.get_node().get_type_op() != Ntype_op::TupAdd);  // value_tup should be true otherwise
    if (key_name.empty()) {                                    // Tuple Concatenation
      bool ok = node_tup->append_tuple(val_dpin);
      if (!ok) {
        tuple_issues = true;
        return;
      }
    } else {
      node_tup->add(key_name, val_dpin);
    }
  } else if (parent_is_a_sub) {
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
  } else {
    // I(parent_tup);  // tup1 = tup2 can have no sink("value") // FIXME->sh: could have a empty parent_tup if the parent is a just
    // tuple predeclaration before if-else
  }

  node2tuple[node.get_compact()] = node_tup;

  // post handling for graph outputs, sub-graph and register.
  bool is_tail = true;
  if (!hier && !tuple_issues) {
    for (auto &e : node.out_edges()) {
      auto sink_type = e.sink.get_type_op();
      if (e.sink.is_graph_output() && e.sink.get_pin_name() == "%") {
        try_create_graph_output(node, node_tup);  // first add outputs
        return;
      } else if (e.sink.get_node().is_type_sub_present() && e.sink.get_pin_name() == "$") {
        auto sub_node = e.sink.get_node();
        try_connect_tuple_to_sub(e.sink, node_tup, sub_node, node);
        return;
      } else if (sink_type == Ntype_op::TupAdd || sink_type == Ntype_op::Mux) {
        is_tail = false;
      }
    }

    if (is_tail) {
      try_create_register(node, node_tup);
    }
  }
}

void Cprop::try_create_register(Node &node, std::shared_ptr<Lgtuple> tup) {
  I(!tuple_issues);

  auto node_dpin = node.setup_driver_pin();
  if (!node_dpin.has_name())
    return;

  auto reg_root_ssa_name = node_dpin.get_name();
  if (reg_root_ssa_name.substr(0, 1) != "#")
    return;

  auto pos = reg_root_ssa_name.find_last_of("_");
  I(pos != std::string::npos);
  auto reg_root_name = reg_root_ssa_name.substr(0, pos);

  auto *lg = node.get_class_lgraph();

  for (const auto &it : tup->get_map()) {
    if (unlikely(it.second.is_invalid())) {
      Pass::error("node {} try to create register but it has invalid field {}", node.debug_name(), it.first);
      continue;
    }

    if (Lgtuple::is_attribute(it.first))
      continue;

    std::string reg_full_name;
    if (it.first.empty()) {
      reg_full_name = reg_root_name;
    } else {
      reg_full_name = absl::StrCat(reg_root_name, ".", it.first);
    }

    if (Node_pin::find_driver_pin(lg, reg_full_name).is_invalid()) {
      auto reg_node = lg->create_node(Ntype_op::Flop);
      setup_clock(reg_node);
      it.second.connect_sink(reg_node.setup_sink_pin("din"));
      reg_name2qpin.insert_or_assign(reg_full_name, reg_node.setup_driver_pin());
      reg_node.setup_driver_pin().set_name(reg_full_name);
    }
  }

  // connect to Q pin after registers are created
  for (const auto &[reg_full_name, attr] : reg_attr_map) {
    auto it_qpin = reg_name2qpin.find(reg_full_name);
    I(it_qpin != reg_name2qpin.end());
    auto attr_node = lg->create_node(Ntype_op::AttrSet);
    auto an_spin   = attr_node.setup_sink_pin("name");
    auto af_spin   = attr_node.setup_sink_pin("field");
    auto av_spin   = attr_node.setup_sink_pin("value");

    auto attr_key_node = node.get_lg()->create_node(Ntype_op::TupKey);
    auto attr_key_dpin = attr_key_node.setup_driver_pin();
    attr_key_dpin.set_name(attr.first);

    it_qpin->second.connect_sink(an_spin);
    attr_key_dpin.connect_sink(af_spin);
    attr.second.connect_sink(av_spin);
    reg_name2qpin.insert_or_assign(reg_full_name, attr_node.setup_driver_pin("Y"));
    attr_node.setup_driver_pin("Y").set_name(reg_full_name);
  }
}

void Cprop::setup_clock(Node &reg_node) {
  auto *   lg = reg_node.get_class_lgraph();
  Node_pin clk_dpin;
  if (!lg->has_graph_input("clock")) {
    clk_dpin = lg->add_graph_input("clock", Port_invalid, 1);
  } else {
    clk_dpin = lg->get_graph_input("clock");
  }

  auto clk_spin = reg_node.setup_sink_pin("clock");
  lg->add_edge(clk_dpin, clk_spin);
}

void Cprop::do_trans(Lgraph *lg) {
  Lbench b("pass.cprop");
  /* bool tup_get_left = false; */

  int n_iters              = 0;
  flop_needs_2nd_iteration = false;
  do {
    tuple_issues = false;
#if 0
    bool last_tuple_issues = false;
    Node last_node;
#endif
    for (auto node : lg->forward()) {
#if 0
      if (tuple_issues && !last_tuple_issues) {
        if (last_node.is_invalid())
          last_node = node;
        fmt::print("node:{} changed tuple issues\n",last_node.debug_name());
        last_node.dump();
      }
      last_node = node;
      last_tuple_issues = tuple_issues;
#endif

      auto op = node.get_type_op();

      auto inp_edges_ordered = node.inp_edges_ordered();

      // Special cases to handle in cprop
      if (op == Ntype_op::AttrGet) {
        process_attr_get(node);
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
        auto ok = process_tuple_get(node, inp_edges_ordered);
        if (!ok) {
          if (!flop_needs_2nd_iteration && !tuple_issues)
            Pass::info("cprop could not simplify node:{}", node.debug_name());
          tuple_issues = true;
        }
        continue;
      } else if (op == Ntype_op::Sext) {
        process_sext(node, inp_edges_ordered);
      } else if (op == Ntype_op::Mux) {
        process_mux(node, inp_edges_ordered);
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
    ++n_iters;
    if (n_iters >= 2)
      break;
  } while (flop_needs_2nd_iteration);

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
    }

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
  }

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
}

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

  I(!node.is_type_loop_breaker());

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

    if (!n.is_type_loop_breaker() && !n.has_outputs()) {
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
