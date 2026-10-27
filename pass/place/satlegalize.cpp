// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satlegalize.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>

#include "cell_criticality.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "sat_solver.hpp"
#include "signal_router.hpp"

namespace livehd::placement {
namespace {
namespace ph      = physical;
using Orientation = ph::Orientation;
using Status      = ph::Status;
struct Candidate {
  Ann_place place;
  int       literal    = 0;
  unsigned  recipient  = 0;
  bool      source_gap = false;
};
struct Cell {
  size_t                 index;
  std::vector<Candidate> candidates;
  Ann_place              before;
};
struct Pair {
  Satlegalize_alignment alignment;
  double                slack, distance;
};
ph::Rect bounds(const Ann_place& p) { return {double(p.x), double(p.y), double(p.x + p.width), double(p.y + p.height)}; }
bool     movable(const ph::Technology& tech, const ph::Component& cell) {
  const auto found = tech.macros.find(cell.macro);
  if (cell.place.status == Status::FIXED || cell.place.status == Status::LOCKED || !cell.place.is_valid()
      || found == tech.macros.end() || !found->second.klass.starts_with("CORE")) {
    return false;
  }
  const auto site = tech.sites.find(found->second.site);
  // Multi-height cells need a row-stack model; leave them immutable obstacles.
  return site != tech.sites.end() && std::abs(found->second.height - site->second.height) < 1e-9;
}
std::optional<ph::Rect> pin_access_rect(const ph::Macro& macro, const ph::Shape& shape, const Ann_place& place, int64_t dbu,
                                        const ph::Layer& layer) {
  if (layer.width <= 0 || (layer.direction != "HORIZONTAL" && layer.direction != "VERTICAL")) {
    return {};
  }
  auto         rect   = ph::placed_rect(macro, place, dbu, shape.rect);
  const double half  = layer.width * dbu / 2;
  rect.xl           += half;
  rect.yl           += half;
  rect.xh           -= half;
  rect.yh           -= half;
  return rect.xl <= rect.xh && rect.yl <= rect.yh ? std::optional{rect} : std::nullopt;
}
// Require an actual imported DEF track or explicit LEF OFFSET. A guessed zero
// track origin and coincident pin centers are deliberately insufficient.
std::vector<double> tracks(const ph::Floorplan& fp, const ph::Layer& layer, const std::string& name, bool horizontal, double low,
                           double high) {
  std::vector<double> result;
  const auto          add = [&](double start, double step, int64_t count) {
    if (step <= 0 || low > high || count <= 0) {
      return;
    }
    auto first = std::max<int64_t>(0, static_cast<int64_t>(std::ceil((low - start) / step - 1e-9)));
    auto last  = std::min<int64_t>(count - 1, static_cast<int64_t>(std::floor((high - start) / step + 1e-9)));
    // Access candidates are bounded independently of the width of the pin.
    for (auto i = first; i <= last && result.size() < 32; ++i) {
      result.push_back(start + i * step);
    }
  };
  bool explicit_tracks = false;
  for (const auto& track : fp.tracks) {
    if (track.layer == name && track.axis == (horizontal ? "Y" : "X")) {
      explicit_tracks = true;
      add(double(track.start), double(track.step), track.count);
    }
  }
  if (!explicit_tracks && layer.has_offset) {
    add((horizontal ? layer.offset_y : layer.offset_x) * fp.dbu,
        (horizontal ? layer.pitch_y : layer.pitch_x) * fp.dbu,
        std::numeric_limits<int32_t>::max());
  }
  return result;
}
bool aligned(const ph::Technology& tech, const ph::Floorplan& fp, const Satlegalize_alignment& pair, const Ann_place& a,
             const Ann_place& b, const std::vector<size_t>* obstruction_indexes = nullptr, ph::Shape* witness = nullptr,
             const ph::Signal_metal_rules* route_rules = nullptr) {
  const auto& first  = fp.components.at(pair.first);
  const auto& second = fp.components.at(pair.second);
  const auto& ma     = tech.macros.at(first.macro);
  const auto& mb     = tech.macros.at(second.macro);
  const auto  pa = ma.pins.find(pair.first_pin), pb = mb.pins.find(pair.second_pin);
  if (pa == ma.pins.end() || pb == mb.pins.end()) {
    return false;
  }
  for (const auto& sa : pa->second.shapes) {
    for (const auto& sb : pb->second.shapes) {
      if (sa.layer != sb.layer) {
        continue;
      }
      const auto layer_it = tech.layers.find(sa.layer);
      if (layer_it == tech.layers.end()) {
        continue;
      }
      const auto& layer = layer_it->second;
      if (layer.width <= 0) {
        continue;
      }
      const bool horizontal = layer.direction == "HORIZONTAL";
      if (!horizontal && layer.direction != "VERTICAL") {
        continue;
      }
      const auto access_a = pin_access_rect(ma, sa, a, fp.dbu, layer), access_b = pin_access_rect(mb, sb, b, fp.dbu, layer);
      if (!access_a || !access_b) {
        continue;
      }
      const auto&  ra   = *access_a;
      const auto&  rb   = *access_b;
      const double half = layer.width * fp.dbu / 2;
      const double low  = horizontal ? std::max(ra.yl, rb.yl) : std::max(ra.xl, rb.xl);
      const double high = horizontal ? std::min(ra.yh, rb.yh) : std::min(ra.xh, rb.xh);
      for (const double track : tracks(fp, layer, sa.layer, horizontal, low, high)) {
        ph::Rect corridor = horizontal ? ph::Rect{std::min((ra.xl + ra.xh) / 2, (rb.xl + rb.xh) / 2) - half,
                                                  track - half,
                                                  std::max((ra.xl + ra.xh) / 2, (rb.xl + rb.xh) / 2) + half,
                                                  track + half}
                                       : ph::Rect{track - half,
                                                  std::min((ra.yl + ra.yh) / 2, (rb.yl + rb.yh) / 2) - half,
                                                  track + half,
                                                  std::max((ra.yl + ra.yh) / 2, (rb.yl + rb.yh) / 2) + half};
        if (route_rules && !route_rules->rectangle({sa.layer, corridor}).legal) {
          continue;
        }
        bool blocked = false;
        for (size_t oi = 0; oi < (obstruction_indexes ? obstruction_indexes->size() : fp.components.size()); ++oi) {
          const size_t k         = obstruction_indexes ? (*obstruction_indexes)[oi] : oi;
          const auto&  c         = fp.components[k];
          const auto&  placement = k == pair.first ? a : k == pair.second ? b : c.place;
          if (tech.macros.contains(c.macro)
              && satlegalize_obstructs(tech,
                                       c,
                                       placement,
                                       {sa.layer, corridor},
                                       fp.dbu,
                                       route_rules,
                                       k == pair.first    ? pair.first_pin
                                       : k == pair.second ? pair.second_pin
                                                          : std::string_view{})) {
            blocked = true;
            break;
          }
        }
        if (!blocked) {
          if (witness) {
            *witness = {sa.layer, corridor};
          }
          return true;
        }
      }
    }
  }
  return false;
}
struct Hpwl_endpoint {
  size_t           component;
  double           x, y;
  const ph::Macro* macro;
};
struct Hpwl_index {
  std::vector<std::vector<Hpwl_endpoint>> nets;
  std::vector<std::vector<size_t>>        incident;
  Hpwl_index(const ph::Technology& tech, const ph::Floorplan& fp, const std::vector<bool>& ignored)
      : incident(fp.components.size()) {
    std::map<std::string, size_t>              cells;
    std::map<std::string, const ph::Terminal*> terminals;
    for (size_t i = 0; i < fp.components.size(); ++i) {
      cells[fp.components[i].name] = i;
    }
    for (const auto& t : fp.terminals) {
      terminals[t.name] = &t;
    }
    for (size_t n = 0; n < fp.nets.size(); ++n) {
      if (ignored[n]) {
        continue;
      }
      const auto& net = fp.nets[n];
      nets.emplace_back();
      for (const auto& connection : net.connections) {
        if (connection.instance == "PIN") {
          const auto found = terminals.find(connection.pin);
          if (found != terminals.end()) {
            nets.back().push_back({fp.components.size(), double(found->second->x), double(found->second->y), nullptr});
          }
          continue;
        }
        const auto found = cells.find(connection.instance);
        if (found == cells.end()) {
          continue;
        }
        const auto&   c = fp.components[found->second];
        const auto    m = tech.macros.find(c.macro);
        Hpwl_endpoint endpoint{found->second, c.place.width / 2.0, c.place.height / 2.0, nullptr};
        if (m != tech.macros.end()) {
          const auto p = m->second.pins.find(connection.pin);
          if (p != m->second.pins.end() && !p->second.shapes.empty()) {
            const auto& rect = p->second.shapes.front().rect;
            endpoint.x       = (rect.xl + rect.xh) / 2 + m->second.origin_x;
            endpoint.y       = (rect.yl + rect.yh) / 2 + m->second.origin_y;
            endpoint.macro   = &m->second;
          }
        }
        nets.back().push_back(endpoint);
        incident[found->second].push_back(nets.size() - 1);
      }
    }
  }
};
double hpwl(const ph::Floorplan& fp, const Hpwl_index& index, const std::set<size_t>* selected_nets = nullptr) {
  double     total   = 0;
  const auto measure = [&](size_t n) {
    double xl = INFINITY, yl = INFINITY, xh = -INFINITY, yh = -INFINITY;
    for (const auto& endpoint : index.nets[n]) {
      double x = endpoint.x, y = endpoint.y;
      if (endpoint.component < fp.components.size()) {
        const auto& c = fp.components[endpoint.component];
        if (endpoint.macro) {
          const auto xy = ph::orient_point(x, y, endpoint.macro->width, endpoint.macro->height, c.place.orientation);
          x             = xy.first * fp.dbu;
          y             = xy.second * fp.dbu;
        }
        x += c.place.x;
        y += c.place.y;
      }
      xl = std::min(xl, x);
      yl = std::min(yl, y);
      xh = std::max(xh, x);
      yh = std::max(yh, y);
    }
    return std::isfinite(xl) ? xh - xl + yh - yl : 0;
  };
  if (selected_nets) {
    for (size_t n : *selected_nets) {
      total += measure(n);
    }
  } else {
    for (size_t n = 0; n < index.nets.size(); ++n) {
      total += measure(n);
    }
  }
  return total;
}
std::vector<Pair> pairs(const ph::Technology& tech, const ph::Floorplan& fp, const timing::Feedback& timing,
                        const std::vector<bool>& ignored) {
  std::map<std::string, size_t> indexes;
  for (size_t i = 0; i < fp.components.size(); ++i) {
    indexes[fp.components[i].name] = i;
  }
  std::vector<Pair> result;
  for (size_t n = 0; n < fp.nets.size(); ++n) {
    if (ignored[n]) {
      continue;
    }
    const auto& net       = fp.nets[n];
    const bool  clock_net = std::any_of(net.connections.begin(), net.connections.end(), [&](const auto& connection) {
      return timing.is_clock_pin(connection.instance, connection.pin);
    });
    if (clock_net) {
      continue;
    }
    const ph::Connection* driver = nullptr;
    for (const auto& c : net.connections) {
      auto i = indexes.find(c.instance);
      if (i == indexes.end()) {
        continue;
      }
      auto m = tech.macros.find(fp.components[i->second].macro);
      if (m == tech.macros.end()) {
        continue;
      }
      auto p = m->second.pins.find(c.pin);
      if (p != m->second.pins.end() && p->second.direction == "OUTPUT") {
        if (driver) {
          driver = nullptr;
          break;
        }
        driver = &c;
      }
    }
    if (!driver) {
      continue;
    }
    for (const auto& sink : net.connections) {
      if (&sink == driver || !indexes.contains(sink.instance)) {
        continue;
      }
      size_t a = indexes.at(driver->instance), b = indexes.at(sink.instance);
      if (a == b || !movable(tech, fp.components[a]) || !movable(tech, fp.components[b])) {
        continue;
      }
      const auto t = timing.pin_rank(sink.instance, sink.pin);
      if (!std::isfinite(t.slack) || t.clock) {
        continue;
      }
      const auto& pa = fp.components[a].place;
      const auto& pb = fp.components[b].place;
      result.push_back({
          {a, b, driver->pin, sink.pin},
          t.slack,
          double(std::abs(pa.x - pb.x) + std::abs(pa.y - pb.y))
      });
    }
  }
  std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return std::tie(a.slack, a.distance) < std::tie(b.slack, b.distance);
  });
  return result;
}
bool orientation_allowed(const ph::Macro& macro, Orientation o) {
  return o == Orientation::N || (o == Orientation::FS && macro.symmetry_x) || (o == Orientation::FN && macro.symmetry_y)
         || (o == Orientation::S && macro.symmetry_x && macro.symmetry_y);
}
using Interval = std::pair<double, double>;
double                area(const ph::Rect& r) { return std::max(0., r.xh - r.xl) * std::max(0., r.yh - r.yl); }
// Nonempty intervals only; touching intervals merge.
std::vector<Interval> merged(std::vector<Interval> intervals) {
  std::erase_if(intervals, [](const Interval& interval) { return interval.second <= interval.first; });
  return ph::merge_intervals(std::move(intervals));
}
std::vector<Interval> subtract(const std::vector<Interval>& available, const std::vector<Interval>& blocked) {
  std::vector<Interval> result;
  for (const auto& interval : available) {
    auto low = interval.first;
    for (const auto& obstacle : blocked) {
      if (obstacle.second <= low) {
        continue;
      }
      if (obstacle.first >= interval.second) {
        break;
      }
      if (obstacle.first > low) {
        result.emplace_back(low, std::min(obstacle.first, interval.second));
      }
      low = std::max(low, obstacle.second);
    }
    if (low < interval.second) {
      result.emplace_back(low, interval.second);
    }
  }
  return result;
}
ph::Rect obstacle_bounds(const ph::Technology& tech, const ph::Component& cell, const Satlegalize_options& options) {
  const auto mask = ph::placement_mask(cell, tech);
  if (mask == ph::Placement_mask::soft) {
    return {};
  }
  auto       box   = bounds(cell.place);
  const auto macro = tech.macros.find(cell.macro);
  if (mask == ph::Placement_mask::none && (macro == tech.macros.end() || !macro->second.klass.starts_with("CORE"))) {
    box.xl -= options.macro_halo_x_dbu;
    box.xh += options.macro_halo_x_dbu;
    box.yl -= options.macro_halo_y_dbu;
    box.yh += options.macro_halo_y_dbu;
  }
  return box;
}
struct Route_grid_view {
  const Satlegalize_route_grid& grid;
  std::vector<double>           x, y;
  Route_grid_view(const Satlegalize_route_grid& input, const ph::Rect& die) : grid(input) {
    const auto& b = grid.bounds;
    const auto  valid_coordinate
        = [](double value) { return std::isfinite(value) && std::abs(value) < double(std::numeric_limits<int64_t>::max()) / 4; };
    if (!grid.bins_x || grid.bins_x > 1024 || !grid.bins_y || grid.bins_y > 1024 || !valid_coordinate(b.xl)
        || !valid_coordinate(b.xh) || !valid_coordinate(b.yl) || !valid_coordinate(b.yh) || b.xh <= b.xl || b.yh <= b.yl) {
      throw std::invalid_argument("invalid satlegalize route grid bounds or counts");
    }
    const size_t count = size_t(grid.bins_x) * grid.bins_y;
    for (const auto* values : {&grid.horizontal, &grid.vertical, &grid.pin_utilization}) {
      if (values->size() != count
          || !std::all_of(values->begin(), values->end(), [](double value) { return std::isfinite(value) && value >= 0; })) {
        throw std::invalid_argument("invalid satlegalize route grid utilization");
      }
    }
    const auto boundaries = [](double low, double high, unsigned bins) {
      std::vector<double> result;
      for (unsigned i = 0; i <= bins; ++i) {
        const double boundary = std::llround(low + (high - low) * (double(i) / bins));
        if (!result.empty() && boundary <= result.back()) {
          throw std::invalid_argument("satlegalize route grid bins collapse on the DBU grid");
        }
        result.push_back(boundary);
      }
      return result;
    };
    x = boundaries(b.xl, b.xh, grid.bins_x);
    y = boundaries(b.yl, b.yh, grid.bins_y);
    if (!die.contains({x.front(), y.front(), x.back(), y.back()})) {
      throw std::invalid_argument("satlegalize route grid lies outside the die");
    }
  }
  std::optional<std::pair<int, int>> index(double px, double py) const {
    if (px < x.front() || px >= x.back() || py < y.front() || py >= y.back()) {
      return {};
    }
    return std::pair{int(std::upper_bound(x.begin(), x.end(), px) - x.begin()) - 1,
                     int(std::upper_bound(y.begin(), y.end(), py) - y.begin()) - 1};
  }
  ph::Rect tile(int ix, int iy) const {
    if (ix < 0 || iy < 0 || ix >= int(grid.bins_x) || iy >= int(grid.bins_y)) {
      return {};
    }
    return {x[ix], y[iy], x[ix + 1], y[iy + 1]};
  }
  double utilization(int ix, int iy) const {
    if (ix < 0 || iy < 0 || ix >= int(grid.bins_x) || iy >= int(grid.bins_y)) {
      return 0;
    }
    const auto bin = size_t(ix) * grid.bins_y + iy;
    return std::max({grid.horizontal[bin], grid.vertical[bin], grid.pin_utilization[bin]});
  }
  double utilization(const ph::Rect& tile_bounds) const {
    const auto at = index(tile_bounds.xl, tile_bounds.yl);
    return at && area(tile_bounds) > 0 ? utilization(at->first, at->second) : 0;
  }
};

