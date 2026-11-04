// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <optional>
#include <set>

#include "legroute_internal.hpp"

namespace livehd::placement {
Legroute_plan_report legroute_plan(const physical::Technology& tech, physical::Floorplan& fp, const timing::Feedback& timing,
                                   const Legroute_options& options, const Legroute_regions& regions,
                                   const physical::Tech_profile* profile, size_t net_pops, bool boxed_handoff,
                                   Legroute_congestion* congestion, bool measure_only) {
  const auto start = lr::Clock::now();
  validate_legroute_options(options);
  if (boxed_handoff && !options.legal_boxes) {
    throw std::invalid_argument("boxed_handoff diagnostic requires legal_boxes");
  }
  auto d         = lr::build_design(tech, fp, timing, options, profile);
  // Existing region IOs never constrain exploration. Keep the seed mesh only
  // for the size/density assessment when route-driven cuts are disabled.
  auto resources = regions;
  resources.pins.clear();
  auto      g            = lr::build_grid(d, resources);
  auto      seed_regions = g.region;
  const int seed_count   = g.regions;
  g.regions              = 1;
  g.region.assign(g.size(), 0);
  const bool           box_endpoints = boxed_handoff || lr::movable_route_partition(options);
  Legroute_plan_report out;
  out.endpoint_basis = box_endpoints ? "box_centroids" : "physical_pins";
  if (congestion) {
    *congestion = {};  // never leave a previous measured map on a skipped attempt
  }
  // PG geometry comes from the DEF's special nets, or from the generated grid legroute_power stored in the
  // regions (its shapes in regions.obstacles, which build_grid deducts; ownership in regions.power_nets)
  const auto has_pg = [](const std::vector<physical::Net>& nets) {
    return std::ranges::any_of(nets,
                               [](const auto& net) { return (net.use == "POWER" || net.use == "GROUND") && !net.wires.empty(); });
  };
  out.pg_missing = !has_pg(fp.special_nets) && regions.power_nets.empty();  // generated nets are PG by construction
  if (out.pg_missing) {
    std::fprintf(stderr,
                 "legroute plan: no power grid in the floorplan or the generated regions; routing capacities carry no "
                 "power budget\n");
  }
  std::optional<lr::Boxes> boxes;
  if (box_endpoints) {
    boxes.emplace(lr::build_boxes(d, g));
  }
  if (!measure_only) {
    // This whole-core area bound is independent of box grouping, placement
    // endpoints and the future region cuts. Do not screen old seed regions:
    // their boundaries may still move. Explicit map measurements keep routing.
    const std::vector<int> empty_capacity(g.size());
    auto                   density = lr::assess_legroute_gates(d, g, nullptr, {}, {}, empty_capacity, empty_capacity, false);
    if (density.cell_dense_regions) {
      density.max_routing_density = std::numeric_limits<double>::quiet_NaN();
      out.gates                   = std::move(density);
      out.route_skip_reason       = Legroute_plan_report::Route_skip_reason::physical_density;
      for (const auto& net : d.nets) {
        if (net.routed && !net.clock) {
          ++out.signals;
          out.timed_signals += std::isfinite(net.slack);
        }
      }
      out.unchecked_signals = out.signals;
      out.component_pressure.assign(fp.components.size(), 1);
      out.component_hot_distance.assign(fp.components.size(), std::numeric_limits<unsigned>::max());
      for (int c : d.movable) {
        out.component_hot_distance[size_t(c)] = 0;
      }
      out.hot_tiles           = g.size();
      out.tile_width_dbu      = g.tile_w;
      out.tile_height_dbu     = g.tile_h;
      out.global_astar_weight = options.global_astar_weight;
      out.seconds             = lr::seconds_since(start);
      return out;
    }
    // The cells must fit at the route's own granularity before the route can mean anything: a coarse window
    // over fit_density cannot hold the cells the route would start from. Local pressure, no route.
    if (options.fit_density > 0) {
      auto fit                 = lr::assess_window_fit(d, g, options.fit_density, options.fit_window);
      out.fit_windows          = fit.windows;
      out.fit_dense_windows    = fit.dense_windows;
      out.fit_max_density      = fit.max_density;
      bool                fits = fit.dense_windows == 0;
      // With boxes, the authoritative fit test is local's own: every box on a distinct fitting tile near its
      // centre (the native windows, grown only as the joint matching needs). Window density is then a report.
      std::vector<double> box_pressure;
      std::vector<int>    pressed_cells;  // members of the boxes the matching names (movable ids)
      if (boxes) {
        const auto match      = lr::match_box_fit(d, g, *boxes, options.fit_first);
        out.fit_matched       = match.matched;
        out.fit_far_boxes     = match.far_boxes;
        out.fit_max_move      = match.max_move;
        out.fit_applied       = match.applied;
        out.fit_reason        = match.reason;
        out.fit_movable_boxes = match.movable_boxes;
        out.fit_free_tiles    = match.free_tiles;
        if (match.reason != "budget") {  // a stopped search is a computational unknown: no located pressure
          box_pressure = match.tile_pressure;
        }
        // a decided matching is the authoritative fit test: its named boxes, not the diagnostic dense windows, get
        // pressure (after a budget stop the windows decide and keep theirs)
        if (match.reason != "budget") {
          fit.tile_pressure.assign(g.size(), 1);
        }
        // a stopped search (budget) is a computational unknown, not a density failure: the window check decides
        fits = match.reason == "budget" ? fit.dense_windows == 0 : match.fits;
        if (match.reason != "budget") {
          if (!match.pressure_boxes.empty()) {
            std::fprintf(stderr, "legroute plan: box fit names %zu boxes to spread\n", match.pressure_boxes.size());
          }
          for (int b : match.pressure_boxes) {
            const auto& cells = boxes->boxes[size_t(b)].cells;
            pressed_cells.insert(pressed_cells.end(), cells.begin(), cells.end());
          }
        }
        if (match.applied) {
          out.endpoint_basis = "matched_box_tiles";
        }
      }
      if (!fits) {
        // the score candidates are ranked by: with box matching, the boxes it could not place near home (a
        // pigeonhole: boxes beyond the free tiles); the dense windows only without boxes (a diagnostic otherwise)
        size_t deficit = fit.dense_windows;
        if (boxes && !out.fit_reason.empty() && out.fit_reason != "budget") {
          deficit = out.fit_reason == "count" ? out.fit_movable_boxes - std::min(out.fit_movable_boxes, out.fit_free_tiles)
                                              : out.fit_far_boxes;
        }
        density.cell_dense_regions  = std::max<size_t>(1, deficit);
        density.max_cell_density    = std::max(density.max_cell_density, fit.max_density);
        density.max_routing_density = std::numeric_limits<double>::quiet_NaN();
        out.gates                   = std::move(density);
        out.route_skip_reason       = Legroute_plan_report::Route_skip_reason::window_density;
        for (const auto& net : d.nets) {
          if (net.routed && !net.clock) {
            ++out.signals;
            out.timed_signals += std::isfinite(net.slack);
          }
        }
        out.unchecked_signals = out.signals;
        // Hot tiles are the dense windows; distance from them bounds the local feedback scope.
        std::vector<unsigned> distance(g.size(), std::numeric_limits<unsigned>::max());
        std::vector<int>      queue;
        // a box's own cells, by their own centres: a grouped box can straddle an empty centroid tile
        const auto            cell_tile = [&](int c) {
          const auto& component = fp.components[size_t(c)];
          const auto& macro     = *d.macro[size_t(c)];
          return g.tile_of(component.place.x + macro.width * fp.dbu / 2, component.place.y + macro.height * fp.dbu / 2);
        };
        for (int m : pressed_cells) {
          auto& p = fit.tile_pressure[size_t(cell_tile(d.movable[size_t(m)]))];
          p       = std::max(p, 1.1);
        }
        for (size_t t = 0; t < g.size(); ++t) {
          if (!box_pressure.empty()) {
            fit.tile_pressure[t] = std::max(fit.tile_pressure[t], box_pressure[t]);
          }
          if (fit.tile_pressure[t] > 1) {
            distance[t] = 0;
            queue.push_back(int(t));
          }
        }
        out.hot_tiles = queue.size();
        for (size_t k = 0; k < queue.size(); ++k) {
          const int t = queue[k];
          for (int q : {g.col(t) > 0 ? t - 1 : -1,
                        g.col(t) + 1 < g.cols ? t + 1 : -1,
                        g.row(t) > 0 ? t - g.cols : -1,
                        g.row(t) + 1 < g.rows ? t + g.cols : -1}) {
            if (q >= 0 && distance[size_t(q)] == std::numeric_limits<unsigned>::max()) {
              distance[size_t(q)] = distance[size_t(t)] + 1;
              queue.push_back(q);
            }
          }
        }
        out.component_pressure.assign(fp.components.size(), 1);
        out.component_hot_distance.assign(fp.components.size(), std::numeric_limits<unsigned>::max());
        for (int c : d.movable) {
          const int tile                        = cell_tile(c);
          out.component_pressure[size_t(c)]     = fit.tile_pressure[size_t(tile)];
          out.component_hot_distance[size_t(c)] = distance[size_t(tile)];
        }
        for (int m : pressed_cells) {
          const auto c                  = size_t(d.movable[size_t(m)]);
          out.component_pressure[c]     = std::max(out.component_pressure[c], 1.1);
          out.component_hot_distance[c] = 0;
        }
        if (!out.hot_tiles) {  // an incomplete matching with no dense window names no place: spread everything a little
          for (int c : d.movable) {
            out.component_pressure[size_t(c)]     = 1.05;
            out.component_hot_distance[size_t(c)] = 0;
          }
          out.hot_tiles = g.size();
        }
        out.tile_width_dbu      = g.tile_w;
        out.tile_height_dbu     = g.tile_h;
        out.global_astar_weight = options.global_astar_weight;
        out.seconds             = lr::seconds_since(start);
        return out;
      }
    }
  }
  std::vector<int> cap_h(g.size()), cap_v(g.size());
  for (size_t l = 0; l < g.layers.size(); ++l) {
    for (size_t t = 0; t < g.size(); ++t) {
      (g.layers[l].horizontal ? cap_h : cap_v)[t] += g.cap[l][t];
    }
  }
  // The clock budget: at least the old flat reservation (a spine track per tile, a rib track every rib_tiles rows), and the tree
  // L4 would build from the boxes where it takes more
  const auto clock = options.clock_spine ? lr::clock_tree_charge(d, g, boxes ? &*boxes : nullptr, options.rib_tiles)
                                         : lr::Clock_charge{std::vector<int>(g.size(), 0), std::vector<int>(g.size(), 0)};
  for (size_t t = 0; t < g.size(); ++t) {
    cap_h[t] += g.box_cap[t];
    if (options.clock_spine) {
      cap_v[t] -= std::max(1, clock.v[t]);
      cap_h[t] -= std::max(int(g.row(int(t)) % std::max(1u, options.rib_tiles) == 0), clock.h[t]);
    }
    cap_h[t] = std::max(0, cap_h[t]);
    cap_v[t] = std::max(0, cap_v[t]);
  }
  if (options.clock_spine) {
    int64_t charged_v = 0, charged_h = 0, tree_v = 0, tree_h = 0, left_v = 0;
    for (size_t t = 0; t < g.size(); ++t) {
      charged_v += std::max(1, clock.v[t]);
      charged_h += std::max(int(g.row(int(t)) % std::max(1u, options.rib_tiles) == 0), clock.h[t]);
      tree_v    += clock.v[t];
      tree_h    += clock.h[t];
      left_v    += cap_v[t];
    }
    std::fprintf(
        stderr,
        "legroute plan: clock budget %lld V / %lld H tile-tracks (tree estimate %lld V / %lld H; %.1f%% of the remaining V)\n",
        static_cast<long long>(charged_v),
        static_cast<long long>(charged_h),
        static_cast<long long>(tree_v),
        static_cast<long long>(tree_h),
        left_v > 0 ? 100.0 * double(charged_v) / double(left_v) : 0.0);
  }
  std::vector<lr::Global_net> nets;
  if (box_endpoints) {
    nets = lr::all_signal_requests(d, g, *boxes);
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& net = d.nets[n];
    if (box_endpoints || !net.routed || net.clock) {
      continue;
    }
    lr::Global_net request;
    request.net  = int(n);
    request.crit = net.crit;
    std::set<int> seen;
    const auto    add = [&](const lr::Pin& pin) {
      const int tile = lr::physical_pin_access(d, g, pin).tile;
      if (seen.insert(tile).second) {
        request.endpoints.push_back(tile);
      }
    };
    if (net.driver >= 0) {
      add(net.pins[size_t(net.driver)]);
    }
    for (const auto& pin : net.pins) {
      add(pin);
    }
    nets.push_back(std::move(request));
  }
  auto routes         = lr::global_route(g,
                                         cap_h,
                                         cap_v,
                                         lr::admission_share(options),
                                         0,
                                         nets,
                                         options.global_margin,
                                         options.global_rounds,
                                         net_pops,
                                         options.global_astar_weight,
                                         options.global_pattern);
  out.route_attempted = true;
  std::vector<size_t> use_h(g.size()), use_v(g.size());
  for (const auto& tree : routes.trees) {
    for (const auto& [a, b] : tree.edges) {
      ++(g.row(a) == g.row(b) ? use_h : use_v)[size_t(a)];
    }
  }
  if (congestion) {
    *congestion = lr::route_congestion(d, g, routes, nets, cap_h, cap_v);
  }
  const auto pressure = [&](int a, int b) {
    const bool  h     = g.row(a) == g.row(b);
    const auto& cap   = h ? cap_h : cap_v;
    const int   limit = std::max(1, int(std::floor(lr::admission_share(options) * std::min(cap[size_t(a)], cap[size_t(b)]))));
    return std::max(1.0, double((h ? use_h : use_v)[size_t(a)]) / limit);
  };
  std::vector<int> tree_nets;
  for (const auto& net : nets) {
    tree_nets.push_back(net.net);
  }
  std::vector<char> gate_hot;
  out.gates = lr::assess_legroute_gates(d, g, boxes ? &*boxes : nullptr, routes, tree_nets, cap_h, cap_v, false, &gate_hot);
  if (!measure_only && !out.gates.unrouted && !out.gates.routing_dense_edges && !out.gates.overflow_tracks) {
    if (options.partition_cells) {
      std::vector<size_t> cells(g.size());
      if (boxes) {
        for (const auto& box : boxes->boxes) {
          cells[size_t(box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy))] += box.cells.size();
        }
      } else {
        for (int c : d.movable) {
          const auto& pose   = fp.components[size_t(c)].place;
          const auto& master = *d.macro[size_t(c)];
          ++cells[size_t(g.tile_of(pose.x + master.width * fp.dbu / 2, pose.y + master.height * fp.dbu / 2))];
        }
      }
      std::vector<char> critical;
      for (const auto& request : nets) {
        critical.push_back(d.nets[size_t(request.net)].rank < options.partition_critical);
      }
      auto partition = lr::partition_routes(g,
                                            cells,
                                            routes,
                                            critical,
                                            cap_h,
                                            cap_v,
                                            lr::admission_share(options),
                                            options.partition_cells,
                                            options.partition_critical_cost,
                                            options.partition_mesh);
      g.region       = std::move(partition.region);
      g.regions      = partition.regions;
      if (boxes && lr::movable_route_partition(options)) {
        const auto cuts
            = lr::route_cuts(g, routes, critical, cap_h, cap_v, lr::admission_share(options), options.partition_critical_cost < 0);
        lr::repair_partition_fit(d, g, *boxes, cuts);
      }
    } else {
      g.region  = std::move(seed_regions);
      g.regions = seed_count;
    }
    out.gates = lr::assess_legroute_gates(d, g, boxes ? &*boxes : nullptr, routes, tree_nets, cap_h, cap_v, true, &gate_hot);
  }
  // A box-fit deficit alone is the seed's job (joint matching with Hall repair inside each region).
  const auto& gate          = out.gates;
  const bool  seed_eligible = !gate.requires_replan()
                              || (gate.unfittable_regions && !gate.oversized_regions && !gate.cell_dense_regions
                                  && !gate.routing_dense_edges && !gate.overflow_tracks && !gate.unrouted);
  if (options.box_route_seed && boxes && !measure_only && seed_eligible) {
    out.box_route_seed_attempted = true;
    auto seed                    = lr::prepare_box_route_seed(d, g, *boxes, cap_h, cap_v, "plan");
    if (seed.accepted) {
      out.box_route_seed_accepted = true;
      out.endpoint_basis          = "provisional_box_tiles";
      nets                        = lr::all_signal_requests(d, g, seed.boxes);
      auto native_trees           = std::move(seed.routes.trees);
      routes                      = std::move(seed.routes);
      routes.trees.clear();
      for (const auto& request : nets) {
        routes.trees.push_back(std::move(native_trees[size_t(request.net)]));
      }
      out.gates = lr::assess_legroute_gates(d, g, &seed.boxes, routes, tree_nets, cap_h, cap_v, true, &gate_hot);
      std::ranges::fill(use_h, 0);
      std::ranges::fill(use_v, 0);
      for (const auto& tree : routes.trees) {
        for (const auto& [a, b] : tree.edges) {
          ++(g.row(a) == g.row(b) ? use_h : use_v)[size_t(a)];
        }
      }
      if (congestion) {
        *congestion = lr::route_congestion(d, g, routes, nets, cap_h, cap_v);
      }
      // Planning's private endpoint view follows the accepted forest. The
      // physical frame is untouched; local reconstructs its original domains.
      boxes = std::move(seed.boxes);
    }
  }
  out.component_pressure.assign(fp.components.size(), 1);
  std::vector<double> tile_pressure(g.size(), 1);
  for (size_t n = 0; n < nets.size(); ++n) {
    if (!routes.trees[n].routed) {
      const auto& request = nets[n];
      const auto& net     = d.nets[size_t(request.net)];
      const auto& tree    = routes.trees[n];
      int         x0 = g.cols, x1 = -1, y0 = g.rows, y1 = -1;
      for (int tile : request.endpoints) {
        x0 = std::min(x0, g.col(tile));
        x1 = std::max(x1, g.col(tile));
        y0 = std::min(y0, g.row(tile));
        y1 = std::max(y1, g.row(tile));
      }
      out.failures.push_back({net.frame,
                              fp.nets[net.frame].name,
                              net.pins.size(),
                              request.endpoints.size(),
                              std::max(0, x1 - x0),
                              std::max(0, y1 - y0),
                              tree.search_limited,
                              tree.topology_disconnected,
                              tree.last_attempt_pops,
                              tree.search_pops,
                              tree.search_attempts});
    }
    // A missing route is an unresolved estimate. Bounded displacement/inflation
    // may change its endpoints; the caller must measure the resulting placement.
    double net_pressure = routes.trees[n].routed || routes.trees[n].search_limited ? 1 : 1.5;
    if (!routes.trees[n].routed && !routes.trees[n].search_limited) {
      for (int tile : nets[n].endpoints) {
        tile_pressure[size_t(tile)] = std::max(tile_pressure[size_t(tile)], net_pressure);
      }
    }
    for (const auto& [a, b] : routes.trees[n].edges) {
      const double ratio       = pressure(a, b);
      net_pressure             = std::max(net_pressure, ratio);
      tile_pressure[size_t(a)] = std::max(tile_pressure[size_t(a)], ratio);
      tile_pressure[size_t(b)] = std::max(tile_pressure[size_t(b)], ratio);
    }
    for (const auto& pin : d.nets[size_t(nets[n].net)].pins) {
      if (pin.comp >= 0 && d.cell[size_t(pin.comp)] >= 0) {
        auto& value = out.component_pressure[size_t(pin.comp)];
        value       = std::max(value, net_pressure);
      }
    }
  }
  // Hot transit tiles also push resident cells away, even when the offending
  // nets have no endpoints there. Endpoints above still receive feedback when
  // the hotspot itself has no movable cells.
  std::vector<unsigned> distance(g.size(), std::numeric_limits<unsigned>::max());
  std::vector<int>      queue;
  for (size_t t = 0; t < g.size(); ++t) {
    if (tile_pressure[t] > 1 || gate_hot[t]) {
      distance[t] = 0;
      queue.push_back(int(t));
    }
  }
  out.hot_tiles = queue.size();
  for (size_t k = 0; k < queue.size(); ++k) {
    const int t = queue[k];
    for (int q : {g.col(t) > 0 ? t - 1 : -1,
                  g.col(t) + 1 < g.cols ? t + 1 : -1,
                  g.row(t) > 0 ? t - g.cols : -1,
                  g.row(t) + 1 < g.rows ? t + g.cols : -1}) {
      if (q >= 0 && distance[size_t(q)] == std::numeric_limits<unsigned>::max()) {
        distance[size_t(q)] = distance[size_t(t)] + 1;
        queue.push_back(q);
      }
    }
  }
  out.tile_width_dbu  = g.tile_w;
  out.tile_height_dbu = g.tile_h;
  out.component_hot_distance.assign(fp.components.size(), std::numeric_limits<unsigned>::max());
  for (int c : d.movable) {
    const auto& component = fp.components[size_t(c)];
    const auto& macro     = *d.macro[size_t(c)];
    const int   tile      = g.tile_of(component.place.x + macro.width * fp.dbu / 2, component.place.y + macro.height * fp.dbu / 2);
    out.component_pressure[size_t(c)]     = std::max(out.component_pressure[size_t(c)], tile_pressure[size_t(tile)]);
    out.component_hot_distance[size_t(c)] = distance[size_t(tile)];
  }
  out.signals          = nets.size();
  out.timed_signals    = std::ranges::count_if(nets, [&](const auto& net) { return std::isfinite(d.nets[size_t(net.net)].slack); });
  out.unrouted         = routes.unrouted;
  out.overflow_edges   = routes.overflow_edges;
  out.overflow_tracks  = routes.overflow_tracks;
  out.search_pops      = routes.search_pops;
  out.search_limited   = routes.search_limited;
  out.pattern_attempts = routes.pattern_attempts;
  out.pattern_completed     = routes.pattern_completed;
  out.pattern_fallbacks     = routes.pattern_fallbacks;
  out.pattern_work          = routes.pattern_work;
  out.pattern_work_limited  = routes.pattern_work_limited;
  out.topology_disconnected = routes.topology_disconnected;
  out.global_astar_weight   = options.global_astar_weight;
  out.seconds               = lr::seconds_since(start);
  return out;
}

