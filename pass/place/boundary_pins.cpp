// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "boundary_pins.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>

#include "pin_adjust.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
using Point = Busorder_point;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
struct Pin {
  int    node;
  double x, y;
  int    original = -1;
};
int append_anchor(dp::Design& d, Point p) {
  const int node = d.node_x.size();
  d.node_x.push_back(p.first);
  d.node_y.push_back(p.second);
  d.node_size_x.push_back(0);
  d.node_size_y.push_back(0);
  const auto append = [](auto& values, auto value) {
    if (!values.empty()) {
      values.push_back(value);
    }
  };
  append(d.node_centroid_color, -1);
  append(d.centroid_node_area, 0.);
  append(d.node_macro, 0);
  append(d.node_align_class, -1);
  append(d.node_snap_class, -1);
  append(d.node_blocked_capacity_h, 0.);
  append(d.node_blocked_capacity_v, 0.);
  append(d.node_legalize_weights, 1.);
  append(d.node_legalize_last, 0);
  append(d.node_legalize_anchor_x, p.first);
  append(d.node_legalize_anchor_y, p.second);
  append(d.node_halo_x, 0.);
  append(d.node_halo_y, 0.);
  append(d.node_pad_x, 0.);
  append(d.node_pad_y, 0.);
  append(d.node_orientation, dp::Orientation::N);
  append(d.node_inflatable, 0);
  if (!d.node_boxes.empty()) {
    d.node_boxes.push_back({p.first, p.second, p.first, p.second});
  }
  return node;
}
}  // namespace

void validate_boundary_pin_options(const Boundary_pin_options& o) {
  if (o.align && !o.split) {
    throw std::invalid_argument("pinalign requires split boundary nets");
  }
  if (!o.iterations || o.iterations > 2000 || !std::isfinite(o.max_displacement_um) || o.max_displacement_um <= 0) {
    throw std::invalid_argument("invalid boundary-pin iteration/displacement controls");
  }
}

