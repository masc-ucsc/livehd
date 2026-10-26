// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "terminal_legalize.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "sat_solver.hpp"
#include "satlegalize.hpp"

namespace livehd::placement {
namespace {
namespace ph             = physical;
using Interval           = std::pair<int64_t, int64_t>;
constexpr double epsilon = 1e-6;
bool fixed(const Ann_place& p) { return p.status == ph::Status::FIXED || p.status == ph::Status::LOCKED; }
bool fixed_physical_cell(const ph::Component& c, const ph::Macro& m) {
  return fixed(c.place) && (m.klass == "CORE WELLTAP" || m.klass == "CORE SPACER");
}
bool orientation_allowed(const ph::Macro& m, ph::Orientation o) {
  return o == ph::Orientation::N || (o == ph::Orientation::FS && m.symmetry_x) || (o == ph::Orientation::FN && m.symmetry_y)
         || (o == ph::Orientation::S && m.symmetry_x && m.symmetry_y);
}
double distance(const Ann_place& a, const Ann_place& b) { return std::abs(double(a.x) - b.x) + std::abs(double(a.y) - b.y); }
void   subtract(std::map<int64_t, int64_t>& gaps, int64_t lo, int64_t hi) {
  auto it = gaps.upper_bound(lo);
  if (it != gaps.begin()) {
    --it;
  }
  while (it != gaps.end() && it->first < hi) {
    const auto [a, b] = *it;
    if (b <= lo) {
      ++it;
      continue;
    }
    it = gaps.erase(it);
    if (a < lo) {
      gaps.emplace(a, lo);
    }
    if (b > hi) {
      gaps.emplace(hi, b);
      break;
    }
  }
}
struct Row {
  size_t                     source;
  int64_t                    x, y, step, height, count;
  std::map<int64_t, int64_t> free;
};
struct Candidate {
  Ann_place place;
  size_t    row;
  int64_t   start, end;  // occupancy, including left/right padding, in sites
};
struct Net {
  double                             slack;
  size_t                             index;
  std::vector<Satlegalize_alignment> pairs;
  std::vector<size_t>                cells;
};
struct Packing_failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};
struct Model {
  const ph::Technology&                 tech;
  ph::Floorplan&                        fp;
  const Terminal_legalize_options&      options;
  Terminal_legalize_report              report;
  std::optional<ph::Signal_metal_rules> route_rules;
  const ph::Signal_metal_rules*         drc() const { return route_rules ? &*route_rules : nullptr; }
  Capacity_bins                         grid;
  std::vector<Row>                      rows;
  std::vector<std::vector<size_t>>      bin_rows;
  std::vector<Ann_place>                original, preferred;
  std::vector<bool>                     movable, installed, critical, compact;
  std::vector<std::optional<double>>    slacks;
  std::vector<unsigned>                 stages;
  std::vector<size_t>                   obstacles;
  std::vector<Satlegalize_alignment>    alignments;
  std::vector<ph::Shape>                corridors;
  std::vector<std::vector<size_t>>      bin_corridors, bin_obstacles;
  std::map<std::string, size_t>         names;
  int64_t                               max_row_height = 0;

  struct Wire_pin {
    size_t           cell;
    double           x, y;
    const ph::Macro* macro = nullptr;
  };
  struct Wire_objective {
    std::vector<ph::Rect> intervals;
    double                xbest = 0, ybest = 0;
  };
  std::vector<std::vector<Wire_pin>>        wire_nets;
  std::vector<std::vector<size_t>>          wire_incident;
  std::map<ph::Orientation, Wire_objective> wire_objectives;
  bool                                      wire_active    = false;
  bool                                      pack_from_gaps = false;
  std::vector<bool>                         wire_changed;

  std::pair<double, double> wire_point(const Wire_pin& pin, const Ann_place* override_place = nullptr) const {
    if (pin.cell == fp.components.size()) {
      return {pin.x, pin.y};
    }
    const auto& p = override_place ? *override_place : fp.components[pin.cell].place;
    double      x = pin.x, y = pin.y;
    if (pin.macro) {
      const auto xy = ph::orient_point(x, y, pin.macro->width, pin.macro->height, p.orientation);
      x             = xy.first * fp.dbu;
      y             = xy.second * fp.dbu;
    }
    return {p.x + x, p.y + y};
  }
  void index_wire_net(const ph::Net& net, const std::vector<size_t>& cells) {
    std::vector<Wire_pin> pins;
    for (const auto& connection : net.connections) {
      if (connection.instance == "PIN") {
        const auto port
            = std::find_if(fp.terminals.begin(), fp.terminals.end(), [&](const auto& p) { return p.name == connection.pin; });
        if (port != fp.terminals.end()) {
          pins.push_back({fp.components.size(), double(port->x), double(port->y)});
        }
        continue;
      }
      const auto found = names.find(connection.instance);
      if (found == names.end()) {
        continue;
      }
      const auto  i = found->second;
      const auto& c = fp.components[i];
      const auto& m = tech.macros.at(c.macro);
      Wire_pin    pin{i, c.place.width / 2., c.place.height / 2.};
      const auto  p = m.pins.find(connection.pin);
      if (p != m.pins.end() && !p->second.shapes.empty()) {
        const auto& r = p->second.shapes.front().rect;
        pin.x         = (r.xl + r.xh) / 2 + m.origin_x;
        pin.y         = (r.yl + r.yh) / 2 + m.origin_y;
        pin.macro     = &m;
      }
      pins.push_back(pin);
    }
    if (pins.size() < 2) {
      return;
    }
    for (size_t cell : cells) {
      wire_incident[cell].push_back(wire_nets.size());
    }
    wire_nets.push_back(std::move(pins));
  }
  void prepare_wire_objective(size_t cell) {
    wire_objectives.clear();
    if (wire_incident[cell].empty()) {
      return;
    }
    const auto empty_rect = [] { return ph::Rect{INFINITY, INFINITY, -INFINITY, -INFINITY}; };
    const auto include    = [](ph::Rect& r, std::pair<double, double> xy) {
      r.xl = std::min(r.xl, xy.first);
      r.xh = std::max(r.xh, xy.first);
      r.yl = std::min(r.yl, xy.second);
      r.yh = std::max(r.yh, xy.second);
    };
    for (auto orientation : {ph::Orientation::N, ph::Orientation::FS, ph::Orientation::FN, ph::Orientation::S}) {
      auto& objective = wire_objectives[orientation];
      auto  position  = original[cell];
      position.x = position.y = 0;
      position.orientation    = orientation;
      for (size_t n : wire_incident[cell]) {
        auto own = empty_rect(), others = empty_rect();
        for (const auto& pin : wire_nets[n]) {
          if (pin.cell == cell) {
            include(own, wire_point(pin, &position));
          } else {
            include(others, wire_point(pin));
          }
        }
        if (!std::isfinite(own.xl) || !std::isfinite(others.xl)) {
          continue;
        }
        // For a translated pin box, HPWL is an orientation-independent constant
        // plus distance to this optimal translation interval on each axis.
        const double xa = others.xl - own.xl, xb = others.xh - own.xh;
        const double ya = others.yl - own.yl, yb = others.yh - own.yh;
        objective.intervals.push_back({std::min(xa, xb), std::min(ya, yb), std::max(xa, xb), std::max(ya, yb)});
      }
      const auto median = [&](bool horizontal) {
        std::vector<std::pair<double, double>> points{{horizontal ? double(preferred[cell].x) : double(preferred[cell].y), 1.}};
        for (const auto& r : objective.intervals) {
          points.emplace_back(horizontal ? r.xl : r.yl, options.remainder_wire_weight / 2);
          points.emplace_back(horizontal ? r.xh : r.yh, options.remainder_wire_weight / 2);
        }
        std::sort(points.begin(), points.end());
        const double half = (1 + options.remainder_wire_weight * objective.intervals.size()) / 2;
        double       sum  = 0;
        for (auto [coordinate, weight] : points) {
          sum += weight;
          if (sum >= half) {
            return coordinate;
          }
        }
        return points.back().first;
      };
      objective.xbest = median(true);
      objective.ybest = median(false);
    }
    wire_active = true;
  }
  double placement_cost(size_t cell, double x, double y, ph::Orientation orientation) const {
    double result = std::abs(x - preferred[cell].x) + std::abs(y - preferred[cell].y);
    if (wire_active) {
      for (const auto& r : wire_objectives.at(orientation).intervals) {
        result += options.remainder_wire_weight * (std::max({r.xl - x, x - r.xh, 0.}) + std::max({r.yl - y, y - r.yh, 0.}));
      }
    }
    return result;
  }
  double placement_cost(size_t cell, const Ann_place& p) const { return placement_cost(cell, p.x, p.y, p.orientation); }
  double outside_wire_cost(size_t cell, int cx, int cy, int ring) const {
    double     best     = INFINITY;
    const auto consider = [&](const ph::Rect& region) {
      for (const auto& [orientation, o] : wire_objectives) {
        best = std::min(best,
                        placement_cost(cell,
                                       std::clamp(o.xbest, region.xl, region.xh),
                                       std::clamp(o.ybest, region.yl, region.yh),
                                       orientation));
      }
    };
    if (cx - ring > 0) {
      consider({-INFINITY, -INFINITY, grid.bounds.xl + (cx - ring) * grid.dx, INFINITY});
    }
    if (cx + ring + 1 < int(grid.nx)) {
      consider({grid.bounds.xl + (cx + ring + 1) * grid.dx, -INFINITY, INFINITY, INFINITY});
    }
    if (cy - ring > 0) {
      consider({-INFINITY, -INFINITY, INFINITY, grid.bounds.yl + (cy - ring) * grid.dy});
    }
    if (cy + ring + 1 < int(grid.ny)) {
      consider({-INFINITY, grid.bounds.yl + (cy + ring + 1) * grid.dy - max_row_height, INFINITY, INFINITY});
    }
    return best;
  }

