// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <tuple>

#include "boundary_pins.hpp"
#include "inou/lefdef/index_union_find.hpp"

namespace livehd::placement {
Pin_align_stats align_boundary_pins(Boundary_pin_model& model, const Region_mesh& mesh, const std::vector<Bus_bit>& identities,
                                    const std::vector<int>& macros) {
  auto&           d = model.design;
  Pin_align_stats stats;
  const int       nets = d.net_weights.size(), count = model.anchors.size();
  if (model.piece_region.size() != size_t(nets) || model.pin_is_driver.size() != d.pin2node.size()) {
    throw std::invalid_argument("pinalign requires directed split-net metadata");
  }
  model.edges = mesh.edges;
  std::vector<int>              node_anchor(d.node_x.size(), -1);
  std::vector<std::vector<int>> edge_pins(model.edges.size()), incident(count), terminals(nets), real(nets);
  for (int i = 0; i < count; ++i) {
    auto& p             = model.anchors[i];
    node_anchor[p.node] = i;
    p.edge              = -1;
    for (size_t k = 0; k < model.edges.size(); ++k) {
      const auto&  e      = model.edges[k];
      const double normal = e.axis == 0 ? p.x : p.y, t = e.axis == 0 ? p.y : p.x;
      if (normal == e.position && e.low < t && t < e.high) {
        if (p.edge >= 0) {
          p.edge = -1;
          break;
        }
        p.edge = k;
      }
    }
    if (p.edge >= 0) {
      edge_pins[p.edge].push_back(i);
    }
  }
  std::vector<int> distance(nets, -1);
  std::queue<int>  queue;
  for (int n = 0; n < nets; ++n) {
    if (!d.net_mask[n]) {
      continue;
    }
    for (int k = d.flat_net2pin_start[n]; k < d.flat_net2pin_start[n + 1]; ++k) {
      const int p = d.flat_net2pin[k], a = node_anchor[d.pin2node[p]];
      if (a >= 0) {
        terminals[n].push_back(a);
      } else {
        real[n].push_back(p);
      }
      if (model.pin_is_driver[p] && distance[n] < 0) {
        distance[n] = 0;
        queue.push(n);
      }
    }
    std::sort(terminals[n].begin(), terminals[n].end());
    terminals[n].erase(std::unique(terminals[n].begin(), terminals[n].end()), terminals[n].end());
    for (int a : terminals[n]) {
      incident[a].push_back(n);
    }
  }
  // Orient the RSMT component graph from the original driver, never from XY.
  while (!queue.empty()) {
    const int n = queue.front();
    queue.pop();
    for (int a : terminals[n]) {
      for (int next : incident[a]) {
        if (distance[next] < 0) {
          distance[next] = distance[n] + 1;
          queue.push(next);
        }
      }
    }
  }
  const auto opposite = [&](int input, int output, int box) {
    const auto& a = model.anchors[input];
    const auto& b = model.anchors[output];
    if (a.edge < 0 || b.edge < 0 || box < 0 || box >= int(mesh.rects.size())) {
      return false;
    }
    const auto& ea = model.edges[a.edge];
    const auto& eb = model.edges[b.edge];
    if (ea.axis != eb.axis) {
      return false;
    }
    const auto&  r  = mesh.rects[box];
    const double lo = ea.axis == 0 ? r.xl : r.yl, hi = ea.axis == 0 ? r.xh : r.yh;
    return (ea.position == lo && eb.position == hi) || (ea.position == hi && eb.position == lo);
  };
  std::set<std::tuple<int, int, int, int, int, int>> candidates;
  struct Cell_paths {
    std::vector<std::pair<int, int>> inputs, outputs;  // anchor and physical cell pin
  };
  std::map<std::pair<int, int>, Cell_paths> cells;
  for (int n = 0; n < nets; ++n) {
    const int box = model.piece_region[n];
    if (distance[n] < 0 || box < 0 || box >= int(mesh.rects.size())) {
      continue;
    }
    std::vector<int> inputs, outputs;
    for (int a : terminals[n]) {
      bool upstream = false, downstream = false;
      for (int other : incident[a]) {
        if (other != n) {
          upstream   |= distance[other] >= 0 && distance[other] < distance[n];
          downstream |= distance[other] > distance[n];
        }
      }
      if (upstream) {
        inputs.push_back(a);
      } else if (downstream) {
        outputs.push_back(a);
      }
    }
    if (real[n].empty() && inputs.size() == 1) {
      for (int b : outputs) {
        if (opposite(inputs[0], b, box)) {
          candidates.emplace(inputs[0], b, box, -1, -1, -1);
        }
      }
    }
    if (real[n].size() != 1) {
      continue;
    }
    const int pin = real[n][0], node = d.pin2node[pin];
    if (node >= d.num_movable || (!d.node_macro.empty() && d.node_macro[node])) {
      continue;
    }
    const auto&  r = mesh.rects[box];
    const double x = d.node_x[node] + d.node_size_x[node] / 2, y = d.node_y[node] + d.node_size_y[node] / 2;
    if (!(r.xl <= x && x < r.xh && r.yl <= y && y < r.yh)) {
      continue;
    }
    auto& paths = cells[{box, node}];
    if (model.pin_is_driver[pin]) {
      for (int a : outputs) {
        paths.outputs.emplace_back(a, pin);
      }
    } else if (inputs.size() == 1) {
      paths.inputs.emplace_back(inputs[0], pin);
    }
  }
  for (const auto& [key, paths] : cells) {
    for (auto [a, input_pin] : paths.inputs) {
      for (auto [b, output_pin] : paths.outputs) {
        if (opposite(a, b, key.first)) {
          candidates.emplace(a, b, key.first, key.second, input_pin, output_pin);
        }
      }
    }
  }
  physical::Index_union_find sets(count);
  for (auto [a, b, box, cell, input_pin, output_pin] : candidates) {
    Pin_align_pair pair{a, b, box, cell, {}, input_pin, output_pin};
    if (cell >= 0) {
      // Native offsets already include the physical cell orientation.
      const bool   vertical_edge = model.edges[model.anchors[a].edge].axis == 0;
      const auto&  offsets       = vertical_edge ? d.pin_offset_y : d.pin_offset_x;
      const double origin        = vertical_edge ? d.node_y[cell] : d.node_x[cell];
      pair.input_target          = origin + offsets[input_pin];
      pair.output_target         = origin + offsets[output_pin];
    }
    stats.pairs.push_back(pair);
    if (cell < 0) {
      ++stats.direct;
    } else {
      ++stats.one_cell;
    }
    sets.merge(a, b);
  }
  std::map<size_t, std::vector<int>> components;
  for (size_t i = 0; i < stats.pairs.size(); ++i) {
    components[sets.find(stats.pairs[i].input)].push_back(i);
  }
  const auto coord = [&](int a) {
    const auto& p = model.anchors[a];
    return model.edges[p.edge].axis == 0 ? p.y : p.x;
  };
  constexpr double tolerance = 1e-6;  // native coordinates; tolerate floating point sums
  const auto       aligned   = [&](const Pin_align_pair& p) {
    return p.cell < 0
                       ? std::abs(coord(p.input) - coord(p.output)) <= tolerance
                       : std::abs(coord(p.input) - p.input_target) <= tolerance && std::abs(coord(p.output) - p.output_target) <= tolerance;
  };
  std::vector<bool> was_aligned;
  for (const auto& p : stats.pairs) {
    was_aligned.push_back(aligned(p));
  }
  const double pitch = std::max(1., std::ceil(d.layout.site_width));
  for (const auto& [component, links] : components) {
    (void)component;
    std::map<int, std::vector<int>> next;  // outgoing pair indices
    std::map<int, int>              degree;
    std::map<int, double>           targets;
    std::set<int>                   touched;
    std::string                     failure;
    const auto                      constrain = [&](int anchor, double coordinate) {
      const auto [it, inserted] = targets.try_emplace(anchor, coordinate);
      if (!inserted && std::abs(it->second - coordinate) > tolerance) {
        failure = "ambiguous";
      }
    };
    for (int k : links) {
      const auto& p = stats.pairs[k];
      degree.try_emplace(p.input, 0);
      ++degree[p.output];
      next[p.input].push_back(k);
      touched.insert(model.anchors[p.input].edge);
      touched.insert(model.anchors[p.output].edge);
      if (p.cell >= 0) {
        constrain(p.input, p.input_target);
        constrain(p.output, p.output_target);
      }
    }
    std::queue<int> ready;
    for (auto [a, incoming] : degree) {
      if (!incoming) {
        ready.push(a);
        // Cell-connected inputs may move; a direct-wire-only root stays fixed.
        targets.try_emplace(a, coord(a));
      }
    }
    unsigned visited   = 0;
    auto     remaining = degree;
    while (!ready.empty()) {
      int a = ready.front();
      ready.pop();
      ++visited;
      for (int k : next[a]) {
        const auto& p = stats.pairs[k];
        if (p.cell < 0) {
          constrain(p.output, targets.at(a));
        }
        if (!--remaining[p.output]) {
          ready.push(p.output);
        }
      }
    }
    if (visited != degree.size()) {
      failure = "ambiguous";
    }
    const auto proposed = [&](int a) {
      const auto it = targets.find(a);
      return it == targets.end() ? coord(a) : it->second;
    };
    const auto changed = [&](int a) { return proposed(a) != coord(a); };
    if (failure.empty()) {
      for (auto [a, incoming] : degree) {
        (void)incoming;
        if (!changed(a)) {
          continue;
        }
        const auto&  e      = model.edges[model.anchors[a].edge];
        const double target = proposed(a);
        if (target < e.low + pitch || target > e.high - pitch) {
          failure = "range";
          break;
        }
        const double x = e.axis == 0 ? e.position : target, y = e.axis == 0 ? target : e.position;
        for (int n : macros) {
          if (n < d.num_movable || n >= int(d.node_x.size())) {
            throw std::invalid_argument("invalid pinalign macro");
          }
          if (d.node_x[n] < x && x < d.node_x[n] + d.node_size_x[n] && d.node_y[n] < y && y < d.node_y[n] + d.node_size_y[n]) {
            failure = "macro";
            break;
          }
        }
        if (!failure.empty()) {
          break;
        }
      }
    }
    // Preserve the existing relative order of every bus on each affected edge,
    // including a bus that was already ambiguous before this optional pass.
    if (failure.empty()) {
      for (int edge : touched) {
        std::map<std::string, std::vector<int>> buses;
        for (int a : edge_pins[edge]) {
          int net = model.anchors[a].original_net;
          if (net >= 0 && net < int(identities.size()) && identities[net].bit >= 0) {
            buses[identities[net].bus].push_back(a);
          }
        }
        for (auto& [name, pins] : buses) {
          (void)name;
          std::sort(pins.begin(), pins.end(), [&](int a, int b) { return std::pair{coord(a), a} < std::pair{coord(b), b}; });
          for (size_t k = 1; k < pins.size(); ++k) {
            int a = pins[k - 1], b = pins[k];
            if (coord(a) < coord(b) && proposed(a) >= proposed(b)) {
              failure = "order";
              break;
            }
          }
          if (!failure.empty()) {
            break;
          }
        }
        if (!failure.empty()) {
          break;
        }
        auto pins = edge_pins[edge];
        std::sort(pins.begin(), pins.end(), [&](int a, int b) { return std::pair{proposed(a), a} < std::pair{proposed(b), b}; });
        for (size_t k = 1; k < pins.size(); ++k) {
          int a = pins[k - 1], b = pins[k];
          if ((changed(a) || changed(b)) && proposed(b) - proposed(a) < pitch) {
            failure = "spacing";
            break;
          }
        }
        if (!failure.empty()) {
          break;
        }
      }
    }
    if (failure.empty()) {
      for (auto [a, incoming] : degree) {
        (void)incoming;
        if (changed(a)) {
          const double target = proposed(a);
          auto&        p      = model.anchors[a];
          if (model.edges[p.edge].axis == 0) {
            p.y = target;
          } else {
            p.x = target;
          }
          d.node_x[p.node] = p.x;
          d.node_y[p.node] = p.y;
          if (!d.node_boxes.empty()) {
            d.node_boxes[p.node] = {p.x, p.y, p.x, p.y};
          }
          ++stats.moved;
        }
      }
      stats.aligned += links.size();
      for (int k : links) {
        stats.already_aligned += was_aligned[k];
      }
    } else {
      auto& skipped  = failure == "ambiguous" ? stats.ambiguous
                       : failure == "order"   ? stats.order_conflicts
                       : failure == "spacing" ? stats.spacing_conflicts
                       : failure == "range"   ? stats.range_conflicts
                                              : stats.macro_conflicts;
      skipped       += links.size();
    }
    for (int k : links) {
      stats.pairs[k].outcome = failure.empty() ? "aligned" : failure;
    }
  }
  for (const auto& p : stats.pairs) {
    if (p.outcome == "aligned" && !aligned(p)) {
      throw std::runtime_error("pinalign chain lost an accepted alignment");
    }
  }
  return stats;
}
}  // namespace livehd::placement
