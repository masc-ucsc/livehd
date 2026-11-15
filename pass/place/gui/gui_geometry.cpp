// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_geometry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>

#include "core/hash_util.hpp"
#include "pass/place/macro_dead_space.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement::gui {
namespace {
using Point    = Scene::Point;
using Interval = std::pair<double, double>;

// Sweep exact DBU x events. Merging y intervals in each slab handles touching,
// overlapping and clipped exclusions without a die-sized occupancy grid.
template <class Visit>
double sweep_core(const Box& core, const std::vector<Box>& exclusions, Visit visit) {
  if (core.xh <= core.xl || core.yh <= core.yl) {
    return 0;
  }
  std::map<double, std::vector<std::pair<Interval, int>>> events;
  events[core.xl];
  events[core.xh];
  for (const auto& b : exclusions) {
    const auto xl = std::max(core.xl, b.xl), xh = std::min(core.xh, b.xh);
    Interval   span{std::max(core.yl, b.yl), std::min(core.yh, b.yh)};
    if (xh <= xl || span.second <= span.first) {
      continue;
    }
    events[xl].emplace_back(span, 1);
    events[xh].emplace_back(span, -1);
  }
  double                  area = 0;
  std::map<Interval, int> active;
  for (auto it = events.begin(); std::next(it) != events.end(); ++it) {
    for (const auto& [span, delta] : it->second) {
      if ((active[span] += delta) == 0) {
        active.erase(span);
      }
    }
    const double xl = it->first, xh = std::next(it)->first;
    auto         y         = core.yl;
    auto         free_span = [&](double top) {
      if (y < top) {
        area += (xh - xl) * (top - y);
        visit(xl, xh, y, top);
      }
    };
    for (const auto& [span, count] : active) {
      free_span(span.first);
      y = std::max(y, span.second);
    }
    free_span(core.yh);
  }
  return area;
}

int direction(const Point& a, const Point& b) {
  if (a[0] != b[0]) {
    return a[0] < b[0] ? 0 : 2;
  }
  return a[1] < b[1] ? 1 : 3;
}
constexpr std::array<std::string_view, 4> face_names{"left", "right", "bottom", "top"};
struct Boundary_gaps {
  std::array<double, 4> body;
  std::pair<std::string_view, double> nearest(double hx = 0, double hy = 0) const {
    double distance = std::numeric_limits<double>::infinity();
    std::string_view side;
    for (size_t i = 0; i < body.size(); ++i) {
      const double gap = body[i] - (i < 2 ? hx : hy);
      if (gap < distance) {
        distance = gap;
        side = face_names[i];
      }
    }
    return {side, distance};
  }
};
Boundary_gaps boundary_gaps(const Scene& scene, const Box& b) {
  Boundary_gaps out;
  out.body.fill(std::numeric_limits<double>::infinity());
  // The whole macro face must project onto a boundary segment. A partial
  // projection at a concave corner cannot establish clearance for that face.
  for (const auto& ring : scene.outline) {
    for (size_t j = 0; j < ring.size(); ++j) {
      const auto& a = ring[j];
      const auto& end = ring[(j + 1) % ring.size()];
      size_t side;
      double gap;
      if (a[0] == end[0] && std::min(a[1], end[1]) <= b.yl && std::max(a[1], end[1]) >= b.yh) {
        const bool left = a[1] > end[1];
        gap = left ? b.xl - a[0] : a[0] - b.xh;
        side = left ? 0 : 1;
      } else if (a[1] == end[1] && std::min(a[0], end[0]) <= b.xl && std::max(a[0], end[0]) >= b.xh) {
        const bool bottom = a[0] < end[0];
        gap = bottom ? b.yl - a[1] : a[1] - b.yh;
        side = bottom ? 2 : 3;
      } else {
        continue;
      }
      if (gap >= 0) {
        out.body[side] = std::min(out.body[side], gap);
      }
    }
  }
  return out;
}
std::optional<Box> facing_strip(const Box& a, const Box& b, bool vertical) {
  const double lo   = vertical ? std::max(a.yl, b.yl) : std::max(a.xl, b.xl);
  const double hi   = vertical ? std::min(a.yh, b.yh) : std::min(a.xh, b.xh);
  const double near = vertical ? std::min(a.xh, b.xh) : std::min(a.yh, b.yh);
  const double far  = vertical ? std::max(a.xl, b.xl) : std::max(a.yl, b.yl);
  if (far <= near || hi - lo <= far - near) {
    return {};
  }
  return vertical ? Box{near, lo, far, hi} : Box{lo, near, hi, far};
}
// Positive-length intersection with the interior, excluding tangencies.
bool crosses_box(Point a, Point b, const Box& box) {
  double first = 0, last = 1;
  auto   clip = [&](double from, double to, double lo, double hi) {
    if (from == to) {
      return from > lo && from < hi;
    }
    double x = (lo - from) / (to - from), y = (hi - from) / (to - from);
    if (x > y) {
      std::swap(x, y);
    }
    first = std::max(first, x);
    last  = std::min(last, y);
    return first < last;
  };
  return a != b && clip(a[0], b[0], box.xl, box.xh) && clip(a[1], b[1], box.yl, box.yh);
}
}  // namespace