Boundary_pin_model split_boundary_nets(const dp::Design& original, const std::vector<Busorder_rect>& mesh,
                                       const Busorder_route& router) {
  original.validate();
  if (mesh.empty() || !router) {
    throw std::invalid_argument("boundary pins require a mesh and RSMT router");
  }
  Boundary_pin_model result;
  result.design = original;
  auto& d       = result.design;
  d.pin2node.clear();
  d.pin2net.clear();
  d.pin_offset_x.clear();
  d.pin_offset_y.clear();
  d.flat_net2pin.clear();
  d.flat_net2pin_start = {0};
  d.flat_node2pin.clear();
  d.flat_node2pin_start.clear();
  d.net_weights.clear();
  d.net_mask.clear();
  std::set<double> cuts[2];
  for (const auto& r : mesh) {
    cuts[0].insert({r.xl, r.xh});
    cuts[1].insert({r.yl, r.yh});
  }
  const auto region = [&](Point p) {
    for (size_t i = 0; i < mesh.size(); ++i) {
      const auto& r = mesh[i];
      if (r.xl <= p.first && p.first < r.xh && r.yl <= p.second && p.second < r.yh) {
        return int(i);
      }
    }
    return -1;  // Exterior pieces retain original fixed IO terminals.
  };
  const auto enabled = [&](int net) { return original.net_mask.empty() || original.net_mask[net]; };
  const auto real_pin
      = [&](int pin) { return Pin{original.pin2node[pin], original.pin_offset_x[pin], original.pin_offset_y[pin], pin}; };
  const auto emit = [&](int net, const std::vector<Pin>& pins, int box = -1) {
    if (pins.empty()) {
      return;
    }
    for (const auto& pin : pins) {
      d.flat_net2pin.push_back(d.pin2node.size());
      d.pin2node.push_back(pin.node);
      d.pin_offset_x.push_back(pin.x);
      d.pin_offset_y.push_back(pin.y);
      result.pin_is_driver.push_back(pin.original == original.flat_net2pin[original.flat_net2pin_start[net]]);
    }
    d.flat_net2pin_start.push_back(d.pin2node.size());
    d.net_weights.push_back(original.net_weights.empty() ? 1 : original.net_weights[net]);
    d.net_mask.push_back(enabled(net) && pins.size() > 1);
    result.parent_net.push_back(net);
    result.piece_region.push_back(box);
  };
  const int nets = original.flat_net2pin_start.size() - 1;
  for (int net = 0; net < nets; ++net) {
    const int        begin = original.flat_net2pin_start[net], end = original.flat_net2pin_start[net + 1];
    std::vector<int> real;
    for (int k = begin; k < end; ++k) {
      real.push_back(original.flat_net2pin[k]);
    }
    if (!enabled(net) || real.size() < 2 || real.size() > 1000) {
      if (enabled(net) && real.size() > 1000) {
        ++result.skipped_high_degree;
      }
      std::vector<Pin> pins;
      for (int pin : real) {
        pins.push_back(real_pin(pin));
      }
      emit(net, pins);
      continue;
    }
    std::vector<Point> terminals;
    for (int pin : real) {
      const auto p = real_pin(pin);
      terminals.emplace_back(original.node_x[p.node] + p.x, original.node_y[p.node] + p.y);
    }
    std::map<Point, int>       points;
    std::vector<Point>         coordinates;
    std::vector<std::set<int>> regions;
    const auto                 point = [&](Point p) {
      auto [it, added] = points.emplace(p, points.size());
      if (added) {
        coordinates.push_back(p);
        regions.emplace_back();
      }
      return it->second;
    };
    std::map<std::pair<int, int>, int> vertices;
    std::vector<int>                   parent;
    const auto                         vertex = [&](int box, int p) {
      auto [it, added] = vertices.emplace(std::pair{box, p}, vertices.size());
      if (added) {
        parent.push_back(it->second);
      }
      regions[p].insert(box);
      return it->second;
    };
    const auto root = [&](int v) {
      while (parent[v] != v) {
        parent[v] = parent[parent[v]];
        v         = parent[v];
      }
      return v;
    };
    const auto segment = [&](Point a, Point b) {
      if (a == b) {
        return;
      }
      const bool horizontal = a.second == b.second;
      if (!horizontal && a.first != b.first) {
        throw std::runtime_error("nonrectilinear boundary segment");
      }
      if (b < a) {
        std::swap(a, b);
      }
      const int    axis = horizontal ? 0 : 1;
      const double lo = horizontal ? a.first : a.second, hi = horizontal ? b.first : b.second;
      Point        prior = a;
      const auto   join  = [&](Point next) {
        const int box  = region({(prior.first + next.first) / 2, (prior.second + next.second) / 2});
        const int from = vertex(box, point(prior)), to = vertex(box, point(next));
        parent[root(from)] = root(to);
        prior              = next;
      };
      for (auto it = cuts[axis].upper_bound(lo); it != cuts[axis].end() && *it < hi; ++it) {
        join(horizontal ? Point{*it, a.second} : Point{a.first, *it});
      }
      join(b);
    };
    for (auto [a, b] : router(terminals)) {
      if (b < a) {
        std::swap(a, b);
      }
      Point bend{b.first, a.second};
      segment(a, bend);
      segment(bend, b);
    }
    std::map<int, std::vector<Pin>> pieces;
    for (size_t i = 0; i < real.size(); ++i) {
      const Point p{double(std::llround(terminals[i].first)), double(std::llround(terminals[i].second))};
      const int   index = point(p);
      int         box   = region(p);
      if (!regions[index].empty() && !regions[index].contains(box)) {
        box = *regions[index].begin();
      }
      pieces[root(vertex(box, index))].push_back(real_pin(real[i]));
    }
    const size_t old_anchors = result.anchors.size();
    for (size_t i = 0; i < coordinates.size(); ++i) {
      if (regions[i].size() < 2) {
        continue;
      }
      const int node = append_anchor(d, coordinates[i]);
      result.anchors.push_back({node, net, coordinates[i].first, coordinates[i].second});
      for (int box : regions[i]) {
        pieces[root(vertices.at({box, int(i)}))].push_back({node, 0, 0});
      }
    }
    if (old_anchors == result.anchors.size()) {
      // No boundary crossing: preserve the original hypernet exactly.
      std::vector<Pin> pins;
      for (int pin : real) {
        pins.push_back(real_pin(pin));
      }
      emit(net, pins);
    } else {
      ++result.split_nets;
      std::map<int, int> component_region;
      for (const auto& [key, value] : vertices) {
        component_region[root(value)] = key.first;
      }
      for (const auto& [component, pins] : pieces) {
        emit(net, pins, component_region.at(component));
      }
    }
  }
  d.build_maps();
  d.validate();
  return result;
}