void write_legroute_plan_failures(const Legroute_plan_report& report, const std::string& path) {
  std::ofstream out(path);
  out << "net_index\tnet_name\tpin_count\tendpoint_tiles\tbbox_width_tiles\tbbox_height_tiles\tbbox_span_tiles"
         "\tsearch_limited\ttopology_disconnected\tlast_attempt_pops\tsearch_pops\tsearch_attempts\n";
  for (const auto& failure : report.failures) {
    // Backslash escapes keep even unusual DEF identifiers on one TSV record.
    std::string name;
    for (char c : failure.net_name) {
      switch (c) {
        case '\\': name += "\\\\"; break;
        case '\t': name += "\\t"; break;
        case '\n': name += "\\n"; break;
        case '\r': name += "\\r"; break;
        default  : name += c; break;
      }
    }
    out << failure.net_index << '\t' << name << '\t' << failure.pin_count << '\t' << failure.endpoint_tiles << '\t'
        << failure.bbox_width_tiles << '\t' << failure.bbox_height_tiles << '\t'
        << failure.bbox_width_tiles + failure.bbox_height_tiles << '\t' << failure.search_limited << '\t'
        << failure.topology_disconnected << '\t' << failure.last_attempt_pops << '\t' << failure.search_pops << '\t'
        << failure.search_attempts << '\n';
  }
  if (!out) {
    throw std::runtime_error("cannot write legroute planning failure report: " + path);
  }
}
}  // namespace livehd::placement