double usable_area(const Box& core, const std::vector<Box>& exclusions) {
  return sweep_core(core, exclusions, [](double, double, double, double) {});
}

void build_floorplan_outline(Scene& scene) {
  scene.outline.clear();
  std::map<Point, std::vector<Point>>     edges;
  // Adjacent slab boundaries cancel even if their interval endpoints differ.
  std::map<double, std::map<double, int>> vertical;
  scene.usable_area = sweep_core(scene.die, scene.exclusions, [&](double xl, double xh, double yl, double yh) {
    edges[{xl, yl}].push_back({xh, yl});
    edges[{xh, yh}].push_back({xl, yh});
    --vertical[xl][yl];
    ++vertical[xl][yh];
    ++vertical[xh][yl];
    --vertical[xh][yh];
  });
  for (const auto& [x, events] : vertical) {
    int    winding = 0;
    double start   = 0;
    for (const auto& [y, delta] : events) {
      if (!delta) {
        continue;
      }
      if (winding > 0) {
        edges[{x, start}].push_back({x, y});
      }
      if (winding < 0) {
        edges[{x, y}].push_back({x, start});
      }
      winding += delta;
      start    = y;
    }
  }
  while (!edges.empty()) {
    const Point start    = edges.begin()->first;
    auto        here     = start;
    int         previous = -1;
    auto&       ring     = scene.outline.emplace_back();
    do {
      auto it = edges.find(here);
      if (it == edges.end()) {
        throw std::logic_error("open floorplan boundary");
      }
      auto& choices = it->second;
      auto  next    = choices.begin();
      // At a shared vertex turn left to keep point-touching islands separate.
      if (choices.size() > 1 && previous >= 0) {
        auto rank = [&](const Point& p) {
          const int turn = (direction(here, p) - previous + 4) % 4;
          return turn == 1 ? 0 : turn == 0 ? 1 : turn == 3 ? 2 : 3;
        };
        next = std::ranges::min_element(choices, {}, rank);
      }
      auto point = *next;
      choices.erase(next);
      if (choices.empty()) {
        edges.erase(it);
      }
      const int heading = direction(here, point);
      if (heading != previous) {
        ring.push_back(here);
      }
      previous = heading;
      here     = point;
    } while (here != start);
    if (ring.size() > 2 && direction(ring.back(), start) == direction(start, ring[1])) {
      ring.erase(ring.begin());
    }
  }
}