struct Density_model {
  const ph::Floorplan&                                         fp;
  std::vector<ph::Rect>                                        rows, obstacles;
  std::vector<size_t>                                          movable_cells;
  std::map<std::tuple<double, double, double, double>, double> capacities;
  Density_model(const ph::Technology& tech, const ph::Floorplan& floorplan, const Satlegalize_options& options) : fp(floorplan) {
    for (const auto& row : fp.rows) {
      const auto site = tech.sites.find(row.site);
      if (site != tech.sites.end() && row.step > 0 && row.count > 0 && site->second.height > 0) {
        rows.push_back({double(row.x), double(row.y), double(row.x + row.count * row.step), row.y + site->second.height * fp.dbu});
      }
    }
    for (size_t i = 0; i < fp.components.size(); ++i) {
      if (movable(tech, fp.components[i])) {
        movable_cells.push_back(i);
      } else {
        obstacles.push_back(obstacle_bounds(tech, fp.components[i], options));
      }
    }
  }
  double capacity(const ph::Rect& tile) {
    const auto key = std::tuple(tile.xl, tile.yl, tile.xh, tile.yh);
    if (const auto found = capacities.find(key); found != capacities.end()) {
      return found->second;
    }
    std::vector<ph::Rect> available, blocked;
    std::vector<double>   boundaries;
    for (const auto& row : rows) {
      if (row.overlaps(tile)) {
        available.push_back(row.intersection(tile));
        boundaries.push_back(available.back().yl);
        boundaries.push_back(available.back().yh);
      }
    }
    for (const auto& obstacle : obstacles) {
      if (obstacle.overlaps(tile)) {
        blocked.push_back(obstacle.intersection(tile));
        boundaries.push_back(blocked.back().yl);
        boundaries.push_back(blocked.back().yh);
      }
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    double result = 0;
    for (size_t y = 1; y < boundaries.size(); ++y) {
      std::vector<Interval> a, b;
      const auto            low = boundaries[y - 1], high = boundaries[y];
      for (const auto& row : available) {
        if (row.yl < high && low < row.yh) {
          a.emplace_back(row.xl, row.xh);
        }
      }
      for (const auto& obstacle : blocked) {
        if (obstacle.yl < high && low < obstacle.yh) {
          b.emplace_back(obstacle.xl, obstacle.xh);
        }
      }
      for (const auto& gap : subtract(merged(std::move(a)), merged(std::move(b)))) {
        result += (gap.second - gap.first) * (high - low);
      }
    }
    capacities[key] = result;
    return result;
  }
  double load(const ph::Rect& tile) const {
    double result = 0;
    for (size_t i : movable_cells) {
      result += area(bounds(fp.components[i].place).intersection(tile));
    }
    return result;
  }
};
// Noncritical cells only: fresh positive slack above the margin on every own
// pin, no clock net, and a timed connection.
std::vector<bool> spillable(const ph::Technology& tech, const ph::Floorplan& fp, const Cell_criticality& criticality,
                            const Satlegalize_options& options) {
  std::vector<bool> safe(fp.components.size());
  for (size_t i = 0; i < safe.size(); ++i) {
    safe[i] = criticality.fully_timed(i) && !criticality.clock_connected[i]
              && *criticality.slack_ps[i] > options.spill_slack_margin_ps && movable(tech, fp.components[i]);
  }
  return safe;
}
std::pair<std::string, std::optional<double>> spill_evidence(const Cell_criticality&    criticality,
                                                             const Satlegalize_options& options, size_t cell,
                                                             bool protected_endpoint) {
  const auto  minimum = criticality.timed_units ? criticality.slack_ps[cell] : std::nullopt;
  std::string reason  = !options.spill_neighbors                               ? "disabled"
                        : protected_endpoint                                   ? "protected alignment endpoint"
                        : criticality.clock_connected[cell]                    ? "clock"
                        : !criticality.fully_timed(cell)                       ? "missing-or-nonfinite timing"
                        : minimum && *minimum <= options.spill_slack_margin_ps ? "slack margin"
                                                                               : "eligible";
  return {reason, minimum};
}

struct Row_space {
  const ph::Row*        row;
  double                height;
  std::vector<Interval> gaps;
};
std::vector<Row_space> row_spaces(const ph::Technology& tech, const ph::Floorplan& fp, const ph::Rect& domain,
                                  const std::vector<ph::Rect>& obstacles) {
  std::vector<Row_space> result;
  double                 max_height = 0;
  for (const auto& row : fp.rows) {
    const auto site = tech.sites.find(row.site);
    if (site == tech.sites.end() || row.step <= 0 || row.count <= 0) {
      continue;
    }
    const double height = site->second.height * fp.dbu;
    if (height > 0 && domain.overlaps({double(row.x), double(row.y), double(row.x + row.count * row.step), row.y + height})) {
      result.push_back({&row, height, {}});
      max_height = std::max(max_height, height);
    }
  }
  std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.row->y < b.row->y; });
  std::vector<std::vector<Interval>> blocked(result.size());
  for (const auto& obstacle : obstacles) {
    auto first = std::lower_bound(result.begin(), result.end(), obstacle.yl - max_height, [](const auto& row, double y) {
      return row.row->y < y;
    });
    for (auto row = first; row != result.end() && row->row->y < obstacle.yh; ++row) {
      if (row->row->y + row->height > obstacle.yl) {
        blocked[row - result.begin()].emplace_back(obstacle.xl, obstacle.xh);
      }
    }
  }
  for (size_t i = 0; i < result.size(); ++i) {
    const auto& row = *result[i].row;
    result[i].gaps  = subtract(
        {
            {std::max(double(row.x), domain.xl), std::min(double(row.x + row.count * row.step), domain.xh)}
    },
        merged(std::move(blocked[i])));
  }
  return result;
}
void overlap_clauses(Sat_solver& solver, const std::vector<Cell>& cells) {
  struct Choice {
    size_t           cell;
    const Candidate* candidate;
  };
  std::vector<Choice> choices;
  for (size_t i = 0; i < cells.size(); ++i) {
    for (const auto& candidate : cells[i].candidates) {
      choices.push_back({i, &candidate});
    }
  }
  std::stable_sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) {
    return a.candidate->place.x < b.candidate->place.x;
  });
  using Active = std::multimap<int64_t, Choice>;
  Active                                   active;
  std::multimap<int64_t, Active::iterator> endings;
  int64_t                                  max_height = 0;
  for (const auto& choice : choices) {
    max_height = std::max(max_height, choice.candidate->place.height);
  }
  for (const auto& choice : choices) {
    const auto box = bounds(choice.candidate->place);
    while (!endings.empty() && endings.begin()->first <= box.xl) {
      active.erase(endings.begin()->second);
      endings.erase(endings.begin());
    }
    for (auto previous = active.lower_bound(choice.candidate->place.y - max_height);
         previous != active.end() && previous->first < box.yh;
         ++previous) {
      if (choice.cell != previous->second.cell && box.overlaps(bounds(previous->second.candidate->place))) {
        solver.clause({-choice.candidate->literal, -previous->second.candidate->literal});
      }
    }
    const auto entry = active.emplace(choice.candidate->place.y, choice);
    endings.emplace(choice.candidate->place.x + choice.candidate->place.width, entry);
  }
}

