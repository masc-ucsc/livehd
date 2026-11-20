// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "share_cells.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "node_util.hpp"

namespace livehd::synth {
namespace gu = livehd::graph_util;

namespace {
int color_of(const hhds::Node_class& n) { return gu::has_color(n) ? gu::color_of(n) : 0; }

uint64_t mix(uint64_t a, uint64_t b) {
  a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
  a ^= a >> 29;
  a *= 0xbf58476d1ce4e5b9ULL;
  return a ^ (a >> 32);
}
uint64_t text_hash(std::string_view s) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (const char c : s) {
    h = (h ^ static_cast<unsigned char>(c)) * 0x100000001b3ULL;
  }
  return h;
}

// A cell that computes a plain value from its operands and nothing else: its twin is interchangeable with it.
bool shareable(const hhds::Node_class& n) {
  if (n.is_invalid() || gu::is_builtin_node(n)) {
    return false;
  }
  const auto op = gu::type_op_of(n);
  if (!Ntype::is_comb(op) || op == Ntype_op::Sub || op == Ntype_op::Clock_cell || op == Ntype_op::Memory || op == Ntype_op::Latch
      || op == Ntype_op::AttrSet || op == Ntype_op::IO || op == Ntype_op::Invalid) {
    return false;
  }
  if (n.attr(livehd::attrs::synth_policy).has()) {
    return false;  // a per-cell mapping request stays with its cell
  }
  int outputs = 0;
  for (auto pin : n.out_sorted_pins()) {
    if (pin.get_port_id() != 0) {
      return false;
    }
    ++outputs;
  }
  return outputs == 1;
}

// One operand: where it plugs in and what it is (a constant by value, else the pin standing for the driver).
struct Operand {
  hhds::Port_id   role     = 0;
  bool            constant = false;
  std::string     value;  // the constant's text
  hhds::Pin_class pin;
  uint64_t        hash = 0;
  bool            operator==(const Operand& o) const {
    return role == o.role && constant == o.constant && (constant ? value == o.value : pin == o.pin);
  }
};

struct Shape {
  Ntype_op             op;
  int                  bits   = 0;
  bool                 unsign = true;
  std::vector<Operand> operands;
  uint64_t             hash = 0;
  bool operator==(const Shape& o) const { return op == o.op && bits == o.bits && unsign == o.unsign && operands == o.operands; }
};

// Region dependencies: a -> b when a cell of color b reads a value made in color a. `reaches` is a plain search; the
// graph has one node per color.
class Color_graph {
public:
  void add(int from, int to) {
    if (from != to && edges_[from].insert(to).second) {
      known_.clear();
    }
  }
  bool has(int from, int to) const {
    const auto it = edges_.find(from);
    return it != edges_.end() && it->second.contains(to);
  }
  bool reaches(int from, int to) {
    if (from == to) {
      return true;
    }
    const auto key = (static_cast<uint64_t>(static_cast<uint32_t>(from)) << 32) | static_cast<uint32_t>(to);
    if (const auto it = known_.find(key); it != known_.end()) {
      return it->second;
    }
    absl::flat_hash_set<int> seen{from};
    std::vector<int>         work{from};
    bool                     found = false;
    while (!work.empty() && !found) {
      const int at = work.back();
      work.pop_back();
      if (const auto it = edges_.find(at); it != edges_.end()) {
        for (const int next : it->second) {
          if (next == to) {
            found = true;
            break;
          }
          if (seen.insert(next).second) {
            work.push_back(next);
          }
        }
      }
    }
    known_.emplace(key, found);
    return found;
  }

private:
  absl::flat_hash_map<int, absl::flat_hash_set<int>> edges_;
  absl::flat_hash_map<uint64_t, bool>                known_;
};
}  // namespace