std::vector<Geometry_issue> macro_geometry(const Scene& scene, const Frame& frame) {
  std::vector<Geometry_issue> issues;
  std::vector<size_t>         bodies;
  const auto                  count = std::min({scene.nodes.size(), frame.x.size(), frame.y.size()});
  const auto                  box   = [&](size_t i) -> Box {
    const auto x = std::round(frame.x[i]), y = std::round(frame.y[i]);
    return {x, y, x + scene.nodes[i].w, y + scene.nodes[i].h};
  };
  for (size_t i = 0; i < count; ++i) {
    const auto& node = scene.nodes[i];
    if (node.kind != Kind::macro && node.kind != Kind::fixed && !node.exclusion) {
      continue;
    }
    if (node.exclusion && !scene.exclusions.empty()) {
      continue;
    }
    bodies.push_back(i);
    if (node.exclusion) {
      continue;
    }
    const auto   b  = box(i);
    const double hx = node.movable ? scene.halo_x : 0, hy = node.movable ? scene.halo_y : 0;
    for (const auto& blocked : scene.exclusions) {
      const double dx = std::max(b.xl - blocked.xh, blocked.xl - b.xh), dy = std::max(b.yl - blocked.yh, blocked.yl - b.yh);
      if (dx < 0 && dy < 0) {
        issues.push_back({"excluded_area", int(i)});
        break;
      }
      if (dx < hx && dy < hy) {
        issues.push_back({"exclusion_halo", int(i)});
        break;
      }
    }
    if (b.xl < scene.die.xl || b.yl < scene.die.yl || b.xh > scene.die.xh || b.yh > scene.die.yh) {
      issues.push_back({"outside_core", int(i)});
    } else if (b.xl - hx < scene.die.xl || b.yl - hy < scene.die.yl || b.xh + hx > scene.die.xh || b.yh + hy > scene.die.yh) {
      issues.push_back({"core_halo", int(i)});
    }
  }
  std::ranges::sort(bodies, [&](size_t i, size_t j) { return std::pair{frame.x[i], i} < std::pair{frame.x[j], j}; });
  const auto sx = std::max(2 * scene.halo_x, scene.channel_x);
  for (size_t left = 0; left < bodies.size(); ++left) {
    const auto i = bodies[left];
    const auto a = box(i);
    for (size_t right = left + 1; right < bodies.size(); ++right) {
      const auto j = bodies[right];
      const auto b = box(j);
      if (b.xl >= a.xh + sx) {
        break;
      }
      const bool e1 = scene.nodes[i].exclusion, e2 = scene.nodes[j].exclusion;
      if (e1 && e2) {
        continue;
      }
      const double dx = std::max(b.xl - a.xh, a.xl - b.xh), dy = std::max(b.yl - a.yh, a.yl - b.yh);
      std::string  cause;
      if (dx < 0 && dy < 0) {
        cause = e1 || e2 ? "excluded_area" : "overlap";
      } else if (dx < (e1 || e2 ? 1 : 2) * scene.halo_x && dy < (e1 || e2 ? 1 : 2) * scene.halo_y) {
        cause = "halo";
      } else if (!e1 && !e2 && dx < scene.channel_x && dy < scene.channel_y) {
        cause = "minimum_channel";
      }
      if (!cause.empty()) {
        issues.push_back({cause, int(e1 ? j : i), int(e1 ? i : j)});
      }
    }
  }
  return issues;
}
std::string floorplan_findings(const Scene& scene, const Frame& frame) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  const auto                                 name = [&](std::string_view s) { w.String(s.data(), s.size()); };
  const auto                                 rect = [&](const Box& b) {
    w.StartArray();
    for (auto v : {b.xl, b.yl, b.xh, b.yh}) {
      w.Double(v);
    }
    w.EndArray();
  };
  const auto body
      = [&](size_t i) { return Box{frame.x[i], frame.y[i], frame.x[i] + scene.nodes[i].w, frame.y[i] + scene.nodes[i].h}; };
  const auto                   limits = Dead_space_limits{std::llround(scene.die.xl),
                                        std::llround(scene.die.yl),
                                        std::llround(scene.die.xh),
                                        std::llround(scene.die.yh),
                                        std::llround(scene.dead_pitch),
                                        std::llround(scene.channel_x),
                                        std::llround(scene.channel_y),
                                        std::llround(scene.dead_narrow_x),
                                        std::llround(scene.dead_narrow_y),
                                        scene.dead_pocket_fraction,
                                        std::llround(scene.dead_min_area)};
  std::vector<Dead_space_rect> blocked;
  auto                         obstacle = [&](Box b, double hx = 0, double hy = 0) {
    blocked.push_back({std::llround(b.xl - hx), std::llround(b.yl - hy), std::llround(b.xh + hx), std::llround(b.yh + hy)});
  };
  for (const auto& b : scene.exclusions) {
    obstacle(b);
  }
  w.StartObject();
  w.Key("ok");
  w.Bool(true);
  w.Key("frame");
  w.Uint64(frame.id);
  w.Key("units");
  w.String("dbu");
  w.Key("usable_area_dbu2");
  w.Double(scene.usable_area);
  w.Key("cell_utilization");
  if (scene.usable_area > 0) {
    w.Double(scene.cell_area / scene.usable_area);
  } else {
    w.Null();
  }
  w.Key("outline");
  w.StartArray();
  for (const auto& ring : scene.outline) {
    w.StartArray();
    for (const auto& p : ring) {
      w.StartArray();
      w.Double(p[0]);
      w.Double(p[1]);
      w.EndArray();
    }
    w.EndArray();
  }
  w.EndArray();
  std::vector<size_t> macros;
  w.Key("macros");
  w.StartArray();
  for (size_t i = 0; i < scene.nodes.size() && i < frame.x.size(); ++i) {
    const auto& node = scene.nodes[i];
    const auto  b    = body(i);
    if (node.kind != Kind::macro && node.kind != Kind::fixed) {
      if (!node.movable && node.kind == Kind::cell) {
        obstacle(b);
      }
      continue;
    }
    macros.push_back(i);
    obstacle(b, scene.halo_x, scene.halo_y);
    const auto gaps = boundary_gaps(scene, b);
    const auto [side, distance] = gaps.nearest();
    w.StartObject();
    w.Key("name");
    name(node.name);
    w.Key("body");
    rect(b);
    w.Key("boundary_side");
    if (side.empty()) {
      w.Null();
    } else {
      name(side);
    }
    w.Key("edge_distance");
    if (std::isfinite(distance)) {
      w.Double(distance);
    } else {
      w.Null();
    }
    // A legal macro can still leave a strip too shallow for even one cell.
    // These are geometric upper bounds; other obstacles and site alignment
    // can reduce capacity. Never treat them as a legalization verdict.
    const auto [halo_side, gap] = gaps.nearest(scene.halo_x, scene.halo_y);
    w.Key("halo_boundary_side");
    if (halo_side.empty()) {
      w.Null();
    } else {
      name(halo_side);
    }
    const bool   vertical = halo_side == "top" || halo_side == "bottom";
    const double pitch    = vertical ? scene.row_height : scene.site_width;
    w.Key("edge_halo_clearance_dbu");
    if (std::isfinite(gap)) {
      w.Double(gap);
    } else {
      w.Null();
    }
    w.Key("edge_pitch_capacity");
    if (std::isfinite(gap) && pitch > 0) {
      w.Double(std::floor(std::max(0., gap) / pitch));
    } else {
      w.Null();
    }
    w.Key("orientation");
    name(node.orient);
    struct Face {
      unsigned pins = 0, connected = 0, peers = 0, away = 0;
      double   distance = 0;
    };
    std::array<Face, 4> faces{};
    for (const auto& pin : node.signal_pins) {
      const std::array edge{pin.x, node.w - pin.x, pin.y, node.h - pin.y};
      const int        face = std::ranges::min_element(edge) - edge.begin();
      auto&            f    = faces[face];
      ++f.pins;
      if (!pin.logic.empty()) {
        ++f.connected;
      }
      for (int peer : pin.logic) {
        const auto& other = scene.nodes.at(peer);
        const auto  dx = frame.x.at(peer) + other.w / 2 - b.xl - pin.x, dy = frame.y.at(peer) + other.h / 2 - b.yl - pin.y;
        f.away += (face == 0 && dx > 0) || (face == 1 && dx < 0) || (face == 2 && dy > 0) || (face == 3 && dy < 0);
        ++f.peers;
        f.distance += std::abs(dx) + std::abs(dy);
      }
    }
    w.Key("pin_access_proxy");
    w.StartObject();
    for (int j = 0; j < 4; ++j) {
      const auto& f = faces[j];
      w.Key(face_names[j].data());
      w.StartObject();
      const double face_gap = gaps.body[j] - (j < 2 ? scene.halo_x : scene.halo_y);
      const double face_pitch = j < 2 ? scene.site_width : scene.row_height;
      w.Key("edge_halo_clearance_dbu");
      if (std::isfinite(face_gap)) {
        w.Double(face_gap);
      } else {
        w.Null();
      }
      w.Key("edge_pitch_capacity");
      if (std::isfinite(face_gap) && face_pitch > 0) {
        w.Double(std::floor(std::max(0., face_gap) / face_pitch));
      } else {
        w.Null();
      }
      w.Key("pins");
      w.Uint(f.pins);
      w.Key("connected_pins");
      w.Uint(f.connected);
      w.Key("logic_connections");
      w.Uint(f.peers);
      w.Key("logic_behind_pin");
      w.Uint(f.away);
      w.Key("mean_manhattan_dbu");
      if (f.peers) {
        w.Double(f.distance / f.peers);
      } else {
        w.Null();
      }
      w.EndObject();
    }
    w.EndObject();
    w.EndObject();
  }
  w.EndArray();
  w.Key("clusters");
  w.StartArray();
  for (const auto& [cluster, members] : scene.groups) {
    Box bounds{INFINITY, INFINITY, -INFINITY, -INFINITY};
    size_t count = 0;
    for (int i : members) {
      if (i < 0 || size_t(i) >= scene.nodes.size() || size_t(i) >= frame.x.size() || size_t(i) >= frame.y.size()) {
        continue;
      }
      const auto b = body(i);
      bounds.xl = std::min(bounds.xl, b.xl);
      bounds.xh = std::max(bounds.xh, b.xh);
      bounds.yl = std::min(bounds.yl, b.yl);
      bounds.yh = std::max(bounds.yh, b.yh);
      ++count;
    }
    if (!count) {
      continue;
    }
    w.StartObject();
    w.Key("name");
    name(cluster);
    w.Key("members");
    w.Uint64(count);
    w.Key("box");
    rect(bounds);
    w.Key("half_perimeter_dbu");
    w.Double(bounds.xh - bounds.xl + bounds.yh - bounds.yl);
    w.EndObject();
  }
  w.EndArray();
  auto report = measure_dead_space(blocked, limits, false);
  w.Key("dead_space");
  w.StartObject();
  w.Key("pitch");
  w.Int64(report.pitch);
  w.Key("free_dbu2");
  w.Int64(report.free_area);
  w.Key("stranded_dbu2");
  w.Int64(report.stranded_area);
  w.Key("components");
  w.StartArray();
  for (const auto& c : report.components) {
    w.StartObject();
    w.Key("cause");
    w.String(c.kind == Dead_kind::notch ? "notch" : c.kind == Dead_kind::pocket ? "pocket" : "island");
    w.Key("area_dbu2");
    w.Int64(c.area);
    w.Key("box");
    w.StartArray();
    for (auto edge : {c.bbox.xl, c.bbox.yl, c.bbox.xh, c.bbox.yh}) {
      w.Int64(edge);
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  w.Key("strips");
  w.StartArray();
  // A vertical strip is narrow in x, a horizontal one in y. Search each axis
  // by the cell's lower edge and include cells that only partially enter the strip.
  std::array<std::vector<size_t>, 2> cells;
  std::array<double, 2> max_extent{};
  if (macros.size() > 1) {
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
      const auto& node = scene.nodes[i];
      if (node.kind != Kind::cell || node.exclusion) {
        continue;
      }
      const auto b = body(i);
      cells[0].push_back(i);
      max_extent[0] = std::max(max_extent[0], b.xh - b.xl);
      max_extent[1] = std::max(max_extent[1], b.yh - b.yl);
    }
    cells[1] = cells[0];
    std::ranges::sort(cells[0], {}, [&](size_t i) { return frame.x[i]; });
    std::ranges::sort(cells[1], {}, [&](size_t i) { return frame.y[i]; });
  }
  // Report facing parallel edges longer than their separation. Measure free
  // area and entrances after intersecting the other bodies/exclusions.
  for (size_t i = 0; i < macros.size(); ++i) {
    for (size_t j = i + 1; j < macros.size(); ++j) {
      const auto left_body = body(macros[i]), right_body = body(macros[j]);
      for (bool vertical : {false, true}) {
        const auto facing = facing_strip(left_body, right_body, vertical);
        if (!facing) {
          continue;
        }
        const Box strip{std::max(facing->xl, scene.die.xl), std::max(facing->yl, scene.die.yl),
                        std::min(facing->xh, scene.die.xh), std::min(facing->yh, scene.die.yh)};
        if (strip.xl >= strip.xh || strip.yl >= strip.yh) {
          continue;
        }
        const auto       lo = vertical ? strip.yl : strip.xl, hi = vertical ? strip.yh : strip.xh;
        const auto       near = vertical ? strip.xl : strip.yl, far = vertical ? strip.xh : strip.yh;
        std::vector<Box> obstacles = scene.exclusions;
        for (auto k : macros) {
          if (k != macros[i] && k != macros[j]) {
            obstacles.push_back(body(k));
          }
        }
        // Keep edge contact for the entrance check; unrelated obstacles cannot affect the cells in this strip.
        std::erase_if(obstacles, [&](const Box& b) {
          return b.xh < strip.xl || b.xl > strip.xh || b.yh < strip.yl || b.yl > strip.yh;
        });
        const auto area = usable_area(strip, obstacles);
        if (area == 0) {
          continue;
        }
        const int axis = vertical ? 0 : 1;
        const auto& indices = cells[axis];
        const auto& positions = vertical ? frame.x : frame.y;
        const auto first = std::ranges::lower_bound(indices, near - max_extent[axis], {}, [&](size_t cell) { return positions[cell]; });
        double cell_area = 0;
        size_t cell_count = 0;
        for (auto it = first; it != indices.end() && positions[*it] < far; ++it) {
          const auto b = body(*it);
          const Box part{std::max(strip.xl, b.xl), std::max(strip.yl, b.yl),
                         std::min(strip.xh, b.xh), std::min(strip.yh, b.yh)};
          if (part.xl >= part.xh || part.yl >= part.yh) {
            continue;
          }
          const auto occupied = obstacles.empty() ? (part.xh - part.xl) * (part.yh - part.yl) : usable_area(part, obstacles);
          if (occupied > 0) {
            ++cell_count;
            cell_area += occupied;
          }
        }
        w.StartObject();
        w.Key("members");
        w.StartArray();
        name(scene.nodes[macros[i]].name);
        name(scene.nodes[macros[j]].name);
        w.EndArray();
        w.Key("box");
        rect(strip);
        w.Key("free_dbu2");
        w.Double(area);
        w.Key("standard_cells");
        w.Uint64(cell_count);
        w.Key("standard_cell_area_dbu2");
        w.Double(cell_area);
        w.Key("standard_cell_utilization");
        w.Double(cell_area / area);
        w.Key("entrances");
        w.StartArray();
        for (double edge : {lo, hi}) {
          if (vertical ? edge == scene.die.yl || edge == scene.die.yh : edge == scene.die.xl || edge == scene.die.xh) {
            continue;  // the core boundary cannot be an entrance
          }
          std::vector<Interval> intervals;
          for (const auto& o : obstacles) {
            if (vertical ? (o.yl <= edge && o.yh >= edge) : (o.xl <= edge && o.xh >= edge)) {
              intervals.emplace_back(vertical ? o.xl : o.yl, vertical ? o.xh : o.yh);
            }
          }
          std::ranges::sort(intervals);
          auto next     = near;
          auto entrance = [&](double stop) {
            if (next < stop) {
              rect(vertical ? Box{next, edge, stop, edge} : Box{edge, next, edge, stop});
            }
          };
          for (const auto& [start, stop] : intervals) {
            entrance(std::min(start, far));
            next = std::max(next, stop);
          }
          entrance(far);
        }
        w.EndArray();
        w.EndObject();
      }
    }
  }
  w.EndArray();
  w.EndObject();
  return buffer.GetString();
}

