// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "bitwidth_rewrite.hpp"

#include "cprop_value.hpp"

namespace {
using namespace livehd::graph_util;
using livehd::cprop_value::make_node;
Cprop::Inp_pins ordered_inp_edges(const hhds::Node_class& node) { return node.inp_pins_snapshot(); }
hhds::Pin_class drv_at(const hhds::Node_class& node, uint32_t port) { return node.get_sink_pin(port).get_driver_pin(); }
int             low_mask_width(const Dlop& mask) {
  auto window = mask_window_of(mask);
  return window && window->first == 0 ? window->second : -1;
}
bool has_single_consumer(const hhds::Pin_class& pin) {
  auto edges = pin.out_edges();
  auto it    = edges.begin();
  return it != edges.end() && ++it == edges.end();
}
bool fits_unsigned_window(const hhds::Pin_class& pin, int width) {
  const int bound = livehd::cprop_value::unsigned_width(pin);
  return width > 0 && bound >= 0 && bound <= width;
}
bool is_computed_comb_op(Ntype_op op) { return Ntype::is_comb(op) && op != Ntype_op::Rem && op != Ntype_op::Clock_cell; }
}  // namespace
using namespace livehd::graph_util;
using livehd::cprop_value::make_node;

// A finite pack with one nonzero lane is a shift when the settled range
// proves that its lane needs no truncation. This avoids materializing a
// private arithmetic producer solely to make a Verilog concat self-sized.
bool Bitwidth_rewrite::scalar_concat_range(hhds::Node_class& node) {
  hhds::Pin_class value;
  int             offset = 0, width = 0;
  for (const auto& lane : concat_lanes(node)) {
    if (lane.value.is_const() && const_of(lane.value).is_known_zero()) {
      continue;
    }
    if (!value.is_invalid()) {
      return false;
    }
    value  = lane.value;
    offset = lane.offset;
    width  = lane.width;
  }
  if (value.is_invalid() || !fits_unsigned_window(value, width)) {
    return false;
  }
  if (offset == 0) {
    return collapse_forward_for_pin(node, value);
  }
  auto shift = make_node(*current_graph, Ntype_op::SHL);
  setup_sink_by_name(shift, "a").connect_driver(value);
  setup_sink_by_name(shift, "b").connect_driver(create_const(*current_graph, *Dlop::create_integer(offset)));
  auto output = shift.create_driver_pin(0);
  set_ubits(output, width + offset);
  livehd::cprop_value::active->capacities.insert_or_assign(output.get_class_index(), width + offset);
  livehd::cprop_value::remember(shift);
  return collapse_forward_for_pin(node, output);
}

bool Bitwidth_rewrite::scalar_get_mask_range(hhds::Node_class& node) {
  const auto source = drv_at(node, 0);
  const auto mask   = drv_at(node, 2);
  if (source.is_invalid() || !mask.is_const()) {
    return false;
  }
  const int width = low_mask_width(const_of(mask));
  if (width <= 0 || source.is_const() || is_graph_input_pin(source) || !has_single_consumer(source)
      || fits_unsigned_window(source, width)) {
    return false;
  }

  // This constructor pushes a finite window through a PRIVATE bitwise region.
  // Every descent commits to deleting that producer before considering its
  // operands; no surviving cone is searched and no growing list is copied.
  // Newly created windows are normalized immediately, including a 65-bit
  // rotate/XOR chain that must become one machine word in this invocation.
  bool                                                 consumed = false;
  std::function<hhds::Pin_class(hhds::Pin_class, int)> window;
  window = [&](hhds::Pin_class pin, int bits) -> hhds::Pin_class {
    if (pin.is_const()) {
      return create_const(*current_graph, *const_of(pin).get_mask_op_opt(0, bits));
    }
    if (fits_unsigned_window(pin, bits)) {
      return pin;
    }
    auto       inner = pin.get_master_node();
    const auto op    = type_op_of(inner);
    if (!is_graph_input_pin(pin) && !inner.is_invalid() && has_single_consumer(pin)) {
      if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
        std::vector<hhds::Pin_class> operands;
        int                          new_windows = 0;
        for (auto sink : inner.inp_sorted_pins()) {
          auto operand = sink.get_driver_pin();
          operands.push_back(operand);
          new_windows += operand.is_const() || fits_unsigned_window(operand, bits) ? 0 : 1;
        }
        // Bitwidth owns this rule. Realization size is a cost hint here;
        // the finite-window identity is valid for signed inputs too.
        const int bound = livehd::graph_util::bits_of(pin);
        if (new_windows <= 1 || (bound > 64 && bits <= 64)) {
          // Attach new window placeholders first so retiring the old parent
          // leaves every operand with the same private ownership it had.
          std::vector<hhds::Node_class> windows;
          for (auto operand : operands) {
            windows.push_back(livehd::cprop_value::make_get_mask(*current_graph, operand, 0, bits));
          }
          livehd::cprop_value::retire(inner);
          consumed = true;
          for (size_t i = 0; i < operands.size(); ++i) {
            const auto narrowed = window(operands[i], bits);
            // The placeholder kept its producer alive while siblings changed.
            livehd::cprop_value::retire(windows[i]);
            operands[i] = narrowed;
          }
          auto result = make_node(*current_graph, op);
          for (auto operand : operands) {
            livehd::graph_util::append_sink_operand(result, op, 0).connect_driver(operand);
          }
          auto output = result.create_driver_pin(0);
          livehd::graph_util::set_ubits(output, bits);
          livehd::cprop_value::active->capacities.insert_or_assign(output.get_class_index(), bits);
          return output;
        }
      } else if (op == Ntype_op::SHL) {
        const auto input = drv_at(inner, 0), amount = drv_at(inner, 1);
        if (amount.is_const() && !const_of(amount).has_unknowns() && const_of(amount).is_just_i64()) {
          const auto k = const_of(amount).to_just_i64();
          if (k >= bits) {
            livehd::cprop_value::retire(inner);
            consumed = true;
            return create_const(*current_graph, *Dlop::create_integer(0));
          }
          if (k > 0) {
            auto keep = livehd::cprop_value::make_get_mask(*current_graph, input, 0, bits - static_cast<int>(k));
            livehd::cprop_value::retire(inner);
            consumed            = true;
            const auto narrowed = window(input, bits - static_cast<int>(k));
            livehd::cprop_value::retire(keep);
            auto result = make_node(*current_graph, Ntype_op::SHL);
            setup_sink_by_name(result, "a").connect_driver(narrowed);
            setup_sink_by_name(result, "b").connect_driver(amount);
            auto output = result.create_driver_pin(0);
            livehd::graph_util::set_ubits(output, bits);
            livehd::cprop_value::active->capacities.insert_or_assign(output.get_class_index(), bits);
            return output;
          }
        }
      }
    }
    auto get    = livehd::cprop_value::make_get_mask(*current_graph, pin, 0, bits);
    auto output = get.create_driver_pin(0);
    livehd::graph_util::set_ubits(output, bits);
    return output;
  };
  const auto result = window(source, width);
  if (!consumed) {
    livehd::cprop_value::retire(result.get_master_node());
    return false;
  }
  return collapse_forward_for_pin(node, result);
}

