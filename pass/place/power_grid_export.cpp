// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>

#include "inou/lefdef/cut_rules.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/routing_rules.hpp"
#include "power_grid.hpp"
#include "power_grid_vias.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement {
namespace {
namespace ph = physical;
ph::Rect intersection(const ph::Rect& a, const ph::Rect& b) {
  return {std::max(a.xl, b.xl), std::max(a.yl, b.yl), std::min(a.xh, b.xh), std::min(a.yh, b.yh)};
}
bool     nonempty(const ph::Rect& r) { return r.xl < r.xh && r.yl < r.yh; }
ph::Rect merged(const ph::Rect& a, const ph::Rect& b) {
  return {std::min(a.xl, b.xl), std::min(a.yl, b.yl), std::max(a.xh, b.xh), std::max(a.yh, b.yh)};
}
bool contains(const ph::Rect& a, const ph::Rect& b) { return a.xl <= b.xl && a.yl <= b.yl && a.xh >= b.xh && a.yh >= b.yh; }
std::array<int64_t, 2> size(const ph::Rect& r) { return {std::llround(r.xh - r.xl), std::llround(r.yh - r.yl)}; }
ph::Route_path         wire_path(const Pg_metal& s, const ph::Technology& tech, const std::string& shape) {
  const bool     horizontal = s.followpin || tech.layers.at(s.layer).direction == "HORIZONTAL";
  ph::Route_path p;
  p.layer = s.layer;
  p.shape = shape;
  if (horizontal) {
    p.width      = std::llround(s.rect.yh - s.rect.yl);
    const auto y = std::llround((s.rect.yh + s.rect.yl) / 2);
    p.steps      = {ph::Route_point{std::llround(s.rect.xl), y, {}}, ph::Route_point{std::llround(s.rect.xh), y, {}}};
  } else {
    p.width      = std::llround(s.rect.xh - s.rect.xl);
    const auto x = std::llround((s.rect.xh + s.rect.xl) / 2);
    p.steps      = {ph::Route_point{x, std::llround(s.rect.yl), {}}, ph::Route_point{x, std::llround(s.rect.yh), {}}};
  }
  return p;
}
}  // namespace
using Stack = std::tuple<std::string, std::string, std::string, int64_t, int64_t>;
static ph::Floorplan make_grid(const ph::Technology& tech, const ph::Floorplan& input, const Pg_profile& profile,
                               std::multiset<Stack>* stacks, Pg_channel_stats* stats) {
  if (!input.special_nets.empty()) {
    throw std::invalid_argument("native PG export requires an unwired power-grid frame");
  }
  std::set<std::string> via_names;
  for (const auto& via : input.vias) {
    if (via.name.empty() || !via_names.insert(via.name).second || (!via.generated && via.shapes.empty())) {
      throw std::invalid_argument("invalid or duplicate retained via definition");
    }
    (void)ph::via_geometry(tech, input, {via.name});
  }
  for (const auto& [name, via] : tech.vias) {
    via_names.insert(name);
  }
  for (const auto& cell : input.components) {
    const auto& m = tech.macros.at(cell.macro);
    if (m.virtual_proxy) {
      throw std::invalid_argument("native PG requires physical macro pin geometry");
    }
  }
  auto         plan        = plan_power_grid(tech, input, profile);
  const size_t grid_shapes = plan.metal.size();
  if (stats) {
    stats->removed_planned_length_um = plan.channel_removed_length_um;
  }
  // Pin shapes participate in via intersections, but remain LEF geometry: never
  // re-emit them as modifiable stripes or trim them to their contacts.
  std::map<std::string, std::vector<size_t>> macro_pins;
  for (const auto& cell : input.components) {
    const auto& master = tech.macros.at(cell.macro);
    if (!master.klass.starts_with("BLOCK")) {
      continue;
    }
    for (int n = 0; n < 2; ++n) {
      bool found_pin = false;
      for (const auto& pin_name : profile.pin_names[n]) {
        const auto pin = master.pins.find(pin_name);
        if (pin == master.pins.end()) {
          continue;
        }
        found_pin = true;
        auto& ids = macro_pins[cell.name + "/" + pin_name];
        for (const auto& shape : pin->second.shapes) {
          if (std::none_of(profile.macro_connect.begin(), profile.macro_connect.end(), [&](const auto& pair) {
                return pair[0] == shape.layer;
              })) {
            continue;
          }
          const auto point = [&](double x, double y) {
            auto [px, py]
                = ph::orient_point(x + master.origin_x, y + master.origin_y, master.width, master.height, cell.place.orientation);
            return std::pair{double(cell.place.x) + std::llround(px * input.dbu),
                             double(cell.place.y) + std::llround(py * input.dbu)};
          };
          auto [x0, y0] = point(shape.rect.xl, shape.rect.yl);
          auto [x1, y1] = point(shape.rect.xh, shape.rect.yh);
          ph::Rect rect{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
          if (!nonempty(rect) || !contains(input.die, rect)) {
            throw std::invalid_argument("invalid macro PG contact: " + cell.name + "/" + pin_name);
          }
          for (size_t id = grid_shapes; id < plan.metal.size(); ++id) {
            const auto& other = plan.metal[id];
            if (other.net != profile.nets[n] && other.layer == shape.layer && nonempty(intersection(other.rect, rect))) {
              throw std::invalid_argument("overlapping macro power/ground pins: " + cell.name + "/" + pin_name);
            }
          }
          ids.push_back(plan.metal.size());
          plan.metal.push_back({profile.nets[n], shape.layer, rect, false});
        }
        if (ids.empty()) {
          throw std::invalid_argument("macro PG pin has no supported contact layer: " + cell.name + "/" + pin_name);
        }
      }
      if (!found_pin) {
        throw std::invalid_argument("macro lacks profile PG pins: " + cell.name + "/" + profile.nets[n]);
      }
    }
  }
  // Explicit via-stack connections certify that the grid, including any
  // channel patches, does not leave a separately powered island. Internal shapes of one macro pin are
  // electrically equivalent; input placement is never modified on failure.
  const bool          verify_grid = !profile.channels.empty() || !profile.stripes.empty();
  std::vector<size_t> parent(verify_grid ? plan.metal.size() : 0);
  std::iota(parent.begin(), parent.end(), 0);
  const auto root = [&](size_t i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i         = parent[i];
    }
    return i;
  };
  const auto join = [&](size_t a, size_t b) {
    if (!parent.empty()) {
      parent[root(b)] = root(a);
    }
  };
  for (const auto& [pin, ids] : macro_pins) {
    for (size_t i = 1; i < ids.size(); ++i) {
      join(ids[0], ids[i]);
    }
  }
  auto                          out = input;
  std::map<std::string, size_t> nets;
  for (int n = 0; n < 2; ++n) {
    for (const auto& net : input.nets) {
      if (net.name == profile.nets[n]) {
        throw std::invalid_argument("PG name conflicts with an ordinary net");
      }
    }
    for (const auto& terminal : input.terminals) {
      if (terminal.name == profile.nets[n]) {
        throw std::invalid_argument("PG name conflicts with an existing terminal");
      }
    }
    nets[profile.nets[n]] = out.special_nets.size();
    ph::Net net;
    net.name = profile.nets[n];
    net.use  = n == 0 ? "POWER" : "GROUND";
    net.wires.push_back({"ROUTED", {}});
    net.connections.push_back({"PIN", net.name});
    for (const auto& cell : input.components) {
      const auto& master = tech.macros.at(cell.macro);
      for (const auto& pin : profile.pin_names[n]) {
        if (master.pins.contains(pin)) {
          net.connections.push_back({cell.name, pin});
        }
      }
    }
    out.special_nets.push_back(std::move(net));
  }
  const auto paths
      = [&](const std::string& net) -> std::vector<ph::Route_path>& { return out.special_nets.at(nets.at(net)).wires[0].paths; };
  // Available internal stack metal is at least the default via landing-pad
  // size on that layer, without enlarging the actual generated via enclosure.
  std::map<std::string, std::array<int64_t, 2>> landing;
  for (const auto& [name, via] : tech.vias) {
    if (!via.is_default || !via.unsupported.empty()) {
      continue;
    }
    for (const auto& shape : via.shapes) {
      if (tech.layers.contains(shape.layer)) {
        auto& dims = landing[shape.layer];
        dims[0]    = std::max(dims[0], int64_t(std::llround((shape.rect.xh - shape.rect.xl) * out.dbu)));
        dims[1]    = std::max(dims[1], int64_t(std::llround((shape.rect.yh - shape.rect.yl) * out.dbu)));
      }
    }
  }
  using CacheKey = std::tuple<std::string, std::string, std::array<int64_t, 2>, std::array<int64_t, 2>, bool, bool, bool, bool>;
  std::map<CacheKey, Pg_via_array>              cache;
  std::set<CacheKey>                            no_fit;
  std::map<std::string, std::vector<ph::Shape>> pads;
  std::map<std::string, std::string>            definitions;
  std::vector<std::optional<ph::Rect>>          attached(plan.metal.size());
  std::vector<Pg_metal>                         patches;
  auto                                          connections = profile.connect;
  connections.insert(connections.end(), profile.macro_connect.begin(), profile.macro_connect.end());
  for (size_t connection = 0; connection < connections.size(); ++connection) {
    const auto& pair             = connections[connection];
    const bool  macro_connection = connection >= profile.connect.size();
    if (macro_connection && macro_pins.empty()) {
      continue;
    }
    const auto lower = std::find(tech.routing_layers.begin(), tech.routing_layers.end(), pair[0]);
    const auto upper = std::find(tech.routing_layers.begin(), tech.routing_layers.end(), pair[1]);
    if (lower == tech.routing_layers.end() || upper == tech.routing_layers.end() || lower >= upper) {
      throw std::invalid_argument("invalid PG routing-stack connection");
    }
    const size_t steps = upper - lower;
    for (size_t i = 0; i < plan.metal.size(); ++i) {
      const auto& a = plan.metal[i];
      if (a.layer != pair[0] || (i >= grid_shapes) != macro_connection) {
        continue;
      }
      for (size_t j = 0; j < grid_shapes; ++j) {
        const auto& b = plan.metal[j];
        if (b.layer != pair[1] || a.net != b.net) {
          continue;
        }
        const auto overlap = intersection(a.rect, b.rect);
        if (!nonempty(overlap)) {
          continue;
        }
        const auto x = std::llround((overlap.xl + overlap.xh) / 2), y = std::llround((overlap.yl + overlap.yh) / 2);
        const auto centered_size = [&](const ph::Rect& rect) {
          return std::array<int64_t, 2>{std::llround(2 * std::min(x - rect.xl, rect.xh - x)),
                                        std::llround(2 * std::min(y - rect.yl, rect.yh - y))};
        };
        std::vector<std::array<int64_t, 2>> available{macro_connection ? centered_size(a.rect) : size(a.rect)};
        for (size_t k = 1; k < steps; ++k) {
          auto        dims    = size(overlap);
          const auto& minimum = landing[*(lower + k)];
          for (int axis = 0; axis < 2; ++axis) {
            dims[axis] = std::max(dims[axis], minimum[axis]);
          }
          available.push_back(dims);
        }
        available.push_back(macro_connection ? centered_size(b.rect) : size(b.rect));
        std::map<std::string, std::vector<ph::Rect>> stack_pads;
        std::vector<const Pg_via_array*>             arrays;
        // Preflight the complete stack before publishing any via or pad. A
        // grazing macro-pin crossing may be unusable while other crossings
        // connect that same terminal. Unsupported rules still fail explicitly.
        for (size_t k = 0; k < steps; ++k) {
          Pg_via_request req{*(lower + k), *(lower + k + 1), available[k], available[k + 1], k != 0, k + 1 != steps};
          req.lower_fixed = macro_connection && k == 0;
          const CacheKey key{req.lower,
                             req.upper,
                             req.lower_size,
                             req.upper_size,
                             req.lower_internal,
                             req.upper_internal,
                             req.lower_fixed,
                             req.upper_fixed};
          if (macro_connection && no_fit.contains(key)) {
            break;
          }
          auto found = cache.find(key);
          if (found == cache.end()) {
            try {
              found = cache.emplace(key, make_pg_via(tech, out.dbu, req)).first;
            } catch (const Pg_via_no_fit&) {
              if (!macro_connection) {
                throw;
              }
              no_fit.insert(key);
              break;
            }
          }
          arrays.push_back(&found->second);
        }
        if (arrays.size() != steps) {
          continue;
        }
        for (size_t k = 0; k < steps; ++k) {
          const auto& array = *arrays[k];
          if (!definitions.contains(array.via.name)) {
            auto     definition = array.via;
            unsigned suffix     = 0;
            while (!via_names.insert(definition.name).second) {
              definition.name = array.via.name + "_pg_" + std::to_string(++suffix);
            }
            definitions.emplace(array.via.name, definition.name);
            out.vias.push_back(definition);
            auto geometry = ph::via_geometry(tech, out, {definition.name});
            for (const auto& shape : geometry) {
              if (tech.layers.contains(shape.layer)) {
                pads[definition.name].push_back(shape);
              }
            }
          }
          const auto&    via_name = definitions.at(array.via.name);
          ph::Route_path p;
          p.layer = array.layers[0];
          p.width = 0;
          p.shape = "STRIPE";
          p.steps = {ph::Route_point{x, y, {}}, ph::Route_via{via_name}};
          paths(a.net).push_back(std::move(p));
          for (const auto& pad : pads.at(via_name)) {
            auto r  = pad.rect;
            r.xl   += x;
            r.xh   += x;
            r.yl   += y;
            r.yh   += y;
            if (!contains(out.die, r)) {
              throw std::invalid_argument("PG via landing pad lies outside die");
            }
            if (macro_connection && pad.layer == a.layer && !contains(a.rect, r)) {
              throw std::invalid_argument("PG via exceeds fixed macro pin contact");
            }
            stack_pads[pad.layer].push_back(r);
            for (auto id : {i, j}) {
              if (pad.layer == plan.metal[id].layer) {
                attached[id] = attached[id] ? merged(*attached[id], r) : r;
              }
            }
          }
        }
        if (stacks) {
          stacks->emplace(a.net, pair[0], pair[1], x, y);
        }
        join(i, j);
        for (size_t k = 1; k < steps; ++k) {
          const auto& layer  = *(lower + k);
          const auto& shapes = stack_pads.at(layer);
          auto        rect   = shapes.front();
          for (const auto& shape : shapes) {
            rect = merged(rect, shape);
          }
          if (std::none_of(shapes.begin(), shapes.end(), [&](const auto& shape) { return contains(shape, rect); })) {
            patches.push_back({a.net, layer, rect, false});
          }
        }
      }
    }
  }
  if (verify_grid) {
    std::map<std::string, size_t> roots;
    for (size_t i = 0; i < plan.metal.size(); ++i) {
      const auto [it, added] = roots.emplace(plan.metal[i].net, root(i));
      if (!added && it->second != root(i)) {
        throw std::invalid_argument("disconnected power-grid metal");
      }
    }
    if (roots.size() != 2) {
      throw std::invalid_argument("missing power-grid net");
    }
  }
  for (const auto& [pin, ids] : macro_pins) {
    if (std::none_of(ids.begin(), ids.end(), [&](size_t id) { return attached[id].has_value(); })) {
      throw std::invalid_argument("macro PG pin has no grid connection: " + pin);
    }
  }
  for (size_t i = 0; i < grid_shapes; ++i) {
    auto shape = plan.metal[i];
    if (!attached[i]) {
      throw std::invalid_argument("PG metal has no via connection on " + shape.layer);
    }
    if (!shape.followpin) {
      const auto& ext = *attached[i];
      if (tech.layers.at(shape.layer).direction == "VERTICAL") {
        shape.rect.yl = shape.layer == profile.pin_layer ? std::min(shape.rect.yl, ext.yl) : ext.yl;
        shape.rect.yh = shape.layer == profile.pin_layer ? std::max(shape.rect.yh, ext.yh) : ext.yh;
      } else {
        shape.rect.xl = shape.layer == profile.pin_layer ? std::min(shape.rect.xl, ext.xl) : ext.xl;
        shape.rect.xh = shape.layer == profile.pin_layer ? std::max(shape.rect.xh, ext.xh) : ext.xh;
      }
    }
    if (!contains(out.die, shape.rect)) {
      throw std::invalid_argument("native PG metal extends outside die");
    }
    if (!profile.channels.empty() && !shape.followpin) {
      const auto& rule = tech.layers.at(shape.layer).rules;
      if (!rule.loaded || !std::isfinite(rule.area) || rule.area < 0
          || (shape.rect.xh - shape.rect.xl) * (shape.rect.yh - shape.rect.yl) + 1e-9 < rule.area * out.dbu * out.dbu) {
        throw std::invalid_argument("channel trimming leaves an unsupported or undersized stripe");
      }
    }
    paths(shape.net).push_back(wire_path(shape, tech, shape.followpin ? "FOLLOWPIN" : "STRIPE"));
    if (shape.layer == profile.pin_layer) {
      auto it = std::find_if(out.terminals.begin(), out.terminals.end(), [&](const auto& t) { return t.name == shape.net; });
      if (it == out.terminals.end()) {
        ph::Terminal terminal;
        terminal.name      = shape.net;
        terminal.net       = shape.net;
        terminal.direction = "INOUT";
        terminal.use       = shape.net == profile.nets[0] ? "POWER" : "GROUND";
        terminal.status    = ph::Status::FIXED;
        out.terminals.push_back(terminal);
        it = out.terminals.end() - 1;
      }
      it->shapes.push_back({shape.layer, shape.rect});
    }
  }
  for (const auto& patch : patches) {
    paths(patch.net).push_back(wire_path(patch, tech, "DRCFILL"));
  }
  if (!profile.channels.empty()) {
    for (const auto& name : profile.nets) {
      if (std::none_of(out.terminals.begin(), out.terminals.end(), [&](const auto& pin) {
            return pin.net == name && !pin.shapes.empty();
          })) {
        throw std::invalid_argument("channel removes every supply terminal for " + name);
      }
    }
    for (const auto& net : out.special_nets) {
      for (const auto& shape : ph::route_geometry(tech, out, net, true)) {
        for (const auto& channel : profile.channels) {
          const ph::Rect region{double(std::llround(channel.region.xl * out.dbu)),
                                double(std::llround(channel.region.yl * out.dbu)),
                                double(std::llround(channel.region.xh * out.dbu)),
                                double(std::llround(channel.region.yh * out.dbu))};
          if (shape.layer == channel.layer && nonempty(intersection(shape.rect, region))) {
            throw std::invalid_argument("PG wire or via landing re-enters a channel");
          }
        }
      }
    }
  }
  return out;
}
std::map<std::string, uint64_t> count_channel_tracks(const ph::Technology& tech, const ph::Floorplan& reference,
                                                     const ph::Floorplan& patched, const std::vector<Pg_channel>& channels) {
  if (reference.dbu <= 0 || reference.dbu != patched.dbu || reference.die != patched.die || reference.tracks != patched.tracks) {
    throw std::invalid_argument("channel track comparison requires matching frame and tracks");
  }
  ph::Signal_metal_rules rules(tech, patched);
  std::set<std::string> channel_layers;
  for (const auto& channel : channels) {
    if (!rules.clearance(channel.layer)) {
      throw std::invalid_argument("unsupported channel track rules on " + channel.layer);
    }
    channel_layers.insert(channel.layer);
  }
  const auto before = ph::routing_occupancy(tech, reference, reference.dbu, {}, nullptr, ph::Signal_wires::include, channel_layers);
  const auto after  = ph::routing_occupancy(tech, patched, patched.dbu, {}, nullptr, ph::Signal_wires::include, channel_layers);
  std::map<std::string, std::set<int64_t>> freed;
  const double                             grid = tech.manufacturing_grid ? tech.manufacturing_grid * patched.dbu : 1;
  if (!std::isfinite(grid) || grid < 1 || grid > INT32_MAX || grid != std::round(grid)) {
    throw std::invalid_argument("unrepresentable channel manufacturing grid");
  }
  for (const auto& channel : channels) {
    auto&        found      = freed[channel.layer];
    const auto&  layer      = tech.layers.at(channel.layer);
    const bool   horizontal = layer.direction == "HORIZONTAL";
    const double width      = layer.width * patched.dbu;
    if ((!horizontal && layer.direction != "VERTICAL") || !std::isfinite(width) || width <= 0 || width > INT32_MAX
        || width != std::round(width) || std::llround(width) % 2) {
      throw std::invalid_argument("unsupported channel signal width/direction");
    }
    const auto reach = *rules.clearance(channel.layer);
    ph::Rect   region{channel.region.xl * patched.dbu,
                    channel.region.yl * patched.dbu,
                    channel.region.xh * patched.dbu,
                    channel.region.yh * patched.dbu};
    for (const auto value : {region.xl, region.yl, region.xh, region.yh}) {
      if (!std::isfinite(value) || value < INT32_MIN || value > INT32_MAX) {
        throw std::invalid_argument("invalid channel accounting extent");
      }
    }
    region         = intersection(region, patched.die);
    const auto low = horizontal ? region.yl : region.xl, high = horizontal ? region.yh : region.xh;
    const auto start = std::ceil(((horizontal ? region.xl : region.yl) + reach) / grid) * grid;
    const auto end   = std::floor(((horizontal ? region.xh : region.yh) - reach) / grid) * grid;
    if (!nonempty(region) || start >= end) {
      continue;
    }
    bool has_tracks = false;
    for (const auto& track : patched.tracks) {
      if (track.layer != channel.layer || track.axis != (horizontal ? "Y" : "X")) {
        continue;
      }
      has_tracks = true;
      if (track.mask || track.same_mask || track.step <= 0 || track.count <= 0 || track.step > INT32_MAX || track.count > INT32_MAX
          || track.start < INT32_MIN || track.start > INT32_MAX) {
        throw std::invalid_argument("unsupported channel track pattern");
      }
      const auto first = std::max<int64_t>(0, std::ceil((low + width / 2 - track.start) / track.step));
      const auto last  = std::min<int64_t>(track.count - 1, std::floor((high - width / 2 - track.start) / track.step));
      if (last - first > 1000000) {
        throw std::invalid_argument("oversized channel track set");
      }
      for (auto i = first; i <= last; ++i) {
        const auto c = track.start + i * track.step;
        if (found.contains(c)) {
          continue;
        }
        const ph::Shape wire{
            channel.layer,
            horizontal ? ph::Rect{start, c - width / 2, end, c + width / 2} : ph::Rect{c - width / 2, start, c + width / 2, end}};
        if (!rules.rectangle(wire).legal) {
          continue;
        }
        const auto legal = [&](const ph::Routing_occupancy& occupancy) {
          for (const auto id : occupancy.query(channel.layer, wire.rect, reach)) {
            if (!rules.spacing(wire, occupancy.at(id).shape).legal) {
              return false;
            }
          }
          return true;
        };
        if (legal(after) && !legal(before)) {
          found.insert(c);
        }
      }
    }
    if (!has_tracks) {
      throw std::invalid_argument("missing channel signal tracks on " + channel.layer);
    }
  }
  std::map<std::string, uint64_t> result;
  for (const auto& [layer, tracks] : freed) {
    result.emplace(layer, tracks.size());
  }
  return result;
}
Pg_profile reserve_power_grid_channels(const ph::Technology& tech, const ph::Floorplan& input, const Pg_profile& profile) {
  auto reserved = profile;
  if (std::none_of(input.nets.begin(), input.nets.end(), [](const auto& net) { return !net.wires.empty(); })) {
    return reserved;
  }
  const auto            plan = plan_power_grid(tech, input, profile);
  ph::Routing_occupancy signals(input.dbu);
  std::set<std::string> layers;
  for (const auto& net : input.nets) {
    for (const auto& shape : ph::route_geometry(tech, input, net)) {
      if (tech.layers.contains(shape.layer)) {
        signals.insert({shape, net.name});
        layers.insert(shape.layer);
      }
    }
  }
  const ph::Signal_metal_rules rules(tech, input);
  for (const auto& metal : plan.metal) {
    if (!layers.contains(metal.layer)) {
      continue;
    }
    const auto reach = rules.clearance(metal.layer);
    if (!reach) {
      throw std::invalid_argument("unsupported signal reservation rules on " + metal.layer);
    }
    bool conflict = false;
    for (const auto id : signals.query(metal.layer, metal.rect, *reach)) {
      if (!rules.spacing({metal.layer, metal.rect}, signals.at(id).shape).legal) {
        conflict = true;
        break;
      }
    }
    if (!conflict) {
      continue;
    }
    if (metal.followpin
        || std::find(profile.followpin_layers.begin(), profile.followpin_layers.end(), metal.layer)
               != profile.followpin_layers.end()) {
      throw std::invalid_argument("signal reservation conflicts with immutable followpin rail on " + metal.layer);
    }
    // Keep the complete stripe cross-section and extent. Cutting only at the
    // signal crossing could create isolated islands or inadequate end spacing.
    reserved.channels.push_back(
        {metal.layer,
         "skip",
         {metal.rect.xl / input.dbu, metal.rect.yl / input.dbu, metal.rect.xh / input.dbu, metal.rect.yh / input.dbu}});
  }
  return reserved;
}
ph::Floorplan make_power_grid(const ph::Technology& tech, const ph::Floorplan& input, const Pg_profile& profile,
                              Pg_channel_stats* stats, bool preserve_signal_terminals) {
  Pg_channel_stats     measured;
  const bool           compare = stats && !profile.channels.empty();
  std::multiset<Stack> patched, reference;
  auto                 out = make_grid(tech, input, profile, compare ? &patched : nullptr, stats ? &measured : nullptr);
  // Validate the final emitted shapes, including via landing extensions and
  // patches, rather than only the nominal stripes. A failed grid is never
  // published over retained signal wiring. The unwired placement path is free
  // of this extra indexing/rule-compilation cost.
  if (preserve_signal_terminals
      || std::any_of(input.nets.begin(), input.nets.end(), [](const auto& net) { return !net.wires.empty(); })) {
    ph::Routing_occupancy supply(out.dbu);
    for (const auto& net : out.special_nets) {
      for (const auto& shape : ph::route_geometry(tech, out, net, true)) {
        supply.insert({shape, net.name});
      }
    }
    const ph::Signal_metal_rules rules(tech, out);
    const auto                   check = [&](const ph::Shape& shape, const std::string& name) {
      const auto cut   = tech.cut_layers.find(shape.layer);
      const auto reach = cut == tech.cut_layers.end() ? rules.clearance(shape.layer)
                                                                        : std::optional<int64_t>{ph::signal_cut_spacing(cut->second, out.dbu)};
      if (!reach) {
        throw std::invalid_argument("unsupported retained signal/PG spacing on " + shape.layer);
      }
      for (const auto id : supply.query(shape.layer, shape.rect, *reach)) {
        const auto& obstacle = supply.at(id);
        bool        legal;
        if (cut == tech.cut_layers.end()) {
          legal = rules.spacing(shape, obstacle.shape).legal;
        } else {
          const auto& a  = shape.rect;
          const auto& b  = obstacle.shape.rect;
          const auto  dx = std::max({0., a.xl - b.xh, b.xl - a.xh});
          const auto  dy = std::max({0., a.yl - b.yh, b.yl - a.yh});
          legal          = !(dx == 0 && dy == 0) && !(dx < *reach && dy < *reach);
        }
        if (!legal) {
          throw std::invalid_argument("power grid conflicts with retained signal " + name + " on " + shape.layer + " ("
                                      + obstacle.net + ")");
        }
      }
    };
    for (const auto& net : input.nets) {
      for (const auto& shape : ph::route_geometry(tech, input, net)) {
        check(shape, net.name);
      }
    }
    if (preserve_signal_terminals) {
      for (const auto& terminal : input.terminals) {
        if (terminal.use == "POWER" || terminal.use == "GROUND") {
          continue;
        }
        if (!terminal.shapes.empty() && terminal.status == ph::Status::UNPLACED) {
          throw std::invalid_argument("cannot preserve unplaced signal IO during PG generation: " + terminal.name);
        }
        for (const auto& shape : terminal.shapes) {
          const auto a = ph::orient_point(shape.rect.xl, shape.rect.yl, 0, 0, terminal.orientation);
          const auto b = ph::orient_point(shape.rect.xh, shape.rect.yh, 0, 0, terminal.orientation);
          check({shape.layer,
                 {terminal.x + std::min(a.first, b.first),
                  terminal.y + std::min(a.second, b.second),
                  terminal.x + std::max(a.first, b.first),
                  terminal.y + std::max(a.second, b.second)}},
                "IO " + terminal.name);
        }
      }
    }
  }
  if (compare) {
    auto baseline = profile;
    baseline.channels.clear();
    const auto original = make_grid(tech, input, baseline, &reference, nullptr);
    try {
      measured.freed_tracks = count_channel_tracks(tech, original, out, profile.channels);
    } catch (const std::invalid_argument& error) {
      measured.track_count_reason = error.what();
    }
    for (const auto& stack : reference) {
      const auto found = patched.find(stack);
      if (found != patched.end()) {
        patched.erase(found);
      } else {
        ++measured.removed_stacks[std::get<1>(stack) + ":" + std::get<2>(stack)];
      }
    }
    for (const auto& stack : patched) {
      ++measured.added_stacks[std::get<1>(stack) + ":" + std::get<2>(stack)];
    }
  }
  if (stats) {
    *stats = std::move(measured);
  }
  return out;
}
void write_power_grid_report(const ph::Floorplan& fp, const Pg_profile& profile, const std::string& output_path,
                             const Pg_channel_stats* stats) {
  if (profile.channel_max_ir_drop_v
      && (!std::isfinite(*profile.channel_max_ir_drop_v) || *profile.channel_max_ir_drop_v < 0)) {
    throw std::invalid_argument("PG channel IR-drop limit must be finite and nonnegative (volts)");
  }
  std::map<std::string, double> length;
  size_t                        via_count = 0;
  for (const auto& net : fp.special_nets) {
    for (const auto& wire : net.wires) {
      for (const auto& path : wire.paths) {
        std::optional<ph::Route_point> previous;
        for (const auto& step : path.steps) {
          if (const auto* point = std::get_if<ph::Route_point>(&step)) {
            if (previous) {
              length[path.layer] += (std::abs(double(point->x) - previous->x) + std::abs(double(point->y) - previous->y)) / fp.dbu;
            }
            previous = *point;
          } else if (std::holds_alternative<ph::Route_via>(step)) {
            ++via_count;
          }
        }
      }
    }
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> report(buffer);
  report.StartObject();
  report.Key("schema_version");
  report.Int(2);
  report.Key("profile");
  report.String(profile.name.c_str());
  report.Key("reference_revision");
  report.String(profile.reference_revision.c_str());
  // Power first, then ground, with the cell pin names each net connects.
  report.Key("supplies");
  report.StartArray();
  for (size_t i = 0; i < profile.nets.size(); ++i) {
    report.StartObject();
    report.Key("net");
    report.String(profile.nets[i].c_str());
    report.Key("pins");
    report.StartArray();
    for (const auto& pin : profile.pin_names[i]) {
      report.String(pin.c_str());
    }
    report.EndArray();
    report.EndObject();
  }
  report.EndArray();
  report.Key("channels");
  report.StartArray();
  for (const auto& channel : profile.channels) {
    report.StartObject();
    report.Key("style");
    report.String(channel.style.c_str());
    report.Key("layer");
    report.String(channel.layer.c_str());
    report.Key("region_um");
    report.StartArray();
    for (auto value : {channel.region.xl, channel.region.yl, channel.region.xh, channel.region.yh}) {
      report.Double(value);
    }
    report.EndArray();
    report.EndObject();
  }
  report.EndArray();
  report.Key("channel_accounting");
  if (stats) {
    report.StartObject();
    report.Key("removed_planned_stripe_length_um");
    report.StartObject();
    for (const auto& [layer, value] : stats->removed_planned_length_um) {
      report.Key(layer.c_str());
      report.Double(value);
    }
    report.EndObject();
    const auto counts = [&](const char* name, const auto& values) {
      report.Key(name);
      report.StartObject();
      for (const auto& [pair, value] : values) {
        report.Key(pair.c_str());
        report.Uint64(value);
      }
      report.EndObject();
    };
    counts("removed_stacks_by_layer_pair", stats->removed_stacks);
    counts("added_stacks_by_layer_pair", stats->added_stacks);
    report.Key("freed_signal_tracks_by_layer");
    if (stats->freed_tracks) {
      report.StartObject();
      for (const auto& [layer, count] : *stats->freed_tracks) {
        report.Key(layer.c_str());
        report.Uint64(count);
      }
      report.EndObject();
    } else {
      report.Null();
    }
    report.Key("freed_signal_tracks_model");
    report.String(
        "distinct centerlines with newly legal full-channel corridors, clearance-inset ends; conservative, no via-access claim");
    report.Key("freed_signal_tracks_unmeasured_reason");
    report.String(stats->track_count_reason.c_str());
    report.EndObject();
  } else {
    report.Null();
  }
  report.Key("channel_ir_drop_verified");
  report.Bool(false);
  report.Key("channel_max_ir_drop_v");
  if (profile.channel_max_ir_drop_v) {
    report.Double(*profile.channel_max_ir_drop_v);
  } else {
    report.Null();
  }
  report.Key("via_instances");
  report.Uint64(via_count);
  report.Key("via_definitions");
  report.Uint64(fp.vias.size());
  report.Key("wire_length_um");
  report.StartObject();
  for (const auto& [layer, value] : length) {
    report.Key(layer.c_str());
    report.Double(value);
  }
  report.EndObject();
  report.Key("unconnected_pg_pins");
  report.Null();
  report.Key("independent_connectivity_verified");
  report.Bool(false);
  report.Key("validation_required");
  report.String("OpenROAD check_power_grid, DRC and IR analysis");
  report.EndObject();
  std::ofstream output(output_path);
  if (!(output << buffer.GetString() << '\n')) {
    throw std::runtime_error("cannot write native power-grid report");
  }
}
}  // namespace livehd::placement