bool same_pose(const Ann_place& a, const Ann_place& b) { return a.x == b.x && a.y == b.y && a.orientation == b.orientation; }
void limit_movement(Sat_solver& solver, const std::vector<Cell>& cells, unsigned maximum) {
  if (!maximum || maximum >= cells.size()) {
    return;
  }
  std::vector<int> previous;
  for (const auto& cell : cells) {
    const auto stay  = std::find_if(cell.candidates.begin(), cell.candidates.end(), [&](const auto& candidate) {
      return same_pose(candidate.place, cell.before);
    });
    int        moved = 0;
    if (stay == cell.candidates.end()) {
      moved = solver.new_variable();
      solver.clause({moved});
    } else {
      moved = -stay->literal;
    }
    // Sequential unary counter: current[j] is implied by at least j+1
    // moved cells. Forbidding the last threshold enforces the whole-region
    // bound, without fixing a subset of cells or shrinking the region.
    std::vector<int> current(std::min<size_t>(previous.size() + 1, maximum + 1));
    for (auto& bit : current) {
      bit = solver.new_variable();
    }
    solver.clause({-moved, current[0]});
    for (size_t j = 0; j < previous.size(); ++j) {
      solver.clause({-previous[j], current[j]});
      if (j + 1 < current.size()) {
        solver.clause({-moved, -previous[j], current[j + 1]});
      }
    }
    previous = std::move(current);
  }
  solver.clause({-previous.back()});
}

