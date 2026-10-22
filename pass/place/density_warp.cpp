// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "density_warp.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "Highs.h"
namespace warp {
double cross(Point a, Point b) { return a.x * b.y - a.y * b.x; }
double length(Point p) { return std::hypot(p.x, p.y); }
void   require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
void invariants(const Mesh& m, const Layout& l) {
  double sum = 0;
  for (const auto& f : m.faces) {
    double area = m.area(f);
    require(area > 0, "nonpositive mesh face area");
    require(area + 1e-8 >= f.reserved, "reserved macro area compressed away");
    sum += area;
    for (int k = 0; k < 4; ++k) {
      Point a = m.points[f.v[k]], b = m.points[f.v[(k + 1) % 4]], c = m.points[f.v[(k + 2) % 4]];
      require(cross(b - a, c - b) > 0, "folded/nonconvex mesh face");
    }
  }
  require(std::abs(sum - 1) <= 1e-6, "mesh does not conserve die area");
  for (size_t k = 0; k < m.macros.size(); ++k) {
    if (m.density_only && !l.insts[m.macros[k]].standard) {
      continue;
    }
    int   first = m.macro_vertices[k].front();
    Point delta = m.points[first] - m.original[first];
    for (int v : m.macro_vertices[k]) {
      require(length((m.points[v] - m.original[v]) - delta) < 1e-7, "macro rigidity violated");
    }
    if (l.insts[m.macros[k]].fixed) {
      require(length(delta) < 1e-9, "fixed object moved");
    }
  }
}

struct Lp {
  HighsLp model;
  Lp() {
    model.a_matrix_.format_ = MatrixFormat::kRowwise;
    model.a_matrix_.start_  = {0};
  }
  int variable(double cost, double lo = 0, double hi = kHighsInf) {
    model.col_cost_.push_back(cost);
    model.col_lower_.push_back(lo);
    model.col_upper_.push_back(hi);
    return model.num_col_++;
  }
  void row(std::vector<std::pair<int, double>> coefficients, double lo, double hi = kHighsInf) {
    // Sparse rows must not contain duplicate columns (degenerate expressions can).
    std::sort(coefficients.begin(), coefficients.end());
    for (size_t i = 0; i < coefficients.size();) {
      auto [column, value] = coefficients[i++];
      while (i < coefficients.size() && coefficients[i].first == column) {
        value += coefficients[i++].second;
      }
      if (std::abs(value) > 1e-14) {
        model.a_matrix_.index_.push_back(column);
        model.a_matrix_.value_.push_back(value);
      }
    }
    model.a_matrix_.start_.push_back(static_cast<HighsInt>(model.a_matrix_.index_.size()));
    model.row_lower_.push_back(lo);
    model.row_upper_.push_back(hi);
    ++model.num_row_;
  }
};
double coordinate(Point p, int axis) { return axis == 0 ? p.x : p.y; }
Pass   solve_axis(Mesh& m, const Layout& l, const Options& o, int axis) {
  auto         start = Clock::now();
  Lp           lp;
  const double scale     = axis == 0 ? l.width() : l.height();
  double       lambda    = o.lambda < 0 ? 1000. * o.bins * o.bins : o.lambda * l.width() * l.height();
  double       free_area = 0, demand = 0;
  for (const auto& f : m.faces) {
    if (f.macro < 0) {
      free_area += f.initial_area;
      demand    += f.demand;
    }
  }
  const double balance_cost = free_area > 0 ? o.balance * scale * m.points.size() / free_area : 0;
  if (o.lambda < 0) {
    // Keep capacity dominant when the optional balancing objective is strong.
    lambda = std::max(lambda, 100 * balance_cost);
  }
  for (size_t v = 0; v < m.points.size(); ++v) {
    double original = coordinate(m.original[v], axis);
    bool   boundary = original == 0 || original == 1;
    lp.variable(0, boundary ? 0 : -original, boundary ? 0 : 1 - original);
  }
  std::vector<int> slack(m.faces.size(), -1);
  for (size_t f = 0; f < m.faces.size(); ++f) {
    if (m.faces[f].macro < 0) {
      slack[f] = lp.variable(lambda);
    }
  }
  for (size_t v = 0; v < m.points.size(); ++v) {
    int a = lp.variable(scale);
    lp.row(
        {
            {                  a,  1},
            {static_cast<int>(v), -1}
    },
        0);
    lp.row(
        {
            {                  a, 1},
            {static_cast<int>(v), 1}
    },
        0);
  }
  for (auto [a, b] : m.edges) {
    int t = lp.variable(o.mu * scale);
    lp.row(
        {
            {t,  1},
            {a, -1},
            {b,  1}
    },
        0);
    lp.row(
        {
            {t,  1},
            {a,  1},
            {b, -1}
    },
        0);
    auto   delta = m.original[b] - m.original[a];
    double gap   = coordinate(delta, axis);
    if (gap > 0) {
      // Never make an originally thin, exact macro-edge face infeasible.
      double minimum = std::min(.01 / o.bins, .1 * gap);
      lp.row(
          {
              {b,  1},
              {a, -1}
      },
          minimum - gap);
    }
  }
  for (size_t f = 0; f < m.faces.size(); ++f) {
    const auto&                         face = m.faces[f];
    std::vector<std::pair<int, double>> coefficients;
    double                              constant = 0;
    for (int k = 0; k < 4; ++k) {
      double next = coordinate(m.points[face.v[(k + 1) % 4]], 1 - axis), prev = coordinate(m.points[face.v[(k + 3) % 4]], 1 - axis);
      double c = .5 * (axis == 0 ? next - prev : prev - next);
      coefficients.emplace_back(face.v[k], c);
      constant += c * coordinate(m.original[face.v[k]], axis);
    }
    if (slack[f] >= 0) {
      if (o.balance > 0 && demand > 0) {
        // Allocate free space to cell demand. A small original-area share keeps
        // empty corridors open; rigid macro faces never enter this objective.
        // Both signs are linear in the active axis, just like capacity.
        double target    = face.target >= 0 ? face.target : .05 * face.initial_area + .95 * free_area * face.demand / demand;
        int    deviation = lp.variable(balance_cost);
        if (o.balance_model == "strain") {
          // Convex piecewise quadratic of fractional area error. An empty
          // uncompressed corridor has more pressure than a compressed one.
          // q >= A0 * (2*t*((A-target)/A0) - t*t), with each axis still an LP.
          for (double t : {-4., -2., -1., -.5, -.25, -.125, -.0625, 0., .0625, .125, .25, .5, 1., 2., 4.}) {
            auto row = coefficients;
            for (auto& term : row) {
              term.second *= 2 * t;
            }
            row.emplace_back(deviation, -1);
            lp.row(std::move(row), -kHighsInf, 2 * t * (target - constant) + t * t * face.initial_area);
          }
        } else {
          auto positive = coefficients, negative = coefficients;
          for (auto& term : negative) {
            term.second = -term.second;
          }
          positive.emplace_back(deviation, -1);
          negative.emplace_back(deviation, -1);
          lp.row(std::move(positive), -kHighsInf, target - constant);
          lp.row(std::move(negative), -kHighsInf, constant - target);
        }
      }
      if (face.reserved > 0) {
        lp.row(coefficients, face.reserved - constant);
      }
      coefficients.emplace_back(slack[f], 1);
      lp.row(std::move(coefficients), face.reserved + face.demand / o.rho - constant);
    }
    for (int k = 0; k < 4; ++k) {
      int    a = face.v[k], b = face.v[(k + 1) % 4], c = face.v[(k + 2) % 4];
      double aa = coordinate(m.points[a], 1 - axis), bb = coordinate(m.points[b], 1 - axis), cc = coordinate(m.points[c], 1 - axis);
      std::array<double, 3> values
          = axis == 0 ? std::array<double, 3>{bb - cc, cc - aa, aa - bb} : std::array<double, 3>{cc - bb, aa - cc, bb - aa};
      double base = values[0] * coordinate(m.original[a], axis) + values[1] * coordinate(m.original[b], axis)
                    + values[2] * coordinate(m.original[c], axis);
      lp.row(
          {
              {a, values[0]},
              {b, values[1]},
              {c, values[2]}
      },
          face.initial_area * 1e-4 - base);
    }
  }
  for (size_t k = 0; k < m.macros.size(); ++k) {
    const auto& macro = l.insts[m.macros[k]];
    if (m.density_only && !macro.standard) {
      continue;
    }
    int t = lp.variable(0, macro.fixed ? 0 : -1, macro.fixed ? 0 : 1);
    for (int v : m.macro_vertices[k]) {
      lp.row(
          {
              {v,  1},
              {t, -1}
      },
          0,
          0);
    }
  }
  // Exact bounds on the bilinearly mapped center keep rigid cell rectangles in
  // the die. A valid center map alone does not guarantee this near the outline.
  for (const auto& c : l.insts) {
    if (!c.macro) {
      const auto&                         f = m.faces[c.face];
      std::vector<std::pair<int, double>> coefficients;
      double                              base = 0;
      for (int k = 0; k < 4; ++k) {
        coefficients.emplace_back(f.v[k], c.weights[k]);
        base += c.weights[k] * coordinate(m.original[f.v[k]], axis);
      }
      double half = (axis == 0 ? c.w / l.width() : c.h / l.height()) / 2;
      lp.row(std::move(coefficients), half - base, 1 - half - base);
    }
  }
  // Scale every objective coefficient equally: same minimizer and relative
  // penalties, bounded dual magnitudes. Keep feasibility tolerances unchanged.
  double max_cost = 0, objective_scale = 1;
  for (double cost : lp.model.col_cost_) {
    max_cost = std::max(max_cost, std::abs(cost));
  }
  if (max_cost > 1024) {
    objective_scale = std::exp2(std::floor(std::log2(1024 / max_cost)));
    for (auto& cost : lp.model.col_cost_) {
      cost *= objective_scale;
    }
  }
  Highs       highs;
  std::string solver_log;
  highs.setCallback([&](int, const std::string& message, const HighsCallbackOutput*, HighsCallbackInput*, void*) {
    solver_log += message;
    if (solver_log.size() > 4096) {
      solver_log.erase(0, solver_log.size() - 4096);
    }
  });
  highs.startCallback(HighsCallbackType::kCallbackLogging);
  highs.setOptionValue("output_flag", true);
  highs.setOptionValue("log_to_console", false);
  highs.setOptionValue("threads", 1);
  highs.setOptionValue("solver", "ipm");
  highs.setOptionValue("run_crossover", "off");
  highs.setOptionValue("time_limit", o.time_limit);
  highs.setOptionValue("small_matrix_value", 1e-12);
  highs.setOptionValue("primal_feasibility_tolerance", 1e-9);
  highs.setOptionValue("ipm_optimality_tolerance", 1e-10);
  require(highs.passModel(lp.model) != HighsStatus::kError, "HiGHS rejected LP");
  auto run_status       = highs.run();
  auto status           = highs.getModelStatus();
  bool simplex_fallback = false;
  if (run_status == HighsStatus::kError || status == HighsModelStatus::kNotset || status == HighsModelStatus::kUnknown
      || status == HighsModelStatus::kSolveError) {
    double remaining = o.time_limit - std::chrono::duration<double>(Clock::now() - start).count();
    if (remaining > 0) {
      highs.clearSolver();
      highs.setOptionValue("solver", "simplex");
      // HiGHS retains cumulative runtime across clearSolver(). Its limit is
      // absolute on that clock, not an allowance for this second run.
      highs.setOptionValue("time_limit", highs.getRunTime() + remaining);
      highs.run();
      status           = highs.getModelStatus();
      simplex_fallback = true;
    }
  }
  const auto& solution = highs.getSolution();
  require(status == HighsModelStatus::kOptimal && solution.value_valid,
          "LP did not reach an optimum: " + highs.modelStatusToString(status) + "; try a smaller --bins or larger --time-limit; "
              + solver_log);
  auto previous = m.points;
  for (size_t v = 0; v < m.points.size(); ++v) {
    double value = coordinate(m.original[v], axis) + solution.col_value[v];
    if (axis == 0) {
      m.points[v].x = value;
    } else {
      m.points[v].y = value;
    }
  }
  try {
    invariants(m, l);
  } catch (...) {
    m.points = std::move(previous);
    throw;
  }
  Pass report{axis == 0 ? "x" : "y", highs.modelStatusToString(status)};
  report.objective_scale = objective_scale;
  if (simplex_fallback) {
    report.status += " (simplex fallback)";
  }
  for (size_t f = 0; f < m.faces.size(); ++f) {
    if (slack[f] >= 0) {
      double s      = std::max(0., m.faces[f].reserved + m.faces[f].demand / o.rho - m.area(m.faces[f]));
      report.slack += s * l.width() * l.height();
      if (s > 1e-9 * m.faces[f].initial_area) {
        ++report.slack_faces;
      }
    }
  }
  report.seconds = std::chrono::duration<double>(Clock::now() - start).count();
  return report;
}
double face_density(const Mesh& m) {
  double peak = 0;
  for (const auto& f : m.faces) {
    if (f.macro < 0) {
      peak = std::max(peak, f.demand / std::max(1e-30, m.area(f) - f.reserved));
    }
  }
  return peak;
}
std::vector<Point> clip_box(std::vector<Point> polygon, Point lo, Point hi) {
  for (int axis = 0; axis < 2; ++axis) {
    for (int side = 0; side < 2; ++side) {
      if (polygon.empty()) {
        return polygon;
      }
      double             bound = coordinate(side == 0 ? lo : hi, axis);
      std::vector<Point> out;
      for (size_t i = 0; i < polygon.size(); ++i) {
        Point a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        bool  ain = side == 0 ? coordinate(a, axis) >= bound : coordinate(a, axis) <= bound;
        bool  bin = side == 0 ? coordinate(b, axis) >= bound : coordinate(b, axis) <= bound;
        if (ain) {
          out.push_back(a);
        }
        if (ain != bin) {
          out.push_back(a + (b - a) * ((bound - coordinate(a, axis)) / (coordinate(b, axis) - coordinate(a, axis))));
        }
      }
      polygon = std::move(out);
    }
  }
  return polygon;
}
Point sample_field(const Mesh& m, Point p) {
  int         i = std::clamp(int(std::upper_bound(m.xs.begin(), m.xs.end(), p.x) - m.xs.begin()) - 1, 0, m.nx - 2);
  int         j = std::clamp(int(std::upper_bound(m.ys.begin(), m.ys.end(), p.y) - m.ys.begin()) - 1, 0, m.ny - 2);
  double      u = std::clamp((p.x - m.xs[i]) / (m.xs[i + 1] - m.xs[i]), 0., 1.);
  double      v = std::clamp((p.y - m.ys[j]) / (m.ys[j + 1] - m.ys[j]), 0., 1.);
  const auto& f = m.faces[j * (m.nx - 1) + i];
  return m.points[f.v[0]] * ((1 - u) * (1 - v)) + m.points[f.v[1]] * (u * (1 - v)) + m.points[f.v[2]] * (u * v)
         + m.points[f.v[3]] * ((1 - u) * v);
}
std::vector<double> macro_occupancy(const Layout& l, int bins) {
  using Rectangle = std::array<double, 4>;
  std::vector<std::vector<Rectangle>> pieces(static_cast<size_t>(bins) * bins);
  double                              dx = l.width() / bins, dy = l.height() / bins;
  for (const auto& c : l.insts) {
    double xl = std::max(l.lo.x, c.p.x), yl = std::max(l.lo.y, c.p.y), xh = std::min(l.hi.x, c.p.x + c.w),
           yh = std::min(l.hi.y, c.p.y + c.h);
    if (xl >= xh || yl >= yh) {
      continue;
    }
    int x0 = std::clamp(int((xl - l.lo.x) / dx), 0, bins - 1), x1 = std::clamp(int((xh - l.lo.x) / dx), 0, bins - 1);
    int y0 = std::clamp(int((yl - l.lo.y) / dy), 0, bins - 1), y1 = std::clamp(int((yh - l.lo.y) / dy), 0, bins - 1);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        Rectangle r{std::max(xl, l.lo.x + x * dx),
                    std::max(yl, l.lo.y + y * dy),
                    std::min(xh, l.lo.x + (x + 1) * dx),
                    std::min(yh, l.lo.y + (y + 1) * dy)};
        if (r[0] < r[2] && r[1] < r[3]) {
          pieces[y * bins + x].push_back(r);
        }
      }
    }
  }
  std::vector<double> out(pieces.size(), 0.);
  for (size_t i = 0; i < pieces.size(); ++i) {
    std::vector<double> xs;
    for (const auto& r : pieces[i]) {
      xs.push_back(r[0]);
      xs.push_back(r[2]);
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    for (size_t j = 1; j < xs.size(); ++j) {
      double                                 mid = (xs[j - 1] + xs[j]) * .5;
      std::vector<std::pair<double, double>> ys;
      for (const auto& r : pieces[i]) {
        if (r[0] < mid && mid < r[2]) {
          ys.emplace_back(r[1], r[3]);
        }
      }
      std::sort(ys.begin(), ys.end());
      double length = 0, hi = -std::numeric_limits<double>::infinity();
      for (auto [a, b] : ys) {
        if (b > hi) {
          length += b - std::max(a, hi);
          hi      = b;
        }
      }
      out[i] += (xs[j] - xs[j - 1]) * length;
    }
  }
  return out;
}
std::vector<double> smooth_box(const std::vector<double>& input, int bins, int window) {
  if (window == 0) {
    return input;
  }
  // A full-block or oversized filter uses the block average everywhere.
  // In particular, never dilute boundary bins with zero padding.
  if (window >= bins) {
    double mean = std::accumulate(input.begin(), input.end(), 0.) / input.size();
    return std::vector<double>(input.size(), mean);
  }
  // A centered even-width box has half-weight end samples: for width 8,
  // offsets -4..4 carry {.5,1,1,1,1,1,1,1,.5}. No directional half-bin bias.
  std::vector<std::pair<int, double>> kernel;
  double                              radius = window * .5;
  for (int i = -window; i <= window; ++i) {
    double w = std::max(0., std::min(i + .5, radius) - std::max(i - .5, -radius));
    if (w > 0) {
      kernel.emplace_back(i, w / window);
    }
  }
  auto reflect = [&](int k) {
    while (k < 0 || k >= bins) {
      if (k < 0) {
        k = -k - 1;
      } else {
        k = 2 * bins - k - 1;
      }
    }
    return k;
  };
  std::vector<double> a(input.size(), 0.), b(input.size(), 0.);
  for (int y = 0; y < bins; ++y) {
    for (int x = 0; x < bins; ++x) {
      for (auto [k, w] : kernel) {
        a[y * bins + x] += w * input[y * bins + reflect(x + k)];
      }
    }
  }
  for (int y = 0; y < bins; ++y) {
    for (int x = 0; x < bins; ++x) {
      for (auto [k, w] : kernel) {
        b[y * bins + x] += w * a[reflect(y + k) * bins + x];
      }
    }
  }
  return b;
}
Density_result density_warp(const Layout& before, const std::vector<double>& mass, const Options& o) {
  const auto start = Clock::now();
  require(o.bins >= 2 && o.bins <= 256 && mass.size() == size_t(o.bins * o.bins) && o.iters >= 0 && o.iters <= 30
              && o.time_limit > 0 && o.time_limit <= 120 && std::isfinite(o.time_limit) && std::isfinite(o.rho) && o.rho > 0
              && o.rho <= 1 && o.smooth_window >= 0 && o.smooth_window <= 1024 && std::isfinite(o.smooth_blend)
              && o.smooth_blend >= 0 && o.smooth_blend <= 1 && std::isfinite(before.lo.x) && std::isfinite(before.lo.y)
              && std::isfinite(before.hi.x) && std::isfinite(before.hi.y) && before.width() > 0 && before.height() > 0,
          "invalid density warp input");
  require(std::isfinite(o.mu) && o.mu >= 0 && std::isfinite(o.lambda) && std::isfinite(o.balance) && o.balance >= 0
              && std::isfinite(o.tol) && o.tol >= 0 && (o.balance_model == "area" || o.balance_model == "strain"),
          "invalid density warp objective");
  for (double value : mass) {
    require(std::isfinite(value) && value >= 0, "invalid density mass");
  }
  for (const auto& c : before.insts) {
    require(c.macro, "density warp accepts macro anchors; cell demand belongs in the mass grid");
    require(std::isfinite(c.p.x) && std::isfinite(c.p.y) && std::isfinite(c.w) && std::isfinite(c.h) && c.w > 0 && c.h > 0,
            "invalid macro geometry");
  }
  double die_area = before.width() * before.height(), bin_area = die_area / (o.bins * o.bins);
  double total = std::accumulate(mass.begin(), mass.end(), 0.);
  require(total >= 0 && total / die_area <= o.rho * (1 + 1e-9), "density grid has infeasible global cap");
  std::vector<double> occupied(mass.size(), 0.), free(mass.size(), bin_area), target(mass.size(), -1.),
      target_density(mass.size(), 0.);
  if (o.macro_space) {
    occupied = macro_occupancy(before, o.bins);
  }
  for (size_t i = 0; i < free.size(); ++i) {
    free[i] = std::max(0., bin_area - occupied[i]);
  }
  double free_total = std::accumulate(free.begin(), free.end(), 0.);
  require(free_total > 0 && total / free_total <= o.rho * (1 + 1e-9), "infeasible global free-area density cap");
  if (o.smooth_window > 0) {
    auto smooth_mass = smooth_box(mass, o.bins, o.smooth_window), smooth_free = smooth_box(free, o.bins, o.smooth_window);
    std::vector<double> requested(free.size(), 0.);
    for (size_t i = 0; i < free.size(); ++i) {
      double original   = mass[i] / std::max(free[i], 1e-9 * bin_area);
      double smoothed   = smooth_mass[i] / std::max(smooth_free[i], 1e-9 * bin_area);
      target_density[i] = (1 - o.smooth_blend) * original + o.smooth_blend * smoothed;
      requested[i]      = target_density[i] > 0 ? mass[i] / target_density[i] : free[i];
    }
    double requested_total = std::accumulate(requested.begin(), requested.end(), 0.);
    require(requested_total > 0, "invalid local density target");
    for (size_t i = 0; i < target.size(); ++i) {
      target[i] = occupied[i] + requested[i] * free_total / requested_total;
    }
  } else if (o.macro_space) {
    for (size_t i = 0; i < target.size(); ++i) {
      target[i] = occupied[i] + .05 * free[i] + .95 * free_total * (total > 0 ? mass[i] / total : free[i] / free_total);
    }
  }
  Mesh m;
  m.density_only = true;
  m.nx = m.ny = o.bins + 1;
  for (int i = 0; i <= o.bins; ++i) {
    m.xs.push_back(double(i) / o.bins);
    m.ys.push_back(double(i) / o.bins);
  }
  for (int j = 0; j < m.ny; ++j) {
    for (int i = 0; i < m.nx; ++i) {
      m.original.push_back({m.xs[i], m.ys[j]});
      if (i + 1 < m.nx) {
        m.edges.emplace_back(m.vertex(i, j), m.vertex(i + 1, j));
      }
      if (j + 1 < m.ny) {
        m.edges.emplace_back(m.vertex(i, j), m.vertex(i, j + 1));
      }
      if (i < o.bins && j < o.bins) {
        Face f{
            {m.vertex(i, j), m.vertex(i + 1, j), m.vertex(i + 1, j + 1), m.vertex(i, j + 1)}
        };
        f.initial_area = 1. / (o.bins * o.bins);
        f.demand       = mass[j * o.bins + i] / die_area;
        f.reserved     = occupied[j * o.bins + i] / die_area;
        f.target       = target[j * o.bins + i] < 0 ? -1 : target[j * o.bins + i] / die_area;
        m.faces.push_back(f);
      }
    }
  }
  m.points = m.original;
  std::vector<Pass> passes;
  invariants(m, before);
  for (int i = 0; i < o.iters; ++i) {
    passes.push_back(solve_axis(m, before, o, 0));
    passes.push_back(solve_axis(m, before, o, 1));
    if (o.balance == 0 && face_density(m) <= o.rho * (1 + o.tol)) {
      break;
    }
  }
  Layout after = before;
  for (auto& c : after.insts) {
    Point center = before.normalize(c.p + Point{c.w / 2, c.h / 2});
    c.p          = before.physical(sample_field(m, center)) - Point{c.w / 2, c.h / 2};
  }
  std::vector<double> out_mass(mass.size(), 0.);
  std::vector<double> out_occupied(mass.size(), 0.);
  for (const auto& f : m.faces) {
    if (f.demand == 0 && f.reserved == 0) {
      continue;
    }
    std::vector<Point> polygon;
    Point              lo{1, 1}, hi{0, 0};
    for (int v : f.v) {
      Point p = m.points[v];
      polygon.push_back(p);
      lo.x = std::min(lo.x, p.x);
      lo.y = std::min(lo.y, p.y);
      hi.x = std::max(hi.x, p.x);
      hi.y = std::max(hi.y, p.y);
    }
    int xl = std::clamp(int(lo.x * o.bins), 0, o.bins - 1), xh = std::clamp(int(hi.x * o.bins), 0, o.bins - 1),
        yl = std::clamp(int(lo.y * o.bins), 0, o.bins - 1), yh = std::clamp(int(hi.y * o.bins), 0, o.bins - 1);
    for (int y = yl; y <= yh; ++y) {
      for (int x = xl; x <= xh; ++x) {
        auto   p    = clip_box(polygon, {double(x) / o.bins, double(y) / o.bins}, {double(x + 1) / o.bins, double(y + 1) / o.bins});
        double area = 0;
        for (size_t k = 0; k < p.size(); ++k) {
          area += cross(p[k], p[(k + 1) % p.size()]);
        }
        out_mass[y * o.bins + x]     += f.demand * die_area * std::abs(area) * .5 / m.area(f);
        out_occupied[y * o.bins + x] += f.reserved * die_area * std::abs(area) * .5 / m.area(f);
      }
    }
  }
  double out_total = std::accumulate(out_mass.begin(), out_mass.end(), 0.);
  require(std::abs(total - out_total) <= 1e-7 * total, "density transport did not conserve mass");
  double total_occupied       = std::accumulate(occupied.begin(), occupied.end(), 0.);
  double transported_occupied = std::accumulate(out_occupied.begin(), out_occupied.end(), 0.);
  require(std::abs(total_occupied - transported_occupied) <= 1e-7 * std::max(1., total_occupied),
          "macro reservation area not conserved");
  std::vector<double> a, b;
  for (size_t i = 0; i < mass.size(); ++i) {
    a.push_back(mass[i] / std::max(free[i], 1e-9 * bin_area));
    b.push_back(out_mass[i] / std::max(bin_area - out_occupied[i], 1e-9 * bin_area));
  }

  Density_result result;
  result.after                = std::move(after);
  result.mesh                 = std::move(m);
  result.passes               = std::move(passes);
  result.occupied             = std::move(occupied);
  result.free                 = std::move(free);
  result.target               = std::move(target);
  result.target_density       = std::move(target_density);
  result.out_mass             = std::move(out_mass);
  result.out_occupied         = std::move(out_occupied);
  result.a                    = std::move(a);
  result.b                    = std::move(b);
  result.total                = total;
  result.out_total            = out_total;
  result.total_occupied       = total_occupied;
  result.transported_occupied = transported_occupied;
  result.seconds              = std::chrono::duration<double>(Clock::now() - start).count();
  return result;
}
}  // namespace warp
