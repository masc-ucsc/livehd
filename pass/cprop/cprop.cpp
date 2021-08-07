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

Cprop::Cprop(bool _hier) : hier(_hier), tuple_found(false) {}

std::tuple<Node_pin, std::shared_ptr<Lgtuple const>> Cprop::get_value(const Node &node) const {
  I(node.is_type(Ntype_op::TupAdd) || node.is_type(Ntype_op::AttrSet));

  auto value_spin = node.get_sink_pin("value");
  if (value_spin.is_connected()) {
    auto value_dpin = value_spin.get_driver_pin();
    auto tup        = find_lgtuple(value_dpin);
    return std::make_tuple(value_dpin, tup);
  }

  return std::make_tuple(invalid_pin, nullptr);
}

void Cprop::add_pin_with_check(const std::shared_ptr<Lgtuple> &tup, const mmap_lib::str &key, Node_pin &dpin) {
  tup->add(key, dpin);

  if (likely(!dpin.is_type_tup())) {
    return;
  }

  auto pos_spin = dpin.get_node().get_sink_pin("field");
  I(pos_spin.is_connected());

  auto pos_dpin = pos_spin.get_driver_pin();
  if (pos_dpin.is_type_const()) {
    auto v = pos_dpin.get_type_const().to_field();
    if (!Lgtuple::is_root_attribute(v)) {
      tup->set_issue();
      tuple_issues = true;
    }
  } else {
    tup->set_issue();
    tuple_issues = true;
  }
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

// Collase forward single node
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
    for (auto &e : inp_edges_ordered) {
      if (e.sink.get_pid() == 0)
        continue;  // ignore selector

      if (e.sink.get_pid() == (sel + 1)) {
        a_pin = e.driver;
        break;
      }
    }

    if (a_pin.is_invalid()) {
      Pass::info("WARNING: mux selector:{} for a disconnected pin in mux. Using zero\n", sel);
      a_pin = node.create_const(0).get_driver_pin();
    }

    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::EQ) {
    // FIXME:
    //
    // 1- eq(X,0) = not(ror(x))
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
  } else if (op == Ntype_op::SRA) {
    auto amt_node = inp_edges_ordered[1].driver.get_node();
    I(amt_node == node.get_sink_pin("b").get_driver_node());

    if (amt_node.is_type_const() && amt_node.get_type_const() == 0) {
      collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
    }
  } else if (op == Ntype_op::SHL) {
    if (inp_edges_ordered.size() == 2) {
      auto amt_node = inp_edges_ordered[1].driver.get_node();
      I(amt_node == node.get_sink_pin("B").get_driver_node());

      if (amt_node.is_type_const() && amt_node.get_type_const() == 0) {
        collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
      }
    } else {
      // res = val << (B.0, B.1, B.2)
      // res = (val <<B.0) | (val<<B.1) | (val<<B.2)
      fmt::print("FIXME: OPT the inputs in B if they are constants\n");
    }
  }
}

