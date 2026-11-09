// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <format>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>

#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
void require(bool value, const char* reason) {
  if (!value) {
    throw std::invalid_argument(std::string("critical cellbox handoff: ") + reason);
  }
}
bool same_frame(const physical::Floorplan& a, const physical::Floorplan& b) {
  return a.design == b.design && a.dbu == b.dbu && a.die == b.die && a.rows == b.rows && a.tracks == b.tracks
         && a.components == b.components && a.terminals == b.terminals && a.nets == b.nets && a.blockages == b.blockages
         && a.vias == b.vias && a.special_nets == b.special_nets && a.gcell_grid == b.gcell_grid
         && a.nondefault_rules == b.nondefault_rules && a.snapshot_blockages == b.snapshot_blockages;
}
auto policy(const Legroute_options& o) {
  return std::tie(o.critical_cellboxes,
                  o.cellbox_sites,
                  o.cellbox_rows,
                  o.cellbox_external_pins,
                  o.cellbox_connections,
                  o.cellbox_critical_choices,
                  o.cellbox_other_choices,
                  o.cellbox_wide_exceptions,
                  o.cellbox_ignored_nets,
                  o.box_reserve,
                  o.cellbox_reserve_tracks,
                  o.cellbox_plan_only,
                  o.cellbox_tile_capacity,
                  o.cellbox_preroute_tiles,
                  o.cellbox_preroute_max_pins,
                  o.cellbox_fixed_regions,
                  o.cellbox_fixed_min_regions,
                  o.reserved_tracks,
                  o.reserved_tracks_per_um,
                  o.global_share,
                  o.density_ceiling,
                  o.cap_scale);
}
bool same_resources(const Track_resource_catalog& a, const Track_resource_catalog& b) {
  if (a.layers.size() != b.layers.size() || a.anonymous_clock_h != b.anonymous_clock_h
      || a.anonymous_clock_v != b.anonymous_clock_v) {
    return false;
  }
  for (size_t l = 0; l < a.layers.size(); ++l) {
    const auto& x = a.layers[l];
    const auto& y = b.layers[l];
    if (x.name != y.name || x.horizontal != y.horizontal || x.grid_layer != y.grid_layer || x.bands != y.bands
        || x.tiles.size() != y.tiles.size()) {
      return false;
    }
    for (size_t t = 0; t < x.tiles.size(); ++t) {
      const auto& u = x.tiles[t];
      const auto& v = y.tiles[t];
      if (std::tie(u.band, u.whole_band_exclusions, u.anonymous_headroom, u.cellbox_budget, u.coarse_capacity)
              != std::tie(v.band, v.whole_band_exclusions, v.anonymous_headroom, v.cellbox_budget, v.coarse_capacity)
          || u.exclusions.size() != v.exclusions.size()) {
        return false;
      }
      for (size_t k = 0; k < u.exclusions.size(); ++k) {
        if (u.exclusions[k].track != v.exclusions[k].track || u.exclusions[k].reasons != v.exclusions[k].reasons) {
          return false;
        }
      }
    }
  }
  return true;
}
std::vector<int64_t> available(const Grid& g, int tile, std::string_view name) {
  const auto it = std::ranges::find(g.resources.layers, name, &Track_resource_layer::name);
  require(it != g.resources.layers.end(), "missing resource layer");
  const auto& t = it->tiles.at(size_t(tile));
  if (t.whole_band_exclusions) {
    return {};
  }
  std::set<size_t> excluded;
  for (const auto& e : t.exclusions) {
    if (e.reasons) {
      excluded.insert(e.track);
    }
  }
  std::vector<int64_t> out;
  const auto&          band = it->bands.at(t.band);
  for (size_t k = 0; k < band.size(); ++k) {
    if (!excluded.contains(k)) {
      out.push_back(band[k]);
    }
  }
  if (out.size() < t.anonymous_headroom) {
    return {};
  }
  out.erase(out.begin(), out.begin() + t.anonymous_headroom);
  return out;
}
void check_slots(const Design& d, const Grid& g, const Cellbox_admission& a, bool installed) {
  require(a.unassigned.empty() && a.slots.size() == a.boxes.boxes.size(), "incomplete admission");
  std::vector<int> cell_owner(d.movable.size(), -1), tile_owner(g.size(), -1);
  for (size_t b = 0; b < a.slots.size(); ++b) {
    const auto& slot = a.slots[b];
    require(slot.tile >= 0 && size_t(slot.tile) < g.size() && (slot.span == 1 || slot.span == 2)
                && g.col(slot.tile) + slot.span <= g.cols,
            "invalid box footprint");
    require(slot.span == 1 || (d.opt.cellbox_wide_exceptions && slot.cells.size() == 1), "invalid wide exception");
    require(slot.cells == a.boxes.boxes[b].cells && slot.cells.size() == slot.poses.size(), "membership/pose mismatch");
    std::vector<int64_t> hs, vs;
    for (int k = 0; k < slot.span; ++k) {
      const int t = slot.tile + k;
      require(!g.blocked.at(size_t(t)) && tile_owner[size_t(t)] < 0, "overlapping or blocked box footprint");
      tile_owner[size_t(t)] = int(b);
      auto h = available(g, t, "M2"), v = available(g, t, "M3");
      if (!k) {
        hs = std::move(h);
        vs = std::move(v);
      } else {
        std::vector<int64_t> h2, v2;
        std::ranges::set_intersection(hs, h, std::back_inserter(h2));
        std::ranges::set_union(vs, v, std::back_inserter(v2));
        hs = std::move(h2);
        vs = std::move(v2);
      }
    }
    for (const auto* list : {&slot.reserved_h, &slot.reserved_v}) {
      require(list->size() == cellbox_reserve(d.opt) && std::is_sorted(list->begin(), list->end())
                  && std::adjacent_find(list->begin(), list->end()) == list->end(),
              "reservation is not cellbox_reserve_tracks distinct coordinates");
    }
    require(std::ranges::includes(hs, slot.reserved_h) && std::ranges::includes(vs, slot.reserved_v),
            "reserved coordinate unavailable");
    std::map<int, unsigned>       counts;
    std::set<std::pair<int, int>> used_sites;
    std::set<int>                 posed;
    for (const auto& p : slot.poses) {
      require(p.movable >= 0 && size_t(p.movable) < d.movable.size() && std::ranges::find(slot.cells, p.movable) != slot.cells.end()
                  && posed.insert(p.movable).second,
              "duplicate or foreign pose");
      require(cell_owner[size_t(p.movable)] < 0, "duplicate movable cell");
      cell_owner[size_t(p.movable)] = int(b);
      const auto  c                 = size_t(d.movable[size_t(p.movable)]);
      const auto& macro             = *d.macro[c];
      const int   width             = cell_sites(d, g, p.movable);
      require(std::llround(macro.height * d.fp.dbu) == g.row_h && p.row >= 0 && unsigned(p.row) < g.rows_per_tile && p.site >= 0
                  && p.site + width <= slot.span * g.sites,
              "invalid native row fit");
      require(p.x == g.left(g.col(slot.tile)) + p.site * g.site && p.y == g.bottom(g.row(slot.tile)) + p.row * g.row_h,
              "pose differs from row/site witness");
      require(p.orientation == g.row_orient.at(size_t(g.row(slot.tile) * g.rows_per_tile + p.row))
                  && physical::orientation_allowed(macro, p.orientation),
              "invalid row orientation");
      for (int s = p.site; s < p.site + width; ++s) {
        const int tile = slot.tile + s / g.sites, local = s % g.sites;
        require(
            std::ranges::any_of(g.segs(tile), [&](const Seg& seg) { return seg.row == p.row && seg.lo <= local && local < seg.hi; })
                && used_sites.emplace(p.row, s).second,
            "occupied or unavailable native site");
      }
      if (installed) {
        const auto& pose = d.fp.components[c].place;
        require(pose.x == p.x && pose.y == p.y && pose.orientation == p.orientation && pose.status == physical::Status::PLACED,
                "installed pose changed");
      }
      for (int n : d.nets_of[size_t(p.movable)]) {
        const auto& net = d.nets[size_t(n)];
        if (net.power || net.control_scope) {
          continue;
        }
        for (const auto& pin : net.pins) {
          counts[n] += pin.comp == int(c);
        }
      }
    }
    unsigned external = 0, connections = 0;
    for (const auto& [n, k] : counts) {
      if (!k) {
        continue;
      }
      connections += k - 1;
      if (k < d.nets[size_t(n)].pins.size()) {
        external += k;
      }
    }
    require(external == slot.external_pins && connections == slot.connections && external <= d.opt.cellbox_external_pins
                && connections <= d.opt.cellbox_connections,
            "physical P/C policy changed");
  }
  require(std::ranges::none_of(cell_owner, [](int b) { return b < 0; }), "missing movable cell");
  require(cell_owner == a.boxes.box_of, "stale cell-to-box map");
}
}  // namespace
void install_critical_handoff(const Design& d, Grid& g, Boxes& boxes) {
  require(d.opt.critical_cellboxes && d.opt.cellbox_full_flow && bool(d.opt.cellbox_handoff),
          "full flow requires a selected Plan snapshot");
  const auto& h = *d.opt.cellbox_handoff;
  require(same_frame(d.fp, h.source_frame), "source frame differs from selected Plan");
  require(policy(d.opt) == policy(h.options), "source policy or control scope changed");
  require(g.rows_per_tile == h.grid.rows_per_tile && g.rows_per_tile == 4 && g.x0 == h.grid.x0 && g.y0 == h.grid.y0
              && g.tile_w == h.grid.tile_w && g.tile_h == h.grid.tile_h && g.cols == h.grid.cols && g.rows == h.grid.rows
              && g.site == h.grid.site && g.row_h == h.grid.row_h && g.row_orient == h.grid.row_orient
              && g.row_present == h.grid.row_present && g.blocked == h.grid.blocked && g.cap == h.grid.cap
              && g.box_cap == h.grid.box_cap && g.obstacles == h.grid.obstacles && g.keys == h.grid.keys && g.key == h.grid.key
              && g.placeable_sites == h.grid.placeable_sites && same_resources(g.resources, h.grid.resources),
          "native grid changed");
  require(g.layers.size() == h.grid.layers.size(), "derived routing stack changed");
  for (size_t l = 0; l < g.layers.size(); ++l) {
    const auto& a = g.layers[l];
    const auto& b = h.grid.layers[l];
    require(std::tie(a.name, a.cut_below, a.horizontal, a.pitch, a.offset, a.width, a.r_um, a.c_um)
                == std::tie(b.name, b.cut_below, b.horizontal, b.pitch, b.offset, b.width, b.r_um, b.c_um),
            "derived routing lattice changed");
  }
  require(!h.routes.unrouted && !h.routes.overflow_edges && !h.routes.overflow_tracks && h.requests.size() == h.routes.trees.size(),
          "incomplete or overflowing forest");
  check_slots(d, g, h.admission, false);
  // Build all derived state on copies. No caller-visible mutation until every check passes.
  auto next_grid  = h.grid;
  auto next_boxes = h.admission.boxes;
  next_grid.critical_box_of_tile.assign(g.size(), -1);
  next_grid.critical_reserved_h.assign(g.size(), {});
  next_grid.critical_reserved_v.assign(g.size(), {});
  for (size_t b = 0; b < h.admission.slots.size(); ++b) {
    const auto& slot   = h.admission.slots[b];
    auto&       box    = next_boxes.boxes[b];
    box.critical_fixed = box.fixed = box.placed = true;
    box.span_columns                            = slot.span;
    box.tile                                    = slot.tile;
    box.reserved_h                              = slot.reserved_h;
    box.reserved_v                              = slot.reserved_v;
    for (int k = 0; k < slot.span; ++k) {
      const auto t                      = size_t(slot.tile + k);
      next_grid.critical_box_of_tile[t] = int(b);
      next_grid.critical_reserved_h[t]  = slot.reserved_h;
      for (auto x : slot.reserved_v) {
        if (x >= g.left(g.col(int(t))) && x < g.left(g.col(int(t)) + 1)) {
          next_grid.critical_reserved_v[t].push_back(x);
        }
      }
    }
  }
  auto frame = d.fp;
  for (const auto& slot : h.admission.slots) {
    for (const auto& p : slot.poses) {
      auto& pose       = frame.components.at(size_t(d.movable.at(size_t(p.movable)))).place;
      pose.x           = p.x;
      pose.y           = p.y;
      pose.orientation = p.orientation;
      pose.status      = physical::Status::PLACED;
    }
  }
  // The forest is checked against the still-unmodified selected endpoint view.
  auto requests = all_signal_requests(d, g, h.admission.boxes);
  std::erase_if(requests, [&](const auto& r) {
    const auto& n = d.nets.at(size_t(r.net));
    return n.power || n.control_scope;
  });
  require(requests.size() == h.requests.size(), "request inventory changed");
  for (size_t i = 0; i < requests.size(); ++i) {
    require(requests[i].net == h.requests[i].net && requests[i].endpoints == h.requests[i].endpoints && h.routes.trees[i].routed,
            "request endpoints or accepted forest changed");
    std::map<int, std::vector<int>> graph;
    std::set<std::pair<int, int>>   seen;
    for (auto [a, b] : h.routes.trees[i].edges) {
      require(a >= 0 && b >= 0 && size_t(a) < g.size() && size_t(b) < g.size()
                  && std::abs(g.col(a) - g.col(b)) + std::abs(g.row(a) - g.row(b)) == 1 && seen.insert(std::minmax(a, b)).second,
              "invalid forest edge");
      graph[a].push_back(b);
      graph[b].push_back(a);
    }
    if (!requests[i].endpoints.empty()) {
      std::set<int>    reached;
      std::vector<int> pending{requests[i].endpoints.front()};
      while (!pending.empty()) {
        int t = pending.back();
        pending.pop_back();
        if (!reached.insert(t).second) {
          continue;
        }
        for (int q : graph[t]) {
          pending.push_back(q);
        }
      }
      for (int t : requests[i].endpoints) {
        require(reached.contains(t), "disconnected accepted forest");
      }
      for (const auto& [t, adj] : graph) {
        (void)adj;
        require(reached.contains(t), "foreign disconnected forest piece");
      }
    }
  }
  auto [cap_h, cap_v] = transit_capacity(g);
  std::map<std::pair<int, int>, size_t> use;
  for (const auto& tree : h.routes.trees) {
    for (auto [a, b] : tree.edges) {
      ++use[std::minmax(a, b)];
    }
  }
  for (const auto& [edge, n] : use) {
    const auto [a, b] = edge;
    const auto& caps  = g.row(a) == g.row(b) ? cap_h : cap_v;
    require(n <= size_t(std::max(0, int(std::floor(admission_share(d.opt) * std::min(caps[size_t(a)], caps[size_t(b)]))))),
            "forest exceeds declared capacity");
  }
  d.fp.components = std::move(frame.components);
  g               = std::move(next_grid);
  boxes           = std::move(next_boxes);
}

