// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_anchor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
// The native solver rounds each halo up to whole sites/rows (placement.cpp halo()).
double pad(const std::vector<double>& halo, size_t i, double pitch) {
  return halo.empty() ? 0 : std::ceil(halo[i] / pitch - 1e-9) * pitch;
}
std::pair<double, double> center(const dp::Design& d, int i) {
  return {d.node_x[i] + d.node_size_x[i] / 2, d.node_y[i] + d.node_size_y[i] / 2};
}
// Total (net-weighted) HPWL with the listed macros centered at `centers`, every other node at its design pose.
double hpwl_with(const dp::Design& d, const std::vector<int>& macros, const std::vector<std::pair<double, double>>& centers) {
  std::vector<int> slot(d.node_x.size(), -1);
  for (size_t k = 0; k < macros.size(); ++k) {
    slot[macros[k]] = int(k);
  }
  const int  nets    = int(d.flat_net2pin_start.size()) - 1;
  double     total   = 0;
  const bool weights = int(d.net_weights.size()) == nets;
  for (int net = 0; net < nets; ++net) {
    if (!d.net_mask.empty() && !d.net_mask[net]) {
      continue;
    }
    const int begin = d.flat_net2pin_start[net], end = d.flat_net2pin_start[net + 1];
    if (end - begin < 2) {
      continue;
    }
    double xl = INFINITY, xh = -INFINITY, yl = INFINITY, yh = -INFINITY;
    for (int k = begin; k < end; ++k) {
      const int p = d.flat_net2pin[k];
      const int v = d.pin2node[p];
      double    x = d.node_x[v], y = d.node_y[v];
      if (slot[v] >= 0) {
        x = centers[slot[v]].first - d.node_size_x[v] / 2;
        y = centers[slot[v]].second - d.node_size_y[v] / 2;
      }
      x  += d.pin_offset_x[p];
      y  += d.pin_offset_y[p];
      xl  = std::min(xl, x);
      xh  = std::max(xh, x);
      yl  = std::min(yl, y);
      yh  = std::max(yh, y);
    }
    total += (weights ? d.net_weights[net] : 1.0) * (xh - xl + yh - yl);
  }
  return total;
}
}  // namespace

void validate_macro_anchor_options(const Macro_anchor_options& o) {
  const auto unit = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; };
  bool       ok = std::isfinite(o.weight) && o.weight >= 0 && o.weight <= 8 && std::isfinite(o.final_weight) && o.final_weight >= 0
            && o.final_weight <= 8 && std::isfinite(o.inflate) && o.inflate >= 0 && o.inflate <= .25 && unit(o.start_overflow)
            && std::isfinite(o.gate) && o.gate >= 0 && unit(o.stable_fraction) && std::isfinite(o.stable_distance)
            && o.stable_distance >= 0 && o.refresh_interval > 0 && o.ramp >= 1 && o.max_refreshes >= 1 && o.settle >= 0
            && unit(o.swap_tolerance) && o.swap_neighbors >= 1 && o.swap_lock >= 0 && o.dead_space_refreshes >= -1
            && std::isfinite(o.dbu_per_um) && o.dbu_per_um > 0
            && (o.select == "all" || o.select == "edge+random" || o.select == "edge+stable") && std::isfinite(o.edge_weight)
            && o.edge_weight >= 0 && o.edge_weight <= 64 && std::isfinite(o.random_weight) && o.random_weight >= 0
            && o.random_weight <= 64 && unit(o.random_fraction) && std::isfinite(o.spring_length) && o.spring_length >= 0
            && o.spring_length <= 4 && unit(o.target_hysteresis) && o.channel_x >= 0 && o.channel_y >= 0;
  double previous = std::numeric_limits<double>::infinity();
  for (double m : o.milestones) {
    ok       = ok && unit(m) && m < previous;
    previous = m;
  }
  if (!ok) {
    throw std::invalid_argument(
        "invalid macro.anchor policy: weight, final_weight in [0,8]; inflate in [0,0.25]; start_overflow, stable_fraction, "
        "swap_tolerance in [0,1]; gate, stable_distance >= "
        "0; refresh_interval, ramp, max_refreshes, swap_neighbors >= 1; settle, swap_lock >= 0; dead_space_refreshes >= -1; "
        "milestones "
        "strictly descending in [0,1]; select all|edge+random|edge+stable; edge_weight, random_weight in [0,64]; random_fraction, "
        "target_hysteresis in [0,1]; spring_length in [0,4]");
  }
}

