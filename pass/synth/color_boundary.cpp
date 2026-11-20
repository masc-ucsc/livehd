// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "color_boundary.hpp"

#include <algorithm>
#include <map>
#include <vector>

#include "bit_facts.hpp"
#include "cell.hpp"
#include "node_util.hpp"
#include "state_sweep.hpp"

namespace livehd::synth {
namespace gu = livehd::graph_util;

namespace {
constexpr int kMaxWidth = 1 << 20;

int color_of(const hhds::Node_class& n) { return gu::has_color(n) ? gu::color_of(n) : 0; }

// A combinational cell whose output is a plain data value.
bool data_cell(const hhds::Node_class& n) {
  if (n.is_invalid() || gu::is_builtin_node(n)) {
    return false;
  }
  const auto op = gu::type_op_of(n);
  return Ntype::is_comb(op) && op != Ntype_op::Sub && op != Ntype_op::Clock_cell && op != Ntype_op::Memory;
}

// A pin that may be handed the rebuilt value: a data operand of a combinational cell or a register's D. A shift
// amount keeps its driver (the blaster recognizes an affine amount by its cells), and so does every clock, reset,
// enable, instance and memory pin.
bool data_reader(const hhds::Pin_class& sink) {
  const auto n  = sink.get_master_node();
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Flop) {
    return sink == gu::find_sink_pin(n, "din");
  }
  if (!data_cell(n)) {
    return false;
  }
  if (op == Ntype_op::SHL || op == Ntype_op::SRA) {
    return sink != gu::find_sink_pin(n, "b");
  }
  return true;
}

// `n` is an And with a non-negative constant operand that clears every bit `known` knows.
bool masks_known(const hhds::Node_class& n, const bit_facts::Known& known) {
  if (gu::type_op_of(n) != Ntype_op::And) {
    return false;
  }
  for (const auto& sink : n.inp_sorted_pins()) {
    for (const auto& drv : sink.get_driver_pins()) {
      if (!drv.is_const()) {
        continue;
      }
      const auto& c = gu::const_of(drv);
      if (c.has_unknowns() || !c.is_integer() || c.is_negative()) {
        continue;
      }
      bool clears = true;
      for (size_t i = 0; i < known.zero.size() && clears; ++i) {
        clears = !(known.zero[i] || known.one[i]) || !c.bit_test(static_cast<int>(i));
      }
      if (clears) {
        return true;
      }
    }
  }
  return false;
}

void known_constants(hhds::Graph& g, Color_boundary_stats& stats) {
  struct Cut {
    hhds::Pin_class                             pin;
    bit_facts::Known                            known;
    std::map<int, std::vector<hhds::Pin_class>> sinks;  // reader color -> its pins
  };
  std::vector<Cut> cuts;
  for (auto node : g.body().nodes()) {
    if (!data_cell(node)) {
      continue;
    }
    const int mine = color_of(node);
    for (auto pin : node.out_sorted_pins()) {
      const int width = gu::bits_of(pin);
      if (width <= 0 || width > kMaxWidth || !gu::is_unsign(pin)) {
        continue;
      }
      Cut cut;
      for (auto edge : pin.out_edges()) {
        if (gu::is_graph_output_pin(edge.sink)) {
          continue;
        }
        const int theirs = color_of(edge.sink.get_master_node());
        if (theirs != mine && data_reader(edge.sink)) {
          cut.sinks[theirs].push_back(edge.sink);
        }
      }
      if (cut.sinks.empty()) {
        continue;
      }
      cut.known       = bit_facts::known_bits(pin, width);
      const auto bits = std::ranges::count(cut.known.zero, true) + std::ranges::count(cut.known.one, true);
      if (bits == 0) {
        continue;
      }
      // A reader that is already `value & keep` with every known bit cleared is this step's own cell from an earlier
      // run (or the source's own mask): it stays, so the step is idempotent.
      for (auto it = cut.sinks.begin(); it != cut.sinks.end();) {
        std::erase_if(it->second, [&](const hhds::Pin_class& sink) { return masks_known(sink.get_master_node(), cut.known); });
        it = it->second.empty() ? cut.sinks.erase(it) : std::next(it);
      }
      if (cut.sinks.empty()) {
        continue;
      }
      cut.pin = pin;
      cuts.push_back(std::move(cut));
    }
  }
  if (cuts.empty()) {
    return;
  }
  // Only a known bit that something reads is worth a cell: the mask the unread-bit step leaves behind makes zeros
  // nobody asks for.
  std::vector<hhds::Pin_class> pins;
  pins.reserve(cuts.size());
  for (const auto& cut : cuts) {
    pins.push_back(cut.pin);
  }
  const auto read = read_bits(&g, pins);
  if (!read) {
    return;
  }
  // Every rewrite keeps every value, so the cuts found on the unmodified graph hold together.
  for (size_t c = 0; c < cuts.size(); ++c) {
    const auto& cut  = cuts[c];
    uint64_t    used = 0;
    for (size_t i = 0; i < cut.known.zero.size() && i < (*read)[c].size(); ++i) {
      used += (cut.known.zero[i] || cut.known.one[i]) && (*read)[c][i] ? 1 : 0;
    }
    if (used == 0) {
      continue;
    }
    ++stats.constant_values;
    stats.constant_bits += used;
    for (const auto& [color, sinks] : cut.sinks) {
      const auto value = bit_facts::known_pin(g, cut.pin, cut.known, color != 0 ? color : -1);
      for (const auto& sink : sinks) {
        sink.del_sink(cut.pin);
        value.connect_sink(sink);
      }
    }
  }
}
}  // namespace

Color_boundary_stats trim_color_boundaries(hhds::Graph* graph) {
  Color_boundary_stats stats;
  if (graph == nullptr) {
    return stats;
  }
  bool colored = false;
  for (auto node : graph->body().nodes()) {
    if (color_of(node) != 0) {
      colored = true;
      break;
    }
  }
  if (!colored) {
    return stats;  // one region: nothing crosses
  }
  known_constants(*graph, stats);
  if (const auto unread = mask_unread_color_outputs(graph)) {
    stats.unread_values = unread->values;
    stats.unread_bits   = unread->bits;
  }
  return stats;
}

}  // namespace livehd::synth