void validate_critical_boxes(const Design& d, const Grid& g, const Boxes& boxes) {
  require(bool(d.opt.cellbox_handoff), "missing selected snapshot");
  const auto& a = d.opt.cellbox_handoff->admission;
  check_slots(d, g, a, true);
  require(boxes.box_of == a.boxes.box_of && boxes.boxes.size() == a.slots.size(), "installed membership changed");
  std::vector<int>                  owner(g.size(), -1);
  std::vector<std::vector<int64_t>> hs(g.size()), vs(g.size());
  for (size_t b = 0; b < a.slots.size(); ++b) {
    const auto& slot = a.slots[b];
    for (int k = 0; k < slot.span; ++k) {
      const auto t = size_t(slot.tile + k);
      owner[t]     = int(b);
      hs[t]        = slot.reserved_h;
      for (auto x : slot.reserved_v) {
        if (x >= g.left(g.col(int(t))) && x < g.left(g.col(int(t)) + 1)) {
          vs[t].push_back(x);
        }
      }
    }
  }
  require(owner == g.critical_box_of_tile && hs == g.critical_reserved_h && vs == g.critical_reserved_v,
          "global/private resource ownership changed");
  for (size_t b = 0; b < boxes.boxes.size(); ++b) {
    const auto& box  = boxes.boxes[b];
    const auto& slot = a.slots[b];
    require(box.critical_fixed && box.fixed && box.placed && box.tile == slot.tile && box.span_columns == slot.span
                && box.cells == slot.cells && box.reserved_h == slot.reserved_h && box.reserved_v == slot.reserved_v,
            "installed box contract changed");
    for (int k = 0; k < slot.span; ++k) {
      require(g.critical_box_of_tile.at(size_t(slot.tile + k)) == int(b), "wide footprint ownership changed");
    }
  }
}

