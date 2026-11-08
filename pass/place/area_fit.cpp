// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "area_fit.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "core/timing_feedback.hpp"
#include "hhds/graph.hpp"
#include "inou/lefdef/tech_profile.hpp"
#include "legroute.hpp"
#include "place.hpp"
#include "placement_technology.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement {
namespace ph = livehd::physical;
namespace fs = std::filesystem;

void validate_area_fit_options(const Area_fit_options& o) {
  if (!(o.min_fraction > 0 && o.min_fraction <= 1) || !(o.max_utilization > 0 && o.max_utilization <= 1)
      || !(o.tolerance > 0 && o.tolerance < 1) || !(o.overflow_slack >= 0) || !std::isfinite(o.overflow_slack)
      || !(o.overflow_floor >= 0) || !std::isfinite(o.overflow_floor) || !o.net_pops) {
    throw std::invalid_argument(
        "place.area: min_fraction in (0,1], max_utilization in (0,1], tolerance in (0,1), finite nonnegative overflow "
        "slack/floor and a positive net_pops");
  }
  if (o.anchor != "io" && o.anchor != "center") {
    throw std::invalid_argument("place.area.anchor must be io or center");
  }
  if (o.oracle != "legroute" && o.oracle != "rudy") {
    throw std::invalid_argument("place.area.oracle must be legroute or rudy");
  }
}

Area_fit_core area_fit_core(const ph::Technology& tech, const ph::Floorplan& fp) {
  if (fp.rows.empty()) {
    throw std::runtime_error("place.area=auto needs a floorplan with rows");
  }
  Area_fit_core core;
  const auto&   site = tech.sites.find(fp.rows.front().site);
  if (site == tech.sites.end()) {
    throw std::runtime_error("place.area=auto: unknown row site " + fp.rows.front().site);
  }
  core.row  = std::llround(site->second.height * fp.dbu);
  core.site = fp.rows.front().step > 0 ? fp.rows.front().step : std::llround(site->second.width * fp.dbu);
  core.rect = {std::numeric_limits<double>::max(),
               std::numeric_limits<double>::max(),
               std::numeric_limits<double>::lowest(),
               std::numeric_limits<double>::lowest()};
  for (const auto& row : fp.rows) {
    const auto step = row.step > 0 ? row.step : core.site;
    core.rect.xl    = std::min(core.rect.xl, double(row.x));
    core.rect.yl    = std::min(core.rect.yl, double(row.y));
    core.rect.xh    = std::max(core.rect.xh, double(row.x + row.count * step));
    core.rect.yh    = std::max(core.rect.yh, double(row.y + core.row));
  }
  return core;
}

namespace {
// Oriented DEF extents of a component from its LEF master.
std::pair<int64_t, int64_t> extent(const ph::Macro& m, const ph::Component& c, int64_t dbu) {
  const auto w = std::llround(m.width * dbu), h = std::llround(m.height * dbu);
  return ph::swaps_axes(c.place.orientation) ? std::pair{h, w} : std::pair{w, h};
}
bool fixed(const ph::Component& c) { return c.place.status == ph::Status::FIXED || c.place.status == ph::Status::LOCKED; }
}  // namespace

Area_fit_areas area_fit_areas(const ph::Technology& tech, const ph::Floorplan& fp) {
  Area_fit_areas areas;
  for (const auto& c : fp.components) {
    const auto m = tech.macros.find(c.macro);
    if (m == tech.macros.end() || ph::placement_mask(c, tech) != ph::Placement_mask::none) {
      continue;
    }
    const auto& klass = m->second.klass;
    const bool  soft  = ph::soft_block(m->second);
    if (!klass.starts_with("CORE") && !klass.starts_with("BLOCK")) {
      continue;  // PAD, COVER, RING and IO-ring ENDCAP masters sit outside the rows
    }
    const auto [w, h] = extent(m->second, c, fp.dbu);
    if (klass.starts_with("CORE") || soft) {
      // A soft block is a standard-cell cluster: its area competes for rows like the cells it stands for.
      areas.cells += double(w) * double(h);
    } else {
      areas.macros += double(w) * double(h);
    }
    if (!klass.starts_with("CORE")) {
      // the region must still hold the largest hard or soft block body
      areas.max_macro_w = std::max(areas.max_macro_w, w);
      areas.max_macro_h = std::max(areas.max_macro_h, h);
    }
  }
  return areas;
}