void encode_choices(Sat_solver& solver, std::vector<Candidate>& choices, const std::string& encoding) {
  std::vector<int> literals;
  for (auto& c : choices) {
    c.literal = solver.new_variable();
    literals.push_back(c.literal);
  }
  if (encoding == "binary") {
    // Bounded binary DBU coordinates and orientation, rather than a binary
    // candidate index. The selector table admits only enumerated legal sites;
    // pin offsets/obstacles still require the full geometric compatibility
    // relation below (one parity equation cannot express coordinate equality).
    int64_t xmin = choices.front().place.x, ymin = choices.front().place.y;
    int64_t xmax = xmin, ymax = ymin;
    for (const auto& candidate : choices) {
      xmin = std::min(xmin, candidate.place.x);
      xmax = std::max(xmax, candidate.place.x);
      ymin = std::min(ymin, candidate.place.y);
      ymax = std::max(ymax, candidate.place.y);
    }
    std::vector<int> xbits, ybits, obits;
    const auto       make_bits = [&](std::vector<int>& bits, uint64_t maximum) {
      for (; maximum; maximum >>= 1) {
        bits.push_back(solver.new_variable());
      }
    };
    make_bits(xbits, static_cast<uint64_t>(xmax - xmin));
    make_bits(ybits, static_cast<uint64_t>(ymax - ymin));
    make_bits(obits, 7);
    for (const auto& candidate : choices) {
      std::vector<int> clause = {candidate.literal};
      const auto       bind   = [&](const std::vector<int>& bits, uint64_t coordinate) {
        for (size_t b = 0; b < bits.size(); ++b) {
          const int bit = coordinate & (uint64_t(1) << b) ? bits[b] : -bits[b];
          solver.clause({-candidate.literal, bit});
          clause.push_back(-bit);
        }
      };
      bind(xbits, static_cast<uint64_t>(candidate.place.x - xmin));
      bind(ybits, static_cast<uint64_t>(candidate.place.y - ymin));
      bind(obits, static_cast<unsigned>(candidate.place.orientation));
      solver.clause(clause);
    }
    solver.clause(literals);  // Exclude binary codes outside the candidate domain.
  } else {
    for (size_t i = 0; i < literals.size(); ++i) {
      for (size_t j = i + 1; j < literals.size(); ++j) {
        solver.clause({-literals[i], -literals[j]});
      }
    }
    // At-most-one plus odd parity is exactly one, including CMS native XOR.
    solver.parity(literals, true);
  }
}
}  // namespace
Satlegalize_report satlegalize(const ph::Technology& tech, ph::Floorplan& fp, const timing::Feedback& timing,
                               const Satlegalize_options& options) {
  if (!options.max_cells || options.max_cells > 65536 || !options.max_candidates || options.max_candidates > 128
      || !options.max_pairs || options.max_pairs > 1024 || options.max_pairs_per_window > 1024 || options.max_moved_cells > 128
      || !options.max_windows || options.max_windows > 1024 || options.move_sites > 128 || options.move_rows > 16
      || !std::isfinite(options.boundary_growth) || options.boundary_growth < 0 || options.boundary_growth > 1
      || !std::isfinite(options.region_width_um) || !std::isfinite(options.region_height_um) || options.region_width_um < 0
      || options.region_height_um < 0 || (options.region_width_um == 0) != (options.region_height_um == 0)
      || !std::isfinite(options.spill_slack_margin_ps) || options.spill_slack_margin_ps < 0
      || !std::isfinite(options.route_peak_floor) || options.route_peak_floor < 0 || options.route_peak_floor > 1
      || options.macro_halo_x_dbu < 0 || options.macro_halo_y_dbu < 0
      || (options.encoding != "candidate" && options.encoding != "binary")) {
    throw std::invalid_argument("invalid satlegalize limits");
  }
  const bool                     region       = options.region_width_um > 0;
  const bool                     fixed_region = region || options.route_grid;
  const bool                     advanced     = fixed_region || options.spill_neighbors;
  std::optional<Route_grid_view> routing;
  if (options.route_grid) {
    if (!options.routing || region) {
      throw std::invalid_argument("satlegalize route grid requires a snapshot and excludes explicit region size");
    }
    routing.emplace(*options.routing, fp.die);
  }
  const auto         backend = sat_backend(options.backend);
  const auto         start   = std::chrono::steady_clock::now();
  Satlegalize_report report;
  for (const auto& cell : fp.components) {
    report.before.push_back(cell.place);
  }
  const auto                   excluded    = prects_networks(tech, fp, options.prects, timing);
  const auto                   data_timing = prects_data_timing(timing, excluded);
  const Hpwl_index             hpwl_index(tech, fp, excluded.ignored);
  std::optional<Density_model>    density;
  std::vector<bool>               safe_spill;
  std::optional<Cell_criticality> evidence_timing;  // unfiltered, diagnostic only
  if (advanced) {
    for (const auto& row : fp.rows) {
      const auto site = tech.sites.find(row.site);
      if (site != tech.sites.end() && row.step > 0 && row.count > 0 && std::abs(row.step - site->second.width * fp.dbu) > 1e-6) {
        throw std::invalid_argument("satlegalize regions require contiguous row sites");
      }
    }
    density.emplace(tech, fp, options);
    if (options.spill_neighbors) {
      safe_spill = spillable(tech, fp, Cell_criticality::from(data_timing, fp), options);
    }
  }
  report.hpwl_before    = hpwl(fp, hpwl_index);
  const auto targets    = pairs(tech, fp, data_timing, excluded.ignored);
  const auto window_for = [&](size_t target) -> std::optional<std::pair<ph::Rect, ph::Rect>> {
    const auto& pair   = targets[target].alignment;
    const auto& first  = fp.components[pair.first];
    const auto& second = fp.components[pair.second];
    const auto  site   = tech.sites.find(tech.macros.at(first.macro).site);
    if (site == tech.sites.end()) {
      return {};
    }
    const double sx = site->second.width * fp.dbu, sy = site->second.height * fp.dbu;
    if (sx <= 0 || sy <= 0 || (!fixed_region && std::abs(first.place.x - second.place.x) > (2 * options.move_sites + 2) * sx)
        || (!options.route_grid && std::abs(first.place.y - second.place.y) > (2 * options.move_rows + 2) * sy)) {
      return {};
    }
    ph::Rect raw;
    if (routing) {
      const auto at = routing->index(first.place.x, first.place.y);
      if (!at) {
        return {};
      }
      raw = routing->tile(at->first, at->second);
    } else if (region) {
      const double width = options.region_width_um * fp.dbu, height = options.region_height_um * fp.dbu;
      if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
        return {};
      }
      const double x = fp.die.xl + std::floor((first.place.x - fp.die.xl) / width) * width;
      const double y = fp.die.yl + std::floor((first.place.y - fp.die.yl) / height) * height;
      raw            = {x, y, x + width, y + height};
    } else {
      raw = {double(std::min(first.place.x, second.place.x)) - options.move_sites * sx,
             double(std::min(first.place.y, second.place.y)) - options.move_rows * sy,
             double(std::max(first.place.x + first.place.width, second.place.x + second.place.width)) + options.move_sites * sx,
             double(std::max(first.place.y + first.place.height, second.place.y + second.place.height)) + options.move_rows * sy};
    }
    const auto window = raw.intersection(fp.die);
    if (area(window) <= 0 || (fixed_region && (!window.contains(bounds(first.place)) || !window.contains(bounds(second.place))))) {
      return {};
    }
    return std::pair{raw, window};
  };
  std::vector<size_t> seeds;
  if (advanced) {
    std::set<std::tuple<double, double, double, double>> unique;
    for (size_t target = 0; target < targets.size() && seeds.size() < options.max_pairs; ++target) {
      const auto geometry = window_for(target);
      if (options.route_grid && !geometry) {
        ++report.cross_grid_pairs;
      }
      if (geometry && unique.emplace(geometry->first.xl, geometry->first.yl, geometry->first.xh, geometry->first.yh).second) {
        seeds.push_back(target);
      }
    }
    report.sampled_windows = seeds.size();
  }
  std::set<size_t> tried, visited;
  const size_t     seed_count = advanced ? seeds.size() : targets.size();
  for (size_t cursor = 0; cursor < seed_count && report.windows < options.max_windows && report.trials < options.max_pairs;
       ++cursor) {
    size_t target = cursor;
    if (advanced) {
      double densest = -1, hottest = -1;
      target = targets.size();
      for (size_t seed : seeds) {
        if (tried.contains(seed) || visited.contains(seed)) {
          continue;
        }
        const auto geometry = window_for(seed);
        if (!geometry) {
          continue;
        }
        const auto   capacity    = density->capacity(geometry->second);
        const double ratio       = capacity > 0 ? density->load(geometry->second) / capacity : 0;
        const double utilization = routing && options.route_guidance ? routing->utilization(geometry->second) : 0;
        if (utilization > hottest || (utilization == hottest && ratio > densest)) {
          hottest = utilization;
          densest = ratio;
          target  = seed;
        }
      }
      if (target == targets.size()) {
        break;
      }
      visited.insert(target);
    }
    if (tried.contains(target)) {
      continue;
    }
    const auto geometry = window_for(target);
    if (!geometry) {
      continue;
    }
    const auto   raw = geometry->first, window = geometry->second;
    const auto   pair = targets[target].alignment;
    const auto&  site = tech.sites.at(tech.macros.at(fp.components[pair.first].macro).site);
    const double sx = site.width * fp.dbu, sy = site.height * fp.dbu;
    ++report.windows;
    Satlegalize_window stats;
    stats.bounds              = window;
    stats.route_utilization   = routing ? routing->utilization(window) : 0;
    stats.usable_area         = advanced ? density->capacity(window) : 0;
    stats.movable_area_before = advanced ? density->load(window) : 0;
    stats.movable_area_after  = stats.movable_area_before;
    stats.density_before      = stats.usable_area > 0 ? stats.movable_area_before / stats.usable_area : 0;
    stats.density_after       = stats.density_before;
    report.window_stats.push_back(stats);
    auto&            window_stats = report.window_stats.back();
    std::set<size_t> protected_cells{pair.first, pair.second};
    for (const auto& accepted : report.accepted) {
      protected_cells.insert(accepted.first);
      protected_cells.insert(accepted.second);
    }
    std::vector<Cell> cells;
    std::set<size_t>  selected, source_contained;
    for (size_t i = 0; i < fp.components.size(); ++i) {
      if (movable(tech, fp.components[i]) && bounds(fp.components[i].place).overlaps(window)) {
        ++window_stats.movable_cells;
        if (options.spill_neighbors && safe_spill[i] && !protected_cells.contains(i)) {
          ++window_stats.spill_eligible_cells;
        }
        if (cells.size() <= options.max_cells) {
          cells.push_back({i, {}, fp.components[i].place});
          selected.insert(i);
          if (options.route_grid && window.contains(bounds(fp.components[i].place))) {
            source_contained.insert(i);
          }
        }
      }
    }
    if (window_stats.movable_cells > options.max_cells) {
      window_stats.oversized = true;
      ++report.oversized;
      continue;
    }
    if (!selected.contains(pair.first) || !selected.contains(pair.second)) {
      continue;
    }
    std::array<ph::Rect, 9> tiles;
    tiles[0]                = window;
    unsigned     tile_count = 1;
    const double width = raw.xh - raw.xl, height = raw.yh - raw.yl;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx || dy) {
          if (routing) {
            const auto at       = routing->index(raw.xl, raw.yl);
            tiles[tile_count++] = routing->tile(at->first + dx, at->second + dy);
          } else {
            const ph::Rect shifted{raw.xl + dx * width, raw.yl + dy * height, raw.xh + dx * width, raw.yh + dy * height};
            tiles[tile_count++] = shifted.intersection(fp.die);
          }
        }
      }
    }
    std::array<double, 9> capacities{}, initial_loads{};
    for (size_t i = 0; i < tiles.size(); ++i) {
      capacities[i]    = advanced ? density->capacity(tiles[i]) : 0;
      initial_loads[i] = advanced ? density->load(tiles[i]) : 0;
      if (advanced && i) {
        Satlegalize_density_tile neighbor;
        neighbor.bounds              = tiles[i];
        neighbor.route_utilization   = routing ? routing->utilization(tiles[i]) : 0;
        neighbor.usable_area         = capacities[i];
        neighbor.movable_area_before = neighbor.movable_area_after = initial_loads[i];
        neighbor.density_before = neighbor.density_after = capacities[i] > 0 ? initial_loads[i] / capacities[i] : 0;
        for (size_t c : density->movable_cells) {
          neighbor.movable_cells += bounds(fp.components[c].place).overlaps(tiles[i]);
        }
        window_stats.neighbors.push_back(neighbor);
      }
    }
    const auto owner = [&](double x, double y) {
      for (size_t i = 0; i < tiles.size(); ++i) {
        if (x >= tiles[i].xl && x < tiles[i].xh && y >= tiles[i].yl && y < tiles[i].yh) {
          return i;
        }
      }
      return tiles.size();
    };
    std::set<size_t> incident_nets;
    for (size_t c : selected) {
      for (size_t n : hpwl_index.incident[c]) {
        incident_nets.insert(n);
      }
    }
    // Every outside cell is immutable throughout the enlarged search domain.
    auto domain
        = advanced ? ph::Rect{raw.xl - width, raw.yl - height, raw.xh + width, raw.yh + height}.intersection(fp.die) : window;
    if (routing) {
      domain = window;
      for (const auto& tile : tiles) {
        if (area(tile) > 0) {
          domain = domain.hull(tile);
        }
      }
    }
    std::vector<ph::Rect> obstacles;
    std::vector<size_t>   obstruction_indexes;
    for (size_t i = 0; i < fp.components.size(); ++i) {
      if (ph::placement_mask(fp.components[i], tech) == ph::Placement_mask::soft) {
        continue;
      }
      const auto box = advanced ? obstacle_bounds(tech, fp.components[i], options) : bounds(fp.components[i].place);
      if (!box.overlaps(domain)) {
        continue;
      }
      if (!advanced || !selected.contains(i)) {
        obstruction_indexes.push_back(i);
      }
      if (!selected.contains(i)) {
        obstacles.push_back(box);
      }
    }
    const auto spaces     = advanced ? row_spaces(tech, fp, domain, obstacles) : std::vector<Row_space>{};
    bool       impossible = false;
    Sat_solver solver(backend);
    for (auto& cell : cells) {
      ++window_stats.candidate_cells;
      const auto& original = fp.components[cell.index];
      const auto& macro    = tech.macros.at(original.macro);
      const auto  distance = [&](const Candidate& candidate) {
        return std::abs(candidate.place.x - original.place.x) + std::abs(candidate.place.y - original.place.y);
      };
      if (!advanced) {
        // Deliberately retain the legacy enumeration and truncation ordering.
        for (const auto& row : fp.rows) {
          if (row.site != macro.site || row.step <= 0 || row.count <= 0 || !orientation_allowed(macro, row.orientation)
              || std::abs(row.y - original.place.y) > options.move_rows * sy + sy / 2 || row.y < window.yl
              || row.y + original.place.height > window.yh) {
            continue;
          }
          const int64_t nearest = std::llround(double(original.place.x - row.x) / row.step);
          for (int64_t offset = -static_cast<int64_t>(options.move_sites); offset <= static_cast<int64_t>(options.move_sites);
               ++offset) {
            const auto slot = nearest + offset;
            if (slot < 0 || slot >= row.count) {
              continue;
            }
            auto place        = original.place;
            place.x           = row.x + slot * row.step;
            place.y           = row.y;
            place.orientation = row.orientation;
            place.status      = Status::PLACED;
            if (std::abs(place.x - original.place.x) > (options.move_sites + .5) * sx || place.x < window.xl
                || place.x + place.width > window.xh || place.x + place.width > row.x + row.count * row.step) {
              continue;
            }
            const auto box = bounds(place);
            if (std::any_of(obstacles.begin(), obstacles.end(), [&](const auto& obstacle) { return box.overlaps(obstacle); })) {
              continue;
            }
            cell.candidates.push_back({place, 0});
          }
        }
        std::stable_sort(cell.candidates.begin(), cell.candidates.end(), [&](const auto& a, const auto& b) {
          return distance(a) < distance(b);
        });
        if (cell.candidates.size() > options.max_candidates) {
          cell.candidates.resize(options.max_candidates);
        }
      } else {
        std::array<std::vector<Candidate>, 9> choices;
        const auto                            add = [&](Candidate candidate, unsigned recipient) {
          candidate.recipient = recipient;
          auto& list          = choices[recipient];
          if (std::any_of(list.begin(), list.end(), [&](const auto& entry) {
                return entry.place.x == candidate.place.x && entry.place.y == candidate.place.y
                       && entry.place.orientation == candidate.place.orientation;
              })) {
            return;
          }
          const auto found = std::upper_bound(list.begin(), list.end(), distance(candidate), [&](auto cost, const auto& entry) {
            return cost < distance(entry);
          });
          list.insert(found, candidate);
          if (list.size() > options.max_candidates) {
            list.pop_back();
          }
        };
        const size_t original_owner
            = owner(original.place.x + original.place.width / 2., original.place.y + original.place.height / 2.);
        const bool straddles = !window.contains(bounds(original.place));
        const bool may_spill = options.spill_neighbors && safe_spill[cell.index] && !protected_cells.contains(cell.index);
        for (const auto& space : spaces) {
          const auto& row = *space.row;
          if (row.site != macro.site || !orientation_allowed(macro, row.orientation)) {
            continue;
          }
          const bool local_row = std::abs(row.y - original.place.y) <= options.move_rows * sy + sy / 2;
          if (local_row) {
            const int64_t nearest = std::llround(double(original.place.x - row.x) / row.step);
            for (int64_t offset = -static_cast<int64_t>(options.move_sites); offset <= static_cast<int64_t>(options.move_sites);
                 ++offset) {
              const auto slot       = nearest + offset;
              auto       place      = original.place;
              place.x               = row.x + slot * row.step;
              place.y               = row.y;
              place.orientation     = row.orientation;
              place.status          = Status::PLACED;
              const auto box        = bounds(place);
              const bool same_owner = straddles && original_owner < tiles.size()
                                      && owner(place.x + place.width / 2., place.y + place.height / 2.) == original_owner;
              if (slot < 0 || slot >= row.count || !fp.die.contains(box) || (!window.contains(box) && !same_owner)
                  || std::abs(place.x - original.place.x) > (options.move_sites + .5) * sx
                  || !std::any_of(space.gaps.begin(), space.gaps.end(), [&](const auto& gap) {
                       return box.xl >= gap.first && box.xh <= gap.second;
                     })) {
                continue;
              }
              add({place, 0}, 0);
            }
          }
          if (!may_spill) {
            continue;
          }
          for (unsigned recipient = 1; recipient < tiles.size(); ++recipient) {
            const auto& tile = tiles[recipient];
            if (capacities[0] <= 0 || capacities[recipient] <= 0
                || initial_loads[recipient] / capacities[recipient] >= initial_loads[0] / capacities[0]
                || (routing && options.route_guidance && routing->utilization(tile) >= routing->utilization(window))
                || row.y < tile.yl || row.y + original.place.height > tile.yh) {
              continue;
            }
            // Free intervals provide bounded escape choices, never a site-by-row radius scan.
            for (const auto& gap : space.gaps) {
              const double low = std::max(gap.first, tile.xl), high = std::min(gap.second, tile.xh) - original.place.width;
              const auto   first_slot = static_cast<int64_t>(std::ceil((low - row.x) / row.step - 1e-9));
              const auto   last_slot  = static_cast<int64_t>(std::floor((high - row.x) / row.step + 1e-9));
              if (low > high || first_slot > last_slot) {
                continue;
              }
              const auto closest
                  = std::clamp<int64_t>(std::llround(double(original.place.x - row.x) / row.step), first_slot, last_slot);
              const auto stride = std::max<int64_t>(1, (original.place.width + row.step - 1) / row.step);
              for (unsigned step = 0; step < options.max_candidates; ++step) {
                bool competitive = false;
                for (int direction : {-1, 1}) {
                  if (!step && direction == 1) {
                    continue;
                  }
                  const auto slot = closest + direction * static_cast<int64_t>(step) * stride;
                  if (slot < first_slot || slot > last_slot) {
                    continue;
                  }
                  auto place        = original.place;
                  place.x           = row.x + slot * row.step;
                  place.y           = row.y;
                  place.orientation = row.orientation;
                  place.status      = Status::PLACED;
                  Candidate candidate{place, 0, recipient};
                  if (choices[recipient].size() < options.max_candidates
                      || distance(candidate) <= distance(choices[recipient].back())) {
                    add(candidate, recipient);
                    competitive = true;
                  }
                }
                if (!competitive && choices[recipient].size() >= options.max_candidates) {
                  break;
                }
              }
            }
          }
        }
        std::vector<Candidate> source_choices;
        if (options.route_grid) {
          auto local_choices = std::move(choices[0]);
          choices[0].clear();
          std::set<std::tuple<int64_t, int64_t, Orientation>> local_positions;
          for (const auto& candidate : local_choices) {
            local_positions.emplace(candidate.place.x, candidate.place.y, candidate.place.orientation);
          }
          for (const auto& space : spaces) {
            const auto& row = *space.row;
            if (row.site != macro.site || !orientation_allowed(macro, row.orientation) || row.y < window.yl
                || row.y + original.place.height > window.yh) {
              continue;
            }
            for (const auto& gap : space.gaps) {
              const double low = std::max(gap.first, window.xl), high = std::min(gap.second, window.xh) - original.place.width;
              if (low > high) {
                continue;
              }
              const auto first_slot = static_cast<int64_t>(std::ceil((low - row.x) / row.step - 1e-9));
              const auto last_slot  = static_cast<int64_t>(std::floor((high - row.x) / row.step + 1e-9));
              if (first_slot > last_slot) {
                continue;
              }
              const auto nearest
                  = std::clamp<int64_t>(std::llround(double(original.place.x - row.x) / row.step), first_slot, last_slot);
              const auto stride = std::max<int64_t>(1, (original.place.width + row.step - 1) / row.step);
              for (unsigned step = 0; step < options.max_candidates; ++step) {
                for (int direction : {-1, 1}) {
                  if (!step && direction == 1) {
                    continue;
                  }
                  const auto slot = nearest + direction * static_cast<int64_t>(step) * stride;
                  if (slot < first_slot || slot > last_slot) {
                    continue;
                  }
                  auto place        = original.place;
                  place.x           = row.x + slot * row.step;
                  place.y           = row.y;
                  place.orientation = row.orientation;
                  place.status      = Status::PLACED;
                  if (!local_positions.contains({place.x, place.y, place.orientation})) {
                    add({place, 0, 0, true}, 0);
                  }
                }
              }
            }
          }
          source_choices = std::move(choices[0]);
          choices[0]     = std::move(local_choices);
        }
        if (options.route_grid) {
          const auto reserve = [&](const Candidate& candidate) {
            if (cell.candidates.size() < options.max_candidates
                && std::none_of(cell.candidates.begin(), cell.candidates.end(), [&](const auto& entry) {
                     return entry.place.x == candidate.place.x && entry.place.y == candidate.place.y
                            && entry.place.orientation == candidate.place.orientation;
                   })) {
              cell.candidates.push_back(candidate);
            }
          };
          if (!choices[0].empty()) {
            reserve(choices[0].front());
          }
          const Candidate* escape = nullptr;
          for (unsigned recipient = 1; recipient < choices.size(); ++recipient) {
            if (!choices[recipient].empty() && (!escape || distance(choices[recipient].front()) < distance(*escape))) {
              escape = &choices[recipient].front();
            }
          }
          if (escape) {
            reserve(*escape);
          }
          if (!source_choices.empty()) {
            reserve(source_choices.front());
          }
          for (unsigned recipient = 1; recipient < choices.size(); ++recipient) {
            if (!choices[recipient].empty()) {
              reserve(choices[recipient].front());
            }
          }
          std::vector<Candidate> remaining = source_choices;
          for (const auto& list : choices) {
            remaining.insert(remaining.end(), list.begin(), list.end());
          }
          std::stable_sort(remaining.begin(), remaining.end(), [&](const auto& a, const auto& b) {
            return distance(a) < distance(b);
          });
          for (const auto& candidate : remaining) {
            reserve(candidate);
          }
        } else {
          // Reserve a stay and a choice in each available recipient before filling by distance.
          for (unsigned recipient = 0; recipient < choices.size() && cell.candidates.size() < options.max_candidates; ++recipient) {
            if (!choices[recipient].empty()) {
              cell.candidates.push_back(choices[recipient].front());
              choices[recipient].erase(choices[recipient].begin());
            }
          }
          std::vector<Candidate> rest;
          for (const auto& list : choices) {
            rest.insert(rest.end(), list.begin(), list.end());
          }
          std::stable_sort(rest.begin(), rest.end(), [&](const auto& a, const auto& b) { return distance(a) < distance(b); });
          for (const auto& candidate : rest) {
            if (cell.candidates.size() == options.max_candidates) {
              break;
            }
            cell.candidates.push_back(candidate);
          }
        }
        bool has_source_gap = false;
        for (const auto& candidate : cell.candidates) {
          if (candidate.source_gap) {
            ++window_stats.source_gap_candidates;
            has_source_gap = true;
          }
          if (candidate.recipient) {
            ++window_stats.spill_candidates;
            ++report.spill_candidates;
          }
        }
        window_stats.source_gap_cells += has_source_gap;
      }
      window_stats.retained_candidates += cell.candidates.size();
      if (cell.candidates.empty()) {
        window_stats.first_no_candidate_cell   = original.name;
        window_stats.first_no_candidate_master = original.macro;
        window_stats.first_no_candidate_place  = original.place;
        if (options.spill_neighbors) {
          if (!evidence_timing) {
            evidence_timing = Cell_criticality::from(timing, fp);
          }
          auto evidence = spill_evidence(*evidence_timing, options, cell.index, protected_cells.contains(cell.index));
          window_stats.first_no_candidate_spill_reason = std::move(evidence.first);
          window_stats.first_no_candidate_slack_ps     = evidence.second;
        } else {
          window_stats.first_no_candidate_spill_reason = "disabled";
        }
        impossible = true;
        break;
      }
      encode_choices(solver, cell.candidates, options.encoding);
    }
    if (impossible) {
      continue;
    }
    limit_movement(solver, cells, options.max_moved_cells);
    if (advanced) {
      overlap_clauses(solver, cells);
    } else {
      for (size_t a = 0; a < cells.size(); ++a) {
        for (size_t b = a + 1; b < cells.size(); ++b) {
          for (const auto& ca : cells[a].candidates) {
            for (const auto& cb : cells[b].candidates) {
              if (bounds(ca.place).overlaps(bounds(cb.place))) {
                solver.clause({-ca.literal, -cb.literal});
              }
            }
          }
        }
      }
    }
    if (options.check_packing) {
      // This checks only the enumerated legal sites and non-overlap clauses.
      // Do not install its model: spill density, pin access, retained
      // alignments and the caller's timing guards still require validation.
      const auto status           = solver.solve({}, options.conflicts, options.milliseconds);
      window_stats.packing_status = status == Sat_status::satisfiable     ? "sat"
                                    : status == Sat_status::unsatisfiable ? "unsat"
                                                                          : "unknown";
      if (status != Sat_status::satisfiable) {
        // An exhausted probe budget is unknown, never a proof of infeasibility.
        continue;
      }
    }
    std::vector<int> accepted;
    for (size_t p = advanced ? 0 : target; p < targets.size() && report.trials < options.max_pairs
                                           && (!options.max_pairs_per_window || window_stats.trials < options.max_pairs_per_window);
         ++p) {
      const auto& proposed = targets[p].alignment;
      if (tried.contains(p) || !selected.contains(proposed.first) || !selected.contains(proposed.second)
          || (options.route_grid && (!source_contained.contains(proposed.first) || !source_contained.contains(proposed.second)))) {
        continue;
      }
      // Classify the complete geometry at this trial's entry. Earlier accepted
      // trials may already have created this corridor; do not count that as a
      // new alignment caused by this proposal.
      const bool already_aligned
          = aligned(tech, fp, proposed, fp.components[proposed.first].place, fp.components[proposed.second].place);
      tried.insert(p);
      ++report.trials;
      ++window_stats.trials;
      const auto ia         = std::find_if(cells.begin(), cells.end(), [&](const auto& c) { return c.index == proposed.first; });
      const auto ib         = std::find_if(cells.begin(), cells.end(), [&](const auto& c) { return c.index == proposed.second; });
      const int  activation = solver.new_variable();
      for (const auto* cell : {&*ia, &*ib}) {
        for (const auto& candidate : cell->candidates) {
          if (candidate.recipient || (options.route_grid && !window.contains(bounds(candidate.place)))) {
            solver.clause({-activation, -candidate.literal});
          }
        }
      }
      auto alignment_obstacles = obstruction_indexes;
      if (advanced) {
        // Other selected OBS poses are decision variables. Do not freeze their
        // GP poses into compatibility clauses; complete actual-pose validation
        // below rejects and blocks a model if its chosen poses obstruct access.
        alignment_obstacles.push_back(proposed.first);
        alignment_obstacles.push_back(proposed.second);
      }
      for (const auto& a : ia->candidates) {
        for (const auto& b : ib->candidates) {
          if (!aligned(tech, fp, proposed, a.place, b.place, &alignment_obstacles)) {
            solver.clause({-activation, -a.literal, -b.literal});
          }
        }
      }
      auto assumptions = accepted;
      assumptions.push_back(activation);
      bool installed = false;
      for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const auto status = solver.solve(assumptions, options.conflicts, options.milliseconds);
        if (status == Sat_status::unknown) {
          ++report.unknown;
          ++window_stats.unknown;
          break;
        }
        if (status == Sat_status::unsatisfiable) {
          ++report.conflicts;
          ++window_stats.conflicts;
          break;
        }
        Satlegalize_checkpoint checkpoint;
        std::vector<int>       block       = {-activation};
        bool                   uses_escape = false;
        std::array<bool, 9>    used_recipients{};
        for (const auto& cell : cells) {
          for (const auto& candidate : cell.candidates) {
            if (solver.value(candidate.literal)) {
              checkpoint.positions.emplace_back(cell.index, candidate.place);
              block.push_back(-candidate.literal);
              uses_escape = uses_escape || candidate.recipient;
              if (candidate.recipient) {
                used_recipients[candidate.recipient] = true;
              }
              break;
            }
          }
        }
        const double          old_hpwl = hpwl(fp, hpwl_index, &incident_nets);
        std::array<double, 9> before_loads{}, after_loads{};
        if (options.spill_neighbors) {
          for (size_t i = 0; i < tiles.size(); ++i) {
            before_loads[i] = density->load(tiles[i]);
          }
        }
        std::vector<std::pair<size_t, Ann_place>> saved;
        unsigned                                  transfers = 0;
        for (const auto& [i, place] : checkpoint.positions) {
          saved.emplace_back(i, fp.components[i].place);
          if (bounds(fp.components[i].place).overlaps(window) && !bounds(place).overlaps(window)) {
            ++transfers;
          }
          fp.components[i].place = place;
        }
        bool density_valid = true;
        if (options.spill_neighbors) {
          bool receiving = false;
          for (size_t i = 0; i < tiles.size(); ++i) {
            after_loads[i] = density->load(tiles[i]);
            if (i && (used_recipients[i] || after_loads[i] > before_loads[i] + 1e-6)) {
              receiving = true;
              if (capacities[i] <= 0 || capacities[0] <= 0
                  || (routing && options.route_guidance && routing->utilization(tiles[i]) >= routing->utilization(window))
                  || after_loads[i] > capacities[i] + 1e-6
                  || after_loads[i] / capacities[i] >= before_loads[0] / capacities[0] - 1e-12) {
                density_valid = false;
              }
            }
          }
          if ((uses_escape || receiving) && after_loads[0] >= before_loads[0] - 1e-6) {
            density_valid = false;
          }
        }
        const double new_hpwl = hpwl(fp, hpwl_index, &incident_nets);
        const bool   valid    = density_valid && new_hpwl <= old_hpwl * (1 + options.boundary_growth) + 1
                           && aligned(tech, fp, proposed, fp.components[proposed.first].place, fp.components[proposed.second].place)
                           && satlegalize_preserved(tech, fp, report);
        if (!valid) {
          ++report.boundary_rejected;
          if (!density_valid) {
            ++report.density_rejected;
          }
          for (const auto& [i, place] : saved) {
            fp.components[i].place = place;
          }
          solver.clause(block);
          continue;
        }
        accepted.push_back(activation);
        report.accepted.push_back(proposed);
        ++window_stats.accepted;
        window_stats.already_aligned += already_aligned;
        report.already_aligned       += already_aligned;
        report.checkpoints.push_back(std::move(checkpoint));
        report.spilled_cells += options.spill_neighbors ? transfers : 0;
        installed             = true;
        break;
      }
      if (!installed) {
        solver.clause({-activation});
        if (!options.skip_conflicts) {
          break;
        }
      }
    }
    for (auto& neighbor : window_stats.neighbors) {
      neighbor.movable_area_after = density->load(neighbor.bounds);
      neighbor.density_after      = neighbor.usable_area > 0 ? neighbor.movable_area_after / neighbor.usable_area : 0;
    }
    window_stats.movable_area_after = advanced ? density->load(window) : 0;
    window_stats.density_after      = window_stats.usable_area > 0 ? window_stats.movable_area_after / window_stats.usable_area : 0;
    for (const auto& cell : cells) {
      const auto& place = fp.components[cell.index].place;
      if (!same_pose(place, cell.before)) {
        ++window_stats.moved_cells;
        const double distance              = std::abs(double(place.x) - cell.before.x) + std::abs(double(place.y) - cell.before.y);
        window_stats.displacement_dbu     += distance;
        window_stats.max_displacement_dbu  = std::max(window_stats.max_displacement_dbu, distance);
      }
      if (!bounds(fp.components[cell.index].place).overlaps(window)) {
        ++window_stats.spilled_cells;
      }
    }
  }
  report.hpwl_after = hpwl(fp, hpwl_index);
  report.seconds    = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  return report;
}

