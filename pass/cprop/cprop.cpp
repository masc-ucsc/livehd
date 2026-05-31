//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "pass_cprop.hpp"
#include "perf_tracing.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::const_value_of;
using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::debug_name;
using livehd::graph_util::find_sink_pin;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::type_op_of;

#define TRACE(x)
// #define TRACE(x) x

namespace {

void sort_inp(std::vector<hhds::Edge_class>& edges) {
  std::sort(edges.begin(), edges.end(), [](const hhds::Edge_class& a, const hhds::Edge_class& b) {
    return a.sink.get_port_id() < b.sink.get_port_id();
  });
}

std::vector<hhds::Edge_class> ordered_inp_edges(const hhds::Node_class& node) {
  auto e = node.inp_edges();
  sort_inp(e);
  return e;
}

using livehd::graph_util::hydrate_const;

[[nodiscard]] hhds::Pin_class setup_sink_by_name(const hhds::Node_class& node, std::string_view name) {
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    return node.create_sink_pin(name);
  }
  auto pid = Ntype::get_sink_pid(op, name);
  if (pid == livehd::Port_invalid) {
    return {};
  }
  return node.create_sink_pin(pid);
}

// "Is there an edge from `driver` to `sink`?"
[[nodiscard]] bool is_driver_connected_to_sink(const hhds::Pin_class& driver, const hhds::Pin_class& sink) {
  if (driver.is_invalid() || sink.is_invalid()) {
    return false;
  }
  for (const auto& e : driver.out_edges()) {
    if (e.sink == sink) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::string sink_pin_name(const hhds::Pin_class& spin) {
  if (spin.is_invalid()) {
    return {};
  }
  auto master = spin.get_master_node();
  auto op     = type_op_of(master);
  return Ntype::get_sink_name(op, spin.get_port_id());
}

}  // namespace

Cprop::Cprop(bool _hier) : hier(_hier) {}

void Cprop::collapse_forward_same_op(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered) {
  auto op = type_op_of(node);

  bool all_done = true;
  for (auto& out : node.out_edges()) {
    if (type_op_of(out.sink.get_master_node()) != op) {
      all_done = false;
      continue;
    }

    if (out.driver.get_port_id() != out.sink.get_port_id()) {
      all_done = false;
      continue;
    }

    for (auto& inp : inp_edges_ordered) {
      if (op == Ntype_op::Xor) {
        if (is_driver_connected_to_sink(inp.driver, out.sink)) {
          out.sink.del_sink(inp.driver);
        } else {
          out.sink.connect_driver(inp.driver);
        }
      } else if (op == Ntype_op::Or || op == Ntype_op::And) {
        out.sink.connect_driver(inp.driver);
      } else {
        I(op != Ntype_op::Sum);
        out.sink.connect_driver(inp.driver);
      }
    }

    out.del_edge();
  }
  if (all_done) {
    I(!node.has_out_edges());
    node.del_node();
  }
}

void Cprop::collapse_forward_sum(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered) {
  if (inp_edges_ordered.size() > 32) {
    return;
  }

  I(type_op_of(node) == Ntype_op::Sum);
  bool all_edges_deleted = true;
  for (auto& out : node.out_edges()) {
    auto next_sum_node = out.sink.get_master_node();
    if (type_op_of(next_sum_node) != Ntype_op::Sum) {
      all_edges_deleted = false;
      continue;
    }

    for (auto& inp : inp_edges_ordered) {
      // Sum(A,Sum(B,C))  = Sum(A+C,B)
      // Sum(Sum(A,B),C)) = Sum(A+C,B)
      if (inp.sink.get_port_id() == 0 && out.sink.get_port_id() == 0) {
        out.sink.connect_driver(inp.driver);
      } else if (inp.sink.get_port_id() == 0 && out.sink.get_port_id() == 1) {
        out.sink.connect_driver(inp.driver);
      } else if (inp.sink.get_port_id() == 1 && out.sink.get_port_id() == 0) {
        setup_sink_by_name(next_sum_node, "b").connect_driver(inp.driver);
      } else {
        setup_sink_by_name(next_sum_node, "a").connect_driver(inp.driver);
      }
    }
    out.del_edge();
  }

  if (all_edges_deleted) {
    bwd_del_node(node);
  }
}

void Cprop::collapse_forward_always_pin0(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered) {
  auto op = type_op_of(node);

  for (auto& out : node.out_edges()) {
    for (auto& inp : inp_edges_ordered) {
      if (op == Ntype_op::Xor) {
        if (is_driver_connected_to_sink(inp.driver, out.sink)) {
          out.sink.del_sink(inp.driver);
        } else {
          out.sink.connect_driver(inp.driver);
        }
      } else {
        out.sink.connect_driver(inp.driver);
      }
    }
  }

  bwd_del_node(node);
}

void Cprop::collapse_forward_for_pin(hhds::Node_class& node, hhds::Pin_class new_dpin) {
  for (auto& out : node.out_edges()) {
    new_dpin.connect_sink(out.sink);
  }

  bwd_del_node(node);
}

bool Cprop::try_constant_prop(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered) {
  int n_inputs_constant = 0;
  int n_inputs          = 0;
  for (auto& e : inp_edges_ordered) {
    n_inputs++;
    if (!is_const_pin(e.driver)) {
      continue;
    }
    n_inputs_constant++;
  }

  if (n_inputs == n_inputs_constant && n_inputs) {
    replace_all_inputs_const(node, inp_edges_ordered);
    return true;
  } else if (n_inputs && n_inputs_constant >= 1) {
    replace_part_inputs_const(node, inp_edges_ordered);
    return true;
  }

  return false;
}

void Cprop::try_collapse_forward(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered) {
  auto op = type_op_of(node);

  if (inp_edges_ordered.size() == 1) {
    auto prev_op = type_op_of(inp_edges_ordered[0].driver.get_master_node());
    if (op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::Div || op == Ntype_op::And || op == Ntype_op::Or
        || op == Ntype_op::Xor) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
      return;
    }
    if (prev_op == Ntype_op::Get_mask) {
      if (op == Ntype_op::Get_mask) {
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
    if (inp_edges_ordered.size() <= 1) {
      node.del_node();
      return;
    }
    auto& a_pin = inp_edges_ordered[1].driver;
    for (auto i = 2u; i < inp_edges_ordered.size(); ++i) {
      if (a_pin != inp_edges_ordered[i].driver) {
        return;
      }
    }
    collapse_forward_for_pin(node, a_pin);
  }
}

void Cprop::replace_part_inputs_const(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered) {
  auto op = type_op_of(node);
  if (op == Ntype_op::Mux) {
    auto& s_pin = inp_edges_ordered[0].driver;
    if (!is_const_pin(s_pin)) {
      return;
    }
    auto s_const = hydrate_const(s_pin);
    if (s_const.has_unknowns()) {
      return;
    }

    I(s_const.is_i());
    size_t sel = s_const.to_i();

    hhds::Pin_class a_pin;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == 0) {
        continue;
      }
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(sel + 1)) {
        a_pin = e.driver;
        break;
      }
    }

    if (a_pin.is_invalid()) {
#ifndef NDEBUG
      Pass::info("WARNING: mux selector:{} for a disconnected pin in mux. Using zero\n", sel);
#endif
      a_pin = create_const(*current_graph, *Dlop::create_integer(0));
    }

    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::EQ) {
    // FIXME: 1- eq(X,0) = not(ror(x))
  } else if (op == Ntype_op::Sum || op == Ntype_op::Or || op == Ntype_op::And) {
    hhds::Edge_class first_const_edge;
    int              nconstants = 0;
    int              npending   = 0;

    Const result;
    if (op == Ntype_op::And) {
      result = Dlop::create_integer(-1);
    }

    std::vector<hhds::Edge_class> edge_it2;
    for (auto& i : inp_edges_ordered) {
      if (!is_const_pin(i.driver)) {
        if (npending == 0) {
          edge_it2.push_back(i);
        }
        npending++;
        continue;
      }

      auto c = hydrate_const(i.driver);

      ++nconstants;

      if (op == Ntype_op::Sum) {
        if (sink_pin_name(i.sink) == "a") {
          result = result.add_op(c);
        } else {
          I(sink_pin_name(i.sink) == "b");
          result = result.sub_op(c);
        }
      } else if (op == Ntype_op::Or) {
        result = result.or_op(c);
      } else {
        I(op == Ntype_op::And);
        result = result.and_op(c);
      }

      if (nconstants == 1) {
        first_const_edge = i;
      } else {
        i.del_edge();
      }
    }

    if (nconstants > 1) {
      first_const_edge.del_edge();
      if (!result.is_known_zero()) {
        auto dpin = create_const(*current_graph, result);
        if (result.is_positive() || op == Ntype_op::Or) {
          setup_sink_by_name(node, "a").connect_driver(dpin);
        } else {
          setup_sink_by_name(node, "b").connect_driver(dpin);
        }
      } else if (npending == 1) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (npending == 0 && nconstants == 1) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
    } else if (npending == 1 && nconstants == 0) {
      if (!(op == Ntype_op::Sum && sink_pin_name(edge_it2[0].sink) == "b")) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    }
  } else if (op == Ntype_op::SRA) {
    auto& amt_pin = inp_edges_ordered[1].driver;
    if (is_const_pin(amt_pin) && hydrate_const(amt_pin).is_known_zero()) {
      collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
    }
  } else if (op == Ntype_op::SHL) {
    if (inp_edges_ordered.size() == 2) {
      auto& amt_pin = inp_edges_ordered[1].driver;
      if (is_const_pin(amt_pin) && hydrate_const(amt_pin).is_known_zero()) {
        collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
      }
    }
  }
}