void critical_local_routes(const Design& d, const Grid& g, const Boxes& boxes, Routing& r, Legroute_report& rep,
                           const Local_forest* forest, const std::vector<char>* only) {
  validate_critical_boxes(d, g, boxes);
  const auto& h = *d.opt.cellbox_handoff;
  r             = {};
  r.trees.resize(d.nets.size());
  r.local.assign(d.nets.size(), 0);
  auto [ch, cv] = transit_capacity(g);
  r.cap_h.assign(ch.begin(), ch.end());
  r.cap_v.assign(cv.begin(), cv.end());
  r.dem_h.assign(g.size(), 0);
  r.dem_v.assign(g.size(), 0);
  r.hist_h.assign(g.size(), 0);
  r.hist_v.assign(g.size(), 0);
  r.layer_dem.assign(g.layers.size(), std::vector<int32_t>(g.size(), 0));
  r.box_dem_h.assign(g.size(), 0);
  r.box_dem_v.assign(g.size(), 0);
  std::vector<int> request_of(d.nets.size(), -1);
  for (size_t q = 0; q < h.requests.size(); ++q) {
    request_of.at(size_t(h.requests[q].net)) = int(q);
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& net = d.nets[n];
    if (net.power || net.control_scope || net.pins.empty() || (only && !(*only)[n])) {
      continue;
    }
    std::map<int, int> node_at;
    auto               node = [&](int tile) {
      auto [it, fresh] = node_at.try_emplace(tile, int(r.trees[n].nodes.size()));
      if (fresh) {
        r.trees[n].nodes.push_back({});
        r.trees[n].nodes.back().tile = tile;
      }
      return it->second;
    };
    std::set<int> protected_tiles;
    for (size_t k = 0; k < net.pins.size(); ++k) {
      const auto& p = net.pins[k];
      const int   m = p.comp >= 0 ? d.cell.at(size_t(p.comp)) : -1;
      const int   b = m >= 0 ? boxes.box_of.at(size_t(m)) : -1;
      const int   t = b >= 0 ? boxes.boxes.at(size_t(b)).tile : physical_pin_access(d, g, p).tile;
      require(t >= 0 && size_t(t) < g.size(), "physical attachment has no tile");
      auto& nd = r.trees[n].nodes[size_t(node(t))];
      nd.pins.push_back(int(k));
      if (b >= 0) {
        nd.box = b;
      } else if (p.term >= 0) {
        nd.term = p.term;
      }
      if (b < 0) {
        protected_tiles.insert(t);  // IO and fixed pins retain their explicit global attachment.
      }
    }
    const int  q          = request_of[n];
    const auto plan_edges = [&]() -> const std::vector<std::pair<int, int>>& {
      return forest ? forest->edges.at(size_t(q)) : h.routes.trees[size_t(q)].edges;
    };
    if (q < 0 || plan_edges().empty()) {
      // Only real ordinary endpoints wholly inside one admitted box use local routing.
      // A source terminal in that same coarse tile remains external and needs an IO carrier.
      int  internal_box = -1;
      bool internal     = true;
      for (const auto& pin : net.pins) {
        const int cell  = pin.comp >= 0 ? d.cell.at(size_t(pin.comp)) : -1;
        const int box   = cell >= 0 ? boxes.box_of.at(size_t(cell)) : -1;
        internal       &= box >= 0 && (internal_box < 0 || internal_box == box);
        internal_box    = box;
      }
      r.local[n] = internal;
      continue;
    }
    std::map<int, std::vector<int>> adjacency;
    std::set<std::pair<int, int>>   remaining;
    for (auto [a, b] : plan_edges()) {
      adjacency[a].push_back(b);
      adjacency[b].push_back(a);
      remaining.insert(std::minmax(a, b));
      ++(g.row(a) == g.row(b) ? r.dem_h : r.dem_v)[size_t(std::min(a, b))];
    }
    if (forest && n < forest->splits.size()) {
      for (int t : forest->splits[n]) {
        if (adjacency.contains(t)) {
          protected_tiles.insert(t);  // a split point delimits two separately tracked pieces of the run
        }
      }
    }
    for (const auto& [t, neighbours] : adjacency) {
      const bool collinear = neighbours.size() == 2
                             && (g.col(neighbours[0]) == g.col(neighbours[1]) || g.row(neighbours[0]) == g.row(neighbours[1]));
      if (!protected_tiles.contains(t) && (neighbours.size() == 1 || collinear)) {
        const auto existing = node_at.find(t);
        if (existing != node_at.end() && r.trees[n].nodes[size_t(existing->second)].box >= 0) {
          // Preserve every physical pin and its box identity, but do not introduce a global detour just to tap it.
          r.trees[n].nodes[size_t(existing->second)].direct_carrier_tap = true;
        }
      }
      // Every box node with only box pins (endpoint, bend or branch too) is a direct carrier tap: Local metal stops at
      // the box edge and Final extends the carriers into the box and joins them there (no Local riser or jog inside
      // the box, whose 20H/20V tracks belong to Final).
      if (const auto existing = node_at.find(t); existing != node_at.end()) {
        auto& nd = r.trees[n].nodes[size_t(existing->second)];
        if (nd.box >= 0 && nd.term < 0 && std::ranges::all_of(nd.pins, [&](int k) {
              const auto& p = net.pins[size_t(k)];
              return p.comp >= 0 && d.cell.at(size_t(p.comp)) >= 0;
            })) {
          nd.direct_carrier_tap = true;
        }
      }
      if (!collinear) {
        protected_tiles.insert(t);  // actual endpoints, bends and branches still delimit the same edge union
      }
    }
    while (!remaining.empty()) {
      auto edge = *remaining.begin();
      int  a = edge.first, b = edge.second;
      if (!protected_tiles.contains(a) && protected_tiles.contains(b)) {
        std::swap(a, b);
      }
      // If this is the middle of a run, walk backward to its protected endpoint.
      if (!protected_tiles.contains(a)) {
        int prev = b, t = a;
        while (!protected_tiles.contains(t)) {
          const auto& adj  = adjacency.at(t);
          int         next = adj[0] == prev ? adj[1] : adj[0];
          prev             = t;
          t                = next;
        }
        a = t;
        b = prev;
      }
      int prev = a, t = b;
      remaining.erase(std::minmax(prev, t));
      while (!protected_tiles.contains(t)) {
        const auto& adj  = adjacency.at(t);
        const int   next = adj[0] == prev ? adj[1] : adj[0];
        prev             = t;
        t                = next;
        require(remaining.erase(std::minmax(prev, t)) == 1, "forest run is not a simple path");
      }
      Conn c;
      c.net      = int(n);
      c.a        = node(a);
      c.b        = node(t);
      c.region   = g.region.at(size_t(a));
      c.weight   = net.weight;
      c.priority = forest && n < forest->priority.size() ? forest->priority[n] : 0;
      c.critical = net.critical;
      c.poly     = {
          {g.col(a), g.row(a)},
          {g.col(t), g.row(t)}
      };
      r.trees[n].conns.push_back(int(r.conns.size()));
      r.conns.push_back(std::move(c));
      rep.critical_connections += net.critical;
    }
  }
  rep.frozen_boxes = boxes.boxes.size();
}
std::vector<std::string> local_backbone_rejections(const Design& d, const Grid& g, const Boxes& boxes, const Routing& r,
                                                   const Final_planned_geometry& geometry) {
  const auto               scope = final_net_scope(d);
  std::vector<std::string> rejected(d.nets.size());
  const auto               reject = [&](size_t n, const std::string& reason) {
    if (scope.required[n] && rejected[n].empty()) {
      rejected[n] = reason;
    }
  };
  const auto checked
      = [](uint8_t state) { return state == uint8_t(Track_state::checked) || state == uint8_t(Track_state::prescribed); };
  std::vector<size_t> segment_count(d.nets.size());
  for (size_t k = 0; k < r.segments.size(); ++k) {
    const auto& s = r.segments[k];
    if (s.net < 0 || size_t(s.net) >= d.nets.size()) {
      throw std::invalid_argument("Local segment has no source net owner");
    }
    const auto n = size_t(s.net);
    ++segment_count[n];
    if (!scope.required[n]) {
      continue;
    }
    if (s.layer < 0 || size_t(s.layer) >= g.layers.size() || s.track < 0 || !checked(s.track_state)) {
      reject(n, "Local main segment " + std::to_string(k) + " has no checked layer/track");
    }
    for (int end = 0; end != 2; ++end) {
      const int node_id = end ? s.node_hi : s.node_lo;
      if (node_id < 0) {
        continue;
      }
      if (n >= r.trees.size() || size_t(node_id) >= r.trees[n].nodes.size()) {
        reject(n, "Local segment references a missing attachment node");
        continue;
      }
      const auto& node = r.trees[n].nodes[size_t(node_id)];
      if (!node.direct_carrier_tap
          && (node.riser_layer < 0 || size_t(node.riser_layer) >= g.layers.size() || node.riser_track < 0
              || !checked(node.riser_state))) {
        reject(n, "Local attachment has no checked riser");
      }
      if (!node.direct_carrier_tap && s.layer >= 0 && size_t(s.layer) < g.layers.size() && !g.layers[size_t(s.layer)].horizontal
          && node.riser_track >= 0 && (node.riser_layer != s.layer || node.riser_track != s.track)) {
        const auto jog   = end ? s.jog_hi : s.jog_lo;
        const auto state = end ? s.jog_hi_state : s.jog_lo_state;
        if (jog < 0 || !checked(state)) {
          reject(n, "Local used jog has no checked track");
        }
      }
    }
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (!scope.required[n]) {
      continue;
    }
    if (n >= r.local.size() || n >= r.trees.size() || n >= geometry.paths.size() || n >= geometry.path_state.size()
        || n >= geometry.joins.size() || n >= geometry.io_stub_failure.size()) {
      reject(n, "Local per-net state is missing");
      continue;
    }
    // The no-backbone exception requires real movable pins in exactly one admitted box, not merely local[n].
    int  internal_box = -1;
    bool internal     = !d.nets[n].pins.empty();
    for (const auto& pin : d.nets[n].pins) {
      const int cell = pin.comp >= 0 && size_t(pin.comp) < d.cell.size() ? d.cell[size_t(pin.comp)] : -1;
      const int box  = cell >= 0 && size_t(cell) < boxes.box_of.size() ? boxes.box_of[size_t(cell)] : -1;
      if (box < 0 || size_t(box) >= boxes.boxes.size() || (internal_box >= 0 && box != internal_box)) {
        internal = false;
      }
      internal_box = box;
    }
    const bool no_backbone  = internal && r.local[n];
    bool       same_tile_io = false;
    if (!internal && !r.local[n] && !segment_count[n] && r.trees[n].nodes.size() == 1 && r.trees[n].conns.empty()) {
      const auto& node     = r.trees[n].nodes.front();
      bool        has_cell = false, has_terminal = false;
      same_tile_io = node.box >= 0 && size_t(node.box) < boxes.boxes.size() && !node.direct_carrier_tap && node.riser_layer >= 0
                     && size_t(node.riser_layer) < g.layers.size() && node.stub_layer >= 0
                     && size_t(node.stub_layer) < g.layers.size() && node.riser_track >= 0 && node.stub_track >= 0
                     && checked(node.riser_state) && checked(node.stub_state) && checked(node.stub_access_state);
      for (const auto& pin : d.nets[n].pins) {
        if (pin.term >= 0) {
          has_terminal = true;  // Every terminal's native access and provenance are checked below.
        } else {
          const int cell  = pin.comp >= 0 && size_t(pin.comp) < d.cell.size() ? d.cell[size_t(pin.comp)] : -1;
          const int box   = cell >= 0 && size_t(cell) < boxes.box_of.size() ? boxes.box_of[size_t(cell)] : -1;
          same_tile_io   &= box >= 0 && box == node.box;
          has_cell        = true;
        }
      }
      same_tile_io &= has_cell && has_terminal;
    }
    if (!no_backbone && ((!segment_count[n] && !same_tile_io) || geometry.paths[n].empty())) {
      reject(n, "Local external net has no complete backbone geometry");
    }
    if (!geometry.io_stub_failure[n].empty()) {
      reject(n, geometry.io_stub_failure[n]);
    }
    if (geometry.path_state[n].size() != geometry.paths[n].size()) {
      reject(n, "Local emitted path provenance is incomplete");
    }
    for (auto state : geometry.path_state[n]) {
      if (!checked(state)) {
        reject(n, "Local emitted path is unchecked or fallback geometry");
      }
    }
    if (no_backbone) {
      continue;
    }
    std::vector<size_t> seen(d.nets[n].pins.size());
    for (const auto& node : r.trees[n].nodes) {
      for (int pin_id : node.pins) {
        if (pin_id < 0 || size_t(pin_id) >= seen.size()) {
          reject(n, "Local attachment references an unknown source pin");
          continue;
        }
        ++seen[size_t(pin_id)];
        const auto& pin = d.nets[n].pins[size_t(pin_id)];
        if (pin.term >= 0) {
          if (size_t(pin.term) >= d.fp.terminals.size() || !planned_terminal_access(d, g, d.fp.terminals[size_t(pin.term)])
              || node.riser_track < 0 || !checked(node.riser_state) || node.stub_track < 0 || !checked(node.stub_state)
              || !checked(node.stub_access_state)) {
            reject(n, "Local die IO attachment has no checked access/stub geometry");
          }
        }
      }
    }
    if (std::ranges::any_of(seen, [](auto count) { return count != 1; })) {
      reject(n, "Local backbone attachments do not preserve every source pin exactly once");
    }
    for (int connection : r.trees[n].conns) {
      if (connection < 0 || size_t(connection) >= r.conns.size()) {
        reject(n, "Local tree references a missing connection");
        continue;
      }
      const auto& c = r.conns[size_t(connection)];
      if (c.net != int(n) || (c.poly.size() >= 2 && c.segs.empty())) {
        reject(n, "Local connection has no main segment inventory");
      }
      for (int segment : c.segs) {
        if (segment < 0 || size_t(segment) >= r.segments.size() || r.segments[size_t(segment)].net != int(n)) {
          reject(n, "Local connection references a missing or foreign main segment");
        }
      }
    }
  }
  return rejected;
}