Share_stats share_duplicate_cells(hhds::Graph* graph) {
  Share_stats stats;
  if (graph == nullptr) {
    return stats;
  }
  auto&       g = *graph;
  // The region dependencies as they are, so a shared cell never adds a loop between two colors.
  Color_graph colors;
  bool        colored = false;
  for (auto node : g.body().nodes()) {
    const int mine  = color_of(node);
    colored        |= mine != 0;
    for (auto edge : node.out_edges()) {
      if (!gu::is_graph_output_pin(edge.sink)) {
        colors.add(mine, color_of(edge.sink.get_master_node()));
      }
    }
  }
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> stands_for;  // a removed cell's output -> the one kept
  absl::flat_hash_map<uint64_t, std::vector<std::pair<Shape, hhds::Pin_class>>> seen;
  std::vector<hhds::Node_class>                                                 doomed;
  const auto                                                                    rep = [&](hhds::Pin_class pin) {
    for (auto it = stands_for.find(pin); it != stands_for.end(); it = stands_for.find(pin)) {
      pin = it->second;
    }
    return pin;
  };
  for (auto node : g.body().nodes(hhds::Node_order::forward)) {
    if (!shareable(node)) {
      continue;
    }
    const auto op = gu::type_op_of(node);
    Shape      shape;
    shape.op    = op;
    bool usable = true;
    for (auto sink : node.inp_sorted_pins()) {
      const auto drivers = sink.get_driver_pins();
      if (drivers.size() != 1) {
        usable = false;
        break;
      }
      const auto& drv = drivers.front();
      Operand     operand;
      operand.role = Ntype::sink_bank(op, sink.get_port_id());
      if (drv.is_const()) {
        operand.constant = true;
        operand.value    = std::string{gu::const_of(drv).serialize()};
        operand.hash     = mix(0x4b, text_hash(operand.value));
      } else {
        if (!gu::is_graph_input_pin(drv) && gu::type_op_of(drv.get_master_node()) == Ntype_op::Latch) {
          usable = false;  // a clock gate keeps its own `clk & latch`
          break;
        }
        operand.pin  = rep(drv);
        operand.hash = mix(0x70, absl::Hash<hhds::Pin_class>{}(operand.pin));
      }
      operand.hash = mix(operand.hash, static_cast<uint64_t>(operand.role));
      shape.operands.push_back(std::move(operand));
    }
    if (!usable || shape.operands.empty()) {
      continue;
    }
    const auto out = node.create_driver_pin(0);
    shape.bits     = gu::bits_of(out);
    shape.unsign   = gu::is_unsign(out);
    if (shape.bits <= 0) {
      continue;  // an unstamped width is not a property two cells share
    }
    if (Ntype::is_banked_sink_op(op)) {
      std::ranges::stable_sort(shape.operands, [](const Operand& a, const Operand& b) { return a.hash < b.hash; });
    }
    shape.hash = mix(static_cast<uint64_t>(op), (static_cast<uint64_t>(shape.bits) << 1) | (shape.unsign ? 1U : 0U));
    for (const auto& operand : shape.operands) {
      shape.hash = mix(shape.hash, operand.hash);
    }
    auto&           bucket = seen[shape.hash];
    hhds::Pin_class kept;
    for (const auto& [other, pin] : bucket) {
      if (other == shape) {
        kept = pin;
        break;
      }
    }
    if (kept.is_invalid()) {
      bucket.emplace_back(std::move(shape), out);
      continue;
    }
    // Its readers will read `kept`: a new dependency of this cell's color on kept's color.
    const int from = color_of(kept.get_master_node());
    const int to   = color_of(node);
    if (colored && from != to && !colors.has(from, to) && colors.reaches(to, from)) {
      ++stats.refused;
      continue;
    }
    colors.add(from, to);
    // ... and of every other color that read this cell.
    bool loop = false;
    for (auto edge : out.out_edges()) {
      if (gu::is_graph_output_pin(edge.sink)) {
        continue;
      }
      const int reader = color_of(edge.sink.get_master_node());
      if (colored && reader != from && !colors.has(from, reader) && colors.reaches(reader, from)) {
        loop = true;
        break;
      }
    }
    if (loop) {
      ++stats.refused;
      continue;
    }
    for (auto edge : out.out_edges()) {
      if (!gu::is_graph_output_pin(edge.sink)) {
        colors.add(from, color_of(edge.sink.get_master_node()));
      }
    }
    stands_for.emplace(out, kept);
    doomed.push_back(node);
    ++stats.cells;
    stats.cross_color += from != to ? 1 : 0;
  }
  if (doomed.empty()) {
    return stats;
  }
  for (const auto& node : doomed) {
    const auto                   out  = node.create_driver_pin(0);
    const auto                   kept = rep(out);
    std::vector<hhds::Pin_class> readers;
    for (auto edge : out.out_edges()) {
      readers.push_back(edge.sink);
    }
    for (const auto& sink : readers) {
      sink.del_sink(out);
      kept.connect_sink(sink);
    }
    gu::drop_drivers(node);
  }
  g.delete_nodes(doomed);
  return stats;
}

}  // namespace livehd::synth