  void reserve(const ph::Rect& b) {
    for (auto& row : rows) {
      if (row.y >= b.yh) {
        break;
      }
      if (row.y + row.height <= b.yl) {
        continue;
      }
      const auto lo = std::clamp<int64_t>(std::floor((b.xl - row.x) / row.step), 0, row.count);
      const auto hi = std::clamp<int64_t>(std::ceil((b.xh - row.x) / row.step), 0, row.count);
      if (hi > lo) {
        subtract(row.free, lo, hi);
      }
    }
  }
  Model(const ph::Technology& technology, ph::Floorplan& frame, const Terminal_legalize_options& controls)
      : tech(technology), fp(frame), options(controls) {
    if (options.route_vias && !options.route_drc) {
      throw std::invalid_argument("terminal via access requires route DRC");
    }
    if (fp.dbu <= 0 || fp.dbu != tech.dbu || fp.rows.empty()) {
      throw std::invalid_argument("terminal legalization requires matching positive DBU units and physical rows");
    }
    if (options.route_drc) {
      route_rules.emplace(tech, fp);
    }
    ph::Rect bounds{std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity()};
    for (size_t i = 0; i < fp.rows.size(); ++i) {
      const auto& r    = fp.rows[i];
      const auto  site = tech.sites.find(r.site);
      if (site == tech.sites.end() || r.count <= 0 || r.step <= 0) {
        throw std::invalid_argument("terminal legalization requires valid row sites");
      }
      const auto h = std::llround(site->second.height * fp.dbu);
      if (h <= 0 || std::abs(site->second.width * fp.dbu - r.step) > epsilon) {
        throw std::invalid_argument("terminal legalization requires contiguous row sites");
      }
      // Ignore sites outside the die, rounding inward to complete sites.
      const int64_t lo = std::max<int64_t>(0, std::ceil((fp.die.xl - r.x) / r.step));
      const int64_t hi = std::min<int64_t>(r.count, std::floor((fp.die.xh - r.x) / r.step));
      if (hi <= lo || r.y < fp.die.yl || r.y + h > fp.die.yh) {
        continue;
      }
      rows.push_back({i, r.x, r.y, r.step, h, r.count, {{lo, hi}}});
      max_row_height = std::max<int64_t>(max_row_height, h);
      bounds.xl      = std::min(bounds.xl, double(r.x + lo * r.step));
      bounds.xh      = std::max(bounds.xh, double(r.x + hi * r.step));
      bounds.yl      = std::min(bounds.yl, double(r.y));
      bounds.yh      = std::max(bounds.yh, double(r.y + h));
    }
    if (rows.empty()) {
      throw std::invalid_argument("terminal legalization has no usable rows");
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return std::tie(a.y, a.x) < std::tie(b.y, b.x); });
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto& a = rows[i];
      for (size_t j = i + 1; j < rows.size() && rows[j].y < a.y + a.height; ++j) {
        const auto& b = rows[j];
        if (std::max(a.x, b.x) < std::min(a.x + a.count * a.step, b.x + b.count * b.step)) {
          throw std::invalid_argument("terminal legalization does not accept overlapping row definitions");
        }
      }
    }
    report.bins_x = options.bins_x;
    report.bins_y = options.bins_y;
    grid          = Capacity_bins(bounds, options.bins_x, options.bins_y);
    bin_rows.resize(grid.bins.size());
    bin_corridors.resize(grid.bins.size());
    bin_obstacles.resize(grid.bins.size());
    const size_t count = fp.components.size();
    movable.resize(count);
    installed.resize(count);
    critical.resize(count);
    compact.resize(count);
    wire_changed.resize(count);
    if (options.remainder_wire_weight > 0) {
      wire_incident.resize(count);
    }
    slacks.resize(count);
    stages.resize(count);
    for (size_t i = 0; i < count; ++i) {
      const auto& c      = fp.components[i];
      const auto  master = tech.macros.find(c.macro);
      if (master == tech.macros.end() || !c.place.is_valid() || !names.emplace(c.name, i).second) {
        throw std::invalid_argument("terminal legalization requires known masters, seeded cells and unique names");
      }
      original.push_back(c.place);
      preferred.push_back(c.place);
      const auto mask = ph::placement_mask(c, tech);
      if (mask == ph::Placement_mask::soft) {
        installed[i] = true;
        continue;
      }
      const bool core = master->second.klass.starts_with("CORE");
      movable[i]      = core && !fixed(c.place);
      if (!core || fixed_physical_cell(c, master->second)) {
        auto b = place_rect(c.place);
        if (!core && mask == ph::Placement_mask::none) {
          b.xl -= options.macro_halo_x_dbu;
          b.xh += options.macro_halo_x_dbu;
          b.yl -= options.macro_halo_y_dbu;
          b.yh += options.macro_halo_y_dbu;
        }
        reserve(b);
        obstacles.push_back(i);
        installed[i] = true;
      }
    }
    // Macro halos and immutable physical infrastructure reduce usable row
    // capacity. Fixed logic cells still consume density, just like critical
    // cells fixed later by this transaction. Taps/spacers need no sizing pad;
    // neighboring movable cells retain their own required padding.
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto& r = rows[i];
      for (const auto& [a, b] : r.free) {
        const ph::Rect strip{double(r.x + a * r.step), double(r.y), double(r.x + b * r.step), double(r.y + r.height)};
        grid.each(strip, [&](size_t bin, double area) {
          grid.bins[bin].capacity += area;
          bin_rows[bin].push_back(i);
        });
      }
    }
    for (auto& list : bin_rows) {
      std::sort(list.begin(), list.end());
      list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    double       total_area     = 0;
    const double total_capacity = grid.total_capacity();
    for (size_t i = 0; i < count; ++i) {
      const auto& c      = fp.components[i];
      const auto& master = tech.macros.at(c.macro);
      if (!master.klass.starts_with("CORE") || fixed_physical_cell(c, master)) {
        continue;
      }
      total_area += double(c.place.width) * c.place.height;
      grid.deposit(c.place, false);
      if (!movable[i]) {
        grid.deposit(c.place, true);
        auto       b    = place_rect(c.place);
        const auto site = tech.sites.find(tech.macros.at(c.macro).site);
        if (site == tech.sites.end()) {
          throw std::invalid_argument("fixed CORE cell has no site");
        }
        const double pad  = options.padding_sites * site->second.width * fp.dbu;
        b.xl             -= pad;
        b.xh             += pad;
        reserve(b);
        installed[i] = true;
        obstacles.push_back(i);
      }
    }
    if (total_area > options.max_density * total_capacity + epsilon) {
      throw std::runtime_error("terminal legalization: total CORE area exceeds the hard density capacity");
    }
    for (const auto& b : grid.bins) {
      if (b.after > options.max_density * b.capacity + epsilon) {
        throw std::runtime_error("terminal legalization: input fixed cells exceed a bin's hard density capacity");
      }
    }
    for (size_t i : obstacles) {
      grid.each(place_rect(fp.components[i].place), [&](size_t b, double) { bin_obstacles[b].push_back(i); });
    }
  }
  bool capacity(const Ann_place& a, const Ann_place* b = nullptr, double limit = -1) const {
    return grid.fits(a, b, limit < 0 ? options.max_density : limit);
  }
  bool compatible(size_t cell, size_t row) const {
    const auto& c = fp.components[cell];
    const auto& m = tech.macros.at(c.macro);
    const auto& r = rows[row];
    return m.site == fp.rows[r.source].site && c.place.height == r.height && orientation_allowed(m, fp.rows[r.source].orientation);
  }
  unsigned padding(size_t cell) const {
    return critical[cell] ? options.critical_padding_sites : (compact[cell] ? 0 : options.padding_sites);
  }
  void candidate(size_t cell, size_t row, int64_t start, std::vector<Candidate>& result, double limit, bool nearest_only,
                 bool complete_menu = false) {
    const auto& r = rows[row];
    auto        p = original[cell];
    p.x           = r.x + (start + padding(cell)) * r.step;
    p.y           = r.y;
    p.orientation = fp.rows[r.source].orientation;
    p.status      = ph::Status::PLACED;
    if (nearest_only && !result.empty() && placement_cost(cell, p) > placement_cost(cell, result.front().place)) {
      return;
    }
    // SAT endpoints retain their original bin membership; a common strip is
    // a selection criterion, not permission to slide along the whole strip.
    if (!nearest_only && grid.cell_of(p) != grid.cell_of(original[cell])) {
      return;
    }
    ++report.candidate_checks;
    if (!capacity(p, nullptr, limit)) {
      return;
    }
    bool blocked = false;
    if (route_rules) {
      for (const auto& corridor : corridors) {
        blocked |= satlegalize_obstructs(tech, fp.components[cell], p, corridor, fp.dbu, drc());
      }
    }
    grid.each(place_rect(p), [&](size_t bin, double) {
      for (size_t corridor : bin_corridors[bin]) {
        blocked |= satlegalize_obstructs(tech, fp.components[cell], p, corridors[corridor], fp.dbu);
      }
    });
    if (blocked) {
      return;
    }
    const int64_t width = (p.width + r.step - 1) / r.step + 2 * padding(cell);
    // Complete local domains enumerate disjoint row/gap/site combinations
    // exactly once; avoid a quadratic duplicate scan on these temporary pools.
    if (!complete_menu && std::any_of(result.begin(), result.end(), [&](const auto& c) {
          return c.place.x == p.x && c.place.y == p.y && c.place.orientation == p.orientation;
        })) {
      return;
    }
    result.push_back({p, row, start, start + width});
    if (!complete_menu && result.size() > (nearest_only ? 1 : options.candidates)) {
      // Tiny horizontal shifts must not consume the entire joint-SAT menu:
      // a different legal row can be necessary to separate overlapping cells
      // or align endpoints. Retain balanced row populations, keeping the
      // closest poses within each row. Ordinary nearest-site search is exact.
      std::map<size_t, size_t> row_counts;
      if (!nearest_only) {
        for (const auto& c : result) {
          ++row_counts[c.row];
        }
      }
      const auto worse = [&](const auto& a, const auto& b) {
        if (!nearest_only && row_counts[a.row] != row_counts[b.row]) {
          return row_counts[a.row] < row_counts[b.row];
        }
        const double da = placement_cost(cell, a.place), db = placement_cost(cell, b.place);
        return std::tie(da, a.place.y, a.place.x) < std::tie(db, b.place.y, b.place.x);
      };
      result.erase(std::max_element(result.begin(), result.end(), worse));
    }
  }
  void row_candidates(size_t cell, size_t row, const Ann_place& anchor, double low, double high, std::vector<Candidate>& result,
                      double limit, bool dense_menu, bool complete_menu = false) {
    if (!compatible(cell, row)) {
      return;
    }
    const auto& r = rows[row];
    if (dense_menu) {
      if (std::abs(double(r.y) - original[cell].y) > options.pair_move_rows * r.height) {
        return;
      }
      const double radius = options.pair_move_sites * double(r.step);
      low                 = std::max(low, original[cell].x - radius);
      high                = std::min(high, original[cell].x + radius);
      if (high < low) {
        return;
      }
    }
    if (!dense_menu && !result.empty()) {
      const double lower_bound
          = std::abs(double(r.y) - preferred[cell].y) + std::max({low - preferred[cell].x, double(preferred[cell].x) - high, 0.});
      if ((!wire_active ? lower_bound
                        : placement_cost(cell,
                                         std::clamp(wire_objectives.at(fp.rows[r.source].orientation).xbest, low, high),
                                         r.y,
                                         fp.rows[r.source].orientation))
          > placement_cost(cell, result.front().place)) {
        return;
      }
    }
    const int64_t width     = (original[cell].width + r.step - 1) / r.step + 2 * padding(cell);
    const double  desired_x = wire_active ? wire_objectives.at(fp.rows[r.source].orientation).xbest : double(anchor.x);
    const int64_t wanted    = std::llround((desired_x - r.x) / r.step) - padding(cell);
    const int64_t minimum   = std::ceil((low - r.x) / r.step) - padding(cell);
    const int64_t maximum   = std::floor((high - r.x) / r.step) - padding(cell);
    auto          it        = r.free.upper_bound(minimum);
    if (it != r.free.begin()) {
      --it;
    }
    for (; it != r.free.end() && it->first <= maximum; ++it) {
      const auto a = std::max(it->first, minimum), b = std::min(it->second - width, maximum);
      if (b < a) {
        continue;
      }
      const auto chosen = std::clamp(wanted, a, b);
      if (dense_menu || !pack_from_gaps) {
        candidate(cell, row, chosen, result, limit, !dense_menu, complete_menu);
      }
      if (dense_menu) {
        for (unsigned shift = 1; shift <= options.pair_move_sites; ++shift) {
          if (chosen + shift <= b) {
            candidate(cell, row, chosen + shift, result, limit, false, complete_menu);
          }
          if (chosen - shift >= a) {
            candidate(cell, row, chosen - shift, result, limit, false, complete_menu);
          }
        }
      } else {
        if (wire_active && !pack_from_gaps) {
          if (chosen > a) {
            candidate(cell, row, chosen - 1, result, limit, true);
          }
          if (chosen < b) {
            candidate(cell, row, chosen + 1, result, limit, true);
          }
        }
        candidate(cell, row, a, result, limit, true);
        candidate(cell, row, b, result, limit, true);
      }
    }
  }
  std::vector<Candidate> menu(size_t cell, const Ann_place& anchor, bool local, double limit = -1, bool complete_local = false) {
    std::vector<Candidate> result;
    if (installed[cell]) {
      // An installed endpoint is immutable; it contributes no new density.
      result.push_back({fp.components[cell].place, rows.size(), 0, 0});
      return result;
    }
    if (local) {
      for (size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (std::abs(double(row.y) - anchor.y) > options.pair_move_rows * row.height) {
          continue;
        }
        row_candidates(cell,
                       r,
                       anchor,
                       anchor.x - options.pair_move_sites * double(row.step),
                       anchor.x + options.pair_move_sites * double(row.step),
                       result,
                       limit,
                       true,
                       complete_local);
      }
    } else {
      const auto [cx, cy] = grid.cell_of(anchor);
      // A direct destination search avoids repeated pushes. Rings have no
      // fixed radius: the last ring covers every bin in the core.
      const int reach     = std::max(grid.nx, grid.ny);
      for (int ring = 0; ring < reach; ++ring) {
        grid.each_ring(cx, cy, ring, [&](size_t bin) {
          if (!grid.has_room(bin, options.max_density)) {
            return;
          }
          const auto& b = grid.bins[bin];
          for (size_t r : bin_rows[bin]) {
            row_candidates(cell, r, anchor, b.bounds.xl, std::nextafter(b.bounds.xh, b.bounds.xl), result, limit, false);
          }
        });
        if (!result.empty()) {
          // A neighboring bin can contain a nearer site than the first bin
          // with space. Stop only when all unvisited bins are farther away.
          double outside = std::numeric_limits<double>::infinity();
          if (cx - ring > 0) {
            outside = std::min(outside, double(anchor.x) - (grid.bounds.xl + (cx - ring) * grid.dx));
          }
          if (cx + ring + 1 < int(grid.nx)) {
            outside = std::min(outside, grid.bounds.xl + (cx + ring + 1) * grid.dx - anchor.x);
          }
          if (cy - ring > 0) {
            outside = std::min(outside, double(anchor.y) - (grid.bounds.yl + (cy - ring) * grid.dy));
          }
          if (cy + ring + 1 < int(grid.ny)) {
            outside = std::min(outside, grid.bounds.yl + (cy + ring + 1) * grid.dy - max_row_height - anchor.y);
          }
          if (wire_active) {
            outside = outside_wire_cost(cell, cx, cy, ring);
          }
          if (placement_cost(cell, result.front().place) <= outside) {
            break;
          }
        }
      }
    }
    std::stable_sort(result.begin(), result.end(), [&](const auto& a, const auto& b) {
      const auto ca = placement_cost(cell, a.place), cb = placement_cost(cell, b.place);
      return std::tie(ca, a.place.y, a.place.x) < std::tie(cb, b.place.y, b.place.x);
    });
    if (!complete_local && result.size() > options.candidates) {
      result.resize(options.candidates);
    }
    return result;
  }
  void install(size_t cell, const Candidate& candidate, unsigned stage) {
    if (installed[cell]) {
      return;
    }
    fp.components[cell].place = candidate.place;
    subtract(rows[candidate.row].free, candidate.start, candidate.end);
    grid.deposit(candidate.place, true);
    installed[cell] = true;
    stages[cell]    = stage;
    obstacles.push_back(cell);
    grid.each(place_rect(candidate.place), [&](size_t bin, double) { bin_obstacles[bin].push_back(cell); });
  }
  void warp_remainder(const std::vector<size_t>& remaining) {
    const auto& policy = options.remainder_warp;
    if (!policy.enabled || policy.weight == 0 || remaining.empty()) {
      return;
    }
    const auto start = warp::Clock::now();
    try {
      DreamPlace::native::Design d;
      d.layout = {grid.bounds.xl, grid.bounds.yl, grid.bounds.xh, grid.bounds.yh, 1, 1};
      std::vector<int>    samples;
      // Remap only unprotected cells into the movable prefix. All locked cells
      // contribute demand at their accepted coordinates, never sample targets.
      std::vector<size_t> order = remaining;
      d.num_movable             = int(order.size());
      for (size_t i = 0; i < installed.size(); ++i) {
        if (installed[i] && ph::placement_mask(fp.components[i], tech) != ph::Placement_mask::soft) {
          order.push_back(i);
        }
      }
      for (size_t k = 0; k < order.size(); ++k) {
        const auto   i     = order[k];
        const auto&  p     = fp.components[i].place;
        const bool   macro = !tech.macros.at(fp.components[i].macro).klass.starts_with("CORE");
        const double hx    = macro ? options.macro_halo_x_dbu : 0;
        const double hy    = macro ? options.macro_halo_y_dbu : 0;
        d.node_x.push_back(p.x - hx);
        d.node_y.push_back(p.y - hy);
        d.node_size_x.push_back(p.width + 2 * hx);
        d.node_size_y.push_back(p.height + 2 * hy);
        d.node_macro.push_back(macro);
        if (k < remaining.size()) {
          samples.push_back(int(k));
        }
      }
      auto controls       = policy;
      controls.dbu_per_um = fp.dbu;
      const auto field    = placement_warp(d, samples, controls, true);
      auto       next     = preferred;
      double     max_move = 0;
      for (const auto& [node, delta] : field.delta) {
        const auto i = order[node];
        auto&      p = next[i];
        if (p.width > grid.bounds.xh - grid.bounds.xl || p.height > grid.bounds.yh - grid.bounds.yl) {
          throw std::invalid_argument("warp cell does not fit core");
        }
        p.x      = std::llround(std::clamp(original[i].x + policy.weight * delta.x, grid.bounds.xl, grid.bounds.xh - p.width));
        p.y      = std::llround(std::clamp(original[i].y + policy.weight * delta.y, grid.bounds.yl, grid.bounds.yh - p.height));
        max_move = std::max(max_move, distance(p, original[i]));
      }
      preferred                        = std::move(next);
      report.warp_samples              = field.delta.size();
      report.warp_max_displacement_dbu = max_move;
      report.warp_status               = "applied";
    } catch (const std::exception& error) {
      report.warp_status = "retained";
      report.warp_reason = error.what();
    }
    report.warp_seconds = std::chrono::duration<double>(warp::Clock::now() - start).count();
  }
  void place_cell(size_t cell, unsigned stage) {
    if (installed[cell]) {
      return;
    }
    wire_active        = false;
    const auto nearest = [&] {
      auto choices = menu(cell, preferred[cell], false);
      if (choices.empty()) {
        // Capacity and obstruction constraints can reject a gap's nearest and
        // boundary sites while leaving an interior site usable. Check every
        // remaining site before declaring destination search exhausted.
        ++report.full_site_scans;
        for (size_t r = 0; r < rows.size(); ++r) {
          if (!compatible(cell, r)) {
            continue;
          }
          const auto&   row   = rows[r];
          const int64_t width = (original[cell].width + row.step - 1) / row.step + 2 * padding(cell);
          for (const auto& [lo, hi] : row.free) {
            for (int64_t start = lo; start + width <= hi; ++start) {
              candidate(cell, r, start, choices, -1, true);
            }
          }
        }
      }
      return choices;
    };
    auto         choices = nearest();
    // Whitespace is useful only if its displacement cost is acceptable. This
    // optional stage-5 relaxation never changes critical/local-pair locks or
    // the hard density cap. Already reserved neighbors retain their padding.
    const double budget  = double(options.padding_max_extra_rows) * original[cell].height;
    if (stage == 5 && !critical[cell] && options.padding_sites && options.padding_max_extra_rows
        && (choices.empty() || distance(choices.front().place, original[cell]) > budget)) {
      compact[cell]        = true;
      auto without_padding = nearest();
      if (!without_padding.empty()
          && (choices.empty()
              || distance(choices.front().place, original[cell])
                     > distance(without_padding.front().place, original[cell]) + budget)) {
        choices = std::move(without_padding);
      } else {
        compact[cell] = false;
      }
    }
    if (choices.empty()) {
      throw Packing_failure("terminal legalization exhausted the full-core destination search for " + fp.components[cell].name
                            + "; critical locks and density cap were preserved; no output committed");
    }
    if (stage == 5 && options.remainder_wire_weight > 0 && distance(choices.front().place, original[cell]) > 0) {
      const auto nearest_place = choices.front().place;
      prepare_wire_objective(cell);
      if (wire_active) {
        auto connected = nearest();
        if (!connected.empty() && placement_cost(cell, connected.front().place) < placement_cost(cell, nearest_place)) {
          choices            = std::move(connected);
          wire_changed[cell] = choices.front().place != nearest_place;
        }
      }
    }
    wire_active = false;
    install(cell, choices.front(), stage);
  }
  struct Joint_endpoint {
    size_t                 cell;
    std::vector<Candidate> choices;
    std::vector<int>       literals, edges;
  };
  std::vector<size_t> alignment_blockers(const Satlegalize_alignment& pair, const std::vector<Candidate>& a,
                                         const std::vector<Candidate>& b) const {
    ph::Rect region = place_rect(original[pair.first]);
    for (const auto* choices : {&a, &b}) {
      for (const auto& choice : *choices) {
        const auto r = place_rect(choice.place);
        region.xl    = std::min(region.xl, r.xl);
        region.yl    = std::min(region.yl, r.yl);
        region.xh    = std::max(region.xh, r.xh);
        region.yh    = std::max(region.yh, r.yh);
      }
    }
    std::set<size_t> fixed_obstacles;
    grid.each(region, [&](size_t bin, double) { fixed_obstacles.insert(bin_obstacles[bin].begin(), bin_obstacles[bin].end()); });
    if (route_rules) {
      fixed_obstacles.insert(obstacles.begin(), obstacles.end());
    }
    fixed_obstacles.insert(pair.first);
    fixed_obstacles.insert(pair.second);
    return std::vector<size_t>(fixed_obstacles.begin(), fixed_obstacles.end());
  }
  bool alignment_supported(const Satlegalize_alignment& pair, const Candidate& a, const Candidate& b,
                           const std::vector<size_t>& blockers, ph::Shape* corridor) const {
    return !(a.row == b.row && a.start < b.end && b.start < a.end) && capacity(a.place, &b.place)
           && satlegalize_aligned(tech, fp, pair, a.place, b.place, blockers, corridor, drc(), options.route_vias);
  }
  // This only selects bounded candidate menus. It never installs a cell;
  // all recovered and neighboring pairs remain decisions of the joint solve.
  std::vector<bool> refine_joint_menus(const std::vector<Satlegalize_alignment>& pairs, const std::map<size_t, size_t>& lookup,
                                       std::vector<Joint_endpoint>& endpoints) {
    // Independent row-balanced pruning can retain no usable pair even when
    // its bounded domain has one. Refine only connected components containing
    // such a hole; fully supported components keep their original menus.
    std::vector<bool> old_supported(pairs.size()), explore(endpoints.size());
    const auto        propagate = [&](auto& marked) {
      bool changed;
      do {
        changed = false;
        for (const auto& pair : pairs) {
          const auto a = lookup.at(pair.first), b = lookup.at(pair.second);
          if (marked[a] != marked[b]) {
            marked[a] = marked[b] = true;
            changed               = true;
          }
        }
      } while (changed);
    };
    for (size_t e = 0; e < pairs.size(); ++e) {
      const auto& pair = pairs[e];
      const auto  a = lookup.at(pair.first), b = lookup.at(pair.second);
      const auto  blockers = alignment_blockers(pair, endpoints[a].choices, endpoints[b].choices);
      for (const auto& ca : endpoints[a].choices) {
        for (const auto& cb : endpoints[b].choices) {
          if (alignment_supported(pair, ca, cb, blockers, nullptr)) {
            old_supported[e] = true;
            break;
          }
        }
        if (old_supported[e]) {
          break;
        }
      }
      if (!old_supported[e]) {
        explore[a] = explore[b] = true;
      }
    }
    propagate(explore);
    if (std::any_of(explore.begin(), explore.end(), [](bool v) { return v; })) {
      struct Support {
        size_t    first, second;
        ph::Shape corridor;
      };
      struct Access {
        double low, high;
        size_t choice;
      };
      std::vector<std::vector<Candidate>> pools(endpoints.size());
      for (size_t ep = 0; ep < endpoints.size(); ++ep) {
        if (explore[ep]) {
          pools[ep]                     = menu(endpoints[ep].cell, original[endpoints[ep].cell], true, -1, true);
          report.joint_pool_candidates += pools[ep].size();
        }
      }
      std::vector<std::vector<Support>> supports(pairs.size());
      std::vector<bool>                 recovered(endpoints.size());
      for (size_t e = 0; e < pairs.size(); ++e) {
        const auto& pair = pairs[e];
        const auto  a = lookup.at(pair.first), b = lookup.at(pair.second);
        if (!explore[a]) {
          continue;
        }
        const auto blockers = alignment_blockers(pair, pools[a], pools[b]);
        const auto access   = [&](size_t ep, const std::string& pin) {
          std::map<std::string, std::vector<Access>> result;
          for (size_t c = 0; c < pools[ep].size(); ++c) {
            for (const auto& shape : satlegalize_pin_access_bounds(tech,
                                                                   fp.components[endpoints[ep].cell],
                                                                   pools[ep][c].place,
                                                                   pin,
                                                                   fp.dbu,
                                                                   options.route_vias)) {
              const bool h = tech.layers.at(shape.layer).direction == "HORIZONTAL";
              result[shape.layer].push_back({h ? shape.rect.yl : shape.rect.xl, h ? shape.rect.yh : shape.rect.xh, c});
            }
          }
          for (auto& [layer, spans] : result) {
            (void)layer;
            std::sort(spans.begin(), spans.end(), [](const auto& x, const auto& y) {
              return std::tie(x.low, x.high, x.choice) < std::tie(y.low, y.high, y.choice);
            });
          }
          return result;
        };
        const auto aa = access(a, pair.first_pin), bb = access(b, pair.second_pin);
        const auto cost = [&](const Support& support) {
          const double da = distance(pools[a][support.first].place, original[pair.first]);
          const double db = distance(pools[b][support.second].place, original[pair.second]);
          return std::tuple{std::max(da, db), da + db, support.first, support.second};
        };
        // Keep the cheapest support for each endpoint pose on each track.
        // Retaining only the globally nearest pairs can erase every witness
        // using a shared cell pose needed by a neighboring edge.
        std::map<std::pair<std::string, double>, std::map<std::pair<size_t, bool>, Support>> track_support;
        for (const auto& [layer, first] : aa) {
          const auto found = bb.find(layer);
          if (found == bb.end()) {
            continue;
          }
          const auto& second = found->second;
          double      span   = 0;
          for (const auto& item : second) {
            span = std::max(span, item.high - item.low);
          }
          for (const auto& ac : first) {
            auto it = std::lower_bound(second.begin(), second.end(), ac.low - span - epsilon, [](const auto& item, double value) {
              return item.low < value;
            });
            for (; it != second.end() && it->low <= ac.high + epsilon; ++it) {
              if (it->high + epsilon < ac.low) {
                continue;
              }
              ph::Shape corridor;
              if (!alignment_supported(pair, pools[a][ac.choice], pools[b][it->choice], blockers, &corridor)) {
                continue;
              }
              const auto& rect  = corridor.rect;
              const bool  h     = tech.layers.at(corridor.layer).direction == "HORIZONTAL";
              auto&       group = track_support[{corridor.layer, h ? (rect.yl + rect.yh) / 2 : (rect.xl + rect.xh) / 2}];
              Support     support{ac.choice, it->choice, std::move(corridor)};
              const auto  retain = [&](size_t choice, bool second_endpoint) {
                const auto key      = std::pair{choice, second_endpoint};
                auto       previous = group.find(key);
                if (previous == group.end()) {
                  group.emplace(key, support);
                } else if (cost(support) < cost(previous->second)) {
                  previous->second = support;
                }
              };
              retain(ac.choice, false);
              retain(it->choice, true);
            }
          }
        }
        std::vector<std::vector<Support>> tracks;
        size_t                            max_supports = 0;
        for (const auto& [track, indexed] : track_support) {
          (void)track;
          auto& group = tracks.emplace_back();
          for (const auto& [pose, support] : indexed) {
            (void)pose;
            group.push_back(support);
          }
          std::sort(group.begin(), group.end(), [&](const auto& x, const auto& y) { return cost(x) < cost(y); });
          group.erase(std::unique(group.begin(),
                                  group.end(),
                                  [](const auto& x, const auto& y) { return x.first == y.first && x.second == y.second; }),
                      group.end());
          max_supports = std::max(max_supports, group.size());
        }
        std::vector<const std::vector<Support>*> ordered;
        for (const auto& group : tracks) {
          ordered.push_back(&group);
        }
        std::sort(ordered.begin(), ordered.end(), [&](const auto* x, const auto* y) {
          return cost(x->front()) < cost(y->front());
        });
        // Expose another track before spending the menu on small shifts along
        // the same one. Shared-cell alternatives still enter one global SAT.
        for (size_t round = 0; round < max_supports; ++round) {
          for (const auto* group : ordered) {
            if (round < group->size()) {
              supports[e].push_back((*group)[round]);
            }
          }
        }
        if (!old_supported[e] && !supports[e].empty()) {
          recovered[a] = recovered[b] = true;
        }
      }
      propagate(recovered);
      std::vector<std::set<size_t>> selected(endpoints.size());
      bool                          changed;
      do {
        changed = false;
        for (size_t e = 0; e < pairs.size(); ++e) {
          const auto a = lookup.at(pairs[e].first), b = lookup.at(pairs[e].second);
          if (!recovered[a]) {
            continue;
          }
          for (const auto& support : supports[e]) {
            const bool ca = selected[a].contains(support.first), cb = selected[b].contains(support.second);
            if ((ca && cb) || (!ca && selected[a].size() == options.candidates)
                || (!cb && selected[b].size() == options.candidates)) {
              continue;
            }
            selected[a].insert(support.first);
            selected[b].insert(support.second);
            changed = true;
            break;
          }
        }
      } while (changed);
      for (size_t ep = 0; ep < endpoints.size(); ++ep) {
        if (!selected[ep].empty()) {
          endpoints[ep].choices.clear();
          for (size_t c : selected[ep]) {
            endpoints[ep].choices.push_back(pools[ep][c]);
          }
        }
      }
    }
    return old_supported;
  }
  void joint_critical(const std::vector<Satlegalize_alignment>& pairs) {
    if (pairs.empty()) {
      return;
    }
    Sat_solver                  solver(sat_backend(options.backend));
    std::map<size_t, size_t>    lookup;
    std::vector<Joint_endpoint> endpoints;
    std::vector<int>            enabled;
    std::vector<bool>           active(pairs.size(), true);
    for (size_t e = 0; e < pairs.size(); ++e) {
      enabled.push_back(solver.new_variable());
      for (size_t cell : {pairs[e].first, pairs[e].second}) {
        auto [it, inserted] = lookup.emplace(cell, endpoints.size());
        if (inserted) {
          endpoints.push_back({cell, menu(cell, original[cell], true), {}, {}});
        }
        endpoints[it->second].edges.push_back(enabled.back());
      }
    }
    report.joint_selected    = pairs.size();
    const auto old_supported = refine_joint_menus(pairs, lookup, endpoints);
    // One decision per physical cell, even if it participates in several nets.
    for (auto& ep : endpoints) {
      for (size_t i = 0; i < ep.choices.size(); ++i) {
        const int lit = solver.new_variable();
        for (int other : ep.literals) {
          solver.clause({-lit, -other});
        }
        ep.literals.push_back(lit);
        auto used = ep.edges;
        used.push_back(-lit);
        solver.clause(used);
      }
      for (int edge : ep.edges) {
        auto required = ep.literals;
        required.push_back(-edge);
        solver.clause(required);
      }
    }
    // Encode conflicts across ALL selected nets, rather than committing pairs
    // sequentially. Row buckets avoid comparing distant candidate rectangles.
    std::vector<std::vector<std::pair<size_t, size_t>>> row_choices(rows.size());
    for (size_t e = 0; e < endpoints.size(); ++e) {
      for (size_t c = 0; c < endpoints[e].choices.size(); ++c) {
        row_choices[endpoints[e].choices[c].row].emplace_back(e, c);
      }
    }
    for (auto& bucket : row_choices) {
      std::sort(bucket.begin(), bucket.end(), [&](auto a, auto b) {
        return endpoints[a.first].choices[a.second].start < endpoints[b.first].choices[b.second].start;
      });
      for (size_t i = 0; i < bucket.size(); ++i) {
        const auto [a, ac] = bucket[i];
        const auto& first  = endpoints[a].choices[ac];
        for (size_t j = i + 1; j < bucket.size(); ++j) {
          const auto [b, bc] = bucket[j];
          if (endpoints[b].choices[bc].start >= first.end) {
            break;
          }
          if (a != b) {
            solver.clause({-endpoints[a].literals[ac], -endpoints[b].literals[bc]});
          }
        }
      }
    }
    struct Witness {
      size_t    first, second;
      ph::Shape corridor;
    };
    std::vector<std::vector<Witness>> witnesses(pairs.size());
    for (size_t e = 0; e < pairs.size(); ++e) {
      const auto&                   pair     = pairs[e];
      const auto&                   a        = endpoints[lookup.at(pair.first)];
      const auto&                   b        = endpoints[lookup.at(pair.second)];
      const auto                    blockers = alignment_blockers(pair, a.choices, b.choices);
      std::vector<std::vector<int>> first_support(a.choices.size()), second_support(b.choices.size());
      for (size_t i = 0; i < a.choices.size(); ++i) {
        for (size_t j = 0; j < b.choices.size(); ++j) {
          // These poses already conflict in the cell-overlap encoding. Do not
          // count their geometric alignment as an available routing witness.
          // Otherwise a pair with no legal witness needs a separate SAT core.
          if (a.choices[i].row == b.choices[j].row && a.choices[i].start < b.choices[j].end
              && b.choices[j].start < a.choices[i].end) {
            continue;
          }
          ph::Shape corridor;
          if (satlegalize_aligned(tech,
                                  fp,
                                  pair,
                                  a.choices[i].place,
                                  b.choices[j].place,
                                  blockers,
                                  &corridor,
                                  drc(),
                                  options.route_vias)) {
            witnesses[e].push_back({i, j, std::move(corridor)});
            first_support[i].push_back(b.literals[j]);
            second_support[j].push_back(a.literals[i]);
          }
        }
      }
      // Arc-support clauses encode exactly the allowed endpoint combinations.
      // With one pose per cell, choosing a pose requires one of its compatible
      // opposite poses. Both directions propagate exhausted supports without
      // enumerating every forbidden Cartesian-product pair.
      const auto support_clauses = [&](const auto& literals, auto& supports) {
        for (size_t i = 0; i < literals.size(); ++i) {
          supports[i].push_back(-enabled[e]);
          supports[i].push_back(-literals[i]);
          solver.clause(supports[i]);
        }
      };
      support_clauses(a.literals, first_support);
      support_clauses(b.literals, second_support);
      report.joint_menu_recovered += !old_supported[e] && !witnesses[e].empty();
    }
    // Empty menus/witness sets already prove that an individual alignment is
    // unavailable. Disable these together before the coupled solve; there is
    // no benefit in extracting the same single-edge core one edge at a time.
    for (size_t e = 0; e < pairs.size(); ++e) {
      if (witnesses[e].empty()) {
        active[e] = false;
        ++report.joint_no_witness;
        ++report.joint_dropped;
      }
    }
    // Different endpoint placements often use the same unavoidable track
    // segment. Expose that shared resource to SAT, so a saturated channel is
    // a small at-most-one problem rather than many lazy rectangle conflicts.
    struct Track_use {
      size_t    edge;
      int       literal;
      ph::Shape common;
    };
    std::vector<Track_use> track_uses;
    for (size_t e = 0; e < pairs.size(); ++e) {
      std::map<std::tuple<std::string, double, double>, size_t> groups;
      const auto&                                               a = endpoints[lookup.at(pairs[e].first)];
      const auto&                                               b = endpoints[lookup.at(pairs[e].second)];
      for (const auto& w : witnesses[e]) {
        const auto& r          = w.corridor.rect;
        const bool  horizontal = tech.layers.at(w.corridor.layer).direction == "HORIZONTAL";
        const auto  key        = std::tuple{w.corridor.layer, horizontal ? r.yl : r.xl, horizontal ? r.yh : r.xh};
        auto [it, inserted]    = groups.emplace(key, track_uses.size());
        if (inserted) {
          track_uses.push_back({e, solver.new_variable(), w.corridor});
        } else {
          auto& common = track_uses[it->second].common.rect;
          common.xl    = std::max(common.xl, r.xl);
          common.yl    = std::max(common.yl, r.yl);
          common.xh    = std::min(common.xh, r.xh);
          common.yh    = std::min(common.yh, r.yh);
        }
        solver.clause({-enabled[e], -a.literals[w.first], -b.literals[w.second], track_uses[it->second].literal});
      }
    }
    std::vector<std::vector<std::pair<size_t, size_t>>> pose_bins(grid.bins.size());
    for (size_t ep = 0; ep < endpoints.size(); ++ep) {
      for (size_t c = 0; c < endpoints[ep].choices.size(); ++c) {
        grid.each(place_rect(endpoints[ep].choices[c].place), [&](size_t bin, double) { pose_bins[bin].emplace_back(ep, c); });
      }
    }
    std::vector<std::vector<size_t>> track_bins(grid.bins.size());
    for (size_t g = 0; g < track_uses.size(); ++g) {
      const auto& use = track_uses[g];
      if (use.common.rect.xh <= use.common.rect.xl || use.common.rect.yh <= use.common.rect.yl) {
        continue;
      }
      std::set<size_t>                    neighbors;
      std::set<std::pair<size_t, size_t>> nearby_poses;
      grid.each(use.common.rect, [&](size_t bin, double) {
        neighbors.insert(track_bins[bin].begin(), track_bins[bin].end());
        nearby_poses.insert(pose_bins[bin].begin(), pose_bins[bin].end());
        track_bins[bin].push_back(g);
      });
      // Every witness using this track occupies its common segment. Expose
      // cell-OBS conflicts on that segment before solving, instead of learning
      // the same obstruction separately for many endpoint combinations.
      for (const auto& [ep, c] : nearby_poses) {
        const auto& endpoint = endpoints[ep];
        if (endpoint.cell == pairs[use.edge].first || endpoint.cell == pairs[use.edge].second) {
          continue;
        }
        if (satlegalize_obstructs(tech, fp.components[endpoint.cell], endpoint.choices[c].place, use.common, fp.dbu)) {
          solver.clause({-use.literal, -endpoint.literals[c]});
          ++report.joint_static_obstructions;
        }
      }
      for (size_t other : neighbors) {
        const auto& prior = track_uses[other];
        if (use.edge != prior.edge && use.common.layer == prior.common.layer && rect_overlap(use.common.rect, prior.common.rect) > 0) {
          solver.clause({-use.literal, -prior.literal});
        }
      }
    }
    std::vector<int>       solution(endpoints.size(), -1);
    std::vector<ph::Shape> solution_corridors(pairs.size());
    // Bin capacities and routing witnesses couple more than two cells. Add
    // exact model conflicts lazily; a model is never committed before all of
    // them pass. A resource limit means UNKNOWN, not geometric infeasibility.
    const auto             solve = [&](double max_distance, int cost_bound = 0, unsigned milliseconds = 0) {
      std::vector<int> assumptions;
      if (cost_bound) {
        assumptions.push_back(cost_bound);
      }
      for (size_t e = 0; e < pairs.size(); ++e) {
        assumptions.push_back(active[e] ? enabled[e] : -enabled[e]);
      }
      for (const auto& ep : endpoints) {
        for (size_t c = 0; c < ep.choices.size(); ++c) {
          if (distance(ep.choices[c].place, original[ep.cell]) > max_distance) {
            assumptions.push_back(-ep.literals[c]);
          }
        }
      }
      const auto deadline
          = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds ? milliseconds : options.sat_milliseconds);
      while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining
            = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        ++report.sat_trials;
        const auto status = solver.solve(assumptions, options.sat_conflicts, std::max<int64_t>(1, remaining));
        if (status != Sat_status::satisfiable) {
          report.sat_unsat   += status == Sat_status::unsatisfiable;
          report.sat_unknown += status == Sat_status::unknown;
          return status;
        }
        std::vector<int> chosen(endpoints.size(), -1);
        for (size_t ep = 0; ep < endpoints.size(); ++ep) {
          for (size_t c = 0; c < endpoints[ep].choices.size(); ++c) {
            if (solver.value(endpoints[ep].literals[c])) {
              chosen[ep] = c;
            }
          }
        }
        std::vector<std::vector<int>>                         cuts;
        std::map<size_t, std::vector<std::pair<int, double>>> bins;
        std::vector<std::vector<size_t>>                      occupied(grid.bins.size());
        for (size_t ep = 0; ep < endpoints.size(); ++ep) {
          if (chosen[ep] < 0) {
            continue;
          }
          const auto& cell = endpoints[ep];
          grid.each(place_rect(cell.choices[chosen[ep]].place), [&](size_t bin, double area) {
            bins[bin].emplace_back(cell.literals[chosen[ep]], area);
            occupied[bin].push_back(ep);
          });
        }
        for (const auto& [bin, contributions] : bins) {
          double           area = grid.bins[bin].after;
          std::vector<int> cut;
          for (const auto& [lit, added] : contributions) {
            area += added;
            cut.push_back(-lit);
          }
          // Leave the margin between target and hard cap for later cells.
          const double limit = std::max(grid.bins[bin].after, options.target_density * grid.bins[bin].capacity);
          if (area > limit + epsilon) {
            ++report.joint_density_cuts;
            cuts.push_back(std::move(cut));
          }
        }
        std::vector<ph::Shape> paths(pairs.size());
        const auto             edge_cut = [&](size_t e) {
          const size_t a = lookup.at(pairs[e].first), b = lookup.at(pairs[e].second);
          return std::vector<int>{-enabled[e], -endpoints[a].literals[chosen[a]], -endpoints[b].literals[chosen[b]]};
        };
        std::vector<std::vector<size_t>> path_bins(grid.bins.size());
        for (size_t e = 0; e < pairs.size(); ++e) {
          if (!active[e]) {
            continue;
          }
          const size_t a = lookup.at(pairs[e].first), b = lookup.at(pairs[e].second);
          const auto   witness = std::find_if(witnesses[e].begin(), witnesses[e].end(), [&](const auto& w) {
            return chosen[a] == int(w.first) && chosen[b] == int(w.second);
          });
          if (witness == witnesses[e].end()) {
            throw std::logic_error("joint SAT model violates an alignment clause");
          }
          paths[e] = witness->corridor;
          std::set<size_t> neighbors, other_paths;
          grid.each(paths[e].rect, [&](size_t bin, double) {
            neighbors.insert(occupied[bin].begin(), occupied[bin].end());
            other_paths.insert(path_bins[bin].begin(), path_bins[bin].end());
            path_bins[bin].push_back(e);
          });
          if (route_rules) {
            for (size_t ep = 0; ep < endpoints.size(); ++ep) {
              // Dropped edges may leave their endpoints unselected. Those
              // cells are installed later, with retained-corridor checks.
              if (chosen[ep] >= 0) {
                neighbors.insert(ep);
              }
            }
            for (size_t prior = 0; prior < e; ++prior) {
              if (!paths[prior].layer.empty()) {
                other_paths.insert(prior);
              }
            }
          }
          for (size_t ep : neighbors) {
            if (ep != a && ep != b
                && satlegalize_obstructs(tech,
                                         fp.components[endpoints[ep].cell],
                                         endpoints[ep].choices[chosen[ep]].place,
                                         paths[e],
                                         fp.dbu,
                                         drc())) {
              auto cut = edge_cut(e);
              cut.push_back(-endpoints[ep].literals[chosen[ep]]);
              ++report.joint_obstruction_cuts;
              cuts.push_back(std::move(cut));
            }
          }
          for (size_t other : other_paths) {
            if (paths[e].layer == paths[other].layer
                && (route_rules ? !route_rules->spacing(paths[e], paths[other]).legal
                                            : rect_overlap(paths[e].rect, paths[other].rect) > 0)) {
              auto cut = edge_cut(e), more = edge_cut(other);
              cut.insert(cut.end(), more.begin(), more.end());
              ++report.joint_corridor_cuts;
              cuts.push_back(std::move(cut));
            }
          }
        }
        if (cuts.empty()) {
          solution           = std::move(chosen);
          solution_corridors = std::move(paths);
          return Sat_status::satisfiable;
        }
        report.joint_cuts += cuts.size();
        for (const auto& cut : cuts) {
          solver.clause(cut);
        }
      }
      ++report.sat_unknown;
      return Sat_status::unknown;
    };
    // Start with the full globally ranked set. Only a conflicting selection is
    // reduced, least critical first; accepted placements are still free to change.
    while (true) {
      const auto status = solve(std::numeric_limits<double>::infinity());
      if (status == Sat_status::satisfiable) {
        break;
      }
      if (status == Sat_status::unknown) {
        // An initial timeout provides no placement to retain. Recover an
        // anytime incumbent by admitting the remaining nets in priority order.
        // Every trial still re-solves ALL admitted cells; nothing is anchored
        // or installed until the final validated joint model is selected.
        report.joint_admission_fallback = true;
        const auto pending              = active;
        std::fill(active.begin(), active.end(), false);
        std::fill(solution.begin(), solution.end(), -1);
        std::fill(solution_corridors.begin(), solution_corridors.end(), ph::Shape{});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.sat_milliseconds);
        for (size_t e = 0; e < pairs.size(); ++e) {
          if (!pending[e]) {
            continue;
          }
          const auto remaining
              = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
          if (remaining <= 0) {
            ++report.joint_timeout_deferred;
            ++report.joint_dropped;
            continue;
          }
          active[e]           = true;
          const auto admitted = solve(std::numeric_limits<double>::infinity(), 0, unsigned(remaining));
          if (admitted != Sat_status::satisfiable) {
            active[e] = false;
            ++report.joint_dropped;
            if (admitted == Sat_status::unknown) {
              ++report.joint_timeout_deferred;
            } else {
              // The preceding selection was feasible, so the newly admitted
              // (least critical) net is necessarily part of this conflict.
              ++report.joint_conflict_dropped;
            }
          }
        }
        break;
      }
      size_t drop = pairs.size();
      for (size_t e = pairs.size(); e-- > 0;) {
        if (active[e] && solver.failed(enabled[e])) {
          drop = e;
          break;
        }
      }
      if (drop == pairs.size()) {
        throw std::logic_error("joint SAT conflict has no active selection assumption");
      }
      active[drop] = false;
      ++report.joint_conflict_dropped;
      ++report.joint_dropped;
    }
    report.joint_feasible = true;
    // Minimize the largest endpoint displacement over the retained selection.
    // Every trial re-solves all cells jointly. Keep the last validated model
    // if optimization hits its solver budget.
    std::vector<double> distances{0};
    double              incumbent = 0;
    for (size_t ep = 0; ep < endpoints.size(); ++ep) {
      if (solution[ep] >= 0) {
        incumbent = std::max(incumbent, distance(endpoints[ep].choices[solution[ep]].place, original[endpoints[ep].cell]));
      }
      for (const auto& candidate : endpoints[ep].choices) {
        distances.push_back(distance(candidate.place, original[endpoints[ep].cell]));
      }
    }
    std::sort(distances.begin(), distances.end());
    distances.erase(std::unique(distances.begin(), distances.end()), distances.end());
    size_t low = 0, high = std::lower_bound(distances.begin(), distances.end(), incumbent) - distances.begin();
    while (low < high) {
      const size_t mid    = low + (high - low) / 2;
      const auto   status = solve(distances[mid]);
      if (status == Sat_status::unknown) {
        break;
      }
      if (status == Sat_status::satisfiable) {
        high = mid;
      } else {
        low = mid + 1;
      }
    }
    report.joint_max_optimized = low == high;
    if (options.joint_minimize_total) {
      uint64_t unit = 0;
      for (const auto& ep : endpoints) {
        for (const auto& choice : ep.choices) {
          unit = std::gcd(unit, uint64_t(distance(choice.place, original[ep.cell])));
        }
      }
      if (unit) {
        std::vector<std::vector<std::pair<int, uint64_t>>> costs;
        for (const auto& ep : endpoints) {
          costs.emplace_back();
          for (size_t c = 0; c < ep.choices.size(); ++c) {
            costs.back().emplace_back(ep.literals[c], uint64_t(distance(ep.choices[c].place, original[ep.cell])) / unit);
          }
        }
        const auto total_cost = [&]() {
          uint64_t total = 0;
          for (size_t ep = 0; ep < endpoints.size(); ++ep) {
            if (solution[ep] >= 0) {
              total += costs[ep][solution[ep]].second;
            }
          }
          return total;
        };
        Sat_choice_cost objective(solver, costs);
        uint64_t        lo_cost = 0, hi_cost = total_cost();
        while (lo_cost < hi_cost) {
          const auto mid    = lo_cost + (hi_cost - lo_cost - 1) / 2;
          const auto status = solve(distances[high], objective.at_most(mid));
          if (status == Sat_status::unknown) {
            break;
          }
          if (status == Sat_status::satisfiable) {
            hi_cost = total_cost();
          } else {
            lo_cost = mid + 1;
          }
        }
        report.joint_total_optimized = lo_cost == hi_cost;
      } else {
        report.joint_total_optimized = true;
      }
    }
    for (size_t ep = 0; ep < endpoints.size(); ++ep) {
      if (solution[ep] >= 0) {
        install(endpoints[ep].cell, endpoints[ep].choices[solution[ep]], 1);
      }
    }
    for (size_t e = 0; e < pairs.size(); ++e) {
      if (!active[e]) {
        continue;
      }
      ++report.critical_pairs;
      const auto moved = [&](size_t cell) {
        const auto& pose = fp.components[cell].place;
        const auto& seed = original[cell];
        return std::tie(pose.x, pose.y, pose.orientation) != std::tie(seed.x, seed.y, seed.orientation);
      };
      const bool both_moved  = moved(pairs[e].first) && moved(pairs[e].second);
      report.joint_pairs    += both_moved;
      report.anchored_pairs += !both_moved;
      const auto ci          = corridors.size();
      corridors.push_back(solution_corridors[e]);
      alignments.push_back(pairs[e]);
      grid.each(corridors.back().rect, [&](size_t bin, double) { bin_corridors[bin].push_back(ci); });
    }
  }
  bool pair(const Satlegalize_alignment& pair, unsigned stage) {
    const auto a = pair.first, b = pair.second;
    if (installed[a] && installed[b]) {
      return false;
    }
    const double limit = stage == 3 ? options.target_density * options.local_pair_fraction : options.max_density;
    auto         ca = menu(a, original[a], true, limit), cb = menu(b, original[b], true, limit);
    if (ca.empty() || cb.empty()) {
      return false;
    }
    // The first candidate is the closest legal pose to this endpoint's GP
    // location. Prefer holding either of these anchors over moving both.
    const auto anchor_a = ca.front().place, anchor_b = cb.front().place;
    const auto [ax, ay] = grid.cell_of(original[a]);
    const auto [bx, by] = grid.cell_of(original[b]);
    // Include a common row/column anchor, even when endpoints lie in different
    // physical rows within the same routing corridor.
    auto aa = original[a], bb = original[b];
    if (ay == by) {
      aa.y = bb.y = installed[a] ? fp.components[a].place.y : installed[b] ? fp.components[b].place.y : (aa.y + bb.y) / 2;
    } else if (ax == bx) {
      aa.x = bb.x = installed[a] ? fp.components[a].place.x : installed[b] ? fp.components[b].place.x : (aa.x + bb.x) / 2;
    }
    auto append = [&](size_t i, const Ann_place& anchor, std::vector<Candidate>& choices) {
      const auto extra = menu(i, anchor, true, limit);
      for (const auto& candidate : extra) {
        if (std::none_of(choices.begin(), choices.end(), [&](const auto& c) { return c.place == candidate.place; })) {
          choices.push_back(candidate);
        }
      }
    };
    // Include choices aligned to each fixed-anchor trial, as well as the
    // bounded joint-movement fallback. All menus still obey original bounds.
    auto at_b = original[a], at_a = original[b];
    if (ay == by) {
      at_b.y = anchor_b.y;
      at_a.y = anchor_a.y;
    } else if (ax == bx) {
      at_b.x = anchor_b.x;
      at_a.x = anchor_a.x;
    }
    append(a, at_b, ca);
    append(b, at_a, cb);
    append(a, aa, ca);
    append(b, bb, cb);
    if (ca.empty() || cb.empty()) {
      return false;
    }
    Sat_solver       solver(sat_backend(options.backend));
    std::vector<int> va, vb;
    const auto       variables = [&](size_t count, std::vector<int>& literals) {
      for (size_t i = 0; i < count; ++i) {
        literals.push_back(solver.new_variable());
      }
      solver.clause(literals);
      for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < i; ++j) {
          solver.clause({-literals[i], -literals[j]});
        }
      }
    };
    variables(ca.size(), va);
    variables(cb.size(), vb);
    std::vector<std::tuple<unsigned, double, size_t, size_t>> feasible;
    ph::Rect                                                  region{std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity()};
    for (const auto* choices : {&ca, &cb}) {
      for (const auto& c : *choices) {
        region.xl = std::min(region.xl, double(c.place.x));
        region.yl = std::min(region.yl, double(c.place.y));
        region.xh = std::max(region.xh, double(c.place.x + c.place.width));
        region.yh = std::max(region.yh, double(c.place.y + c.place.height));
      }
    }
    std::vector<size_t> pair_obstacles;
    grid.each(region, [&](size_t bin, double) {
      pair_obstacles.insert(pair_obstacles.end(), bin_obstacles[bin].begin(), bin_obstacles[bin].end());
    });
    if (route_rules) {
      pair_obstacles.insert(pair_obstacles.end(), obstacles.begin(), obstacles.end());
    }
    std::sort(pair_obstacles.begin(), pair_obstacles.end());
    pair_obstacles.erase(std::unique(pair_obstacles.begin(), pair_obstacles.end()), pair_obstacles.end());
    if (!installed[a]) {
      pair_obstacles.push_back(a);
    }
    if (!installed[b]) {
      pair_obstacles.push_back(b);
    }
    for (size_t i = 0; i < ca.size(); ++i) {
      for (size_t j = 0; j < cb.size(); ++j) {
        bool valid = rect_overlap(place_rect(ca[i].place), place_rect(cb[j].place)) == 0;
        if (!installed[a] && !installed[b] && ca[i].row == cb[j].row) {
          valid &= ca[i].end <= cb[j].start || cb[j].end <= ca[i].start;
        }
        if (!installed[a] && !installed[b]) {
          valid &= capacity(ca[i].place, &cb[j].place, limit);
        }
        if (valid) {
          valid = satlegalize_aligned(tech, fp, pair, ca[i].place, cb[j].place, pair_obstacles, nullptr, drc(), options.route_vias);
        }
        if (valid) {
          const unsigned joint = ca[i].place != anchor_a && cb[j].place != anchor_b;
          feasible.emplace_back(joint, distance(ca[i].place, original[a]) + distance(cb[j].place, original[b]), i, j);
        } else {
          solver.clause({-va[i], -vb[j]});
        }
      }
    }
    ++report.sat_trials;
    if (feasible.empty()) {
      ++report.sat_unsat;
      return false;
    }
    std::sort(feasible.begin(), feasible.end());
    // Try anchored alignments first. Only if neither endpoint can stay at its
    // anchor may both move, still within their original bins and radii.
    const auto [joint, cost, i, j] = feasible.front();
    (void)cost;
    const int  assumptions[] = {va[i], vb[j]};
    const auto status        = solver.solve(assumptions, options.sat_conflicts, options.sat_milliseconds);
    if (status != Sat_status::satisfiable) {
      report.sat_unknown += status == Sat_status::unknown;
      report.sat_unsat   += status == Sat_status::unsatisfiable;
      return false;
    }
    report.joint_pairs    += joint;
    report.anchored_pairs += !joint;
    install(a, ca[i], stage);
    install(b, cb[j], stage);
    alignments.push_back(pair);
    ph::Shape corridor;
    if (!satlegalize_aligned(tech, fp, pair, ca[i].place, cb[j].place, pair_obstacles, &corridor, drc(), options.route_vias)) {
      throw std::logic_error("terminal legalization lost an accepted alignment");
    }
    const auto ci = corridors.size();
    corridors.push_back(std::move(corridor));
    grid.each(corridors.back().rect, [&](size_t bin, double) { bin_corridors[bin].push_back(ci); });
    return true;
  }
  std::vector<std::vector<size_t>> untimed_inputs, support_nets;
  std::vector<Net>                 rank(const timing::Feedback& timing) {
    if (options.critical_fanin_depth) {
      untimed_inputs.resize(fp.components.size());
      support_nets.resize(fp.nets.size());
    }
    const auto feedback = [&](const ph::Connection& c) -> const timing::Feedback::Pin* {
      auto it = timing.pins.find(c.instance == "PIN" ? c.pin : c.instance + ":" + c.pin);
      if (it == timing.pins.end() && c.instance != "PIN") {
        it = timing.pins.find(c.instance + "/" + c.pin);
      }
      return it == timing.pins.end() ? nullptr : &it->second;
    };
    const auto excluded = prects_networks(tech, fp, options.prects, timing);
    report.ignored_nets = std::count(excluded.ignored.begin(), excluded.ignored.end(), true);
    std::vector<Net> nets;
    for (size_t n = 0; n < fp.nets.size(); ++n) {
      if (excluded.ignored[n]) {
        continue;
      }
      const auto&           net    = fp.nets[n];
      const ph::Connection* driver = nullptr;
      bool                  clock = false, power = false, multiple = false;
      double                slack = std::numeric_limits<double>::infinity();
      Net                   item{slack, n, {}, {}};
      std::vector<size_t>   untimed_sinks;
      for (const auto& c : net.connections) {
        const auto pin  = feedback(c);
        clock          |= pin && pin->clock;
        if (pin && pin->slack && std::isfinite(*pin->slack)) {
          slack = std::min(slack, *pin->slack * timing.time_unit_ps);
        }
        const auto found = names.find(c.instance);
        if (found == names.end()) {
          continue;
        }
        const auto  i = found->second;
        const auto& m = tech.macros.at(fp.components[i].macro);
        const auto  p = m.pins.find(c.pin);
        if (p != m.pins.end()) {
          power |= p->second.use == "POWER" || p->second.use == "GROUND";
          clock |= p->second.use == "CLOCK";
          if (options.critical_fanin_depth && movable[i] && p->second.direction == "INPUT"
              && (!pin || !pin->slack || !std::isfinite(*pin->slack))) {
            untimed_sinks.push_back(i);
          }
          if (p->second.direction == "OUTPUT") {
            multiple |= driver != nullptr;
            driver    = &c;
          }
        }
        if (movable[i]) {
          item.cells.push_back(i);
          if (pin && pin->slack && std::isfinite(*pin->slack)) {
            const double value = *pin->slack * timing.time_unit_ps;
            slacks[i]          = slacks[i] ? std::min(*slacks[i], value) : value;
          }
        }
      }
      if (clock || power || item.cells.empty()) {
        continue;
      }
      std::sort(item.cells.begin(), item.cells.end());
      item.cells.erase(std::unique(item.cells.begin(), item.cells.end()), item.cells.end());
      if (!untimed_sinks.empty()) {
        support_nets[n] = item.cells;
        for (size_t i : untimed_sinks) {
          untimed_inputs[i].push_back(n);
        }
      }
      if (!std::isfinite(slack)) {
        continue;
      }
      if (options.remainder_wire_weight > 0) {
        index_wire_net(net, item.cells);
      }
      item.slack = slack;
      if (driver && !multiple && names.contains(driver->instance)) {
        const size_t a = names.at(driver->instance);
        for (const auto& sink : net.connections) {
          const auto found = names.find(sink.instance);
          if (found == names.end() || found->second == a || !movable[a] || !movable[found->second]) {
            continue;
          }
          const auto& m = tech.macros.at(fp.components[found->second].macro);
          const auto  p = m.pins.find(sink.pin);
          if (p != m.pins.end() && p->second.direction == "INPUT") {
            item.pairs.push_back({a, found->second, driver->pin, sink.pin});
          }
        }
      }
      nets.push_back(std::move(item));
    }
    std::stable_sort(nets.begin(), nets.end(), [](const auto& a, const auto& b) {
      return std::tie(a.slack, a.index) < std::tie(b.slack, b.index);
    });
    return nets;
  }
  void run(const timing::Feedback& timing) {
    const auto nets      = rank(timing);
    report.timed_nets    = nets.size();
    report.critical_nets = std::min(nets.size(), size_t(std::ceil(std::nextafter(options.critical_fraction * nets.size(), 0.))));
    std::vector<size_t> selected;
    for (size_t n = 0; n < report.critical_nets; ++n) {
      for (size_t i : nets[n].cells) {
        if (!critical[i]) {
          critical[i] = true;
          selected.push_back(i);
        }
      }
    }
    const size_t primary_cells = selected.size();
    size_t       begin = 0, end = selected.size();
    for (unsigned depth = 0; depth < options.critical_fanin_depth && begin < end; ++depth) {
      for (size_t p = begin; p < end; ++p) {
        for (size_t net : untimed_inputs[selected[p]]) {
          for (size_t i : support_nets[net]) {
            if (!critical[i]) {
              critical[i] = true;
              selected.push_back(i);
            }
          }
        }
      }
      begin = end;
      end   = selected.size();
    }
    report.supporting_cells = selected.size() - primary_cells;
    report.critical_cells   = selected.size();
    std::vector<Satlegalize_alignment> proposals;
    for (size_t n = 0; n < report.critical_nets && proposals.size() < options.critical_pair_limit; ++n) {
      // A two-terminal net is a single tree edge. Multi-terminal trees need
      // explicit RSMT edge evidence and remain in ordinary critical legalization.
      if (fp.nets[nets[n].index].connections.size() != 2 || nets[n].pairs.size() != 1) {
        continue;
      }
      const auto& p = nets[n].pairs.front();
      const auto  a = grid.cell_of(original[p.first]), b = grid.cell_of(original[p.second]);
      if (a.first == b.first || a.second == b.second) {
        proposals.push_back(p);
      }
    }
    joint_critical(proposals);
    for (size_t i : selected) {
      place_cell(i, 2);
    }
    unsigned attempts = 0;
    for (size_t n = report.critical_nets; n < nets.size() && attempts < options.local_pair_limit; ++n) {
      for (const auto& p : nets[n].pairs) {
        if (attempts >= options.local_pair_limit) {
          break;
        }
        if (grid.cell_of(original[p.first]) == grid.cell_of(original[p.second]) && !(installed[p.first] && installed[p.second])) {
          ++attempts;
          report.local_pairs += pair(p, 3);
        }
      }
    }
    std::vector<size_t> remaining;
    for (size_t i = 0; i < movable.size(); ++i) {
      if (movable[i] && !installed[i]) {
        remaining.push_back(i);
      }
    }
    warp_remainder(remaining);
    // Placing the most critical remaining cells first leaves displacement to
    // less critical cells. Direct destinations eliminate push oscillations and
    // allow transfer past full intermediate bins without moving locked cells.
    std::stable_sort(remaining.begin(), remaining.end(), [&](size_t a, size_t b) {
      const auto sa = slacks[a].value_or(std::numeric_limits<double>::infinity());
      const auto sb = slacks[b].value_or(std::numeric_limits<double>::infinity());
      if (sa != sb) {
        return sa < sb;
      }
      return original[a].width > original[b].width;
    });
    // Retry only the unprotected remainder if timing order fragments rows.
    // Every accepted critical/local pair and critical cell stays bit-exact.
    const auto saved_rows        = rows;
    const auto saved_bins        = grid.bins;
    const auto saved_obstacles   = obstacles.size();
    const auto restore_remainder = [&] {
      rows        = saved_rows;
      grid.bins = saved_bins;
      obstacles.resize(saved_obstacles);
      for (auto& bin : bin_obstacles) {
        bin.clear();
      }
      for (size_t i : obstacles) {
        grid.each(place_rect(fp.components[i].place), [&](size_t b, double) { bin_obstacles[b].push_back(i); });
      }
      for (size_t i : remaining) {
        fp.components[i].place = original[i];
        installed[i]           = false;
        compact[i]             = false;
        wire_changed[i]        = false;
        stages[i]              = 0;
      }
    };
    for (unsigned attempt = 0;; ++attempt) {
      try {
        for (size_t i : remaining) {
          place_cell(i, 5);
        }
        break;
      } catch (const Packing_failure&) {
        if (attempt == 2) {
          throw;
        }
        ++report.packing_retries;
        restore_remainder();
        std::stable_sort(remaining.begin(), remaining.end(), [&](size_t a, size_t b) {
          return original[a].width > original[b].width;
        });
        // Nearest-site placement can repeatedly split a usable gap into two
        // fragments too small for the remaining padded cells. The final retry
        // packs from gap/bin edges, preserving every cell's mandatory padding.
        pack_from_gaps = attempt == 1;
      }
    }
    for (const auto& b : grid.bins) {
      if (b.after > options.max_density * b.capacity + epsilon) {
        throw std::logic_error("terminal legalization violated its hard density cap");
      }
    }
    for (size_t i = 0; i < movable.size(); ++i) {
      if (!movable[i]) {
        if (fp.components[i].place != original[i]) {
          throw std::logic_error("terminal legalization moved an input obstacle");
        }
        continue;
      }
      const auto&    p     = fp.components[i].place;
      const double   moved = distance(p, original[i]);
      const auto     a = grid.cell_of(original[i]), b = grid.cell_of(p);
      const unsigned hops            = std::abs(a.first - b.first) + std::abs(a.second - b.second);
      report.moved_cells            += moved > 0;
      report.crossed_cells          += hops > 0;
      report.max_bin_distance        = std::max(report.max_bin_distance, size_t(hops));
      report.max_displacement_dbu    = std::max(report.max_displacement_dbu, moved);
      report.total_displacement_dbu += moved;
      report.relaxed_padding_cells  += compact[i];
      report.wire_changed_cells     += wire_changed[i];
      report.cells.push_back(
          {i, original[i], p, stages[i], hops, critical[i], slacks[i].value_or(0), slacks[i].has_value(), padding(i)});
    }
  }
};
}  // namespace