size_t critical_backbone_conflicts(const Design& d, const Grid& g, const Final_planned_geometry& geometry,
                                   std::vector<std::string>* rejected) {
  if (rejected) {
    rejected->assign(d.nets.size(), {});
  }
  size_t conflicts = 0;
  for (size_t n = 0; n < geometry.paths.size(); ++n) {
    const auto& paths  = geometry.paths[n];
    const auto  reject = [&] {
      ++conflicts;
      if (rejected) {
        rejected->at(n) = "Local backbone violates exact private reserved-coordinate ownership";
      }
    };
    for (const auto& path : paths) {
      if (path.layer != "M2" && path.layer != "M3") {
        continue;
      }
      const auto layer  = d.tech.layers.find(path.layer);
      const auto native = layer == d.tech.layers.end() ? std::optional<int64_t>{} : physical::wire_width_dbu(layer->second, g.dbu);
      const auto width  = path.width ? path.width : native;
      if (!width || *width <= 0) {
        reject();
        continue;
      }
      std::optional<physical::Route_point> previous;
      bool                                 bad = false;
      for (const auto& step : path.steps) {
        const auto* point = std::get_if<physical::Route_point>(&step);
        if (!point) {
          bad = true;
          continue;
        }  // Planned carriers are straight wires; no concealed layer/rectangle changes.
        if (previous) {
          const bool horizontal = path.layer == "M2";
          if (horizontal ? previous->y != point->y : previous->x != point->x) {
            bad      = true;
            previous = *point;
            continue;
          }
          const int64_t coordinate = horizontal ? point->y : point->x;
          const int64_t a = horizontal ? previous->x : previous->y, b = horizontal ? point->x : point->y;
          const int64_t ae = previous->extension.value_or((*width + 1) / 2), be = point->extension.value_or((*width + 1) / 2);
          if (ae < 0 || be < 0) {
            bad      = true;
            previous = *point;
            continue;
          }
          const int64_t low = std::min(a - ae, b - be), high = std::max(a + ae, b + be);
          const int     band  = horizontal ? g.row_of(coordinate) : g.col_of(coordinate);
          const int     first = horizontal ? g.col_of(low) : g.row_of(low);
          const int     last  = horizontal ? g.col_of(high) : g.row_of(high);
          for (int along = first; along <= last; ++along) {
            const int     t     = horizontal ? g.tile(along, band) : g.tile(band, along);
            const int64_t begin = horizontal ? g.left(along) : g.bottom(along);
            const int64_t end   = begin + (horizontal ? g.tile_w : g.tile_h);
            if (high <= begin || low >= end) {
              continue;
            }
            const auto& local  = horizontal ? g.critical_reserved_h.at(size_t(t)) : g.critical_reserved_v.at(size_t(t));
            bad               |= std::ranges::binary_search(local, coordinate);
          }
        }
        previous = *point;
      }
      if (bad) {
        reject();
      }
    }
  }
  return conflicts;
}
bool repair_forest(const Grid& g, std::set<std::pair<int, int>>& E, const std::set<int>& terminals,
                   std::unordered_map<int64_t, int>& use, const std::function<int(int, int)>& limit, const Forest_costs& costs,
                   int margin) {
  const auto key  = [&](int x, int y) { return forest_edge_key(g, x, y); };
  const auto drop = [&](std::pair<int, int> e) {
    if (E.erase(e)) {
      --use[key(e.first, e.second)];
    }
  };
  for (bool pruned = true; pruned;) {  // dangling branches (no terminal at their end)
    pruned = false;
    std::map<int, std::vector<std::pair<int, int>>> incident;
    for (const auto& e : E) {
      incident[e.first].push_back(e);
      incident[e.second].push_back(e);
    }
    for (const auto& [t, list] : incident) {
      if (list.size() == 1 && !terminals.contains(t) && E.contains(list.front())) {
        drop(list.front());
        pruned = true;
      }
    }
  }
  int        c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  const auto grow = [&](int t) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  };
  for (int t : terminals) {
    grow(t);
  }
  for (auto [x, y] : E) {
    grow(x);
    grow(y);
  }
  c0 = std::max(0, c0 - margin);
  r0 = std::max(0, r0 - margin);
  c1 = std::min(g.cols - 1, c1 + margin);
  r1 = std::min(g.rows - 1, r1 + margin);
  std::map<int, int> parent;
  const auto         find = [&](int t) {
    parent.try_emplace(t, t);
    while (parent[t] != t) {
      parent[t] = parent[parent[t]];
      t         = parent[t];
    }
    return t;
  };
  std::set<int> nodes(terminals.begin(), terminals.end());
  for (auto [x, y] : E) {
    parent[find(x)] = find(y);
    nodes.insert(x);
    nodes.insert(y);
  }
  if (nodes.size() <= 1) {
    return true;
  }
  std::set<int> connected;
  const int     root0 = find(*nodes.begin());
  for (int t : nodes) {
    if (find(t) == root0) {
      connected.insert(t);
    }
  }
  // Legroute metal is 1D: every bend of a tile path is a via between an H and a V layer (a join node with its layer
  // adjacency), so a turn costs costs.bend tiles. State: tile x arrival direction (0 H, 1 V, 2 start).
  while (connected.size() < nodes.size()) {
    std::unordered_map<int64_t, double>  dist;
    std::unordered_map<int64_t, int64_t> from;
    const auto state = [](int t, int dir) { return int64_t(t) * 3 + dir; };
    using Entry      = std::pair<double, int64_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    for (int t : connected) {
      dist[state(t, 2)] = 0;
      queue.emplace(0, state(t, 2));
    }
    int64_t arrival = -1;
    while (!queue.empty()) {
      auto [cost, st] = queue.top();
      queue.pop();
      if (cost > dist[st]) {
        continue;
      }
      const int t = int(st / 3), dir = int(st % 3);
      if (nodes.contains(t) && !connected.contains(t)) {
        arrival = st;
        break;
      }
      const int col = g.col(t), row = g.row(t);
      for (int u : {col > c0 ? t - 1 : -1, col < c1 ? t + 1 : -1, row > r0 ? t - g.cols : -1, row < r1 ? t + g.cols : -1}) {
        if (u < 0) {
          continue;
        }
        const bool horizontal = g.row(t) == g.row(u);
        const int  cap        = limit(t, u);
        const int  used       = use[key(t, u)];
        if (used >= cap) {
          continue;  // the forest stays within the admitted capacity (the handoff contract)
        }
        double next = cost + 1 + double(used + 1) / cap + (dir != 2 && dir != (horizontal ? 0 : 1) ? costs.bend : 0);
        if (const auto* hist = horizontal ? costs.hist_h : costs.hist_v) {
          next += costs.hist_weight * ((*hist)[size_t(t)] + (*hist)[size_t(u)]);
        }
        const auto su = state(u, horizontal ? 0 : 1);
        const auto it = dist.find(su);
        if (it == dist.end() || next < it->second) {
          dist[su] = next;
          from[su] = st;
          queue.emplace(next, su);
        }
      }
    }
    if (arrival < 0) {
      return false;
    }
    std::set<int> seen;  // a path revisiting a tile (another arrival direction) would close a cycle
    for (int64_t st = arrival;; st = from.at(st)) {
      if (!seen.insert(int(st / 3)).second) {
        return false;
      }
      if (st % 3 == 2) {
        break;
      }
    }
    const int hit   = int(arrival / 3);
    parent[find(hit)] = find(root0);
    for (int64_t st = arrival; st % 3 != 2;) {
      const int64_t ps = from.at(st);
      const int     t = int(st / 3), p = int(ps / 3);
      E.insert(std::minmax(p, t));
      ++use[key(p, t)];
      parent[find(p)] = find(root0);
      st              = ps;
    }
    for (int t : nodes) {
      if (find(t) == find(root0)) {
        connected.insert(t);
      }
    }
  }
  return true;
}