Macro_target_generator native_target_generator(Native_target_config config) {
  return [config = std::move(config)](dp::Design& physical, bool late) {
    // Halo padding exactly as place() pads the GP design before its own legalization.
    const auto          n      = physical.node_x.size();
    dp::Design          padded = physical;
    const auto&         l      = physical.layout;
    std::vector<double> hx(n), hy(n);
    for (size_t i = 0; i < n; ++i) {
      hx[i]                  = pad(physical.node_halo_x, i, l.site_width);
      hy[i]                  = pad(physical.node_halo_y, i, l.row_height);
      padded.node_x[i]      -= hx[i];
      padded.node_y[i]      -= hy[i];
      padded.node_size_x[i] += 2 * hx[i];
      padded.node_size_y[i] += 2 * hy[i];
      if (!padded.node_boxes.empty()) {
        padded.node_boxes[i].xl -= hx[i];
        padded.node_boxes[i].xh -= hx[i];
        padded.node_boxes[i].yl -= hy[i];
        padded.node_boxes[i].yh -= hy[i];
      }
      if (!padded.node_legalize_anchor_x.empty()) {
        padded.node_legalize_anchor_x[i] -= hx[i];
        padded.node_legalize_anchor_y[i] -= hy[i];
      }
      for (int k = padded.flat_node2pin_start[i]; k < padded.flat_node2pin_start[i + 1]; ++k) {
        const int p             = padded.flat_node2pin[k];
        padded.pin_offset_x[p] += hx[i];
        padded.pin_offset_y[p] += hy[i];
      }
    }
    padded.node_halo_x.clear();
    padded.node_halo_y.clear();
    const auto to_physical = [&](const dp::Design& from) {
      for (size_t i = 0; i < n; ++i) {
        physical.node_x[i] = from.node_x[i] + hx[i];
        physical.node_y[i] = from.node_y[i] + hy[i];
      }
    };
    std::function<void(dp::Design&)> after;
    if (config.after_macro_legalize) {
      after = [&](dp::Design& snapshot) {
        to_physical(snapshot);
        config.after_macro_legalize(physical, late);
        for (size_t i = 0; i < n; ++i) {
          snapshot.node_x[i] = physical.node_x[i] - hx[i];
          snapshot.node_y[i] = physical.node_y[i] - hy[i];
        }
      };
    }
    if (config.macro_snap) {
      const auto snap = config.macro_snap_regular
                            ? dp::detail::macro_regular_snap(padded, config.macro_snap_options, config.macro_snap_weight)
                            : dp::detail::macro_align_snap(padded, config.macro_snap_tolerance, config.macro_snap_weight);
      dp::detail::legalize(padded, config.bins_x, config.bins_y, &snap, nullptr, after, true);
    } else {
      dp::detail::legalize(padded, config.bins_x, config.bins_y, nullptr, nullptr, after, true);
    }
    to_physical(padded);
  };
}

std::vector<Macro_wire_gradient> macro_wire_gradients(const dp::Design& d, const std::vector<int>& macros) {
  std::vector<int> slot(d.node_x.size(), -1);
  for (size_t k = 0; k < macros.size(); ++k) {
    slot[macros[k]] = int(k);
  }
  std::vector<Macro_wire_gradient> g(macros.size());
  const int                        nets = int(d.flat_net2pin_start.size()) - 1;
  for (int net = 0; net < nets; ++net) {
    if (!d.net_mask.empty() && !d.net_mask[net]) {
      continue;
    }
    const int begin = d.flat_net2pin_start[net], end = d.flat_net2pin_start[net + 1];
    if (end - begin < 2) {
      continue;
    }
    // Distinct-node first/second extremes per axis decide the one-sided HPWL derivative.
    struct Extreme {
      double v1, v2;
      int    node;
    };
    Extreme xmax{-INFINITY, -INFINITY, -1}, xmin{INFINITY, INFINITY, -1}, ymax{-INFINITY, -INFINITY, -1},
        ymin{INFINITY, INFINITY, -1};
    bool touches = false;
    for (int k = begin; k < end; ++k) {
      const int    p = d.flat_net2pin[k];
      const int    v = d.pin2node[p];
      const double x = d.node_x[v] + d.pin_offset_x[p], y = d.node_y[v] + d.pin_offset_y[p];
      touches         = touches || slot[v] >= 0;
      const auto high = [&](Extreme& e, double value) {
        if (v == e.node) {
          e.v1 = std::max(e.v1, value);
        } else if (value > e.v1) {
          e.v2   = e.v1;
          e.v1   = value;
          e.node = v;
        } else {
          e.v2 = std::max(e.v2, value);
        }
      };
      const auto low = [&](Extreme& e, double value) {
        if (v == e.node) {
          e.v1 = std::min(e.v1, value);
        } else if (value < e.v1) {
          e.v2   = e.v1;
          e.v1   = value;
          e.node = v;
        } else {
          e.v2 = std::min(e.v2, value);
        }
      };
      high(xmax, x);
      low(xmin, x);
      high(ymax, y);
      low(ymin, y);
    }
    if (!touches) {
      continue;
    }
    const double w = 1.0 / (end - begin - 1);
    if (xmax.node >= 0 && slot[xmax.node] >= 0 && xmax.v1 > xmax.v2) {
      g[slot[xmax.node]].x += w;
    }
    if (xmin.node >= 0 && slot[xmin.node] >= 0 && xmin.v1 < xmin.v2) {
      g[slot[xmin.node]].x -= w;
    }
    if (ymax.node >= 0 && slot[ymax.node] >= 0 && ymax.v1 > ymax.v2) {
      g[slot[ymax.node]].y += w;
    }
    if (ymin.node >= 0 && slot[ymin.node] >= 0 && ymin.v1 < ymin.v2) {
      g[slot[ymin.node]].y -= w;
    }
  }
  return g;
}