void Cprop::replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {
  // simple constant propagation
  auto op = node.get_type_op();
  if (op == Ntype_op::SHL) {
    Lconst val = node.get_sink_pin("a").get_driver_node().get_type_const();

    Lconst result(0);

    bool zero_shifts = true;
    for (auto &amt_dpin : node.get_sink_pin("B").inp_drivers()) {
      Lconst amt = amt_dpin.get_type_const();

      result      = result.or_op(val << amt);
      zero_shifts = false;
    }

    if (zero_shifts)
      replace_node(node, Lconst(-1));
    else
      replace_node(node, result);

  } else if (op == Ntype_op::Ror) {
    Lconst result(0);
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      result = result.ror_op(c);
    }

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
    Lconst result(-1);
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
      eq     = eq && !first.eq_op(c).is_known_false();
    }

    Lconst result(eq ? 1 : 0);

    TRACE(fmt::print("cprop: eq node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Ntype_op::Mux) {
    auto sel_const = inp_edges_ordered[0].driver.get_node().get_type_const();
    I(sel_const.is_i());  // string with ??? in mux? Give me test case to debug

    size_t sel = sel_const.to_i();

    Lconst result;
    for (auto &e : inp_edges_ordered) {
      if (e.sink.get_pid() == 0)
        continue;  // ignore selector

      if (e.sink.get_pid() == (sel + 1)) {
        result = e.driver.get_node().get_type_const();
        break;
      }
    }

    if (result.get_bits() == 0) {
      result = Lconst(0);
      Pass::info("WARNING: mux:{} selector:{} goes for disconnected pin in mux. Using zero\n", node.debug_name(), sel);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Mult) {
    Lconst result(1);
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

void Cprop::try_connect_tuple_to_sub(const std::shared_ptr<Lgtuple const> &tup, Node &sub_node, Node &tup_node) {
  I(sub_node.is_type_sub_present());
  I(tup_node.get_type_op() == Ntype_op::TupAdd);

  const auto &sub = sub_node.get_type_sub_node();

  for (auto &it : sub.get_input_pins()) {
    if (it.first->name == "$") {
      continue;
    }

    if (tup->has_dpin(it.first->name)) {
      auto sub_spin = sub_node.setup_sink_pin_raw(it.second);
      if (!sub_spin.is_connected()) {
        XEdge_iterator out_edges;  // Empty list
        auto           dpin = expand_data_and_attributes(tup_node, it.first->name, out_edges, tup);
        I(!dpin.is_invalid());
        sub_spin.connect_driver(dpin);
      }
    } else {
      Pass::info("could not find IO {} in graph {}", it.first->name, sub.get_name());
      // tuple_issues = true; // The tup may be fine, maybe it is just the sub which has issues (unclear)
    }
  }
}

void Cprop::try_connect_lgcpp(const Node &node) {
  const auto &sub = node.get_type_sub_node();
  const auto &reg = Lgcpp_plugin::get_registry();

  auto it = reg.find(sub.get_name());
  if (it == reg.end())
    return;

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

std::tuple<mmap_lib::str, mmap_lib::str> Cprop::get_tuple_name_key(const Node &node) const {
  mmap_lib::str key_name;
  if (node.is_sink_connected("field")) {
    auto node2 = node.get_sink_pin("field").get_driver_node();
    if (node2.is_type_const()) {
      key_name = node2.get_type_const().to_field();
    }
  }

  mmap_lib::str tup_name;
  if (node.setup_driver_pin("Y").has_name()) {
    tup_name = node.setup_driver_pin("Y").get_name();
  } else {
    auto spin = node.get_sink_pin("parent");
    if (spin.is_connected()) {
      auto dpin = spin.get_driver_pin();
      if (dpin.has_name())
        tup_name = dpin.get_name();
    }
  }

  if (tup_name.empty()) {
    tup_name = node.default_instance_name();
  }

  return std::make_tuple(tup_name, key_name);
}

void Cprop::tuple_shl_mut(Node &node) {
  // SHL can have a tuple in B port. E.g: 1<<(2,3). Any data pin gets expanded directly
  auto spin_amount = node.get_sink_pin("B");

  for (auto &dpin : spin_amount.inp_drivers()) {
    auto tup = find_lgtuple(dpin);
    if (tup == nullptr)
      continue;
    if (!tup->is_correct() || tuple_issues)  // conservative in expansion
      continue;

#if 0
    if (tup->is_empty()) {
      auto const_dpin = node.create_const(-1).setup_driver_pin();
      collapse_forward_for_pin(node, const_dpin);
      return;
    }
#endif

    XEdge::del_edge(dpin, spin_amount);
    for (const auto &e : tup->get_map()) {
      if (Lgtuple::is_attribute(e.first))
        continue;

      spin_amount.connect_driver(e.second);
    }
  }
}

void Cprop::tuple_mux_mut(Node &node) {
  auto inp_edges_ordered = node.inp_edges_ordered();

  std::vector<std::shared_ptr<Lgtuple const>> tup_list;
  tup_list.resize(inp_edges_ordered.size() - 1);

  bool        some_tuple_found = false;
  bool        some_pending     = false;
  mmap_lib::str scalar_field;
  for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
    auto tup        = find_lgtuple(inp_edges_ordered[i].driver);
    tup_list[i - 1] = tup;
    if (tup) {
      auto n = tup->get_scalar_name();
      if (!n.empty() && !scalar_field.empty() && n != scalar_field) {
        Pass::info("mux:{} has inputs with different scalar fields {} vs {} (must be dead code eliminated)",
                   node.debug_name(),
                   n,
                   scalar_field);
        return;
      }
      if (!n.empty())
        scalar_field = n;
      some_tuple_found = true;
    } else {
      some_pending = true;
    }
  }

  if (!some_tuple_found)
    return;

  if (some_pending) {
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      if (tup_list[i - 1])
        continue;
      auto tup = std::make_shared<Lgtuple>("");  // scalar
      if (!tuple_issues)
        tup->add(scalar_field, inp_edges_ordered[i].driver);
      tup_list[i - 1] = tup;
    }
  }

  Node_pin &sel_dpin = inp_edges_ordered[0].driver;

  auto [tup, pending_iterations] = Lgtuple::get_mux_tup(tup_list);  // it can handle tuples with issues
  if (tup) {
    node2tuple[node.get_compact()] = tup;
  }

  if (tup == nullptr && tuple_issues)
    return;

  if (pending_iterations) {
    Pass::info("mux:{} has pending iterations to get all inputs as tuple", node.debug_name());
    if (tup) {
      tup->dump();
    }
    tuple_issues = true;
    return;
  }

  if (!tup) {
    if (!tuple_issues)
      tuple_done.insert(node.get_compact());
    return;
  }
  I(tup->is_correct());

  auto cmux_list                 = tup->make_mux(node, sel_dpin, tup_list);
  node2tuple[node.get_compact()] = tup;
  for (auto &cmux : cmux_list) {
    tuple_done.insert(cmux);
  }
}

void Cprop::tuple_flop_mut(Node &node) {
  I(!tuple_done.contains(node.get_compact()));

  if (!node.has_outputs())
    return;

  auto din_spin = node.get_sink_pin("din");
  if (!din_spin.is_connected()) {
    Pass::info("flop:{} has no driving din (legal but strange)", node.debug_name());
    return;
  }

  auto din_node = node.get_sink_pin("din").get_driver_node();

  auto din_it = node2tuple.find(din_node.get_compact());
  if (din_it == node2tuple.end())
    return;

  auto din_tup = din_it->second;

  if (!din_tup->is_correct()) {
    I(tuple_issues);
    din_tup->dump();
    fmt::print("flop:{} tuple:{} has issues (this may be OK with more iterations)\n", node.debug_name(), din_tup->get_name());
    return;
  }

  auto node_it = node2tuple.find(node.get_compact());
  if (node_it != node2tuple.end())
    return;

  auto [flop_tup, pending_iterations] = din_tup->get_flop_tup(node);
  if (!tuple_issues && pending_iterations) {
    Pass::info("flop:{} has pending iterations to get all inputs as tuple", node.debug_name());
    if (flop_tup) {
      flop_tup->dump();
    }
    tuple_issues = true;
  }

  if (flop_tup) {
    node2tuple[node.get_compact()] = flop_tup;
  }
  if (tuple_issues || !flop_tup)
    return;

  flop_tup = din_tup->make_flop(node);
  I(flop_tup);
  node2tuple[node.get_compact()] = flop_tup;

  for (const auto &e : flop_tup->get_map()) {
    if (e.second.is_type_flop()) {
      tuple_done.insert(e.second.get_node().get_compact());
    }
  }
  tuple_issues = true;  // FIXME: trigger next iter, no issues
}

void Cprop::tuple_get_mask_mut(Node &node) {
  // parents can be tuple, nothing to do if not tuple.
  //
  // output is always scalar

  auto a_it   = node2tuple.end();
  auto a_spin = node.get_sink_pin("a");
  if (a_spin.is_connected()) {
    auto a_node = a_spin.get_driver_node();
    a_it        = node2tuple.find(a_node.get_compact());
  }

  auto mask_it   = node2tuple.end();
  auto mask_spin = node.get_sink_pin("mask");
  if (mask_spin.is_connected()) {
    auto mask_node = mask_spin.get_driver_node();
    mask_it        = node2tuple.find(mask_node.get_compact());
  }

  if (a_it == node2tuple.end() && mask_it == node2tuple.end()) {
    return;
  }

  if (tuple_issues) {  // Any tuple issues could be delayed. This always has trivial output
    return;
  }

  //---------------------------------------------
  // Figure out "a" sink

  if (a_it != node2tuple.end()) {
    auto a_tup = a_it->second;
    I(a_tup->is_correct());

    if (a_tup->is_empty()) {
      auto zero_dpin = node.create_const(0).setup_driver_pin();
      collapse_forward_for_pin(node, zero_dpin);
      return;
    }
    auto flat_a_dpin = a_tup->flatten();
    if (flat_a_dpin.is_invalid()) {
      return;
    }

    a_spin.del();
    node.setup_sink_pin("a").connect_driver(flat_a_dpin);
  }

  //---------------------------------------------
  // Figure out "mask" sink
  if (mask_it == node2tuple.end()) {
    return;
  }

  auto mask_tup = mask_it->second;
  if (!mask_tup->is_correct())
    return;

  I(mask_tup->is_correct());
  if (mask_tup->is_empty()) {
    mask_spin.del();
    node.setup_sink_pin("mask").connect_driver(node.create_const(-1));
  } else {
    auto b_dpin = mask_tup->get_dpin("__range_begin");
    auto e_dpin = mask_tup->get_dpin("__range_end");

    Node mask_node;
    if (e_dpin.is_invalid() && b_dpin.is_invalid()) {
      mask_node = node.create_const(-1);
    } else {
      Node b_mask_node;
      if (!b_dpin.is_invalid()) {
        if (b_dpin.is_type_const()) {
          auto v      = Lconst(0) - (Lconst(1) << b_dpin.get_type_const());
          b_mask_node = node.create_const(v);
        } else {
          auto zero_dpin = node.create_const(0).setup_driver_pin();
          auto one_dpin  = node.create_const(1).setup_driver_pin();

          auto shl_node = node.create(Ntype_op::SHL);
          shl_node.setup_sink_pin("a").connect_driver(one_dpin);
          shl_node.setup_sink_pin("b").connect_driver(b_dpin);

          b_mask_node = node.create(Ntype_op::Sum);
          b_mask_node.setup_sink_pin("A").connect_driver(zero_dpin);
          b_mask_node.setup_sink_pin("B").connect_driver(shl_node);
        }
      }

      Node e_mask_node;
      if (!e_dpin.is_invalid()) {
        if (e_dpin.is_type_const()) {
          auto e_val = e_dpin.get_type_const();
          if (e_val > 0)
            e_mask_node = node.create_const((Lconst(2) << e_val) - Lconst(1));  // 2 to avoid <<(e_val+1)
        }
        if (e_mask_node.is_invalid()) {
          // if (end<0) {
          //  tmp = a_driver.__sbits+end
          // }else{
          //  tmp = end
          // }
          // mask = ((1<<(tmp))-1)

          auto zero_dpin = node.create_const(0).setup_driver_pin();
          auto one_dpin  = node.create_const(1).setup_driver_pin();

          auto lt_node = node.create(Ntype_op::LT);
          lt_node.setup_sink_pin("A").connect_driver(e_dpin);
          lt_node.setup_sink_pin("B").connect_driver(zero_dpin);

          auto bits_node = node.create(Ntype_op::AttrGet);
          bits_node.setup_sink_pin("field").connect_driver(node.create_const(Lconst::from_string("__sbits")));
          bits_node.setup_sink_pin("parent").connect_driver(a_spin.get_driver_pin());

          auto tmp_node = node.create(Ntype_op::Sum);
          tmp_node.setup_sink_pin("A").connect_driver(bits_node);
          tmp_node.setup_sink_pin("A").connect_driver(e_dpin);  // e_dpin < 0

          auto mux_node = node.create(Ntype_op::Mux);
          mux_node.setup_sink_pin("0").connect_driver(lt_node);
          mux_node.setup_sink_pin("1").connect_driver(e_dpin);
          mux_node.setup_sink_pin("2").connect_driver(tmp_node);

          auto mux_add_node = node.create(Ntype_op::Sum);
          mux_add_node.setup_sink_pin("A").connect_driver(mux_node);
          mux_add_node.setup_sink_pin("A").connect_driver(node.create_const(1));

          auto shl_node = node.create(Ntype_op::SHL);
          shl_node.setup_sink_pin("a").connect_driver(one_dpin);
          shl_node.setup_sink_pin("b").connect_driver(mux_add_node);

          e_mask_node = node.create(Ntype_op::Sum);
          e_mask_node.setup_sink_pin("A").connect_driver(shl_node);
          e_mask_node.setup_sink_pin("B").connect_driver(one_dpin);
        }
      }
      if (b_mask_node.is_invalid()) {
        I(!e_mask_node.is_invalid());
        mask_node = e_mask_node;
      } else if (e_mask_node.is_invalid()) {
        I(!b_mask_node.is_invalid());
        mask_node = b_mask_node;
      } else {
        mask_node = node.create(Ntype_op::And);
        mask_node.setup_sink_pin("A").connect_driver(b_mask_node);
        mask_node.setup_sink_pin("A").connect_driver(e_mask_node);
      }
    }

    I(!mask_node.is_invalid());
    mask_spin.del();
    node.setup_sink_pin("mask").connect_driver(mask_node);
  }
}

void Cprop::tuple_subgraph(const Node &node) {
  const auto &sub = node.get_type_sub_node();

  auto *sub_lg = node.ref_library()->try_find_lgraph(sub.get_lgid());
  if (sub_lg == nullptr || sub_lg->is_empty()) {
    mmap_lib::str sub_name{sub.get_name()};
    if (sub_name.starts_with("__")) {
      auto cell_name  = sub_name.substr(2);
      auto cell_ntype = Ntype::get_op(cell_name);
      if (cell_ntype != Ntype_op::Invalid) {
        if (cell_ntype == Ntype_op::Sub) {
          node.dump();
          Pass::error("Structural Lgraph does not allow sub graphs as node");
        }
        auto              node_tup = std::make_shared<Lgtuple>(sub_name);
        std::vector<bool> read_map;
        if (cell_ntype == Ntype_op::Memory) {
          auto n_ports         = 0u;
          auto n_rd_ports      = 0u;
          auto node_input_spin = node.get_sink_pin("$");
          if (node_input_spin.is_connected()) {
            auto parent_node = node_input_spin.get_driver_node();
            auto parent_tup  = find_lgtuple(parent_node);
            if (parent_tup) {
              for (const auto &e : parent_tup->get_map()) {
                if (Lgtuple::is_attribute(e.first))
                  continue;

                auto l = Lgtuple::get_first_level_name(e.first).to_lower();
                if (l == "addr") {
                  ++n_ports;
                } else if (l=="rdport") {
                  if (!e.second.is_type_const()) {
                    node_tup->set_issue();
                    continue;  // Maybe later
                  }

                  auto v = e.second.get_type_const();
                  if (!v.is_i()) {
                    Pass::error("Memory {} rdport:{} must be a constant bitmask (1 rd, 0 wr)", node.debug_name(), v.to_pyrope());
                  }
                  if (v.is_known_false()) {
                    read_map.emplace_back(false);
                  } else {
                    read_map.emplace_back(true);
                    ++n_rd_ports;
                  }
                }
              }
            }
          }
          if (n_ports == 0 || n_rd_ports <= 0 || !node_tup->is_correct()) {
            Pass::info("Memory {} still can not figure out ports. (Maybe more iterations)", node.debug_name());
            node_tup->set_issue();
          } else {
            fmt::print("found a memory {} with {} rd ports at ", node.debug_name(), n_rd_ports);
            for (auto i = 0u; i < read_map.size(); ++i) {
              if (read_map[i]) {
                fmt::print(" {}", i);
                node_tup->add(mmap_lib::str(i), node.setup_driver_pin_raw(i));
              }
            }
            fmt::print("\n");
          }
        } else {
          node_tup->add(node.setup_driver_pin_raw(0));
        }
        node2tuple[node.get_compact()] = node_tup;
        return;  // reconnect_sub_as_cell when no issues pending
      }
    }

    // Still a blackbox, not much to do
    for (const auto &e : node.inp_edges()) {
      auto parent_node = e.driver.get_node();
      if (parent_node.is_type_tup() && !node2tuple.contains(e.driver.get_node().get_compact())) {
        tuple_issues = true;
        return;
      }
    }

    try_connect_lgcpp(node);
    return;
  }

  auto it2 = node2tuple.find(node.get_compact());
  if (it2 != node2tuple.end())
    return;

  mmap_lib::str method;
  if (node.has_name()) {
    method = node.get_name();
  } else {
    method = sub.get_name();
  }

  auto node_tup = std::make_shared<Lgtuple>(method);

  for (auto &it : sub.get_output_pins()) {
    auto pin_name{it.first->name};
    if (pin_name.size() > 2 && pin_name.substr(0, 2) == "%.") {
      pin_name = pin_name.substr(2);
    }
    if (it.first->has_io_pos()) {
      auto pos = it.first->get_io_pos();
      pin_name = mmap_lib::str::concat(":", pos, ":", pin_name);
    }
    auto dpin = node.setup_driver_pin_raw(it.second);
    I(dpin == node.get_driver_pin(it.first->name));

    node_tup->add(pin_name, dpin);
  }

  node2tuple[node.get_compact()] = node_tup;
}

void Cprop::tuple_tuple_add(const Node &node) {
  auto [tup_name, key_name]    = get_tuple_name_key(node);
  auto [value_dpin, value_tup] = get_value(node);

  bool                           parent_is_a_sub = false;
  Node_pin                       parent_dpin;
  std::shared_ptr<Lgtuple const> parent_tup;
  {
    auto parent_spin = node.get_sink_pin("parent");
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

  std::shared_ptr<Lgtuple> node_tup;

  if (has_key) {
    // When key is provided it is mostly variations of tuple add

    auto v = (has_parent_tup ? 0x4 : 0) + (has_parent_scalar ? 0x2 : 0) + (has_value_scalar ? 0x1 : 0);

    switch (v) {
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
      } break;
      case 0x1: {
        node_tup = std::make_shared<Lgtuple>(tup_name);
        add_pin_with_check(node_tup, key_name, value_dpin);
      } break;
      case 0x2: {
        if (!has_value_tup) {
          if (!tuple_issues) {
            node.dump();
            Pass::error("tuple:{} with key:{} parent value and no scalar (connect to value)", node.debug_name(), key_name);
          }
          return;
        }
        node_tup = std::make_shared<Lgtuple>(tup_name);
        // node_tup->add(parent_dpin); // Maybe missing tuple field?
        node_tup->add(key_name, value_tup);
      } break;
      case 0x3: {
        node_tup = std::make_shared<Lgtuple>(tup_name);
        add_pin_with_check(node_tup, "0", parent_dpin);
        add_pin_with_check(node_tup, key_name, value_dpin);
      } break;
      case 0x4: {
        node_tup = std::make_shared<Lgtuple>(*parent_tup);
        if (Lgtuple::is_attribute(key_name) && value_tup->is_scalar()) {
          auto v_dpin = value_tup->get_dpin();
          if (v_dpin.is_invalid()) {
            node_tup->set_issue();
            tuple_issues = true;
            Pass::info("node:{} field:{} can not find a non attribute field a on tuple:{}",
                       node.debug_name(),
                       key_name,
                       value_tup->get_name());
          }
          node_tup->add(key_name, v_dpin);
        } else {
          if (Lgtuple::is_attribute(key_name)) {
            node_tup->set_issue();
            tuple_issues = true;
            Pass::info("node:{} attribute:{} can not have a complex sub tuple:{}",
                       node.debug_name(),
                       key_name,
                       value_tup->get_name());
          } else {
            node_tup->add(key_name, value_tup);
          }
        }
      } break;
      case 0x5: {
        node_tup = std::make_shared<Lgtuple>(*parent_tup);
        add_pin_with_check(node_tup, key_name, value_dpin);
      } break;
      default: I(false);  // impossible cases
    }
  } else {
    // When key is NOT provided it is mostly variations of tuple concat

    if (has_parent_tup) {
      node_tup = std::make_shared<Lgtuple>(*parent_tup);
    } else if (parent_is_a_sub) {
      node_tup = std::make_shared<Lgtuple>(tup_name);

      auto parent_node = node.get_sink_pin("parent").get_driver_node();
      I(parent_node.is_type_sub());

      const auto &sub = parent_node.get_type_sub_node();
      for (auto &it : sub.get_output_pins()) {
        auto sub_dpin = parent_node.setup_driver_pin_raw(it.second);
        auto pin_name{it.first->name};
        if (pin_name.size() > 2 && pin_name.substr(0, 2) == "%.") {
          pin_name = pin_name.substr(2);
        }
        if (it.first->has_io_pos()) {
          auto io_name = mmap_lib::str::concat(std::string(":") + std::to_string(it.first->get_io_pos()) + std::string(":"), pin_name);
          node_tup->add(io_name, sub_dpin);
        } else {
          node_tup->add(pin_name, sub_dpin);
        }
      }
    } else {
      node_tup = std::make_shared<Lgtuple>(tup_name);
      if (has_parent_scalar) {
        add_pin_with_check(node_tup, "0", parent_dpin);
      }
    }

    if (has_value_scalar) {
      I(!has_value_tup);
      I(!value_dpin.is_type_tup());
      node_tup->concat(value_dpin);
    } else if (has_value_tup) {
      node_tup->concat(value_tup);
    }
  }

  node2tuple[node.get_compact()] = node_tup;
}

bool Cprop::tuple_tuple_get(const Node &node) {
  auto parent_dpin          = node.get_sink_pin("parent").get_driver_pin();
  auto parent_node          = parent_dpin.get_node();
  auto [tup_name, key_name] = get_tuple_name_key(node);

  std::shared_ptr<Lgtuple const> node_tup;

  auto ptup_it = node2tuple.find(parent_node.get_compact());
  if (ptup_it != node2tuple.end()) {
    node_tup = ptup_it->second;
    if (!node_tup->is_correct()) {
      return false;
    }
  }

  if (key_name.empty()) {
    if (node.is_sink_connected("field") && node_tup) {
      auto field_node  = node.get_sink_pin("field").get_driver_node();
      auto fieldtup_it = node2tuple.find(field_node.get_compact());
      if (fieldtup_it != node2tuple.end()) {
        I(node_tup->is_correct());

        auto sub_tup = node_tup->get_sub_tuple(fieldtup_it->second);
        if (sub_tup) {
          node2tuple[node.get_compact()] = sub_tup;
          return true;
        }
        fieldtup_it->second->dump();
        node.dump();
        Pass::info("FIXME: need to handle runtime tuple index node:{}\n", node.debug_name());
        fieldtup_it->second->set_issue();
        tuple_issues = true;
        return false;
      }
    }
    if (!tuple_issues) {
      Pass::error("tuple_get {} for tuple {} has no way to find field", node.debug_name(), tup_name);
      node_tup->set_issue();  // It is not right
      tuple_issues = true;
    }
    return false;
  }

  if (!node_tup) {
    if (key_name == "0" || parent_dpin.is_type_register()) {
      auto self_tup = std::make_shared<Lgtuple>(tup_name);
      self_tup->add(parent_dpin);
      node2tuple[node.get_compact()] = self_tup;
      return true;
    }

    if (Lgtuple::is_root_attribute(key_name)) {
      // No node2tuple. This node will be converted to AttrGet
      return true;
    }
    Pass::info("tuple_get {} for key:{} has no defined tuple (may be OK)", node.debug_name(), key_name);
    return false;
  }

  I(node_tup->is_correct());

  bool        is_attr_get = false;
  mmap_lib::str main_field{key_name};
  if (Lgtuple::is_attribute(key_name)) {
    main_field  = Lgtuple::get_all_but_last_level(key_name);
    is_attr_get = true;
  }

  auto sub_tup = node_tup->get_sub_tuple(main_field);
  if (!sub_tup) {
    if (main_field == "$") {
      bool dollar_to_subs = true;
      for (auto &e : node.out_edges()) {
        if (e.sink.is_type(Ntype_op::Sub))
          continue;
        dollar_to_subs = false;
        break;
      }
      if (dollar_to_subs) {
        return true;
      }
    }
    node_tup->dump();
    Pass::info("tuple_get {} for key:{} field is not present (may be OK after iterations)", node.debug_name(), main_field);
    tuple_issues = true;
    return false;
  }

  if (sub_tup->has_just_attributes() && !is_attr_get) {
    node_tup->dump();
    Pass::info("tuple_get {} for key:{} just has attributes (may be OK)", node.debug_name(), main_field);
    node2tuple[node.get_compact()] = sub_tup;
    return true;
  }

  if (!is_attr_get) {
    node2tuple[node.get_compact()] = sub_tup;
  }

  return true;
}

void Cprop::tuple_attr_set(const Node &node) {
  if (hier)  // hier still not supported
    return;

  auto self_tup = find_lgtuple(node);
  if (self_tup && self_tup->is_correct())
    return;  // already done

  auto field_spin = node.get_sink_pin("field");
  I(field_spin.is_connected());

  auto attr_field = field_spin.get_driver_pin().get_type_const().to_field();
  I(Lgtuple::is_root_attribute(attr_field));  // AttrSet is only for root fields

  if (attr_field != "__dp_assign")
    return;

  auto value_spin = node.get_sink_pin("value");
  if (unlikely(!value_spin.is_connected())) {
    node.dump();
    Pass::error("node:{} has := assign without rhs value to assign", node.debug_name());
    return;
  }

  auto parent_spin = node.get_sink_pin("parent");
  if (unlikely(!parent_spin.is_connected())) {
    Pass::error("node:{} has := assign does not have prior assign to lhs??", node.debug_name());
  }

  auto parent_tup = find_lgtuple(parent_spin.get_driver_node());
  if (parent_tup == nullptr) {
    node2tuple.erase(node.get_compact());  // erase if exists
    return;
  }

  if (!parent_tup->is_correct()) {
    node2tuple[node.get_compact()] = parent_tup;
    return;
  }

  std::shared_ptr<Lgtuple> node_tup;
  if (parent_tup->is_scalar()) {
    auto node_dpin = node.get_driver_pin();

    node_tup = std::make_shared<Lgtuple>(parent_tup->get_name());
    mmap_lib::str parent_key;
    for (const auto &e : parent_tup->get_map()) {
      if (!Lgtuple::is_attribute(e.first)) {
        parent_key = e.first;
        continue;
      }
      GI(!parent_key.empty(), parent_key == Lgtuple::get_all_but_last_level(e.first));
      parent_key = Lgtuple::get_all_but_last_level(e.first);
      node_tup->add(e.first, e.second);
    }
    if (parent_key.empty())
      node_tup->add(node_dpin);
    else
      node_tup->add(parent_key, node_dpin);
  } else {
    auto value_tup = find_lgtuple(value_spin.get_driver_node());
    if (value_tup) {
      node_tup = parent_tup->create_assign(value_tup);
    } else {
      node_tup = parent_tup->create_assign(value_spin.get_driver_pin());
    }
    I(parent_tup->is_correct());

    if (node_tup == nullptr) {
      parent_tup->dump();
      node.dump();
      Pass::error("node:{} has := assign with a tuple in lhs, and something failed", node.debug_name());
    }
  }

  I(node_tup);

  I(node_tup->is_correct());
  tuple_done.insert(node.get_compact());
  node2tuple[node.get_compact()] = node_tup;
}

bool Cprop::scalar_mux(Node &node, XEdge_iterator &inp_edges_ordered) {
  if (inp_edges_ordered.size() != 3)
    return false;  // Maybe future optimizations

  if (inp_edges_ordered[1].driver == inp_edges_ordered[2].driver) {
    collapse_forward_for_pin(node, inp_edges_ordered[1].driver);
    return true;
  }

  bool false_path_zero = false;
  bool false_path_one  = false;
  if (inp_edges_ordered[1].driver.is_type_const()) {
    auto v          = inp_edges_ordered[1].driver.get_type_const();
    false_path_zero = (v == Lconst(0));
    false_path_one  = (v == Lconst(1));
  }

  bool true_path_zero = false;
  bool true_path_one  = false;
  if (inp_edges_ordered[2].driver.is_type_const()) {
    auto v         = inp_edges_ordered[2].driver.get_type_const();
    true_path_zero = (v == Lconst(0));
    true_path_one  = (v == Lconst(1) || v == Lconst(-1));
  }

  bool false_path_sel = inp_edges_ordered[0].driver == inp_edges_ordered[1].driver;
  bool true_path_sel  = inp_edges_ordered[0].driver == inp_edges_ordered[2].driver;

  if ((false_path_zero && true_path_sel)        // mux(sel,0  ,sel) -> sel
      || (false_path_sel && true_path_one)      // mux(sel,sel,  1) -> sel
      || (false_path_zero && true_path_one)) {  // mux(sel,0  ,  1) -> sel
    collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
    return true;
  }

  if (false_path_sel && true_path_zero) {  // mux(sel,sel,0) -> !sel
    auto not_node = node.create(Ntype_op::Not);
    not_node.connect_sink(inp_edges_ordered[0].driver);
    auto not_dpin = not_node.setup_driver_pin();
    collapse_forward_for_pin(node, not_dpin);
    return true;
  }

  if (false_path_one && true_path_sel) {  // mux(sel,1,sel) -> 1
    collapse_forward_for_pin(node, inp_edges_ordered[1].driver);
    return true;
  }

  return false;
}

void Cprop::scalar_sext(Node &node, XEdge_iterator &inp_edges_ordered) {
  const auto &pos_dpin = inp_edges_ordered[1].driver;
  if (!pos_dpin.is_type_const()) {
    return;  // not much to do
  }

  int64_t self_pos;
  {
    auto v = pos_dpin.get_type_const();
    if (!v.is_i())
      return;

    self_pos = v.to_i();
  }

  const auto &wire_dpin = inp_edges_ordered[0].driver;

  if (self_pos == 1) {
    for (auto &e : node.out_edges()) {
      if (e.sink.is_type(Ntype_op::Mux)) {
        e.sink.connect_driver(wire_dpin);
        e.del_edge();
      }
    }
  }

  if (wire_dpin.get_type_op() != Ntype_op::Sext)
    return;

  // Sext(Sext(X,a),b) == Sext(X, min(a,b))

  auto parent_pos_dpin = wire_dpin.get_node().get_sink_pin("b").get_driver_pin();
  if (!parent_pos_dpin.is_type_const())
    return;

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

std::shared_ptr<Lgtuple const> Cprop::find_lgtuple(const Node_pin &up_dpin) const {
  auto up_node = up_dpin.get_node();

  auto ptup_it = node2tuple.find(up_node.get_compact());
  if (ptup_it != node2tuple.end()) {
    I(!up_node.is_type_const());
    return ptup_it->second;
  }

  return nullptr;
}

std::shared_ptr<Lgtuple const> Cprop::find_lgtuple(const Node &up_node) const {
  auto ptup_it = node2tuple.find(up_node.get_compact());
  if (ptup_it != node2tuple.end()) {
    I(!up_node.is_type_const());
    return ptup_it->second;
  }

  return nullptr;
}

void Cprop::reconnect_sub_as_cell(Node &node, Ntype_op cell_ntype) {
  if (!node.is_sink_connected("$"))
    return;

  auto input_spin = node.get_sink_pin("$");
  auto input_dpin = input_spin.get_driver_pin();
  auto tup        = find_lgtuple(input_dpin);

  // 1st. Reconnect output (all cells but memory/sub have single output at pin 0)
  node.set_type(cell_ntype);

  I(cell_ntype != Ntype_op::Sub);  // structural is not allowed with subs
  if (cell_ntype == Ntype_op::Memory) {
    connect_clock_pin_if_needed(node);
    // memories should have the outputs already connected
  } else {
    auto sink_list = node.out_sinks();
    if (!sink_list.empty()) {
      for (auto &dp : node.out_connected_pins()) {
        dp.del();
      }

      for (auto &sp : sink_list) {
        // single sink pins map to zero always
        node.setup_driver_pin_raw(0).connect_sink(sp);
      }
    }
  }

  // 2nd. Reconnect inputs
  if (tup) {
    input_spin.del();

    for (const auto &e : tup->get_sort_map()) {
      if (Lgtuple::is_attribute(e.first)) {
        continue;
      }

      mmap_lib::str pin_name;
      int           pin_pid = 0;
      if (Ntype::is_single_sink(cell_ntype)) {
        pin_pid  = 0;
        pin_name = Ntype::get_sink_name(cell_ntype, 0);
      } else {
        pin_name = Lgtuple::get_first_level_name(e.first);
        if (!Ntype::has_sink(cell_ntype, pin_name)) {
          Pass::error("node:{} trying to connect cell:{} pin:{}, pin name does not exist",
                      node.debug_name(),
                      Ntype::get_name(cell_ntype),
                      pin_name);
          return;
        }
        pin_pid = Ntype::get_sink_pid(cell_ntype, pin_name);
        I(pin_pid >= 0);
      }

      auto spin = node.setup_sink_pin_raw(pin_pid);
      if (spin.is_connected() && Ntype::is_single_driver_per_pin(cell_ntype)) {
        if (cell_ntype != Ntype_op::Memory) {
          Pass::error("node:{} with cell:{} pin:{} can not have multiple drivers",
                      node.debug_name(),
                      Ntype::get_name(cell_ntype),
                      pin_name);
          return;
        }
        int delta_pid = 0;
        do {
          delta_pid += 11;
          spin = node.setup_sink_pin_raw(pin_pid + delta_pid);
        } while (spin.is_connected());
      }
      XEdge_iterator out_edges;  // Empty list
      auto           dpin = expand_data_and_attributes(node, e.first, out_edges, tup);
      I(!dpin.is_invalid());
      spin.connect_driver(dpin);  // e.second);
    }
  } else {
    // Possible direct connect if there is a single pin
    auto driver_list = input_spin.inp_drivers();
    input_spin.del();
    if (!Ntype::is_single_sink(cell_ntype)) {
      Pass::error("node:{} with cell:{} can have multiple sinks, but none selected",
                  node.debug_name(),
                  Ntype::get_name(cell_ntype));
      return;
    }
    if (driver_list.size() > 1 && Ntype::is_single_driver_per_pin(cell_ntype)) {
      auto pin_name = Ntype::get_sink_name(cell_ntype, 0);
      Pass::error("node:{} with cell:{} pin:{} can not have multiple drivers",
                  node.debug_name(),
                  Ntype::get_name(cell_ntype),
                  pin_name);
      return;
    }
    auto spin = node.setup_sink_pin_raw(0);
    for (auto &dp : driver_list) {
      spin.connect_driver(dp);
    }
  }
}

void Cprop::reconnect_tuple_sub(Node &node) {
  I(!hier);

  const auto &sub = node.get_type_sub_node();

  mmap_lib::str sub_name{sub.get_name()};
  if (sub_name.size() > 2 && sub_name.substr(0, 2) == "__") {
    auto cell_name  = sub_name.substr(2);
    auto cell_ntype = Ntype::get_op(cell_name);
    if (cell_ntype != Ntype_op::Invalid) {
      reconnect_sub_as_cell(node, cell_ntype);
      return;
    }
  }

  std::vector<bool> connected_ios(sub.size() + 1);
  connected_ios[0] = true;  // 0 is invalid instance_pid

  const auto &io_pins = sub.get_io_pins();

  Node_pin dollar_spin;
  for (auto &spin : node.inp_connected_pins()) {
    auto instance_pid = spin.get_pid();
    if (instance_pid == 0) {  // "$" pin
      dollar_spin = spin;
      continue;
    }

    if (!sub.has_instance_pin(instance_pid)) {
      if (instance_pid < io_pins.size())
        continue;  // only this could be pending. Anything else is an error
      Pass::error("graph {} connects to subgraph {} and the input pid is invalid. Recompile {}",
                  node.get_class_lgraph()->get_name(),
                  sub.get_name(),
                  node.get_class_lgraph()->get_name());
      return;
    }
    I(io_pins.size() > instance_pid);

    connected_ios[instance_pid] = true;
    const auto &io_pin          = io_pins[instance_pid];

    if (io_pin.dir != Sub_node::Direction::Input) {  // OOPS!!!
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
      try_connect_tuple_to_sub(it2->second, node, parent_node);

      for (auto &spin : node.inp_connected_pins()) {
        auto instance_pid = spin.get_pid();
        I(io_pins.size() > instance_pid);
        connected_ios[instance_pid] = true;
      }
    }
  }

  for (auto &dpin : node.out_connected_pins()) {
    auto instance_pid = dpin.get_pid();
    I(io_pins.size() > instance_pid);
    connected_ios[instance_pid] = true;
  }

  for (auto pid = 1u; pid < connected_ios.size(); ++pid) {
    if (connected_ios[pid])
      continue;

    if (!io_pins[pid].is_input())
      continue;

    // only punch inputs (clock, resetxxx, foo, but not outputs)
    auto *lg = node.get_class_lgraph();
    Pass::info("instance {} has unconnected {}. Punching through lgraph {}", node.debug_name(), io_pins[pid].name, lg->get_name());

    I(io_pins[pid].is_input());
    Node_pin dpin;
    if (lg->has_graph_input(io_pins[pid].name)) {
      dpin = lg->get_graph_input(io_pins[pid].name);
    } else {
      dpin = lg->add_graph_input(io_pins[pid].name, Port_invalid, 0);
    }
    I(dpin.is_driver());
    node.setup_sink_pin(io_pins[pid].name).connect_driver(dpin);
  }
}

void Cprop::reconnect_tuple_add(Node &node) {
  // Some tupleAdd should be converted to AttrSet
  auto pos_spin = node.get_sink_pin("field");
  if (!pos_spin.is_invalid()) {
    auto pos_dpin = pos_spin.get_driver_pin();
    if (pos_dpin.is_type_const()) {
      auto field = pos_dpin.get_type_const().to_field();
      if (Lgtuple::is_root_attribute(field)) {
        if (!Ntype::has_sink(Ntype_op::Flop, field.substr(2)) && field != "__fdef") {
          node.set_type(Ntype_op::AttrSet);
        }
      }
    }
  }

  std::shared_ptr<Lgtuple const> node_tup;
  {
    auto it = node2tuple.find(node.get_compact());
    if (it != node2tuple.end()) {
      node_tup = it->second;
      I(node_tup->is_correct());
    }
  }

  if (!node_tup || tuple_issues) {
    return;
  }

  // bool in_tuple_add_chain = false;
  XEdge_iterator pending_out_edges;
  for (auto &e : node.out_edges()) {
    auto sink_type = e.sink.get_type_op();
    if (e.sink.is_graph_output() && e.sink.get_pin_name() == "%") {
      try_create_graph_output(node, node_tup);  // first add outputs
    } else if (e.sink.get_node().is_type_sub_present() && e.sink.get_pin_name() == "$") {
      auto sub_node = e.sink.get_node();
      try_connect_tuple_to_sub(node_tup, sub_node, node);
    } else if (sink_type == Ntype_op::Get_mask  // get_mask handles tuples at both inputs
               || sink_type == Ntype_op::TupAdd
               || (sink_type == Ntype_op::Mux && e.sink.get_pid()
                   && !tuple_done.contains(e.sink.get_node().get_compact()))  // mux handles tuples at inputs, not select
               || sink_type == Ntype_op::TupGet
               || (sink_type == Ntype_op::SHL && e.sink.get_pid())  // SHL handles tuples at B (not a)
    ) {
    } else {
      pending_out_edges.emplace_back(e);
    }
  }

  if (!pending_out_edges.empty()) {
    if (node_tup->is_empty()) {
      // Empty tuple. Useless but legal. Just delete
    } else if (node_tup->is_trivial_scalar()) {
      expand_data_and_attributes(node, "", pending_out_edges, node_tup);
    } else {
      Pass::info("some pins:{} did not connect (may be fine)", pending_out_edges[0].sink.debug_name());
    }
  }
}

void Cprop::reconnect_tuple_get(Node &node) {
  auto [tup_name, key_name] = get_tuple_name_key(node);

  auto it = node2tuple.find(node.get_compact());

  bool is_attr_get = Lgtuple::is_attribute(key_name);
  if (is_attr_get) {
    node.set_type(Ntype_op::AttrGet);

    if (it != node2tuple.end()) {
      node.setup_sink_pin("parent").del();

      auto out_edges_list = node.out_edges();
      I(!out_edges_list.empty());
      auto new_dpin = expand_data_and_attributes(node, "", out_edges_list, it->second);

      for (auto &e : new_dpin.out_edges()) {
        node.setup_driver_pin().connect_sink(e.sink);
        e.del_edge();
      }

      node.setup_sink_pin("parent").connect_driver(new_dpin);
    }

    return;
  }
  I(it != node2tuple.end());

  if (it->second->is_trivial_scalar()) {
    auto out_edges_list = node.out_edges();
    if (!out_edges_list.empty())
      expand_data_and_attributes(node, "", out_edges_list, it->second);
  }
}

Node_pin Cprop::expand_data_and_attributes(Node &node, const mmap_lib::str &key_name, XEdge_iterator &pending_out_edges,
                                           const std::shared_ptr<Lgtuple const> &node_tup) {
  I(!hier);
  I(node_tup);
  I(node_tup->is_correct());
  I(!tuple_issues);

  auto value_dpin = node_tup->get_dpin(key_name);

  std::shared_ptr<Lgtuple> use_tup;

  for (const auto &it : node_tup->get_level_attributes(key_name)) {
    I(Lgtuple::is_attribute(it.first));
    auto attr = Lgtuple::get_last_level(it.first);
    if (Ntype::has_sink(Ntype_op::Flop, attr.substr(2)))
      continue;  // Do not create attr for flop config (handled in cprop directly)

    if (!use_tup) {
      use_tup = std::make_shared<Lgtuple>(*node_tup);
    }
    use_tup->add(attr, it.second);

    auto attr_node = node.create(Ntype_op::AttrSet);
    auto an_spin   = attr_node.setup_sink_pin("parent");
    auto af_spin   = attr_node.setup_sink_pin("field");
    auto av_spin   = attr_node.setup_sink_pin("value");

    // auto attr_key_node = node.create_const(Lconst::string(attr));
    auto attr_key_node = node.create_const(Lconst::from_string(attr));
    auto attr_key_dpin = attr_key_node.setup_driver_pin();
    attr_key_dpin.connect_sink(af_spin);

    av_spin.connect_driver(it.second);
    if (!value_dpin.is_invalid())  // weird but valid case of just attributes no data
      an_spin.connect_driver(value_dpin);

    value_dpin = attr_node.setup_driver_pin("Y");  // to chain all the attributes
    auto str   = node_tup->get_name();
    if (!str.empty())
      value_dpin.set_name(str);
    else if (value_dpin.has_name())
      value_dpin.del_name();
  }

  if (value_dpin.is_invalid()) {
    node_tup->dump();
    Pass::error("tuple could not find key:{} or any attribute", key_name);
    return invalid_pin;
  }

  for (auto &e : pending_out_edges) {
    auto sink_node = e.sink.get_node();
    if (sink_node.is_type(Ntype_op::AttrSet) && sink_node.get_driver_pin("Y").get_name() == node_tup->get_name())
      continue;  // already connected attribute field
    value_dpin.connect_sink(e.sink);
    e.del_edge();
  }

  if (use_tup) {
    node2tuple[value_dpin.get_node().get_compact()] = use_tup;
  }

  return value_dpin;
}

void Cprop::scalar_pass(Lgraph *lg) {
  tuple_found = false;

  for (auto node : lg->forward()) {
    auto op = node.get_type_op();
    if (op > Ntype_op::Mux) {
      tuple_found |= (op == Ntype_op::TupAdd || op == Ntype_op::TupGet);
      continue;
    }

    // fmt::print("scalar node:{}\n", node.debug_name());

    auto inp_edges_ordered = node.inp_edges_ordered();

    if (op == Ntype_op::Sext) {
      scalar_sext(node, inp_edges_ordered);
    } else if (op == Ntype_op::Mux) {
      bool del = scalar_mux(node, inp_edges_ordered);
      if (del)
        continue;
    } else if (op == Ntype_op::Not) {
      // FIXME:
      //
      // 1- not(not(x)) = x
      //
      // 2- mux(sel=ror(not(s)), X, Y) = mux(sel=ror(s), Y, X)
    } else if (op == Ntype_op::Ror) {
      // FIXME:
      //
      // 1- ror(ror(X)) == ror(X)
      //
      // 2- not(ror(not(X))) == ror(x)
      //
      // BW: ror(X) and X.__sbits==1 -> X

    } else if (op == Ntype_op::Get_mask) {
      // FIXME:
      //
      // 1- get_mask(get_mask(X,a),b)) == get_mask(X, set_mask(a=a,val=-1,mask=b))
      //
      // 2- get_mask(0,b) == 0
      //
      // 3- get_mask(-1,b) && b>0 == b
      //
      // 4- get_mask(a,-1) && a>0 == a
      //
      // 5- get_mask(a,0) == 0
      //
      // 6- set_mask(a=0,val=X,mask=b) == get_mask(X,b)
      //
      // 7- set_mask(a=X,val=Y,mask=-1) == Y
      //
      // 8- set_mask(a=X,val=Y,mask=0) == X
      //
      // 9- Since set_mask(a=X,val=-1,mask=b) == X | b
      // 9.1- get_mask(or(X,b),c) && b bit_implies c == get_mask(set_mask(a=X,val=-1,mask=b),c) -> get_mask(-1,c) (if c>0 -> c)
      //
      // 10- Since set_mask(a=X,val=0,mask=b) == X & b
      // 10.1- get_mask(and(X,b),c) && b bit_implies c == get_mask(set_mask(a=X,val=0,mask=b),c) -> get_mask(0,c) -> c
      //
      // 11- get_mask(set_mask(a=Y,val=X,mask=a),b) && a bit_implies b -> get_mask(X,b)
      //
      // 12- get_mask(set_mask(a=Y,val=X,mask=a),b) && ~a bit_implies b -> get_mask(Y,b)
      //
      // 13- set_mask(a=get_mask(Y,a),val=X,mask=b) && popcount(a) < popcount(b) -> get_mask(X,b)
      //
      // 14- eq(get_mask(X,b), c) and b bit_implies c == ror(get_mask(X,b))
      //
    } else if (!node.has_outputs()) {
      bwd_del_node(node);
      continue;
    }

    auto replaced_some = try_constant_prop(node, inp_edges_ordered);

    if (node.is_invalid())
      continue;  // It got deleted

    if (replaced_some) {
      inp_edges_ordered = node.inp_edges_ordered();
    }

    try_collapse_forward(node, inp_edges_ordered);
  }
}

void Cprop::connect_clock_pin_if_needed(Node &node) {
  auto spin_clock = node.setup_sink_pin("clock");
  if (spin_clock.is_connected())
    return;

  auto *lg = node.get_class_lgraph();

  Node_pin clock_pin;
  if (lg->has_graph_input("clock")) {
    clock_pin = lg->get_graph_input("clock");
  } else {
    clock_pin = lg->add_graph_input("clock", Port_invalid, 1);
  }
  spin_clock.connect_driver(clock_pin);
}

void Cprop::connect_reset_pin_if_needed(Node &node) {
  auto spin_reset = node.setup_sink_pin("reset");
  if (spin_reset.is_connected())
    return;

  auto *lg = node.get_class_lgraph();

  Node_pin reset_pin;
  if (lg->has_graph_input("reset")) {
    reset_pin = lg->get_graph_input("reset");
  } else {
    reset_pin = lg->add_graph_input("reset", Port_invalid, 1);
  }
  spin_reset.connect_driver(reset_pin);
}

void Cprop::tuple_pass(Lgraph *lg) {
  node2tuple.clear();
  tuple_done.clear();

  if (!tuple_found)
    return;

  for (auto iter = 0; iter < 6; ++iter) {
    tuple_issues = iter == 0;  // First iter may not be correct if there are flops or subgraphs
    for (auto node : lg->forward(hier)) {
      if (tuple_done.contains(node.get_compact()))
        continue;

      auto op = node.get_type_op();
      if (op != Ntype_op::Get_mask && op != Ntype_op::SHL && (op < Ntype_op::Mux || op == Ntype_op::Const))
        continue;

      // fmt::print("tuple  node:{}\n", node.debug_name());

      I(op != Ntype_op::IO);  // no IOs in fwd iterator

      if (iter > 0) {
        node2tuple.erase(node.get_compact());  // no output reuse
      }

      if (op == Ntype_op::Mux) {
        tuple_mux_mut(node);
      } else if (op == Ntype_op::Latch || op == Ntype_op::Fflop || op == Ntype_op::Memory) {
#ifndef NDEBUG
        fmt::print("cprop FIXME node:{} (similar to flop)\n", node.debug_name());
#endif
      } else if (op == Ntype_op::SHL) {
        tuple_shl_mut(node);
      } else if (op == Ntype_op::Flop) {
        tuple_flop_mut(node);
      } else if (op == Ntype_op::Sub) {
        tuple_subgraph(node);
      } else if (op == Ntype_op::Get_mask) {
        tuple_get_mask_mut(node);  // WARNING: This is allowed to transform the node
        // This is needed to handle the common string concat that can be used to index tuples (tuples used to select tuples)
        bool all_const         = true;
        auto inp_edges_ordered = node.inp_edges_ordered();
        for (auto &e : inp_edges_ordered) {
          if (e.driver.is_type_const())
            continue;
          all_const = false;
          break;
        }
        if (all_const) {
          replace_all_inputs_const(node, inp_edges_ordered);
        }
      } else if (op == Ntype_op::TupAdd) {
        tuple_tuple_add(node);
      } else if (op == Ntype_op::TupGet) {
        auto ok = tuple_tuple_get(node);
        if (!ok) {
          if (!tuple_issues)
            Pass::info("cprop could not simplify node:{}", node.debug_name());
          tuple_issues = true;
        }
      } else if (op == Ntype_op::AttrSet) {
        tuple_attr_set(node);
      } else {
        I(op == Ntype_op::AttrGet || op == Ntype_op::CompileErr);
      }
    }
    if (!tuple_issues)
      break;
  }

  if (tuple_issues)
    return;

  // tuple chain clean up and connect default flop pins
  for (auto node : lg->fast()) {
    auto op = node.get_type_op();

    if (op == Ntype_op::TupAdd) {
      reconnect_tuple_add(node);
    } else if (op == Ntype_op::TupGet) {
      reconnect_tuple_get(node);
    } else if (op == Ntype_op::Sub) {
      reconnect_tuple_sub(node);
    } else if (op == Ntype_op::Flop) {
      connect_clock_pin_if_needed(node);
      connect_reset_pin_if_needed(node);
    } else if (op == Ntype_op::Get_mask) {
      tuple_get_mask_mut(node);
    }
  }  // end of lg->fast()

  for (auto node : lg->fast()) {
    auto op = node.get_type_op();

    if (op == Ntype_op::TupAdd || op == Ntype_op::TupGet) {
      bwd_del_node(node);
    } else if (op <= Ntype_op::Mux && !node.has_outputs()) {
      bwd_del_node(node);
    }
  }

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

void Cprop::clean_io(Lgraph *lg) {
  // Remove IOs that are undriven (this is a bit controversial, but to be
  // consistent with FIRRTL for LEC)
  //
  // TODO: Maybe a cprop option (preserve_io) unset by default

  auto *sub = lg->ref_self_sub_node();

  lg->each_graph_input([&sub](Node_pin &dpin) {
    if (dpin.is_connected())
      return;

    auto pid = dpin.get_pid();

    dpin.del();

    if (sub->has_instance_pin(pid))
      sub->del_pin(pid);
  });

  lg->each_graph_output([&sub](Node_pin &dpin) {
    auto spin = dpin.change_to_sink_from_graph_out_driver();
    if (spin.is_connected())
      return;

    auto pid = dpin.get_pid();

    dpin.del();

    if (sub->has_instance_pin(pid))
      sub->del_pin(pid);
  });
}

void Cprop::do_trans(Lgraph *lg) {
  Lbench b("pass.cprop");

  scalar_pass(lg);
  tuple_pass(lg);
  if (tuple_found && !tuple_issues) {
    scalar_pass(lg);
    tuple_pass(lg);
  }
  // clean_io(lg);
}

void Cprop::try_create_graph_output(Node &node, const std::shared_ptr<Lgtuple const> &tup) {
  I(!hier);
  I(tup->is_correct());

  auto *lg          = node.get_class_lgraph();
  bool  local_error = false;
  bool  tup_scalar  = tup->is_scalar();  // It could have just attributes
  for (const auto &it : tup->get_map()) {
    if (unlikely(it.second.is_invalid())) {
      local_error = true;
      Pass::error("graph {} has output but it has invalid field {}", lg->get_name(), it.first);
      continue;
    }

    mmap_lib::str out_name{it.first};
    if (out_name.size() > 2 && out_name.substr(0, 2) == "%.") {
      out_name = out_name.substr(2);
    }
    if (tup_scalar)  // Remove foo.0.0.0 if scalar
      out_name = Lgtuple::get_canonical_name(out_name);

    if (unlikely(it.first.empty() || out_name.empty())) {
      local_error = true;
      Pass::info("Tuple {} for graph {} without named field (pyrope supports unnamed)", tup->get_name(), lg->get_name());
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

  for (const auto &e : node.inp_edges()) {
    if (e.driver.is_graph_io())
      continue;
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
