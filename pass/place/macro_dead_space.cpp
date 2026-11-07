// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_dead_space.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

namespace livehd::placement {
namespace {
int64_t ceil_div(int64_t a, int64_t b) { return a >= 0 ? (a + b - 1) / b : -((-a) / b); }

// The classified raster. `dead` holds 1 + Dead_kind for dead cells, 0 otherwise.
struct Raster {
  int                  w = 0, h = 0;
  int64_t              p = 1;
  std::vector<uint8_t> free, dead;
  std::vector<uint8_t> main;  // open cells of the largest region
  int64_t              free_cells = 0, dead_cells = 0;
  int64_t              stranded_cells = 0;  // dead cells plus non-channel cells of kept regions other than the largest
};

void check(const Dead_space_limits& l) {
  if (l.pitch <= 0 || l.core_xh <= l.core_xl || l.core_yh <= l.core_yl || l.channel_x < 0 || l.channel_y < 0 || l.narrow_x < 0
      || l.narrow_y < 0 || l.min_area < 0 || !(l.pocket_fraction >= 0 && l.pocket_fraction <= 1)) {
    throw std::invalid_argument("invalid macro dead-space limits");
  }
}

// 4-connected labels of the cells where `mask` is set; returns the count.
int label(const Raster& r, const std::vector<uint8_t>& mask, std::vector<int>& lab, std::vector<int>& queue) {
  const auto n = static_cast<size_t>(r.w) * r.h;
  lab.assign(n, -1);
  int count = 0;
  for (size_t s = 0; s < n; ++s) {
    if (!mask[s] || lab[s] >= 0) {
      continue;
    }
    queue.clear();
    queue.push_back(static_cast<int>(s));
    lab[s] = count;
    for (size_t q = 0; q < queue.size(); ++q) {
      const int c = queue[q], x = c % r.w, y = c / r.w;
      for (const auto& [nx, ny] : std::array<std::pair<int, int>, 4>{
               {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}}
      }) {
        if (nx < 0 || ny < 0 || nx >= r.w || ny >= r.h) {
          continue;
        }
        const int m = ny * r.w + nx;
        if (mask[m] && lab[m] < 0) {
          lab[m] = count;
          queue.push_back(m);
        }
      }
    }
    ++count;
  }
  return count;
}

Raster classify(const std::vector<Dead_space_rect>& obstacles, const Dead_space_limits& l) {
  Raster r;
  r.p              = l.pitch;
  const auto p     = l.pitch;
  const auto width = l.core_xh - l.core_xl, height = l.core_yh - l.core_yl;
  r.w          = static_cast<int>(ceil_div(width, p));
  r.h          = static_cast<int>(ceil_div(height, p));
  const auto n = static_cast<size_t>(r.w) * r.h;
  r.free.assign(n, 1);
  // Cell centers past the core edge are outside.
  const auto first_out = [&](int64_t lo, int64_t hi) { return static_cast<int>(ceil_div(hi - lo - p / 2, p)); };
  const int  wx = first_out(l.core_xl, l.core_xh), wy = first_out(l.core_yl, l.core_yh);
  for (int y = 0; y < r.h; ++y) {
    for (int x = 0; x < r.w; ++x) {
      if (x >= wx || y >= wy) {
        r.free[static_cast<size_t>(y) * r.w + x] = 0;
      }
    }
  }
  for (const auto& o : obstacles) {
    const int x0 = static_cast<int>(std::clamp<int64_t>(ceil_div(o.xl - l.core_xl - p / 2, p), 0, r.w));
    const int x1 = static_cast<int>(std::clamp<int64_t>(ceil_div(o.xh - l.core_xl - p / 2, p), 0, r.w));
    const int y0 = static_cast<int>(std::clamp<int64_t>(ceil_div(o.yl - l.core_yl - p / 2, p), 0, r.h));
    const int y1 = static_cast<int>(std::clamp<int64_t>(ceil_div(o.yh - l.core_yl - p / 2, p), 0, r.h));
    for (int y = y0; y < y1; ++y) {
      std::fill_n(r.free.begin() + static_cast<ptrdiff_t>(y) * r.w + x0, std::max(0, x1 - x0), uint8_t{0});
    }
  }
  // Free run lengths through each cell, in cells.
  std::vector<int> hr(n, 0), vr(n, 0);
  for (int y = 0; y < r.h; ++y) {
    for (int x = 0; x < r.w;) {
      const auto row = static_cast<size_t>(y) * r.w;
      if (!r.free[row + x]) {
        ++x;
        continue;
      }
      int e = x;
      while (e < r.w && r.free[row + e]) {
        ++e;
      }
      std::fill(hr.begin() + static_cast<ptrdiff_t>(row) + x, hr.begin() + static_cast<ptrdiff_t>(row) + e, e - x);
      x = e;
    }
  }
  for (int x = 0; x < r.w; ++x) {
    for (int y = 0; y < r.h;) {
      if (!r.free[static_cast<size_t>(y) * r.w + x]) {
        ++y;
        continue;
      }
      int e = y;
      while (e < r.h && r.free[static_cast<size_t>(e) * r.w + x]) {
        ++e;
      }
      for (int k = y; k < e; ++k) {
        vr[static_cast<size_t>(k) * r.w + x] = e - y;
      }
      y = e;
    }
  }
  std::vector<uint8_t> chan(n, 0), open(n, 0);
  for (size_t i = 0; i < n; ++i) {
    if (!r.free[i]) {
      continue;
    }
    ++r.free_cells;
    const auto hx = int64_t(hr[i]) * p, vy = int64_t(vr[i]) * p;
    chan[i] = hx <= l.channel_x + p || vy <= l.channel_y + p;
    open[i] = !chan[i] && hx >= l.narrow_x && vy >= l.narrow_y;
  }
  std::vector<int> queue, region;
  const int        regions = label(r, open, region, queue);
  // Every other free cell joins the nearest open region through free cells (breadth first).
  queue.clear();
  for (size_t i = 0; i < n; ++i) {
    if (region[i] >= 0) {
      queue.push_back(static_cast<int>(i));
    }
  }
  for (size_t q = 0; q < queue.size(); ++q) {
    const int c = queue[q], x = c % r.w, y = c / r.w;
    for (const auto& [nx, ny] : std::array<std::pair<int, int>, 4>{
             {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}}
    }) {
      if (nx < 0 || ny < 0 || nx >= r.w || ny >= r.h) {
        continue;
      }
      const int m = ny * r.w + nx;
      if (r.free[m] && region[m] < 0) {
        region[m] = region[c];
        queue.push_back(m);
      }
    }
  }
  std::vector<int64_t> area(regions, 0), open_area(regions, 0);
  for (size_t i = 0; i < n; ++i) {
    if (region[i] >= 0) {
      ++area[region[i]];
      open_area[region[i]] += open[i];
    }
  }
  std::vector<uint8_t> kept(regions, 0);
  int                  largest = -1;
  for (int k = 0; k < regions; ++k) {
    kept[k] = double(area[k]) >= l.pocket_fraction * double(r.free_cells);
    if (largest < 0 || area[k] > area[largest]) {
      largest = k;
    }
  }
  if (largest >= 0) {
    kept[largest] = 1;
  }
  // Islands: connected free components (channels included) below min_area.
  std::vector<int>     island;
  const int            islands = label(r, r.free, island, queue);
  std::vector<int64_t> island_area(islands, 0);
  for (size_t i = 0; i < n; ++i) {
    if (island[i] >= 0) {
      ++island_area[island[i]];
    }
  }
  r.dead.assign(n, 0);
  const auto cell_area = p * p;
  for (size_t i = 0; i < n; ++i) {
    if (!r.free[i]) {
      continue;
    }
    if (island_area[island[i]] * cell_area < l.min_area) {
      r.dead[i] = 1 + uint8_t(Dead_kind::island);
    } else if (chan[i]) {
      continue;
    } else if (region[i] < 0 || !kept[region[i]]) {
      r.dead[i] = 1 + uint8_t(Dead_kind::pocket);
    } else if (!open[i]) {
      r.dead[i] = 1 + uint8_t(Dead_kind::notch);
    }
  }
  r.main.assign(n, 0);
  for (size_t i = 0; i < n; ++i) {
    r.dead_cells     += r.dead[i] != 0;
    r.stranded_cells += r.dead[i] != 0 || (r.free[i] && !chan[i] && region[i] >= 0 && region[i] != largest);
    r.main[i]         = open[i] && region[i] == largest;
  }
  return r;
}