std::pair<double, double> area_fit_anchor(const ph::Floorplan& fp, const Area_fit_core& core, const std::string& anchor) {
  const std::pair<double, double> center{(core.rect.xl + core.rect.xh) / 2, (core.rect.yl + core.rect.yh) / 2};
  if (anchor == "center") {
    return center;
  }
  double x = 0, y = 0;
  size_t n = 0;
  for (const auto& t : fp.terminals) {
    if (t.status == ph::Status::UNPLACED || (t.use != "SIGNAL" && !t.use.empty())) {
      continue;
    }
    x += double(t.x);
    y += double(t.y);
    ++n;
  }
  return n ? std::pair{x / double(n), y / double(n)} : center;
}

ph::Rect area_fit_region(const ph::Technology& tech, const ph::Floorplan& fp, const Area_fit_core& core,
                         const Area_fit_areas& areas, double fraction, std::pair<double, double> anchor) {
  const double W = core.rect.xh - core.rect.xl, H = core.rect.yh - core.rect.yl;
  const auto   sites = int64_t(std::llround(W / double(core.site))), rows = int64_t(std::llround(H / double(core.row)));
  const double scale = std::sqrt(std::clamp(fraction, 0.0, 1.0));
  // each side rounded up to whole sites/rows (the area never falls under the fraction), at least the largest macro
  auto         w     = std::clamp<int64_t>(int64_t(std::ceil(W * scale / double(core.site) - 1e-9)), 1, sites);
  auto         h     = std::clamp<int64_t>(int64_t(std::ceil(H * scale / double(core.row) - 1e-9)), 1, rows);
  w                  = std::min(sites, std::max(w, (areas.max_macro_w + core.site - 1) / core.site));
  h                  = std::min(rows, std::max(h, (areas.max_macro_h + core.row - 1) / core.row));
  // the lower-left on the site/row lattice nearest to centering at the anchor, inside the core
  const auto x0
      = std::clamp<int64_t>(std::llround((anchor.first - core.rect.xl) / double(core.site) - double(w) / 2), 0, sites - w);
  const auto y0 = std::clamp<int64_t>(std::llround((anchor.second - core.rect.yl) / double(core.row) - double(h) / 2), 0, rows - h);
  ph::Rect   r{core.rect.xl + double(x0 * core.site),
               core.rect.yl + double(y0 * core.row),
               core.rect.xl + double((x0 + w) * core.site),
               core.rect.yl + double((y0 + h) * core.row)};
  // fixed components keep their place: the region covers them (grown on the lattice, still inside the core)
  for (const auto& c : fp.components) {
    const auto m = tech.macros.find(c.macro);
    if (!fixed(c) || m == tech.macros.end() || ph::placement_mask(c, tech) != ph::Placement_mask::none) {
      continue;
    }
    const auto [cw, ch] = extent(m->second, c, fp.dbu);
    const ph::Rect body{double(c.place.x), double(c.place.y), double(c.place.x + cw), double(c.place.y + ch)};
    if (!body.overlaps(core.rect)) {
      continue;  // outside the rows already (a pad ring cell): no row of the region can matter
    }
    r.xl = std::min(r.xl, core.rect.xl + std::floor((body.xl - core.rect.xl) / double(core.site)) * double(core.site));
    r.yl = std::min(r.yl, core.rect.yl + std::floor((body.yl - core.rect.yl) / double(core.row)) * double(core.row));
    r.xh = std::max(r.xh, core.rect.xl + std::ceil((body.xh - core.rect.xl) / double(core.site)) * double(core.site));
    r.yh = std::max(r.yh, core.rect.yl + std::ceil((body.yh - core.rect.yl) / double(core.row)) * double(core.row));
  }
  return r.intersection(core.rect);
}