void fit_terminal_legalize_grid(const ph::Technology& tech, const ph::Floorplan& fp, Terminal_legalize_options& options) {
  if (fp.rows.empty() || fp.dbu <= 0 || !(options.max_density > 0)) {
    return;  // Normal legalization validation reports malformed inputs.
  }
  double xl = INFINITY, xh = -INFINITY, yl = INFINITY, yh = -INFINITY, height = 0, widest = 0;
  for (const auto& r : fp.rows) {
    const auto site = tech.sites.find(r.site);
    if (site == tech.sites.end()) {
      continue;
    }
    const auto h = std::llround(site->second.height * fp.dbu);
    xl           = std::min(xl, std::max(double(r.x), fp.die.xl));
    xh           = std::max(xh, std::min(double(r.x + r.count * r.step), fp.die.xh));
    yl           = std::min(yl, std::max(double(r.y), fp.die.yl));
    yh           = std::max(yh, std::min(double(r.y + h), fp.die.yh));
    height       = std::max(height, double(h));
  }
  for (const auto& c : fp.components) {
    if (auto m = tech.macros.find(c.macro);
        m != tech.macros.end() && m->second.klass.starts_with("CORE") && !fixed_physical_cell(c, m->second)) {
      widest = std::max(widest, double(c.place.width));
    }
  }
  const auto cap_bins = [](unsigned requested, double extent, double minimum) {
    if (!(extent > 0) || !std::isfinite(extent) || !(minimum > 0)) {
      return requested;
    }
    return static_cast<unsigned>(std::min(double(requested), std::max(1., std::floor(extent / minimum))));
  };
  options.bins_x = cap_bins(options.bins_x, xh - xl, widest / options.max_density);
  options.bins_y = cap_bins(options.bins_y, yh - yl, height);
}