Boundary_pin_order_stats order_boundary_pins(Boundary_pin_model& model, const std::vector<Busorder_rect>& mesh,
                                             const std::vector<Bus_bit>& identities, const std::vector<int>& macros) {
  const auto               start = Clock::now();
  auto&                    d     = model.design;
  Boundary_pin_order_stats stats;
  for (int node : macros) {
    if (node < d.num_movable || node >= int(d.node_x.size())) {
      throw std::invalid_argument("boundary-pin obstacle must be a fixed real macro");
    }
  }
  const auto inside_macro = [&](double x, double y) {
    return std::any_of(macros.begin(), macros.end(), [&](int n) {
      return d.node_x[n] < x && x < d.node_x[n] + d.node_size_x[n] && d.node_y[n] < y && y < d.node_y[n] + d.node_size_y[n];
    });
  };
  for (const auto& pin : model.anchors) {
    stats.macro_pins_before += inside_macro(pin.x, pin.y);
  }
  stats.before = dp::hpwl(d);
  model.edges  = busorder_edges(mesh);
  std::map<int, std::vector<int>> by_edge;
  for (size_t i = 0; i < model.anchors.size(); ++i) {
    auto& pin = model.anchors[i];
    pin.edge  = -1;
    for (size_t k = 0; k < model.edges.size(); ++k) {
      const auto& e      = model.edges[k];
      double      normal = e.axis == 0 ? pin.x : pin.y, t = e.axis == 0 ? pin.y : pin.x;
      if (normal == e.position && t > e.low && t < e.high) {
        if (pin.edge >= 0) {
          pin.edge = -1;
          break;
        }
        pin.edge = k;
      }
    }
    if (pin.edge < 0) {
      ++stats.corner_pins;
    } else {
      by_edge[pin.edge].push_back(i);
    }
  }
  const double pitch    = std::max(1., std::ceil(d.layout.site_width));
  const auto   identity = [&](int anchor) {
    int net = model.anchors[anchor].original_net;
    return net < int(identities.size()) ? identities[net] : Bus_bit{};
  };
  for (const auto& [edge_id, anchors] : by_edge) {
    const auto& edge       = model.edges[edge_id];
    const auto  coordinate = [&](int anchor) {
      const auto& p = model.anchors[anchor];
      return edge.axis == 0 ? p.y : p.x;
    };
    const auto install = [&](int anchor, double t) {
      auto& p = model.anchors[anchor];
      if (edge.axis == 0) {
        p.y = t;
      } else {
        p.x = t;
      }
      d.node_x[p.node] = p.x;
      d.node_y[p.node] = p.y;
      if (!d.node_boxes.empty()) {
        d.node_boxes[p.node] = {p.x, p.y, p.x, p.y};
      }
    };
    std::map<std::string, std::vector<int>> groups;
    for (int p : anchors) {
      auto bit = identity(p);
      groups[bit.bit >= 0 ? bit.bus : "~net_" + std::to_string(model.anchors[p].original_net)].push_back(p);
    }
    const auto ordered = [&](const std::vector<int>& group) {
      bool up = true, down = true;
      for (size_t k = 1; k < group.size(); ++k) {
        up   &= coordinate(group[k - 1]) < coordinate(group[k]);
        down &= coordinate(group[k - 1]) > coordinate(group[k]);
      }
      return up || down;
    };
    std::set<std::string> ambiguous;
    for (auto& [name, group] : groups) {
      std::sort(group.begin(), group.end(), [&](int a, int b) { return identity(a).bit < identity(b).bit; });
      for (size_t k = 1; k < group.size(); ++k) {
        if (identity(group[k - 1]).bit == identity(group[k]).bit) {
          ambiguous.insert(name);
        }
      }
      if (ambiguous.contains(name)) {
        ++stats.ambiguous_groups;
      } else if (group.size() > 1) {
        ++stats.groups;
        stats.unordered_before += !ordered(group);
      }
    }
    std::vector<double> slots;
    for (double t = std::ceil((edge.low + pitch) / pitch) * pitch; t <= edge.high - pitch; t += pitch) {
      slots.push_back(t);
    }
    std::vector<bool> blocked(slots.size());
    // A rectangle edge may extend through a macro after mesh pruning. Keep
    // slots on macro boundaries usable, but never select their interiors.
    for (size_t k = 0; k < slots.size(); ++k) {
      blocked[k] = inside_macro(edge.axis == 0 ? edge.position : slots[k], edge.axis == 0 ? slots[k] : edge.position);
    }

    const auto reserve = [&](double t) {
      auto begin = std::upper_bound(slots.begin(), slots.end(), t - pitch + .1);
      auto end   = std::lower_bound(slots.begin(), slots.end(), t + pitch - .1);
      for (auto it = begin; it != end; ++it) {
        blocked[it - slots.begin()] = true;
      }
    };
    for (const auto& name : ambiguous) {
      for (int p : groups.at(name)) {
        reserve(coordinate(p));
      }
    }
    std::map<int, double> saved;
    for (int p : anchors) {
      saved[p] = coordinate(p);
    }
    bool feasible = slots.size() >= anchors.size();
    for (const auto& [name, group] : groups) {
      if (!feasible) {
        break;
      }
      if (ambiguous.contains(name)) {
        continue;
      }
      struct Interval {
        double lo, hi, weight;
      };
      std::map<int, std::vector<Interval>> bounds;
      for (int a : group) {
        const int     node = model.anchors[a].node;
        std::set<int> nets;
        for (int k = d.flat_node2pin_start[node]; k < d.flat_node2pin_start[node + 1]; ++k) {
          nets.insert(d.pin2net[d.flat_node2pin[k]]);
        }
        for (int net : nets) {
          if (!d.net_mask[net]) {
            continue;
          }
          double lo = INFINITY, hi = -INFINITY;
          for (int k = d.flat_net2pin_start[net]; k < d.flat_net2pin_start[net + 1]; ++k) {
            int pin = d.flat_net2pin[k], n = d.pin2node[pin];
            if (n == node) {
              continue;
            }
            double t = edge.axis == 0 ? d.node_y[n] + d.pin_offset_y[pin] : d.node_x[n] + d.pin_offset_x[pin];
            lo       = std::min(lo, t);
            hi       = std::max(hi, t);
          }
          if (std::isfinite(lo)) {
            bounds[a].push_back({lo, hi, d.net_weights[net]});
          }
        }
      }
      const auto cost = [&](int a, size_t slot) {
        if (blocked[slot]) {
          return double(INFINITY);
        }
        double value = 1e-9 * std::abs(slots[slot] - saved.at(a));
        for (const auto& b : bounds[a]) {
          value += b.weight * (std::max(b.hi, slots[slot]) - std::min(b.lo, slots[slot]));
        }
        return value;
      };
      double           best = INFINITY;
      std::vector<int> chosen, sequence;
      for (bool reverse : {false, true}) {
        auto order = group;
        if (reverse) {
          std::reverse(order.begin(), order.end());
        }
        auto assignment
            = ordered_pin_slots(order.size(), slots.size(), [&](size_t k, size_t slot) { return cost(order[k], slot); });
        if (assignment.empty()) {
          continue;
        }
        double value = 0;
        for (size_t k = 0; k < order.size(); ++k) {
          value += cost(order[k], assignment[k]);
        }
        if (value < best) {
          best     = value;
          chosen   = std::move(assignment);
          sequence = std::move(order);
        }
      }
      if (chosen.empty()) {
        feasible = false;
        break;
      }
      for (size_t k = 0; k < chosen.size(); ++k) {
        install(sequence[k], slots[chosen[k]]);
        reserve(slots[chosen[k]]);
      }
    }
    if (!feasible) {
      ++stats.capacity_edges;
      for (const auto& [a, t] : saved) {
        install(a, t);
      }
    }
    for (const auto& [name, group] : groups) {
      if (!ambiguous.contains(name) && group.size() > 1) {
        stats.unordered_after += !ordered(group);
      }
    }
    for (const auto& [a, t] : saved) {
      stats.moved += coordinate(a) != t;
    }
  }
  for (const auto& pin : model.anchors) {
    stats.macro_pins_after += inside_macro(pin.x, pin.y);
  }
  stats.after   = dp::hpwl(d);
  stats.seconds = elapsed(start);
  d.validate();
  return stats;
}