namespace {
// What L5 could not place: per net the failing conns and the nodes with a failing riser, jog or IO stub, and every
// tile (with its direction) a failure crosses.
struct Local_failures {
  std::map<int, std::set<int>>     conns, nodes;
  std::vector<std::pair<int, bool>> tiles;
  size_t                           items = 0;
  size_t                           nets() const {
    std::set<int> all;
    for (const auto& [n, _] : conns) {
      all.insert(n);
    }
    for (const auto& [n, _] : nodes) {
      all.insert(n);
    }
    return all.size();
  }
};
Local_failures local_failures(const Grid& g, const Routing& R) {
  Local_failures f;
  const auto     fallback = [](uint8_t state) { return state == uint8_t(Track_state::fallback); };
  for (const auto& sg : R.segments) {
    if (sg.net < 0 || size_t(sg.net) >= R.trees.size()) {
      continue;
    }
    if (sg.track < 0
        || (sg.track_state != uint8_t(Track_state::checked) && sg.track_state != uint8_t(Track_state::prescribed))) {
      ++f.items;
      f.conns[sg.net].insert(sg.conn);
      if (sg.layer >= 0 && size_t(sg.layer) < g.layers.size()) {
        const bool h = g.layers[size_t(sg.layer)].horizontal;
        for (int k = sg.lo; k <= sg.hi; ++k) {
          const int t = h ? g.tile(k, sg.band) : g.tile(sg.band, k);
          if (t >= 0 && size_t(t) < g.size()) {
            f.tiles.emplace_back(t, h);
          }
        }
      }
    }
    for (auto [node, track, state] : {std::tuple{sg.node_lo, sg.jog_lo, sg.jog_lo_state}, std::tuple{sg.node_hi, sg.jog_hi, sg.jog_hi_state}}) {
      if (node >= 0 && track >= 0 && fallback(state)) {
        ++f.items;
        f.nodes[sg.net].insert(node);
        f.tiles.emplace_back(R.trees[size_t(sg.net)].nodes[size_t(node)].tile, true);
      }
    }
  }
  for (size_t n = 0; n < R.trees.size(); ++n) {
    const auto& nodes = R.trees[n].nodes;
    for (size_t k = 0; k < nodes.size(); ++k) {
      const auto& node = nodes[k];
      if (node.tile < 0) {
        continue;
      }
      if (node.riser_track >= 0 && fallback(node.riser_state)) {
        ++f.items;
        f.nodes[int(n)].insert(int(k));
        f.tiles.emplace_back(node.tile, false);
      }
      if (node.stub_track >= 0 && fallback(node.stub_state)) {
        ++f.items;
        f.nodes[int(n)].insert(int(k));
        f.tiles.emplace_back(node.tile, true);
      }
    }
  }
  return f;
}
}  // namespace