ph::Floorplan area_fit_fence(const ph::Floorplan& fp, const ph::Rect& region) {
  auto fenced = fp;
  fenced.rows.clear();
  for (const auto& row : fp.rows) {
    if (row.step <= 0 || double(row.y) < region.yl || double(row.y) >= region.yh) {
      continue;
    }
    const auto first = std::max<int64_t>(0, int64_t(std::ceil((region.xl - double(row.x)) / double(row.step) - 1e-9)));
    const auto last  = std::min<int64_t>(row.count, int64_t(std::floor((region.xh - double(row.x)) / double(row.step) + 1e-9)));
    if (last <= first) {
      continue;
    }
    auto clipped  = row;
    clipped.x     = row.x + first * row.step;
    clipped.count = last - first;
    fenced.rows.push_back(std::move(clipped));
  }
  if (fenced.rows.empty()) {
    throw std::runtime_error("place.area: the region holds no complete row");
  }
  return fenced;
}

namespace {
// RUDY (Spindler & Johannes): each net spreads its bounding-box wirelength uniformly over the box, H and V demand
// apart, against the track capacity of the routing layers of each direction. Pins at the cell centers.
Area_fit_route rudy_route(const ph::Technology& tech, const ph::Floorplan& fp) {
  Area_fit_route out;
  out.oracle          = "rudy";
  const auto     core = area_fit_core(tech, fp);
  const ph::Rect bounds{std::min(core.rect.xl, fp.die.xl),
                        std::min(core.rect.yl, fp.die.yl),
                        std::max(core.rect.xh, fp.die.xh),
                        std::max(core.rect.yh, fp.die.yh)};
  const int      bins_x = 128, bins_y = 128;
  const double   bw = (bounds.xh - bounds.xl) / bins_x, bh = (bounds.yh - bounds.yl) / bins_y;
  // tracks per DBU in each direction, summed over the routing layers (the first layer stays for pin access)
  double         tracks_h = 0, tracks_v = 0;
  for (size_t l = 1; l < tech.routing_layers.size(); ++l) {
    const auto& layer = tech.layers.at(tech.routing_layers[l]);
    if (layer.direction == "HORIZONTAL" && layer.pitch_y > 0) {
      tracks_h += 1 / (layer.pitch_y * double(fp.dbu));
    } else if (layer.direction == "VERTICAL" && layer.pitch_x > 0) {
      tracks_v += 1 / (layer.pitch_x * double(fp.dbu));
    }
  }
  std::vector<double>                              dh(size_t(bins_x) * bins_y), dv(dh.size());
  std::map<std::string, std::pair<double, double>> where;
  for (const auto& c : fp.components) {
    const auto m = tech.macros.find(c.macro);
    if (m == tech.macros.end()) {
      continue;
    }
    const auto [w, h] = extent(m->second, c, fp.dbu);
    where[c.name]     = {double(c.place.x) + double(w) / 2, double(c.place.y) + double(h) / 2};
  }
  for (const auto& t : fp.terminals) {
    where["PIN:" + t.name] = {double(t.x), double(t.y)};
  }
  for (const auto& net : fp.nets) {
    if (net.connections.size() < 2 || (!net.use.empty() && net.use != "SIGNAL")) {
      continue;
    }
    ++out.signals;
    double xl = std::numeric_limits<double>::max(), yl = xl, xh = std::numeric_limits<double>::lowest(), yh = xh;
    for (const auto& c : net.connections) {
      const auto it = where.find(c.instance == "PIN" ? "PIN:" + c.pin : c.instance);
      if (it == where.end()) {
        continue;
      }
      xl = std::min(xl, it->second.first);
      xh = std::max(xh, it->second.first);
      yl = std::min(yl, it->second.second);
      yh = std::max(yh, it->second.second);
    }
    if (xl > xh) {
      continue;
    }
    // a degenerate box spreads over one bin
    xh             = std::max(xh, xl + bw);
    yh             = std::max(yh, yl + bh);
    const double w = xh - xl, h = yh - yl;
    const int    ixl = std::clamp(int((xl - bounds.xl) / bw), 0, bins_x - 1),
                 ixh = std::clamp(int((xh - bounds.xl) / bw), 0, bins_x - 1);
    const int    iyl = std::clamp(int((yl - bounds.yl) / bh), 0, bins_y - 1),
                 iyh = std::clamp(int((yh - bounds.yl) / bh), 0, bins_y - 1);
    for (int ix = ixl; ix <= ixh; ++ix) {
      const double ox = std::min(xh, bounds.xl + (ix + 1) * bw) - std::max(xl, bounds.xl + ix * bw);
      for (int iy = iyl; iy <= iyh; ++iy) {
        const double oy = std::min(yh, bounds.yl + (iy + 1) * bh) - std::max(yl, bounds.yl + iy * bh);
        if (ox <= 0 || oy <= 0) {
          continue;
        }
        // wire length in the bin: the box's H length times the overlap fraction (and V alike)
        const auto k  = size_t(ix) * bins_y + size_t(iy);
        dh[k]        += w * (ox * oy) / (w * h);
        dv[k]        += h * (ox * oy) / (w * h);
      }
    }
  }
  // capacity: tracks crossing the bin times its length (wire length the bin can hold); macro bodies keep theirs
  // (an optimistic estimate: the fallback when legroute is unavailable)
  const double cap_h = tracks_h * bh * bw, cap_v = tracks_v * bw * bh;
  double       over = 0, total = 0;
  for (size_t k = 0; k < dh.size(); ++k) {
    if ((dh[k] > 0 && cap_h <= 0) || (dv[k] > 0 && cap_v <= 0)) {
      out.capacity_valid = false;
    }
    over               += std::max(0.0, dh[k] - cap_h) + std::max(0.0, dv[k] - cap_v);
    total              += cap_h + cap_v;
    out.peak            = std::max({out.peak, cap_h > 0 ? dh[k] / cap_h : 0.0, cap_v > 0 ? dv[k] / cap_v : 0.0});
    out.overflow_edges += (dh[k] > cap_h) + (dv[k] > cap_v);
  }
  // Zero total capacity has no finite normalized overflow. Keep the JSON finite;
  // capacity_valid carries the rejection instead of disguising it as a clean route.
  out.overflow = total > 0 ? over / total : 0;
  return out;
}
}  // namespace

