// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute plan: one global route of a placement on the tile grid, and what it says about routability
// (todo/livehd/2r-route.html). The grid, the capacity, the clock charge, the router and the report; no gate decides
// here whether the placement is accepted: the caller ranks the rounds it measured (Legroute_plan_report::residual).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#include "core/json_util.hpp"
#include "legroute_internal.hpp"

namespace livehd::placement {
void validate_legroute_options(const Legroute_options& o) {
  const auto fail = [](const char* what) { throw std::invalid_argument(std::string("legroute: ") + what); };
  if (!std::isfinite(o.capacity_share) || !(o.capacity_share > 0) || o.capacity_share > 1) {
    fail("capacity_share must be in (0, 1]");
  }
  if (!std::isfinite(o.global_astar_weight) || o.global_astar_weight < 1) {
    fail("global_astar_weight must be finite and at least 1");
  }
  if (!std::isfinite(o.crit_window_ps) || !(o.crit_window_ps > 0)) {
    fail("crit_window_ps must be positive");
  }
  if (!o.rib_tiles) {
    fail("rib_tiles must be positive");
  }
}

Legroute_plan_report legroute_plan(const physical::Technology& tech, physical::Floorplan& fp, const timing::Feedback& timing,
                                   const Legroute_options& options, const Legroute_power& power,
                                   const physical::Tech_profile* profile, size_t net_pops, Legroute_congestion* congestion) {
  const auto start = lr::Clock::now();
  validate_legroute_options(options);
  auto                 d = lr::build_design(tech, fp, timing, options, profile);
  auto                 g = lr::build_grid(d, power);
  Legroute_plan_report out;
  if (congestion) {
    *congestion = {};
  }
  // PG geometry comes from the DEF's special nets, or from the generated grid the caller stored in `power` (its
  // shapes in power.obstacles, which build_grid deducts; ownership in power.power_nets)
  const auto has_pg = [](const std::vector<physical::Net>& nets) {
    return std::ranges::any_of(nets,
                               [](const auto& net) { return (net.use == "POWER" || net.use == "GROUND") && !net.wires.empty(); });
  };
  out.pg_missing = !has_pg(fp.special_nets) && power.power_nets.empty();  // generated nets are PG by construction
  if (out.pg_missing) {
    std::fprintf(stderr,
                 "legroute plan: no power grid in the floorplan or the generated grid; routing capacities carry no "
                 "power budget\n");
  }
  auto [cap_h, cap_v] = lr::transit_capacity(g);
  for (size_t l = 0; l < g.layers.size(); ++l) {
    size_t tracks = 0;
    for (const auto capacity : g.cap[l]) {
      tracks += size_t(capacity);
    }
    out.layers.push_back({g.layers[l].name, g.layers[l].horizontal, tracks});
  }
  // The clock budget: at least a flat reservation (a spine track per tile, a rib track every rib_tiles rows), and
  // the estimated spine tree where it takes more
  const auto clock = options.clock_charge ? lr::clock_tree_charge(d, g, options.rib_tiles)
                                          : lr::Clock_charge{std::vector<int>(g.size(), 0), std::vector<int>(g.size(), 0)};
  for (size_t t = 0; t < g.size(); ++t) {
    if (options.clock_charge) {
      cap_v[t] -= std::max(1, clock.v[t]);
      cap_h[t] -= std::max(int(g.row(int(t)) % std::max(1u, options.rib_tiles) == 0), clock.h[t]);
    }
    cap_h[t] = std::max(0, cap_h[t]);
    cap_v[t] = std::max(0, cap_v[t]);
  }
  if (options.clock_charge) {
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
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& net = d.nets[n];
    if (!net.routed || net.clock) {
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
  const auto routes = lr::global_route(g,
                                       cap_h,
                                       cap_v,
                                       options.capacity_share,
                                       nets,
                                       options.global_margin,
                                       options.global_rounds,
                                       net_pops,
                                       options.global_astar_weight,
                                       options.global_pattern);
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
    const int   limit = std::max(1, int(std::floor(options.capacity_share * std::min(cap[size_t(a)], cap[size_t(b)]))));
    return std::max(1.0, double((h ? use_h : use_v)[size_t(a)]) / limit);
  };
  const auto cell_tile = [&](int component) {
    const auto& pose  = fp.components[size_t(component)].place;
    const auto& macro = *d.macro[size_t(component)];
    return g.tile_of(pose.x + macro.width * fp.dbu / 2, pose.y + macro.height * fp.dbu / 2);
  };
  out.component_pressure.assign(fp.components.size(), 1);
  std::vector<double> tile_pressure(g.size(), 1);
  std::vector<char>   hot(g.size(), 0);
  for (size_t n = 0; n < nets.size(); ++n) {
    const auto& request = nets[n];
    const auto& net     = d.nets[size_t(request.net)];
    const auto& tree    = routes.trees[n];
    if (!tree.routed) {
      int x0 = g.cols, x1 = -1, y0 = g.rows, y1 = -1;
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
      // Every pin of an unresolved net marks its tile (a movable cell: the tile of its centre), also when the
      // search only ran out of budget.
      for (const auto& pin : net.pins) {
        const bool movable = pin.comp >= 0 && d.cell[size_t(pin.comp)] >= 0;
        hot[size_t(movable ? cell_tile(pin.comp) : lr::physical_pin_access(d, g, pin).tile)] = 1;
      }
    }
    // A missing route is an unresolved estimate. Bounded displacement/inflation
    // may change its endpoints; the caller must measure the resulting placement.
    double net_pressure = tree.routed || tree.search_limited ? 1 : 1.5;
    if (!tree.routed && !tree.search_limited) {
      for (int tile : request.endpoints) {
        tile_pressure[size_t(tile)] = std::max(tile_pressure[size_t(tile)], net_pressure);
      }
    }
    for (const auto& [a, b] : tree.edges) {
      const double ratio       = pressure(a, b);
      net_pressure             = std::max(net_pressure, ratio);
      tile_pressure[size_t(a)] = std::max(tile_pressure[size_t(a)], ratio);
      tile_pressure[size_t(b)] = std::max(tile_pressure[size_t(b)], ratio);
    }
    for (const auto& pin : net.pins) {
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
    if (tile_pressure[t] > 1 || hot[t]) {
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
  out.cols            = g.cols;
  out.rows            = g.rows;
  out.component_hot_distance.assign(fp.components.size(), std::numeric_limits<unsigned>::max());
  for (int c : d.movable) {
    const int tile                        = cell_tile(c);
    out.component_pressure[size_t(c)]     = std::max(out.component_pressure[size_t(c)], tile_pressure[size_t(tile)]);
    out.component_hot_distance[size_t(c)] = distance[size_t(tile)];
  }
  out.signals               = nets.size();
  out.timed_signals         = size_t(std::ranges::count_if(nets, [&](const auto& net) {
    return std::isfinite(d.nets[size_t(net.net)].slack);
  }));
  out.unrouted              = routes.unrouted;
  out.overflow_edges        = routes.overflow_edges;
  out.overflow_tracks       = routes.overflow_tracks;
  out.overflow_edges_h      = routes.overflow_edges_h;
  out.overflow_edges_v      = routes.overflow_edges_v;
  out.overflow_tracks_h     = routes.overflow_tracks_h;
  out.overflow_tracks_v     = routes.overflow_tracks_v;
  out.search_pops           = routes.search_pops;
  out.search_limited        = routes.search_limited;
  out.pattern_attempts      = routes.pattern_attempts;
  out.pattern_completed     = routes.pattern_completed;
  out.pattern_fallbacks     = routes.pattern_fallbacks;
  out.pattern_work          = routes.pattern_work;
  out.pattern_work_limited  = routes.pattern_work_limited;
  out.topology_disconnected = routes.topology_disconnected;
  out.seconds               = lr::seconds_since(start);
  return out;
}

std::string legroute_plan_json(const Legroute_plan_report& r) {
  std::ostringstream out;
  out << std::boolalpha << "{\"pg_missing\":" << r.pg_missing << ",\"signals\":" << r.signals
      << ",\"timed_signals\":" << r.timed_signals << ",\"unrouted\":" << r.unrouted << ",\"search_limited\":" << r.search_limited
      << ",\"topology_disconnected\":" << r.topology_disconnected << ",\"overflow_edges\":" << r.overflow_edges
      << ",\"overflow_tracks\":" << r.overflow_tracks << ",\"overflow\":{\"basis\":\"2d_router_demand_per_direction\""
      << ",\"horizontal\":{\"edges\":" << r.overflow_edges_h << ",\"tracks\":" << r.overflow_tracks_h
      << "},\"vertical\":{\"edges\":" << r.overflow_edges_v << ",\"tracks\":" << r.overflow_tracks_v << "}},\"residual\":"
      << r.residual() << ",\"grid\":{\"cols\":" << r.cols << ",\"rows\":" << r.rows << ",\"tile_width_dbu\":" << r.tile_width_dbu
      << ",\"tile_height_dbu\":" << r.tile_height_dbu << "},\"layers\":[";
  for (size_t i = 0; i < r.layers.size(); ++i) {
    const auto& layer = r.layers[i];
    out << (i ? "," : "") << "{\"name\":\"" << json_util::escape(layer.name) << "\",\"direction\":\""
        << (layer.horizontal ? "horizontal" : "vertical") << "\",\"capacity_tracks\":" << layer.capacity_tracks << '}';
  }
  out << "],\"search_pops\":" << r.search_pops << ",\"pattern_attempts\":" << r.pattern_attempts
      << ",\"pattern_completed\":" << r.pattern_completed << ",\"pattern_fallbacks\":" << r.pattern_fallbacks
      << ",\"pattern_work\":" << r.pattern_work << ",\"pattern_work_limited\":" << r.pattern_work_limited
      << ",\"hot_tiles\":" << r.hot_tiles << ",\"seconds\":" << r.seconds << '}';
  return out.str();
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
