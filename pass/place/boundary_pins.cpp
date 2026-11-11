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

#include "cell_criticality.hpp"
#include "inou/lefdef/index_union_find.hpp"
#include "pin_adjust.hpp"
#include "placement_session.hpp"
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
  append(d.node_placement_mask, DreamPlace::native::PlacementMask::none);
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
  if (o.clean_cuts && !o.split) {
    throw std::invalid_argument("clean region cuts require split boundary nets");
  }
  if (!std::isfinite(o.align_near) || o.align_near < 0) {
    throw std::invalid_argument("invalid pinalign near distance");
  }
  if (!o.iterations || o.iterations > 2000 || !std::isfinite(o.max_displacement_um) || o.max_displacement_um <= 0
      || o.settle_iterations > 2000 || !std::isfinite(o.pitch_um) || o.pitch_um < 0) {
    throw std::invalid_argument("invalid boundary-pin iteration/displacement/pitch controls");
  }
}

unsigned move_dipping_cells(dp::Design& d, const Region_mesh& mesh, double max_move) {
  d.validate();
  std::vector<unsigned> load;
  for (const auto& r : mesh.rects) {
    load.push_back(r.cells);
  }
  unsigned moved = 0;
  for (int c = 0; c < d.num_movable; ++c) {
    if (!d.node_macro.empty() && d.node_macro[c]) {
      continue;
    }
    const double w = d.node_size_x[c], h = d.node_size_y[c], x = d.node_x[c] + w / 2, y = d.node_y[c] + h / 2;
    const auto   home = mesh.rect_of(x, y);
    if (!home) {
      continue;
    }
    const auto& r     = mesh.rects[*home];
    unsigned    sides = 0b1111;  // the sides every other pin is beyond: left, right, below, above
    bool        any   = false;
    for (int k = d.flat_node2pin_start[c]; k < d.flat_node2pin_start[c + 1] && sides; ++k) {
      const int net = d.pin2net[d.flat_node2pin[k]], begin = d.flat_net2pin_start[net], end = d.flat_net2pin_start[net + 1];
      if ((!d.net_mask.empty() && !d.net_mask[net]) || end - begin < 2 || end - begin > 1000) {
        continue;
      }
      for (int q = begin; q < end; ++q) {
        const int pin = d.flat_net2pin[q], node = d.pin2node[pin];
        if (node == c) {
          continue;
        }
        any             = true;
        const double px = d.node_x[node] + d.pin_offset_x[pin], py = d.node_y[node] + d.pin_offset_y[pin];
        sides &= unsigned(px < r.xl) | unsigned(px >= r.xh) << 1 | unsigned(py < r.yl) << 2 | unsigned(py >= r.yh) << 3;
      }
    }
    if (!any || !sides) {
      continue;
    }
    // across the nearest such side, just outside the rectangle
    const double gap[] = {x - r.xl, r.xh - x, y - r.yl, r.yh - y};
    int          side  = -1;
    for (int s = 0; s < 4; ++s) {
      if (sides >> s & 1 && (side < 0 || gap[s] < gap[side])) {
        side = s;
      }
    }
    const double nx = side == 0 ? r.xl - w / 2 - 1 : side == 1 ? r.xh + w / 2 : x;
    const double ny = side == 2 ? r.yl - h / 2 - 1 : side == 3 ? r.yh + h / 2 : y;
    const auto   to = mesh.rect_of(nx, ny);
    if (!to || *to == *home || std::abs(nx - x) + std::abs(ny - y) > max_move || load[*to] >= mesh.options.max_cells) {
      continue;
    }
    d.node_x[c] = nx - w / 2;
    d.node_y[c] = ny - h / 2;
    if (!d.node_boxes.empty()) {
      d.node_boxes[c] = {d.node_x[c], d.node_y[c], d.node_x[c] + w, d.node_y[c] + h};
    }
    --load[*home];
    ++load[*to];
    ++moved;
  }
  return moved;
}