Area_fit_route area_fit_route(const ph::Technology& tech, ph::Floorplan fp, const Area_fit_options& options) {
  const auto  start = std::chrono::steady_clock::now();
  std::string error;
  if (options.oracle == "legroute") {
    try {
      Legroute_options lopt;
      lopt.output_dir     = "";
      lopt.global_pattern = options.pattern;
      const auto plan     = legroute_plan(tech,
                                          fp,
                                          timing::Feedback{},
                                          lopt,
                                          {},
                                          ph::detect_tech_profile(tech),
                                          options.net_pops,
                                          false,
                                          nullptr,
                                          true);
      if (!plan.route_attempted) {
        throw std::runtime_error(std::format("legroute plan skipped the route ({})", plan.route_skip_name()));
      }
      Area_fit_route out;
      out.oracle          = "legroute";
      out.signals         = plan.signals;
      out.unrouted        = plan.unrouted;
      out.overflow_edges  = plan.overflow_edges;
      out.overflow_tracks = plan.overflow_tracks;
      out.overflow        = plan.signals ? double(plan.overflow_tracks) / double(plan.signals) : 0;
      out.seconds         = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      return out;
    } catch (const std::exception& e) {
      error = e.what();
      std::fprintf(stderr, "place.area: legroute oracle failed (%s); RUDY estimate instead\n", e.what());
    }
  }
  auto out    = rudy_route(tech, fp);
  out.error   = error;
  out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  return out;
}

bool area_fit_routable(const Area_fit_route& probe, const Area_fit_route* reference, const Area_fit_options& options) {
  if (!probe.capacity_valid || (reference && !reference->capacity_valid)) {
    return false;
  }
  if (probe.oracle == "rudy") {
    const double ref = reference && reference->oracle == "rudy" ? reference->overflow : 0;
    return probe.overflow <= ref * (1 + options.overflow_slack) + options.overflow_floor;
  }
  const double floor = options.overflow_floor * double(probe.signals);
  if (reference && reference->oracle != probe.oracle) {
    reference = nullptr;  // a reference measured by another oracle does not compare
  }
  const double ref_unrouted = reference ? double(reference->unrouted) : 0;
  const double ref_tracks   = reference ? double(reference->overflow_tracks) : 0;
  return double(probe.unrouted) <= ref_unrouted + floor
         && double(probe.overflow_tracks) <= ref_tracks * (1 + options.overflow_slack) + floor;
}

void validate_area_fit_final_route(const Area_fit_route& final_route, const Area_fit_route& reference,
                                   const Area_fit_options& options) {
  if (!area_fit_routable(final_route, &reference, options)) {
    throw std::runtime_error("place.area: final placement failed the frozen-reference routing acceptance check");
  }
}