namespace {
using Seg_key = std::tuple<int, int, int, size_t>;  // net, end tiles of its conn, segment index in the conn
Seg_key seg_key(const Routing& r, const Segment& sg, size_t i) {
  const auto& c     = r.conns[size_t(sg.conn)];
  const auto& nodes = r.trees[size_t(sg.net)].nodes;
  return Seg_key{sg.net, nodes[size_t(c.a)].tile, nodes[size_t(c.b)].tile, i};
}
}  // namespace

Kept_tracks collect_kept(const Grid& g, const Routing& R, const std::function<bool(int)>& keep) {
  Kept_tracks out;
  for (const auto& c : R.conns) {
    if (!keep(c.net)) {
      continue;
    }
    for (size_t i = 0; i < c.segs.size(); ++i) {
      const auto& sg = R.segments[size_t(c.segs[i])];
      if (sg.track >= 0 && sg.track_state == uint8_t(Track_state::checked)) {
        out.segs.emplace(seg_key(R, sg, i), sg);
      }
    }
  }
  std::set<std::pair<int, int>> v_arrival;  // (net, node)
  for (const auto& sg : R.segments) {
    if (sg.net >= 0 && sg.layer >= 0 && !g.layers[size_t(sg.layer)].horizontal) {
      for (int node : {sg.node_lo, sg.node_hi}) {
        if (node >= 0) {
          v_arrival.emplace(sg.net, node);
        }
      }
    }
  }
  for (size_t n = 0; n < R.trees.size(); ++n) {
    if (!keep(int(n))) {
      continue;
    }
    const auto& nodes = R.trees[n].nodes;
    for (size_t k = 0; k < nodes.size(); ++k) {
      const auto& nd = nodes[k];
      if (nd.riser_track >= 0 && nd.riser_state == uint8_t(Track_state::checked) && !nd.direct_carrier_tap
          && !v_arrival.contains({int(n), int(k)})) {
        out.risers.emplace(std::pair{int(n), nd.tile}, std::pair{nd.riser_layer, nd.riser_track});
      }
    }
  }
  return out;
}

void l5_with_kept(const Design& d, const Grid& g, Routing& R, const Kept_tracks& kept, unsigned threads) {
  assign_layers(d, g, R);
  std::vector<char> preset(R.segments.size(), 0);
  // the new tree's vertical arrival per (net, node): its key decides whether the node's riser stays
  std::map<std::pair<int, int>, Seg_key> new_v_arrival;
  for (const auto& c : R.conns) {
    for (size_t i = 0; i < c.segs.size(); ++i) {
      const auto& sg = R.segments[size_t(c.segs[i])];
      if (sg.layer >= 0 && !g.layers[size_t(sg.layer)].horizontal) {
        for (int node : {sg.node_lo, sg.node_hi}) {
          if (node >= 0) {
            new_v_arrival.try_emplace({sg.net, node}, seg_key(R, sg, i));
          }
        }
      }
    }
  }
  std::set<std::pair<int, int>> riser_kept;  // (net, node) whose riser keeps its coordinate
  for (size_t n = 0; n < R.trees.size(); ++n) {
    auto& nodes = R.trees[n].nodes;
    for (size_t k = 0; k < nodes.size(); ++k) {
      auto& nd = nodes[k];
      if (nd.direct_carrier_tap) {
        continue;
      }
      if (const auto v = new_v_arrival.find({int(n), int(k)}); v != new_v_arrival.end()) {
        if (kept.segs.contains(v->second)) {
          riser_kept.emplace(int(n), int(k));
        }
      } else if (const auto it = kept.risers.find({int(n), nd.tile}); it != kept.risers.end()) {
        nd.riser_layer = it->second.first;
        nd.riser_track = it->second.second;
        riser_kept.emplace(int(n), int(k));
      }
    }
  }
  for (const auto& c : R.conns) {
    for (size_t i = 0; i < c.segs.size(); ++i) {
      auto& sg = R.segments[size_t(c.segs[i])];
      auto  it = kept.segs.find(seg_key(R, sg, i));
      if (it != kept.segs.end() && sg.layer >= 0 && g.layers[size_t(it->second.layer)].horizontal) {
        // a horizontal end on a join stops at the node's riser: it keeps its span only if that riser stays
        for (int node : {sg.node_lo, sg.node_hi}) {
          if (node >= 0 && !R.trees[size_t(sg.net)].nodes[size_t(node)].direct_carrier_tap && !riser_kept.contains({sg.net, node})) {
            it = kept.segs.end();
            break;
          }
        }
      }
      if (it != kept.segs.end()) {
        sg.layer                  = it->second.layer;
        sg.track                  = it->second.track;
        sg.tap_lo                 = it->second.tap_lo;
        sg.tap_hi                 = it->second.tap_hi;
        sg.span_lo                = it->second.span_lo;
        sg.span_hi                = it->second.span_hi;
        preset[size_t(c.segs[i])] = 1;
      }
    }
  }
  assign_tracks(d, g, R, threads);
  // a kept track passed every pick filter when it was chosen, and every new pick was checked against it
  for (auto& tree : R.trees) {
    for (auto& nd : tree.nodes) {
      if (nd.riser_state == uint8_t(Track_state::prescribed)) {
        nd.riser_state = uint8_t(Track_state::checked);
      }
    }
  }
  for (size_t si = 0; si < R.segments.size(); ++si) {
    auto& sg = R.segments[si];
    if (!preset[si] || sg.track_state != uint8_t(Track_state::prescribed)) {
      continue;
    }
    sg.track_state = uint8_t(Track_state::checked);
    for (int node : {sg.node_lo, sg.node_hi}) {
      if (node >= 0) {
        auto& nd = R.trees[size_t(sg.net)].nodes[size_t(node)];
        if (nd.riser_track == sg.track && nd.riser_layer == sg.layer && nd.riser_state == uint8_t(Track_state::prescribed)) {
          nd.riser_state = uint8_t(Track_state::checked);
        }
      }
    }
  }
}