void Cprop::replace_all_inputs_const(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered) {
  auto op = type_op_of(node);
  if (op == Ntype_op::SHL) {
    auto a_pin = livehd::graph_util::get_driver_of_sink_name(node, "a");
    if (a_pin.is_invalid()) {
      return;
    }
    Const val = hydrate_const(a_pin);

    Const result;
    result = Dlop::create_integer(0);

    bool zero_shifts = true;
    for (auto& amt_dpin : livehd::graph_util::inp_drivers_of(node, "b")) {
      Const amt   = hydrate_const(amt_dpin);
      result      = result.or_op(val.shl_op(amt));  // pass the Const shift amount
      zero_shifts = false;
    }

    if (zero_shifts) {
      replace_node(node, Dlop::create_integer(-1));
    } else {
      replace_node(node, result);
    }

  } else if (op == Ntype_op::Ror) {
    Const result;
    result = Dlop::create_integer(0);
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      result = result.ror_op(c);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Set_mask) {
    auto a_pin     = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto mask_pin  = livehd::graph_util::get_driver_of_sink_name(node, "mask");
    auto value_pin = livehd::graph_util::get_driver_of_sink_name(node, "value");

    if (a_pin.is_invalid()) {
      return;
    }
    Const val = hydrate_const(a_pin);

    if (!mask_pin.is_invalid() && !value_pin.is_invalid()) {
      auto mask  = hydrate_const(mask_pin);
      auto value = hydrate_const(value_pin);
      replace_node(node, val.set_mask_op(mask, value));
    } else {
      replace_node(node, val);
    }
  } else if (op == Ntype_op::Sum) {
    Const result;
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      if (i.sink.get_port_id() == 0) {
        result = result.add_op(c);
      } else {
        result = result.sub_op(c);
      }
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Or) {
    Const result;
    for (auto& e : inp_edges_ordered) {
      auto c = hydrate_const(e.driver);
      result = result.or_op(c);
    }

    replace_logic_node(node, result);

  } else if (op == Ntype_op::And) {
    Const result;
    result = Dlop::create_integer(-1);
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      result = result.and_op(c);
    }

    replace_node(node, result);

  } else if (op == Ntype_op::EQ) {
    bool eq = true;
    I(inp_edges_ordered.size() > 1);
    auto first = hydrate_const(inp_edges_ordered[0].driver);
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      auto c = hydrate_const(inp_edges_ordered[i].driver);
      eq     = eq && !first.eq_op(c)->is_known_false();
    }

    Const result;
    result = Dlop::create_integer(eq ? 1 : 0);

    replace_node(node, result);
  } else if (op == Ntype_op::Mux) {
    auto sel_const = hydrate_const(inp_edges_ordered[0].driver);
    I(sel_const.is_i());

    size_t sel = sel_const.to_i();

    Const result;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == 0) {
        continue;
      }
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(sel + 1)) {
        result = hydrate_const(e.driver);
        break;
      }
    }

    if (result.get_bits() == 0) {
      result = Dlop::create_integer(0);
#ifndef NDEBUG
      Pass::info("WARNING: mux:{} selector:{} goes for disconnected pin in mux. Using zero\n", debug_name(node), sel);
#endif
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Mult) {
    Const result;
    result = Dlop::create_integer(1);
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      result = result.mult_op(c);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Div) {
    I(inp_edges_ordered.size() == 2);
    Const a = hydrate_const(inp_edges_ordered[0].driver);
    Const b = hydrate_const(inp_edges_ordered[1].driver);

    auto result = a.div_op(b);

    replace_node(node, result);
  } else {
#ifndef NDEBUG
    Pass::info("FIXME: cprop still does not copy prop node:{}\n", debug_name(node));
#endif
  }
}