double macro_swap_gain(const dp::Design& d, int a, int b, std::pair<double, double> ta, std::pair<double, double> tb) {
  std::vector<int> nets;
  for (int v : {a, b}) {
    for (int k = d.flat_node2pin_start[v]; k < d.flat_node2pin_start[v + 1]; ++k) {
      nets.push_back(d.pin2net[d.flat_node2pin[k]]);
    }
  }
  std::sort(nets.begin(), nets.end());
  nets.erase(std::unique(nets.begin(), nets.end()), nets.end());
  const auto hpwl = [&](std::pair<double, double> ca, std::pair<double, double> cb) {
    double total = 0;
    for (int net : nets) {
      if (!d.net_mask.empty() && !d.net_mask[net]) {
        continue;
      }
      double xl = INFINITY, xh = -INFINITY, yl = INFINITY, yh = -INFINITY;
      for (int k = d.flat_net2pin_start[net]; k < d.flat_net2pin_start[net + 1]; ++k) {
        const int p = d.flat_net2pin[k];
        const int v = d.pin2node[p];
        double    x = d.node_x[v], y = d.node_y[v];
        if (v == a || v == b) {
          const auto c = v == a ? ca : cb;
          x            = c.first - d.node_size_x[v] / 2;
          y            = c.second - d.node_size_y[v] / 2;
        }
        x  += d.pin_offset_x[p];
        y  += d.pin_offset_y[p];
        xl  = std::min(xl, x);
        xh  = std::max(xh, x);
        yl  = std::min(yl, y);
        yh  = std::max(yh, y);
      }
      if (xh >= xl) {
        total += xh - xl + yh - yl;
      }
    }
    return total;
  };
  return hpwl(tb, ta) - hpwl(ta, tb);
}

double gated_anchor_weight(double base, double gate, const Macro_wire_gradient& g, double dx, double dy, double reference) {
  const double distance = std::hypot(dx, dy);
  if (gate <= 0 || distance <= 0 || reference <= 0) {
    return base;
  }
  const double slope = (g.x * dx + g.y * dy) / distance;  // HPWL growth per unit move toward the target
  return base / (1 + gate * std::max(0.0, slope) / reference);
}

std::vector<bool> edge_anchored_macros(const dp::Design& d, const std::vector<int>& macros,
                                       const std::vector<std::pair<double, double>>& targets, std::int64_t channel_x,
                                       std::int64_t channel_y, bool chain) {
  struct Box {
    double xl, yl, xh, yh, hx, hy;
  };
  const auto&      l = d.layout;
  std::vector<Box> box(macros.size());
  for (size_t k = 0; k < macros.size(); ++k) {
    const int    i  = macros[k];
    const double hx = d.node_halo_x.empty() ? 0 : d.node_halo_x[i];
    const double hy = d.node_halo_y.empty() ? 0 : d.node_halo_y[i];
    box[k]          = {targets[k].first - d.node_size_x[i] / 2,
                       targets[k].second - d.node_size_y[i] / 2,
                       targets[k].first + d.node_size_x[i] / 2,
                       targets[k].second + d.node_size_y[i] / 2,
                       hx,
                       hy};
  }
  // Tolerance: one site / one row on top of the channel+halo rule (track and grid rounding).
  const double                     tx = l.site_width, ty = l.row_height;
  const double                     mx = (l.xl + l.xh) / 2, my = (l.yl + l.yh) / 2;
  // side: 0 left, 1 right, 2 bottom, 3 top. A macro is edge-anchored to a side when its target touches it within
  // channel+halo, or (as macro.edge_snap chains) when it faces a macro anchored to that side across a gap of at most
  // the macro spacing (max(channel, 2 halo)) + tolerance, on the far side from that edge, with its center in that
  // half of the core.
  std::vector<std::array<bool, 4>> side(macros.size(), {false, false, false, false});
  std::vector<std::pair<int, int>> queue;
  for (size_t k = 0; k < macros.size(); ++k) {
    const auto&  b    = box[k];
    const double gx   = double(channel_x) + b.hx + tx;
    const double gy   = double(channel_y) + b.hy + ty;
    const bool   at[] = {b.xl - l.xl <= gx, l.xh - b.xh <= gx, b.yl - l.yl <= gy, l.yh - b.yh <= gy};
    for (int s = 0; s < 4; ++s) {
      if (at[s]) {
        side[k][s] = true;
        queue.push_back({int(k), s});
      }
    }
  }
  while (chain && !queue.empty()) {
    const auto [ka, s] = queue.back();
    queue.pop_back();
    const auto& a = box[ka];
    for (size_t k = 0; k < macros.size(); ++k) {
      if (side[k][s]) {
        continue;
      }
      const auto&  b  = box[k];
      const double cx = (b.xl + b.xh) / 2, cy = (b.yl + b.yh) / 2;
      bool         chained = false;
      if (s < 2) {
        const double space = std::max(double(channel_x), a.hx + b.hx) + tx;
        const double gap   = s == 0 ? b.xl - a.xh : a.xl - b.xh;
        chained = std::min(a.yh, b.yh) > std::max(a.yl, b.yl) && gap >= -tx && gap <= space && (s == 0 ? cx <= mx : cx >= mx);
      } else {
        const double space = std::max(double(channel_y), a.hy + b.hy) + ty;
        const double gap   = s == 2 ? b.yl - a.yh : a.yl - b.yh;
        chained = std::min(a.xh, b.xh) > std::max(a.xl, b.xl) && gap >= -ty && gap <= space && (s == 2 ? cy <= my : cy >= my);
      }
      if (chained) {
        side[k][s] = true;
        queue.push_back({int(k), s});
      }
    }
  }
  std::vector<bool> edge(macros.size());
  for (size_t k = 0; k < macros.size(); ++k) {
    edge[k] = side[k][0] || side[k][1] || side[k][2] || side[k][3];
  }
  return edge;
}