Boundary_pin_model split_boundary_nets(const dp::Design& original, const Region_mesh& mesh, const Busorder_route& router,
                                       bool remove_dips) {
  original.validate();
  if (mesh.rects.empty() || !router) {
    throw std::invalid_argument("boundary pins require a mesh and RSMT router");
  }
  Boundary_pin_model result;
  result.design = original;
  auto& d       = result.design;
  d.pin_timing.reset();  // Physical pin indices do not name pins in the temporary split topology.
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
  for (const auto& r : mesh.rects) {
    cuts[0].insert({r.xl, r.xh});
    cuts[1].insert({r.yl, r.yh});
  }
  // Exterior (-1) pieces retain original fixed IO terminals.
  const auto region  = [&](Point p) { return mesh.rect_of(p.first, p.second).value_or(-1); };
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
    physical::Index_union_find         sets;
    const auto                         vertex = [&](int box, int p) {
      auto [it, added] = vertices.emplace(std::pair{box, p}, vertices.size());
      if (added) {
        sets.add();
      }
      regions[p].insert(box);
      return it->second;
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
        sets.merge(to, from);
        prior = next;
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
    std::vector<int> real_vertex;
    for (size_t i = 0; i < real.size(); ++i) {
      const Point p{double(std::llround(terminals[i].first)), double(std::llround(terminals[i].second))};
      const int   index = point(p);
      int         box   = region(p);
      if (!regions[index].empty() && !regions[index].contains(box)) {
        box = *regions[index].begin();
      }
      real_vertex.push_back(vertex(box, index));
    }
    std::vector<char> dropped(coordinates.size(), 0);  // crossings of removed dips
    if (remove_dips) {
      std::set<size_t> with_pins;
      for (int v : real_vertex) {
        with_pins.insert(sets.find(v));
      }
      std::map<size_t, std::pair<int, std::vector<int>>> crossings;  // pinless piece -> (rectangle, crossing points)
      for (size_t i = 0; i < coordinates.size(); ++i) {
        if (regions[i].size() < 2) {
          continue;
        }
        for (int box : regions[i]) {
          const size_t c = sets.find(vertices.at({box, int(i)}));
          if (box >= 0 && !with_pins.contains(c)) {
            auto& [where, at] = crossings[c];
            where             = box;
            at.push_back(int(i));
          }
        }
      }
      for (const auto& [c, entry] : crossings) {
        const auto& [box, at] = entry;
        bool line_x = true, line_y = true, ok = true, same = true;
        int  other = -2;
        for (int i : at) {
          line_x      &= coordinates[i].first == coordinates[at[0]].first;
          line_y      &= coordinates[i].second == coordinates[at[0]].second;
          const int q  = regions[i].size() == 2 ? *regions[i].begin() == box ? *regions[i].rbegin() : *regions[i].begin() : -2;
          ok          &= !dropped[i] && q != -2;
          same        &= other == -2 || q == other;
          other        = q;
        }
        if (!ok || !(line_x || line_y)) {
          continue;
        }
        // a dip into one neighbor: its crossings join there; a dip between two neighbors (the tree bent below their
        // shared boundary line): the crossings go, the neighbors' pieces stay apart (legroute joins them by a hop)
        for (int i : at) {
          dropped[i] = 1;
          if (same) {
            sets.merge(vertices.at({other, i}), vertices.at({other, at[0]}));
          }
        }
        ++result.dips_removed;
      }
    }
    std::map<size_t, std::vector<Pin>> pieces;
    for (size_t i = 0; i < real.size(); ++i) {
      pieces[sets.find(real_vertex[i])].push_back(real_pin(real[i]));
    }
    const size_t old_anchors = result.anchors.size();
    for (size_t i = 0; i < coordinates.size(); ++i) {
      if (regions[i].size() < 2 || dropped[i]) {
        continue;
      }
      const int node = append_anchor(d, coordinates[i]);
      result.anchors.push_back({node, net, coordinates[i].first, coordinates[i].second});
      for (int box : regions[i]) {
        pieces[sets.find(vertices.at({box, int(i)}))].push_back({node, 0, 0});
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
      std::map<size_t, int> component_region;
      for (const auto& [key, value] : vertices) {
        component_region[sets.find(value)] = key.first;
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

Boundary_pin_order_stats order_boundary_pins(Boundary_pin_model& model, const Region_mesh& mesh,
                                             const std::vector<Bus_bit>& identities, const std::vector<int>& macros,
                                             double requested_pitch) {
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
  model.edges  = mesh.edges;
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
  const double site     = std::max(1., std::ceil(d.layout.site_width));
  const double wanted   = std::max(site, std::round(requested_pitch / site) * site);  // a whole number of sites
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
    std::map<int, double> saved;
    for (int p : anchors) {
      saved[p] = coordinate(p);
    }
    // One assignment of every group on the edge at `pitch`; restores the saved coordinates when it does not fit.
    const auto assign = [&](double pitch) {
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
        // the pull of the pieces each pin joins: the span of their other pins along the edge, so the order (and
        // its direction) follows where the cells already are
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
        for (const auto& [a, t] : saved) {
          install(a, t);
        }
      }
      return feasible;
    };
    double pitch = wanted;
    bool   fits  = assign(pitch);
    while (!fits && pitch > site) {  // a crowded edge keeps the order at a tighter pitch
      pitch = std::max(site, std::round(pitch / 2 / site) * site);
      fits  = assign(pitch);
    }
    if (!fits) {
      ++stats.capacity_edges;
    } else {
      stats.narrowed_edges += pitch < wanted;
      stats.min_pitch       = stats.min_pitch > 0 ? std::min(stats.min_pitch, pitch) : pitch;
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

Boundary_pins::Boundary_pins(const Region_mesh& regions, Busorder_route router, double units, Boundary_pin_options policy,
                             std::string directory, std::vector<Bus_bit> bus_bits, std::vector<int> macro_nodes,
                             std::vector<std::string> net_drivers)
    : options(policy)
    , mesh(regions)
    , route(std::move(router))
    , dbu(units)
    , output(std::move(directory))
    , identities(std::move(bus_bits))
    , macros(std::move(macro_nodes))
    , drivers(std::move(net_drivers)) {
  validate_boundary_pin_options(options);
  if (!std::isfinite(dbu) || dbu <= 0) {
    throw std::invalid_argument("invalid boundary-pin units");
  }
}

void Boundary_pins::refine(dp::Design& physical, const dp::Options& base, Placement_session* session,
                           const std::vector<std::string>& node_names) {
  const auto start = Clock::now();
  measure_options  = base;
  original_nodes   = physical.node_x.size();
  original_pins    = physical.pin2node.size();
  original_nets    = physical.flat_net2pin_start.size() - 1;
  observe(physical, "before");
  if (options.split && options.clean_cuts) {
    moved_cells = move_dipping_cells(physical, mesh, options.max_displacement_um * dbu);
  }
  if (options.split) {
    model = split_boundary_nets(physical, mesh, route, options.clean_cuts);
  } else {
    model.design = physical;
  }
  original_anchors = model.anchors;
  auto& work       = model.design;
  limit_displacement(work, node_weight_criticality(physical), options.max_displacement_um * dbu);
  auto control           = base;
  // Settle a converged placement so that only the pins move cells: fillers start in its free space (random fillers
  // push cells around: real HPWL +20-30% in 50 iterations) and the seeded density weight follows the starting
  // overflow (GP stopped while still spreading, so no fixed weight holds it).
  control.fillers_in_whitespace  = true;
  control.hold_overflow          = true;
  control.post_density_factor    = 0;
  control.wirelength_gamma_start = control.wirelength_gamma_end;
  control.max_step               = std::min(control.max_step, .05);
  control.legalize = control.detailed_place = false;
  control.before_legalize                   = {};
  control.after_macro_legalize              = {};
  control.progress_observer                 = {};
  control.checkpoint                        = {};
  control.checkpoint_interval               = 0;
  control.checkpoint_final                  = false;
  control.checkpoint_after_density          = false;
  control.terminal_update                   = {};
  control.terminal_interval                 = 0;
  control.macro_pull_update                 = {};
  control.macro_pull_interval               = 0;
  auto        actual                        = physical;
  std::string phase                         = "iteration";
  control.observer_interval                 = 10;
  control.observer                          = [&](const dp::Design& snapshot, dp::PlacementStage stage, int iteration) {
    if (stage != dp::PlacementStage::global_placement) {
      return;
    }
    for (int i = 0; i < actual.num_movable; ++i) {
      actual.node_x[i] = snapshot.node_x[i];
      actual.node_y[i] = snapshot.node_y[i];
    }
    history.push_back({phase,
                       unsigned(iteration),
                       dp::hpwl(actual),
                       dp::density_overflow(actual, base.bins_x, base.bins_y, base.target_density, base.scale_macro_density),
                       dp::hpwl(snapshot),
                       elapsed(start)});
  };
  auto temporary_names = node_names;
  if (session) {
    if (node_names.size() != physical.node_x.size()) {
      throw std::invalid_argument("boundary session identities mismatch");
    }
    for (size_t i = physical.node_x.size(); i < work.node_x.size(); ++i) {
      temporary_names.push_back("region:" + std::to_string(i));
    }
    if (base.checkpoint && base.checkpoint_interval > 0) {
      control.checkpoint_interval = base.checkpoint_interval;
      control.checkpoint_overflow_milestones.clear();
      control.checkpoint_start = 0;
      control.checkpoint_final = true;
      control.checkpoint       = [&](const dp::Design& snapshot, int iteration) {
        for (int i = 0; i < actual.num_movable; ++i) {
          actual.node_x[i] = snapshot.node_x[i];
          actual.node_y[i] = snapshot.node_y[i];
        }
        auto update = base.checkpoint(actual, iteration);
        if (update.replacement || !update.node_size_x.empty() || !update.node_size_y.empty() || !update.pin_offset_x.empty()
            || !update.pin_offset_y.empty() || (update.pin_timing && update.pin_timing->enabled())) {
          throw std::runtime_error("boundary session timing requires physical net-weight updates without topology edits");
        }
        if (!update.net_weights.empty()) {
          actual.net_weights = update.net_weights;
          if (options.split) {
            update.net_weights.clear();
            for (int net : model.parent_net) {
              update.net_weights.push_back(actual.net_weights.at(net));
            }
          }
        }
        return update;
      };
    }
  }
  // GP with every virtual pin fixed; the pins never move.
  const auto settle = [&](unsigned iterations) {
    control.max_iterations = control.min_iterations = control.schedule_iterations = iterations;
    dp::GpState state;
    const auto  result = session ? session->advance(work, control, temporary_names, "boundary_" + phase, iterations)
                                 : dp::place(work, control, &state);
    for (const auto& pin : model.anchors) {
      if (work.node_x[pin.node] != pin.x || work.node_y[pin.node] != pin.y) {
        throw std::runtime_error("virtual boundary pin moved");
      }
    }
    return result;
  };
  // Anchors at the original crossings, a short settle, then each bus is ordered by where its settled cells pull
  // and every pin on the edge spaced at the pitch; the final settle runs with the ordered pins fixed.
  if (options.split && options.order && options.settle_iterations) {
    phase = "settle";
    settle(options.settle_iterations);
    phase = "iteration";
  }
  if (options.split && options.order) {
    ordering = order_boundary_pins(model, mesh, identities, macros, options.pitch_um * dbu);
  }
  if (options.split && options.align) {
    alignment = align_boundary_pins(model, mesh, identities, macros, options.align_near * options.pitch_um * dbu);
  }
  build_seconds     = elapsed(start);
  // Keep the last settled candidate, even if its metrics regress: this
  // experiment measures the overhead of pins, without an ordering selector.
  const auto result = settle(options.iterations);
  for (int i = 0; i < physical.num_movable; ++i) {
    physical.node_x[i] = work.node_x[i];
    physical.node_y[i] = work.node_y[i];
  }
  if (session) {
    physical.net_weights = actual.net_weights;
    session->synchronize(physical, base, node_names, "boundary_release");
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
  j.Key("pitch_dbu");
  j.Double(options.pitch_um * dbu);
  j.Key("min_pitch_dbu");
  j.Double(ordering.min_pitch);
  j.Key("narrowed_edges");
  j.Uint(ordering.narrowed_edges);
  j.Key("settle_iterations");
  j.Uint(options.order ? options.settle_iterations : 0);
  j.EndObject();
  j.Key("clean_cuts");
  j.StartObject();
  j.Key("enabled");
  j.Bool(options.clean_cuts);
  j.Key("moved_cells");
  j.Uint(moved_cells);
  j.Key("removed_dips");
  j.Uint(model.dips_removed);
  j.EndObject();
  j.Key("pinalign");
  j.StartObject();
  j.Key("enabled");
  j.Bool(options.align);
  j.Key("direct");
  j.Uint(alignment.direct);
  j.Key("one_cell");
  j.Uint(alignment.one_cell);
  j.Key("near");
  j.Uint(alignment.near);
  j.Key("near_pitches");
  j.Double(options.align_near);
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
    j.Key("input_cell_pin");
    j.Int(p.input_cell_pin);
    j.Key("output_cell_pin");
    j.Int(p.output_cell_pin);
    if (p.cell >= 0) {
      j.Key("input_target_dbu");
      j.Double(p.input_target);
      j.Key("output_target_dbu");
      j.Double(p.output_target);
    }
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
  j.Uint64(released_counts ? released_counts->first : model.design.net_weights.size());
  j.Key("placement_pins");
  j.Uint64(released_counts ? released_counts->second : model.design.pin2node.size());
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
  for (const auto& r : mesh.rects) {
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
  // The fixed virtual IO pins of the regions (the contract with region-local placement and routing): the net by its
  // driver, its bus bit, the edge (normal axis 0 = vertical boundary) and the rectangles on its low and high side.
  std::ofstream pins(output + "/pins.tsv");
  pins << "node\toriginal_net\tx_dbu\ty_dbu\tedge\toriginal_x_dbu\toriginal_y_dbu\tdriver\tbus\tbit\taxis\trect_low\trect_high\n";
  for (const auto& p : model.anchors) {
    const auto& old  = original_anchors[&p - model.anchors.data()];
    const auto  side = [&](double delta) {
      const int  axis = p.edge >= 0 ? model.edges[p.edge].axis : -1;
      const auto rect = axis == 0 ? mesh.rect_of(p.x + delta, p.y) : axis == 1 ? mesh.rect_of(p.x, p.y + delta) : std::nullopt;
      return rect.value_or(-1);
    };
    const auto& bit = p.original_net < int(identities.size()) ? identities[p.original_net] : Bus_bit{};
    pins << p.node << '\t' << p.original_net << '\t' << p.x << '\t' << p.y << '\t' << p.edge << '\t' << old.x << '\t' << old.y
         << '\t' << (p.original_net < int(drivers.size()) ? drivers[p.original_net] : "") << '\t' << bit.bus << '\t' << bit.bit
         << '\t' << (p.edge >= 0 ? model.edges[p.edge].axis : -1) << '\t' << side(-1) << '\t' << side(1) << '\n';
  }
  if (!report || !pins) {
    throw std::runtime_error("cannot write boundary-pin report");
  }
}
}  // namespace livehd::placement