Grid occupancy_grid(const Scene& scene, const Frame& frame, int bins) {
  Grid         grid;
  const auto&  core = scene.die;
  const double w = (core.xh - core.xl) / bins, h = (core.yh - core.yl) / bins;
  if (w <= 0 || h <= 0) {
    grid.note = "empty core";
    return grid;
  }
  grid.bx = grid.by = bins;
  grid.v.resize(size_t(bins) * bins);
  std::vector<double> occupied(grid.v.size());
  const auto          tile
      = [&](int x, int y) { return Box{core.xl + x * w, core.yl + y * h, core.xl + (x + 1) * w, core.yl + (y + 1) * h}; };
  for (size_t i = 0; i < scene.nodes.size(); ++i) {
    const auto& node = scene.nodes[i];
    if (node.kind == Kind::io || node.kind == Kind::blockage || node.exclusion) {
      continue;
    }
    const double xl = frame.x[i], yl = frame.y[i], xh = xl + node.w, yh = yl + node.h;
    const auto   bin = [&](double xy, double origin, double pitch) {
      return int(std::clamp(std::floor((xy - origin) / pitch), 0., double(bins - 1)));
    };
    for (int x = bin(xl, core.xl, w); x <= bin(xh, core.xl, w); ++x) {
      for (int y = bin(yl, core.yl, h); y <= bin(yh, core.yl, h); ++y) {
        const auto b = tile(x, y);
        const Box  intersection{std::max(b.xl, xl), std::max(b.yl, yl), std::min(b.xh, xh), std::min(b.yh, yh)};
        if (intersection.xh > intersection.xl && intersection.yh > intersection.yl) {
          occupied[size_t(x) * bins + y] += scene.exclusions.empty()
                                                ? (intersection.xh - intersection.xl) * (intersection.yh - intersection.yl)
                                                : usable_area(intersection, scene.exclusions);
        }
      }
    }
  }
  for (int x = 0; x < bins; ++x) {
    for (int y = 0; y < bins; ++y) {
      const auto   k     = size_t(x) * bins + y;
      const double area  = scene.exclusions.empty() ? w * h : usable_area(tile(x, y), scene.exclusions);
      grid.v[k]          = area > 0 ? occupied[k] / area : 0;
      grid.peak          = std::max(grid.peak, double(grid.v[k]));
      grid.overflow     += std::max(0., occupied[k] - area);
    }
  }
  grid.note = "Occupied cell and macro area divided by usable bin area; overflow in square DBU";
  return grid;
}
std::string grid_findings(const Scene& scene, const Frame& frame, const std::string& kind, const Grid& grid) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> out(buffer);
  out.StartObject();
  out.Key("kind");
  out.String(kind.c_str());
  out.Key("frame");
  out.Uint64(frame.id);
  out.Key("available");
  out.Bool(!grid.v.empty());
  out.Key("note");
  out.String(grid.note.c_str());
  if (!grid.v.empty()) {
    out.Key("peak");
    out.Double(grid.peak);
    out.Key("overflow");
    out.Double(grid.overflow);
    out.Key("over_capacity_share");
    out.Double(double(std::ranges::count_if(grid.v, [](float v) { return v > 1; })) / grid.v.size());
    std::vector<size_t> ranking(grid.v.size());
    std::iota(ranking.begin(), ranking.end(), 0);
    size_t n = std::min(size_t{8}, ranking.size());
    std::partial_sort(ranking.begin(), ranking.begin() + n, ranking.end(), [&](auto a, auto b) {
      return grid.v[a] == grid.v[b] ? a < b : grid.v[a] > grid.v[b];
    });
    const double dx = (scene.die.xh - scene.die.xl) / grid.bx, dy = (scene.die.yh - scene.die.yl) / grid.by;
    out.Key("hotspots");
    out.StartArray();
    for (size_t k = 0; k < n && grid.v[ranking[k]] > 0; ++k) {
      const size_t idx = ranking[k];
      const double x = scene.die.xl + (idx / grid.by) * dx, y = scene.die.yl + (idx % grid.by) * dy;
      out.StartObject();
      out.Key("value");
      out.Double(grid.v[idx]);
      out.Key("box_dbu");
      out.StartArray();
      for (auto edge : {x, y, x + dx, y + dy}) {
        out.Double(edge);
      }
      out.EndArray();
      out.Key("overlapping_macros");
      out.StartArray();
      for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const auto& m = scene.nodes[i];
        if ((m.kind == Kind::fixed || m.kind == Kind::macro) && !m.exclusion && frame.x[i] < x + dx && frame.y[i] < y + dy
            && frame.x[i] + m.w > x && frame.y[i] + m.h > y) {
          out.String(m.name.c_str());
        }
      }
      out.EndArray();
      out.EndObject();
    }
    out.EndArray();
  }
  out.EndObject();
  return buffer.GetString();
}
std::string macro_signature(const Scene& scene, const Frame& frame) {
  struct Entry {
    std::string name, group, side, orient;
    int         x, y, order = 0;
    double      along;
  };
  std::vector<Entry> entries;
  constexpr int      bins = 16;
  const auto         quantize
      = [](double center, double lo, double hi) { return int(std::floor((center - lo) * bins / std::max(1., hi - lo))); };
  for (size_t i = 0; i < scene.nodes.size(); ++i) {
    const auto& n = scene.nodes[i];
    if (n.kind != Kind::macro && n.kind != Kind::fixed) {
      continue;
    }
    Box        body{frame.x.at(i), frame.y.at(i), frame.x.at(i) + n.w, frame.y.at(i) + n.h};
    const auto side = boundary_gaps(scene, body).nearest().first;
    const auto x = (body.xl + body.xh) / 2, y = (body.yl + body.yh) / 2;
    entries.push_back({n.name,
                       n.group,
                       std::string(side),
                       n.orient,
                       quantize(x, scene.die.xl, scene.die.xh),
                       quantize(y, scene.die.yl, scene.die.yh),
                       0,
                       side == "left" || side == "right" ? y : x});
  }
  std::ranges::sort(entries, [](const auto& a, const auto& b) {
    return std::tie(a.group, a.side, a.along, a.name) < std::tie(b.group, b.side, b.along, b.name);
  });
  for (size_t i = 1; i < entries.size(); ++i) {
    if (entries[i].group == entries[i - 1].group && entries[i].side == entries[i - 1].side) {
      entries[i].order = entries[i - 1].order + 1;
    }
  }
  std::ranges::sort(entries, {}, &Entry::name);
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("bins_per_axis");
  w.Int(bins);
  w.Key("macros");
  w.StartObject();
  for (const auto& e : entries) {
    w.Key(e.name.c_str());
    w.StartObject();
    w.Key("group");
    w.String(e.group.c_str());
    w.Key("side");
    w.String(e.side.c_str());
    w.Key("order");
    w.Int(e.order);
    w.Key("orient");
    w.String(e.orient.c_str());
    w.Key("position");
    w.StartArray();
    w.Int(e.x);
    w.Int(e.y);
    w.EndArray();
    w.EndObject();
  }
  w.EndObject();
  w.EndObject();
  return buffer.GetString();
}