Macro_anchor::Macro_anchor(Macro_anchor_options options, std::vector<int> ids, int iteration_budget, Macro_target_generator gen,
                           std::string path)
    : o(std::move(options)), macros(std::move(ids)), budget(iteration_budget), generator(std::move(gen)), report(std::move(path)) {
  validate_macro_anchor_options(o);
  lock.assign(macros.size(), 0);
  steady.assign(macros.size(), 0);
}

void Macro_anchor::restart(std::vector<int> ids) {
  auto keep_options   = std::move(o);
  auto keep_generator = std::move(generator);
  auto keep_report    = std::move(report);
  *this = Macro_anchor(std::move(keep_options), std::move(ids), budget, std::move(keep_generator), std::move(keep_report));
}

double Macro_anchor::density_margin() const {
  if (o.inflate <= 0 || !history.empty()) {
    return 0;
  }
  if (gp_overflow < 0 || initial_overflow <= o.start_overflow) {
    return gp_overflow < 0 ? o.inflate : 0;
  }
  return o.inflate * std::clamp((gp_overflow - o.start_overflow) / (initial_overflow - o.start_overflow), 0.0, 1.0);
}

std::optional<std::vector<dp::MacroPull>> Macro_anchor::update(const dp::Design& d, int iteration) {
  if (macros.empty()) {
    return std::nullopt;
  }
  // GP macro motion per refresh window (RMS center displacement / macro max side).
  if (motion_ref.size() != macros.size() || iteration - motion_iter >= o.refresh_interval) {
    if (motion_ref.size() == macros.size() && iteration > motion_iter) {
      double              sum = 0, length = 0;
      std::vector<double> moved(macros.size());
      for (size_t k = 0; k < macros.size(); ++k) {
        const int  i    = macros[k];
        const auto c    = center(d, i);
        moved[k]        = std::hypot(c.first - motion_ref[k].first, c.second - motion_ref[k].second);
        const double m  = moved[k] / std::max({d.node_size_x[i], d.node_size_y[i], 1e-9});
        sum            += m * m;
        length         += std::max(d.node_size_x[i], d.node_size_y[i]);
      }
      length             /= double(macros.size());
      const double scale  = o.refresh_interval / double(iteration - motion_iter);
      motion_rms          = std::sqrt(sum / double(macros.size())) * scale;
      std::nth_element(moved.begin(), moved.begin() + moved.size() / 2, moved.end());
      // Reported only: the median motion in units of the mean macro side.
      const double median = moved[moved.size() / 2] / std::max(length, 1e-9) * scale;
      motion              = motion_rms;
      motion_trace.push_back({double(iteration), gp_overflow, motion_rms, median});
    }
    motion_ref.resize(macros.size());
    for (size_t k = 0; k < macros.size(); ++k) {
      motion_ref[k] = center(d, macros[k]);
    }
    motion_iter = iteration;
  }
  if (int(history.size()) >= o.max_refreshes || iteration + o.settle >= budget || iteration == last) {
    return std::nullopt;
  }
  if (gp_overflow < 0) {
    return std::nullopt;  // no GP progress sample yet
  }
  const bool  converging = o.final_weight > o.weight && o.weight > 0;
  const bool  selective  = o.select == "edge+random" || o.select == "edge+stable";
  const bool  periodic   = !history.empty() && (!stable || converging || selective) && iteration - last >= o.refresh_interval;
  std::string reason;
  if (history.empty()) {
    if (gp_overflow > o.start_overflow) {
      return std::nullopt;
    }
    // When the GP overflow never rose above 2x start_overflow (sparse macro stages start below it), the overflow
    // says nothing about convergence: targets legalized from a GP pose whose macros still move by more than the
    // spring length per refresh window are stale before the next refresh. Wait (at most half the budget) until the
    // RMS macro motion per window, in units of each macro's own size, is within the spring length.
    const double settled       = o.spring_length > 0 ? o.spring_length : .1;
    const bool   uninformative = o.start_overflow < 1 && peak_overflow <= 2 * o.start_overflow;  // 1: start at once
    if (uninformative && (motion < 0 || motion > settled) && 2 * iteration < budget) {
      return std::nullopt;
    }
    reason = "start";
    // Milestones already passed at the first refresh are consumed by it.
    while (next_milestone < int(o.milestones.size()) && gp_overflow <= o.milestones[next_milestone]) {
      ++next_milestone;
    }
  } else {
    bool crossed = false;
    while (next_milestone < int(o.milestones.size()) && gp_overflow <= o.milestones[next_milestone]) {
      ++next_milestone;
      crossed = true;
    }
    if (crossed) {
      reason = std::format("overflow<={}", o.milestones[next_milestone - 1]);
    } else if (periodic) {
      reason = "periodic";
    } else {
      return std::nullopt;
    }
  }
  refresh(d, iteration, gp_overflow, reason);
  const bool measure = selective ? o.edge_weight <= 0 && o.random_weight <= 0 : o.weight <= 0;
  if (measure || pulls.empty() || history.back().status != "ok") {
    return std::nullopt;  // measure-only or failed refresh: keep pulls and momentum
  }
  return pulls;
}