Dead_space_report report_of(const Raster& r, const Dead_space_limits& l, bool outlines, std::vector<int>* component_of = nullptr) {
  Dead_space_report out;
  out.pitch              = r.p;
  out.grid_x             = r.w;
  out.grid_y             = r.h;
  const auto ca          = r.p * r.p;
  out.free_area          = r.free_cells * ca;
  const auto           n = r.dead.size();
  std::vector<int>     lab, queue;
  std::vector<uint8_t> mask(n);
  for (size_t i = 0; i < n; ++i) {
    mask[i] = r.dead[i] != 0;
  }
  const int count = label(r, mask, lab, queue);
  struct Acc {
    int64_t                cells = 0;
    int                    x0 = std::numeric_limits<int>::max(), y0 = std::numeric_limits<int>::max(), x1 = -1, y1 = -1;
    std::array<int64_t, 3> kinds{};
  };
  std::vector<Acc> acc(count);
  for (size_t i = 0; i < n; ++i) {
    if (lab[i] < 0) {
      continue;
    }
    auto&     a = acc[lab[i]];
    const int x = static_cast<int>(i % r.w), y = static_cast<int>(i / r.w);
    ++a.cells;
    a.x0 = std::min(a.x0, x);
    a.y0 = std::min(a.y0, y);
    a.x1 = std::max(a.x1, x);
    a.y1 = std::max(a.y1, y);
    ++a.kinds[r.dead[i] - 1];
    const auto kind = static_cast<Dead_kind>(r.dead[i] - 1);
    (kind == Dead_kind::notch ? out.notch_area : kind == Dead_kind::pocket ? out.pocket_area : out.island_area) += ca;
  }
  out.dead_area     = out.notch_area + out.pocket_area + out.island_area;
  out.stranded_area = r.stranded_cells * ca;
  std::vector<int> order(count);
  for (int k = 0; k < count; ++k) {
    order[k] = k;
  }
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return acc[a].cells > acc[b].cells; });
  std::vector<int> rank(count);
  for (int k = 0; k < count; ++k) {
    rank[order[k]] = k;
  }
  const auto clip = [&](int64_t v, int64_t lo, int64_t hi) { return std::clamp(v, lo, hi); };
  for (const int k : order) {
    const auto&          a = acc[k];
    Dead_space_component c;
    c.area = a.cells * ca;
    c.bbox = {clip(l.core_xl + int64_t(a.x0) * r.p, l.core_xl, l.core_xh),
              clip(l.core_yl + int64_t(a.y0) * r.p, l.core_yl, l.core_yh),
              clip(l.core_xl + int64_t(a.x1 + 1) * r.p, l.core_xl, l.core_xh),
              clip(l.core_yl + int64_t(a.y1 + 1) * r.p, l.core_yl, l.core_yh)};
    c.kind = static_cast<Dead_kind>(std::max_element(a.kinds.begin(), a.kinds.end()) - a.kinds.begin());
    out.components.push_back(c);
  }
  if (outlines) {
    // Row runs of each component, merged with an identical run on the row below.
    std::vector<std::vector<std::tuple<int, int, size_t>>> open_runs(count);  // (x0, x1, rect index) ending on the last row
    for (int y = 0; y < r.h; ++y) {
      std::vector<std::vector<std::tuple<int, int, size_t>>> next(count);
      for (int x = 0; x < r.w;) {
        const int k = lab[static_cast<size_t>(y) * r.w + x];
        if (k < 0) {
          ++x;
          continue;
        }
        int e = x;
        while (e < r.w && lab[static_cast<size_t>(y) * r.w + e] == k) {
          ++e;
        }
        auto&      rects = out.components[rank[k]].rects;
        const auto prev  = std::find_if(open_runs[k].begin(), open_runs[k].end(), [&](const auto& t) {
          return std::get<0>(t) == x && std::get<1>(t) == e;
        });
        size_t     idx;
        if (prev != open_runs[k].end()) {
          idx           = std::get<2>(*prev);
          rects[idx].yh = clip(l.core_yl + int64_t(y + 1) * r.p, l.core_yl, l.core_yh);
        } else {
          idx = rects.size();
          rects.push_back({clip(l.core_xl + int64_t(x) * r.p, l.core_xl, l.core_xh),
                           clip(l.core_yl + int64_t(y) * r.p, l.core_yl, l.core_yh),
                           clip(l.core_xl + int64_t(e) * r.p, l.core_xl, l.core_xh),
                           clip(l.core_yl + int64_t(y + 1) * r.p, l.core_yl, l.core_yh)});
        }
        next[k].emplace_back(x, e, idx);
        x = e;
      }
      open_runs = std::move(next);
    }
  }
  if (component_of) {
    component_of->assign(n, -1);
    for (size_t i = 0; i < n; ++i) {
      if (lab[i] >= 0) {
        (*component_of)[i] = rank[lab[i]];
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------- collapse
using Key = std::tuple<std::vector<size_t>, int64_t, int64_t>;  // members, dx, dy
struct Mover {
  std::vector<size_t> members;
  bool                x_axis;
  int                 sign;
};

std::vector<Dead_space_rect> obstacles_of(const std::vector<Dead_space_object>& objects, const Dead_space_collapse_limits& l) {
  std::vector<Dead_space_rect> out;
  out.reserve(objects.size());
  for (const auto& o : objects) {
    const auto hx = o.macro ? l.metric_halo_x : 0, hy = o.macro ? l.metric_halo_y : 0;
    out.push_back({o.xl - hx, o.yl - hy, o.xh + hx, o.yh + hy});
  }
  return out;
}

// The collapse objective: stranded area (dead area plus kept regions other than the largest), so that a move
// cannot "remove" a pocket by growing it past the pocket fraction.
int64_t stranded_area_of(const std::vector<Dead_space_object>& objects, const Dead_space_collapse_limits& l) {
  const auto r = classify(obstacles_of(objects, l), l.metric);
  return r.stranded_cells * r.p * r.p;
}

// Spans along (lo, hi) and across (a, b) the move axis.
struct Span {
  int64_t lo, hi, a, b;
};
Span span_of(const Dead_space_object& o, bool x_axis) {
  return x_axis ? Span{o.xl, o.xh, o.yl, o.yh} : Span{o.yl, o.yh, o.xl, o.xh};
}

// Largest legal translation (>= 0, a multiple of grid) of the members along the axis.
int64_t max_slide(const std::vector<Dead_space_object>& objects, const std::vector<uint8_t>& in_group, const Mover& m,
                  const Dead_space_collapse_limits& l) {
  const auto core_lo = m.x_axis ? l.metric.core_xl : l.metric.core_yl;
  const auto core_hi = m.x_axis ? l.metric.core_xh : l.metric.core_yh;
  const auto halo = m.x_axis ? l.halo_x : l.halo_y, cross_h = m.x_axis ? l.halo_y : l.halo_x;
  const auto space = m.x_axis ? l.space_x : l.space_y, cross = m.x_axis ? l.space_y : l.space_x;
  int64_t    limit = std::numeric_limits<int64_t>::max();
  for (const auto i : m.members) {
    const auto s = span_of(objects[i], m.x_axis);
    limit        = std::min(limit, m.sign > 0 ? core_hi - halo - s.hi : s.lo - (core_lo + halo));
    for (size_t j = 0; j < objects.size(); ++j) {
      if (in_group[j]) {
        continue;
      }
      const auto o   = span_of(objects[j], m.x_axis);
      const auto gap = objects[j].macro ? cross : cross_h;
      if (!(o.a < s.b + gap && s.a < o.b + gap)) {
        continue;
      }
      const auto clear = objects[j].macro ? space : halo;
      if (m.sign > 0 ? o.lo >= s.hi : o.hi <= s.lo) {
        limit = std::min(limit, m.sign > 0 ? o.lo - clear - s.hi : s.lo - (o.hi + clear));
      } else if (o.lo < s.hi && s.lo < o.hi) {
        return 0;  // overlapping along the axis within the across clearance: not a clean starting point
      }
    }
  }
  return limit <= 0 ? 0 : limit / l.grid * l.grid;
}

bool legal_after(const std::vector<Dead_space_object>& objects, const std::vector<uint8_t>& in_group,
                 const std::vector<size_t>& members, const Dead_space_collapse_limits& l) {
  for (const auto i : members) {
    const auto& a = objects[i];
    if (a.xl - l.halo_x < l.metric.core_xl || a.yl - l.halo_y < l.metric.core_yl || a.xh + l.halo_x > l.metric.core_xh
        || a.yh + l.halo_y > l.metric.core_yh) {
      return false;
    }
    for (size_t j = 0; j < objects.size(); ++j) {
      if (in_group[j]) {
        continue;
      }
      const auto& b  = objects[j];
      const auto  sx = b.macro ? l.space_x : l.halo_x, sy = b.macro ? l.space_y : l.halo_y;
      if (!(a.xh + sx <= b.xl || b.xh + sx <= a.xl || a.yh + sy <= b.yl || b.yh + sy <= a.yl)) {
        return false;
      }
    }
  }
  return true;
}

void shift(std::vector<Dead_space_object>& objects, const std::vector<size_t>& members, int64_t dx, int64_t dy) {
  for (const auto i : members) {
    objects[i].xl += dx;
    objects[i].xh += dx;
    objects[i].yl += dy;
    objects[i].yh += dy;
  }
}

// Close the member set over rigid groups; empty when a member cannot move.
std::vector<size_t> close_group(const std::vector<Dead_space_object>& objects, std::vector<size_t> members) {
  std::vector<uint8_t> in(objects.size(), 0);
  for (const auto i : members) {
    in[i] = 1;
  }
  for (size_t k = 0; k < members.size(); ++k) {
    const auto g = objects[members[k]].group;
    if (g == 0) {
      continue;
    }
    for (size_t j = 0; j < objects.size(); ++j) {
      if (!in[j] && objects[j].group == g) {
        in[j] = 1;
        members.push_back(j);
      }
    }
  }
  for (const auto i : members) {
    if (!objects[i].movable || !objects[i].macro) {
      return {};
    }
  }
  std::sort(members.begin(), members.end());
  return members;
}

// Movers for macro `seed` moving along the axis: itself, its aligned line
// (column for x moves, row for y moves) and the block slice behind the line.
std::vector<std::vector<size_t>> groups_for(const std::vector<Dead_space_object>& objects, size_t seed, bool x_axis, int sign,
                                            const Dead_space_collapse_limits& l) {
  const auto                       tol    = l.metric.pitch;
  const auto                       link_a = (x_axis ? l.space_y : l.space_x) + 2 * l.metric.pitch;  // array spacing across the axis
  const auto                       link_l = (x_axis ? l.space_x : l.space_y) + 2 * l.metric.pitch;  // array spacing along the axis
  std::vector<std::vector<size_t>> out;
  out.push_back({seed});
  // The line: same along-axis extent, chained across the axis.
  std::vector<size_t>  line{seed};
  std::vector<uint8_t> in(objects.size(), 0);
  in[seed]      = 1;
  const auto s0 = span_of(objects[seed], x_axis);
  for (size_t k = 0; k < line.size(); ++k) {
    const auto s = span_of(objects[line[k]], x_axis);
    for (size_t j = 0; j < objects.size(); ++j) {
      if (in[j] || !objects[j].macro) {
        continue;
      }
      const auto o = span_of(objects[j], x_axis);
      if (std::abs(o.lo - s0.lo) <= tol && std::abs(o.hi - s0.hi) <= tol && (o.a >= s.b ? o.a - s.b : s.a - o.b) <= link_a
          && !(o.a < s.b && s.a < o.b)) {
        in[j] = 1;
        line.push_back(j);
      }
    }
  }
  if (line.size() > 1) {
    out.push_back(line);
  }
  // The block slice: macros behind the line, inside its across extent, chained along the axis.
  int64_t a0 = std::numeric_limits<int64_t>::max(), b0 = std::numeric_limits<int64_t>::min();
  for (const auto i : line) {
    const auto s = span_of(objects[i], x_axis);
    a0           = std::min(a0, s.a);
    b0           = std::max(b0, s.b);
  }
  auto slice = line;
  for (size_t k = 0; k < slice.size(); ++k) {
    const auto s = span_of(objects[slice[k]], x_axis);
    for (size_t j = 0; j < objects.size(); ++j) {
      if (in[j] || !objects[j].macro) {
        continue;
      }
      const auto o      = span_of(objects[j], x_axis);
      const bool behind = sign > 0 ? o.hi <= s.lo && s.lo - o.hi <= link_l : o.lo >= s.hi && o.lo - s.hi <= link_l;
      if (behind && o.a >= a0 - tol && o.b <= b0 + tol && o.a < s.b + link_a && s.a < o.b + link_a) {
        in[j] = 1;
        slice.push_back(j);
      }
    }
  }
  if (slice.size() > line.size()) {
    out.push_back(slice);
  }
  for (auto& g : out) {
    g = close_group(objects, g);
  }
  std::erase_if(out, [](const auto& g) { return g.empty(); });
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}
}  // namespace

Dead_space_report measure_dead_space(const std::vector<Dead_space_rect>& obstacles, const Dead_space_limits& limits,
                                     bool outlines) {
  check(limits);
  return report_of(classify(obstacles, limits), limits, outlines);
}

Dead_space_collapse_report collapse_dead_space(std::vector<Dead_space_object>& objects, const Dead_space_collapse_limits& l,
                                               const Dead_space_cost& cost) {
  check(l.metric);
  if (l.grid <= 0 || l.halo_x < 0 || l.halo_y < 0 || l.space_x < 0 || l.space_y < 0 || l.metric_halo_x < 0 || l.metric_halo_y < 0
      || l.max_moves < 0 || l.max_components < 0 || l.min_gain < 0 || l.wl_move_budget < 0 || l.wl_total_budget < 0) {
    throw std::invalid_argument("invalid macro dead-space collapse limits");
  }
  Dead_space_collapse_report report;
  report.before = measure_dead_space(obstacles_of(objects, l), l.metric);
  std::vector<uint8_t>   touched(objects.size(), 0);
  const auto             p    = l.metric.pitch;
  const auto             snap = [&](int64_t v, int64_t origin) { return origin + (v - origin) / l.grid * l.grid; };
  std::map<Key, int64_t> gains;  // last evaluated gain per candidate (-1: illegal)
  for (int round = 0; round < l.max_moves; ++round) {
    const auto       raster = classify(obstacles_of(objects, l), l.metric);
    std::vector<int> comp;
    const auto       now  = report_of(raster, l.metric, false, &comp);
    const auto       base = now.stranded_area;
    const auto       at   = [&](int64_t x, int64_t y) -> int64_t {
      if (x < l.metric.core_xl || y < l.metric.core_yl) {
        return -1;
      }
      const auto cx = (x - l.metric.core_xl) / p, cy = (y - l.metric.core_yl) / p;
      return cx >= raster.w || cy >= raster.h ? -1 : cy * raster.w + cx;
    };
    const auto cell = [&](int64_t x, int64_t y) -> int {
      const auto i = at(x, y);
      if (i < 0) {
        return -1;
      }
      const auto c = comp[static_cast<size_t>(i)];
      return c >= 0 && c < l.max_components ? c : -1;
    };
    // Dead depth of the examined components right ahead of a face (DBU), sampling across it.
    const auto depth = [&](const Dead_space_object& o, bool x_axis, int sign) {
      const auto s     = span_of(o, x_axis);
      const auto reach = (x_axis ? l.space_x : l.space_y) + 2 * p;
      int64_t    best  = 0;
      for (auto a = s.a + p / 2; a < s.b; a += p) {
        int64_t d = 0;
        for (auto t = sign > 0 ? s.hi + p / 2 : s.lo - p / 2;; t += sign * p) {
          const auto c = x_axis ? cell(t, a) : cell(a, t);
          if (c < 0) {
            if (d > 0 || std::abs(t - (sign > 0 ? s.hi : s.lo)) > reach) {
              break;
            }
            continue;  // the channel just outside the face
          }
          d = std::abs(t - (sign > 0 ? s.hi : s.lo)) + p / 2;
        }
        best = std::max(best, d);
      }
      return best;
    };
    // A macro on the rim of the main region: one of its faces sees main open cells within a channel.
    const auto on_rim = [&](const Dead_space_object& o) {
      for (const bool x_axis : {true, false}) {
        const auto s     = span_of(o, x_axis);
        const auto reach = (x_axis ? l.space_x : l.space_y) + 2 * p;
        for (const int sign : {1, -1}) {
          for (auto a = s.a + p / 2; a < s.b; a += p) {
            for (auto t = sign > 0 ? s.hi + p / 2 : s.lo - p / 2; std::abs(t - (sign > 0 ? s.hi : s.lo)) <= reach; t += sign * p) {
              const auto i = x_axis ? at(t, a) : at(a, t);
              if (i >= 0 && raster.main[static_cast<size_t>(i)]) {
                return true;
              }
            }
          }
        }
      }
      return false;
    };
    struct Best {
      int64_t             gain    = 0;
      bool                outward = false;
      double              wl      = 0;
      std::vector<size_t> members;
      int64_t             dx = 0, dy = 0;
    } best;
    // Candidates are enumerated first, then evaluated lazily (below).
    struct Candidate {
      Key  key;
      bool outward;
    };
    std::vector<Candidate> candidates;
    std::set<Key>          seen;
    const auto             consider
        = [&](const std::vector<size_t>& members, const std::vector<uint8_t>&, int64_t dx, int64_t dy, bool outward) {
            if (dx == 0 && dy == 0) {
              return;
            }
            Key key{members, dx, dy};
            if (seen.insert(key).second) {
              candidates.push_back({std::move(key), outward});
            }
          };
    const auto members_mask = [&](const std::vector<size_t>& members) {
      std::vector<uint8_t> in(objects.size(), 0);
      for (const auto m : members) {
        in[m] = 1;
      }
      return in;
    };
    // (1) Translations into a bordering dead component.
    for (size_t i = 0; i < objects.size(); ++i) {
      if (!objects[i].movable || !objects[i].macro) {
        continue;
      }
      for (const bool x_axis : {true, false}) {
        for (const int sign : {1, -1}) {
          const auto d = depth(objects[i], x_axis, sign);
          if (d <= 0) {
            continue;
          }
          for (const auto& members : groups_for(objects, i, x_axis, sign, l)) {
            const auto in    = members_mask(members);
            const auto slide = max_slide(objects, in, Mover{members, x_axis, sign}, l);
            if (slide <= 0) {
              continue;
            }
            int64_t lo = std::numeric_limits<int64_t>::max(), hi = std::numeric_limits<int64_t>::min();
            for (const auto m : members) {
              const auto s = span_of(objects[m], x_axis);
              lo           = std::min(lo, s.lo);
              hi           = std::max(hi, s.hi);
            }
            const auto mid     = x_axis ? (l.metric.core_xl + l.metric.core_xh) / 2 : (l.metric.core_yl + l.metric.core_yh) / 2;
            const bool outward = (lo + hi) / 2 < mid ? sign < 0 : sign > 0;
            for (const auto dist : {slide, std::min(slide, d / l.grid * l.grid)}) {
              consider(members, in, x_axis ? sign * dist : 0, x_axis ? 0 : sign * dist, outward);
            }
          }
        }
      }
    }
    // (2) Relocations: a single macro from the rim of the main region into a
    // corner of a dead component it fits, packed against the walls there.
    std::vector<size_t> rim;
    for (size_t i = 0; i < objects.size(); ++i) {
      if (objects[i].movable && objects[i].macro && objects[i].group == 0 && on_rim(objects[i])) {
        rim.push_back(i);
      }
    }
    for (int c = 0; c < static_cast<int>(now.components.size()) && c < l.max_components; ++c) {
      const auto& box = now.components[c].bbox;
      for (const auto i : rim) {
        const auto w = objects[i].xh - objects[i].xl, h = objects[i].yh - objects[i].yl;
        if (w > box.xh - box.xl + 2 * p || h > box.yh - box.yl + 2 * p) {
          continue;
        }
        const std::vector<size_t> members{i};
        const auto                in = members_mask(members);
        for (const int corner : {0, 1, 2, 3}) {
          const bool right = corner & 1, top = corner & 2;
          // Start at the corner, step inward by whole pitches until legal, then slide back against the walls.
          const auto ox = objects[i].xl, oy = objects[i].yl;
          bool       placed = false;
          for (int k = 0; k <= 3 && !placed; ++k) {
            const auto x = snap(right ? box.xh - w - k * p : box.xl + k * p, l.metric.core_xl);
            const auto y = snap(top ? box.yh - h - k * p : box.yl + k * p, l.metric.core_yl);
            shift(objects, members, x - ox, y - oy);
            placed = legal_after(objects, in, members, l);
            if (!placed) {
              shift(objects, members, ox - x, oy - y);
            }
          }
          if (!placed) {
            continue;
          }
          for (const bool x_axis : {true, false}) {
            const int  sign  = x_axis ? (right ? 1 : -1) : (top ? 1 : -1);
            const auto slide = max_slide(objects, in, Mover{members, x_axis, sign}, l);
            shift(objects, members, x_axis ? sign * slide : 0, x_axis ? 0 : sign * slide);
          }
          const auto dx = objects[i].xl - ox, dy = objects[i].yl - oy;
          shift(objects, members, -dx, -dy);
          consider(members, in, dx, dy, false);
        }
      }
    }
    // Lazy greedy: a candidate's gain from an earlier round (kept while nothing moved near it) bounds it; evaluate in
    // decreasing bound order and stop once no remaining bound can beat the best evaluated gain.
    std::vector<int64_t> bound(candidates.size());
    std::vector<size_t>  order(candidates.size());
    for (size_t k = 0; k < candidates.size(); ++k) {
      const auto it = gains.find(candidates[k].key);
      bound[k]      = it == gains.end() ? std::numeric_limits<int64_t>::max() : it->second;
      order[k]      = k;
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return bound[a] > bound[b]; });
    for (const auto k : order) {
      if (bound[k] < std::max<int64_t>(1, l.min_gain) || (!best.members.empty() && bound[k] < best.gain)) {
        break;
      }
      const auto& [members, dx, dy] = candidates[k].key;
      std::vector<uint8_t> in(objects.size(), 0);
      for (const auto m : members) {
        in[m] = 1;
      }
      shift(objects, members, dx, dy);
      const bool ok = legal_after(objects, in, members, l);
      ++report.candidates;
      const auto gain = ok ? base - stranded_area_of(objects, l) : -1;
      shift(objects, members, -dx, -dy);
      gains[candidates[k].key] = gain;
      if (gain < std::max<int64_t>(1, l.min_gain) || gain < best.gain) {
        continue;
      }
      const double wl = cost ? cost(members, dx, dy) : 0.0;
      if (wl > l.wl_move_budget || report.wl_delta + wl > l.wl_total_budget) {
        continue;
      }
      const bool outward = candidates[k].outward;
      if (std::tie(gain, outward) > std::tie(best.gain, best.outward)
          || (gain == best.gain && outward == best.outward && wl < best.wl)) {
        best = {gain, outward, wl, members, dx, dy};
      }
    }
    if (best.members.empty()) {
      break;
    }
    // Forget the gains of candidates near the applied move (or moving one of its members).
    {
      const auto      reach = std::max(l.metric.narrow_x, l.metric.narrow_y) + std::max(l.space_x, l.space_y) + 2 * p;
      Dead_space_rect area{std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::min(),
                           std::numeric_limits<int64_t>::min()};
      const auto      grow = [](Dead_space_rect& r, const Dead_space_object& o, int64_t dx, int64_t dy) {
        r.xl = std::min({r.xl, o.xl, o.xl + dx});
        r.yl = std::min({r.yl, o.yl, o.yl + dy});
        r.xh = std::max({r.xh, o.xh, o.xh + dx});
        r.yh = std::max({r.yh, o.yh, o.yh + dy});
      };
      for (const auto m : best.members) {
        grow(area, objects[m], best.dx, best.dy);
      }
      std::vector<uint8_t> moved(objects.size(), 0);
      for (const auto m : best.members) {
        moved[m] = 1;
      }
      for (auto it = gains.begin(); it != gains.end();) {
        const auto& [members, dx, dy] = it->first;
        Dead_space_rect r{std::numeric_limits<int64_t>::max(),
                          std::numeric_limits<int64_t>::max(),
                          std::numeric_limits<int64_t>::min(),
                          std::numeric_limits<int64_t>::min()};
        bool            stale = false;
        for (const auto m : members) {
          stale |= moved[m] != 0;
          grow(r, objects[m], dx, dy);
        }
        stale |= r.xl < area.xh + reach && area.xl - reach < r.xh && r.yl < area.yh + reach && area.yl - reach < r.yh;
        it     = stale ? gains.erase(it) : std::next(it);
      }
    }
    shift(objects, best.members, best.dx, best.dy);
    report.wl_delta += best.wl;
    ++report.moves;
    report.distance += static_cast<int64_t>(best.members.size()) * (std::abs(best.dx) + std::abs(best.dy));
    for (const auto m : best.members) {
      touched[m] = 1;
    }
  }
  report.moved = std::count(touched.begin(), touched.end(), uint8_t{1});
  report.after = measure_dead_space(obstacles_of(objects, l), l.metric);
  return report;
}

std::optional<Dead_space_options> parse_dead_space_options(std::string_view text) {
  const auto trim = [](std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
      s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
      s.remove_suffix(1);
    }
    return s;
  };
  text = trim(text);
  if (text.empty() || text == "off") {
    return std::nullopt;
  }
  Dead_space_options o;
  if (text == "auto") {
    return o;
  }
  const auto number = [&](std::string_view key, std::string_view value) {
    const std::string s(value);
    size_t            used = 0;
    double            v    = 0;
    try {
      v = std::stod(s, &used);
    } catch (const std::exception&) {
      used = 0;
    }
    if (used != s.size() || s.empty() || !std::isfinite(v) || v < 0) {
      throw std::invalid_argument(std::string("macro dead_space: ") + std::string(key) + " requires a nonnegative number");
    }
    return v;
  };
  const auto integer = [&](std::string_view key, std::string_view value) {
    const double v = number(key, value);
    if (v > std::numeric_limits<int>::max() || std::floor(v) != v) {
      throw std::invalid_argument(std::string("macro dead_space: ") + std::string(key)
                                  + " requires a nonnegative integer in range");
    }
    return static_cast<int>(v);
  };
  while (!text.empty()) {
    const auto comma = text.find(',');
    const auto token = trim(text.substr(0, comma));
    text             = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    const auto eq    = token.find('=');
    const auto key   = trim(token.substr(0, eq));
    const auto value = eq == std::string_view::npos ? std::string_view{} : trim(token.substr(eq + 1));
    if (eq == std::string_view::npos && key == "metric") {
      o.collapse = false;
    } else if (eq == std::string_view::npos && (key == "collapse" || key == "auto")) {
      o.collapse = true;
    } else if (key == "narrow") {
      o.narrow_um = value == "auto" ? -1 : number(key, value);
    } else if (key == "pocket") {
      o.pocket_fraction = number(key, value);
      if (o.pocket_fraction > 1) {
        throw std::invalid_argument("macro dead_space: pocket is a fraction in [0, 1]");
      }
    } else if (key == "cells") {
      o.island_cells = number(key, value);
    } else if (key == "hpwl") {
      o.hpwl_total = number(key, value);
    } else if (key == "move_hpwl") {
      o.hpwl_move = number(key, value);
    } else if (key == "moves") {
      o.max_moves = integer(key, value);
    } else if (key == "grid") {
      o.max_cells_axis = integer(key, value);
      if (o.max_cells_axis < 8) {
        throw std::invalid_argument("macro dead_space: grid needs at least 8 cells per axis");
      }
    } else {
      throw std::invalid_argument("macro dead_space: unknown token '" + std::string(token)
                                  + "' (off, metric, auto, collapse, narrow=, pocket=, cells=, hpwl=, move_hpwl=, moves=, grid=)");
    }
  }
  return o;
}
}  // namespace livehd::placement