void Cprop::replace_node(hhds::Node_class& node, const Const& result) {
  auto dpin = create_const(*current_graph, result);

  for (auto& out : node.out_edges()) {
    auto out_bits = bits_of(out.driver);
    auto new_bits = bits_of(dpin);
    if (new_bits == out_bits || out_bits == 0) {
      dpin.connect_sink(out.sink);
    } else {
      auto result2 = result.adjust_bits(out_bits);
      auto dpin2   = create_const(*current_graph, *result2);
      dpin2.connect_sink(out.sink);
    }
  }

  node.del_node();
}

void Cprop::replace_logic_node(hhds::Node_class& node, const Const& result) {
  hhds::Pin_class dpin_0;

  for (auto& out : node.out_edges()) {
    if (dpin_0.is_invalid()) {
      dpin_0 = create_const(*current_graph, result);
    }
    dpin_0.connect_sink(out.sink);
  }

  node.del_node();
}

bool Cprop::scalar_mux(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered) {
  if (inp_edges_ordered.size() != 3) {
    return false;
  }

  if (inp_edges_ordered[1].driver == inp_edges_ordered[2].driver) {
    collapse_forward_for_pin(node, inp_edges_ordered[1].driver);
    return true;
  }

  bool false_path_zero = false;
  bool false_path_one  = false;
  if (is_const_pin(inp_edges_ordered[1].driver)) {
    auto v          = hydrate_const(inp_edges_ordered[1].driver);
    false_path_zero = v.is_known_zero() || v.is_string();
    false_path_one  = v.is_i() && v.to_i() == -1;
  }

  bool true_path_zero = false;
  bool true_path_one  = false;
  if (is_const_pin(inp_edges_ordered[2].driver)) {
    auto v         = hydrate_const(inp_edges_ordered[2].driver);
    true_path_zero = v.is_known_zero() || v.is_string();
    true_path_one  = v.is_i() && v.to_i() == -1;
  }

  bool false_path_sel = inp_edges_ordered[0].driver == inp_edges_ordered[1].driver;
  bool true_path_sel  = inp_edges_ordered[0].driver == inp_edges_ordered[2].driver;

  if ((false_path_zero && true_path_sel) || (false_path_sel && true_path_one) || (false_path_zero && true_path_one)) {
    collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
    return true;
  }

  if (false_path_sel && true_path_zero) {
    auto not_node = create_typed_node(*current_graph, Ntype_op::Not);
    auto not_spin = not_node.create_sink_pin(0);
    not_spin.connect_driver(inp_edges_ordered[0].driver);
    auto not_dpin = not_node.create_driver_pin(0);
    collapse_forward_for_pin(node, not_dpin);
    return true;
  }

  if (false_path_one && true_path_sel) {
    collapse_forward_for_pin(node, inp_edges_ordered[1].driver);
    return true;
  }

  return false;
}

