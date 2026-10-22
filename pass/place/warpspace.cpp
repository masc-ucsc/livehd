// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "warpspace.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <set>
#include <stdexcept>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
namespace livehd::placement {
namespace dp = DreamPlace::native;
void validate_warp_options(const Warp_options& o) {
  if (o.bins < 2 || o.bins > 64 || !o.iterations || o.iterations > 6 || !o.updates || o.updates > 30 || !std::isfinite(o.weight)
      || o.weight < 0 || o.weight > 1 || !std::isfinite(o.warmup) || o.warmup < 0 || o.warmup >= 1 || !std::isfinite(o.seconds)
      || o.seconds <= 0 || o.seconds > 30 || !std::isfinite(o.dbu_per_um) || o.dbu_per_um <= 0) {
    throw std::invalid_argument(
        "invalid warpspace policy: bins 2..64, iterations 1..6, updates 1..30, weight 0..1, warmup [0,1), seconds (0,30]");
  }
}
Warp_field placement_warp(const dp::Design& d, const std::vector<int>& memories, const Warp_options& o, bool sample_cells) {
  validate_warp_options(o);
  const auto nodes = d.node_x.size();
  if (d.num_movable < 0 || size_t(d.num_movable) > nodes || d.node_y.size() != nodes || d.node_size_x.size() != nodes
      || d.node_size_y.size() != nodes || (!d.node_macro.empty() && d.node_macro.size() != nodes)) {
    throw std::invalid_argument("inconsistent warp node arrays");
  }
  for (int n : memories) {
    if (n < 0 || n >= d.num_movable || d.node_macro.empty() || (bool(d.node_macro[n]) == sample_cells)) {
      throw std::invalid_argument("invalid movable warp sample");
    }
  }
  warp::Layout layout;
  const double units = o.dbu_per_um;
  layout.lo          = {d.layout.xl / units, d.layout.yl / units};
  layout.hi          = {d.layout.xh / units, d.layout.yh / units};
  if (!std::isfinite(layout.lo.x) || !std::isfinite(layout.lo.y) || !std::isfinite(layout.hi.x) || !std::isfinite(layout.hi.y)
      || layout.width() <= 0 || layout.height() <= 0) {
    throw std::invalid_argument("invalid warp layout");
  }
  std::vector<double> mass(o.bins * o.bins);
  const double        dx = layout.width() / o.bins, dy = layout.height() / o.bins;
  for (size_t n = 0; n < d.node_x.size(); ++n) {
    const double w = d.node_size_x[n] / units, h = d.node_size_y[n] / units;
    const double origin_x = d.node_x[n] / units, origin_y = d.node_y[n] / units;
    if (!std::isfinite(d.node_x[n]) || !std::isfinite(d.node_y[n]) || !std::isfinite(w) || !std::isfinite(h) || w < 0 || h < 0) {
      throw std::invalid_argument("invalid warp node geometry");
    }
    if (w <= 0 || h <= 0) {
      continue;
    }
    if (!d.node_macro.empty() && d.node_macro[n]) {
      warp::Inst c;
      c.name     = std::to_string(n);
      c.p        = {origin_x, origin_y};
      c.original = c.p;
      c.w        = w;
      c.h        = h;
      c.macro    = true;
      c.fixed    = n >= size_t(d.num_movable);
      layout.insts.push_back(std::move(c));
      continue;
    }
    const double xl = std::max(layout.lo.x, origin_x), xh = std::min(layout.hi.x, origin_x + w);
    const double yl = std::max(layout.lo.y, origin_y), yh = std::min(layout.hi.y, origin_y + h);
    if (xl >= xh || yl >= yh) {
      continue;
    }
    const int x0 = std::clamp(int((xl - layout.lo.x) / dx), 0, int(o.bins) - 1);
    const int x1 = std::clamp(int((xh - layout.lo.x) / dx), 0, int(o.bins) - 1);
    const int y0 = std::clamp(int((yl - layout.lo.y) / dy), 0, int(o.bins) - 1);
    const int y1 = std::clamp(int((yh - layout.lo.y) / dy), 0, int(o.bins) - 1);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        mass[y * o.bins + x] += std::max(0., std::min(xh, layout.lo.x + (x + 1) * dx) - std::max(xl, layout.lo.x + x * dx))
                                * std::max(0., std::min(yh, layout.lo.y + (y + 1) * dy) - std::max(yl, layout.lo.y + y * dy));
      }
    }
  }
  warp::Options controls;
  controls.bins          = o.bins;
  controls.iters         = o.iterations;
  controls.time_limit    = o.seconds;
  controls.macro_space   = true;
  controls.balance       = 3;
  controls.balance_model = "strain";
  Warp_field field;
  field.density = warp::density_warp(layout, mass, controls);
  for (int n : memories) {
    const warp::Point center{d.node_x[n] + d.node_size_x[n] / 2, d.node_y[n] + d.node_size_y[n] / 2};
    const auto        mapped = layout.physical(warp::sample_field(field.density.mesh, layout.normalize(center * (1 / units))));
    if (!std::isfinite(mapped.x) || !std::isfinite(mapped.y)) {
      throw std::runtime_error("nonfinite warp anchor");
    }
    field.delta.emplace(n, mapped * units - center);
  }
  return field;
}
std::vector<Warp_centroid_shift> shift_warp_centroids(const dp::Design& d, const std::map<int, warp::Point>& samples,
                                                      std::vector<dp::FloorplanCircle>& targets, double gain) {
  if (!std::isfinite(gain) || gain < 0 || gain > 1) {
    throw std::invalid_argument("invalid centroid warp gain");
  }
  if (targets.size() != d.centroid_groups.size()) {
    throw std::invalid_argument("warp centroid count mismatch");
  }
  std::vector<Warp_centroid_shift> own(targets.size()), result(targets.size());
  std::vector<unsigned>            depths(targets.size());
  for (size_t g = 0; g < targets.size(); ++g) {
    std::set<int> seen;
    for (int p = int(g); p >= 0; p = d.centroid_groups.at(p).parent) {
      if (!seen.insert(p).second) {
        throw std::invalid_argument("cyclic warp centroid hierarchy");
      }
      ++depths[g];
    }
  }
  for (const auto& [n, delta] : samples) {
    if (n < 0 || n >= d.num_movable || d.node_macro.empty() || !d.node_macro.at(n)) {
      continue;
    }
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) {
      throw std::invalid_argument("nonfinite centroid warp sample");
    }
    int owner = -1;
    for (size_t g = 0; g < targets.size(); ++g) {
      const auto& group           = d.centroid_groups[g];
      const bool  explicit_member = std::find(group.nodes.begin(), group.nodes.end(), n) != group.nodes.end();
      const bool  color_member
          = !d.node_centroid_color.empty()
            && std::find(group.colors.begin(), group.colors.end(), d.node_centroid_color.at(n)) != group.colors.end();
      if ((explicit_member || color_member) && (owner < 0 || depths[g] > depths[owner])) {
        owner = g;
      }
    }
    if (owner >= 0) {
      own[owner].sum = own[owner].sum + delta;
      ++own[owner].memories;
    }
  }
  for (size_t g = 0; g < own.size(); ++g) {
    if (!own[g].memories) {
      continue;
    }
    const auto movement = own[g].sum * (gain / own[g].memories);
    for (int p = int(g); p >= 0; p = d.centroid_groups[p].parent) {
      result[p].sum = result[p].sum + movement;
      ++result[p].count;
    }
  }
  const auto before = targets;
  for (size_t g = 0; g < targets.size(); ++g) {
    result[g].memories = own[g].memories;
    if (result[g].count) {
      targets[g].x = before[g].x + result[g].sum.x / result[g].count;
      targets[g].y = before[g].y + result[g].sum.y / result[g].count;
    }
  }
  return result;
}
namespace {
void save(const std::string& path, const std::string& content) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream out(path);
  out << content << '\n';
  if (!out) {
    throw std::runtime_error("cannot write warp report: " + path);
  }
}
}  // namespace
void write_warp_status(const std::string& path, const std::string& status, const std::string& reason) {
  rapidjson::StringBuffer                    b;
  rapidjson::Writer<rapidjson::StringBuffer> w(b);
  w.StartObject();
  w.Key("status");
  w.String(status.c_str());
  w.Key("reason");
  w.String(reason.c_str());
  w.EndObject();
  save(path, b.GetString());
}
void write_warp_field(const Warp_field& f, const Warp_options& o, const std::string& path, int iteration, const dp::Design& d) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("algorithm");
  w.String("density-warp-v3-scaled");
  w.Key("field_area_units");
  w.String("um2");
  w.Key("sample_coordinate_units");
  w.String("DBU");
  w.Key("iteration");
  w.Int(iteration);
  w.Key("weight");
  w.Double(o.weight);
  w.Key("seconds");
  w.Double(f.density.seconds);
  w.Key("mass_before");
  w.Double(f.density.total);
  w.Key("mass_after");
  w.Double(f.density.out_total);
  w.Key("occupied_before");
  w.Double(f.density.total_occupied);
  w.Key("occupied_after");
  w.Double(f.density.transported_occupied);
  w.Key("bins");
  w.Uint(o.bins);
  w.Key("hpwl");
  w.Double(dp::hpwl(d));
  for (bool after : {false, true}) {
    const auto& values = after ? f.density.b : f.density.a;
    auto        sorted = values;
    std::sort(sorted.begin(), sorted.end());
    w.Key(after ? "density_after" : "density_before");
    w.StartObject();
    w.Key("peak");
    w.Double(sorted.back());
    w.Key("p99");
    w.Double(sorted[std::min(sorted.size() - 1, size_t(.99 * sorted.size()))]);
    double overflow = 0;
    for (double value : values) {
      overflow += std::max(0., value - .85);
    }
    w.Key("overflow");
    w.Double(overflow);
    w.Key("grid");
    w.StartArray();
    for (double value : values) {
      w.Double(value);
    }
    w.EndArray();
    w.EndObject();
  }
  warp::Point mean;
  double      sum_movement = 0, max_movement = 0;
  for (const auto& [node, delta] : f.delta) {
    (void)node;
    mean          = mean + delta;
    sum_movement += warp::length(delta);
    max_movement  = std::max(max_movement, warp::length(delta));
  }
  const double count = std::max<size_t>(1, f.delta.size());
  w.Key("mean_dx");
  w.Double(mean.x / count);
  w.Key("mean_dy");
  w.Double(mean.y / count);
  w.Key("mean_movement");
  w.Double(sum_movement / count);
  w.Key("max_movement");
  w.Double(max_movement);
  w.Key("status");
  w.String("optimal");
  w.Key("anchor_predictions_only");
  w.Bool(true);
  w.Key("iterations");
  w.Uint(o.iterations);
  w.Key("axis_seconds_limit");
  w.Double(o.seconds);
  w.Key("passes");
  w.StartArray();
  for (const auto& pass : f.density.passes) {
    w.StartObject();
    w.Key("axis");
    w.String(pass.axis.c_str());
    w.Key("status");
    w.String(pass.status.c_str());
    w.Key("seconds");
    w.Double(pass.seconds);
    w.Key("objective_scale");
    w.Double(pass.objective_scale);
    w.Key("capacity_slack_um2");
    w.Double(pass.slack);
    w.EndObject();
  }
  w.EndArray();
  w.Key("samples");
  w.StartArray();
  for (const auto& [n, delta] : f.delta) {
    w.StartObject();
    w.Key("node");
    w.Int(n);
    w.Key("dx");
    w.Double(delta.x);
    w.Key("dy");
    w.Double(delta.y);
    w.Key("anchor_x");
    w.Double(d.node_x[n] + d.node_size_x[n] / 2 + delta.x);
    w.Key("anchor_y");
    w.Double(d.node_y[n] + d.node_size_y[n] / 2 + delta.y);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  save(path, buffer.GetString());
}
Macro_warp::Macro_warp(Warp_options o, int budget, std::string path)
    : options(o), prefix(std::move(path)), iteration_budget(budget) {
  validate_warp_options(o);
  next   = std::max(1, int(std::ceil(budget * o.warmup)));
  stride = std::max(1, int(std::ceil(double(std::max(0, budget - next)) / o.updates)));
}
std::optional<std::vector<dp::MacroPull>> Macro_warp::refresh(const dp::Design& d, const std::vector<int>& ids, int iteration) {
  if (!options.enabled || options.weight == 0 || ids.empty() || iteration < next || iteration >= iteration_budget
      || (options.once_after_density && iteration > iteration_budget - 20) || iteration == last
      || attempts >= (options.once_after_density ? 1U : options.updates)) {
    return std::nullopt;
  }
  last = iteration;
  next = iteration + stride;
  ++attempts;
  const auto started = warp::Clock::now();
  try {
    auto                       field = placement_warp(d, ids, options);
    std::vector<dp::MacroPull> pulls;
    for (const auto& [n, delta] : field.delta) {
      pulls.push_back(
          {n, d.node_x[n] + d.node_size_x[n] / 2 + delta.x, d.node_y[n] + d.node_size_y[n] / 2 + delta.y, options.weight});
    }
    write_warp_field(field, options, prefix + "_" + std::to_string(attempts) + ".json", iteration, d);
    ++successes;
    elapsed_seconds += std::chrono::duration<double>(warp::Clock::now() - started).count();
    return pulls;
  } catch (const std::exception& error) {
    // No partial field or stale geometry-dependent targets are retained.
    rapidjson::StringBuffer                    b;
    rapidjson::Writer<rapidjson::StringBuffer> w(b);
    w.StartObject();
    w.Key("iteration");
    w.Int(iteration);
    w.Key("status");
    w.String("cleared");
    w.Key("weight");
    w.Double(options.weight);
    w.Key("seconds");
    w.Double(std::chrono::duration<double>(warp::Clock::now() - started).count());
    w.Key("reason");
    w.String(error.what());
    w.EndObject();
    save(prefix + "_" + std::to_string(attempts) + ".json", b.GetString());
    elapsed_seconds += std::chrono::duration<double>(warp::Clock::now() - started).count();
    return std::vector<dp::MacroPull>{};
  }
}
void Macro_warp::finish(int actual) const {
  save(prefix + "_summary.json",
       "{\"attempts\":" + std::to_string(attempts) + ",\"successes\":" + std::to_string(successes) + ",\"once_after_density\":"
           + (options.once_after_density ? "true" : "false") + ",\"last_anchor_iteration\":" + std::to_string(last)
           + ",\"iterations_after_anchor\":" + std::to_string(last < 0 ? 0 : std::max(0, actual - last))
           + ",\"actual_iterations\":" + std::to_string(actual) + ",\"budget\":" + std::to_string(options.updates)
           + ",\"iteration_budget\":" + std::to_string(iteration_budget) + ",\"early_termination\":"
           + (actual < iteration_budget ? "true" : "false") + ",\"seconds\":" + std::to_string(elapsed_seconds) + "}");
}
}  // namespace livehd::placement