// Called only after bitwidth convergence. This is the fact-dependent rule
// set, not a general cprop invocation: no constant-propagation schedule, state
// analysis, repeated pack discovery, or fresh-node fixed point runs here.
void Bitwidth_rewrite::run(hhds::Graph& graph, const std::function<int(const hhds::Pin_class&)>& query,
                           const std::function<void(const hhds::Pin_class&)>&      erase,
                           const std::function<void(const hhds::Pin_class&, int)>& publish) {
  current_graph = &graph;
  livehd::cprop_value::Facts facts;
  facts.track_wiring  = false;
  facts.range         = query;
  facts.erase_range   = erase;
  facts.publish_range = publish;
  livehd::cprop_value::Scope    scope(facts);
  std::vector<hhds::Node_class> order;
  for (auto node : graph.body().nodes(hhds::Node_order::forward)) {
    order.push_back(node);
  }
  size_t edge_budget = 0;
  for (auto node : order) {
    for ([[maybe_unused]] auto input : node.inp_sorted_pins()) {
      ++edge_budget;
    }
  }
  facts.wiring.set_edge_budget(edge_budget);
  facts.inspection_budget = edge_budget;
  for (auto node : order) {
    if (node.is_invalid() || facts.generations.contains(node.get_class_index())) {
      continue;
    }
    const auto op = type_op_of(node);
    if (!is_computed_comb_op(op)) {
      continue;
    }
    auto inputs = ordered_inp_edges(node);
    if (op == Ntype_op::Mux) {
      if (scalar_mux(node, inputs, false) || node.is_invalid()) {
        continue;
      }
      if (type_op_of(node) != Ntype_op::Mux) {
        inputs = ordered_inp_edges(node);
        scalar_bool(node, inputs);
      }
    } else if (op == Ntype_op::EQ) {
      scalar_eq(node, inputs);
    } else if (op == Ntype_op::Sext && inputs.size() >= 2) {
      scalar_sext(node, inputs);
    } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
      if (op == Ntype_op::Or && try_broadcast_or(node, inputs)) {
        continue;
      }
      scalar_bool(node, inputs);
    } else if (op == Ntype_op::Get_mask) {
      scalar_get_mask_range(node);
    } else if (op == Ntype_op::Set_mask) {
      scalar_set_mask(node);
    } else if (op == Ntype_op::Concat) {
      if (scalar_concat_range(node)) {
        continue;
      }
      merge_concat_slices(node);
    }
    if (!node.is_invalid()) {
      if (type_op_of(node) == Ntype_op::Or || type_op_of(node) == Ntype_op::Set_mask) {
        canonicalize_pack(node);
      }
      if (!node.is_invalid()) {
        remember_node(node);
      }
    }
  }
  // Disjoint expression groups are considered once against the settled facts.
  vectorize_bit_reductions();
  current_graph = nullptr;
}