std::set<int> fixed_net_set(const Design& d, const Grid& g, const Boxes& boxes) {
  (void)g;
  (void)boxes;
  std::set<int> out;
  if (d.opt.cellbox_handoff) {
    for (const auto& p : d.opt.cellbox_handoff->preroutes) {
      out.insert(p.net);
    }
    out.insert(d.opt.cellbox_handoff->fixed_nets.begin(), d.opt.cellbox_handoff->fixed_nets.end());
  }
  return out;
}

std::set<int> select_region_nets(const Design& d, const Grid& g, const Boxes& boxes) {
  std::set<int> out;
  const int     k = int(d.opt.cellbox_fixed_regions), need = int(d.opt.cellbox_fixed_min_regions);
  if (k <= 0 || need <= 0) {
    return out;
  }
  const auto region = [&](int t) { return std::pair{std::min(k - 1, g.col(t) * k / g.cols), std::min(k - 1, g.row(t) * k / g.rows)}; };
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& net = d.nets[n];
    if (net.power || net.control_scope || !net.routed || net.pins.size() < 2) {
      continue;
    }
    std::set<std::pair<int, int>> regions;
    for (const auto& p : net.pins) {
      const int m = p.comp >= 0 ? d.cell[size_t(p.comp)] : -1;
      const int b = m >= 0 ? boxes.box_of.at(size_t(m)) : -1;
      const int t = b >= 0 ? boxes.boxes.at(size_t(b)).tile : physical_pin_access(d, g, p).tile;
      if (t >= 0) {
        regions.insert(region(t));
      }
    }
    if (int(regions.size()) >= need) {
      out.insert(int(n));
    }
  }
  return out;
}

Plan_fixed plan_fixed_tracks(const Design& d, const Grid& g, const std::shared_ptr<const Critical_cellbox_handoff>& h,
                             const std::set<int>& candidates, unsigned threads) {
  Plan_fixed out;
  out.use_h.assign(g.size(), 0);
  out.use_v.assign(g.size(), 0);
  out.candidates = candidates.size();
  if (candidates.empty()) {
    return out;
  }
  auto opt            = d.opt;
  opt.cellbox_handoff = h;
  Design      d2{d.tech, d.fp, opt, d.profile, d.macro, d.movable, d.cell, d.width, d.flop, d.tall, d.nets, d.nets_of, d.wns,
            d.flop_w, d.widest};
  const auto  saved = d.fp.components;  // installation writes the admitted poses; Plan's frame must stay as it was
  Grid        g2    = g;
  Boxes       b2;
  Routing     R;
  Legroute_report scratch;
  try {
    install_critical_handoff(d2, g2, b2);
    std::vector<char> only(d.nets.size(), 0);
    for (int n : candidates) {
      only[size_t(n)] = 1;
    }
    critical_local_routes(d2, g2, b2, R, scratch, nullptr, &only);
    assign_layers(d2, g2, R);
    assign_tracks(d2, g2, R, threads);
  } catch (...) {
    d.fp.components = saved;
    throw;
  }
  d.fp.components = saved;
  std::set<int> failed;
  for (const auto& sg : R.segments) {
    if (sg.track < 0 || (sg.track_state != uint8_t(Track_state::checked) && sg.track_state != uint8_t(Track_state::prescribed))) {
      failed.insert(sg.net);
    }
  }
  for (size_t n = 0; n < R.trees.size(); ++n) {
    for (const auto& nd : R.trees[n].nodes) {
      if (nd.riser_track >= 0 && nd.riser_state == uint8_t(Track_state::fallback)) {
        failed.insert(int(n));
      }
    }
  }
  out.failed = failed.size();
  for (int n : candidates) {
    if (!failed.contains(n)) {
      out.nets.push_back(n);
    }
  }
  const std::set<int> fixed(out.nets.begin(), out.nets.end());
  out.kept = std::make_shared<Kept_tracks>(collect_kept(g2, R, [&](int n) { return fixed.contains(n); }));
  // the tracks they take: a net once per tile and direction (segments and risers)
  std::set<std::tuple<int, int, bool>> taken;  // (net, tile, horizontal)
  for (const auto& sg : R.segments) {
    if (!fixed.contains(sg.net) || sg.layer < 0) {
      continue;
    }
    const bool hz = g2.layers[size_t(sg.layer)].horizontal;
    for (int k = sg.lo; k <= sg.hi; ++k) {
      taken.emplace(sg.net, hz ? g2.tile(k, sg.band) : g2.tile(sg.band, k), hz);
    }
  }
  for (int n : out.nets) {
    for (const auto& nd : R.trees[size_t(n)].nodes) {
      if (nd.riser_track >= 0 && nd.tile >= 0) {
        taken.emplace(n, nd.tile, false);
      }
    }
  }
  for (const auto& [n, t, hz] : taken) {
    ++(hz ? out.use_h : out.use_v)[size_t(t)];
  }
  return out;
}