void Macro_anchor::refresh(const dp::Design& d, int iteration, double overflow, const std::string& reason) {
  const auto           started = std::chrono::steady_clock::now();
  Macro_anchor_refresh r;
  r.iteration      = iteration;
  r.overflow       = overflow;
  r.reason         = reason;
  r.macros         = int(macros.size());
  r.motion         = motion;
  last             = iteration;
  dp::Design legal = d;
  if (o.target_hysteresis > 0 && targets.size() == macros.size()) {
    for (size_t k = 0; k < macros.size(); ++k) {
      const int  i     = macros[k];
      const auto c     = center(d, i);
      legal.node_x[i] += o.target_hysteresis * (targets[k].first - c.first);
      legal.node_y[i] += o.target_hysteresis * (targets[k].second - c.second);
    }
  }
  try {
    const bool late
        = o.dead_space_refreshes < 0
          || (o.dead_space_refreshes > 0 && iteration >= budget - o.settle - o.dead_space_refreshes * o.refresh_interval);
    r.late = late;
    generator(legal, late);
    r.status = "ok";
  } catch (const std::exception& error) {
    r.status = std::string("failed: ") + error.what();
    ++failures;
  }
  if (r.status != "ok") {
    r.seconds  = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    seconds   += r.seconds;
    history.push_back(r);
    return;
  }
  legal_targets.resize(macros.size());
  for (size_t k = 0; k < macros.size(); ++k) {
    legal_targets[k] = center(legal, macros[k]);  // swaps/locks may change targets, not this legal set
  }
  legal_targets_late = r.late;
  std::vector<std::pair<double, double>> next(macros.size());
  for (size_t k = 0; k < macros.size(); ++k) {
    const int i = macros[k];
    if (lock[k] > 0 && !targets.empty()) {
      next[k] = targets[k];  // a swapped pair keeps its exchanged targets while GP carries it across
      --lock[k];
      ++r.locked;
    } else {
      next[k] = center(legal, i);
    }
  }
  if (!targets.empty()) {
    for (size_t k = 0; k < macros.size(); ++k) {
      const int    i      = macros[k];
      const double limit  = o.stable_distance * std::min(d.node_size_x[i], d.node_size_y[i]);
      r.changed          += std::hypot(next[k].first - targets[k].first, next[k].second - targets[k].second) > limit;
    }
  } else {
    r.changed = int(macros.size());
  }
  for (size_t k = 0; k < macros.size(); ++k) {
    const int    i     = macros[k];
    const double limit = o.stable_distance * std::min(d.node_size_x[i], d.node_size_y[i]);
    const bool same = !targets.empty() && std::hypot(next[k].first - targets[k].first, next[k].second - targets[k].second) <= limit;
    steady[k]       = same ? steady[k] + 1 : 0;
  }
  const auto gradients = macro_wire_gradients(d, macros);
  if (o.swaps) {
    // Candidate pairs: same size within tolerance, nearest first, whose descent directions point at each other.
    struct Candidate {
      double gain;
      size_t a, b;
    };
    std::vector<Candidate> candidates;
    for (size_t a = 0; a < macros.size(); ++a) {
      if (lock[a] > 0) {
        continue;
      }
      const int                              ia = macros[a];
      const auto                             ca = center(d, ia);
      std::vector<std::pair<double, size_t>> near;
      for (size_t b = 0; b < macros.size(); ++b) {
        const int ib = macros[b];
        if (b == a || lock[b] > 0) {
          continue;
        }
        const auto similar = [&](double u, double v) { return std::abs(u - v) <= o.swap_tolerance * std::max(u, v); };
        if (!similar(d.node_size_x[ia], d.node_size_x[ib]) || !similar(d.node_size_y[ia], d.node_size_y[ib])) {
          continue;
        }
        const auto cb = center(d, ib);
        near.push_back({std::hypot(cb.first - ca.first, cb.second - ca.second), b});
      }
      const auto count = std::min<size_t>(near.size(), o.swap_neighbors);
      std::partial_sort(near.begin(), near.begin() + count, near.end());
      for (size_t c = 0; c < count; ++c) {
        const size_t b = near[c].second;
        if (b < a && lock[b] == 0) {
          // examined from both sides; keep the pair once
          bool listed = false;
          for (const auto& x : candidates) {
            listed = listed || (x.a == b && x.b == a);
          }
          if (listed) {
            continue;
          }
        }
        const auto pair = std::minmax(ia, macros[b]);
        if (std::find(swapped_pairs.begin(), swapped_pairs.end(), std::pair{pair.first, pair.second}) != swapped_pairs.end()) {
          continue;
        }
        const auto   cb = center(d, macros[b]);
        const double dx = cb.first - ca.first, dy = cb.second - ca.second;
        const auto&  ga = gradients[a];
        const auto&  gb = gradients[b];
        const bool   ab = -(ga.x * dx + ga.y * dy) > 0;
        const bool   ba = (gb.x * dx + gb.y * dy) > 0;
        if (!ab || !ba) {
          continue;
        }
        const double gain = macro_swap_gain(d, ia, macros[b], next[a], next[b]);
        if (gain < -1e-9) {
          candidates.push_back({gain, a, b});
        }
      }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& x, const auto& y) {
      return x.gain != y.gain ? x.gain < y.gain : std::pair{x.a, x.b} < std::pair{y.a, y.b};
    });
    std::vector<bool> used(macros.size());
    jump.assign(macros.size(), false);
    for (const auto& c : candidates) {
      if (used[c.a] || used[c.b]) {
        continue;
      }
      used[c.a] = used[c.b] = true;
      std::swap(next[c.a], next[c.b]);
      lock[c.a] = lock[c.b] = o.swap_lock;
      const auto pair       = std::minmax(macros[c.a], macros[c.b]);
      swapped_pairs.push_back({pair.first, pair.second});
      jump[c.a] = jump[c.b] = o.swap_jump;
      ++r.swaps;
    }
    swap_count += r.swaps;
  }
  targets = std::move(next);
  if (!o.swaps) {
    jump.clear();
  }
  // Gate reference: median nonzero wirelength-gradient magnitude.
  std::vector<double> magnitudes;
  for (const auto& g : gradients) {
    const double m = std::hypot(g.x, g.y);
    if (m > 0) {
      magnitudes.push_back(m);
    }
  }
  double reference = 1;
  if (!magnitudes.empty()) {
    std::nth_element(magnitudes.begin(), magnitudes.begin() + magnitudes.size() / 2, magnitudes.end());
    reference = magnitudes[magnitudes.size() / 2];
  }
  const int ok_refreshes = 1 + int(std::count_if(history.begin(), history.end(), [](const auto& h) { return h.status == "ok"; }));
  r.base_weight          = o.weight * std::min(1.0, double(ok_refreshes) / o.ramp);
  if (converge > 0 && o.final_weight > o.weight) {
    r.base_weight = o.weight + (o.final_weight - o.weight) * std::min(1.0, double(converge) / o.ramp);
  }
  const double ramp_factor = std::min(1.0, double(ok_refreshes) / o.ramp);
  const bool   selective   = o.select == "edge+random" || o.select == "edge+stable";
  kind.assign(macros.size(), selective ? 0 : 3);
  if (selective) {
    const auto       edge = edge_anchored_macros(d, macros, targets, o.channel_x, o.channel_y, o.edge_chain);
    std::vector<int> rest;
    for (size_t k = 0; k < macros.size(); ++k) {
      if (edge[k]) {
        kind[k] = 1;
        ++r.edge;
      } else {
        rest.push_back(int(k));
      }
    }
    if (o.select == "edge+stable") {
      // The weak anchors go to the interior macros whose legal target held since the previous refresh; macros whose
      // target still jumps stay free until their GP position settles.
      for (int k : rest) {
        if (steady[k] > 0) {
          kind[k] = 2;
          ++r.random;
        }
      }
    } else {
      // Fresh deterministic sample per refresh (seeded by the refresh count).
      std::mt19937 rng(0x5eed + 7919 * unsigned(history.size()));
      std::shuffle(rest.begin(), rest.end(), rng);
      const auto take = size_t(std::llround(o.random_fraction * double(rest.size())));
      for (size_t j = 0; j < take && j < rest.size(); ++j) {
        kind[rest[j]] = 2;
        ++r.random;
      }
    }
    r.base_weight = o.edge_weight * ramp_factor;
  }
  double length_mean = 0;
  for (int i : macros) {
    length_mean += std::max(d.node_size_x[i], d.node_size_y[i]);
  }
  length_mean         /= std::max<size_t>(1, macros.size());
  const double spring  = o.spring_length > 0 ? std::max(1e-9, o.spring_length * length_mean) : 0;
  pulls.clear();
  double weight_sum = 0;
  for (size_t k = 0; k < macros.size(); ++k) {
    const int    i  = macros[k];
    const auto   c  = center(d, i);
    const double dx = targets[k].first - c.first, dy = targets[k].second - c.second;
    const double distance  = std::hypot(dx, dy);
    r.mean_distance       += distance;
    r.max_distance         = std::max(r.max_distance, distance);
    double w               = 0;
    // A macro's pull ramps with its own target stability: a jumping target restarts its ramp.
    const double own_ramp         = std::min(1.0, double(steady[k] + 1) / o.ramp);
    if (kind[k] == 3) {
      w = gated_anchor_weight(r.base_weight, o.gate, gradients[k], dx, dy, reference);
    } else if (kind[k] == 1) {
      w = o.edge_gate ? gated_anchor_weight(o.edge_weight * own_ramp, o.gate, gradients[k], dx, dy, reference)
                      : o.edge_weight * own_ramp;
    } else if (kind[k] == 2) {
      w = gated_anchor_weight(o.random_weight * own_ramp, o.gate, gradients[k], dx, dy, reference);
    }
    weight_sum += w;
    dp::MacroPull pull{i, targets[k].first, targets[k].second, w, k < jump.size() && jump[k]};
    pull.length = spring;
    pulls.push_back(pull);
  }
  r.mean_distance /= std::max<size_t>(1, macros.size());
  r.mean_weight    = weight_sum / std::max<size_t>(1, macros.size());
  stable           = stable || (r.changed <= o.stable_fraction * macros.size() && r.swaps == 0 && r.locked == 0);
  converge        += stable;
  r.seconds        = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  seconds         += r.seconds;
  history.push_back(r);
}