Boundary_pins::Boundary_pins(std::vector<Busorder_rect> boxes, Busorder_route router, double units, Boundary_pin_options policy,
                             std::string directory, std::vector<Bus_bit> bus_bits, std::vector<int> macro_nodes)
    : options(policy)
    , mesh(std::move(boxes))
    , route(std::move(router))
    , dbu(units)
    , output(std::move(directory))
    , identities(std::move(bus_bits))
    , macros(std::move(macro_nodes)) {
  validate_boundary_pin_options(options);
  if (!std::isfinite(dbu) || dbu <= 0) {
    throw std::invalid_argument("invalid boundary-pin units");
  }
}

void Boundary_pins::refine(dp::Design& physical, const dp::Options& base) {
  const auto start = Clock::now();
  measure_options  = base;
  original_nodes   = physical.node_x.size();
  original_pins    = physical.pin2node.size();
  original_nets    = physical.flat_net2pin_start.size() - 1;
  observe(physical, "before");
  if (options.split) {
    model = split_boundary_nets(physical, mesh, route);
  } else {
    model.design = physical;
  }
  original_anchors = model.anchors;
  if (options.split && options.order) {
    ordering = order_boundary_pins(model, mesh, identities, macros);
  }
  if (options.split && options.align) {
    alignment = align_boundary_pins(model, mesh, identities, macros);
  }
  build_seconds            = elapsed(start);
  auto&               work = model.design;
  std::vector<double> criticality(original_nodes, 1);
  for (size_t net = 0; net < physical.net_weights.size(); ++net) {
    if (physical.net_mask.empty() || physical.net_mask[net]) {
      for (int k = physical.flat_net2pin_start[net]; k < physical.flat_net2pin_start[net + 1]; ++k) {
        const int node    = physical.pin2node[physical.flat_net2pin[k]];
        criticality[node] = std::max(criticality[node], physical.net_weights[net]);
      }
    }
  }
  if (work.node_boxes.empty()) {
    work.node_boxes.assign(work.node_x.size(), {work.layout.xl, work.layout.yl, work.layout.xh, work.layout.yh});
  }
  for (int i = 0; i < work.num_movable; ++i) {
    const double reach = options.max_displacement_um * dbu / std::sqrt(criticality[i]);
    auto&        box   = work.node_boxes[i];
    box.xl             = std::max(box.xl, work.node_x[i] - reach);
    box.xh             = std::min(box.xh, work.node_x[i] + reach);
    box.yl             = std::max(box.yl, work.node_y[i] - reach);
    box.yh             = std::min(box.yh, work.node_y[i] + reach);
  }
  auto control           = base;
  control.max_iterations = control.min_iterations = control.schedule_iterations = options.iterations;
  control.post_density_factor                                                   = 0;
  control.wirelength_gamma_start                                                = control.wirelength_gamma_end;
  control.max_step                                                              = std::min(control.max_step, .05);
  control.legalize = control.detailed_place = false;
  control.before_legalize                   = {};
  control.after_macro_legalize              = {};
  control.observer                          = {};
  control.progress_observer                 = {};
  control.checkpoint                        = {};
  control.checkpoint_interval               = 0;
  control.checkpoint_final                  = false;
  control.checkpoint_after_density          = false;
  control.terminal_update                   = {};
  control.terminal_interval                 = 0;
  control.macro_pull_update                 = {};
  control.macro_pull_interval               = 0;
  auto actual                               = physical;
  control.observer_interval                 = 10;
  control.observer                          = [&](const dp::Design& snapshot, dp::PlacementStage stage, int iteration) {
    if (stage != dp::PlacementStage::global_placement) {
      return;
    }
    for (int i = 0; i < actual.num_movable; ++i) {
      actual.node_x[i] = snapshot.node_x[i];
      actual.node_y[i] = snapshot.node_y[i];
    }
    history.push_back({"iteration",
                       unsigned(iteration),
                       dp::hpwl(actual),
                       dp::density_overflow(actual, base.bins_x, base.bins_y, base.target_density, base.scale_macro_density),
                       dp::hpwl(snapshot),
                       elapsed(start)});
  };
  // Keep the last settled candidate, even if its metrics regress: this
  // experiment measures the overhead of pins, without an ordering selector.
  dp::GpState state;
  const auto  result = dp::place(work, control, &state);
  for (const auto& pin : model.anchors) {
    if (work.node_x[pin.node] != pin.x || work.node_y[pin.node] != pin.y) {
      throw std::runtime_error("virtual boundary pin moved");
    }
  }
  for (int i = 0; i < physical.num_movable; ++i) {
    physical.node_x[i] = work.node_x[i];
    physical.node_y[i] = work.node_y[i];
  }
  observe(physical, "after_gp");
  history.back().iteration  = result.iterations;
  history.back().seconds    = elapsed(start);
  history.back().split_hpwl = dp::hpwl(work);
  write();
}

