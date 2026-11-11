// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Final is a joint layout/whole-net selection for each admitted cellbox.  Local
// conductors are immutable inputs; no sequential whole-net routing fallback.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "cellbox_sat_router.hpp"
#include "core/json_util.hpp"
#include "inou/lefdef/cut_rules.hpp"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/routing_rules.hpp"
#include "inou/lefdef/signal_via.hpp"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace ph = physical;
namespace {
std::vector<std::vector<ph::Shape>> local_pin_shapes(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Net& net) {
  const ph::Route_components          components(frame);
  std::vector<std::vector<ph::Shape>> pins;
  for (const auto& connection : net.connections) {
    const auto found = components.find(connection.instance);
    if (connection.instance == "PIN" || found == components.end()) {
      throw std::invalid_argument("SAT member pin has no original component");
    }
    const auto&            cell  = *found->second;
    const auto&            macro = tech.macros.at(cell.macro);
    const auto&            pin   = macro.pins.at(connection.pin);
    std::vector<ph::Shape> shapes;
    for (const auto& shape : pin.shapes) {
      shapes.push_back({shape.layer, ph::placed_rect(macro, cell.place, frame.dbu, shape.rect)});
    }
    if (shapes.empty()) {
      throw std::invalid_argument("SAT member pin has no native geometry");
    }
    pins.push_back(std::move(shapes));
  }
  return pins;
}
std::string independently_connected(const ph::Technology& tech, const ph::Floorplan& frame, const Cellbox_sat_net& net,
                                    const Signal_route_geometry& geometry) {
  std::vector<ph::Route_path> paths;
  std::vector<ph::Shape>      fixed_metal;
  for (const auto& component : net.incoming) {
    paths.insert(paths.end(), component.paths.begin(), component.paths.end());
    fixed_metal.insert(fixed_metal.end(), component.terminals.begin(), component.terminals.end());
  }
  paths.insert(paths.end(), geometry.paths.begin(), geometry.paths.end());
  const auto pins    = local_pin_shapes(tech, frame, net.local_net);
  const auto verdict = ph::check_conductor_connectivity(tech, frame, paths, fixed_metal, pins, &geometry.nondefault_rules);
  return verdict.connected ? std::string{} : "independent box connectivity: " + verdict.reason;
}
ph::Rect footprint(const Grid& g, const Box& box) {
  if (box.tile < 0 || size_t(box.tile) >= g.size() || box.span_columns < 1 || g.col(box.tile) + box.span_columns > g.cols) {
    throw std::logic_error("Final SAT: invalid admitted footprint");
  }
  return {double(g.left(g.col(box.tile))),
          double(g.bottom(g.row(box.tile))),
          double(g.left(g.col(box.tile)) + int64_t(box.span_columns) * g.tile_w),
          double(g.bottom(g.row(box.tile)) + g.tile_h)};
}
bool intersects(const ph::Rect& a, const ph::Rect& b) { return a.xl < b.xh && b.xl < a.xh && a.yl < b.yh && b.yl < a.yh; }
bool contacts(const ph::Rect& a, const ph::Rect& b) {
  const auto dx = std::min(a.xh, b.xh) - std::max(a.xl, b.xl);
  const auto dy = std::min(a.yh, b.yh) - std::max(a.yl, b.yl);
  return dx >= 0 && dy >= 0 && (dx > 0 || dy > 0);
}
ph::Rect expanded(ph::Rect r, int64_t amount) {
  r.xl -= double(amount);
  r.yl -= double(amount);
  r.xh += double(amount);
  r.yh += double(amount);
  return r;
}
bool same_pose(const Ann_place& a, const Ann_place& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height && a.orientation == b.orientation
         && a.status == b.status;
}

// A small source frame retains global coordinates and technology definitions.
// Connections are filtered by actual identity, never reconstructed from names
// guessed from a route or from the chosen placement.
ph::Floorplan member_frame(const ph::Floorplan& source, const std::vector<size_t>& members) {
  ph::Floorplan out;
  out.design           = source.design;
  out.dbu              = source.dbu;
  out.die              = source.die;
  out.rows             = source.rows;
  out.tracks           = source.tracks;
  out.vias             = source.vias;
  out.nondefault_rules = source.nondefault_rules;
  out.blockages        = source.blockages;
  std::set<std::string> names;
  for (const auto c : members) {
    out.components.push_back(source.components.at(c));
    names.insert(source.components.at(c).name);
  }
  const auto project = [&](const std::vector<ph::Net>& from, std::vector<ph::Net>& to) {
    for (const auto& net : from) {
      ph::Net local;
      local.name = net.name;
      local.use  = net.use;
      for (const auto& connection : net.connections) {
        if (connection.instance == "*" || names.contains(connection.instance)) {
          local.connections.push_back(connection);
        }
      }
      if (!local.connections.empty()) {
        to.push_back(std::move(local));
      }
    }
  };
  project(source.nets, out.nets);
  project(source.special_nets, out.special_nets);
  return out;
}

std::vector<Ann_place> pose_domain(const Design& d, const Grid& g, const Box& box, int movable) {
  const auto             component = size_t(d.movable.at(size_t(movable)));
  const auto&            original  = d.fp.components.at(component).place;
  const auto&            macro     = d.tech.macros.at(d.fp.components.at(component).macro);
  const auto             bounds    = footprint(g, box);
  std::vector<Ann_place> poses;
  if (original.width <= 0 || original.height <= 0 || g.site <= 0 || g.row_h <= 0) {
    return poses;
  }
  // All native rows/sites and both permitted row mirrors are enumerated.  SAT
  // decides simultaneous nonoverlap; the original placement is only ordering.
  for (int64_t y = int64_t(bounds.yl); y + original.height <= int64_t(bounds.yh); y += g.row_h) {
    const auto row = size_t((y - g.y0) / g.row_h);
    if (row >= g.row_present.size() || !g.row_present[row] || row >= g.row_orient.size()) {
      continue;
    }
    const auto                   base = g.row_orient[row];
    std::vector<ph::Orientation> orientations{base};
    if (macro.symmetry_y) {
      if (base == ph::Orientation::N) {
        orientations.push_back(ph::Orientation::FN);
      } else if (base == ph::Orientation::FN) {
        orientations.push_back(ph::Orientation::N);
      } else if (base == ph::Orientation::FS) {
        orientations.push_back(ph::Orientation::S);
      } else if (base == ph::Orientation::S) {
        orientations.push_back(ph::Orientation::FS);
      }
    }
    for (int64_t x = int64_t(bounds.xl); x + original.width <= int64_t(bounds.xh); x += g.site) {
      bool available = true;
      for (int64_t yy = y; available && yy < y + original.height; yy += g.row_h) {
        const int local_row = int((yy - bounds.yl) / g.row_h);
        for (int64_t xx = x; available && xx < x + original.width; xx += g.site) {
          const int  tile = box.tile + int((xx - int64_t(bounds.xl)) / g.tile_w);
          const auto site = (xx - g.left(g.col(tile))) / g.site;
          available       = std::ranges::any_of(g.segs(tile), [&](const Seg& seg) {
            return int(seg.row) == local_row && site >= seg.lo && site < seg.hi;
          });
        }
      }
      if (!available) {
        continue;
      }
      for (const auto orientation : orientations) {
        if (!ph::orientation_allowed(macro, orientation)) {
          continue;
        }
        auto pose        = original;
        pose.x           = x;
        pose.y           = y;
        pose.orientation = orientation;
        pose.status      = ph::Status::PLACED;
        poses.push_back(pose);
      }
    }
  }
  std::ranges::stable_sort(poses, [&](const auto& a, const auto& b) {
    return std::tuple{std::abs(a.x - original.x) + std::abs(a.y - original.y), a.y, a.x, a.orientation}
           < std::tuple{std::abs(b.x - original.x) + std::abs(b.y - original.y), b.y, b.x, b.orientation};
  });
  return poses;
}

std::vector<ph::Shape> metal(const ph::Technology& tech, const ph::Floorplan& frame, const std::string& name,
                             const std::vector<ph::Route_path>& paths) {
  ph::Net net;
  net.name = name;
  net.wires.push_back({"ROUTED", paths});
  return ph::coalesce_route_metal(tech, ph::route_geometry(tech, frame, net));
}

// Exact rectangular union coverage, not a bounding-box shortcut.
bool covered(const ph::Shape& wanted, const std::vector<ph::Shape>& by) {
  std::vector<ph::Rect> remainder{wanted.rect};
  for (const auto& shape : by) {
    if (shape.layer != wanted.layer) {
      continue;
    }
    std::vector<ph::Rect> next;
    for (const auto& r : remainder) {
      if (!intersects(r, shape.rect)) {
        next.push_back(r);
        continue;
      }
      const auto xl = std::max(r.xl, shape.rect.xl), xh = std::min(r.xh, shape.rect.xh);
      const auto yl = std::max(r.yl, shape.rect.yl), yh = std::min(r.yh, shape.rect.yh);
      if (r.xl < xl) {
        next.push_back({r.xl, r.yl, xl, r.yh});
      }
      if (xh < r.xh) {
        next.push_back({xh, r.yl, r.xh, r.yh});
      }
      if (r.yl < yl) {
        next.push_back({xl, r.yl, xh, yl});
      }
      if (yh < r.yh) {
        next.push_back({xl, yh, xh, r.yh});
      }
    }
    remainder = std::move(next);
    if (remainder.empty()) {
      return true;
    }
  }
  return remainder.empty();
}

std::vector<ph::Shape> owned_pin_shapes(const Design& d, const ph::Floorplan& frame, size_t net,
                                        const ph::Floorplan*                 posed_members       = nullptr,
                                        std::vector<std::vector<ph::Shape>>* physical_pin_unions = nullptr) {
  std::vector<ph::Shape> shapes;
  for (const auto& pin : d.nets.at(net).pins) {
    const auto begin = shapes.size();
    if (pin.comp >= 0) {
      const auto& cell  = frame.components.at(size_t(pin.comp));
      const auto& macro = d.tech.macros.at(cell.macro);
      const auto* pose  = &cell.place;
      if (posed_members) {
        const auto found
            = std::ranges::find_if(posed_members->components, [&](const auto& member) { return member.name == cell.name; });
        if (found != posed_members->components.end()) {
          pose = &found->place;
        }
      }
      for (const auto& shape : macro.pins.at(std::string(pin.name)).shapes) {
        shapes.push_back({shape.layer, ph::placed_rect(macro, *pose, frame.dbu, shape.rect)});
      }
    } else if (pin.term >= 0) {
      const auto& terminal = frame.terminals.at(size_t(pin.term));
      for (const auto& shape : terminal.shapes) {
        shapes.push_back({shape.layer, ph::orient_rect(shape.rect, 0, 0, terminal.orientation).translated(terminal.x, terminal.y)});
      }
    }
    if (physical_pin_unions) {
      physical_pin_unions->emplace_back(shapes.begin() + begin, shapes.end());
    }
  }
  return shapes;
}

std::string check_metal(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Routing_occupancy& scene,
                        const std::vector<ph::Shape>& shapes, const std::string& owner, const std::vector<ph::Shape>& source_pins) {
  const ph::Signal_metal_rules rules(tech, frame);
  auto                         combined = shapes;
  combined.insert(combined.end(), source_pins.begin(), source_pins.end());
  combined = ph::coalesce_route_metal(tech, combined);
  // Removing a contained rectangle preserves the exact metal set. In
  // particular, a native via plate inside a real pin is not a new tiny wire
  // or an exposed internal EOL. Do not fill L/T-shaped unions with a hull.
  std::vector<ph::Shape> owner_union;
  for (size_t k = 0; k < combined.size(); ++k) {
    bool contained = false;
    if (tech.layers.contains(combined[k].layer)) {
      for (size_t j = 0; j < combined.size(); ++j) {
        if (j != k && combined[k].layer == combined[j].layer && combined[j].rect.contains(combined[k].rect)
            && (combined[j].rect != combined[k].rect || j < k)) {
          contained = true;
          break;
        }
      }
    }
    if (!contained) {
      owner_union.push_back(combined[k]);
    }
  }
  for (const auto& shape : owner_union) {
    if (!frame.die.contains(shape.rect)) {
      return "metal outside die";
    }
    // Real pins supply intrinsic connectivity and complete landing area, but
    // are not newly emitted routing. In particular, upstream Local admission
    // must not reject untouched movable pins before the box layout is solved.
    // Retain the full union for any piece actually touched by routed metal.
    if (!std::ranges::any_of(shapes,
                             [&](const auto& route) { return route.layer == shape.layer && intersects(route.rect, shape.rect); })) {
      continue;
    }
    const auto             cut = tech.cut_layers.find(shape.layer);
    std::optional<int64_t> clearance;
    if (cut == tech.cut_layers.end()) {
      if (!covered(shape, source_pins)) {
        if (const auto verdict = rules.rectangle(shape); !verdict.legal) {
          return std::format("{} on {} [{},{},{},{}]",
                             verdict.reason,
                             shape.layer,
                             shape.rect.xl,
                             shape.rect.yl,
                             shape.rect.xh,
                             shape.rect.yh);
        }
      }
      clearance = rules.clearance(shape.layer);
    } else {
      clearance = ph::signal_cut_spacing(cut->second, frame.dbu);
    }
    if (!clearance) {
      return "unsupported native clearance on " + shape.layer;
    }
    for (const auto q : scene.query(shape.layer, shape.rect, *clearance, owner)) {
      const auto other = scene.at(q);
      const auto verdict
          = cut == tech.cut_layers.end()
                ? rules.spacing_with_union(shape, other.shape, owner_union)
                : ph::Rule_check{!ph::violates_cut_spacing(shape.rect, other.shape.rect, double(*clearance)), "cut spacing"};
      if (!verdict.legal) {
        return std::format("native spacing on {} [{},{},{},{}] against {} [{},{},{},{}]: {}",
                           shape.layer,
                           shape.rect.xl,
                           shape.rect.yl,
                           shape.rect.xh,
                           shape.rect.yh,
                           other.net,
                           other.shape.rect.xl,
                           other.shape.rect.yl,
                           other.shape.rect.xh,
                           other.shape.rect.yh,
                           verdict.reason);
      }
    }
  }
  return {};
}
void insert_metal(ph::Routing_occupancy& scene, const std::vector<ph::Shape>& shapes, const std::string& owner) {
  for (const auto& shape : shapes) {
    scene.insert({shape, owner, true});
  }
}

// Materialized vias are not straight carrier paths. Inspect their actual
// finite landing metal against the same private ownership in every touched
// tile, including both tiles of a wide footprint. No center-only exemption.
std::string private_metal_failure(const Grid& g, const std::vector<ph::Shape>& shapes) {
  if (g.critical_reserved_h.size() != g.size() || g.critical_reserved_v.size() != g.size()) {
    return "incomplete exact private-coordinate masks";
  }
  for (const auto& shape : shapes) {
    if (shape.layer != "M2" && shape.layer != "M3") {
      continue;
    }
    const bool h         = shape.layer == "M2";
    const int  first_col = std::max(0, int(std::floor((shape.rect.xl - g.x0) / g.tile_w)));
    const int  last_col  = std::min(g.cols - 1, int(std::floor((shape.rect.xh - g.x0) / g.tile_w)));
    const int  first_row = std::max(0, int(std::floor((shape.rect.yl - g.y0) / g.tile_h)));
    const int  last_row  = std::min(g.rows - 1, int(std::floor((shape.rect.yh - g.y0) / g.tile_h)));
    for (int row = first_row; row <= last_row; ++row) {
      for (int col = first_col; col <= last_col; ++col) {
        const int      tile = g.tile(col, row);
        const ph::Rect bounds{double(g.left(col)),
                              double(g.bottom(row)),
                              double(g.left(col) + g.tile_w),
                              double(g.bottom(row) + g.tile_h)};
        if (!intersects(shape.rect, bounds)) {
          continue;
        }
        const auto& coordinates = h ? g.critical_reserved_h.at(size_t(tile)) : g.critical_reserved_v.at(size_t(tile));
        for (const auto coordinate : coordinates) {
          if (h ? shape.rect.yl <= coordinate && coordinate <= shape.rect.yh
                : shape.rect.xl <= coordinate && coordinate <= shape.rect.xh) {
            return std::format("Local metal covers private {} coordinate {} in tile {}", shape.layer, coordinate, tile);
          }
        }
      }
    }
  }
  return {};
}

// A deterministic fixed-site stack, never an invocation of a route search.
// Outer plates must be covered by the already admitted Local/IO conductor;
// intermediate native plates and cuts are checked with the complete owner.
std::string materialize_joins(const Design& d, const Grid& g, size_t net, const Final_planned_geometry& planned,
                              const ph::Floorplan& work, std::vector<ph::Route_path>& paths) {
  auto retained = metal(d.tech, work, d.fp.nets[d.nets[net].frame].name, paths);
  for (const auto& pin : d.nets[net].pins) {
    if (pin.term >= 0) {
      const auto access = planned_terminal_access(d, g, d.fp.terminals.at(size_t(pin.term)));
      if (access) {
        retained.push_back(access->shape);
      }
    }
  }
  // Consume the producer's exact native choices. Independently selecting the
  // first DEFAULT per layer can replace a touching chain with disconnected
  // plates, even when every individual via definition is valid.
  const auto& selected = planned.adjacent_join_vias.at(net);
  std::map<std::tuple<std::string, int64_t, int64_t>, std::vector<const ph::Route_path*>> by_site;
  for (const auto& path : selected) {
    if (path.steps.size() != 2) {
      return "invalid selected Local via witness";
    }
    const auto* point = std::get_if<ph::Route_point>(&path.steps[0]);
    const auto* via   = std::get_if<ph::Route_via>(&path.steps[1]);
    if (!point || !via || !d.tech.vias.contains(via->name) || !d.tech.vias.at(via->name).is_default) {
      return "invalid selected Local via identity";
    }
    by_site[{path.layer, point->x, point->y}].push_back(&path);
  }
  for (const auto& [from, to, x, y] : planned.joins.at(net)) {
    const auto a = std::ranges::find(d.tech.routing_layers, from), b = std::ranges::find(d.tech.routing_layers, to);
    if (a == d.tech.routing_layers.end() || b == d.tech.routing_layers.end()) {
      return "unknown Local join layer";
    }
    const auto             lo = std::min(a, b), hi = std::max(a, b);
    std::vector<ph::Shape> reachable;
    for (auto layer = lo; layer != hi; ++layer) {
      const auto&            lower   = *layer;
      const auto&            upper   = *(layer + 1);
      const auto             offered = by_site.find({lower, x, y});
      std::vector<ph::Shape> next;
      if (offered != by_site.end()) {
        for (const auto* path : offered->second) {
          const auto&            via = std::get<ph::Route_via>(path->steps[1]);
          std::vector<ph::Shape> plates;
          try {
            for (auto shape : ph::via_geometry(d.tech, work, via)) {
              if (d.tech.layers.contains(shape.layer)) {
                shape.rect = shape.rect.translated(x, y);
                plates.push_back(shape);
              }
            }
          } catch (const std::exception&) {
            continue;
          }
          if (plates.size() != 2) {
            continue;
          }
          const auto low  = std::ranges::find_if(plates, [&](const auto& s) { return s.layer == lower; });
          const auto high = std::ranges::find_if(plates, [&](const auto& s) { return s.layer == upper; });
          if (low == plates.end() || high == plates.end()) {
            continue;
          }
          if ((layer == lo && !covered(*low, retained)) || (layer + 1 == hi && !covered(*high, retained))) {
            continue;
          }
          if (layer != lo && !std::ranges::any_of(reachable, [&](const auto& previous) {
                return previous.layer == low->layer && contacts(previous.rect, low->rect);
              })) {
            continue;
          }
          next.push_back(*high);
        }
      }
      if (next.empty()) {
        return std::format("unmaterialized fixed Local join {}>{} at {},{}", from, to, x, y);
      }
      reachable = std::move(next);
    }
  }
  // Every selected physical component remains mandatory, including choices
  // shared by coincident joins. The native conductor audit rejects islands.
  paths.insert(paths.end(), selected.begin(), selected.end());
  return {};
}

// Geometric component equivalence comes from actual same-layer contact and
// the connected walk represented by each native path, never the net ID.
std::vector<Cellbox_sat_incoming> incoming_components(const Design& d, const Grid& g, size_t net, const ph::Floorplan& frame,
                                                      const std::vector<ph::Route_path>& paths) {
  std::vector<Cellbox_sat_incoming>   pieces;
  std::vector<std::vector<ph::Shape>> shapes;
  for (const auto& path : paths) {
    Cellbox_sat_incoming item;
    item.paths.push_back(path);
    shapes.push_back(metal(d.tech, frame, d.fp.nets[d.nets[net].frame].name, item.paths));
    pieces.push_back(std::move(item));
  }
  for (const auto& pin : d.nets[net].pins) {
    if (pin.term >= 0) {
      const auto access = planned_terminal_access(d, g, d.fp.terminals.at(size_t(pin.term)));
      if (!access) {
        continue;
      }
      Cellbox_sat_incoming item;
      item.terminals.push_back(access->shape);
      shapes.push_back(item.terminals);
      pieces.push_back(std::move(item));
    }
  }
  std::vector<size_t> parent(pieces.size());
  std::iota(parent.begin(), parent.end(), 0);
  const auto root = [&](size_t i) {
    while (parent[i] != i) {
      i = parent[i];
    }
    return i;
  };
  ph::Routing_occupancy index(std::max<int64_t>(1, frame.dbu));
  std::vector<size_t>   piece_of;
  for (size_t i = 0; i < shapes.size(); ++i) {
    for (const auto& shape : shapes[i]) {
      if (!d.tech.layers.contains(shape.layer)) {
        continue;
      }
      for (const auto q : index.query(shape.layer, shape.rect, 1)) {
        if (contacts(shape.rect, index.at(q).shape.rect)) {
          parent[root(i)] = root(piece_of.at(q));
        }
      }
      const auto id = index.insert({shape, {}, true});
      if (id != piece_of.size()) {
        throw std::logic_error("nonsequential incoming geometry index");
      }
      piece_of.push_back(i);
    }
  }
  std::map<size_t, Cellbox_sat_incoming> merged;
  for (size_t i = 0; i < pieces.size(); ++i) {
    auto& item = merged[root(i)];
    item.paths.insert(item.paths.end(), pieces[i].paths.begin(), pieces[i].paths.end());
    item.terminals.insert(item.terminals.end(), pieces[i].terminals.begin(), pieces[i].terminals.end());
  }
  std::vector<Cellbox_sat_incoming> out;
  for (auto& [id, item] : merged) {
    item.id = std::format("source-net:{}:component:{}", d.nets[net].frame, id);
    const std::vector<std::vector<ph::Shape>> no_pin_equivalence;
    const auto check = ph::check_conductor_connectivity(d.tech, frame, item.paths, item.terminals, no_pin_equivalence);
    if (!check.connected) {
      throw std::invalid_argument("incoming component connectivity: " + check.reason);
    }
    out.push_back(std::move(item));
  }
  return out;
}

std::vector<int64_t> native_tracks(const ph::Floorplan& frame, const std::string& layer, bool horizontal, const ph::Rect& bounds) {
  std::vector<int64_t> out;
  for (const auto& pattern : frame.tracks) {
    if (!pattern.supported() || pattern.layer != layer || pattern.axis != (horizontal ? "Y" : "X")) {
      continue;
    }
    const auto [first, last] = pattern.index_range(horizontal ? bounds.yl : bounds.xl, horizontal ? bounds.yh : bounds.xh);
    for (auto k = first; k <= last; ++k) {
      out.push_back(pattern.start + k * pattern.step);
    }
  }
  std::ranges::sort(out);
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

struct Box_result {
  Cellbox_sat_status  solver_status = Cellbox_sat_status::unknown;
  Cellbox_sat_status  status        = Cellbox_sat_status::invalid;
  std::string         reason;
  Cellbox_sat_metrics metrics;
  bool                attempted = false;
};
}  // namespace

void validate_final_membership(const Design& d, const Grid& g, const Boxes& boxes) {
  if (boxes.box_of.size() != d.movable.size() || (!d.movable.empty() && (g.site <= 0 || g.row_h <= 0))) {
    throw std::logic_error("Final SAT: missing membership/grid");
  }
  std::vector<bool> seen(d.movable.size(), false);
  for (size_t bi = 0; bi < boxes.boxes.size(); ++bi) {
    const auto& box = boxes.boxes[bi];
    (void)footprint(g, box);
    if (d.opt.cellbox_full_flow && (!box.critical_fixed || !box.fixed || !box.placed)) {
      throw std::logic_error("Final SAT: stale admitted cellbox");
    }
    for (const int movable : box.cells) {
      if (movable < 0 || size_t(movable) >= seen.size() || seen[size_t(movable)] || boxes.box_of[size_t(movable)] != int(bi)) {
        throw std::logic_error("Final SAT: duplicate/stale member");
      }
      seen[size_t(movable)] = true;
      if (d.opt.cellbox_full_flow) {
        const auto& pose   = d.fp.components.at(size_t(d.movable.at(size_t(movable)))).place;
        const auto  bounds = footprint(g, box);
        if (pose.x < bounds.xl || pose.y < bounds.yl || pose.x + pose.width > bounds.xh || pose.y + pose.height > bounds.yh
            || (pose.x - int64_t(bounds.xl)) % g.site || (pose.y - int64_t(bounds.yl)) % g.row_h) {
          throw std::logic_error("Final SAT: source admission pose left its footprint");
        }
      }
    }
  }
  if (std::ranges::find(seen, false) != seen.end()) {
    throw std::logic_error("Final SAT: omitted source cell");
  }
}

void final_wires(const Design& d, const Grid& g, const Boxes& boxes, const Routing& routing, const Legroute_regions& regions,
                 Legroute_report& rep) {
  const auto started = Clock::now();
  validate_final_membership(d, g, boxes);
  const auto scope      = final_net_scope(d);
  const bool diagnostic = d.opt.cellbox_diagnostic_final;
  if (diagnostic && routing.final_local_rejected.size() != d.nets.size()) {
    throw std::invalid_argument("diagnostic SAT Final requires complete Local admission records");
  }
  std::vector<char> attempted(d.nets.size(), false);
  size_t            attempt_count = 0;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (scope.required[n] && (!d.opt.final_net_limit || attempt_count < d.opt.final_net_limit)) {
      attempted[n] = true;
      ++attempt_count;
    }
  }
  std::vector<std::string> why(d.nets.size());
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (scope.required[n] && !attempted[n]) {
      why[n] = "Final diagnostic net limit";
    }
  }
  std::vector<std::vector<ph::Route_path>> paths(d.nets.size());
  auto                                     work = d.fp;  // The only commit to the caller is at the very end.
  std::vector<size_t>                      design_of_frame(work.nets.size(), SIZE_MAX);
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (d.nets[n].frame >= work.nets.size() || design_of_frame[d.nets[n].frame] != SIZE_MAX) {
      throw std::logic_error("SAT Final source net mapping is not one-to-one");
    }
    design_of_frame[d.nets[n].frame] = n;
    for (const auto& pin : d.nets[n].pins) {
      if (pin.term >= 0) {
        work.terminals.at(size_t(pin.term)).net = work.nets[d.nets[n].frame].name;
      }
    }
  }
  work.special_nets.insert(work.special_nets.end(), regions.power_nets.begin(), regions.power_nets.end());
  work.vias.insert(work.vias.end(), regions.power_vias.begin(), regions.power_vias.end());
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (scope.required[n]) {
      work.nets[d.nets[n].frame].wires.clear();
    }
  }
  auto                  static_frame = work;
  std::set<std::string> removed;
  for (const int c : d.movable) {
    removed.insert(work.components.at(size_t(c)).name);
  }
  std::erase_if(static_frame.components, [&](const auto& c) { return removed.contains(c.name); });
  const auto prune = [&](std::vector<ph::Net>& nets) {
    for (auto& net : nets) {
      std::erase_if(net.connections, [&](const auto& c) { return removed.contains(c.instance); });
    }
  };
  prune(static_frame.nets);
  prune(static_frame.special_nets);
  auto fixed = ph::routing_occupancy(d.tech, static_frame, work.dbu);
  if (regions.power_nets.empty()) {
    for (const auto& shape : regions.obstacles) {
      fixed.insert({shape, {}, false});
    }
  }
  const auto immutable_static = fixed;
  auto       planned          = final_planned_geometry(d, g, boxes, routing);
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (!scope.required[n] || !attempted[n]) {
      continue;
    }
    if (!d.profile) {
      why[n] = "missing native routing profile";
      continue;
    }
    if (diagnostic && !routing.final_local_rejected[n].empty()) {
      why[n] = "Local rejected: " + routing.final_local_rejected[n];
      continue;
    }
    if (!planned.io_stub_failure[n].empty()) {
      why[n] = planned.io_stub_failure[n];
      continue;
    }
    if (d.opt.cellbox_full_flow
        && std::ranges::any_of(planned.path_state[n], [](uint8_t state) { return state != uint8_t(Track_state::checked); })) {
      why[n] = "unchecked Local path";
      continue;
    }
    try {
      // Every original source path remains mandatory immutable ingress. Its
      // original Wire metadata is restored verbatim when staged output is
      // assembled below; generated paths are appended separately.
      for (const auto& wire : d.fp.nets[d.nets[n].frame].wires) {
        paths[n].insert(paths[n].end(), wire.paths.begin(), wire.paths.end());
      }
      paths[n].insert(paths[n].end(), planned.paths[n].begin(), planned.paths[n].end());
      const auto& owner = work.nets[d.nets[n].frame].name;
      why[n] = check_metal(d.tech, work, fixed, metal(d.tech, work, owner, paths[n]), owner, owned_pin_shapes(d, work, n));
      if (why[n].empty()) {
        why[n] = materialize_joins(d, g, n, planned, work, paths[n]);
      }
      if (!why[n].empty()) {
        ++rep.global_conflict_nets;
        paths[n].clear();
        continue;
      }
      Final_cut_state cuts(d.tech, work);
      why[n] = cuts.add(paths[n]);
      if (!why[n].empty()) {
        ++rep.final_union_cut_rejections;
      }
      if (why[n].empty()) {
        why[n] = check_metal(d.tech,
                             work,
                             fixed,
                             metal(d.tech, work, work.nets[d.nets[n].frame].name, paths[n]),
                             work.nets[d.nets[n].frame].name,
                             owned_pin_shapes(d, work, n));
      }
      if (why[n].empty() && d.opt.cellbox_full_flow) {
        why[n] = private_metal_failure(g, metal(d.tech, work, owner, paths[n]));
      }
      if (!why[n].empty()) {
        ++rep.global_conflict_nets;
        paths[n].clear();
        continue;
      }
      insert_metal(fixed, metal(d.tech, work, work.nets[d.nets[n].frame].name, paths[n]), work.nets[d.nets[n].frame].name);
    } catch (const std::exception& error) {
      why[n] = error.what();
      paths[n].clear();
      ++rep.global_conflict_nets;
    }
  }
  if (d.opt.cellbox_full_flow) {
    auto carriers = planned;
    for (size_t n = 0; n < why.size(); ++n) {
      if (!why[n].empty()) {
        carriers.paths[n].clear();
      }
    }
    std::vector<std::string> rejected(d.nets.size());
    (void)critical_backbone_conflicts(d, g, carriers, &rejected);
    for (size_t n = 0; n < why.size(); ++n) {
      if (why[n].empty() && !rejected[n].empty()) {
        why[n] = rejected[n];
        ++rep.global_conflict_nets;
      }
    }
  }
  fixed = immutable_static;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (scope.required[n] && why[n].empty()) {
      insert_metal(fixed, metal(d.tech, work, work.nets[d.nets[n].frame].name, paths[n]), work.nets[d.nets[n].frame].name);
    }
  }
  auto                                           base = std::make_shared<const ph::Routing_occupancy>(std::move(fixed));
  ph::Routing_occupancy                          accepted(base);
  std::vector<std::vector<Cellbox_sat_incoming>> roots(d.nets.size());
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (why[n].empty()) {
      roots[n] = incoming_components(d, g, n, work, paths[n]);
    }
  }
  // Index current foreign member pins/OBS once. Updates append a new version;
  // stale versions are filtered, so no old pose becomes a permanent obstacle.
  ph::Routing_occupancy member_index(std::max<int64_t>(1, work.dbu));
  struct Member_record {
    size_t             box, version;
    ph::Occupied_shape occupied;
  };
  std::vector<Member_record>       member_records;
  std::vector<size_t>              versions(boxes.boxes.size(), 0);
  std::vector<std::vector<size_t>> members(boxes.boxes.size());
  const auto                       index_members = [&](size_t bi) {
    auto frame     = member_frame(work, members[bi]);
    auto occupancy = ph::routing_occupancy(d.tech, frame, work.dbu);
    for (size_t k = 0; k < occupancy.size(); ++k) {
      auto       occupied = occupancy.at(k);
      const auto id       = member_index.insert(occupied);
      if (id != member_records.size()) {
        throw std::logic_error("member index identity");
      }
      member_records.push_back({bi, versions[bi], std::move(occupied)});
    }
  };
  for (size_t bi = 0; bi < boxes.boxes.size(); ++bi) {
    for (const int m : boxes.boxes[bi].cells) {
      members[bi].push_back(size_t(d.movable.at(size_t(m))));
    }
    index_members(bi);
  }
  const ph::Signal_metal_rules native_rules(d.tech, work);
  int64_t                      halo = 1;
  for (const auto& layer : d.tech.routing_layers) {
    if (const auto clearance = native_rules.clearance(layer)) {
      halo = std::max(halo, *clearance);
    }
  }
  for (const auto& layer : g.layers) {
    halo = std::max(halo, layer.pitch * int64_t(d.tech.routing_layers.size() + 2));
  }
  std::vector<Box_result>          results(boxes.boxes.size());
  std::vector<std::vector<size_t>> obligations(boxes.boxes.size());
  const auto                       upstream_why = why;
  const auto                       low_v = std::ranges::find_if(g.layers, [](const auto& layer) { return !layer.horizontal; });
  // Relocation retry: the boxes the moves touched route first, the rest keep their order.
  std::vector<size_t> box_order;
  std::vector<char>   prioritized(boxes.boxes.size(), 0);
  for (const int bi : d.opt.final_box_priority) {
    if (bi >= 0 && size_t(bi) < boxes.boxes.size() && !prioritized[size_t(bi)]) {
      prioritized[size_t(bi)] = 1;
      box_order.push_back(size_t(bi));
    }
  }
  for (size_t bi = 0; bi < boxes.boxes.size(); ++bi) {
    if (!prioritized[bi]) {
      box_order.push_back(bi);
    }
  }
  for (const size_t bi : box_order) {
    const auto&         box   = boxes.boxes[bi];
    auto                local = member_frame(work, members[bi]);
    Cellbox_sat_problem problem;
    problem.bounds = footprint(g, box);
    std::map<std::string, size_t> component;
    for (size_t k = 0; k < local.components.size(); ++k) {
      component.emplace(local.components[k].name, k);
    }
    for (size_t k = 0; k < box.cells.size(); ++k) {
      problem.cells.push_back({k, pose_domain(d, g, box, box.cells[k])});
    }
    // Retain actual nearby fixed bodies, including pinless taps/endcaps. Their
    // geometry is already in background; the solver also needs body exclusions.
    for (const auto& fixed_cell : static_frame.components) {
      const auto& p = fixed_cell.place;
      if (intersects(problem.bounds, {double(p.x), double(p.y), double(p.x + p.width), double(p.y + p.height)})) {
        local.components.push_back(fixed_cell);
      }
    }
    std::vector<size_t> incident;
    for (size_t n = 0; n < d.nets.size(); ++n) {
      if (d.nets[n].power || (d.opt.cellbox_full_flow ? d.nets[n].control_scope : !scope.required[n])) {
        continue;
      }
      const auto&     source = work.nets[d.nets[n].frame];
      Cellbox_sat_net net;
      net.source_net     = d.nets[n].frame;
      net.local_net.name = source.name;
      net.local_net.use  = source.use;
      for (const auto& pin : source.connections) {
        if (component.contains(pin.instance)) {
          net.local_net.connections.push_back(pin);
        }
      }
      if (net.local_net.connections.empty()) {
        continue;
      }
      incident.push_back(n);
      obligations[bi].push_back(n);
      net.external = net.local_net.connections.size() != source.connections.size();
      if (!upstream_why[n].empty()) {
        results[bi].reason = "required incoming owner rejected: " + upstream_why[n];
      }
      auto domain       = std::make_shared<Signal_route_domain>();
      domain->owner     = source.name;
      domain->pin_layer = d.profile ? d.profile->route.access.pin : g.pin_layer;
      if (low_v == g.layers.end()) {
        results[bi].reason = "no native vertical local layer";
        continue;
      }
      domain->local_layers = {g.box_layer, low_v->name};
      Signal_route_region region;
      region.bounds = problem.bounds;
      region.tracks[g.box_layer]
          = d.opt.cellbox_full_flow ? box.reserved_h : native_tracks(work, g.box_layer, true, problem.bounds);
      region.tracks[low_v->name]
          = d.opt.cellbox_full_flow ? box.reserved_v : native_tracks(work, low_v->name, false, problem.bounds);
      domain->regions.push_back(std::move(region));
      for (const auto& layer : g.layers) {
        domain->max_stack_bridge_dbu = std::max(domain->max_stack_bridge_dbu, layer.pitch);
      }
      for (const auto& incoming : roots[n]) {
        auto shapes = metal(d.tech, work, source.name, incoming.paths);
        shapes.insert(shapes.end(), incoming.terminals.begin(), incoming.terminals.end());
        if (!std::ranges::any_of(shapes, [&](const auto& shape) {
              return d.tech.layers.contains(shape.layer) && intersects(shape.rect, problem.bounds);
            })) {
          continue;
        }
        net.incoming.push_back(incoming);
        for (const auto& shape : shapes) {
          const auto layer = d.tech.layers.find(shape.layer);
          if (layer == d.tech.layers.end() || !intersects(shape.rect, problem.bounds)) {
            continue;
          }
          // Keep the full immutable component above for provenance and
          // connectivity, but grant NEW geometry rights only inside this
          // box along the actual carrier's native coordinate and width.
          auto seed      = shape;
          auto extension = shape.rect;
          if (layer->second.direction == "HORIZONTAL") {
            seed.rect.xl = std::max(seed.rect.xl, problem.bounds.xl);
            seed.rect.xh = std::min(seed.rect.xh, problem.bounds.xh);
            extension.xl = problem.bounds.xl;
            extension.xh = problem.bounds.xh;
          } else if (layer->second.direction == "VERTICAL") {
            seed.rect.yl = std::max(seed.rect.yl, problem.bounds.yl);
            seed.rect.yh = std::min(seed.rect.yh, problem.bounds.yh);
            extension.yl = problem.bounds.yl;
            extension.yh = problem.bounds.yh;
          } else {
            continue;
          }
          domain->carriers.push_back({source.name, incoming.id, seed, extension});
        }
      }
      net.domain = std::move(domain);
      problem.nets.push_back(std::move(net));
    }
    // Independent source-side census, including physical endpoint multiplicity.
    // Omitting a complete net must not create a false zero-signal transaction.
    std::map<size_t, std::vector<ph::Connection>> expected_pins, supplied_pins;
    for (size_t source = 0; source < work.nets.size(); ++source) {
      std::vector<ph::Connection> pins;
      for (const auto& pin : work.nets[source].connections) {
        if (component.contains(pin.instance)) {
          pins.push_back(pin);
        }
      }
      if (pins.empty()) {
        continue;
      }
      const size_t n = design_of_frame[source];
      if (n == SIZE_MAX) {
        results[bi].reason = "source member net omitted from native inventory";
        continue;
      }
      if (d.nets[n].power || (d.opt.cellbox_full_flow ? d.nets[n].control_scope : !scope.required[n])) {
        continue;
      }
      expected_pins.emplace(source, std::move(pins));
    }
    for (const auto& net : problem.nets) {
      if (!supplied_pins.emplace(net.source_net, net.local_net.connections).second) {
        results[bi].reason = "duplicate source net in cellbox problem";
      }
    }
    const auto canonical_pins = [](auto& inventory) {
      for (auto& [source, pins] : inventory) {
        (void)source;
        std::ranges::sort(pins,
                          [](const auto& a, const auto& b) { return std::tie(a.instance, a.pin) < std::tie(b.instance, b.pin); });
      }
    };
    canonical_pins(expected_pins);
    canonical_pins(supplied_pins);
    if (expected_pins != supplied_pins) {
      results[bi].reason = "incomplete original cellbox pin/net obligations";
    }
    if (!d.profile) {
      results[bi].reason = "missing native routing profile";
    }
    if (!results[bi].reason.empty()) {
      for (const auto n : incident) {
        if (why[n].empty()) {
          why[n] = results[bi].reason;
        }
      }
      continue;
    }
    problem.profile             = signal_route_profile(*d.profile, d.tech);
    auto             background = accepted;
    std::set<size_t> queried;
    const auto       window      = expanded(problem.bounds, halo);
    const auto       add_foreign = [&](const std::string& layer) {
      for (const auto q : member_index.query(layer, window, 0)) {
        if (queried.insert(q).second) {
          const auto& record = member_records.at(q);
          if (record.box != bi && versions[record.box] > 0 && record.version == versions[record.box]) {
            background.insert(record.occupied);
          }
        }
      }
    };
    for (const auto& layer : d.tech.routing_layers) {
      add_foreign(layer);
    }
    for (const auto& [layer, rule] : d.tech.cut_layers) {
      (void)rule;
      add_foreign(layer);
    }
    Cellbox_sat_limits limits;
    const bool         clock_box = std::ranges::any_of(incident, [&](size_t n) { return d.nets[n].clock; });
    const auto         budget_ms = clock_box && d.opt.final_clock_route_ms ? d.opt.final_clock_route_ms : d.opt.route_ms;
    limits.deadline              = Clock::now() + std::chrono::milliseconds(budget_ms);
    limits.access_candidates     = d.opt.route_access;
    limits.route_patterns = clock_box && d.opt.final_clock_route_patterns ? d.opt.final_clock_route_patterns : d.opt.route_patterns;
    auto solved           = solve_cellbox_sat(d.tech, local, background, problem, limits);
    results[bi]           = {solved.status, solved.status, solved.reason, solved.metrics, true};
    if (solved.status != Cellbox_sat_status::complete) {
      for (const auto n : incident) {
        if (why[n].empty()) {
          why[n] = "cellbox SAT: " + solved.reason;
        }
      }
      continue;
    }
    std::string rejection;
    if (solved.poses.size() != problem.cells.size() || solved.routes.size() != problem.nets.size()) {
      rejection = "incomplete SAT result identity";
    }
    for (size_t k = 0; rejection.empty() && k < solved.poses.size(); ++k) {
      if (!std::ranges::any_of(problem.cells[k].poses, [&](const auto& pose) { return same_pose(pose, solved.poses[k]); })) {
        rejection = "SAT pose outside native domain";
        break;
      }
      local.components[k].place = solved.poses[k];
      const auto&    pose       = solved.poses[k];
      const ph::Rect body{double(pose.x), double(pose.y), double(pose.x + pose.width), double(pose.y + pose.height)};
      for (size_t j = 0; j < k; ++j) {
        const auto& p = solved.poses[j];
        if (intersects(body, {double(p.x), double(p.y), double(p.x + p.width), double(p.y + p.height)})) {
          rejection = "SAT cell body overlap";
        }
      }
    }
    auto trial = background;
    if (rejection.empty()) {
      auto member_only = local;
      member_only.components.resize(members[bi].size());
      member_only.blockages.clear();
      const auto posed = ph::routing_occupancy(d.tech, member_only, local.dbu);
      for (size_t k = 0; k < posed.size(); ++k) {
        trial.insert(posed.at(k));
      }
    }
    std::set<size_t> selected;
    for (const auto& route : solved.routes) {
      if (!rejection.empty()) {
        break;
      }
      const auto required = std::ranges::find_if(problem.nets, [&](const auto& net) { return net.source_net == route.source_net; });
      if (required == problem.nets.end() || !selected.insert(route.source_net).second) {
        rejection = "SAT returned unknown/duplicate net";
        break;
      }
      try {
        apply_signal_route_rules(local, route.geometry);
        rejection = independently_connected(d.tech, local, *required, route.geometry);
        if (!rejection.empty()) {
          break;
        }
        std::vector<ph::Route_path> retained;
        for (const auto& incoming : required->incoming) {
          retained.insert(retained.end(), incoming.paths.begin(), incoming.paths.end());
        }
        rejection = validate_signal_route_domain(d.tech,
                                                 local,
                                                 required->local_net,
                                                 route.geometry.paths,
                                                 *required->domain,
                                                 &route.geometry.nondefault_rules,
                                                 nullptr,
                                                 retained);
        if (!rejection.empty()) {
          break;
        }
        auto all = retained;
        all.insert(all.end(), route.geometry.paths.begin(), route.geometry.paths.end());
        Final_cut_state cuts(d.tech, local);
        rejection = cuts.add(all);
        if (!rejection.empty()) {
          break;
        }
        const auto shapes = metal(d.tech, local, required->local_net.name, all);
        rejection         = check_metal(d.tech,
                                        local,
                                        trial,
                                        shapes,
                                        required->local_net.name,
                                        owned_pin_shapes(d, work, design_of_frame[route.source_net], &local));
        if (!rejection.empty()) {
          break;
        }
        insert_metal(trial, shapes, required->local_net.name);
      } catch (const std::exception& error) {
        rejection = error.what();
      }
    }
    if (!rejection.empty()) {
      results[bi].status = Cellbox_sat_status::invalid;
      results[bi].reason = rejection;
      for (const auto n : incident) {
        if (why[n].empty()) {
          why[n] = "SAT result rejected: " + rejection;
        }
      }
      continue;
    }
    // Scratch commit only. The live design, incoming paths and source inventory
    // have not changed, and no later box can observe a rejected transaction.
    for (size_t k = 0; k < members[bi].size(); ++k) {
      work.components[members[bi][k]].place = solved.poses[k];
    }
    for (const auto& route : solved.routes) {
      apply_signal_route_rules(work, route.geometry);
      const auto n  = std::ranges::find_if(d.nets, [&](const auto& net) { return net.frame == route.source_net; });
      const auto id = size_t(n - d.nets.begin());
      paths[id].insert(paths[id].end(), route.geometry.paths.begin(), route.geometry.paths.end());
      insert_metal(accepted,
                   metal(d.tech, work, work.nets[route.source_net].name, route.geometry.paths),
                   work.nets[route.source_net].name);
    }
    ++versions[bi];
    index_members(bi);
  }
  // Rebuild from actual final poses and independently decode every original
  // net. This catches cross-box finite-pad, carrier-extension and cut conflicts.
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (scope.required[n] && why[n].empty()) {
      auto& wires           = work.nets[d.nets[n].frame].wires;
      wires                 = d.fp.nets[d.nets[n].frame].wires;
      size_t original_paths = 0;
      for (const auto& wire : wires) {
        original_paths += wire.paths.size();
      }
      if (paths[n].size() > original_paths) {
        wires.push_back({
            "ROUTED",
            {paths[n].begin() + original_paths, paths[n].end()}
        });
      }
    }
  }
  auto final_scene = ph::routing_occupancy(d.tech, work, work.dbu, {}, nullptr, ph::Signal_wires::exclude);
  if (regions.power_nets.empty()) {
    for (const auto& shape : regions.obstacles) {
      final_scene.insert({shape, {}, false});
    }
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (scope.required[n] && why[n].empty()) {
      try {
        const auto&                         net    = work.nets[d.nets[n].frame];
        const auto                          shapes = metal(d.tech, work, net.name, paths[n]);
        std::vector<std::vector<ph::Shape>> physical_pin_unions;
        const auto                          source_pins = owned_pin_shapes(d, work, n, nullptr, &physical_pin_unions);
        Final_cut_state                     cuts(d.tech, work);
        why[n] = cuts.add(paths[n]);
        if (why[n].empty()) {
          why[n] = check_metal(d.tech, work, final_scene, shapes, net.name, source_pins);
        }
        if (why[n].empty()) {
          const auto check = ph::check_conductor_connectivity(d.tech, work, paths[n], {}, physical_pin_unions);
          if (!check.connected) {
            ++rep.final_precommit_disconnected;
            ++rep.disconnected_nets;
            why[n] = "precommit connectivity: disconnected: " + check.reason;
          }
        }
        if (why[n].empty()) {
          insert_metal(final_scene, shapes, net.name);
        }
      } catch (const std::exception& error) {
        why[n] = error.what();
      }
    }
  }
  rep.final_skipped_nets    = scope.skipped;
  rep.final_diagnostic_only = diagnostic;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (scope.required[n]) {
      ++rep.routed_nets;
      if (!attempted[n]) {
        ++rep.unchecked_nets;
        rep.clock_unchecked_nets          += d.nets[n].clock;
        rep.final_detail_unattempted_pins += d.nets[n].pins.size();
      }
      rep.final_required_pins += d.nets[n].pins.size();
      rep.clock_required_nets += d.nets[n].clock;
      if (n < routing.local.size() && routing.local[n]) {
        ++rep.local_nets;
      }
      if (!attempted[n]) {
        continue;
      }
      if (why[n].empty()) {
        ++rep.connected_nets;
        rep.clock_connected_nets += d.nets[n].clock;
        rep.final_connected_pins += d.nets[n].pins.size();
      } else {
        ++rep.failed_nets;
        rep.clock_failed_nets += d.nets[n].clock;
        ++rep.failure_reasons[diagnostic && !routing.final_local_rejected[n].empty() ? "Local rejected before Final" : why[n]];
        if (rep.failure_sample.size() < 10) {
          rep.failure_sample.push_back(work.nets[d.nets[n].frame].name + ": " + why[n]);
        }
        if (diagnostic && !routing.final_local_rejected[n].empty()) {
          ++rep.final_local_rejected_nets;
          rep.final_local_rejected_pins += d.nets[n].pins.size();
        } else {
          ++rep.final_detail_failed_nets;
          rep.final_detail_failed_pins += d.nets[n].pins.size();
        }
      }
    }
  }
  const bool all_boxes
      = std::ranges::all_of(results, [](const auto& result) { return result.status == Cellbox_sat_status::complete; });
  std::vector<bool> assembly_complete(boxes.boxes.size(), false);
  size_t            complete_boxes = 0, attempted_boxes = 0, upstream_blocked_boxes = 0;
  for (size_t bi = 0; bi < results.size(); ++bi) {
    attempted_boxes += results[bi].attempted;
    upstream_blocked_boxes
        += std::ranges::any_of(obligations[bi], [&](size_t n) { return attempted[n] && !upstream_why[n].empty(); });
    assembly_complete[bi] = results[bi].status == Cellbox_sat_status::complete
                            && std::ranges::all_of(obligations[bi], [&](size_t n) { return !scope.required[n] || why[n].empty(); });
    complete_boxes += assembly_complete[bi];
    if (results[bi].attempted && !assembly_complete[bi]) {
      rep.final_failed_boxes.push_back(int(bi));
    }
  }
  rep.final_attempted_boxes        = attempted_boxes;
  rep.final_complete_boxes         = complete_boxes;
  rep.final_upstream_blocked_boxes = upstream_blocked_boxes;
  if (!all_boxes) {
    for (size_t bi = 0; bi < results.size() && rep.failure_sample.size() < 10; ++bi) {
      if (results[bi].status == Cellbox_sat_status::complete) {
        continue;
      }
      std::string names;
      const auto& box = boxes.boxes[bi];
      for (size_t k = 0; k < box.cells.size() && k < 3; ++k) {
        if (k) {
          names += ',';
        }
        names += d.fp.components.at(size_t(d.movable.at(size_t(box.cells[k])))).name;
      }
      if (box.cells.size() > 3) {
        names += ",...";
      }
      rep.failure_sample.push_back(std::format("cellbox {} tile {} members [{}] solver {} local {}: {}",
                                               bi,
                                               box.tile,
                                               names,
                                               int(results[bi].solver_status),
                                               int(results[bi].status),
                                               results[bi].reason.empty() ? "no completion reason recorded" : results[bi].reason));
    }
    rep.failure_sample.push_back("one or more joint cellbox SAT transactions did not complete");
  }
  rep.wires_emitted = !diagnostic && all_boxes && rep.failed_nets == 0 && rep.unchecked_nets == 0 && d.profile;
  const auto commit = [&] {
    if (rep.wires_emitted) {
      // Preserve source net order, complete connectivity, controls, PG and all
      // unrelated metadata. Only selected member poses, required wires/rules move.
      for (const int c : d.movable) {
        d.fp.components[size_t(c)].place = work.components[size_t(c)].place;
      }
      for (size_t n = 0; n < d.nets.size(); ++n) {
        if (scope.required[n]) {
          d.fp.nets[d.nets[n].frame].wires = std::move(work.nets[d.nets[n].frame].wires);
        }
      }
      d.fp.nondefault_rules = std::move(work.nondefault_rules);
    }
  };
  if (!d.opt.output_dir.empty()) {
    std::ofstream nets(d.opt.output_dir + "/final_nets.tsv");
    nets << "net_index\tnet\tpins\toutcome\n";
    for (size_t n = 0; n < d.nets.size(); ++n) {
      if (scope.required[n]) {
        nets << n << '\t' << std::quoted(d.fp.nets[d.nets[n].frame].name.str()) << '\t' << d.nets[n].pins.size() << '\t'
             << std::quoted(why[n].empty() ? "connected" : why[n]) << '\n';
      }
    }
    std::ofstream times(d.opt.output_dir + "/final_box_times.tsv");
    times << "box\tsolver_status\tlocal_status\tassembly_complete\tattempted\tlayouts\tcandidates\tSAT_calls\tlayout_SAT_"
             "calls\troute_SAT_calls\tSAT_seconds\ttotal_"
             "seconds\treason\tsetup_seconds\tlayout_seconds\tcandidate_seconds\tverification_seconds"
             "\tsetup_cpu_seconds\tlayout_cpu_seconds\tcandidate_cpu_seconds\tSAT_cpu_seconds\tverification_cpu_seconds"
             "\ttotal_cpu_seconds\troute_trials\tnative_rejections\tincompatible_pairs\trejected_models\tvariables\tclauses"
             "\tlayout_limit\tcandidate_limit\tconstruction_limit\tdeadline_hit\tgeneration_time_limit\n";
    times << std::setprecision(12);
    size_t              calls = 0, layout_calls = 0, route_calls = 0;
    double              sat = 0, total = 0;
    Cellbox_sat_metrics totals;
    size_t              layout_limits = 0, candidate_limits = 0, construction_limits = 0, deadlines = 0, generation_limits = 0;
    for (size_t bi = 0; bi < results.size(); ++bi) {
      const auto& r                    = results[bi];
      const auto& m                    = r.metrics;
      calls                           += r.metrics.sat_calls;
      layout_calls                    += r.metrics.layout_sat_calls;
      route_calls                     += r.metrics.route_sat_calls;
      sat                             += r.metrics.sat_seconds;
      total                           += r.metrics.total_seconds;
      totals.setup_seconds            += m.setup_seconds;
      totals.layout_seconds           += m.layout_seconds;
      totals.candidate_seconds        += m.candidate_seconds;
      totals.verification_seconds     += m.verification_seconds;
      totals.setup_cpu_seconds        += m.setup_cpu_seconds;
      totals.layout_cpu_seconds       += m.layout_cpu_seconds;
      totals.candidate_cpu_seconds    += m.candidate_cpu_seconds;
      totals.sat_cpu_seconds          += m.sat_cpu_seconds;
      totals.verification_cpu_seconds += m.verification_cpu_seconds;
      totals.total_cpu_seconds        += m.total_cpu_seconds;
      totals.route_trials             += m.route_trials;
      totals.native_rejections        += m.native_rejections;
      totals.incompatible_pairs       += m.incompatible_pairs;
      totals.rejected_models          += m.rejected_models;
      totals.variables                += m.variables;
      totals.clauses                  += m.clauses;
      layout_limits                   += m.layout_limit;
      candidate_limits                += m.candidate_limit;
      construction_limits             += m.construction_limit;
      deadlines                       += m.deadline_hit;
      generation_limits               += m.generation_time_limit;
      times << bi << '\t' << int(r.solver_status) << '\t' << int(r.status) << '\t' << assembly_complete[bi] << '\t' << r.attempted
            << '\t' << r.metrics.layouts << '\t' << r.metrics.candidates << '\t' << r.metrics.sat_calls << '\t'
            << r.metrics.layout_sat_calls << '\t' << r.metrics.route_sat_calls << '\t' << r.metrics.sat_seconds << '\t'
            << r.metrics.total_seconds << '\t' << std::quoted(r.reason) << '\t' << m.setup_seconds << '\t' << m.layout_seconds
            << '\t' << m.candidate_seconds << '\t' << m.verification_seconds << '\t' << m.setup_cpu_seconds << '\t'
            << m.layout_cpu_seconds << '\t' << m.candidate_cpu_seconds << '\t' << m.sat_cpu_seconds << '\t'
            << m.verification_cpu_seconds << '\t' << m.total_cpu_seconds << '\t' << m.route_trials << '\t' << m.native_rejections
            << '\t' << m.incompatible_pairs << '\t' << m.rejected_models << '\t' << m.variables << '\t' << m.clauses << '\t'
            << m.layout_limit << '\t' << m.candidate_limit << '\t' << m.construction_limit << '\t' << m.deadline_hit << '\t'
            << m.generation_time_limit << '\n';
    }
    std::ofstream report(d.opt.output_dir + "/final_runtime.json");
    report << std::setprecision(12) << "{\"solver\":\"native_joint_cellbox_sat\",\"diagnostic\":" << (diagnostic ? "true" : "false")
           << ",\"box_count\":" << boxes.boxes.size() << ",\"assembly_complete_boxes\":" << complete_boxes
           << ",\"attempted_boxes\":" << attempted_boxes << ",\"upstream_blocked_boxes\":" << upstream_blocked_boxes
           << ",\"SAT_calls\":" << calls << ",\"SAT_seconds\":" << sat << ",\"layout_SAT_calls\":" << layout_calls
           << ",\"route_SAT_calls\":" << route_calls
           << ",\"input_timing_basis\":\"pre_SAT_Placement\",\"selected_placement_timing_measured\":false"
           << ",\"box_total_seconds\":" << total << ",\"final_elapsed_seconds\":" << seconds_since(started)
           << ",\"box_phase_seconds\":{\"setup\":" << totals.setup_seconds << ",\"layout\":" << totals.layout_seconds
           << ",\"candidate\":" << totals.candidate_seconds << ",\"SAT\":" << sat
           << ",\"verification\":" << totals.verification_seconds << '}'
           << ",\"box_phase_cpu_seconds\":{\"setup\":" << totals.setup_cpu_seconds << ",\"layout\":" << totals.layout_cpu_seconds
           << ",\"candidate\":" << totals.candidate_cpu_seconds << ",\"SAT\":" << totals.sat_cpu_seconds
           << ",\"verification\":" << totals.verification_cpu_seconds << ",\"total\":" << totals.total_cpu_seconds << '}'
           << ",\"box_search_counts\":{\"route_trials\":" << totals.route_trials
           << ",\"native_rejections\":" << totals.native_rejections << ",\"incompatible_pairs\":" << totals.incompatible_pairs
           << ",\"rejected_models\":" << totals.rejected_models << ",\"variables\":" << totals.variables
           << ",\"clauses\":" << totals.clauses << '}' << ",\"box_limit_counts\":{\"layout\":" << layout_limits
           << ",\"candidate\":" << candidate_limits << ",\"construction\":" << construction_limits << ",\"deadline\":" << deadlines
           << ",\"generation_time\":" << generation_limits << '}' << ",\"required_physical_pins\":" << rep.final_required_pins
           << ",\"connected_physical_pins\":" << rep.final_connected_pins
           << ",\"wires_emitted\":" << (rep.wires_emitted ? "true" : "false") << "}\n";
    nets.close();
    times.close();
    report.close();
    if (!nets || !times || !report) {
      rep.wires_emitted = false;
      throw std::runtime_error("cannot write SAT Final reports");
    }
  }
  commit();
}
}  // namespace livehd::placement::lr