void Cprop::scalar_sext(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges_ordered) {
  const auto& pos_dpin = inp_edges_ordered[1].driver;
  if (!is_const_pin(pos_dpin)) {
    return;
  }

  int64_t self_pos;
  {
    auto v = hydrate_const(pos_dpin);
    if (!v.is_i()) {
      return;
    }
    self_pos = v.to_i();
  }

  const auto& wire_dpin = inp_edges_ordered[0].driver;

  if (self_pos == 1) {
    for (auto& e : node.out_edges()) {
      if (type_op_of(e.sink.get_master_node()) == Ntype_op::Mux) {
        e.sink.connect_driver(wire_dpin);
        e.del_edge();
      }
    }
  }

  auto wire_master = wire_dpin.get_master_node();
  if (type_op_of(wire_master) != Ntype_op::Sext) {
    return;
  }

  // Sext(Sext(X,a),b) == Sext(X, min(a,b))
  auto parent_pos_dpin = livehd::graph_util::get_driver_of_sink_name(wire_master, "b");
  if (!is_const_pin(parent_pos_dpin)) {
    return;
  }

  auto parent_pos = hydrate_const(parent_pos_dpin).to_i();

  auto b = std::min(self_pos, parent_pos);
  if (b != self_pos) {
    auto new_const_dpin = create_const(*current_graph, *Dlop::create_integer(b));
    inp_edges_ordered[1].del_edge();
    setup_sink_by_name(node, "b").connect_driver(new_const_dpin);
  }

  auto parent_wire_dpin = livehd::graph_util::get_driver_of_sink_name(wire_master, "a");
  inp_edges_ordered[0].del_edge();
  setup_sink_by_name(node, "a").connect_driver(parent_wire_dpin);
}