Terminal_legalize_report terminal_legalize(const physical::Technology& tech, physical::Floorplan& fp,
                                           const timing::Feedback& timing, const Terminal_legalize_options& options) {
  if (!options.bins_x || options.bins_x > 1024 || !options.bins_y || options.bins_y > 1024 || !std::isfinite(options.max_density)
      || options.max_density <= 0 || options.max_density > 1 || !std::isfinite(options.target_density)
      || options.target_density <= 0 || options.target_density >= options.max_density || !std::isfinite(options.critical_fraction)
      || options.critical_fraction < 0 || options.critical_fraction > 1 || !std::isfinite(options.local_pair_fraction)
      || options.local_pair_fraction < 0 || options.local_pair_fraction > 1 || options.padding_sites > 128
      || !std::isfinite(options.remainder_wire_weight) || options.remainder_wire_weight < 0 || options.remainder_wire_weight > 100
      || options.padding_max_extra_rows > 1000000 || options.critical_padding_sites > 128 || options.critical_fanin_depth > 8
      || !options.candidates || options.candidates > 128 || options.pair_move_sites > 128 || options.pair_move_rows > 128
      || !options.sat_conflicts || !options.sat_milliseconds || options.macro_halo_x_dbu < 0 || options.macro_halo_y_dbu < 0
      || !std::isfinite(timing.time_unit_ps) || timing.time_unit_ps <= 0) {
    throw std::invalid_argument("invalid terminal legalization controls; target density must be below the hard bin cap");
  }
  validate_warp_options(options.remainder_warp);
  (void)sat_backend(options.backend);
  const auto start    = std::chrono::steady_clock::now();
  auto       proposal = fp;
  Model      model(tech, proposal, options);
  model.run(timing);
  model.report.bins       = std::move(model.grid.bins);
  model.report.alignments = std::move(model.alignments);
  model.report.seconds    = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  fp                      = std::move(proposal);
  return std::move(model.report);
}

