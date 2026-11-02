// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute driver: legroute.local (L1 grid, L2 boxes, L3 SAT placement, L4 clock spines, L5 tracks) ->
// legroute.final (F1-F4).
#include "legroute.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "core/json_util.hpp"
#include "legroute_internal.hpp"

namespace livehd::placement {
namespace lr {
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

Legroute_report legroute(const physical::Technology& tech, physical::Floorplan& fp, const timing::Feedback& timing,
                         const Legroute_options& options, const Legroute_regions& regions, const physical::Tech_profile* profile) {
  validate_legroute_options(options);
  const auto      start = lr::Clock::now();
  Legroute_report rep;
  auto            opt = options;
  if (!opt.threads) {
    opt.threads = std::max(1u, std::thread::hardware_concurrency());
  }
  auto                                     components = fp.components;
  auto                                     rules      = fp.nondefault_rules;
  std::vector<std::vector<physical::Wire>> wires;
  for (const auto& n : fp.nets) {
    wires.push_back(n.wires);
  }
  auto       step_t = lr::Clock::now();
  const auto step   = [&](const char* name) {
    rep.steps.push_back({name, lr::seconds_since(step_t)});
    std::fprintf(stderr, "legroute step %s: %.1f s\n", name, rep.steps.back().seconds);
    step_t = lr::Clock::now();
  };
  try {
    auto d      = lr::build_design(tech, fp, timing, opt, profile);
    auto g      = lr::build_grid(d, regions);
    rep.tile_w  = g.tile_w;
    rep.tile_h  = g.tile_h;
    rep.cols    = g.cols;
    rep.rows    = g.rows;
    rep.regions = g.regions;
    for (auto b : g.blocked) {
      rep.tiles_free += !b;
    }
    step("L1 grid");
    auto boxes   = lr::build_boxes(d, g);
    rep.cells    = d.movable.size();
    rep.boxes    = boxes.boxes.size();
    double sites = 0;
    for (const auto& b : boxes.boxes) {
      rep.max_cells_per_box  = std::max(rep.max_cells_per_box, b.cells.size());
      sites                 += b.sites;
      rep.legal_boxes       += b.placed;
    }
    rep.mean_cells_per_box = rep.boxes ? double(rep.cells) / rep.boxes : 0;
    rep.fill               = rep.boxes ? sites / (2.0 * g.sites * rep.boxes) : 0;
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
    lr::sat_place(d, g, boxes, regions, R, rep);
    lr::box_demand(d, g, boxes, R);
    for (const auto& b : boxes.boxes) {
      if (b.placed && b.tile == g.tile_of(b.cx, b.cy)) {  // legal_boxes: on its legal tile, nothing moved
        continue;
      }
      const double dd      = (std::abs(double(g.cx(g.col(b.tile))) - b.cx) + std::abs(double(g.cy(g.row(b.tile))) - b.cy)) / g.dbu;
      rep.displacement_um += dd;
      rep.max_displacement_um = std::max(rep.max_displacement_um, dd);
    }
    step("L3 SAT placement");
    lr::pack_cells(d, g, boxes);
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
    step("L4 clock spines");
    lr::assign_layers(d, g, R);
    lr::assign_tracks(d, g, R, opt.threads);
    rep.unassigned_segments = R.unassigned;
    step("L5 layers, tracks");
    if (!options.output_dir.empty() && !opt.dump_regions.empty()) {
      lr::dump_regions(d, g, boxes, R, regions, rep, options.output_dir, opt.dump_regions);
      // legroute_wires.json: every assigned segment as a wire on its layer and track, between the centres of its end
      // tiles (for drawings of the routes; the diagnostic mode only, the file is as large as the routing)
      std::vector<std::string> per_net(d.nets.size());
      for (const auto& sg : R.segments) {
        if (sg.net < 0 || sg.layer < 0 || sg.track < 0 || sg.layer >= int(g.layers.size())) {
          continue;
        }
        const auto&   lay   = g.layers[size_t(sg.layer)];
        const int64_t at    = g.track_coord(sg.layer, sg.track);
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
        wires_out << std::format(R"({}{{"name":"{}","segs":[{}]}})", first_net ? "" : ",\n", fp.nets[d.nets[ni].frame].name, per_net[ni]);
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
    if (opt.final) {
      lr::final_place(d, g, boxes, R, opt.threads);
      step("F1 in-box placement");
      lr::final_wires(d, g, boxes, R, regions, rep);
      step("F2-F4 routes, taps, wires");
    }
    rep.seconds = lr::seconds_since(start);
    if (!options.output_dir.empty()) {
      write_legroute_report(rep, options.output_dir + "/legroute.json");
    }
  } catch (...) {
    fp.components       = std::move(components);
    fp.nondefault_rules = std::move(rules);
    for (size_t n = 0; n < fp.nets.size() && n < wires.size(); ++n) {
      fp.nets[n].wires = std::move(wires[n]);
    }
    throw;
  }
  return rep;
}

void write_legroute_report(const Legroute_report& r, const std::string& path) {
  const auto    quote = [](const std::string& s) { return '"' + json_util::escape(s) + '"'; };
  std::ofstream out(path);
  out << std::format(
      R"({{"schema_version":2,"tile_um":[{},{}],"grid":[{},{}],"regions":{},"tiles_free":{},"cells":{},"boxes":{},)"
      R"("mean_cells_per_box":{:.3f},"max_cells_per_box":{},"fill":{:.3f},"internal_nets":{},"external_nets":{},)"
      R"("displacement_um":{:.1f},"max_displacement_um":{:.2f},"spilled_regions":{},"sat_regions":{},"sat_variables":{},)"
      R"("sat_clauses":{},"sat_solves":{},"sat_relaxed_windows":{},"sat_unsat_regions":{},"sat_unknown_regions":{},"sat_cut_regions":{},"sat_waves":{},"sat_hops_free":{},"sat_hops_moved":{},"spill_rounds":{},"spilled_boxes":{},"windows_grown":{},"shifted_tiles":{},"alt_tried":{},"alt_rescued":{},"alt_seconds":{:.1f},)"
      R"("sat_binding_tiles":{},"sat_unjoined_crossings":{},"sat_hops_over_share":{},"global_nets":{},"global_pieces":{},"global_unrouted":{},"global_full_edges":{},"global_overflow_edges":{},"global_overflow_tracks":{},"global_rounds":{},"global_seconds":{:.1f},"sat_adjacent_nets":{},"sat_relaxed_tiles":{},"sat_deficit_tracks":{},"crit_pinned":{},"crit_unpinned_regions":{},"crit_cost_before":{:.1f},"crit_cost_after":{:.1f},"tree_nets":{},"tree_boxes":{},"tree_skipped_nets":{},"tree_dropped_regions":{},"tree_tiles_over_half":{},"tree_um_before":{:.1f},"tree_um_after":{:.1f},"tree_max_load":{:.3f},"forced_trees_dropped":{},"forced_crit_unpinned":{},"tree_load_skipped":{},"tree_clauses_before":{},"tree_clauses_after":{},"io_feedthroughs":{},"io_boxes":{},"io_skipped":{},"io_lifted":{},"sat_alt_regions":{},"sat_both_regions":{},"sat_skipped_regions":{},"sat_fail_fast_skipped":{},"tier_kept":{},"tier_dropped":{},"tier_timeouts":{},"tier_solves":{},"tier_cost_before":{:.1f},"tier_cost_after":{:.1f},"tier_crit_before":{:.1f},"tier_crit_after":{:.1f},"lazy_rounds":{},"lazy_bounds":{},"lazy_skipped":{},"frozen_boxes":{},"legal_boxes":{},"legal_shuffled":{},"sat_seeded":{},"unroutable_cells":{},)"
      R"("sat_seconds":{:.3f},"sat_seconds_max":{:.3f},)"
      R"("connections":{},"critical_connections":{},"unassigned_segments":{},)"
      R"("clock_domains":{},"clock_ribs":{},"wire_um":{:.1f},"critical_wire_um":{:.1f},"vias":{},"wires_emitted":{},"route_passes":{},"routed_nets":{},"local_nets":{},"joins":{},"taps":{},)"
      R"("connected_nets":{},"unchecked_nets":{},"failed_nets":{},"global_conflict_nets":{},"join_failures":{},"tap_failures":{},)"
      R"("tree_fallbacks":{},)"
      R"("disconnected_nets":{},"critical_delay_ps":{:.2f},"seconds":{:.3f},"steps":[)",
      r.tile_w,
      r.tile_h,
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
      r.spilled_regions,
      r.sat_regions,
      r.sat_variables,
      r.sat_clauses,
      r.sat_solves,
      r.sat_relaxed_windows,
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
      r.global_full_edges,
      r.global_overflow_edges,
      r.global_overflow_tracks,
      r.global_rounds,
      r.global_seconds,
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
      r.sat_seeded,
      r.unroutable_components.size(),
      r.sat_seconds,
      r.sat_seconds_max,
      r.connections,
      r.critical_connections,
      r.unassigned_segments,
      r.clock_domains,
      r.clock_ribs,
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
      r.disconnected_nets,
      r.critical_delay_ps,
      r.seconds);
  for (size_t k = 0; k < r.steps.size(); ++k) {
    out << std::format(R"({}{{"step":"{}","seconds":{:.3f}}})", k ? "," : "", r.steps[k].name, r.steps[k].seconds);
  }
  out << R"(],"failure_reasons":{)";
  size_t k = 0;
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
    out << std::format(R"({}{{"region":{},"round":{},"fill":{:.3f},"cut":{:.3f},"boxes":{},"hops":{},"boundary_tracks":{},"pinned":{},"status":"{}","adopted":"{}","families":{{)",
                       i ? "," : "", trial.region, trial.round, trial.fill, trial.cut, trial.boxes, trial.hops, trial.boundary_tracks, trial.pinned,
                       trial.status, trial.adopted);
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
  out << "}\n";
  if (!out) {
    throw std::runtime_error("cannot write " + path);
  }
}
}  // namespace livehd::placement
