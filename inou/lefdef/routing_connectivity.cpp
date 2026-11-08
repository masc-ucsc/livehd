// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_connectivity.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "index_union_find.hpp"
#include "routing_geometry.hpp"
#include "routing_occupancy.hpp"

namespace livehd::physical {
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
namespace {
Route_connectivity check_connectivity(const Technology& tech, const Floorplan& fp, const Net& net,
                                      const Route_components* components, const Route_width_rules* trial_rules,
                                      std::span<const Shape> fixed_metal, std::span<const std::vector<Shape>> physical_pin_unions,
                                      bool require_route_pair) {
  Route_connectivity out;
  try {
    if (require_route_pair && (net.connections.size() < 2 || net.wires.empty())) {
      throw std::runtime_error("missing endpoints or route");
    }
    Routing_occupancy   index(fp.dbu);
    Index_union_find    graph;
    std::vector<size_t> nodes;
    const auto          append = [&](const Shape& s) {
      const auto node = graph.add();
      if (tech.layers.contains(s.layer)) {
        for (auto id : index.query(s.layer, s.rect, 0)) {
          if (s.rect.touches(index.at(id).shape.rect)) {
            graph.merge(node, nodes[id]);
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
                if (tech.layers.contains(shapes[j].layer) && shapes[i].rect.overlaps(shapes[j].rect)) {
                  reached.insert(shapes[j].layer);
                  graph.merge(piece_nodes[i], piece_nodes[j]);
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
    for (const auto& shape : fixed_metal) {
      if (!tech.layers.contains(shape.layer) || shape.rect.empty()) {
        throw std::runtime_error("unsupported fixed conductor shape");
      }
      const auto rect = snapped_dbu(shape.rect, 1e-6);
      if (!rect) {
        throw std::runtime_error("fractional fixed conductor geometry");
      }
      append({shape.layer, *rect});
    }
    if (require_route_pair && !graph.size()) {
      throw std::runtime_error("route has no metal");
    }
    std::optional<Route_components> local;
    if (!components && (require_route_pair || !net.connections.empty())) {
      local.emplace(fp);
      components = &*local;
    }
    if (components && !components->matches(fp)) {
      throw std::invalid_argument("component index does not match routing floorplan");
    }
    const auto append_pin = [&](const std::vector<Shape>& shapes, const auto& transform) {
      if (shapes.empty()) {
        throw std::runtime_error("empty endpoint pin");
      }
      const auto pin_node = graph.add();
      for (const auto& s : shapes) {
        if (!tech.layers.contains(s.layer) || s.rect.empty()) {
          throw std::runtime_error("unsupported endpoint pin shape");
        }
        const auto r = snapped_dbu(transform(s.rect), 1e-6);
        if (!r) {
          throw std::runtime_error("fractional endpoint geometry");
        }
        graph.merge(pin_node, append({s.layer, *r}));
      }
    };
    for (const auto& endpoint : net.connections) {
      if (endpoint.instance == "PIN") {
        const Terminal* terminal = nullptr;
        for (const auto& candidate : fp.terminals) {
          if (candidate.name == endpoint.pin) {
            if (terminal) {
              throw std::runtime_error("duplicate terminal name");
            }
            terminal = &candidate;
          }
        }
        if (!terminal || terminal->status == Status::UNPLACED) {
          throw std::runtime_error("missing or unplaced terminal endpoint");
        }
        append_pin(terminal->shapes,
                   [&](const Rect& r) { return orient_rect(r, 0, 0, terminal->orientation).translated(terminal->x, terminal->y); });
        continue;
      }
      const auto found = components->find(endpoint.instance);
      if (found == components->end()) {
        throw std::runtime_error("unsupported non-cell endpoint");
      }
      const auto& cell   = *found->second;
      const auto& master = tech.macros.at(cell.macro);
      if (master.virtual_proxy || cell.place.status == Status::UNPLACED) {
        throw std::runtime_error("unphysical endpoint");
      }
      const auto& pin = master.pins.at(endpoint.pin);
      append_pin(pin.shapes, [&](const Rect& r) { return placed_rect(master, cell.place, fp.dbu, r); });
    }
    for (const auto& pin : physical_pin_unions) {
      append_pin(pin, [](const Rect& rect) { return rect; });
    }
    if (!graph.size()) {
      throw std::runtime_error("route has no metal");
    }
    std::set<size_t> roots;
    for (size_t i = 0; i < graph.size(); ++i) {
      roots.insert(graph.find(i));
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
}  // namespace

Route_connectivity check_route_connectivity(const Technology& tech, const Floorplan& fp, const Net& net,
                                            const Route_components* components, const Route_width_rules* trial_rules) {
  return check_connectivity(tech, fp, net, components, trial_rules, {}, {}, true);
}

Route_connectivity check_conductor_connectivity(const Technology& tech, const Floorplan& fp, std::span<const Route_path> paths,
                                                std::span<const Shape>              fixed_metal,
                                                std::span<const std::vector<Shape>> physical_pin_unions,
                                                const Route_width_rules*            trial_rules) {
  Net conductors;
  if (!paths.empty()) {
    conductors.wires.push_back({
        "ROUTED",
        {paths.begin(), paths.end()}
    });
  }
  return check_connectivity(tech, fp, conductors, nullptr, trial_rules, fixed_metal, physical_pin_unions, false);
}
}  // namespace livehd::physical
