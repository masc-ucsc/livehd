// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_connectivity.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "routing_geometry.hpp"
#include "routing_occupancy.hpp"

namespace livehd::physical {
namespace {
bool contact(const Rect& a, const Rect& b, bool cut = false) {
  const auto x = std::min(a.xh, b.xh) - std::max(a.xl, b.xl);
  const auto y = std::min(a.yh, b.yh) - std::max(a.yl, b.yl);
  return cut ? x > 0 && y > 0 : x >= 0 && y >= 0 && (x > 0 || y > 0);
}
struct Connections {
  std::vector<size_t> parent;
  size_t              add() {
    auto n = parent.size();
    parent.push_back(n);
    return n;
  }
  size_t root(size_t n) {
    while (parent[n] != n) {
      parent[n] = parent[parent[n]];
      n         = parent[n];
    }
    return n;
  }
  void join(size_t a, size_t b) { parent[root(a)] = root(b); }
};
}  // namespace
Route_components::Route_components(const Floorplan& fp) : frame_(&fp), data_(fp.components.data()), size_(fp.components.size()) {
  cells_.reserve(size_);
  for (const auto& c : fp.components) {
    if (!cells_.emplace(c.name, &c).second) {
      throw std::invalid_argument("duplicate component name");
    }
  }
}
bool Route_components::matches(const Floorplan& fp) const {
  return frame_ == &fp && data_ == fp.components.data() && size_ == fp.components.size();
}
Route_connectivity check_route_connectivity(const Technology& tech, const Floorplan& fp, const Net& net,
                                            const Route_components* components, const Route_width_rules* trial_rules) {
  Route_connectivity out;
  try {
    if (net.connections.size() < 2 || net.wires.empty()) {
      throw std::runtime_error("missing endpoints or route");
    }
    Routing_occupancy   index(fp.dbu);
    Connections         graph;
    std::vector<size_t> nodes;
    const auto          append = [&](const Shape& s) {
      const auto node = graph.add();
      if (tech.layers.contains(s.layer)) {
        for (auto id : index.query(s.layer, s.rect, 0)) {
          if (contact(s.rect, index.at(id).shape.rect)) {
            graph.join(node, nodes[id]);
          }
        }
        const auto id = index.insert({s, {}});
        if (id != nodes.size()) {
          throw std::runtime_error("connectivity index mismatch");
        }
        nodes.push_back(node);
      } else if (!tech.cut_layers.contains(s.layer)) {
        throw std::runtime_error("unknown connectivity layer");
      }
      return node;
    };
    for (const auto& wire : net.wires) {
      for (const auto& path : wire.paths) {
        // Validate full path first, including unsupported geometry/empty paths.
        Net whole;
        whole.wires.push_back({wire.type, {path}});
        (void)route_geometry(tech, fp, whole, false, trial_rules);
        std::optional<Route_point> previous;
        auto                       layer = path.layer;
        for (const auto& step : path.steps) {
          const auto* point = std::get_if<Route_point>(&step);
          if (!previous) {
            previous = *point;
            continue;
          }
          Net piece;
          piece.wires.push_back({wire.type, {{layer, path.width, path.shape, {*previous, step}, path.taper_rule}}});
          const auto          shapes = route_geometry(tech, fp, piece, false, trial_rules);
          std::vector<size_t> piece_nodes;
          for (const auto& s : shapes) {
            piece_nodes.push_back(append(s));
          }
          if (std::holds_alternative<Route_via>(step)) {
            std::set<std::string> metals;
            bool                  has_cut = false;
            for (size_t i = 0; i < shapes.size(); ++i) {
              if (tech.layers.contains(shapes[i].layer)) {
                metals.insert(shapes[i].layer);
                continue;
              }
              has_cut = true;
              std::set<std::string> reached;
              for (size_t j = 0; j < shapes.size(); ++j) {
                if (tech.layers.contains(shapes[j].layer) && contact(shapes[i].rect, shapes[j].rect, true)) {
                  reached.insert(shapes[j].layer);
                  graph.join(piece_nodes[i], piece_nodes[j]);
                }
              }
              if (reached.size() != 2) {
                throw std::runtime_error("cut does not contact both via landings");
              }
            }
            if (!has_cut || metals.size() != 2 || !metals.erase(layer)) {
              throw std::runtime_error("invalid via stack");
            }
            layer = *metals.begin();
          } else if (point) {
            previous = *point;
          }
        }
      }
    }
    if (graph.parent.empty()) {
      throw std::runtime_error("route has no metal");
    }
    std::optional<Route_components> local;
    if (!components) {
      local.emplace(fp);
      components = &*local;
    }
    if (!components->matches(fp)) {
      throw std::invalid_argument("component index does not match routing floorplan");
    }
    const auto& cells = *components;
    for (const auto& endpoint : net.connections) {
      const auto found = cells.find(endpoint.instance);
      if (found == cells.end()) {
        throw std::runtime_error("unsupported non-cell endpoint");
      }
      const auto& cell   = *found->second;
      const auto& master = tech.macros.at(cell.macro);
      if (master.virtual_proxy || cell.place.status == Status::UNPLACED) {
        throw std::runtime_error("unphysical endpoint");
      }
      const auto& pin = master.pins.at(endpoint.pin);
      if (pin.shapes.empty()) {
        throw std::runtime_error("empty endpoint pin");
      }
      const auto pin_node = graph.add();
      for (const auto& s : pin.shapes) {
        if (!tech.layers.contains(s.layer) || s.rect.empty()) {
          throw std::runtime_error("unsupported endpoint pin shape");
        }
        auto r = placed_rect(master, cell.place, fp.dbu, s.rect);
        for (auto* v : {&r.xl, &r.yl, &r.xh, &r.yh}) {
          if (!std::isfinite(*v) || std::abs(*v - std::round(*v)) > 1e-6) {
            throw std::runtime_error("fractional endpoint geometry");
          }
          *v = std::round(*v);
        }
        graph.join(pin_node, append({s.layer, r}));
      }
    }
    std::set<size_t> roots;
    for (size_t i = 0; i < graph.parent.size(); ++i) {
      roots.insert(graph.root(i));
    }
    out.components = roots.size();
    out.connected  = roots.size() == 1;
    if (!out.connected) {
      out.reason = "disconnected route or endpoint";
    }
  } catch (const std::exception& e) {
    out.reason = e.what();
  }
  return out;
}
}  // namespace livehd::physical