std::string area_fit_route_json(const Area_fit_route& r) {
  std::string error;
  for (const char c : r.error) {
    if (c == '"' || c == '\\') {
      error += '\\';
    }
    error += (c == '\n' || c == '\t') ? ' ' : c;
  }
  return std::format(
      R"({{"oracle":"{}","error":"{}","capacity_valid":{},"signals":{},"unrouted":{},"overflow_edges":{},"overflow_tracks":{},"overflow":{},"peak":{},"seconds":{}}})",
      r.oracle,
      error,
      r.capacity_valid,
      r.signals,
      r.unrouted,
      r.overflow_edges,
      r.overflow_tracks,
      r.overflow,
      r.peak,
      r.seconds);
}

bool area_fit_applies(const Options& o) {
  return o.area_fit.enabled && !o.floorplan.empty() && o.full && !o.timing_seed && !o.retain_placement && !o.previous
         && !o.cell_stage && !o.analyze_colors_only && !o.restart_seed;
}

namespace {
struct Probe {
  double                        fraction = 1;
  ph::Rect                      region;
  double                        density = 0, cell_utilization = 0, utilization = 0;
  bool                          placed = false, routable = false;
  bool                          fp_snap_off = false;  // placed without the floorplan centroid snap (it did not fit)
  std::optional<Area_fit_route> route;
  double                        hpwl_um = 0, seconds = 0;
  std::string                   error;
};
double qor_hpwl(const std::string& qor) {
  rapidjson::Document q;
  q.Parse(qor.c_str());
  return !q.HasParseError() && q.IsObject() && q.HasMember("hpwl_um") && q["hpwl_um"].IsNumber() ? q["hpwl_um"].GetDouble() : 0;
}
std::string rect_json(const ph::Rect& r, double dbu) {
  return std::format("[{},{},{},{}]", r.xl / dbu, r.yl / dbu, r.xh / dbu, r.yh / dbu);
}
std::string escape(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += (c == '\n' || c == '\t') ? ' ' : c;
  }
  return out;
}
}  // namespace

