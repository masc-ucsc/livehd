// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute driver: legroute.local (L1 grid, L2 boxes, L3 SAT placement, L4 clock spines, L5 tracks) ->
// legroute.final (F1-F4).
#include "legroute.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "core/json_util.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/routing_rules.hpp"
#include "legroute_internal.hpp"

namespace livehd::placement {
namespace lr {
Legroute_gate_report paired_pin_supply_gate(const Design& d, const Grid& g) {
  Legroute_gate_report gate;
  gate.density_limit = d.opt.density_ceiling;
  std::set<int>    regions;
  std::set<size_t> components;
  for (const auto& reserve : g.paired_pin_reservations) {
    if (!reserve.short_supply && !reserve.grouping_invalid) {
      continue;
    }
    ++gate.unfittable_boxes;
    regions.insert(g.region.at(size_t(reserve.tile)));
    for (int cell : reserve.cells) {
      components.insert(size_t(d.movable.at(size_t(cell))));
    }
    const auto x = double(g.left(g.col(reserve.tile))), y = double(g.bottom(g.row(reserve.tile)));
    gate.hotspots.push_back({x, y, x + g.tile_w, y + g.tile_h});
  }
  gate.unfittable_regions = regions.size();
  gate.affected_components.assign(components.begin(), components.end());
  return gate;
}

void write_paired_pin_reservations(const Design& d, const Grid& g, const std::string& filename) {
  std::filesystem::create_directories(std::filesystem::path(filename).parent_path());
  std::ofstream out(filename);
  out << "{\"schema_version\":1,\"policy\":\"paired_external_pins\",\"pin_cap\":" << d.opt.paired_pin_cap << ','
      << "\"scope\":\"ordinary_required_pins\",\"fixed_boxes\":true,"
         "\"certificate\":\"anonymous_reservation_counts_only\",\"boxes\":[";
  bool first = true;
  for (const auto& reserve : g.paired_pin_reservations) {
    out << (first ? "" : ",")
        << std::format(
               R"({{"box":{},"tile":{},"external_pins":{},"external_rows":[{},{}],"vertical_layer":"{}",)"
               R"("vertical_reserved":{},"vertical_usable":{},"internal_vertical_estimate":{},"internal_horizontal_estimate":{},"horizontal_layer":"{}",)"
               R"("horizontal_reserved":[{},{}],"horizontal_usable":[{},{}],"opposite_row_pair_deficit":{},)"
               R"("short_supply":{},"grouping_invalid":{},"actual_pairs":{},"cells":[)",
               reserve.box,
               reserve.tile,
               reserve.external_pins.size(),
               reserve.external_rows[0],
               reserve.external_rows[1],
               json_util::escape(g.layers.at(size_t(reserve.vertical_layer)).name),
               reserve.vertical_tracks,
               reserve.vertical_usable,
               reserve.internal_vertical_estimate,
               reserve.internal_horizontal_estimate,
               json_util::escape(g.box_layer),
               reserve.horizontal_tracks[0],
               reserve.horizontal_tracks[1],
               reserve.horizontal_usable[0],
               reserve.horizontal_usable[1],
               std::max(reserve.external_rows[0], reserve.external_rows[1]) > reserve.vertical_tracks
                   ? std::max(reserve.external_rows[0], reserve.external_rows[1]) - reserve.vertical_tracks
                   : 0,
               reserve.short_supply,
               reserve.grouping_invalid,
               reserve.actual_pairs);
    first = false;
    for (size_t i = 0; i < reserve.cells.size(); ++i) {
      const auto  comp = size_t(d.movable.at(size_t(reserve.cells[i])));
      const auto& cell = d.fp.components.at(comp);
      out << (i ? "," : "")
          << std::format(R"({{"component":{},"name":"{}","x":{},"y":{}}})",
                         comp,
                         json_util::escape(cell.name),
                         cell.place.x,
                         cell.place.y);
    }
    out << "],\"internal_nets\":[";
    for (size_t i = 0; i < reserve.internal_nets.size(); ++i) {
      out << (i ? "," : "") << d.nets.at(size_t(reserve.internal_nets[i])).frame;
    }
    out << "],\"pin_obligations\":[";
    for (size_t i = 0; i < reserve.external_pins.size(); ++i) {
      const auto& obligation = reserve.external_pins[i];
      const auto& net        = d.nets.at(size_t(obligation.net));
      const auto& pin        = net.pins.at(size_t(obligation.pin));
      out << (i ? "," : "")
          << std::format(R"({{"net_index":{},"net":"{}","pin_index":{},"component":{},"pin":"{}","row":{}}})",
                         net.frame,
                         json_util::escape(d.fp.nets.at(net.frame).name),
                         obligation.pin,
                         pin.comp,
                         json_util::escape(pin.name),
                         obligation.row);
    }
    out << "],\"layers\":[";
    for (size_t l = 0; l < g.resources.layers.size(); ++l) {
      const auto& layer = g.resources.layers[l];
      const auto& tile  = layer.tiles.at(size_t(reserve.tile));
      out << (l ? "," : "")
          << std::format(R"({{"layer":"{}","usable":{},"protected":{},"remaining_coarse_capacity":{}}})",
                         json_util::escape(layer.name),
                         layer.usable_capacity(size_t(reserve.tile)),
                         tile.cellbox_budget,
                         tile.coarse_capacity);
    }
    out << "]}";
  }
  out << "]}\n";
  if (!out) {
    throw std::runtime_error("cannot write paired-pin reservation report: " + filename);
  }
}

void write_local_track_requests(const Design& d, const Grid& g, const Routing& routing, const std::string& directory) {
  std::filesystem::create_directories(directory);
  std::ofstream segments(directory + "/local_segments.tsv"), nodes(directory + "/local_nodes.tsv"),
      resources(directory + "/local_resources.tsv");
  segments << "segment\tnet_index\tnet\tlayer\tband\tlo_tile\thi_tile\ttrack\tcoordinate_dbu\tstate\tnode_lo\tnode_hi"
              "\tjog_lo_layer\tjog_lo_track\tjog_lo_state\tjog_hi_layer\tjog_hi_track\tjog_hi_state\n";
  for (size_t i = 0; i < routing.segments.size(); ++i) {
    const auto& s     = routing.segments[i];
    const auto& net   = d.nets.at(size_t(s.net));
    const auto& layer = g.layers.at(size_t(s.layer));
    segments << i << '\t' << net.frame << '\t' << json_util::escape(d.fp.nets.at(net.frame).name) << '\t' << layer.name << '\t'
             << s.band << '\t' << s.lo << '\t' << s.hi << '\t' << s.track << '\t'
             << (s.track < 0 ? -1 : g.track_coord(s.layer, s.track)) << '\t' << int(s.track_state) << '\t' << s.node_lo << '\t'
             << s.node_hi << '\t' << s.jog_lo_layer << '\t' << s.jog_lo << '\t' << int(s.jog_lo_state) << '\t' << s.jog_hi_layer
             << '\t' << s.jog_hi << '\t' << int(s.jog_hi_state) << '\n';
  }
  nodes << "net_index\tnode\ttile\tbox\tterminal\triser_layer\triser_track\triser_state\tstub_layer\tstub_track\tstub_state"
           "\tstub_access_state\n";
  for (size_t n = 0; n < routing.trees.size(); ++n) {
    for (size_t i = 0; i < routing.trees[n].nodes.size(); ++i) {
      const auto& p = routing.trees[n].nodes[i];
      nodes << d.nets.at(n).frame << '\t' << i << '\t' << p.tile << '\t' << p.box << '\t' << p.term << '\t' << p.riser_layer << '\t'
            << p.riser_track << '\t' << int(p.riser_state) << '\t' << p.stub_layer << '\t' << p.stub_track << '\t'
            << int(p.stub_state) << '\t' << int(p.stub_access_state) << '\n';
    }
  }
  resources << "layer_index\tlayer\thorizontal\ttile\tcol\trow\tband\tphysical_slots\tusable\tbox_budget\tcoarse_capacity\n";
  for (size_t l = 0; l < g.resources.layers.size(); ++l) {
    const auto& layer = g.resources.layers[l];
    for (size_t t = 0; t < layer.tiles.size(); ++t) {
      const auto& tile = layer.tiles[t];
      resources << l << '\t' << layer.name << '\t' << layer.horizontal << '\t' << t << '\t' << g.col(int(t)) << '\t'
                << g.row(int(t)) << '\t' << tile.band << '\t' << layer.bands.at(tile.band).size() << '\t'
                << layer.usable_capacity(t) << '\t' << tile.cellbox_budget << '\t' << tile.coarse_capacity << '\n';
    }
  }
  if (!segments || !nodes || !resources) {
    throw std::runtime_error("cannot write Local track diagnostics in " + directory);
  }
}

Legroute_options sat_alt_options(const Legroute_options& base) {
  auto             o = base;
  std::string_view rest(base.sat_alt);
  o.sat_alt.clear();
  while (!rest.empty()) {
    const auto comma = rest.find(',');
    const auto item  = rest.substr(0, comma);
    rest             = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
    if (item.empty()) {
      continue;
    }
    const auto eq = item.find('=');
    if (eq == std::string_view::npos) {
      throw std::invalid_argument(std::format("legroute: sat_alt item '{}' is not key=value", item));
    }
    const std::string key(item.substr(0, eq)), value(item.substr(eq + 1));
    const auto        number = [&] {
      size_t used = 0;
      double v    = 0;
      try {
        v = std::stod(value, &used);
      } catch (const std::exception&) {
        used = 0;
      }
      if (used == 0 || used != value.size()) {
        throw std::invalid_argument(std::format("legroute: sat_alt {} is not a number", key));
      }
      return v;
    };
    const auto integer = [&] {
      const double v = number();
      if (!std::isfinite(v) || v < 0 || v > std::numeric_limits<unsigned>::max() || std::floor(v) != v) {
        throw std::invalid_argument(std::format("legroute: sat_alt {} is not an unsigned integer", key));
      }
      return static_cast<unsigned>(v);
    };
    const auto flag = [&] {
      if (value != "true" && value != "false" && value != "1" && value != "0") {
        throw std::invalid_argument(std::format("legroute: sat_alt {} is not a boolean", key));
      }
      return value == "true" || value == "1";
    };
    if (key == "crit_boxes") {
      o.crit_boxes = number();
    } else if (key == "tree_boxes") {
      o.tree_boxes = number();
    } else if (key == "tree_moves") {
      o.tree_moves = integer();
    } else if (key == "tree_load") {
      o.tree_load = number();
    } else if (key == "tree_clauses") {
      o.tree_clauses = number();
    } else if (key == "tree_congestion") {
      o.tree_congestion = number();
    } else if (key == "tree_by_crit") {
      o.tree_by_crit = flag();
    } else if (key == "io_preroute") {
      o.io_preroute = flag();
    } else if (key == "window_tiles") {
      o.window_tiles = integer();
    } else {
      throw std::invalid_argument(std::format("legroute: sat_alt option '{}' is not a policy option", key));
    }
  }
  return o;
}
std::vector<std::string> tier_names(const std::string& tiers) {
  std::vector<std::string> names;
  for (size_t i = 0; i <= tiers.size();) {
    const size_t j = std::min(tiers.size(), tiers.find(',', i));
    if (j > i) {
      names.push_back(tiers.substr(i, j - i));
    }
    i = j + 1;
  }
  return names;
}
}  // namespace lr

void validate_legroute_options(const Legroute_options& o) {
  const auto fail = [](const char* what) { throw std::invalid_argument(std::string("legroute: ") + what); };
  if (o.cellbox_diagnostic_final && (!o.cellbox_full_flow || !o.final)) {
    fail("cellbox_diagnostic_final requires cellbox_full_flow and final");
  }
  if (o.cellbox_full_flow && (!o.critical_cellboxes || o.clock_spine || o.legal_boxes || o.paired_pin_reserve)) {
    fail("cellbox_full_flow requires critical admission, no legacy boxes or clock-spine reconstruction");
  }
  if (o.paired_pin_reserve && (!o.legal_boxes || o.legal_window || o.legal_shuffle)) {
    fail("paired_pin_reserve currently requires fixed legal_boxes (legal_window=0 and legal_shuffle=0)");
  }
  if (o.paired_pin_cap && (!o.paired_pin_reserve || o.paired_pin_cap % 2)) {
    fail("paired_pin_cap must be even and requires paired_pin_reserve");
  }
  if (o.paired_pin_reserve && o.clock_spine) {
    fail("paired_pin_reserve currently requires ordinary-net scope with clock_spine=false");
  }
  if (o.paired_pin_reserve && o.cap_scale < lr::admission_share(o)) {
    fail("paired_pin_reserve requires cap_scale at least the Plan admission share");
  }
  if (!std::isfinite(o.reserved_tracks_per_um) || (o.reserved_tracks_per_um < 0 && o.reserved_tracks_per_um != -1)) {
    fail("reserved_tracks_per_um must be -1 (per-tile count) or a nonnegative finite density");
  }
  if (!std::isfinite(o.global_astar_weight) || o.global_astar_weight < 1) {
    fail("global_astar_weight must be finite and at least 1");
  }
  if (!std::isfinite(o.density_ceiling) || !(o.density_ceiling > 0) || o.density_ceiling > 1) {
    fail("density_ceiling must be in (0, 1]");
  }
  if (!std::isfinite(o.fit_density) || o.fit_density < 0 || o.fit_density > 1) {
    fail("fit_density must be in [0, 1] (0 disables the window fit check)");
  }
  if (o.fit_density > 0 && o.fit_window == 0) {
    fail("fit_window must be at least 1 when fit_density is enabled");
  }

  if ((o.partition_cells || o.global_all) && (!o.global_route || o.region_pins)) {
    fail("partition_cells and global_all require global_route=true and region_pins=false");
  }
  if (o.global_all && !o.partition_cells && !o.legal_boxes) {
    fail("movable global_all requires partition_cells>0 to establish coherent box regions");
  }
  if (o.box_route_seed && !legroute_box_route_seed_compatible(o)) {
    fail("box_route_seed requires movable route-driven boxes without freeze, pre-placement policies or sat_alt");
  }
  if (o.global_keep
      && (!o.legal_boxes || !(o.partition_cells || o.global_all) || o.legal_window || o.legal_shuffle || o.hop_choice
          || o.crit_boxes > 0 || o.tree_boxes > 0 || o.tree_load > 0 || o.io_preroute || !o.sat_alt.empty())) {
    fail("global_keep requires an all-signal prepass, fixed legal boxes, fixed hops and no pre-placement policies");
  }
  if (!(o.partition_critical >= 0 && o.partition_critical <= 1)) {
    fail("partition_critical must be in [0, 1]");
  }
  if (o.partition_timeout_rounds
      && (!o.partition_cells || !o.legal_boxes || o.legal_window || o.legal_shuffle || o.hop_choice || !o.sat_alt.empty()
          || o.crit_boxes > 0 || o.tree_boxes > 0 || o.tree_load > 0 || o.io_preroute)) {
    fail("partition_timeout_rounds requires a route-driven partition, fixed legal boxes/hops and one policy without pre-placement");
  }
  if (!std::isfinite(o.partition_critical_cost) || !(o.partition_critical_cost == -1 || o.partition_critical_cost >= 0)) {
    fail("partition_critical_cost must be -1 (forbidden) or nonnegative (penalized)");
  }
  for (double v : {o.merge_cost, o.crit_window_ps, o.cap_scale, o.tree_clauses, o.tree_congestion}) {
    if (!std::isfinite(v)) {
      fail("numeric options must be finite");
    }
  }
  if (!(o.cap_scale > 0) || !(o.tree_clauses > 0)) {
    fail("cap_scale and tree_clauses must be positive");
  }
  if (!(o.box_fill > 0 && o.box_fill <= 1)) {
    fail("box_fill must be in (0, 1]");
  }
  if (o.box_net_max < 2) {
    fail("box_net_max must be at least 2");
  }
  if (!(o.merge_cost > 0)) {
    fail("merge_cost must be positive");
  }
  if (!(o.crit_fraction > 0 && o.crit_fraction <= 1)) {
    fail("crit_fraction must be in (0, 1]");
  }
  if (!(o.crit_window_ps > 0)) {
    fail("crit_window_ps must be positive");
  }
  if (!(o.crit_boxes >= 0 && o.crit_boxes <= 1)) {
    fail("crit_boxes must be in [0, 1]");
  }
  if (!(o.tree_boxes >= 0 && o.tree_boxes <= 1)) {
    fail("tree_boxes must be in [0, 1]");
  }
  if (!(o.tree_load >= 0 && o.tree_load <= 1)) {
    fail("tree_load must be in [0, 1]");
  }
  if (!o.rib_tiles) {
    fail("rib_tiles must be positive");
  }
  if (!(o.tree_congestion >= 0)) {
    fail("tree_congestion must not be negative");
  }
  if (!(o.hop_spread >= 0)) {
    fail("hop_spread must not be negative");
  }
  if (!(o.hop_share > 0 && o.hop_share <= 1) || !(o.reserve_share > 0 && o.reserve_share <= 1)) {
    fail("hop_share and reserve_share must be in (0, 1]");
  }
  if (!(o.global_fraction >= 0 && o.global_fraction <= 1)) {
    fail("global_fraction must be in [0, 1]");
  }
  if (!(o.global_share > 0 && o.global_share <= 1)) {
    fail("global_share must be in (0, 1]");
  }
  if (o.capacity_lazy > 2) {
    fail("capacity_lazy must be 0, 1 or 2");
  }
  if (!(o.sat_min_fill >= 0 && o.sat_min_fill < 1)) {
    fail("sat_min_fill must be in [0, 1)");
  }
  if (!(o.tier_fraction >= 0 && o.tier_fraction <= 1) || !o.tier_batch) {
    fail("tier_fraction must be in [0, 1] and tier_batch positive");
  }
  for (const auto& name : lr::tier_names(o.tiers)) {
    if (name != "adjacent" && name != "near" && name != "pins" && name != "home") {
      fail("tiers must name adjacent, near, pins or home");
    }
  }
  if (!o.sat_alt.empty()) {
    validate_legroute_options(lr::sat_alt_options(o));
  }
}

namespace {
// Post-F1 export of selected boxes (Legroute_options::final_box_dump), for offline in-box experiments. Coordinates
// are DBU; L5 metal is exported as assigned tracks and spans, with node/joint/jog data, not as Final's materialized
// geometry, and every track carries its provenance (checked / prescribed / fallback).
void dump_final_boxes(const lr::Design& d, const lr::Grid& g, const lr::Boxes& boxes, const lr::Routing& R,
                      const Legroute_regions& regions, const std::string& dir) {
  std::filesystem::create_directories(dir);
  auto work = d.fp;  // the checkpoint: post-F1 poses, the planned PG merged as final routes against it
  work.special_nets.insert(work.special_nets.end(), regions.power_nets.begin(), regions.power_nets.end());
  work.vias.insert(work.vias.end(), regions.power_vias.begin(), regions.power_vias.end());
  physical::write_def(work, dir + "/post_f1.def");
  // Final's own planned geometry (exact wire rectangles and layer joins), not a tile-span approximation
  const auto                         planned = lr::final_planned_geometry(d, g, boxes, R);
  const physical::Signal_metal_rules metal_rules(d.tech, d.fp);
  // one net's planned shapes within [x0,x1]x[y0,y1], each with the state of the L5 track its path was drawn on
  const auto                         planned_near = [&](int n, double x0, double y0, double x1, double y1) {
    std::vector<std::pair<physical::Shape, uint8_t>> out;
    const auto&                                      paths = planned.paths[size_t(n)];
    for (size_t k = 0; k < paths.size(); ++k) {
      physical::Net one{d.fp.nets[d.nets[size_t(n)].frame].name, {}, {physical::Wire{"ROUTED", {paths[k]}}}};
      for (const auto& sh : physical::route_geometry(d.tech, d.fp, one)) {
        if (sh.rect.xh >= x0 && sh.rect.xl <= x1 && sh.rect.yh >= y0 && sh.rect.yl <= y1) {
          out.emplace_back(sh, k < planned.path_state[size_t(n)].size() ? planned.path_state[size_t(n)][k] : uint8_t(0));
        }
      }
    }
    return out;
  };
  const auto q     = [](std::string_view v) { return '"' + json_util::escape(std::string(v)) + '"'; };
  const auto state = [](uint8_t s) {
    static constexpr std::array<const char*, 4> names{"none", "checked", "prescribed", "fallback"};
    return std::string(s < names.size() ? names[s] : "unknown");
  };
  const auto number = [](std::string_view token, int& value) {
    const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    return ec == std::errc{} && end == token.data() + token.size();
  };
  // "all": every placed box's tile
  std::string all;
  if (d.opt.final_box_dump == "all") {
    for (const auto& b : boxes.boxes) {
      if (b.tile >= 0) {
        all += std::format("{}{}:{}", all.empty() ? "" : ";", g.col(b.tile), g.row(b.tile));
      }
    }
  }
  std::string_view spec = d.opt.final_box_dump == "all" ? std::string_view(all) : std::string_view(d.opt.final_box_dump);
  while (!spec.empty()) {
    const auto semi  = spec.find(';');
    const auto item  = spec.substr(0, semi);
    spec             = semi == std::string_view::npos ? std::string_view{} : spec.substr(semi + 1);
    const auto colon = item.find(':');
    int        col = -1, row = -1;
    if (colon == std::string_view::npos || !number(item.substr(0, colon), col) || !number(item.substr(colon + 1), row) || col < 0
        || row < 0 || col >= g.cols || row >= g.rows) {
      throw std::invalid_argument(std::format("legroute final_box_dump entry '{}' must be col:row inside the grid", item));
    }
    const int tile = g.tile(col, row);
    int       box  = -1;
    for (size_t b = 0; b < boxes.boxes.size(); ++b) {
      if (boxes.boxes[b].tile == tile) {
        box = int(b);
      }
    }
    std::ofstream out(std::format("{}/box_c{}_r{}.json", dir, col, row));
    out << std::format(R"({{"col":{},"row":{},"tile":{},"box":{},"rect_dbu":[{},{},{},{}],"blocked":{},"box_short":{},)",
                       col,
                       row,
                       tile,
                       box,
                       g.left(col),
                       g.bottom(row),
                       g.left(col) + g.tile_w,
                       g.bottom(row) + g.tile_h,
                       int(g.blocked[size_t(tile)]),
                       int(size_t(tile) < g.box_short.size() && g.box_short[size_t(tile)]));
    out << R"("free_segments":[)";
    for (size_t k = 0; k < g.segs(tile).size(); ++k) {
      const auto& sg = g.segs(tile)[k];
      out << std::format(R"({}{{"row":{},"lo_site":{},"hi_site":{}}})", k ? "," : "", int(sg.row), sg.lo, sg.hi);
    }
    out << std::format(R"(],"site_dbu":{},"row_dbu":{},"cells":[)", g.site, g.row_h);
    std::set<int> incident;
    if (box >= 0) {
      size_t k = 0;
      for (int m : boxes.boxes[size_t(box)].cells) {
        const auto& comp = d.fp.components[size_t(d.movable[size_t(m)])];
        out << std::format(R"({}{{"name":{},"macro":{},"x":{},"y":{},"orient":{},"width_dbu":{}}})",
                           k++ ? "," : "",
                           q(comp.name),
                           q(comp.macro),
                           comp.place.x,
                           comp.place.y,
                           q(physical::orientation_name(comp.place.orientation)),
                           d.width[size_t(m)]);
        for (int n : d.nets_of[size_t(m)]) {
          incident.insert(n);
        }
      }
    }
    // nets: the box's incident nets with their pins; metal: every net's L5 metal over the tile and a one-tile halo
    // (foreign transit included), each with its provenance
    int low_h = 0;  // legacy jogs/stubs without an explicit layer use the lowest horizontal routing layer
    while (low_h + 1 < int(g.layers.size()) && !g.layers[size_t(low_h)].horizontal) {
      ++low_h;
    }
    out << R"(],"nets":[)";
    size_t k = 0;
    for (int n : incident) {
      const auto& N = d.nets[size_t(n)];
      out << std::format(R"({}{{"net":{},"name":{},"clock":{},"power":{},"routed":{},"pins":[)",
                         k++ ? "," : "",
                         n,
                         q(d.fp.nets[N.frame].name),
                         int(N.clock),
                         int(N.power),
                         int(N.routed));
      for (size_t p = 0; p < N.pins.size(); ++p) {
        const auto& P      = N.pins[p];
        const bool  in_box = box >= 0 && P.comp >= 0 && d.cell[P.comp] >= 0 && boxes.box_of[size_t(d.cell[P.comp])] == box;
        const auto  owner  = P.comp >= 0   ? d.fp.components[size_t(P.comp)].name
                             : P.term >= 0 ? d.fp.terminals[size_t(P.term)].name
                                           : std::string{};
        out << std::format(R"({}{{"owner":{},"pin":{},"terminal":{},"in_box":{},"driver":{}}})",
                           p ? "," : "",
                           q(owner),
                           q(P.name),
                           int(P.term >= 0),
                           int(in_box),
                           int(int(p) == N.driver));
      }
      out << R"(],"nodes":[)";  // the net's tree nodes in the tile: box node, riser and stub with provenance
      size_t m = 0;
      for (size_t v = 0; v < R.trees[size_t(n)].nodes.size(); ++v) {
        const auto& node = R.trees[size_t(n)].nodes[v];
        if (node.tile != tile) {
          continue;
        }
        const int stub_layer = node.stub_layer >= 0 ? node.stub_layer : node.stub_track >= 0 ? low_h : -1;
        out << std::format(R"({}{{"node":{},"box":{},"term":{},"vpin":{},"riser_layer":{},"riser_coord":{},"riser_state":{},)"
                           R"("stub_layer":{},"stub_coord":{},"stub_state":{}}})",
                           m++ ? "," : "",
                           v,
                           node.box,
                           node.term,
                           node.vpin,
                           q(node.riser_layer >= 0 ? g.layers[size_t(node.riser_layer)].name : ""),
                           node.riser_layer >= 0 && node.riser_track >= 0 ? g.track_coord(node.riser_layer, node.riser_track) : -1,
                           q(state(node.riser_state)),
                           q(stub_layer >= 0 ? g.layers[size_t(stub_layer)].name : ""),
                           node.stub_track >= 0 ? g.track_coord(stub_layer, node.stub_track) : -1,
                           q(state(node.stub_state)));
      }
      // the net's planned metal near the tile (tile plus one-tile halo): exact rectangles and joins
      const double hx0 = double(g.left(col) - g.tile_w), hy0 = double(g.bottom(row) - g.tile_h);
      const double hx1 = double(g.left(col) + 2 * g.tile_w), hy1 = double(g.bottom(row) + 2 * g.tile_h);
      out << std::format(R"(],"io_stub_failure":{},"planned_shapes":[)", q(planned.io_stub_failure[size_t(n)]));
      size_t ps = 0;
      for (const auto& [sh, st] : planned_near(n, hx0, hy0, hx1, hy1)) {
        out << std::format(R"({}{{"layer":{},"rect":[{},{},{},{}],"state":{}}})",
                           ps++ ? "," : "",
                           q(sh.layer),
                           sh.rect.xl,
                           sh.rect.yl,
                           sh.rect.xh,
                           sh.rect.yh,
                           q(state(st)));
      }
      out << R"(],"planned_joins":[)";
      size_t pj = 0;
      for (const auto& [from, to, x, y] : planned.joins[size_t(n)]) {
        if (double(x) < hx0 || double(x) > hx1 || double(y) < hy0 || double(y) > hy1) {
          continue;
        }
        out << std::format(R"({}{{"from":{},"to":{},"x":{},"y":{}}})", pj++ ? "," : "", q(from), q(to), x, y);
      }
      out << "]";
      out << "}";
    }
    out << R"(],"metal":[)";
    k = 0;
    for (size_t si = 0; si < R.segments.size(); ++si) {
      const auto& sg = R.segments[si];
      if (sg.layer < 0) {
        continue;
      }
      const auto& L     = g.layers[size_t(sg.layer)];
      const int   along = L.horizontal ? col : row, band = L.horizontal ? row : col;
      if (std::abs(sg.band - band) > 1 || along < sg.lo - 1 || along > sg.hi + 1) {
        continue;
      }
      const int jog_lo_layer = sg.jog_lo_layer >= 0 ? sg.jog_lo_layer : sg.jog_lo >= 0 ? low_h : -1;
      const int jog_hi_layer = sg.jog_hi_layer >= 0 ? sg.jog_hi_layer : sg.jog_hi >= 0 ? low_h : -1;
      out << std::format(
          R"({}{{"segment":{},"net":{},"incident":{},"layer":{},"horizontal":{},"band":{},"tiles":[{},{}],)"
          R"("track_coord":{},"state":{},"clock":{},"node_lo":{},"node_hi":{},)"
          R"("jog_lo_layer":{},"jog_lo_coord":{},"jog_lo_state":{},"jog_hi_layer":{},"jog_hi_coord":{},"jog_hi_state":{}}})",
          k++ ? "," : "",
          si,
          sg.net,
          int(incident.contains(sg.net)),
          q(L.name),
          int(L.horizontal),
          sg.band,
          sg.lo,
          sg.hi,
          sg.track >= 0 ? g.track_coord(sg.layer, sg.track) : -1,
          q(state(sg.track_state)),
          int(sg.clock),
          sg.node_lo,
          sg.node_hi,
          q(jog_lo_layer >= 0 ? g.layers[size_t(jog_lo_layer)].name : ""),
          sg.jog_lo >= 0 ? g.track_coord(jog_lo_layer, sg.jog_lo) : -1,
          q(state(sg.jog_lo_state)),
          q(jog_hi_layer >= 0 ? g.layers[size_t(jog_hi_layer)].name : ""),
          sg.jog_hi >= 0 ? g.track_coord(jog_hi_layer, sg.jog_hi) : -1,
          q(state(sg.jog_hi_state)));
    }
    // every other net's planned metal over the tile and its one-tile halo, grown by the largest routing pitch so that
    // spacing context at the halo edge is kept (exact rectangles, with track states)
    out << R"(],"foreign_planned":[)";
    {
      // the rule-derived query reach (wide-metal PRL/EOL included), never less than a pitch
      int64_t margin = 0;
      for (const auto& L : g.layers) {
        margin = std::max({margin, L.pitch, metal_rules.clearance(L.name).value_or(L.pitch)});
      }
      const double  hx0 = double(g.left(col) - g.tile_w - margin), hy0 = double(g.bottom(row) - g.tile_h - margin);
      const double  hx1 = double(g.left(col) + 2 * g.tile_w + margin), hy1 = double(g.bottom(row) + 2 * g.tile_h + margin);
      std::set<int> near;
      for (const auto& sg : R.segments) {
        if (sg.net >= 0 && sg.layer >= 0 && !incident.contains(sg.net)) {
          const auto& L     = g.layers[size_t(sg.layer)];
          const int   along = L.horizontal ? col : row, band = L.horizontal ? row : col;
          // candidates two tiles out: a wide wire on the next band's track can still reach into the halo; the exact
          // shape test below decides
          if (std::abs(sg.band - band) <= 2 && along >= sg.lo - 2 && along <= sg.hi + 2) {
            near.insert(sg.net);
          }
        }
      }
      for (size_t n2 = 0; n2 < R.trees.size(); ++n2) {
        for (const auto& node : R.trees[n2].nodes) {
          if (node.tile >= 0 && std::abs(g.col(node.tile) - col) <= 2 && std::abs(g.row(node.tile) - row) <= 2
              && !incident.contains(int(n2))) {
            near.insert(int(n2));
          }
        }
      }
      size_t fk = 0;
      for (int n2 : near) {
        for (const auto& [sh, st] : planned_near(n2, hx0, hy0, hx1, hy1)) {
          out << std::format(R"({}{{"net":{},"name":{},"layer":{},"rect":[{},{},{},{}],"state":{}}})",
                             fk++ ? "," : "",
                             n2,
                             q(d.fp.nets[d.nets[size_t(n2)].frame].name),
                             q(sh.layer),
                             sh.rect.xl,
                             sh.rect.yl,
                             sh.rect.xh,
                             sh.rect.yh,
                             q(state(st)));
        }
      }
      // their deferred layer joins near the tile (requests; Final's router realizes the vias)
      out << R"(],"foreign_joins":[)";
      size_t fj = 0;
      for (int n2 : near) {
        for (const auto& [from, to, x, y] : planned.joins[size_t(n2)]) {
          if (double(x) >= hx0 && double(x) <= hx1 && double(y) >= hy0 && double(y) <= hy1) {
            out << std::format(R"({}{{"net":{},"name":{},"from":{},"to":{},"x":{},"y":{}}})",
                               fj++ ? "," : "",
                               n2,
                               q(d.fp.nets[d.nets[size_t(n2)].frame].name),
                               q(from),
                               q(to),
                               x,
                               y);
          }
        }
      }
    }
    // the clock attachments designated for this box: its spine taps (boxed clock sinks are not tree nodes)
    out << R"(],"clock_taps":[)";
    {
      size_t ck = 0;
      for (const auto& sp : R.spines) {
        for (const auto& [tb, ts] : sp.taps) {
          if (tb != box || ts < 0) {
            continue;
          }
          const auto& tap = R.segments[size_t(ts)];
          out << std::format(R"({}{{"net":{},"name":{},"segment":{},"layer":{},"track_coord":{},"state":{},"tiles":[{},{}]}})",
                             ck++ ? "," : "",
                             sp.net,
                             q(d.fp.nets[d.nets[size_t(sp.net)].frame].name),
                             ts,
                             q(g.layers[size_t(tap.layer)].name),
                             tap.track >= 0 ? g.track_coord(tap.layer, tap.track) : -1,
                             q(state(tap.track_state)),
                             tap.lo,
                             tap.hi);
        }
      }
    }
    out << "]}\n";
  }
  std::fprintf(stderr, "legroute final: box dump written to %s\n", dir.c_str());
}
}  // namespace

Legroute_report legroute(const physical::Technology& tech, physical::Floorplan& fp, const timing::Feedback& timing,
                         const Legroute_options& options, const Legroute_regions& regions, const physical::Tech_profile* profile) {
  if (options.critical_cellboxes && (!options.cellbox_full_flow || !options.cellbox_handoff)) {
    throw std::invalid_argument("critical_cellboxes Local/Final requires an explicit selected full-flow handoff");
  }
  validate_legroute_options(options);
  const auto      start = lr::Clock::now();
  Legroute_report rep;
  rep.final_diagnostic_only = options.cellbox_diagnostic_final;
  rep.global_astar_weight = options.global_astar_weight;
  auto opt                = options;
  if (!opt.threads) {
    opt.threads = std::max(1u, std::thread::hardware_concurrency());
  }
  auto                                     components = fp.components;
  auto                                     rules      = fp.nondefault_rules;
  std::vector<std::vector<physical::Wire>> wires;
  for (const auto& n : fp.nets) {
    wires.push_back(n.wires);
  }
  const auto restore_frame = [&] {
    fp.components       = std::move(components);
    fp.nondefault_rules = std::move(rules);
    for (size_t n = 0; n < fp.nets.size() && n < wires.size(); ++n) {
      fp.nets[n].wires = std::move(wires[n]);
    }
  };
  auto       step_t = lr::Clock::now();
  const auto step   = [&](const char* name) {
    rep.steps.push_back({name, lr::seconds_since(step_t)});
    std::fprintf(stderr, "legroute step %s: %.1f s\n", name, rep.steps.back().seconds);
    step_t = lr::Clock::now();
  };
  try {
    auto d             = lr::build_design(tech, fp, timing, opt, profile);
    auto g             = lr::build_grid(d, regions);
    rep.tile_w         = g.tile_w;
    rep.tile_h         = g.tile_h;
    rep.dbu_per_micron = g.dbu;
    rep.cols           = g.cols;
    rep.rows           = g.rows;
    rep.regions        = g.regions;
    for (auto b : g.blocked) {
      rep.tiles_free += !b;
    }
    step("L1 grid");
    lr::Boxes boxes;
    if (opt.cellbox_full_flow) {
      lr::install_critical_handoff(d, g, boxes);
    } else {
      boxes = lr::build_boxes(d, g);
    }
    if (opt.paired_pin_reserve) {
      lr::resolve_paired_pin_reservations(d, g, boxes);
      if (!options.output_dir.empty()) {
        lr::write_paired_pin_reservations(d, g, options.output_dir + "/local_paired_pin_reservations.json");
      }
      auto supply = lr::paired_pin_supply_gate(d, g);
      if (supply.requires_replan()) {
        throw Legroute_replan(std::move(supply));
      }
    }
    if (opt.cellbox_full_flow && !options.output_dir.empty()) {
      std::ofstream scope(options.output_dir + "/critical_source_scope.json");
      size_t        physical_pins = 0, singleton_pins = 0, controls = 0;
      scope << "{\"definition\":\"complete source; intrinsic singleton pins require no pairwise wire\",\"nets\":[";
      for (size_t n = 0; n < d.nets.size(); ++n) {
        const auto& net    = d.nets[n];
        const auto& source = fp.nets[net.frame];
        size_t      pins   = 0;
        for (const auto& p : net.pins) {
          pins += p.comp >= 0;
        }
        if (!net.power && !net.control_scope) {
          physical_pins  += pins;
          singleton_pins += net.pins.size() == 1 ? pins : 0;
        }
        controls += net.control_scope;
        scope << (n ? "," : "") << "{\"source_net\":" << net.frame << ",\"name\":\"" << json_util::escape(source.name)
              << "\",\"control_scope\":" << (net.control_scope ? "true" : "false")
              << ",\"power\":" << (net.power ? "true" : "false")
              << ",\"intrinsic_singleton\":" << (net.pins.size() == 1 ? "true" : "false") << ",\"endpoints\":[";
        for (size_t k = 0; k < source.connections.size(); ++k) {
          const auto& p = source.connections[k];
          scope << (k ? "," : "") << "[\"" << json_util::escape(p.instance) << "\",\"" << json_util::escape(p.pin) << "\"]";
        }
        scope << "]}";
      }
      scope << "],\"ordinary_physical_pins\":" << physical_pins << ",\"intrinsic_singleton_cell_pins\":" << singleton_pins
            << ",\"excluded_control_nets\":" << controls << "}\n";
      if (!scope) {
        throw std::runtime_error("cannot write complete critical source scope");
      }
    }
    rep.cells    = d.movable.size();
    rep.boxes    = boxes.boxes.size();
    double sites = 0;
    for (const auto& b : boxes.boxes) {
      rep.max_cells_per_box  = std::max(rep.max_cells_per_box, b.cells.size());
      sites                 += b.sites;
      rep.legal_boxes       += b.placed;
    }
    rep.mean_cells_per_box = rep.boxes ? double(rep.cells) / rep.boxes : 0;
    double allocated_sites = 0;
    for (const auto& b : boxes.boxes) {
      allocated_sites += double(g.rows_per_tile) * g.sites * b.span_columns;
    }
    rep.fill = allocated_sites ? sites / allocated_sites : 0;
    {
      std::set<int> inside, outside;
      for (const auto& b : boxes.boxes) {
        std::map<int, int> count;
        for (int c : b.cells) {
          for (int n : d.nets_of[c]) {
            if (!d.nets[n].clock && !d.nets[n].power) {
              for (const auto& p : d.nets[n].pins) {
                count[n] += p.comp == d.movable[c];
              }
            }
          }
        }
        for (const auto& [n, k] : count) {
          (k == int(d.nets[n].pins.size()) ? inside : outside).insert(n);
        }
      }
      rep.internal_nets = inside.size();
      rep.external_nets = outside.size();
    }
    step("L2 cell-boxes");
    lr::Routing R;
    if (opt.cellbox_full_flow) {
      lr::critical_local_routes(d, g, boxes, R, rep);
    } else {
      lr::sat_place(d, g, boxes, regions, R, rep);
    }
    if (rep.sat_unsat_regions || rep.sat_unknown_regions) {
      // Required regional placement did not complete. Track assignment and
      // detailed routing cannot turn this partial model into a valid result.
      step("L3 SAT placement");
      rep.failure_sample.push_back("required SAT failed; clock, track and detailed routing skipped");
      lr::Final_net_scope scope;
      if (opt.final) {
        scope                  = lr::final_net_scope(d);
        rep.final_skipped_nets = scope.skipped;
      }
      for (size_t n = 0; n < d.nets.size(); ++n) {
        if (opt.final ? scope.required[n] : d.nets[n].requires_final_route()) {
          ++rep.routed_nets;
          ++rep.unchecked_nets;
          rep.final_required_pins  += d.nets[n].pins.size();
          rep.clock_required_nets  += d.nets[n].clock;
          rep.clock_unchecked_nets += d.nets[n].clock;
        }
      }
      rep.seconds = lr::seconds_since(start);
      if (!options.output_dir.empty()) {
        write_legroute_report(rep, options.output_dir + "/legroute.json");
      }
      restore_frame();
      return rep;
    }
    if (!opt.cellbox_full_flow) {
      lr::box_demand(d, g, boxes, R);
    }
    if (opt.paired_pin_reserve) {
      lr::validate_paired_pin_reservations(d, g, boxes);
    }
    for (const auto& b : boxes.boxes) {
      if (b.critical_fixed || (b.placed && b.tile == g.tile_of(b.cx, b.cy))) {  // legal_boxes: on its legal tile, nothing moved
        continue;
      }
      const double dd      = (std::abs(double(g.cx(g.col(b.tile))) - b.cx) + std::abs(double(g.cy(g.row(b.tile))) - b.cy)) / g.dbu;
      rep.displacement_um += dd;
      rep.max_displacement_um = std::max(rep.max_displacement_um, dd);
    }
    step(opt.cellbox_full_flow ? "L3 selected Plan forest (no SAT solve)" : "L3 SAT placement");
    if (!opt.cellbox_full_flow) {
      lr::pack_cells(d, g, boxes);
    }
    if (opt.paired_pin_reserve) {
      lr::validate_paired_pin_reservations(d, g, boxes);
    }
    if (opt.clock_spine) {
      lr::clock_spines(d, g, boxes, R);
    }
    for (auto& c : R.conns) {  // the clock trees' joins to their spines: an L (within the spine's reservation)
      if (c.poly.empty()) {
        const int ta = R.trees[c.net].nodes[c.a].tile, tb = R.trees[c.net].nodes[c.b].tile;
        c.poly.emplace_back(g.col(ta), g.row(ta));
        if (g.col(ta) != g.col(tb) && g.row(ta) != g.row(tb)) {
          c.poly.emplace_back(g.col(tb), g.row(ta));
        }
        c.poly.emplace_back(g.col(tb), g.row(tb));
      }
    }
    rep.clock_domains = R.clock_domains;
    rep.clock_ribs    = R.clock_ribs;
    if (opt.clock_spine && R.clock_reserved_v.size() == g.size()) {
      // the tree is built from the final regions and box tiles; the reservation came from the poses before SAT
      const auto built = lr::clock_tree_charge(d, g, &boxes, opt.rib_tiles);
      for (size_t t = 0; t < g.size(); ++t) {
        const int over          = std::max(0, built.v[t] - R.clock_reserved_v[t]) + std::max(0, built.h[t] - R.clock_reserved_h[t]);
        rep.clock_excess_tiles += over > 0;
        rep.clock_excess_tracks += size_t(over);
      }
      std::fprintf(stderr,
                   "legroute L4: clock tree within its reservation on all but %zu tiles (%zu tracks over)\n",
                   rep.clock_excess_tiles,
                   rep.clock_excess_tracks);
    }
    step("L4 clock spines");
    lr::assign_layers(d, g, R);
    lr::assign_tracks(d, g, R, opt.threads);
    std::vector<std::string> local_structural_rejections;
    if (opt.cellbox_full_flow) {
      lr::validate_critical_boxes(d, g, boxes);
      const auto               carriers = lr::final_planned_geometry(d, g, boxes, R);
      std::vector<std::string> private_rejections;
      rep.final_domain_rejections
          += lr::critical_backbone_conflicts(d, g, carriers, opt.cellbox_diagnostic_final ? &private_rejections : nullptr);
      if (opt.cellbox_diagnostic_final) {
        local_structural_rejections = lr::local_backbone_rejections(d, g, boxes, R, carriers);
        const auto scope            = lr::final_net_scope(d);
        R.final_local_rejected.resize(d.nets.size());
        for (size_t n = 0; n < d.nets.size(); ++n) {
          if (!scope.required[n]) {
            continue;
          }
          R.final_local_rejected[n] = "Local native metal audit unexamined";
          if (!private_rejections[n].empty()) {
            if (!local_structural_rejections[n].empty()) {
              local_structural_rejections[n] += "; ";
            }
            local_structural_rejections[n] += private_rejections[n];
          }
        }
      }
      if (!options.output_dir.empty()) {
        auto       exported = fp;
        const auto scope    = lr::final_net_scope(d);
        for (size_t n = 0; n < d.nets.size(); ++n) {
          if (scope.required[n]) {
            auto& net = exported.nets[d.nets[n].frame];
            net.wires.clear();
            if (!carriers.paths[n].empty()) {
              net.wires.push_back({"ROUTED", carriers.paths[n]});
            }
          }
        }
        physical::write_def(exported, options.output_dir + "/critical_local_carriers.def");
        std::ofstream meta(options.output_dir + "/critical_local_carriers.json");
        meta << std::format(
            R"({{"scope":"selected_plan_backbone_before_pin_and_via_completion","physical_certificate":false,"domain_rejections":{},"source":"critical_local_carriers.def"}})",
            rep.final_domain_rejections)
             << '\n';
        if (!meta) {
          throw std::runtime_error("cannot export Local carrier provenance");
        }
      }
      if (rep.final_domain_rejections) {
        rep.failure_sample.push_back(opt.cellbox_diagnostic_final
                                         ? "Local private-coordinate violations excluded from diagnostic Final scene"
                                         : "Local backbone overlaps private cellbox reserved coordinates; Final skipped");
      }
    }
    if (!opt.paired_pin_reserve && !opt.cellbox_full_flow) {
      // The cellbox allowance after L5: per budgeted routing layer and free tile, the usable physical tracks (after
      // PG/obstacle exclusions) minus the distinct tracks shared metal took there (transit segments, clock, and the
      // risers of non-box nodes; a box's own riser on its tile spends its own allowance, not a foreign one).
      for (size_t l = 0; l < g.layers.size(); ++l) {
        const auto& catalog = g.resources.layers[l];
        if (std::ranges::none_of(catalog.tiles, [](const auto& tile) { return tile.cellbox_budget > 0; })) {
          continue;
        }
        std::vector<std::set<int64_t>> used(g.size());
        for (const auto& s : R.segments) {
          if (s.layer != int(l) || s.track < 0
              || (s.track_state != uint8_t(lr::Track_state::checked) && s.track_state != uint8_t(lr::Track_state::prescribed))) {
            continue;
          }
          for (int k = s.lo; k <= s.hi; ++k) {
            used[size_t(g.layers[l].horizontal ? g.tile(k, s.band) : g.tile(s.band, k))].insert(s.track);
          }
        }
        for (const auto& tree : R.trees) {
          for (const auto& node : tree.nodes) {
            if (node.riser_layer == int(l) && node.riser_track >= 0 && node.box < 0 && node.tile >= 0
                && node.riser_state != uint8_t(lr::Track_state::fallback)) {
              used[size_t(node.tile)].insert(node.riser_track);
            }
          }
        }
        for (size_t t = 0; t < g.size(); ++t) {
          const auto& tile = catalog.tiles[t];
          if (!tile.cellbox_budget || g.blocked[t]) {
            continue;
          }
          const auto usable = tile.whole_band_exclusions ? size_t(0) : catalog.bands.at(tile.band).size() - tile.exclusions.size();
          const auto left   = usable > used[t].size() ? usable - used[t].size() : size_t(0);
          if (left < tile.cellbox_budget) {
            ++rep.cellbox_allowance_short_tiles;
            rep.cellbox_allowance_short_tracks += tile.cellbox_budget - left;
          }
        }
      }
      if (rep.cellbox_allowance_short_tiles) {
        std::fprintf(stderr,
                     "legroute L5: cellbox allowance short on %zu tile layers (%zu tracks): shared metal took tracks Final was "
                     "promised\n",
                     rep.cellbox_allowance_short_tiles,
                     rep.cellbox_allowance_short_tracks);
      }
    }
    if (opt.paired_pin_reserve) {
      rep.cellbox_allowance_short_tiles  = R.paired_allowance_short_tiles;
      rep.cellbox_allowance_short_tracks = R.paired_allowance_short_tracks;
      lr::validate_paired_pin_reservations(d, g, boxes);
    }
    rep.unassigned_segments                 = R.unassigned;
    rep.l5_repair_attempts                  = R.l5_repair_attempts;
    rep.l5_repair_successes                 = R.l5_repair_successes;
    rep.l5_repair_unknown                   = R.l5_repair_unknown;
    rep.l5_repair_unsat                     = R.l5_repair_unsat;
    rep.l5_repair_sat_calls                 = R.l5_repair_sat_calls;
    rep.l5_repair_sat                       = R.l5_repair_sat;
    rep.l5_repair_sat_unsat                 = R.l5_repair_sat_unsat;
    rep.l5_repair_sat_unknown               = R.l5_repair_sat_unknown;
    rep.l5_repair_sat_variables             = R.l5_repair_sat_variables;
    rep.l5_repair_sat_clauses               = R.l5_repair_sat_clauses;
    rep.l5_repair_sat_build_seconds         = R.l5_repair_sat_build_seconds;
    rep.l5_repair_sat_solve_seconds         = R.l5_repair_sat_solve_seconds;
    rep.l5_repair_candidate_seconds         = R.l5_repair_candidate_seconds;
    rep.l5_repair_native_validation_seconds = R.l5_repair_native_validation_seconds;
    rep.l5_repair_seconds                   = R.l5_repair_seconds;

    rep.track_failures        = R.track_failures;
    rep.track_pick_rejections = R.track_pick_rejections;
    rep.track_fallback_shadow = R.track_fallback_shadow;
    rep.track_failure_samples = R.track_failure_samples;
    std::fprintf(stderr,
                 "legroute L5: %zu segments, %zu on an overfull layer (%zu tile overloads), %zu unassigned\n",
                 R.segments.size(),
                 R.layer_overflow_segments,
                 R.layer_overflow_tiles,
                 R.unassigned);
    step("L5 layers, tracks");
    if (opt.cellbox_diagnostic_final || (opt.l5_fallback_shadow && !options.output_dir.empty())) {
      rep.local_metal_audit_attempted = true;
      // Clear older successful join witnesses even when this candidate never qualifies for the probe below.
      if (!options.output_dir.empty()) {
        std::filesystem::remove(options.output_dir + "/local_backbone_join_probe.def");
        std::filesystem::remove(options.output_dir + "/local_backbone_join_probe.json");
        lr::write_local_track_requests(d, g, R, options.output_dir);
      }
      // Pure Local backbone geometry: no cell-pin routing or via selection is performed here. Keep unresolved
      // joins explicit so an assigned track count cannot be mistaken for a physically connected handoff.
      const auto planned          = lr::final_planned_geometry(d, g, boxes, R);
      const auto scope            = lr::final_net_scope(d);
      // Audit one immutable snapshot containing every required backbone simultaneously. Original signal routes
      // are replaced, while native occupancy retains real pins, macro obstructions and decoded PG geometry.
      // This is a metal-only diagnostic: unresolved join vias and signal connectivity are deliberately absent.
      const auto audit_deadline   = lr::Clock::now() + std::chrono::seconds(55);
      const auto audit_checkpoint = [&] {
        if (lr::Clock::now() >= audit_deadline) {
          throw std::runtime_error("Local backbone metal audit exceeded 55s budget");
        }
      };
      auto work = fp;
      for (auto& net : work.nets) {
        if (net.use != "POWER" && net.use != "GROUND") {
          net.wires.clear();
        }
      }
      for (size_t n = 0; n < d.nets.size(); ++n) {
        const auto& net = d.nets[n];
        for (const auto& pin : net.pins) {
          if (pin.term >= 0) {
            work.terminals[size_t(pin.term)].net = work.nets.at(net.frame).name;
          }
        }
        if (scope.required[n]) {
          work.nets.at(net.frame).wires.push_back({"ROUTED", planned.paths[n]});
        }
      }
      work.special_nets.insert(work.special_nets.end(), regions.power_nets.begin(), regions.power_nets.end());
      work.vias.insert(work.vias.end(), regions.power_vias.begin(), regions.power_vias.end());
      std::optional<physical::Routing_occupancy> occupancy;
      std::string                                audit_error;
      std::vector<std::vector<physical::Shape>>  raw_backbone(d.nets.size()), normalized_backbone(d.nets.size());
      try {
        occupancy.emplace(
            physical::routing_occupancy(tech,
                                        work,
                                        work.dbu,
                                        audit_checkpoint,
                                        nullptr,
                                        opt.cellbox_full_flow ? physical::Signal_wires::exclude : physical::Signal_wires::include));
        if (opt.cellbox_full_flow) {
          for (size_t n = 0; n < d.nets.size(); ++n) {
            if (!scope.required[n]) {
              continue;
            }
            audit_checkpoint();
            const auto& net        = work.nets.at(d.nets[n].frame);
            raw_backbone[n]        = physical::route_geometry(tech, work, net);
            normalized_backbone[n] = physical::coalesce_route_metal(tech, raw_backbone[n]);
            for (const auto& shape : normalized_backbone[n]) {
              occupancy->insert({shape, net.name, true});
            }
          }
        }
        if (regions.power_nets.empty()) {
          for (const auto& obstacle : regions.obstacles) {
            audit_checkpoint();
            occupancy->insert({obstacle, {}, false});
          }
        }
        audit_checkpoint();
      } catch (const std::exception& error) {
        audit_error = error.what();
        occupancy.reset();  // No partially imported scene may certify even a single shape.
      }
      const physical::Signal_metal_rules metal_rules(tech, work);
      size_t                             total_metal_conflicts = 0, total_metal_unknown = 0;
      bool                               metal_audit_complete = audit_error.empty();
      std::ofstream                      geometry_out;
      if (!options.output_dir.empty()) {
        geometry_out.open(options.output_dir + "/local_backbone_geometry.json");
      }
      geometry_out << "{\"schema_version\":1,\"physical_certificate\":false,"
                      "\"scope\":\"required_backbone_without_selected_join_vias\","
                      "\"metal_audit_scope\":\"all_required_backbones_and_static_pins_obstructions_pg_excluding_own_net\","
                      "\"path_state_semantics\":\"raw_planned_paths_before_same_net_union\","
                   << "\"shape_semantics\":\"" << (opt.cellbox_full_flow ? "exact_collinear_same_net_unions" : "raw_route_shapes")
                   << "\",\"nets\":[";
      bool first_net = true;
      for (size_t n = 0; n < d.nets.size(); ++n) {
        if (!scope.required[n]) {
          continue;
        }
        const auto&                  net = work.nets.at(d.nets[n].frame);
        std::vector<physical::Shape> shapes;
        std::string                  geometry_error;
        try {
          shapes = opt.cellbox_full_flow ? normalized_backbone[n] : physical::route_geometry(tech, work, net);
        } catch (const std::exception& error) {
          geometry_error = error.what();
        }
        size_t      metal_conflicts = 0, metal_unknown = 0;
        std::string first_conflict, first_unknown;
        if (!geometry_error.empty() || !audit_error.empty()) {
          metal_unknown = shapes.size();
          first_unknown = geometry_error.empty() ? audit_error : geometry_error;
        } else {
          for (size_t k = 0; k < shapes.size(); ++k) {
            const auto& shape    = shapes[k];
            const auto  describe = [&](const std::string& reason) {
              return std::format("shape {} {} [{},{},{},{}]: {}",
                                 k,
                                 shape.layer,
                                 shape.rect.xl,
                                 shape.rect.yl,
                                 shape.rect.xh,
                                 shape.rect.yh,
                                 reason);
            };
            try {
              audit_checkpoint();
              std::string conflict;
              if (!work.die.contains(shape.rect)) {
                conflict = "outside die";
              } else if (const auto clearance = metal_rules.clearance(shape.layer); !clearance) {
                ++metal_unknown;
                if (first_unknown.empty()) {
                  const auto check = metal_rules.rectangle(shape);
                  first_unknown    = describe("unsupported or unavailable metal rules: " + check.reason);
                }
                continue;
              } else if (const auto check = metal_rules.rectangle(shape); !check.legal) {
                conflict = check.reason;
              } else {
                for (const auto id : occupancy->query(shape.layer, shape.rect, *clearance, net.name)) {
                  audit_checkpoint();
                  const auto other = occupancy->at(id);
                  if (const auto spacing_check = metal_rules.spacing(shape, other.shape); !spacing_check.legal) {
                    conflict = std::format("{} against {} '{}' [{},{},{},{}]",
                                           spacing_check.reason,
                                           other.net.empty() ? "blockage"
                                           : other.routed    ? "routed metal"
                                                             : "pin",
                                           other.net,
                                           other.shape.rect.xl,
                                           other.shape.rect.yl,
                                           other.shape.rect.xh,
                                           other.shape.rect.yh);
                    break;
                  }
                }
              }
              if (!conflict.empty()) {
                ++metal_conflicts;  // Each candidate shape counts once, regardless of how many neighbors conflict.
                if (first_conflict.empty()) {
                  first_conflict = describe(conflict);
                }
              }
            } catch (const std::exception& error) {
              audit_error    = error.what();
              metal_unknown += shapes.size() - k;
              if (first_unknown.empty()) {
                first_unknown = audit_error;
              }
              break;
            }
          }
        }
        const bool net_audit_complete  = geometry_error.empty() && first_unknown.empty();
        if (opt.cellbox_diagnostic_final) {
          auto& reason      = R.final_local_rejected[n];
          reason            = local_structural_rejections[n];
          const auto append = [&](const std::string& why) {
            if (!why.empty()) {
              if (!reason.empty()) {
                reason += "; ";
              }
              reason += why;
            }
          };
          append(geometry_error);
          append(first_conflict);
          append(first_unknown);
          if ((!net_audit_complete || metal_conflicts || metal_unknown || !occupancy || !audit_error.empty()) && reason.empty()) {
            reason = "Local native metal audit incomplete or rejected";
          }
        }
        metal_audit_complete          &= net_audit_complete;
        total_metal_conflicts         += metal_conflicts;
        total_metal_unknown           += metal_unknown;
        geometry_out << (first_net ? "" : ",")
                     << std::format(R"({{"net":"{}","net_index":{},"metal_conflict_shapes":{},)"
                                    R"("first_metal_conflict":"{}","metal_unknown_shapes":{},"first_metal_unknown":"{}",)"
                                    R"("metal_audit_complete":{},"geometry_error":"{}","shapes":[)",
                                    json_util::escape(net.name),
                                    d.nets[n].frame,
                                    metal_conflicts,
                                    json_util::escape(first_conflict),
                                    metal_unknown,
                                    json_util::escape(first_unknown),
                                    net_audit_complete,
                                    json_util::escape(geometry_error));
        first_net = false;
        for (size_t k = 0; k < shapes.size(); ++k) {
          const auto& shape = shapes[k];
          geometry_out << (k ? "," : "")
                       << std::format(R"(["{}",{},{},{},{}])",
                                      json_util::escape(shape.layer),
                                      shape.rect.xl,
                                      shape.rect.yl,
                                      shape.rect.xh,
                                      shape.rect.yh);
        }
        if (opt.cellbox_full_flow) {
          geometry_out << "],\"raw_shapes\":[";
          for (size_t k = 0; k < raw_backbone[n].size(); ++k) {
            const auto& shape = raw_backbone[n][k];
            geometry_out << (k ? "," : "")
                         << std::format(R"(["{}",{},{},{},{}])",
                                        json_util::escape(shape.layer),
                                        shape.rect.xl,
                                        shape.rect.yl,
                                        shape.rect.xh,
                                        shape.rect.yh);
          }
        }
        geometry_out << "],\"path_states\":[";
        for (size_t k = 0; k < planned.path_state[n].size(); ++k) {
          geometry_out << (k ? "," : "") << unsigned(planned.path_state[n][k]);
        }
        geometry_out << "],\"selected_adjacent_join_vias\":[";
        for (size_t k = 0; k < planned.adjacent_join_vias[n].size(); ++k) {
          const auto& path = planned.adjacent_join_vias[n][k];
          const auto& at   = std::get<physical::Route_point>(path.steps.at(0));
          const auto& via  = std::get<physical::Route_via>(path.steps.at(1));
          geometry_out << (k ? "," : "")
                       << std::format(R"({{"lower_layer":"{}","via":"{}","orientation":"N","x":{},"y":{},"via_checked":false}})",
                                      json_util::escape(path.layer),
                                      json_util::escape(via.name),
                                      at.x,
                                      at.y);
        }
        geometry_out << "],\"unresolved_joins\":[";
        for (size_t k = 0; k < planned.joins[n].size(); ++k) {
          const auto& [from, to, x, y] = planned.joins[n][k];
          geometry_out << (k ? "," : "")
                       << std::format(R"(["{}","{}",{},{}])", json_util::escape(from), json_util::escape(to), x, y);
        }
        geometry_out << "],\"io_stub_failure\":\"" << json_util::escape(planned.io_stub_failure[n]) << '"';
        if (opt.cellbox_diagnostic_final) {
          geometry_out << ",\"diagnostic_final_local_rejected\":\"" << json_util::escape(R.final_local_rejected[n]) << '"';
        }
        geometry_out << '}';
      }
      geometry_out << std::format(R"(],"metal_conflict_shapes":{},"metal_unknown_shapes":{},)"
                                  R"("metal_audit_complete":{},"metal_audit_error":"{}"}})"
                                  "\n",
                                  total_metal_conflicts,
                                  total_metal_unknown,
                                  metal_audit_complete,
                                  json_util::escape(audit_error));
      if (!options.output_dir.empty() && !geometry_out) {
        throw std::runtime_error("cannot write Local backbone geometry diagnostic");
      }
      rep.local_metal_audit_complete  = metal_audit_complete;
      rep.local_metal_conflict_shapes = total_metal_conflicts;
      rep.local_metal_unknown_shapes  = total_metal_unknown;
      if (!opt.cellbox_diagnostic_final && !options.output_dir.empty() && !R.unassigned && metal_audit_complete
          && !total_metal_conflicts && !total_metal_unknown && occupancy) {
        rep.local_join_probe_attempted = true;
        rep.local_join_probe_complete
            = lr::write_local_join_probe(d, g, planned, work, *occupancy, options.output_dir, audit_deadline);
      }
    }
    if (!options.output_dir.empty() && !opt.dump_regions.empty()) {
      lr::dump_regions(d, g, boxes, R, regions, rep, options.output_dir, opt.dump_regions);
      // legroute_wires.json: every assigned segment as a wire on its layer and track, between the centres of its end
      // tiles (for drawings of the routes; the diagnostic mode only, the file is as large as the routing)
      std::vector<std::string> per_net(d.nets.size());
      for (const auto& sg : R.segments) {
        if (sg.net < 0 || sg.layer < 0 || sg.track < 0 || sg.layer >= int(g.layers.size())) {
          continue;
        }
        const auto&   lay        = g.layers[size_t(sg.layer)];
        const int64_t at         = g.track_coord(sg.layer, sg.track);
        per_net[size_t(sg.net)] += std::format("{}[{},{},{},{},{},{}]",
                                               per_net[size_t(sg.net)].empty() ? "" : ",",
                                               sg.layer,
                                               lay.horizontal ? g.cx(sg.lo) : at,
                                               lay.horizontal ? at : g.cy(sg.lo),
                                               lay.horizontal ? g.cx(sg.hi) : at,
                                               lay.horizontal ? at : g.cy(sg.hi),
                                               sg.critical ? 1 : 0);
      }
      std::ofstream wires_out(options.output_dir + "/legroute_wires.json");
      wires_out << std::format(R"({{"dbu":{},"layers":[)", g.dbu);
      for (size_t li = 0; li < g.layers.size(); ++li) {
        wires_out << std::format(R"({}{{"name":"{}","horizontal":{},"pitch":{},"width":{}}})",
                                 li ? "," : "",
                                 g.layers[li].name,
                                 g.layers[li].horizontal ? "true" : "false",
                                 g.layers[li].pitch,
                                 g.layers[li].width);
      }
      wires_out << R"(],"nets":[)";
      bool first_net = true;
      for (size_t ni = 0; ni < d.nets.size(); ++ni) {
        if (per_net[ni].empty()) {
          continue;
        }
        wires_out << std::format(R"({}{{"name":"{}","segs":[{}]}})",
                                 first_net ? "" : ",\n",
                                 fp.nets[d.nets[ni].frame].name,
                                 per_net[ni]);
        first_net = false;
      }
      wires_out << "]}\n";
    }
    if (!options.output_dir.empty()) {  // legroute.guide: the routes as detailed-routing guides (OpenROAD read_guides)
      // per net, the corridor of tiles its route covers, on every routing layer (the detailed router picks layers and
      // tracks inside it; the pin layer only on the tiles holding its pins, so the guides connect through the stack)
      std::ofstream            out(options.output_dir + "/legroute.guide");
      std::vector<std::string> layers;
      for (const auto& l : g.layers) {
        layers.push_back(l.name);
      }
      const auto rect = [&](int t, const std::string& layer) {
        return std::format("{} {} {} {} {}\n",
                           g.left(g.col(t)),
                           g.bottom(g.row(t)),
                           g.left(g.col(t) + 1),
                           g.bottom(g.row(t) + 1),
                           layer);
      };
      for (size_t n = 0; n < d.nets.size(); ++n) {
        if (!d.nets[n].routed || d.nets[n].clock || d.nets[n].power) {
          continue;
        }
        const auto&   tree = R.trees[n];
        std::set<int> corridor, pin_tiles;
        for (int ci : tree.conns) {
          const auto& c = R.conns[ci];
          for (size_t k = 0; k + 1 < c.poly.size(); ++k) {
            const auto [c0, r0] = c.poly[k];
            const auto [c1, r1] = c.poly[k + 1];
            for (int cc = std::min(c0, c1); cc <= std::max(c0, c1); ++cc) {
              for (int rr = std::min(r0, r1); rr <= std::max(r0, r1); ++rr) {
                corridor.insert(g.tile(cc, rr));
              }
            }
          }
        }
        for (const auto& node : tree.nodes) {
          if (node.tile >= 0) {
            corridor.insert(node.tile);
            if (!node.pins.empty()) {
              pin_tiles.insert(node.tile);
            }
          }
        }
        for (const auto& pin : d.nets[n].pins) {  // every pin's tile (a net inside one tile has no tree)
          const int box = pin.comp >= 0 && d.cell[pin.comp] >= 0 ? boxes.box_of[d.cell[pin.comp]] : -1;
          int       t   = -1;
          if (box >= 0) {
            t = boxes.boxes[box].tile;
          } else if (pin.comp >= 0) {
            t = g.tile_of(double(d.fp.components[pin.comp].place.x), double(d.fp.components[pin.comp].place.y));
          } else if (pin.term >= 0) {
            t = g.tile_of(double(d.fp.terminals[pin.term].x), double(d.fp.terminals[pin.term].y));
          }
          if (t >= 0) {
            corridor.insert(t);
            pin_tiles.insert(t);
          }
        }
        if (corridor.empty()) {
          continue;
        }
        out << d.fp.nets[d.nets[n].frame].name << "\n(\n";
        for (int t : corridor) {
          if (pin_tiles.contains(t)) {
            out << rect(t, g.pin_layer);
          }
          out << rect(t, g.box_layer);
          for (const auto& l : layers) {
            out << rect(t, l);
          }
        }
        out << ")\n";
      }
    }
    for (const auto& c : R.conns) {
      if (c.poly.size() >= 2) {
        ++rep.connections;
        const double w        = lr::wire_um(g, c);
        rep.wire_um          += w;
        rep.critical_wire_um += c.critical ? w : 0;
      }
    }
    for (int n = 0; n < int(d.nets.size()); ++n) {
      if (d.nets[n].critical) {
        rep.critical_delay_ps = std::max(rep.critical_delay_ps, lr::net_delay_ps(d, g, boxes, R, n));
      }
    }
    if (opt.final && !opt.cellbox_diagnostic_final
        && (rep.unassigned_segments || rep.cellbox_allowance_short_tiles || rep.crossing_policy_invalid
            || rep.final_domain_rejections || rep.local_handoff_rejected())) {
      // Final connects cell pins to a completed Local route. It must not hide a failed physical handoff by
      // attempting to reconstruct the interbox routing. Keep the legal SAT placement for planning feedback,
      // but do not emit any of the partial Local routing as connected wires.
      rep.failure_sample.push_back(rep.local_handoff_rejected()
                                       ? "required Local metal audit or join probe failed or remained unknown; Final skipped"
                                       : "required Local track assignment or reservation failed; Final skipped");
      const auto scope       = lr::final_net_scope(d);
      rep.final_skipped_nets = scope.skipped;
      for (size_t n = 0; n < d.nets.size(); ++n) {
        if (scope.required[n]) {
          ++rep.routed_nets;
          ++rep.unchecked_nets;
          rep.final_required_pins  += d.nets[n].pins.size();
          rep.clock_required_nets  += d.nets[n].clock;
          rep.clock_unchecked_nets += d.nets[n].clock;
        }
      }
      rep.seconds = lr::seconds_since(start);
      if (!options.output_dir.empty()) {
        write_legroute_report(rep, options.output_dir + "/legroute.json");
      }
      return rep;
    }
    if (opt.final) {
      lr::final_place(d, g, boxes, R, opt.threads);
      step("F1 in-box placement");
      if (!opt.final_box_dump.empty() && !options.output_dir.empty()) {
        dump_final_boxes(d, g, boxes, R, regions, options.output_dir + "/box_dump");
        if (opt.final_box_dump_stop) {
          rep.failure_sample.push_back("final_box_dump_stop: Final stopped after the F1 box dump (diagnostic)");
          rep.seconds = lr::seconds_since(start);
          if (!options.output_dir.empty()) {
            write_legroute_report(rep, options.output_dir + "/legroute.json");
          }
          return rep;
        }
      }
      lr::final_wires(d, g, boxes, R, regions, rep);
      if (opt.cellbox_diagnostic_final) {
        rep.final_diagnostic_only = true;
        rep.wires_emitted         = false;
        for (auto& net : fp.nets) {
          if (net.use != "POWER" && net.use != "GROUND") {
            net.wires.clear();
          }
        }
      }
      step("F2-F4 routes, taps, wires");
    }
    {  // actual cell motion against the given poses (not the box-centre metric)
      std::vector<double> moves;
      for (int c : d.movable) {
        const auto&  from = components[size_t(c)].place;
        const auto&  to   = fp.components[size_t(c)].place;
        const double dd   = (std::abs(double(to.x - from.x)) + std::abs(double(to.y - from.y))) / fp.dbu;
        moves.push_back(dd);
        rep.cells_moved += dd > 0;
      }
      if (!moves.empty()) {
        std::ranges::sort(moves);
        rep.cell_max_displacement_um = moves.back();
        // nearest rank: the ceil(0.95 N)-th smallest
        rep.cell_p95_displacement_um = moves[std::min(moves.size(), (moves.size() * 95 + 99) / 100) - 1];
      }
    }
    rep.seconds = lr::seconds_since(start);
    if (!options.output_dir.empty()) {
      write_legroute_report(rep, options.output_dir + "/legroute.json");
    }
  } catch (...) {
    restore_frame();
    throw;
  }
  return rep;
}

void write_legroute_report(const Legroute_report& r, const std::string& path) {
  const auto    quote = [](const std::string& s) { return '"' + json_util::escape(s) + '"'; };
  std::ofstream out(path);
  out << std::format(
      R"({{"schema_version":2,"tile_dbu":[{},{}],"dbu_per_micron":{},"tile_um":[{},{}],"grid":[{},{}],"regions":{},"tiles_free":{},"cells":{},"boxes":{},)"
      R"("mean_cells_per_box":{:.3f},"max_cells_per_box":{},"fill":{:.3f},"internal_nets":{},"external_nets":{},)"
      R"("displacement_um":{:.1f},"max_displacement_um":{:.2f},"cell_max_displacement_um":{:.3f},"cell_p95_displacement_um":{:.3f},"cells_moved":{},"spilled_regions":{},"sat_regions":{},"sat_variables":{},)"
      R"("sat_clauses":{},"sat_solves":{},"sat_relaxed_windows":{},"sat_root_fallbacks":{},"sat_unsat_regions":{},"sat_unknown_regions":{},"sat_cut_regions":{},"sat_waves":{},"sat_hops_free":{},"sat_hops_moved":{},"spill_rounds":{},"spilled_boxes":{},"windows_grown":{},"shifted_tiles":{},"alt_tried":{},"alt_rescued":{},"alt_seconds":{:.1f},)"
      R"("sat_binding_tiles":{},"sat_unjoined_crossings":{},"sat_hops_over_share":{},"global_nets":{},"global_pieces":{},"global_unrouted":{},"global_topology_disconnected":{},"global_full_edges":{},"global_overflow_edges":{},"global_overflow_tracks":{},"global_rounds":{},"global_seconds":{:.1f},"global_astar_weight":{},"global_search_pops":{},"global_search_limited":{},"global_retained_parts":{},"sat_fixed_overflow":{},"crossings_built":{},"crossings_reused":{},"partition_oversized":{},"partition_indivisible_cells":{},"partition_crossings":{},"partition_critical_crossings":{},"partition_max_cells":{},"partition_route_calls":{},"partition_rejected_repairs":{},"partition_seconds":{:.3f},"sat_adjacent_nets":{},"sat_relaxed_tiles":{},"sat_deficit_tracks":{},"crit_pinned":{},"crit_unpinned_regions":{},"crit_cost_before":{:.1f},"crit_cost_after":{:.1f},"tree_nets":{},"tree_boxes":{},"tree_skipped_nets":{},"tree_dropped_regions":{},"tree_tiles_over_half":{},"tree_um_before":{:.1f},"tree_um_after":{:.1f},"tree_max_load":{:.3f},"forced_trees_dropped":{},"forced_crit_unpinned":{},"tree_load_skipped":{},"tree_clauses_before":{},"tree_clauses_after":{},"io_feedthroughs":{},"io_boxes":{},"io_skipped":{},"io_lifted":{},"sat_alt_regions":{},"sat_both_regions":{},"sat_skipped_regions":{},"sat_fail_fast_skipped":{},"tier_kept":{},"tier_dropped":{},"tier_timeouts":{},"tier_solves":{},"tier_cost_before":{:.1f},"tier_cost_after":{:.1f},"tier_crit_before":{:.1f},"tier_crit_after":{:.1f},"lazy_rounds":{},"lazy_bounds":{},"lazy_skipped":{},"frozen_boxes":{},"legal_boxes":{},"legal_shuffled":{},"mesh_joins":{},"sat_seeded":{},"unroutable_cells":{},)"
      R"("global_pattern_attempts":{},"global_pattern_completed":{},"global_pattern_fallbacks":{},"global_pattern_work":{},"global_pattern_work_limited":{},)"
      R"("sat_seconds":{:.3f},"sat_seconds_max":{:.3f},)"
      R"("connections":{},"critical_connections":{},"unassigned_segments":{},)"
      R"("l5_repair_attempts":{},"l5_repair_successes":{},"l5_repair_unknown":{},"l5_repair_unsat":{},)"
      R"("l5_repair_sat_calls":{},"l5_repair_sat":{},"l5_repair_sat_unsat":{},"l5_repair_sat_unknown":{},"l5_repair_sat_variables":{},"l5_repair_sat_clauses":{},"l5_repair_sat_build_seconds":{:.6f},"l5_repair_sat_solve_seconds":{:.6f},"l5_repair_candidate_seconds":{:.6f},"l5_repair_native_validation_seconds":{:.6f},"l5_repair_seconds":{:.6f},)"
      R"("clock_domains":{},"clock_ribs":{},"clock_excess_tiles":{},"clock_excess_tracks":{},"crossing_over_policy":{},"crossing_policy_invalid":{},"cellbox_allowance_short_tiles":{},"cellbox_allowance_short_tracks":{},"wire_um":{:.1f},"critical_wire_um":{:.1f},"vias":{},"wires_emitted":{},"route_passes":{},"routed_nets":{},"local_nets":{},"joins":{},"taps":{},)"
      R"("connected_nets":{},"unchecked_nets":{},"failed_nets":{},"global_conflict_nets":{},"join_failures":{},"tap_failures":{},)"
      R"("tree_fallbacks":{},"clock_required_nets":{},"clock_connected_nets":{},"clock_failed_nets":{},"clock_unchecked_nets":{},)"
      R"("final_domain_rejections":{},"final_required_pins":{},"final_connected_pins":{},"final_piece_seconds":{:.6f},)"
      R"("final_prior_cut_rejections":{},"final_pending_cut_rejections":{},"final_union_cut_rejections":{},"final_precommit_disconnected":{},"final_skipped_nets":{},)"
      R"("disconnected_nets":{},"critical_delay_ps":{:.2f},"seconds":{:.3f},"steps":[)",
      r.tile_w,
      r.tile_h,
      r.dbu_per_micron,
      double(r.tile_w) / r.dbu_per_micron,
      double(r.tile_h) / r.dbu_per_micron,
      r.cols,
      r.rows,
      r.regions,
      r.tiles_free,
      r.cells,
      r.boxes,
      r.mean_cells_per_box,
      r.max_cells_per_box,
      r.fill,
      r.internal_nets,
      r.external_nets,
      r.displacement_um,
      r.max_displacement_um,
      r.cell_max_displacement_um,
      r.cell_p95_displacement_um,
      r.cells_moved,
      r.spilled_regions,
      r.sat_regions,
      r.sat_variables,
      r.sat_clauses,
      r.sat_solves,
      r.sat_relaxed_windows,
      r.sat_root_fallbacks,
      r.sat_unsat_regions,
      r.sat_unknown_regions,
      r.sat_cut_regions,
      r.sat_waves,
      r.sat_hops_free,
      r.sat_hops_moved,
      r.spill_rounds,
      r.spilled_boxes,
      r.windows_grown,
      r.shifted_tiles,
      r.alt_tried,
      r.alt_rescued,
      r.alt_seconds,
      r.sat_binding_tiles,
      r.sat_unjoined_crossings,
      r.sat_hops_over_share,
      r.global_nets,
      r.global_pieces,
      r.global_unrouted,
      r.global_topology_disconnected,
      r.global_full_edges,
      r.global_overflow_edges,
      r.global_overflow_tracks,
      r.global_rounds,
      r.global_seconds,
      r.global_astar_weight,
      r.global_search_pops,
      r.global_search_limited,
      r.global_retained_parts,
      r.sat_fixed_overflow,
      r.crossings_built,
      r.crossings_reused,
      r.partition_oversized,
      r.partition_indivisible_cells,
      r.partition_crossings,
      r.partition_critical_crossings,
      r.partition_max_cells,
      r.partition_route_calls,
      r.partition_rejected_repairs,
      r.partition_seconds,
      r.sat_adjacent_nets,
      r.sat_relaxed_tiles,
      r.sat_deficit_tracks,
      r.crit_pinned,
      r.crit_unpinned_regions,
      r.crit_cost_before,
      r.crit_cost_after,
      r.tree_nets,
      r.tree_boxes,
      r.tree_skipped_nets,
      r.tree_dropped_regions,
      r.tree_tiles_over_half,
      r.tree_um_before,
      r.tree_um_after,
      r.tree_max_load,
      r.forced_trees_dropped,
      r.forced_crit_unpinned,
      r.tree_load_skipped,
      r.tree_clauses_before,
      r.tree_clauses_after,
      r.io_feedthroughs,
      r.io_boxes,
      r.io_skipped,
      r.io_lifted,
      r.sat_alt_regions,
      r.sat_both_regions,
      r.sat_skipped_regions,
      r.sat_fail_fast_skipped,
      r.tier_kept,
      r.tier_dropped,
      r.tier_timeouts,
      r.tier_solves,
      r.tier_cost_before,
      r.tier_cost_after,
      r.tier_crit_before,
      r.tier_crit_after,
      r.lazy_rounds,
      r.lazy_bounds,
      r.lazy_skipped,
      r.frozen_boxes,
      r.legal_boxes,
      r.legal_shuffled,
      r.mesh_joins,
      r.sat_seeded,
      r.unroutable_components.size(),
      r.global_pattern_attempts,
      r.global_pattern_completed,
      r.global_pattern_fallbacks,
      r.global_pattern_work,
      r.global_pattern_work_limited,
      r.sat_seconds,
      r.sat_seconds_max,
      r.connections,
      r.critical_connections,
      r.unassigned_segments,
      r.l5_repair_attempts,
      r.l5_repair_successes,
      r.l5_repair_unknown,
      r.l5_repair_unsat,
      r.l5_repair_sat_calls,
      r.l5_repair_sat,
      r.l5_repair_sat_unsat,
      r.l5_repair_sat_unknown,
      r.l5_repair_sat_variables,
      r.l5_repair_sat_clauses,
      r.l5_repair_sat_build_seconds,
      r.l5_repair_sat_solve_seconds,
      r.l5_repair_candidate_seconds,
      r.l5_repair_native_validation_seconds,
      r.l5_repair_seconds,

      r.clock_domains,
      r.clock_ribs,
      r.clock_excess_tiles,
      r.clock_excess_tracks,
      r.crossing_over_policy,
      r.crossing_policy_invalid,
      r.cellbox_allowance_short_tiles,
      r.cellbox_allowance_short_tracks,
      r.wire_um,
      r.critical_wire_um,
      r.vias,
      r.wires_emitted,
      r.route_passes,
      r.routed_nets,
      r.local_nets,
      r.joins,
      r.taps,
      r.connected_nets,
      r.unchecked_nets,
      r.failed_nets,
      r.global_conflict_nets,
      r.join_failures,
      r.tap_failures,
      r.tree_fallbacks,
      r.clock_required_nets,
      r.clock_connected_nets,
      r.clock_failed_nets,
      r.clock_unchecked_nets,
      r.final_domain_rejections,
      r.final_required_pins,
      r.final_connected_pins,
      r.final_piece_seconds,
      r.final_prior_cut_rejections,
      r.final_pending_cut_rejections,
      r.final_union_cut_rejections,
      r.final_precommit_disconnected,
      r.final_skipped_nets,
      r.disconnected_nets,
      r.critical_delay_ps,
      r.seconds);
  for (size_t k = 0; k < r.steps.size(); ++k) {
    out << std::format(R"({}{{"step":"{}","seconds":{:.3f}}})", k ? "," : "", r.steps[k].name, r.steps[k].seconds);
  }
  out << std::format(
      R"(],"partition_timeout_rounds":{},"partition_timeout_regions":{},"sat_reused_regions":{},)"
      R"("final_diagnostic_only":{},"final_local_rejected_nets":{},"final_local_rejected_pins":{},)"
      R"("final_detail_failed_nets":{},"final_detail_failed_pins":{},"final_detail_unattempted_pins":{},)"
      R"("local_metal_audit_attempted":{},"local_metal_audit_complete":{},"local_metal_conflict_shapes":{},"local_metal_unknown_shapes":{},)"
      R"("local_join_probe_attempted":{},"local_join_probe_complete":{},"track_failures":{{)",
      r.partition_timeout_rounds,
      r.partition_timeout_regions,
      r.sat_reused_regions,
      r.final_diagnostic_only,
      r.final_local_rejected_nets,
      r.final_local_rejected_pins,
      r.final_detail_failed_nets,
      r.final_detail_failed_pins,
      r.final_detail_unattempted_pins,
      r.local_metal_audit_attempted,
      r.local_metal_audit_complete,
      r.local_metal_conflict_shapes,
      r.local_metal_unknown_shapes,
      r.local_join_probe_attempted,
      r.local_join_probe_complete);
  size_t k = 0;
  for (const auto& [kind, n] : r.track_failures) {
    out << (k++ ? "," : "") << quote(kind) << ':' << n;
  }
  out << R"(},"track_pick_rejections":{)";
  k = 0;
  for (const auto& [kind, n] : r.track_pick_rejections) {
    out << (k++ ? "," : "") << quote(kind) << ':' << n;
  }
  out << R"(},"track_fallback_shadow":{)";
  k = 0;
  for (const auto& [kind, n] : r.track_fallback_shadow) {
    out << (k++ ? "," : "") << quote(kind) << ':' << n;
  }
  out << R"(},"track_failure_samples":[)";
  for (size_t i = 0; i < r.track_failure_samples.size(); ++i) {
    out << (i ? "," : "") << quote(r.track_failure_samples[i]);
  }
  out << R"(],"clock_conflicts":[)";
  for (size_t i = 0; i < r.clock_conflicts.size(); ++i) {
    out << (i ? "," : "") << quote(r.clock_conflicts[i]);
  }
  out << R"(],"failure_reasons":{)";
  k = 0;
  for (const auto& [why, n] : r.failure_reasons) {
    out << (k++ ? "," : "") << quote(why) << ':' << n;
  }
  out << R"(},"failure_sample":[)";
  for (size_t i = 0; i < r.failure_sample.size(); ++i) {
    out << (i ? "," : "") << quote(r.failure_sample[i]);
  }
  out << R"(],"region_stats":[)";  // [status, boxes, free tiles]
  for (size_t i = 0; i < r.region_stats.size(); ++i) {
    const auto& st = r.region_stats[i];
    out << std::format("{}[{},{},{}]", i ? "," : "", st.status, st.boxes, st.tiles);
  }
  out << R"(],"alt_trials":[)";
  for (size_t i = 0; i < r.alt_trials.size(); ++i) {
    const auto& trial = r.alt_trials[i];
    out << std::format(
        R"({}{{"region":{},"round":{},"fill":{:.3f},"cut":{:.3f},"boxes":{},"hops":{},"boundary_tracks":{},"pinned":{},"status":"{}","adopted":"{}","families":{{)",
        i ? "," : "",
        trial.region,
        trial.round,
        trial.fill,
        trial.cut,
        trial.boxes,
        trial.hops,
        trial.boundary_tracks,
        trial.pinned,
        trial.status,
        trial.adopted);
    for (size_t fi = 0; fi < trial.families.size(); ++fi) {
      const auto& [fam_kind, fam_status, fam_seconds] = trial.families[fi];
      out << std::format(R"({}"{}":["{}",{:.1f}])", fi ? "," : "", fam_kind, fam_status, fam_seconds);
    }
    out << "}}";
  }
  out << R"(],"failure_boxes":[)";
  for (size_t i = 0; i < r.failure_boxes.size(); ++i) {
    const auto& b = r.failure_boxes[i];
    out << std::format(
        R"({}{{"box":{},"tile":[{},{}],"cells":{},"fill":{:.2f},"internal_pins":{},"external_pins":{},"failed_nets":{},)"
        R"("box_demand":{:.1f},"box_capacity":{:.0f},"vertical_demand":{:.1f}}})",
        i ? "," : "",
        b.box,
        b.col,
        b.row,
        b.cells,
        b.fill,
        b.internal_pins,
        b.external_pins,
        b.failed_nets,
        b.box_demand,
        b.box_capacity,
        b.vertical_demand);
  }
  out << "]";
  const auto pattern = [&](const char* name, const std::vector<Legroute_report::Bucket>& v) {
    out << ",\"" << name << "\":[";
    for (size_t i = 0; i < v.size(); ++i) {
      out << std::format(R"({}{{"bucket":"{}","boxes":{},"failing":{}}})", i ? "," : "", v[i].name, v[i].boxes, v[i].failing);
    }
    out << "]";
  };
  pattern("fill_pattern", r.fill_pattern);
  pattern("internal_pin_pattern", r.internal_pattern);
  pattern("external_pin_pattern", r.external_pattern);
  out << std::format(
      R"(,"coarse_backbone_fixed":{},"coarse_backbone_parts":{},"coarse_backbone_edges":{},"coarse_attachment_stubs":{})",
      r.coarse_backbone_fixed,
      r.coarse_backbone_parts,
      r.coarse_backbone_edges,
      r.coarse_attachment_stubs);
  out << std::format(R"(,"box_route_seed_attempts":{},"box_route_seed_successes":{})",
                     r.box_route_seed_attempts,
                     r.box_route_seed_successes);
  out << std::format(R"(,"coarse_attachment_choices":{},"coarse_attachments_changed":{})",
                     r.coarse_attachment_choices,
                     r.coarse_attachments_changed);
  out << std::format(
      R"(,"coarse_corridor_free":{},"corridor_components":{},"corridor_candidate_edges":{},"corridor_connectivity_cuts":{},)"
      R"("corridor_connectivity_rounds":{},"corridor_checked_components":{},"corridor_selected_edges":{})",
      r.coarse_corridor_free,
      r.corridor_components,
      r.corridor_candidate_edges,
      r.corridor_connectivity_cuts,
      r.corridor_connectivity_rounds,
      r.corridor_checked_components,
      r.corridor_selected_edges);
  out << std::format(R"(,"corridor_seed_attempts":{},"corridor_seed_successes":{},"corridor_seed_solves":{},)"
                     R"("corridor_seed_variables":{},"corridor_seed_clauses":{},"corridor_seed_seconds":{})",
                     r.corridor_seed_attempts,
                     r.corridor_seed_successes,
                     r.corridor_seed_solves,
                     r.corridor_seed_variables,
                     r.corridor_seed_clauses,
                     r.corridor_seed_seconds);
  out << "}\n";
  if (!out) {
    throw std::runtime_error("cannot write " + path);
  }
}
}  // namespace livehd::placement