void Boundary_pins::observe(const dp::Design& physical, const std::string& stage) {
  history.push_back({stage,
                     0,
                     dp::hpwl(physical),
                     dp::density_overflow(physical,
                                          measure_options.bins_x,
                                          measure_options.bins_y,
                                          measure_options.target_density,
                                          measure_options.scale_macro_density),
                     0,
                     0});
  // The original design is the only object passed to legalization and export.
  if (stage != "before") {
    write();
  }
}

void Boundary_pins::write() const {
  if (output.empty()) {
    return;
  }
  std::filesystem::create_directories(output);
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> j(buffer);
  j.StartObject();
  j.Key("split");
  j.Bool(options.split);
  j.Key("order");
  j.Bool(options.order);
  j.Key("ordering");
  j.StartObject();
  j.Key("groups");
  j.Uint(ordering.groups);
  j.Key("unordered_before");
  j.Uint(ordering.unordered_before);
  j.Key("unordered_after");
  j.Uint(ordering.unordered_after);
  j.Key("ambiguous_groups");
  j.Uint(ordering.ambiguous_groups);
  j.Key("capacity_edges");
  j.Uint(ordering.capacity_edges);
  j.Key("corner_pins");
  j.Uint(ordering.corner_pins);
  j.Key("macro_pins_before");
  j.Uint(ordering.macro_pins_before);
  j.Key("macro_pins_after");
  j.Uint(ordering.macro_pins_after);
  j.Key("moved");
  j.Uint(ordering.moved);
  j.Key("seconds");
  j.Double(ordering.seconds);
  j.Key("weighted_split_hpwl_before");
  j.Double(ordering.before);
  j.Key("weighted_split_hpwl_after");
  j.Double(ordering.after);
  j.EndObject();
  j.Key("pinalign");
  j.StartObject();
  j.Key("enabled");
  j.Bool(options.align);
  j.Key("direct");
  j.Uint(alignment.direct);
  j.Key("one_cell");
  j.Uint(alignment.one_cell);
  j.Key("aligned");
  j.Uint(alignment.aligned);
  j.Key("already_aligned");
  j.Uint(alignment.already_aligned);
  j.Key("newly_aligned");
  j.Uint(alignment.aligned - alignment.already_aligned);
  j.Key("moved");
  j.Uint(alignment.moved);
  j.Key("ambiguous");
  j.Uint(alignment.ambiguous);
  j.Key("order_conflicts");
  j.Uint(alignment.order_conflicts);
  j.Key("spacing_conflicts");
  j.Uint(alignment.spacing_conflicts);
  j.Key("range_conflicts");
  j.Uint(alignment.range_conflicts);
  j.Key("macro_conflicts");
  j.Uint(alignment.macro_conflicts);
  j.Key("pairs");
  j.StartArray();
  for (const auto& p : alignment.pairs) {
    j.StartObject();
    j.Key("input");
    j.Int(p.input);
    j.Key("output");
    j.Int(p.output);
    j.Key("rectangle");
    j.Int(p.rectangle);
    j.Key("cell");
    j.Int(p.cell);
    j.Key("outcome");
    j.String(p.outcome.c_str());
    j.EndObject();
  }
  j.EndArray();
  j.EndObject();
  j.Key("dbu_per_um");
  j.Double(dbu);
  j.Key("iterations");
  j.Uint(options.iterations);
  j.Key("build_seconds");
  j.Double(build_seconds);
  j.Key("original_nodes");
  j.Uint64(original_nodes);
  j.Key("original_pins");
  j.Uint64(original_pins);
  j.Key("original_nets");
  j.Uint64(original_nets);
  j.Key("placement_nets");
  j.Uint64(model.design.net_weights.size());
  j.Key("placement_pins");
  j.Uint64(model.design.pin2node.size());
  j.Key("virtual_pins");
  j.Uint64(model.anchors.size());
  j.Key("split_nets");
  j.Uint(model.split_nets);
  j.Key("skipped_high_degree_nets");
  j.Uint(model.skipped_high_degree);
  j.Key("model");
  j.String(options.order
               ? "Fixed transparent pins optimized and bus-ordered on their original edge; connected per-rectangle placement nets"
               : "Fixed transparent RSMT boundary pins; connected per-rectangle placement nets; no reordering");
  j.Key("rectangles");
  j.StartArray();
  for (const auto& r : mesh) {
    j.StartArray();
    for (double v : {r.xl, r.yl, r.xh, r.yh}) {
      j.Double(v);
    }
    j.EndArray();
  }
  j.EndArray();
  j.Key("history");
  j.StartArray();
  for (const auto& s : history) {
    j.StartObject();
    j.Key("stage");
    j.String(s.stage.c_str());
    j.Key("iteration");
    j.Uint(s.iteration);
    j.Key("real_hpwl");
    j.Double(s.hpwl);
    j.Key("real_overflow");
    j.Double(s.overflow);
    j.Key("split_hpwl");
    j.Double(s.split_hpwl);
    j.Key("seconds");
    j.Double(s.seconds);
    j.EndObject();
  }
  j.EndArray();
  j.EndObject();
  std::ofstream report(output + "/report.json");
  report << buffer.GetString() << '\n';
  std::ofstream pins(output + "/pins.tsv");
  pins << "node\toriginal_net\tx_dbu\ty_dbu\tedge\toriginal_x_dbu\toriginal_y_dbu\n";
  for (const auto& p : model.anchors) {
    const auto& old = original_anchors[&p - model.anchors.data()];
    pins << p.node << '\t' << p.original_net << '\t' << p.x << '\t' << p.y << '\t' << p.edge << '\t' << old.x << '\t' << old.y
         << '\n';
  }
  if (!report || !pins) {
    throw std::runtime_error("cannot write boundary-pin report");
  }
}
}  // namespace livehd::placement