hhds::Pin_class Cprop::try_find_single_driver_pin(hhds::Node_class& node, int64_t pos) {
  I(type_op_of(node) == Ntype_op::Set_mask);

  auto a_pin    = livehd::graph_util::get_driver_of_sink_name(node, "a");
  auto mask_pin = livehd::graph_util::get_driver_of_sink_name(node, "mask");
  if (a_pin.is_invalid() || mask_pin.is_invalid()) {
    return {};
  }
  if (!is_const_pin(mask_pin)) {
    return {};
  }

  auto mask_const               = hydrate_const(mask_pin);
  auto [range_begin, range_end] = mask_const.get_mask_range();
  if (pos >= range_end || pos < range_begin) {
    if (is_const_pin(a_pin)) {
      auto v = hydrate_const(a_pin).get_mask_op(Dlop::get_mask_value(pos));
      return create_const(*current_graph, *v);
    }
    auto a_master = a_pin.get_master_node();
    if (type_op_of(a_master) != Ntype_op::Set_mask) {
      return {};
    }
    return try_find_single_driver_pin(a_master, pos);
  }
  if (range_begin == pos && range_end == (pos + 1)) {
    return livehd::graph_util::get_driver_of_sink_name(node, "value");
  }

  return {};
}

bool Cprop::scalar_get_mask(hhds::Node_class& node) {
  auto a_pin     = livehd::graph_util::get_driver_of_sink_name(node, "a");
  auto mask_pin  = livehd::graph_util::get_driver_of_sink_name(node, "mask");
  if (a_pin.is_invalid() || mask_pin.is_invalid() || !node.has_out_edges()) {
    node.del_node();
    return true;
  }
  if (!is_const_pin(mask_pin)) {
    return false;
  }

  auto mask_const = hydrate_const(mask_pin);

  // Rule 4: get_mask(a, -1) == a
  if (mask_const.is_i() && mask_const.to_i() == -1) {
    collapse_forward_for_pin(node, a_pin);
    return true;
  }

  auto a_master = a_pin.get_master_node();
  if (type_op_of(a_master) != Ntype_op::Set_mask) {
    return false;
  }

  auto [range_begin, range_end] = mask_const.get_mask_range();

  if ((range_begin + 1) != range_end) {
    return false;
  }

  auto dpin = try_find_single_driver_pin(a_master, range_begin);
  if (!dpin.is_invalid()) {
    collapse_forward_for_pin(node, dpin);
    return true;
  }

  return false;
}