void Macro_anchor::before_legalize(dp::Design& d) {
  pre_legal.clear();
  for (int i : macros) {
    pre_legal.push_back(center(d, i));
  }
  if (o.final_targets && legal_targets.size() == macros.size() && !history.empty()) {
    const auto last_ok = std::find_if(history.rbegin(), history.rend(), [](const auto& h) { return h.status == "ok"; });
    if (last_ok != history.rend()) {
      // GP kept moving after the last refresh. Legalize the final GP pose once more (same chain, hysteresis
      // toward the last targets) and keep whichever legal set gives the lower HPWL against the final cell pose.
      // The final pass skips the host chain, so a last set from an early refresh (which skipped the late
      // passes, e.g. the dead-space collapse) is not comparable: the fresh late set replaces it outright.
      const bool must_refresh = o.dead_space_refreshes != 0 && !legal_targets_late;
      final_cost_last         = hpwl_with(d, macros, legal_targets);
      final_choice            = "last";
      dp::Design fresh = d;
      for (size_t k = 0; k < macros.size(); ++k) {
        const int  i     = macros[k];
        const auto c     = center(d, i);
        fresh.node_x[i] += o.target_hysteresis * (targets[k].first - c.first);
        fresh.node_y[i] += o.target_hysteresis * (targets[k].second - c.second);
      }
      try {
        generator(fresh, true);
        std::vector<std::pair<double, double>> candidate(macros.size());
        for (size_t k = 0; k < macros.size(); ++k) {
          candidate[k] = center(fresh, macros[k]);
        }
        final_cost_fresh = hpwl_with(d, macros, candidate);
        if (must_refresh || final_cost_fresh < final_cost_last) {
          legal_targets      = std::move(candidate);
          legal_targets_late = true;
          final_choice       = must_refresh ? "fresh (last not late)" : "fresh";
        }
      } catch (const std::exception&) {
        final_choice = "last (fresh failed)";
      }
    }
    if (last_ok != history.rend()) {
      for (size_t k = 0; k < macros.size(); ++k) {
        const int i = macros[k];
        d.node_x[i] = legal_targets[k].first - d.node_size_x[i] / 2;
        d.node_y[i] = legal_targets[k].second - d.node_size_y[i] / 2;
      }
      seeded           = true;
      // An early set the late passes never saw seeds the hard pass, but the host chain must still run on it.
      override_pending = o.dead_space_refreshes == 0 || legal_targets_late;
    }
  }
}

