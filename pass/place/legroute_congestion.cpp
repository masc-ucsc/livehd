// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>

#include "legroute_internal.hpp"

namespace livehd::placement {
namespace lr {
Legroute_congestion route_congestion(const Design& d, const Grid& g, const Global_routes& routes,
                                     const std::vector<Global_net>& nets, const std::vector<int>& cap_h,
                                     const std::vector<int>& cap_v) {
  Legroute_congestion out;
  out.bounds          = {double(g.x0), double(g.y0), double(g.left(g.cols)), double(g.bottom(g.rows))};
  out.cols            = g.cols;
  out.rows            = g.rows;
  out.admission_share = admission_share(d.opt);
  out.signals         = nets.size();
  out.unrouted        = routes.unrouted;
  out.search_limited  = routes.search_limited;
  for (auto* values :
       {&out.horizontal, &out.vertical, &out.pin_utilization, &out.unresolved_horizontal, &out.unresolved_vertical}) {
    values->assign(g.size(), 0);
  }
  out.demand_h.assign(g.size(), 0);
  out.demand_v.assign(g.size(), 0);
  out.limit_h.assign(g.size(), 0);
  out.limit_v.assign(g.size(), 0);
  const auto index = [&](int t) { return size_t(g.col(t)) * g.rows + g.row(t); };
  const auto ratio = [](double used, int capacity) {
    // No capacity is not one free track: preserve a finite, strictly failing
    // utilization for optimization while the exact zero remains in limit_*.
    return capacity > 0 ? used / capacity : used > 0 ? 1 + used : 0;
  };
  for (int t = 0; size_t(t) < g.size(); ++t) {
    const auto k = index(t);
    if (g.col(t) + 1 < g.cols) {
      out.limit_h[k] = int(std::floor(out.admission_share * std::min(cap_h[t], cap_h[t + 1])));
    }
    if (g.row(t) + 1 < g.rows) {
      out.limit_v[k] = int(std::floor(out.admission_share * std::min(cap_v[t], cap_v[t + g.cols])));
    }
  }
  for (size_t n = 0; n < nets.size(); ++n) {
    const auto& request = nets[n];
    const auto& tree    = routes.trees.at(n);
    for (const auto& [a, b] : tree.edges) {
      ++(g.row(a) == g.row(b) ? out.demand_h : out.demand_v)[index(std::min(a, b))];
    }
    // Pin/via access is not modeled by this aggregate H/V route. Its separate
    // pin_utilization channel is explicitly zero; final routing checks access.
    if (tree.routed) {
      continue;
    }
    // Failed and search-limited requests must not look like zero congestion.
    // Distribute one bounding-box path's directional demand over its tiles.
    int xl = g.cols, xh = -1, yl = g.rows, yh = -1;
    for (int t : request.endpoints) {
      xl = std::min(xl, g.col(t));
      xh = std::max(xh, g.col(t));
      yl = std::min(yl, g.row(t));
      yh = std::max(yh, g.row(t));
    }
    const double area = double(xh - xl + 1) * (yh - yl + 1);
    for (int x = xl; x <= xh; ++x) {
      for (int y = yl; y <= yh; ++y) {
        const int  t                  = g.tile(x, y);
        const auto k                  = index(t);
        out.unresolved_horizontal[k] += ratio(std::max(1, xh - xl) / area, int(std::floor(out.admission_share * cap_h[t])));
        out.unresolved_vertical[k]   += ratio(std::max(1, yh - yl) / area, int(std::floor(out.admission_share * cap_v[t])));
      }
    }
  }
  for (int t = 0; size_t(t) < g.size(); ++t) {
    const auto k   = index(t);
    const auto add = [&](bool h, int q) {
      auto&        values = h ? out.horizontal : out.vertical;
      const double value  = ratio((h ? out.demand_h : out.demand_v)[k], (h ? out.limit_h : out.limit_v)[k]);
      values[k]           = std::max(values[k], value);
      values[index(q)]    = std::max(values[index(q)], value);
    };
    if (g.col(t) + 1 < g.cols) {
      add(true, t + 1);
    }
    if (g.row(t) + 1 < g.rows) {
      add(false, t + g.cols);
    }
  }
  return out;
}
}  // namespace lr

Legroute_congestion resample_legroute_congestion(const Legroute_congestion& source, const physical::Rect& bounds, int cols,
                                                 int rows) {
  const auto valid_bounds = [](const physical::Rect& r) {
    return std::isfinite(r.xl) && std::isfinite(r.yl) && std::isfinite(r.xh) && std::isfinite(r.yh) && r.xh > r.xl && r.yh > r.yl;
  };
  if (cols <= 0 || rows <= 0 || source.cols <= 0 || source.rows <= 0 || !valid_bounds(bounds) || !valid_bounds(source.bounds)) {
    throw std::invalid_argument("invalid coarse congestion resampling grid");
  }
  Legroute_congestion out;
  out.bounds          = bounds;
  out.cols            = cols;
  out.rows            = rows;
  out.admission_share = source.admission_share;
  out.signals         = source.signals;
  out.unrouted        = source.unrouted;
  out.search_limited  = source.search_limited;
  const double sx     = (source.bounds.xh - source.bounds.xl) / source.cols;
  const double sy     = (source.bounds.yh - source.bounds.yl) / source.rows;
  const double dx = (bounds.xh - bounds.xl) / cols, dy = (bounds.yh - bounds.yl) / rows;
  for (double step : {sx, sy, dx, dy}) {
    if (!std::isfinite(step) || step <= 0) {
      throw std::invalid_argument("invalid coarse congestion cell dimensions");
    }
  }
  const auto sample = [&](const auto& input, auto& output, bool peak, std::vector<double>* means = nullptr) {
    if (input.size() != size_t(source.cols) * source.rows
        || std::ranges::any_of(input, [](double v) { return !std::isfinite(v) || v < 0; })) {
      throw std::invalid_argument("invalid coarse congestion map");
    }
    output.assign(size_t(cols) * rows, 0);
    if (means) {
      means->assign(output.size(), 0);
    }
    for (int x = 0; x < cols; ++x) {
      const double xl = bounds.xl + x * dx, xh = xl + dx;
      const int    first_x = int(std::clamp(std::floor((xl - source.bounds.xl) / sx), 0., double(source.cols)));
      const int    last_x  = int(std::clamp(std::ceil((xh - source.bounds.xl) / sx), 0., double(source.cols)));
      for (int y = 0; y < rows; ++y) {
        const double yl = bounds.yl + y * dy, yh = yl + dy;
        const int    first_y = int(std::clamp(std::floor((yl - source.bounds.yl) / sy), 0., double(source.rows)));
        const int    last_y  = int(std::clamp(std::ceil((yh - source.bounds.yl) / sy), 0., double(source.rows)));
        double       sum = 0, maximum = 0;
        for (int xx = first_x; xx < last_x; ++xx) {
          const double overlap_x
              = std::max(0., std::min(xh, source.bounds.xl + (xx + 1) * sx) - std::max(xl, source.bounds.xl + xx * sx));
          for (int yy = first_y; yy < last_y; ++yy) {
            const double overlap_y
                = std::max(0., std::min(yh, source.bounds.yl + (yy + 1) * sy) - std::max(yl, source.bounds.yl + yy * sy));
            const double value  = input[size_t(xx) * source.rows + yy];
            sum                += overlap_x * overlap_y * value;
            if (overlap_x > 0 && overlap_y > 0) {
              maximum = std::max(maximum, value);
            }
          }
        }
        output[size_t(x) * rows + y] = peak ? maximum : sum / (dx * dy);
        if (means) {
          (*means)[size_t(x) * rows + y] = sum / (dx * dy);
        }
      }
    }
  };
  sample(source.horizontal, out.horizontal, true, &out.mean_horizontal);
  sample(source.vertical, out.vertical, true, &out.mean_vertical);
  sample(source.pin_utilization, out.pin_utilization, false);
  sample(source.unresolved_horizontal, out.unresolved_horizontal, false);
  sample(source.unresolved_vertical, out.unresolved_vertical, false);
  return out;
}
}  // namespace livehd::placement