Result place_area_fit(hhds::Graph& source, hhds::GraphLibrary& library, const Options& options,
                      const std::function<void()>& release_source) {
  validate_area_fit_options(options.area_fit);
  const auto&  af        = options.area_fit;
  const auto&  tech      = options.technology().lef;
  const auto   frame     = ph::read_def(options.floorplan);
  const auto   core      = area_fit_core(tech, frame);
  const auto   anchor    = area_fit_anchor(frame, core, af.anchor);
  const double dbu       = double(frame.dbu);
  const double core_area = (core.rect.xh - core.rect.xl) * (core.rect.yh - core.rect.yl);
  fs::create_directories(options.output_dir + "/area_fit");
  const auto start = std::chrono::steady_clock::now();

  std::vector<Probe> probes;
  Area_fit_areas     areas;
  // the options of a probe at an area fraction (1: the given core, unchanged)
  const auto         probe_options = [&](Probe& p, const std::string& dir) {
    auto o             = options;
    o.area_fit.enabled = false;
    o.cache_dir.clear();
    o.placement_session.reset();
    o.output_dir = dir;
    if (p.fraction < 1) {
      fs::create_directories(dir);
      const auto fence = area_fit_fence(frame, p.region);
      o.floorplan      = dir + "/area_fit_fence.def";
      ph::write_def(fence, o.floorplan);
      // the GP target and the legalizer's bin cap follow the region's cell utilization (as the generated-core search
      // does): a target under it could never spread the cells
      if (p.cell_utilization > 0) {
        o.density = std::max(options.density, std::min(.99, p.cell_utilization + .05));
        o.terminal_legalize_options.max_density
            = std::max(options.terminal_legalize_options.max_density, std::min(1.0, o.density + .05));
      }
    }
    if (p.fp_snap_off) {
      o.fp_snap_enabled = false;
    }
    p.density = o.density;
    return o;
  };
  const auto measure = [&](Probe& p, Result& r) {
    auto placed = r.floorplan;
    placed.rows = frame.rows;  // the wires may use the tracks of the whole core
    p.route     = area_fit_route(tech, std::move(placed), af);
    p.hpwl_um   = qor_hpwl(r.qor);
  };
  const auto set_region = [&](Probe& p) {
    p.region           = p.fraction < 1 ? area_fit_region(tech, frame, core, areas, p.fraction, anchor) : core.rect;
    const double area  = (p.region.xh - p.region.xl) * (p.region.yh - p.region.yl);
    p.fraction         = area / core_area;  // the snapped region's real fraction
    p.utilization      = (areas.cells + areas.macros) / area;
    p.cell_utilization = area > areas.macros ? areas.cells / (area - areas.macros) : 1;
  };
  const auto run_probe = [&](Probe& p) {
    const auto t0  = std::chrono::steady_clock::now();
    const auto dir = std::format("{}/area_fit/probe{}", options.output_dir, probes.size());
    std::fprintf(stderr,
                 "place.area: probe %zu: area fraction %.4f region [%.2f %.2f %.2f %.2f] um, utilization %.4f (cells %.4f)\n",
                 probes.size(),
                 p.fraction,
                 p.region.xl / dbu,
                 p.region.yl / dbu,
                 p.region.xh / dbu,
                 p.region.yh / dbu,
                 p.utilization,
                 p.cell_utilization);
    try {
      auto   o               = probe_options(p, dir);
      auto   private_library = std::make_unique<hhds::GraphLibrary>();  // outlives r (declared before it)
      Result r;
      try {
        r = place(source, *private_library, o);
      } catch (const std::runtime_error& e) {
        // the centroid snap of floorplan planning may not fit the circles of a smaller region: the probe places
        // without it (the same fallback the full core takes when it cannot snap)
        if (p.fraction >= 1 || !o.fp_snap_enabled
            || std::string_view(e.what()).find("floorplan snap could not contain centroid circles") == std::string_view::npos) {
          throw;
        }
        std::fprintf(stderr, "place.area: probe %zu: %s; placing without the floorplan snap\n", probes.size(), e.what());
        p.fp_snap_off   = true;
        o               = probe_options(p, dir + "_nosnap");
        private_library = std::make_unique<hhds::GraphLibrary>();
        r               = place(source, *private_library, o);
      }
      p.placed = true;
      measure(p, r);
      p.routable = area_fit_routable(*p.route, probes.empty() ? nullptr : &*probes.front().route, af);
      if (probes.empty()) {
        areas = area_fit_areas(tech, r.floorplan);
      }
    } catch (const std::exception& e) {
      if (probes.empty()) {
        throw;  // the given core itself does not place: the same failure as without the search
      }
      p.error = e.what();
      std::fprintf(stderr, "place.area: probe %zu failed to place: %s\n", probes.size(), e.what());
    }
    p.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr,
                 "place.area: probe %zu: %s (overflow tracks %zu, unrouted %zu, hpwl %.0f um, %.0f s)\n",
                 probes.size(),
                 p.routable ? "routable" : "not routable",
                 p.route ? p.route->overflow_tracks : 0,
                 p.route ? p.route->unrouted : 0,
                 p.hpwl_um,
                 p.seconds);
    probes.push_back(p);
    return p.routable;
  };

  // the reference: the given core (it also measures the cell and macro areas)
  Probe reference;
  reference.region = core.rect;
  run_probe(reference);
  if (!area_fit_routable(*probes.front().route, &*probes.front().route, af)) {
    throw std::runtime_error("place.area: full-core reference has no valid routing-capacity estimate");
  }
  probes.front().routable = true;  // the reference routes by definition: smaller regions compare against it
  set_region(probes.front());
  // the densest region considered: the cell-utilization ceiling, the smallest fraction asked, the largest macro
  const double lowest = std::max({af.min_fraction,
                                  (areas.cells / af.max_utilization + areas.macros) / core_area,
                                  double(areas.max_macro_w) * double(areas.max_macro_h) / core_area});
  double       hi = 1, lo = lowest;  // routable at hi; lo is a candidate until it fails
  bool         lo_failed = false;
  for (unsigned k = 0; k < af.probes && lo < 1; ++k) {
    Probe p;
    p.fraction = lo_failed ? (lo + hi) / 2 : lo;
    if (hi - p.fraction < af.tolerance / 2) {
      break;
    }
    set_region(p);
    if (p.fraction >= hi - 1e-9) {
      break;  // the snapped region is no smaller than the routable one
    }
    if (run_probe(p)) {
      hi = p.fraction;
      if (!lo_failed) {
        break;  // the densest region routes
      }
    } else {
      lo        = p.fraction;
      lo_failed = true;
    }
    if (lo_failed && hi - lo <= af.tolerance) {
      break;
    }
  }
  // the chosen region: the smallest routable probe
  size_t chosen = 0;
  for (size_t i = 1; i < probes.size(); ++i) {
    if (probes[i].routable && probes[i].fraction < probes[chosen].fraction) {
      chosen = i;
    }
  }
  auto final_probe             = probes[chosen];
  auto o                       = probe_options(final_probe, options.output_dir + "/area_fit/final");
  o.output_dir                 = options.output_dir;
  const auto t0                = std::chrono::steady_clock::now();
  auto       result            = place(source, library, o, release_source);
  auto       final_route_probe = final_probe;
  measure(final_route_probe, result);
  validate_area_fit_final_route(*final_route_probe.route, *probes.front().route, af);
  const double final_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  result.floorplan.rows      = frame.rows;  // legal in the real core: the fence was a subset of its rows

  std::string list;
  for (size_t i = 0; i < probes.size(); ++i) {
    const auto& p  = probes[i];
    list          += std::format(
        R"({}{{"fraction":{},"fp_snap_off":{},"region_um":{},"utilization":{},"cell_utilization":{},"density":{},"placed":{},"routable":{},"hpwl_um":{},"seconds":{},"route":{},"error":"{}"}})",
        i ? "," : "",
        p.fraction,
        p.fp_snap_off,
        rect_json(p.region, dbu),
        p.utilization,
        p.cell_utilization,
        p.density,
        p.placed,
        p.routable,
        p.hpwl_um,
        p.seconds,
        p.route ? area_fit_route_json(*p.route) : "null",
        escape(p.error));
  }
  const auto json = std::format(
      R"({{"mode":"auto","anchor":"{}","anchor_um":[{},{}],"oracle":"{}","core_um":{},"core_area_um2":{},"cell_area_um2":{},"macro_area_um2":{},"core_utilization":{},"lowest_fraction":{},"chosen_probe":{},"region_um":{},"region_area_um2":{},"fraction":{},"trimmed_area_um2":{},"utilization":{},"cell_utilization":{},"density":{},"probes":{},"placements":{},"route":{},"reference_route":{},"hpwl_um":{},"final_seconds":{},"seconds":{},"trace":[{}]}})",
      af.anchor,
      anchor.first / dbu,
      anchor.second / dbu,
      af.oracle,
      rect_json(core.rect, dbu),
      core_area / dbu / dbu,
      areas.cells / dbu / dbu,
      areas.macros / dbu / dbu,
      (areas.cells + areas.macros) / core_area,
      lowest,
      chosen,
      rect_json(final_probe.region, dbu),
      final_probe.fraction * core_area / dbu / dbu,
      final_probe.fraction,
      (1 - final_probe.fraction) * core_area / dbu / dbu,
      final_probe.utilization,
      final_probe.cell_utilization,
      final_probe.density,
      probes.size(),
      probes.size() + 1,
      area_fit_route_json(*final_route_probe.route),
      area_fit_route_json(*probes.front().route),
      final_route_probe.hpwl_um,
      final_seconds,
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
      list);
  std::ofstream(options.output_dir + "/area_fit.json") << json << '\n';
  rapidjson::Document qor;
  qor.Parse(result.qor.c_str());
  if (!qor.HasParseError() && qor.IsObject()) {
    rapidjson::Document fit;
    fit.Parse(json.c_str());
    auto& allocator = qor.GetAllocator();
    fit.RemoveMember("trace");
    qor.AddMember("area_fit", rapidjson::Value(fit, allocator), allocator);
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    qor.Accept(writer);
    result.qor = buffer.GetString();
    std::ofstream(options.output_dir + "/qor.json") << result.qor << '\n';
  }
  std::fprintf(stderr,
               "place.area: chose probe %zu: area fraction %.4f (%.0f of %.0f um2), utilization %.4f, %zu probes\n",
               chosen,
               final_probe.fraction,
               final_probe.fraction * core_area / dbu / dbu,
               core_area / dbu / dbu,
               final_probe.utilization,
               probes.size());
  return result;
}
}  // namespace livehd::placement