void write_terminal_legalize_report(const Terminal_legalize_report& r, const physical::Floorplan& fp,
                                    const Terminal_legalize_options& o, const std::string& prefix) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  const auto number = [&](const char* key, double value) {
    w.Key(key);
    w.Double(value);
  };
  const auto count = [&](const char* key, size_t value) {
    w.Key(key);
    w.Uint64(value);
  };
  count("schema", 1);
  count("bins_x", r.bins_x);
  count("bins_y", r.bins_y);
  number("max_density", o.max_density);
  number("target_density", o.target_density);
  number("critical_fraction", o.critical_fraction);
  number("local_pair_fraction", o.local_pair_fraction);
  count("padding_sites", o.padding_sites);
  count("padding_max_extra_rows", o.padding_max_extra_rows);
  count("relaxed_padding_cells", r.relaxed_padding_cells);
  number("remainder_wire_weight", o.remainder_wire_weight);
  count("warp_samples", r.warp_samples);
  number("warp_seconds", r.warp_seconds);
  number("warp_max_displacement_dbu", r.warp_max_displacement_dbu);
  w.Key("warp_status");
  w.String(r.warp_status.c_str());
  w.Key("warp_reason");
  w.String(r.warp_reason.c_str());
  count("wire_changed_cells", r.wire_changed_cells);
  count("critical_padding_sites", o.critical_padding_sites);
  count("critical_fanin_depth", o.critical_fanin_depth);
  count("joint_minimize_total", o.joint_minimize_total);
  count("supporting_cells", r.supporting_cells);
  count("full_site_scans", r.full_site_scans);
  count("ignored_nets", r.ignored_nets);
  count("timed_nets", r.timed_nets);
  count("critical_nets", r.critical_nets);
  count("critical_cells", r.critical_cells);
  count("critical_pairs", r.critical_pairs);
  count("anchored_pairs", r.anchored_pairs);
  count("joint_pairs", r.joint_pairs);
  count("joint_selected", r.joint_selected);
  count("joint_feasible", r.joint_feasible);
  count("joint_max_optimized", r.joint_max_optimized);
  count("joint_total_optimized", r.joint_total_optimized);
  count("joint_dropped", r.joint_dropped);
  count("joint_cuts", r.joint_cuts);
  count("joint_no_witness", r.joint_no_witness);
  count("joint_menu_recovered", r.joint_menu_recovered);
  count("joint_pool_candidates", r.joint_pool_candidates);
  count("joint_conflict_dropped", r.joint_conflict_dropped);
  count("joint_density_cuts", r.joint_density_cuts);
  count("joint_obstruction_cuts", r.joint_obstruction_cuts);
  count("joint_static_obstructions", r.joint_static_obstructions);
  count("joint_admission_fallback", r.joint_admission_fallback);
  count("joint_timeout_deferred", r.joint_timeout_deferred);
  count("joint_corridor_cuts", r.joint_corridor_cuts);
  count("local_pairs", r.local_pairs);
  count("sat_trials", r.sat_trials);
  count("sat_unknown", r.sat_unknown);
  count("sat_unsat", r.sat_unsat);
  count("candidate_checks", r.candidate_checks);
  count("packing_retries", r.packing_retries);
  count("moved_cells", r.moved_cells);
  count("crossed_cells", r.crossed_cells);
  count("max_bin_distance", r.max_bin_distance);
  number("seconds", r.seconds);
  number("max_displacement_um", r.max_displacement_dbu / fp.dbu);
  number("total_displacement_um", r.total_displacement_dbu / fp.dbu);
  w.Key("bins");
  w.StartArray();
  for (const auto& b : r.bins) {
    w.StartObject();
    w.Key("bounds_dbu");
    w.StartArray();
    for (double v : {b.bounds.xl, b.bounds.yl, b.bounds.xh, b.bounds.yh}) {
      w.Double(v);
    }
    w.EndArray();
    number("capacity_dbu2", b.capacity);
    number("before_area_dbu2", b.before);
    number("after_area_dbu2", b.after);
    number("before_density", b.capacity > 0 ? b.before / b.capacity : 0);
    number("after_density", b.capacity > 0 ? b.after / b.capacity : 0);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  const auto directory = std::filesystem::path(prefix).parent_path();
  if (!directory.empty()) {
    std::filesystem::create_directories(directory);
  }
  std::ofstream json(prefix + ".json");
  json << buffer.GetString() << '\n';
  std::ofstream cells(prefix + "_cells.tsv");
  cells << "name\tstage\tcritical\tslack_ps\tbefore_x\tbefore_y\tafter_x\tafter_y\tdisplacement_um\tbin_distance\tpadding_sites\n";
  for (const auto& c : r.cells) {
    cells << fp.components[c.index].name << '\t' << c.stage << '\t' << c.critical << '\t';
    if (c.timed) {
      cells << c.slack_ps;
    } else {
      cells << "nan";
    }
    cells << '\t' << c.before.x << '\t' << c.before.y << '\t' << c.after.x << '\t' << c.after.y << '\t'
          << distance(c.before, c.after) / fp.dbu << '\t' << c.bin_distance << '\t' << c.padding_sites << '\n';
  }
  if (!json || !cells) {
    throw std::runtime_error("cannot write terminal legalization reports");
  }
}
}  // namespace livehd::placement