void Cprop::scalar_pass(hhds::Graph* g) {
  std::vector<hhds::Node_class> snapshot;
  for (auto node : g->forward_class()) {
    snapshot.push_back(node);
  }

  for (auto& node : snapshot) {
    if (node.is_invalid()) {
      continue;
    }
    auto op = type_op_of(node);
    if (op > Ntype_op::Mux) {
      continue;
    }

    auto inp_edges_ordered = ordered_inp_edges(node);

    if (op == Ntype_op::Sext) {
      if (inp_edges_ordered.size() >= 2) {
        scalar_sext(node, inp_edges_ordered);
      }
    } else if (op == Ntype_op::Mux) {
      bool del = scalar_mux(node, inp_edges_ordered);
      if (del) {
        continue;
      }
    } else if (op == Ntype_op::Get_mask) {
      bool del = scalar_get_mask(node);
      if (del) {
        continue;
      }
    } else if (!node.has_out_edges()) {
      bwd_del_node(node);
      continue;
    }

    auto replaced_some = try_constant_prop(node, inp_edges_ordered);

    if (node.is_invalid()) {
      continue;
    }

    if (replaced_some) {
      inp_edges_ordered = ordered_inp_edges(node);
    }

    try_collapse_forward(node, inp_edges_ordered);
  }
}

void Cprop::do_trans(const std::shared_ptr<hhds::Graph>& g) {
  if (!g) {
    return;
  }

  auto gio  = g->get_io();
  auto name = gio ? std::string{gio->get_name()} : std::string{};

  TRACE_EVENT("pass", nullptr, [&name](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(absl::StrCat(converted_str, name));
  });

  current_graph = g.get();
  scalar_pass(current_graph);
  current_graph = nullptr;
}

void Cprop::bwd_del_node(hhds::Node_class& node) {
  // Aggressive del: also remove single-user inputs that become dead.
  // WARNING: only call when all needed downstream edges have been added.

  if (hier) {
    return;
  }

  I(!Ntype::is_loop_last(type_op_of(node)));

  absl::flat_hash_set<hhds::Class_index> potential_set;
  std::deque<hhds::Node_class>           potential;

  for (const auto& e : node.inp_edges()) {
    if (is_graph_input_pin(e.driver) || is_graph_output_pin(e.driver)) {
      continue;
    }
    auto master = e.driver.get_master_node();
    // CONST_NODE (and other singletons) cannot be deleted: const pins are
    // leaves of the form CONST_NODE.pid_N, so dropping the consumer just
    // leaves the pin unreferenced — harmless and dedup-friendly.
    if (livehd::graph_util::is_builtin_node(master)) {
      continue;
    }
    if (potential_set.contains(master.get_class_index())) {
      continue;
    }
    potential.emplace_back(master);
    potential_set.insert(master.get_class_index());
  }

  node.del_node();

  while (!potential.empty()) {
    auto n = potential.front();
    potential.pop_front();

    if (n.is_invalid()) {
      continue;
    }

    if (!Ntype::is_loop_last(type_op_of(n)) && !n.has_out_edges()) {
      for (auto e : n.inp_edges()) {
        if (is_graph_input_pin(e.driver) || is_graph_output_pin(e.driver)) {
          continue;
        }
        auto d_master = e.driver.get_master_node();
        if (livehd::graph_util::is_builtin_node(d_master)) {
          continue;
        }
        if (potential_set.contains(d_master.get_class_index())) {
          continue;
        }
        potential.emplace_back(d_master);
        potential_set.insert(d_master.get_class_index());
      }
      n.del_node();
    }
  }
}