Local_detour_result local_detours(const Design& d, const Grid& g, const Boxes& boxes, Routing& R, Legroute_report& rep,
                                  unsigned rounds, unsigned threads, const Kept_tracks& fixed_kept) {
  const auto  start = Clock::now();
  const auto& h     = *d.opt.cellbox_handoff;
  std::vector<int> request_of(d.nets.size(), -1);
  Local_forest     forest;
  forest.edges.resize(h.requests.size());
  forest.priority.assign(d.nets.size(), 0);
  forest.splits.assign(d.nets.size(), {});
  std::vector<unsigned> split_count(d.nets.size(), 0);
  for (size_t q = 0; q < h.requests.size(); ++q) {
    request_of.at(size_t(h.requests[q].net)) = int(q);
    forest.edges[q]                          = h.routes.trees[q].edges;
  }
  auto [cap_h, cap_v] = transit_capacity(g);
  const double share  = admission_share(d.opt);
  const auto   key    = [&](int x, int y) { return forest_edge_key(g, x, y); };
  const std::function<int(int, int)> limit = [&](int x, int y) {
    const auto& caps = g.row(x) == g.row(y) ? cap_h : cap_v;
    return std::max(0, int(std::floor(share * std::min(caps[size_t(x)], caps[size_t(y)]))));
  };
  std::vector<double> hist_h(g.size(), 0), hist_v(g.size(), 0);
  // Incremental L5: the nets outside `moved` keep every checked layer/track of the previous round (prescribed, so
  // only the moved nets pick against them); a full rebuild when moved is null.
  const auto          rebuild = [&](const Local_forest& f, const std::set<int>* moved) {
    Kept_tracks kept;
    if (moved) {
      kept = collect_kept(g, R, [&](int n) { return !moved->contains(n); });
    }
    for (const auto& [seg_id, sg] : fixed_kept.segs) {  // the nets fixed first keep their tracks in every round
      kept.segs.insert_or_assign(seg_id, sg);
    }
    for (const auto& [riser_id, riser] : fixed_kept.risers) {
      kept.risers.insert_or_assign(riser_id, riser);
    }
    const size_t critical = rep.critical_connections;
    critical_local_routes(d, g, boxes, R, rep, &f);
    rep.critical_connections = critical;
    l5_with_kept(d, g, R, kept, threads);
  };
  Routing       best;
  Local_forest  best_forest;
  size_t        best_nets = std::numeric_limits<size_t>::max(), best_items = best_nets;
  unsigned      best_round = 0, ran = 0;
  std::set<int> rerouted;
  for (unsigned round = 0;; ++round) {
    const auto   failures = local_failures(g, R);
    const size_t nets     = failures.nets();
    if (round == 0) {
      rep.local_detour_first_failed     = nets;
      rep.local_detour_first_unassigned = R.unassigned;
    }
    std::fprintf(stderr,
                 "legroute L5 detour round %u: %zu failing nets (%zu failing segments/risers/jogs/stubs), %zu unassigned\n",
                 round,
                 nets,
                 failures.items,
                 R.unassigned);
    if (nets < best_nets || (nets == best_nets && failures.items < best_items)) {
      if (round != rounds) {
        best = R;  // the last round needs no snapshot: it is R itself
      }
      best_forest = forest;
      best_nets  = nets;
      best_items = failures.items;
      best_round = round;
    }
    if (!nets || round == rounds) {
      break;
    }
    for (auto [t, horizontal] : failures.tiles) {
      (horizontal ? hist_h : hist_v)[size_t(t)] += 1;
    }
    std::unordered_map<int64_t, int> use;
    for (const auto& es : forest.edges) {
      for (auto [x, y] : es) {
        ++use[key(x, y)];
      }
    }
    std::set<int> failing;
    for (const auto& [n, _] : failures.conns) {
      failing.insert(n);
    }
    for (const auto& [n, _] : failures.nodes) {
      failing.insert(n);
    }
    const auto fixed_nets = fixed_net_set(d, g, boxes);  // fixed before Local (and preroutes): never rerouted
    for (int n : failing) {
      const int q = request_of.at(size_t(n));
      if (q < 0 || fixed_nets.contains(n)) {
        continue;
      }
      if (d.opt.cellbox_local_detour_boost) {
        ++forest.priority[size_t(n)];
      }
      const auto& tree = R.trees[size_t(n)];
      std::set<int> terminals;
      for (const auto& node : tree.nodes) {
        if (!node.pins.empty() && node.tile >= 0) {
          terminals.insert(node.tile);
        }
      }
      std::set<std::pair<int, int>> E;
      for (auto [x, y] : forest.edges[size_t(q)]) {
        E.insert(std::minmax(x, y));
      }
      const auto old = E;
      const auto drop = [&](std::pair<int, int> e) {
        if (E.erase(e)) {
          --use[key(e.first, e.second)];
        }
      };
      // A failing long run first splits at its middle tile (each piece then takes its own track: a run needs one
      // track free over its whole span); short runs, and runs split twice, rip and detour.
      std::set<int> rip;
      if (const auto it = failures.conns.find(n); it != failures.conns.end()) {
        for (int ci : it->second) {
          if (ci < 0 || size_t(ci) >= R.conns.size()) {
            continue;  // a segment without a conn (not a Local tree run) has no tile path to reroute
          }
          const auto& poly = R.conns[size_t(ci)].poly;
          const int   len  = poly.size() == 2 ? std::abs(poly[0].first - poly[1].first) + std::abs(poly[0].second - poly[1].second) : 0;
          if (len >= 4 && split_count[size_t(n)] < 2 * it->second.size() + 2) {
            const int mc = (poly[0].first + poly[1].first) / 2, mr = (poly[0].second + poly[1].second) / 2;
            if (forest.splits[size_t(n)].insert(g.tile(mc, mr)).second) {
              ++split_count[size_t(n)];
              ++rep.local_detour_splits;
              continue;
            }
          }
          rip.insert(ci);
        }
      }
      if (const auto it = failures.nodes.find(n); it != failures.nodes.end()) {
        for (int ci : tree.conns) {
          if (it->second.contains(R.conns[size_t(ci)].a) || it->second.contains(R.conns[size_t(ci)].b)) {
            rip.insert(ci);
          }
        }
      }
      for (int ci : rip) {
        if (ci < 0 || size_t(ci) >= R.conns.size()) {
          continue;
        }
        const auto& poly = R.conns[size_t(ci)].poly;
        for (size_t i = 0; i + 1 < poly.size(); ++i) {
          auto [c, r]        = poly[i];
          const auto [c1, r1] = poly[i + 1];
          while (c != c1 || r != r1) {
            const int t = g.tile(c, r);
            c += (c1 > c) - (c1 < c);
            r += (r1 > r) - (r1 < r);
            drop(std::minmax(t, g.tile(c, r)));
          }
        }
      }
      Forest_costs costs;
      costs.hist_h = &hist_h;
      costs.hist_v = &hist_v;
      const auto restore = [&]() {
        for (const auto& e : E) {
          --use[key(e.first, e.second)];
        }
        E = old;
        for (const auto& e : E) {
          ++use[key(e.first, e.second)];
        }
      };
      bool ok = repair_forest(g, E, terminals, use, limit, costs, 4) || repair_forest(g, E, terminals, use, limit, costs, 16);
      if (!ok) {
        restore();
        continue;
      }
      if (E != old) {
        rerouted.insert(n);
      }
      forest.edges[size_t(q)].assign(E.begin(), E.end());
    }
    // Incremental: every checked segment whose run (net, end tiles) is unchanged keeps its track, failing nets
    // included; only the failing, split or rerouted runs pick again.
    const std::set<int> none;
    rebuild(forest, d.opt.cellbox_local_detour_incremental ? &none : nullptr);
    ran = round + 1;
  }
  if (best_round != ran) {
    R = std::move(best);
  }
  Local_detour_result result;
  result.forest = std::move(best_forest);
  result.tile_cost.resize(g.size());
  for (size_t t = 0; t < g.size(); ++t) {
    result.tile_cost[t] = hist_h[t] + hist_v[t];
  }
  {
    const auto         failures = local_failures(g, R);
    std::map<int, int> count;
    for (const auto& [n, conns] : failures.conns) {
      for (int ci : conns) {
        if (ci < 0 || size_t(ci) >= R.conns.size()) {
          continue;
        }
        for (int node : {R.conns[size_t(ci)].a, R.conns[size_t(ci)].b}) {
          if (const int b = R.trees[size_t(n)].nodes[size_t(node)].box; b >= 0) {
            ++count[b];
          }
        }
      }
    }
    for (const auto& [n, nodes] : failures.nodes) {
      for (int node : nodes) {
        if (const int b = R.trees[size_t(n)].nodes[size_t(node)].box; b >= 0) {
          ++count[b];
        }
      }
    }
    std::vector<std::pair<int, int>> ranked;
    for (auto [b, k] : count) {
      ranked.emplace_back(-k, b);
    }
    std::ranges::sort(ranked);
    for (auto [k, b] : ranked) {
      result.failing_boxes.push_back(b);
    }
  }
  rep.local_detour_rounds     = ran;
  rep.local_detour_best_round = best_round;
  rep.local_detour_nets       = rerouted.size();
  rep.local_detour_best_failed = best_nets;
  rep.local_detour_seconds    = seconds_since(start);
  std::fprintf(stderr,
               "legroute L5 detours: %u rounds, %zu nets rerouted, failing nets %zu -> %zu (round %u), %.1f s\n",
               ran,
               rerouted.size(),
               rep.local_detour_first_failed,
               best_nets,
               best_round,
               rep.local_detour_seconds);
  return result;
}
}  // namespace livehd::placement::lr