bool Macro_anchor::final_override(dp::Design& d) {
  if (!override_pending) {
    return false;
  }
  override_pending = false;
  overridden       = true;
  for (size_t k = 0; k < macros.size(); ++k) {
    const int i = macros[k];
    d.node_x[i] = legal_targets[k].first - d.node_size_x[i] / 2;
    d.node_y[i] = legal_targets[k].second - d.node_size_y[i] / 2;
  }
  return true;
}

void Macro_anchor::finish(const dp::Design& d, int iterations) {
  if (report.empty()) {
    return;
  }
  const double um         = o.dbu_per_um;
  double       snap_total = 0, snap_max = 0, target_total = 0, target_max = 0;
  int          snap_moved    = 0;
  double       kind_snap[4]  = {0, 0, 0, 0};
  int          kind_count[4] = {0, 0, 0, 0};
  for (size_t k = 0; k < macros.size(); ++k) {
    const auto c = center(d, macros[k]);
    if (k < pre_legal.size()) {
      const double m  = std::hypot(c.first - pre_legal[k].first, c.second - pre_legal[k].second) / um;
      snap_total     += m;
      snap_max        = std::max(snap_max, m);
      snap_moved     += m > 1e-6;
      if (k < kind.size()) {
        kind_snap[kind[k]] += m;
        ++kind_count[kind[k]];
      }
    }
    if (k < targets.size()) {
      const double m  = std::hypot(c.first - targets[k].first, c.second - targets[k].second) / um;
      target_total   += m;
      target_max      = std::max(target_max, m);
    }
  }
  const double  count = std::max<size_t>(1, macros.size());
  std::ofstream out(report);
  out << std::format(
      R"({{"schema_version":1,"weight":{},"gate":{},"swaps_enabled":{},"macros":{},"gp_iterations":{},"refresh_count":{},"failures":{},"swaps_accepted":{},"seconds":{},)",
      o.weight,
      o.gate,
      o.swaps,
      macros.size(),
      iterations,
      std::count_if(history.begin(), history.end(), [](const auto& h) { return h.status == "ok"; }),
      failures,
      swap_count,
      seconds);
  out << std::format(
      R"("final_snap":{{"measured":{},"moved_macros":{},"mean_um":{},"max_um":{},"total_um":{}}},"final_vs_last_target":{{"measured":{},"mean_um":{},"max_um":{}}},"refreshes":[)",
      !pre_legal.empty(),
      snap_moved,
      snap_total / count,
      snap_max,
      snap_total,
      !targets.empty(),
      target_total / count,
      target_max);
  for (size_t k = 0; k < history.size(); ++k) {
    const auto& h = history[k];
    out << (k ? "," : "")
        << std::format(
               R"({{"iteration":{},"overflow":{},"late":{},"reason":"{}","status":"{}","changed":{},"swaps":{},"locked":{},"base_weight":{},"mean_weight":{},"mean_distance_um":{},"max_distance_um":{},"seconds":{},"edge":{},"random":{},"motion":{}}})",
               h.iteration,
               h.overflow,
               h.late,
               h.reason,
               h.status.substr(0, h.status.find('"')),
               h.changed,
               h.swaps,
               h.locked,
               h.base_weight,
               h.mean_weight,
               h.mean_distance / um,
               h.max_distance / um,
               h.seconds,
               h.edge,
               h.random,
               h.motion);
  }
  out << std::format(
      R"(],"select":"{}","edge_chain":{},"final_targets_seeded":{},"final_targets_kept":{},"final_snap_by_kind":{{"free":[{},{}],"edge":[{},{}],"random":[{},{}],"all":[{},{}]}})",
      o.select,
      o.edge_chain,
      seeded,
      overridden,
      kind_count[0],
      kind_count[0] ? kind_snap[0] / kind_count[0] : 0.0,
      kind_count[1],
      kind_count[1] ? kind_snap[1] / kind_count[1] : 0.0,
      kind_count[2],
      kind_count[2] ? kind_snap[2] / kind_count[2] : 0.0,
      kind_count[3],
      kind_count[3] ? kind_snap[3] / kind_count[3] : 0.0);
  out << std::format(R"(,"final_choice":"{}","final_cost_last":{},"final_cost_fresh":{},"motion_trace":[)",
                     final_choice,
                     final_cost_last,
                     final_cost_fresh);
  for (size_t k = 0; k < motion_trace.size(); ++k) {
    out << (k ? "," : "")
        << std::format("[{},{:.4f},{:.4f},{:.4f}]", motion_trace[k][0], motion_trace[k][1], motion_trace[k][2], motion_trace[k][3]);
  }
  out << "]";
  out << ",\"swapped_pairs\":[";
  for (size_t k = 0; k < swapped_pairs.size(); ++k) {
    out << (k ? "," : "") << std::format("[{},{}]", swapped_pairs[k].first, swapped_pairs[k].second);
  }
  out << "]}\n";
  if (!out) {
    throw std::runtime_error("cannot write macro anchor report");
  }
}
}  // namespace livehd::placement