bool satlegalize_route_preserved(const Satlegalize_route_quality& candidate, const Satlegalize_route_quality& baseline,
                                 double peak_floor) {
  return candidate.overflow <= baseline.overflow + 1e-9
         && std::max(candidate.peak, peak_floor) <= std::max(baseline.peak, peak_floor) + 1e-9
         && candidate.blocked_pins <= baseline.blocked_pins && candidate.unroutable_nets <= baseline.unroutable_nets;
}

std::vector<physical::Shape> satlegalize_pin_access_bounds(const physical::Technology& tech, const physical::Component& cell,
                                                           const Ann_place& place, const std::string& pin, int64_t dbu,
                                                           const physical::Tech_profile* via_profile) {
  std::vector<physical::Shape> result;
  const auto&                  macro = tech.macros.at(cell.macro);
  const auto                   found = macro.pins.find(pin);
  if (found == macro.pins.end()) {
    return result;
  }
  for (const auto& shape : found->second.shapes) {
    const auto layer = tech.layers.find(shape.layer);
    if (layer != tech.layers.end()) {
      if (auto rect = pin_access_rect(macro, shape, place, dbu, layer->second)) {
        result.push_back({shape.layer, *rect});
      }
    }
    // A broad-phase bound only: the via oracle verifies full pin enclosure
    // and literal escape tracks for every surviving pair of candidate poses.
    if (via_profile && shape.layer == via_profile->route.access.pin && tech.layers.contains(via_profile->route.access.escape)) {
      if (auto rect = pin_access_rect(macro, shape, place, dbu, tech.layers.at(via_profile->route.access.escape))) {
        result.push_back({via_profile->route.access.escape, *rect});
      }
    }
  }
  return result;
}

