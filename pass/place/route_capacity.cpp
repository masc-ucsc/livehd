// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "route_capacity.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <stdexcept>

namespace livehd::placement {
namespace ph = physical;
namespace {
// True when a lies farther than d from b along either axis.
bool apart(const ph::Rect& a, const ph::Rect& b, double d) {
  return a.xl > b.xh + d || a.xh < b.xl - d || a.yl > b.yh + d || a.yh < b.yl - d;
}
}  // namespace
Route_capacity::Route_capacity(const ph::Technology& tech, const ph::Floorplan& fp, const ph::Routing_occupancy& obstacles,
                               Route_capacity_grid grid, const std::function<void()>& checkpoint)
    : grid_(std::move(grid)), rules_(tech, fp) {
  if (checkpoint) {
    checkpoint();
  }
  if (!grid_.bins_x || !grid_.bins_y || grid_.bins_x > 1024 || grid_.bins_y > 1024 || grid_.layers.empty()
      || !std::isfinite(grid_.max_preroute_fraction) || grid_.max_preroute_fraction < 0 || grid_.max_preroute_fraction > 1) {
    throw std::invalid_argument("invalid route capacity grid");
  }
  for (auto v : {grid_.bounds.xl, grid_.bounds.yl, grid_.bounds.xh, grid_.bounds.yh}) {
    if (!std::isfinite(v) || v != std::round(v) || v < INT32_MIN || v > INT32_MAX) {
      throw std::invalid_argument("invalid capacity bounds");
    }
  }
  if (grid_.bounds.empty() || !fp.die.contains(grid_.bounds)) {
    throw std::invalid_argument("capacity bounds outside die");
  }
  const double dx = (grid_.bounds.xh - grid_.bounds.xl) / grid_.bins_x, dy = (grid_.bounds.yh - grid_.bounds.yl) / grid_.bins_y;
  // Metal below the lowest modeled routing layer is pin-access metal and uses
  // no modeled tracks; neither do cuts.
  const auto lowest = std::find_if(tech.routing_layers.begin(), tech.routing_layers.end(), [&](const auto& name) {
    return std::find(grid_.layers.begin(), grid_.layers.end(), name) != grid_.layers.end();
  });
  if (lowest != tech.routing_layers.end()) {
    ignored_layers_.insert(tech.routing_layers.begin(), lowest);
  }
  for (const auto& [name, cut] : tech.cut_layers) {
    ignored_layers_.insert(name);
  }
  std::set<std::string> names;
  for (const auto& name : grid_.layers) {
    if (!names.insert(name).second) {
      throw std::invalid_argument("duplicate capacity layer");
    }
    const auto& layer      = tech.layers.at(name);
    const bool  horizontal = layer.direction == "HORIZONTAL";
    if (!horizontal && layer.direction != "VERTICAL") {
      throw std::invalid_argument("unknown capacity layer direction");
    }
    const auto clearance = rules_.clearance(name);
    const auto nominal   = ph::wire_width_dbu(layer, fp.dbu);
    if (!clearance || !nominal) {
      throw std::invalid_argument("unsupported capacity layer rules");
    }
    const double width = *nominal;
    layers_.emplace(name, Layer_index{cells_.size(), double(*clearance) + width / 2 + 1});
    std::set<int64_t> coordinates;
    const double      lo = horizontal ? grid_.bounds.yl : grid_.bounds.xl, hi = horizontal ? grid_.bounds.yh : grid_.bounds.xh;
    for (const auto& t : fp.tracks) {
      if (t.layer != name || t.axis != (horizontal ? "Y" : "X")) {
        continue;
      }
      if (!t.supported()) {
        throw std::invalid_argument("unsupported capacity tracks");
      }
      const auto [first, last] = t.index_range(lo + width / 2, hi - width / 2);
      if (last - first > 1000000) {
        throw std::invalid_argument("oversized capacity track set");
      }
      for (auto i = first; i <= last; ++i) {
        coordinates.insert(t.start + i * t.step);
      }
    }
    if (coordinates.empty()) {
      throw std::invalid_argument("no usable capacity track coordinates");
    }
    for (size_t x = 0; x < grid_.bins_x; ++x) {
      for (size_t y = 0; y < grid_.bins_y; ++y) {
        if (checkpoint) {
          checkpoint();
        }
        Cell cell;
        cell.bounds
            = {grid_.bounds.xl + x * dx, grid_.bounds.yl + y * dy, grid_.bounds.xl + (x + 1) * dx, grid_.bounds.yl + (y + 1) * dy};
        cell.horizontal = horizontal;
        cell.report     = {name, x, y, 0, 0};
        // All track footprints fit in this envelope. Query the spatial index
        // once per cell instead of repeatedly sorting the same long PG rails
        // for every track. Keep each track's original clearance test below.
        const ph::Rect envelope{std::max(grid_.bounds.xl, std::floor(cell.bounds.xl) - width / 2),
                                std::max(grid_.bounds.yl, std::floor(cell.bounds.yl) - width / 2),
                                std::min(grid_.bounds.xh, std::ceil(cell.bounds.xh) + width / 2),
                                std::min(grid_.bounds.yh, std::ceil(cell.bounds.yh) + width / 2)};
        const auto     nearby = obstacles.query(name, envelope, *clearance);
        const auto     low = horizontal ? cell.bounds.yl : cell.bounds.xl, high = horizontal ? cell.bounds.yh : cell.bounds.xh;
        for (auto it = coordinates.lower_bound(std::ceil(low)); it != coordinates.end() && *it < high; ++it) {
          const double c = *it;
          ph::Shape    footprint{name,
                              horizontal
                                     ? ph::Rect{std::floor(cell.bounds.xl), c - width / 2, std::ceil(cell.bounds.xh), c + width / 2}
                                     : ph::Rect{c - width / 2, std::floor(cell.bounds.yl), c + width / 2, std::ceil(cell.bounds.yh)}};
          cell.tracks.push_back({footprint, true, false});
        }
        // Visit each obstacle only on tracks whose transverse footprints can
        // reach it. A dense PG otherwise repeats the same full obstacle list
        // for every track in the bin. Keep the exact spacing predicate below.
        const auto center = [horizontal](const Track& track) {
          const auto& rect = track.footprint.rect;
          return horizontal ? (rect.yl + rect.yh) / 2 : (rect.xl + rect.xh) / 2;
        };
        size_t visited = 0;
        size_t usable = cell.tracks.size();
        for (auto id : nearby) {
          // Obstructions only remove capacity. Once every track is blocked,
          // the remaining shapes cannot change this bin's result.
          if (!usable) {
            break;
          }
          if (checkpoint && (visited++ % 1024 == 0)) {
            checkpoint();
          }
          const auto& obstacle = obstacles.at(id).shape;
          const double first = (horizontal ? obstacle.rect.yl : obstacle.rect.xl) - *clearance - width / 2;
          const double last = (horizontal ? obstacle.rect.yh : obstacle.rect.xh) + *clearance + width / 2;
          auto track = std::lower_bound(cell.tracks.begin(), cell.tracks.end(), first,
                                        [&](const Track& item, double value) { return center(item) < value; });
          for (; track != cell.tracks.end() && center(*track) <= last; ++track) {
            if (!track->usable) {
              continue;
            }
            const auto& footprint = track->footprint;
            if (apart(obstacle.rect, footprint.rect, *clearance)) {
              continue;
            }
            if (!rules_.spacing(footprint, obstacle).legal) {
              track->usable = false;
              --usable;
            }
          }
        }
        cell.report.usable_tracks = usable;
        cells_.push_back(std::move(cell));
      }
    }
  }
  // Static obstacles determine usable capacity once. Commits change reserved
  // tracks only; demand shares must retain these original directional totals.
  total_h_.assign(grid_.bins_x * grid_.bins_y, 0);
  total_v_.assign(grid_.bins_x * grid_.bins_y, 0);
  for (const auto& cell : cells_) {
    (cell.horizontal ? total_h_ : total_v_)[cell.report.x * grid_.bins_y + cell.report.y] += cell.report.usable_tracks;
  }
}
Route_capacity_trial Route_capacity::check(const std::vector<ph::Shape>& shapes, const Remaining_route_demand& demand) const {
  return check_impl(shapes, demand, false);
}
void Route_capacity::add_obstructions(const ph::Routing_occupancy& added, const std::function<void()>& checkpoint) {
  const auto count = grid_.bins_x * grid_.bins_y;
  for (size_t id = 0; id < added.size(); ++id) {
    if (checkpoint) {
      checkpoint();
    }
    const auto obstacle = added.at(id).shape;
    const auto layer = layers_.find(obstacle.layer);
    if (layer == layers_.end()) {
      continue;
    }
    const auto clearance = *rules_.clearance(obstacle.layer);
    const auto reach = layer->second.reach;
    for (size_t index = layer->second.start; index < layer->second.start + count; ++index) {
      if (checkpoint && index % 256 == 0) {
        checkpoint();
      }
      auto& cell = cells_[index];
      if (!cell.report.usable_tracks || apart(obstacle.rect, cell.bounds, reach)) {
        continue;
      }
      for (auto& track : cell.tracks) {
        const auto& r = track.footprint.rect;
        if (!track.usable || apart(obstacle.rect, r, clearance)) {
          continue;
        }
        if (!rules_.spacing(track.footprint, obstacle).legal) {
          track.usable = false;
          --cell.report.usable_tracks;
          --(cell.horizontal ? total_h_ : total_v_)[cell.report.x * grid_.bins_y + cell.report.y];
        }
      }
    }
  }
}
Route_capacity_seed::Route_capacity_seed(const ph::Technology& tech, const ph::Floorplan& fp, Route_capacity_grid grid,
                                         const ph::Routing_power_grid* pg, const std::function<void()>& checkpoint)
    : technology_(&tech), signature_(fp) {
  const auto obstacles = ph::routing_occupancy(tech, fp, fp.dbu, checkpoint, pg, ph::Signal_wires::exclude);
  capacity_ = std::make_unique<Route_capacity>(tech, fp, obstacles, std::move(grid), checkpoint);
  signature_.nets.clear();
  for (auto& net : signature_.special_nets) {
    net.connections.clear();
  }
}
std::unique_ptr<Route_capacity> Route_capacity_seed::instantiate(const ph::Technology& tech, const ph::Floorplan& fp,
                                                                 const Route_capacity_grid& grid,
                                                                 const std::function<void()>& checkpoint) const {
  if (checkpoint) {
    checkpoint();
  }
  if (&tech != technology_ || grid != capacity_->grid_ || fp.dbu != signature_.dbu || fp.die != signature_.die
      || fp.rows != signature_.rows || fp.tracks != signature_.tracks || fp.terminals != signature_.terminals
      || fp.vias != signature_.vias || fp.nondefault_rules != signature_.nondefault_rules
      || !ph::same_supply_wiring(fp.special_nets, signature_.special_nets) || fp.components.size() < signature_.components.size()) {
    return nullptr;
  }
  for (size_t i = 0; i < signature_.components.size(); ++i) {
    if (checkpoint && i % 1024 == 0) {
      checkpoint();
    }
    const auto& a = fp.components[i];
    const auto& b = signature_.components[i];
    auto place = a.place;
    // Routing can lock an existing cell without changing its physical shapes.
    if (place.status == ph::Status::UNPLACED) {
      return nullptr;
    }
    place.status = b.place.status;
    if (a.name != b.name || a.macro != b.macro || place != b.place) {
      return nullptr;
    }
  }
  auto result = std::unique_ptr<Route_capacity>(new Route_capacity(*capacity_));
  if (checkpoint) {
    checkpoint();
  }
  ph::Floorplan added;
  added.dbu = fp.dbu;
  added.die = fp.die;
  added.components.assign(fp.components.begin() + signature_.components.size(), fp.components.end());
  const auto obstacles = ph::routing_occupancy(tech, added, fp.dbu, checkpoint, nullptr, ph::Signal_wires::exclude);
  result->add_obstructions(obstacles, checkpoint);
  return result;
}
Route_capacity_trial Route_capacity::precheck(const std::vector<ph::Shape>& shapes) const {
  const auto             count = grid_.bins_x * grid_.bins_y;
  Remaining_route_demand empty{std::vector<double>(count, 0), std::vector<double>(count, 0), 1, 1};
  auto                   trial = check_impl(shapes, empty, false);
  // Zero demand is only a lower bound, never authorization to reserve tracks.
  trial.prechecked             = trial.approved;
  trial.approved               = false;
  return trial;
}
Route_capacity_trial Route_capacity::precheck_demand(const Remaining_route_demand& demand) const {
  return check_impl({}, demand, false, nullptr, true);
}
Route_capacity_trial Route_capacity::check_remaining(const Route_capacity_trial&   preliminary,
                                                     const Remaining_route_demand& demand) const {
  if (preliminary.owner != this || preliminary.epoch != epoch_ || !preliminary.prechecked) {
    throw std::invalid_argument("unapproved or stale capacity precheck");
  }
  return check_impl({}, demand, false, &preliminary);
}
void Route_capacity::reserve_existing(const std::vector<ph::Shape>& shapes) {
  if (shapes.empty()) {
    return;
  }
  const auto                   count = grid_.bins_x * grid_.bins_y;
  const Remaining_route_demand empty{std::vector<double>(count, 0), std::vector<double>(count, 0), 1, 1};
  const auto                   trial = check_impl(shapes, empty, true);
  if (!trial.legal) {
    throw std::invalid_argument("existing pre-route capacity: " + trial.reason);
  }
  commit(trial);
}
Route_capacity_trial Route_capacity::check_impl(const std::vector<ph::Shape>& shapes, const Remaining_route_demand& demand,
                                                bool existing, const Route_capacity_trial* prechecked, bool demand_only) const {
  Route_capacity_trial out;
  out.owner       = this;
  out.epoch       = epoch_;
  const auto fail = [&](const std::string& why) {
    out.reason = why;
    return out;
  };
  const auto count = grid_.bins_x * grid_.bins_y;
  if (demand.horizontal.size() != count || demand.vertical.size() != count || !std::isfinite(demand.horizontal_capacity)
      || !std::isfinite(demand.vertical_capacity) || demand.horizontal_capacity <= 0 || demand.vertical_capacity <= 0) {
    return fail("missing or invalid remaining RUDY demand");
  }
  for (const auto* map : {&demand.horizontal, &demand.vertical}) {
    for (auto v : *map) {
      if (!std::isfinite(v) || v < 0) {
        return fail("invalid remaining RUDY utilization");
      }
    }
  }
  if (demand.blocked_pins != 0 || demand.unroutable_nets != 0) {
    return fail("remaining RSMT has blocked pins or unreachable nets");
  }
  if (!demand.pin_utilization.empty() && demand.pin_utilization.size() != count) {
    return fail("invalid remaining pin utilization grid");
  }
  for (size_t k = 0; k < demand.pin_utilization.size(); ++k) {
    const auto u = demand.pin_utilization[k];
    if (!std::isfinite(u) || u < 0 || u > 1) {
      out.detail = std::format("bin=({},{}) pin_utilization={} maximum_utilization=1", k / grid_.bins_y, k % grid_.bins_y, u);
      return fail("remaining pin access exceeds capacity");
    }
  }
  const double dx = (grid_.bounds.xh - grid_.bounds.xl) / grid_.bins_x, dy = (grid_.bounds.yh - grid_.bounds.yl) / grid_.bins_y;
  const auto&  total_h = total_h_;
  const auto&  total_v = total_v_;
  std::vector<size_t>                 added(cells_.size(), 0);
  std::set<std::pair<size_t, size_t>> additions;
  bool                                signal_shape = prechecked != nullptr;
  if (prechecked) {
    for (auto [cell, track] : prechecked->additions) {
      ++added[cell];
    }
  }
  for (const auto& s : shapes) {
    if (std::find(grid_.layers.begin(), grid_.layers.end(), s.layer) == grid_.layers.end()) {
      if (ignored_layers_.contains(s.layer)) {
        continue;
      }
      return fail("route uses layer outside capacity model");
    }
    signal_shape = true;
    for (auto v : {s.rect.xl, s.rect.yl, s.rect.xh, s.rect.yh}) {
      if (!std::isfinite(v) || v != std::round(v)) {
        return fail("invalid capacity shape");
      }
    }
    if (s.rect.empty() || !grid_.bounds.contains(s.rect)) {
      return fail("capacity shape outside grid");
    }
    bool        touched = false;
    const auto& layer   = layers_.at(s.layer);
    const auto  bin     = [](double v, double lo, double step, size_t n) {
      return size_t(std::clamp(std::floor((v - lo) / step), 0., double(n - 1)));
    };
    const auto xl = bin(s.rect.xl - layer.reach, grid_.bounds.xl, dx, grid_.bins_x);
    const auto xh = bin(s.rect.xh + layer.reach, grid_.bounds.xl, dx, grid_.bins_x);
    const auto yl = bin(s.rect.yl - layer.reach, grid_.bounds.yl, dy, grid_.bins_y);
    const auto yh = bin(s.rect.yh + layer.reach, grid_.bounds.yl, dy, grid_.bins_y);
    for (size_t x = xl; x <= xh; ++x) {
      for (size_t y = yl; y <= yh; ++y) {
        const auto  i    = layer.start + x * grid_.bins_y + y;
        const auto& cell = cells_[i];
        for (size_t t = 0; t < cell.tracks.size(); ++t) {
          const auto& track = cell.tracks[t];
          // The layer reach bounds every supported spacing rule. Coarse bins
          // contain many tracks too far away to interact with this wire.
          if (apart(s.rect, track.footprint.rect, layer.reach)) {
            continue;
          }
          if (rules_.spacing(track.footprint, s).legal) {
            continue;
          }
          touched = true;
          if (!track.usable) {
            if (grid_.count_only_usable_tracks) {
              continue;
            }
            out.detail = std::format("layer={} bin=({},{}) track={} usable_tracks={}", s.layer, x, y, t, cell.report.usable_tracks);
            return fail("route consumes obstructed track capacity");
          }
          if (!track.reserved && additions.emplace(i, t).second) {
            ++added[i];
          }
        }
      }
    }
    if (!touched) {
      return fail("route has no track capacity");
    }
  }
  if (!signal_shape && !demand_only) {
    return fail("route has no signal-layer geometry");
  }
  for (size_t k = 0; k < count; ++k) {
    const auto h = demand.horizontal[k] * demand.horizontal_capacity * dy, v = demand.vertical[k] * demand.vertical_capacity * dx;
    if (!std::isfinite(h) || !std::isfinite(v) || h > total_h[k] + 1e-9 || v > total_v[k] + 1e-9) {
      out.detail = std::format(
          "bin=({},{}) horizontal_demand_tracks={} horizontal_usable_tracks={} vertical_demand_tracks={} vertical_usable_tracks={}",
          k / grid_.bins_y,
          k % grid_.bins_y,
          h,
          total_h[k],
          v,
          total_v[k]);
      return fail("remaining RUDY exceeds usable capacity");
    }
  }
  for (size_t i = 0; i < cells_.size(); ++i) {
    const auto&  c        = cells_[i];
    const auto   k        = c.report.x * grid_.bins_y + c.report.y;
    const double occupied = c.report.reserved_tracks + added[i], usable = c.report.usable_tracks;
    if (!existing && occupied > grid_.max_preroute_fraction * usable + 1e-9) {
      out.detail = std::format("layer={} bin=({},{}) occupied_tracks={} usable_tracks={} maximum_fraction={}",
                               c.report.layer,
                               c.report.x,
                               c.report.y,
                               occupied,
                               usable,
                               grid_.max_preroute_fraction);
      return fail("pre-route capacity fraction");
    }
    const auto total     = (c.horizontal ? total_h : total_v)[k];
    const auto remaining = c.horizontal ? demand.horizontal[k] * demand.horizontal_capacity * dy
                                        : demand.vertical[k] * demand.vertical_capacity * dx;
    // Allocate directional demand proportional to original usable capacity,
    // not leftover capacity; pre-routing cannot move demand out of its layer.
    const auto share     = total > 0 ? remaining * usable / total : 0.;
    if (share + occupied > usable + 1e-9) {
      out.detail = std::format("layer={} bin=({},{}) remaining_demand_tracks={} occupied_tracks={} usable_tracks={}",
                               c.report.layer,
                               c.report.x,
                               c.report.y,
                               share,
                               occupied,
                               usable);
      return fail("remaining RUDY plus pre-route exceeds layer capacity");
    }
  }
  if (prechecked) {
    // The successful precheck already owns a sorted, unique footprint. The
    // reservation epoch guard makes rebuilding its tree set unnecessary.
    out.additions = prechecked->additions;
  } else {
    out.additions.assign(additions.begin(), additions.end());
  }
  out.legal    = true;
  out.approved = !demand_only;
  return out;
}
void Route_capacity::commit(const Route_capacity_trial& trial) {
  if (trial.owner != this || trial.epoch != epoch_ || !trial.approved) {
    throw std::invalid_argument("unapproved or stale capacity trial");
  }
  for (auto [i, t] : trial.additions) {
    cells_[i].tracks[t].reserved = true;
    ++cells_[i].report.reserved_tracks;
  }
  ++epoch_;
}
std::vector<Route_capacity_cell> Route_capacity::report() const {
  std::vector<Route_capacity_cell> result;
  for (const auto& c : cells_) {
    result.push_back(c.report);
  }
  return result;
}
}  // namespace livehd::placement