std::string endpoint_evidence(const Scene& scene, const Frame& frame, timing::Whatif& timer) {
  struct Group {
    hash_util::Field_combiner population, clock_population;
    unsigned endpoints = 0, failing = 0, resolved = 0, macro = 0, channel = 0, either = 0;
    double   wns = std::numeric_limits<double>::infinity(), tns = 0, macro_tns = 0, channel_tns = 0, either_tns = 0;
    void     add(uint64_t endpoint, uint64_t clock_endpoint, double slack, bool path, bool touches, bool crosses) {
      population.add(endpoint);
      clock_population.add(clock_endpoint);
      ++endpoints;
      wns = std::min(wns, slack);
      if (slack >= 0) {
        return;
      }
      ++failing;
      tns += slack;
      if (path) {
        ++resolved;
      }
      if (touches) {
        ++macro;
        macro_tns += slack;
      }
      if (crosses) {
        ++channel;
        channel_tns += slack;
      }
      if (touches || crosses) {
        ++either;
        either_tns += slack;
      }
    }
  };
  std::vector<Box>   bodies, strips;
  std::vector<Point> centers;
  for (size_t i = 0; i < scene.nodes.size(); ++i) {
    const auto& node = scene.nodes[i];
    const Box   body{frame.x.at(i), frame.y.at(i), frame.x.at(i) + node.w, frame.y.at(i) + node.h};
    centers.push_back({(body.xl + body.xh) / 2, (body.yl + body.yh) / 2});
    if (node.kind == Kind::macro || node.kind == Kind::fixed) {
      bodies.push_back(body);
    }
  }
  for (size_t i = 0; i < bodies.size(); ++i) {
    for (size_t j = i + 1; j < bodies.size(); ++j) {
      for (bool vertical : {true, false}) {
        if (auto strip = facing_strip(bodies[i], bodies[j], vertical)) {
          // The projected channel must have free area between the facing edges.
          std::vector<Box> blocked = scene.exclusions;
          for (size_t k = 0; k < bodies.size(); ++k) {
            if (k != i && k != j) {
              blocked.push_back(bodies[k]);
            }
          }
          sweep_core(*strip, blocked, [&](double xl, double xh, double yl, double yh) { strips.push_back({xl, yl, xh, yh}); });
        }
      }
    }
  }
  std::map<std::pair<int, int>, bool>  segments;
  std::unordered_map<std::string, int> owners;
  auto                                 index = [&](const std::string& pin) {
    auto [it, added] = owners.try_emplace(pin, -1);
    if (added) {
      auto node = scene.index.find(pin.substr(0, pin.rfind(':')));
      if (node != scene.index.end()) {
        it->second = node->second;
      }
    }
    return it->second;
  };
  auto type = [&](int i) -> std::string {
    if (i < 0) {
      return "unresolved";
    }
    const auto& n = scene.nodes[i];
    if (n.kind == Kind::io) {
      return "io";
    }
    if (n.kind == Kind::macro || n.kind == Kind::fixed) {
      return "macro";
    }
    return n.seq ? "register" : "logic";
  };
  Group                        total;
  std::map<std::string, Group> clocks, families;
  bool                         available = timer.visit_setup_paths(
      [&](const std::string& end, double slack, const std::string& clock, const std::vector<std::string>& pins) {
        int  previous = -1;
        bool macro = false, channel = false, complete = !pins.empty();
        for (const auto& pin : pins) {
          int i = index(pin);
          if (i < 0) {
            complete = false;
            previous = -1;
            continue;
          }
          macro |= scene.nodes[i].kind == Kind::macro || scene.nodes[i].kind == Kind::fixed;
          if (!channel && i != previous && previous >= 0) {
            auto [it, added] = segments.try_emplace(std::minmax(previous, i), false);
            if (added) {
              it->second
                  = std::ranges::any_of(strips, [&](const Box& box) { return crosses_box(centers[previous], centers[i], box); });
            }
            channel = it->second;
          }
          previous = i;
        }
        const auto launch = pins.empty() ? "unresolved" : type(index(pins.front())), capture = type(index(end));
        const auto family = launch == "unresolved" || capture == "unresolved" ? "unresolved"
                                                    : launch == "io" || capture == "io"               ? "io"
                                                                                                      : launch + "_to_" + capture;
        const auto endpoint = hash_util::fnv1a64(end);
        const auto clock_endpoint = hash_util::combine64(endpoint, hash_util::fnv1a64(clock));
        for (auto* group : {&total, &clocks[clock.empty() ? "unresolved" : clock], &families[family]}) {
          group->add(endpoint, clock_endpoint, slack, complete, macro, channel);
        }
      });
  if (!available) {
    return "null";
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  auto                                       group = [&](const Group& g) {
    writer.StartObject();
    auto count = [&](const char* key, unsigned value) {
      writer.Key(key);
      writer.Uint(value);
    };
    auto ratio = [&](const char* key, double a, double b) {
      writer.Key(key);
      if (b) {
        writer.Double(a / b);
      } else {
        writer.Null();
      }
    };
    count("endpoints", g.endpoints);
    writer.Key("population");
    writer.String(std::format("{:016x}", g.population.value()).c_str());
    writer.Key("clock_population");
    writer.String(std::format("{:016x}", g.clock_population.value()).c_str());
    count("failing", g.failing);
    count("resolved_failing_paths", g.resolved);
    writer.Key("wns_ps");
    if (g.endpoints) {
      writer.Double(g.wns);
    } else {
      writer.Null();
    }
    writer.Key("tns_ps");
    writer.Double(g.tns);
    count("macro_pin_failing", g.macro);
    count("channel_proxy_failing", g.channel);
    count("macro_or_channel_failing", g.either);
    writer.Key("macro_pin_tns_ps");
    writer.Double(g.macro_tns);
    writer.Key("channel_proxy_tns_ps");
    writer.Double(g.channel_tns);
    ratio("known_macro_or_channel_endpoint_share", g.either, g.failing);
    ratio("known_macro_or_channel_tns_share", g.either_tns, g.tns);
    writer.EndObject();
  };
  auto groups = [&](const char* key, const auto& table) {
    writer.Key(key);
    writer.StartObject();
    for (const auto& [label, value] : table) {
      writer.Key(label.c_str());
      group(value);
    }
    writer.EndObject();
  };
  writer.StartObject();
  auto analysis = timer.setup_analysis();
  writer.Key("engine");
  writer.String(analysis.engine.c_str());
  writer.Key("setup_corners");
  writer.StartArray();
  for (const auto& corner : analysis.corners) {
    writer.String(corner.c_str());
  }
  writer.EndArray();
  hash_util::Field_combiner clock_periods;
  writer.Key("clock_periods_ps");
  writer.StartObject();
  for (const auto& [name, period] : analysis.periods_ps) {
    clock_periods.add(hash_util::fnv1a64_u64(std::bit_cast<uint64_t>(period), hash_util::fnv1a64(name)));
    writer.Key(name.c_str());
    writer.Double(period);
  }
  writer.EndObject();
  writer.Key("clock_periods_hash");
  writer.String(std::format("{:016x}", clock_periods.value()).c_str());
  auto& issues = analysis.constraint_issues;
  std::ranges::sort(issues);
  writer.Key("constraint_issues");
  writer.StartArray();
  hash_util::Field_combiner issue_hash;
  for (const auto& issue : issues) {
    issue_hash.add(hash_util::fnv1a64(issue));
    writer.String(issue.c_str());
  }
  writer.EndArray();
  writer.Key("constraint_issues_hash");
  writer.String(std::format("{:016x}", issue_hash.value()).c_str());
  const bool single_clock = analysis.periods_ps.size() == 1 && clocks.size() == 1
                            && analysis.periods_ps.contains(clocks.begin()->first);
  const double period = single_clock ? analysis.periods_ps.begin()->second : 0;
  writer.Key("frequency_mhz");
  if (single_clock && analysis.full_cycle && period > 0 && total.endpoints && period > total.wns) {
    writer.Double(1e6 / (period - total.wns));
  } else {
    writer.Null();
  }
  writer.Key("frequency_status");
  writer.String(!total.endpoints ? "no_constrained_endpoints"
                : analysis.periods_ps.size() > 1 ? "multiple_clocks"
                : !single_clock ? "unresolved_clock"
                : !analysis.full_cycle ? "unverified_period_dependence"
                : period <= 0 || period <= total.wns ? "nonpositive_period" : "available");
  writer.Key("channel_model");
  writer.String("straight segments between cell centers crossing free macro strips");
  writer.Key("total");
  group(total);
  groups("capture_clocks", clocks);
  groups("path_families", families);
  writer.EndObject();
  return buffer.GetString();
}

}  // namespace livehd::placement::gui