bool satlegalize_aligned(const physical::Technology& tech, const physical::Floorplan& fp, const Satlegalize_alignment& pair,
                         const Ann_place& a, const Ann_place& b, const std::vector<size_t>& obstacles, physical::Shape* witness,
                         const physical::Signal_metal_rules* route_rules, const physical::Tech_profile* via_profile) {
  if (via_profile && !route_rules) {
    throw std::invalid_argument("SAT via access requires the routing rule oracle");
  }
  if (aligned(tech, fp, pair, a, b, &obstacles, witness, route_rules)) {
    return true;
  }
  if (!via_profile) {
    return false;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
  // Reject impossible common-track candidates before copying the floorplan
  // and indexing its obstructions. Use the whole pin union's bounds, without
  // an enclosure inset: adjacent rectangles can jointly cover a via pad.
  const auto pin_y_bounds
      = [&](size_t index, const std::string& name, const Ann_place& place) -> std::optional<std::pair<double, double>> {
    const auto& macro = tech.macros.at(fp.components.at(index).macro);
    const auto  pin   = macro.pins.find(name);
    if (pin == macro.pins.end()) {
      return {};
    }
    std::optional<std::pair<double, double>> result;
    for (const auto& shape : pin->second.shapes) {
      if (shape.layer != via_profile->route.access.pin) {
        return {};
      }
      const auto rect = ph::placed_rect(macro, place, fp.dbu, shape.rect);
      if (!std::isfinite(rect.yl) || !std::isfinite(rect.yh)) {
        return {};
      }
      // The pin-access oracle rounds representable coordinates to DEF DBU.
      const auto low = std::round(rect.yl), high = std::round(rect.yh);
      if (!result) {
        result = {low, high};
      } else {
        result->first  = std::min(result->first, low);
        result->second = std::max(result->second, high);
      }
    }
    return result;
  };
  const auto first_y  = pin_y_bounds(pair.first, pair.first_pin, a);
  const auto second_y = pin_y_bounds(pair.second, pair.second_pin, b);
  if (!first_y || !second_y || first_y->first > second_y->second || second_y->first > first_y->second) {
    return false;
  }
  ph::Floorplan probe;
  probe.dbu              = fp.dbu;
  probe.die              = fp.die;
  probe.tracks           = fp.tracks;
  probe.vias             = fp.vias;
  probe.special_nets     = fp.special_nets;
  probe.terminals        = fp.terminals;
  probe.nondefault_rules = fp.nondefault_rules;
  std::set<size_t> included;
  const auto       add = [&](size_t index) {
    if (included.insert(index).second) {
      auto cell = fp.components.at(index);
      if (index == pair.first) {
        cell.place = a;
      }
      if (index == pair.second) {
        cell.place = b;
      }
      probe.components.push_back(std::move(cell));
    }
  };
  add(pair.first);
  add(pair.second);
  for (auto index : obstacles) {
    add(index);
  }
  std::set<std::string> names;
  std::set<std::string> nearby_cells;
  for (const auto& cell : probe.components) {
    nearby_cells.insert(cell.name);
  }
  for (const auto& net : fp.nets) {
    names.insert(net.name);
    if (!net.wires.empty()) {
      probe.nets.push_back(net);
      continue;
    }
    // Preserve other local pins even when their nets have no wire yet. Clock
    // inputs often have LEF USE SIGNAL, so inspecting USE alone misses them.
    ph::Net pending;
    pending.name = net.name;
    pending.use  = net.use;
    for (const auto& connection : net.connections) {
      if (!nearby_cells.contains(connection.instance)) {
        continue;
      }
      if ((connection.instance == fp.components.at(pair.first).name && connection.pin == pair.first_pin)
          || (connection.instance == fp.components.at(pair.second).name && connection.pin == pair.second_pin)) {
        continue;  // The synthetic witness below owns these two endpoints.
      }
      pending.connections.push_back(connection);
    }
    if (!pending.connections.empty()) {
      probe.nets.push_back(std::move(pending));
    }
  }
  std::string name = "__livehd_sat_access";
  while (names.contains(name)) {
    name += '_';
  }
  ph::Net net{
      name,
      {{fp.components.at(pair.first).name, pair.first_pin}, {fp.components.at(pair.second).name, pair.second_pin}},
      {},
      "SIGNAL"
  };
  probe.nets.push_back(net);
  const auto                occupancy = ph::routing_occupancy(tech, probe, fp.dbu);
  const Signal_router       router(tech, probe, occupancy, *route_rules, signal_route_profile(*via_profile, tech));
  // Bound this optional geometric predicate independently of solver fanout.
  const Signal_route_limits limits{1024, 256, deadline};
  const auto                result = router.probe_route(net, {limits, {}, false, true});
  if (!result.routable || !result.corridor) {
    return false;
  }
  if (witness) {
    *witness = *result.corridor;
  }
  return true;
}

bool satlegalize_obstructs(const physical::Technology& tech, const physical::Component& cell, const Ann_place& place,
                           const physical::Shape& corridor, int64_t dbu, const physical::Signal_metal_rules* route_rules,
                           std::string_view allowed_pin) {
  const auto& macro     = tech.macros.at(cell.macro);
  const auto  conflicts = [&](const ph::Shape& shape) {
    if (shape.layer != corridor.layer) {
      return false;
    }
    const ph::Shape placed{shape.layer, ph::placed_rect(macro, place, dbu, shape.rect)};
    return route_rules ? !route_rules->spacing(corridor, placed).legal : corridor.rect.overlaps(placed.rect);
  };
  // Supply pins are immutable metal, even when LEF does not list them in OBS.
  // Check them before the body shortcut: abutment rails can extend beyond the
  // nominal cell boundary. Include candidate endpoints with their trial pose.
  for (const auto& [name, pin] : macro.pins) {
    if (name == allowed_pin || (!route_rules && pin.use != "POWER" && pin.use != "GROUND")) {
      continue;
    }
    for (const auto& shape : pin.shapes) {
      if (conflicts(shape)) {
        return true;
      }
    }
  }
  if (route_rules) {
    if (macro.conservative_obstructions.contains(corridor.layer)
        || (macro.blocked_layers.contains(corridor.layer)
            && std::none_of(macro.obstructions.begin(), macro.obstructions.end(), [&](const auto& s) {
                 return s.layer == corridor.layer;
               }))) {
      return !route_rules->spacing(corridor, {corridor.layer, bounds(place)}).legal;
    }
    return std::any_of(macro.obstructions.begin(), macro.obstructions.end(), conflicts);
  }
  if (!corridor.rect.overlaps(bounds(place))) {
    return false;
  }
  if (!macro.blocked_layers.contains(corridor.layer)) {
    return false;
  }
  if (macro.conservative_obstructions.contains(corridor.layer)
      || std::none_of(macro.obstructions.begin(), macro.obstructions.end(), [&](const auto& obstruction) {
           return obstruction.layer == corridor.layer;
         })) {
    return true;
  }
  return std::any_of(macro.obstructions.begin(), macro.obstructions.end(), [&](const auto& obstruction) {
    return obstruction.layer == corridor.layer && corridor.rect.overlaps(ph::placed_rect(macro, place, dbu, obstruction.rect));
  });
}

bool satlegalize_preserved(const ph::Technology& tech, const ph::Floorplan& fp, const Satlegalize_report& report, size_t prefix) {
  prefix = std::min(prefix, report.accepted.size());
  for (size_t i = 0; i < prefix; ++i) {
    const auto& pair = report.accepted[i];
    if (pair.first >= fp.components.size() || pair.second >= fp.components.size()
        || !aligned(tech, fp, pair, fp.components[pair.first].place, fp.components[pair.second].place)) {
      return false;
    }
  }
  return true;
}
void satlegalize_restore(ph::Floorplan& fp, const Satlegalize_report& report, size_t prefix) {
  if (fp.components.size() != report.before.size() || prefix > report.checkpoints.size()) {
    throw std::invalid_argument("satlegalize rollback snapshot mismatch");
  }
  for (size_t i = 0; i < fp.components.size(); ++i) {
    fp.components[i].place = report.before[i];
  }
  for (size_t p = 0; p < prefix; ++p) {
    for (const auto& [i, place] : report.checkpoints[p].positions) {
      fp.components.at(i).place = place;
    }
  }
}
}  // namespace livehd::placement
