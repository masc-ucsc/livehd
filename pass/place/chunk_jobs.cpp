// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "chunk_jobs.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <numeric>
#include <set>
#include <thread>

#include "centroid_record.hpp"
#include "chunk_demand.hpp"
#include "chunk_refine.hpp"
#include "core/binary_message.hpp"
#include "core/hash_util.hpp"
#include "core/job_manager.hpp"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"
extern char** environ;
namespace livehd::placement {
namespace fs = std::filesystem;
namespace dp = DreamPlace::native;
namespace {
using Doc                                   = rapidjson::Document;
using Value                                 = rapidjson::Value;
using Clock                                 = std::chrono::steady_clock;
constexpr char             jobs_class[]     = "livehd.placement.chunk_jobs";
constexpr char             feedback_class[] = "livehd.placement.chunk_feedback";
constexpr std::string_view magic            = "LHD-home-1\n";
constexpr std::string_view stepped_magic = "LHD-home-2\n";
Value                      str(const std::string& v, Doc& d) { return Value(v.data(), v.size(), d.GetAllocator()); }
Doc                        rec(const char* type, const std::string& design, const std::string& id) {
  Doc   d(rapidjson::kObjectType);
  auto& a = d.GetAllocator();
  d.AddMember("class", Value(type, a), a)
      .AddMember("design", str(design, d), a)
      .AddMember("id", str(id, d), a)
      .AddMember("schema_version", 1, a);
  return d;
}
template <class A>
void packet(A& a, Chunk_job& j) {
  auto& d = j.physical;
  auto& l = d.layout;
  binary_message::fields(a,
                         j.design,
                         j.id,
                         j.plan_key,
                         j.budget,
                         j.dbu,
                         j.iterations,
                         j.threads,
                         j.detailed,
                         j.multirow,
                         j.density,
                         j.box,
                         j.names,
                         j.boundary_nets,
                         j.original_nets,
                         l.xl,
                         l.yl,
                         l.xh,
                         l.yh,
                         l.site_width,
                         l.row_height,
                         d.num_movable,
                         d.node_x,
                         d.node_y,
                         d.node_size_x,
                         d.node_size_y,
                         d.node_macro,
                         d.node_halo_x,
                         d.node_halo_y,
                         d.pin2node,
                         d.pin_offset_x,
                         d.pin_offset_y,
                         d.flat_net2pin,
                         d.flat_net2pin_start,
                         d.net_weights);
}
void need(bool ok, const std::string& msg) {
  if (!ok) {
    throw std::runtime_error(msg);
  }
}
Chunk_job read_job(const fs::path& path, const std::string& expected) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  need(bool(f), "cannot open " + path.string());
  auto size = f.tellg();
  need(size >= 0, "invalid packet length");
  std::string bytes(size_t(size), '\0');
  f.seekg(0);
  f.read(bytes.data(), size);
  need(bool(f) && (bytes.starts_with(magic) || bytes.starts_with(stepped_magic)), "invalid or incompatible chunk job");
  need(std::format("{:016x}", hash_util::fnv1a64(bytes)) == expected, "chunk packet does not match the exported job");
  binary_message::Decoder decoder{std::string_view(bytes).substr(magic.size())};
  Chunk_job               j;
  packet(decoder, j);
  if (bytes.starts_with(stepped_magic)) {
    binary_message::fields(decoder, j.perimeter, j.exclusions);
  }
  if (!decoder.bytes.empty()) {
    binary_message::fields(decoder, j.timed_nets, j.wire_rc);
  }
  if (!decoder.bytes.empty()) {
    binary_message::fields(decoder, j.orientations, j.pin_dictionary, j.pin_labels);
    need(j.orientations.size() == j.names.size() && j.pin_labels.size() == j.physical.pin2node.size(),
         "invalid chunk physical identity fields");
  }
  need(decoder.bytes.empty() && j.physical.num_movable >= 0 && j.names.size() == size_t(j.physical.num_movable)
           && j.physical.node_macro.size() >= j.names.size() && j.boundary_nets.size() == j.original_nets.size() && j.dbu > 0
           && j.iterations >= 0 && j.threads > 0,
       "invalid chunk job fields");
  return j;
}
uint64_t peak_bytes(const rusage& u) {
#ifdef __APPLE__
  return u.ru_maxrss;
#else
  return uint64_t(u.ru_maxrss) * 1024;
#endif
}
void put_positions(const fs::path& path, const Chunk_job& j) {
  // Stream names and poses: no second coordinate DOM for a large home.
  std::ofstream                                file(path);
  rapidjson::OStreamWrapper                    os(file);
  rapidjson::Writer<rapidjson::OStreamWrapper> w(os);
  w.StartArray();
  w.StartObject();
  w.Key("class");
  w.String("livehd.placement.chunk_positions");
  w.Key("schema_version");
  w.Int(1);
  w.Key("design");
  w.String(j.design.c_str());
  w.Key("id");
  w.String(j.id.c_str());
  w.Key("plan_key");
  w.Uint64(j.plan_key);
  w.Key("units");
  w.String("design_dbu");
  w.Key("nodes");
  w.StartArray();
  for (int i = 0; i < j.physical.num_movable; ++i) {
    w.StartArray();
    w.String(j.names[i].c_str());
    w.Double(j.box[0] + j.physical.node_x[i]);
    w.Double(j.box[1] + j.physical.node_y[i]);
    w.EndArray();
  }
  w.EndArray();
  w.Key("sizes_dbu");
  w.StartArray();
  for (int i = 0; i < j.physical.num_movable; ++i) {
    w.StartArray();
    w.Double(j.physical.node_size_x[i]);
    w.Double(j.physical.node_size_y[i]);
    w.EndArray();
  }
  w.EndArray();
  if (!j.orientations.empty()) {
    w.Key("macro_orientations");
    w.StartObject();
    for (int i = 0; i < j.physical.num_movable; ++i) {
      if (j.physical.node_macro[i]) {
        w.Key(j.names[i].c_str());
        w.String(j.orientations[i].c_str());
      }
    }
    w.EndObject();
  }
  w.Key("macro_sizes_dbu");
  w.StartArray();
  for (int i = 0; i < j.physical.num_movable; ++i) {
    if (j.physical.node_macro[i]) {
      w.StartArray();
      w.Int(i);
      w.Double(j.physical.node_size_x[i]);
      w.Double(j.physical.node_size_y[i]);
      w.EndArray();
    }
  }
  w.EndArray();
  w.EndObject();
  w.EndArray();
  file.close();
  need(bool(file), "cannot write chunk positions");
}
// Distributed-wire Elmore estimate (ohm * fF -> 0.001 ps). Calibration carries
// the STA cell, load and exterior delays at export; only local wire spans vary.
std::array<double, 2> pin_span(const Chunk_job& j, int net, const double* xs, const double* ys) {
  const auto& d  = j.physical;
  double      xl = INFINITY, yl = INFINITY, xh = -INFINITY, yh = -INFINITY;
  for (int k = d.flat_net2pin_start.at(net); k < d.flat_net2pin_start.at(net + 1); ++k) {
    const auto p = d.flat_net2pin.at(k), i = d.pin2node.at(p);
    double     x = xs[i] + d.pin_offset_x.at(p), y = ys[i] + d.pin_offset_y.at(p);
    xl = std::min(xl, x);
    xh = std::max(xh, x);
    yl = std::min(yl, y);
    yh = std::max(yh, y);
  }
  return {xh - xl, yh - yl};
}
double wire_delay_ps(const Chunk_job& j, int net, const double* xs, const double* ys) {
  const auto   span = pin_span(j, net, xs, ys);
  const double h = span[0] / j.dbu, v = span[1] / j.dbu;
  const auto [rh, ch, rv, cv] = j.wire_rc;
  return .0005 * (rh * h + rv * v) * (ch * h + cv * v);
}
void report_timing(Doc& report, const Chunk_job& job, const double* xs, const double* ys) {
  auto& a = report.GetAllocator();
  if (!job.timed_nets.empty()) {
    double worst = INFINITY;
    for (const auto& [net, slack, seed_delay] : job.timed_nets) {
      worst = std::min(worst, slack + seed_delay - wire_delay_ps(job, net, xs, ys));
    }
    report.AddMember("estimated_wns_ps", std::min(0., worst), a)
        .AddMember("estimated_worst_slack_ps", worst, a)
        .AddMember("timed_nets", uint64_t(job.timed_nets.size()), a);
  }
}
// Each physical net is either internal to one worker, shared, or entirely fixed.
// The worker never scores the artificial terminal added to its local solve.
struct Pin_bounds {
  double xl = INFINITY, yl = INFINITY, xh = -INFINITY, yh = -INFINITY;
  void   add(double x, double y) {
    xl = std::min(xl, x);
    yl = std::min(yl, y);
    xh = std::max(xh, x);
    yh = std::max(yh, y);
  }
  double span() const { return xl <= xh ? xh - xl + yh - yl : 0; }
  Value  json(Doc& doc) const {
    Value box(rapidjson::kArrayType);
    if (xl <= xh) {
      for (double v : {xl, yl, xh, yh}) {
        box.PushBack(v, doc.GetAllocator());
      }
    }
    return box;
  }
};

Value chunk_score(const Chunk_job& job, const std::map<int, uint64_t>& shared, Doc& doc, const double* xs = nullptr,
                  const double* ys = nullptr) {
  const auto& d = job.physical;
  auto&       a = doc.GetAllocator();
  Value       score(rapidjson::kObjectType), bounds(rapidjson::kArrayType);
  double      internal = 0, worst = INFINITY;
  if (!xs) {
    xs = d.node_x.data();
  }
  if (!ys) {
    ys = d.node_y.data();
  }
  for (size_t n = 0; n < d.net_weights.size(); ++n) {
    Pin_bounds box;
    for (int k = d.flat_net2pin_start[n]; k < d.flat_net2pin_start[n + 1]; ++k) {
      int p = d.flat_net2pin[k], i = d.pin2node[p];
      if (size_t(i) < job.names.size()) {
        box.add(job.box[0] + xs[i] + d.pin_offset_x[p], job.box[1] + ys[i] + d.pin_offset_y[p]);
      }
    }
    auto it = shared.find(n);
    if (it == shared.end()) {
      internal += box.span();
    } else if (box.xl <= box.xh) {
      Value entry(rapidjson::kArrayType);
      entry.PushBack(it->second, a).PushBack(box.json(doc), a);
      bounds.PushBack(entry, a);
    }
  }
  for (const auto& [n, slack, seed] : job.timed_nets) {
    if (!shared.contains(n)) {
      worst = std::min(worst, slack + seed - wire_delay_ps(job, n, xs, ys));
    }
  }
  score.AddMember("internal_dbu", internal, a).AddMember("bounds", bounds, a);
  if (std::isfinite(worst)) {
    score.AddMember("internal_slack_ps", worst, a);
  }
  return score;
}

// A bounded view for the browser. The full position list stays on disk even for a very large home.
void write_chunk_preview(const fs::path& path, const Chunk_job& job, int macros, const dp::InteractView* frame = nullptr,
                         const std::map<int, uint64_t>* shared = nullptr) {
  auto        view  = rec("livehd.placement.chunk_preview", job.design, job.id);
  auto&       a     = view.GetAllocator();
  const auto& d     = job.physical;
  const int   owned = job.names.size();
  const auto* xs    = frame ? frame->x : d.node_x.data();
  const auto* ys    = frame ? frame->y : d.node_y.data();
  if (frame) {
    double wl = 0;
    for (size_t n = 0; n < d.net_weights.size(); ++n) {
      if (!d.net_mask.empty() && !d.net_mask[n]) {
        continue;
      }
      const auto [dx, dy]  = pin_span(job, n, xs, ys);
      wl                  += d.net_weights[n] * (dx + dy);
    }
    view.AddMember("iterations", frame->iteration, a).AddMember("hpwl_dbu", wl, a);
    report_timing(view, job, xs, ys);
    if (shared) {
      view.AddMember("score", chunk_score(job, *shared, view, xs, ys), a);
    }
  }
  const std::array   count{owned - macros, macros};
  std::array<int, 2> seen{};
  Value              boxes(rapidjson::kArrayType), names(rapidjson::kArrayType);
  for (int i = 0; i < owned; ++i) {
    const int kind   = d.node_macro[i] != 0;
    const int stride = 1 + (count[kind] - 1) / 1024;
    if (seen[kind]++ % stride == 0) {
      Value box(rapidjson::kArrayType);
      box.PushBack(xs[i] + job.box[0], a)
          .PushBack(ys[i] + job.box[1], a)
          .PushBack(d.node_size_x[i], a)
          .PushBack(d.node_size_y[i], a)
          .PushBack(kind, a);
      boxes.PushBack(box, a);
      names.PushBack(str(job.names.at(i), view), a);
    }
  }
  constexpr int                                   bins = 16;
  const double                                    bw = (job.box[2] - job.box[0]) / bins, bh = (job.box[3] - job.box[1]) / bins;
  std::array<double, bins * bins>                 occupied{}, cell_area{};
  std::array<std::vector<Chunk_box>, bins * bins> obstacles;
  for (size_t i = 0; i < d.node_size_x.size(); ++i) {
    if (d.node_size_x[i] <= 0 || d.node_size_y[i] <= 0) {
      continue;
    }
    double xl = xs[i], yl = ys[i], xh = xl + d.node_size_x[i], yh = yl + d.node_size_y[i];
    for (int y = std::max(0, int(yl / bh)); y < bins && y * bh < yh; ++y) {
      for (int x = std::max(0, int(xl / bw)); x < bins && x * bw < xh; ++x) {
        Chunk_box    r{std::max(xl, x * bw), std::max(yl, y * bh), std::min(xh, (x + 1) * bw), std::min(yh, (y + 1) * bh)};
        const int    index = y * bins + x;
        const double area  = (r[2] - r[0]) * (r[3] - r[1]);
        if (i < size_t(owned)) {
          occupied[index] += area / (bw * bh);
        }
        if (i >= size_t(owned) || d.node_macro[i]) {
          obstacles[index].push_back(r);
        } else {
          cell_area[index] += area;
        }
      }
    }
  }
  Value  utilization(rapidjson::kArrayType);
  double peak = 0, overflow = 0, total = 0;
  for (size_t i = 0; i < cell_area.size(); ++i) {
    // Union overlapping blockages (including projected exterior rectangles).
    std::vector<double> cuts;
    for (const auto& r : obstacles[i]) {
      cuts.push_back(r[0]);
      cuts.push_back(r[2]);
    }
    std::sort(cuts.begin(), cuts.end());
    double blocked = 0;
    for (size_t j = 1; j < cuts.size(); ++j) {
      if (cuts[j] == cuts[j - 1]) {
        continue;
      }
      std::vector<std::pair<double, double>> intervals;
      for (const auto& r : obstacles[i]) {
        if (r[0] < cuts[j] && r[2] > cuts[j - 1]) {
          intervals.emplace_back(r[1], r[3]);
        }
      }
      std::sort(intervals.begin(), intervals.end());
      double end = -INFINITY, length = 0;
      for (auto [lo, hi] : intervals) {
        length += std::max(0., hi - std::max(lo, end));
        end     = std::max(end, hi);
      }
      blocked += (cuts[j] - cuts[j - 1]) * length;
    }
    double available = std::max(0., bw * bh - blocked);
    if (available > 1.e-9 * bw * bh) {
      const double ratio = cell_area[i] / available;
      utilization.PushBack(ratio, a);
      peak = std::max(peak, ratio);
    } else {
      utilization.PushBack(Value(), a);
    }
    overflow += std::max(0., cell_area[i] - job.density * available);
    total    += cell_area[i];
  }
  view.AddMember("cell_density", utilization, a)
      .AddMember("peak_density", peak, a)
      .AddMember("target_density", job.density, a)
      .AddMember("overflow_fraction", total ? overflow / total : 0., a);
  Value grid(rapidjson::kArrayType), halos(rapidjson::kObjectType);
  grid.PushBack(d.layout.site_width, a).PushBack(d.layout.row_height, a);
  for (int i = 0; i < owned; ++i) {
    if (d.node_macro[i]) {
      Value halo(rapidjson::kArrayType);
      halo.PushBack(d.node_halo_x.empty() ? 0 : d.node_halo_x[i], a).PushBack(d.node_halo_y.empty() ? 0 : d.node_halo_y[i], a);
      halos.AddMember(str(job.names.at(i), view), halo, a);
    }
  }
  view.AddMember("grid_dbu", grid, a).AddMember("macro_halos_dbu", halos, a);
  Value density(rapidjson::kArrayType);
  for (double share : occupied) {
    density.PushBack(share, a);
  }
  view.AddMember("occupancy", density, a).AddMember("bins", bins, a);
  view.AddMember("names", names, a).AddMember("boxes_dbu", boxes, a).AddMember("owned", owned, a);
  metadata::Bundle bundle;
  bundle.replace(view);
  bundle.write(path.string());
}
void boundary_feedback(Doc& f, const Chunk_job& j) {
  auto& a = f.GetAllocator();
  Value ports(rapidjson::kArrayType);
  auto& d = j.physical;
  for (size_t k = 0; k < j.boundary_nets.size(); ++k) {
    int n = j.boundary_nets[k];
    need(n >= 0 && size_t(n + 1) < d.flat_net2pin_start.size(), "invalid local boundary net");
    auto first = d.flat_net2pin_start[n], last = d.flat_net2pin_start[n + 1] - 1;
    need(last > first, "missing local boundary pins");
    int    t = d.pin2node[d.flat_net2pin[last]];
    double x = d.node_x[t], y = d.node_y[t], sumx = 0, sumy = 0;
    for (int at = first; at < last; ++at) {
      int pin = d.flat_net2pin[at], i = d.pin2node[pin];
      sumx += d.node_x[i] + d.pin_offset_x[pin] - x;
      sumy += d.node_y[i] + d.pin_offset_y[pin] - y;
    }
    Value p(rapidjson::kObjectType), contact(rapidjson::kArrayType), offset(rapidjson::kArrayType);
    contact.PushBack(x + j.box[0], a).PushBack(y + j.box[1], a);
    offset.PushBack(sumx / (last - first), a).PushBack(sumy / (last - first), a);
    p.AddMember("net_index", j.original_nets[k], a)
        .AddMember("contact_dbu", contact, a)
        .AddMember("local_pins", last - first, a)
        .AddMember("mean_pin_offset_dbu", offset, a);
    ports.PushBack(p, a);
  }
  f.AddMember("boundary", ports, a);
}
}  // namespace
static std::string write_chunk_job(const fs::path& path, Chunk_job& j) {
  binary_message::Encoder en;
  en.bytes = j.perimeter.empty() ? magic : stepped_magic;
  packet(en, j);
  if (!j.perimeter.empty()) {
    binary_message::fields(en, j.perimeter, j.exclusions);
  }
  if (!j.timed_nets.empty() || !j.orientations.empty()) {
    binary_message::fields(en, j.timed_nets, j.wire_rc);
    if (!j.orientations.empty()) {
      binary_message::fields(en, j.orientations, j.pin_dictionary, j.pin_labels);
    }
  }
  std::ofstream f(path, std::ios::binary);
  f.write(en.bytes.data(), en.bytes.size());
  f.close();
  need(bool(f), "cannot write chunk job " + path.string());
  return std::format("{:016x}", hash_util::fnv1a64(en.bytes));
}
std::string write_chunk_jobs(const fs::path& manifest, const metadata::Bundle& base, const Chunk_model& model,
                             const Chunk_plan& plan, dp::Design& physical, std::span<const int> owners,
                             std::span<const std::string> names, const std::string& design, int dbu, int iterations, int threads,
                             bool detailed, bool strict, bool adjust_contacts, const std::string& selected,
                             const Chunk_predictor* predictor, std::span<const double> slack_ps, std::array<double, 4> wire_rc,
                             std::span<const std::array<double, 2>> windows, const std::function<std::string(int)>& orientation,
                             const std::function<std::string_view(int)>& pin_name) {
  std::optional<size_t> only;
  if (!selected.empty()) {
    auto it = std::find_if(plan.chunks.begin(), plan.chunks.end(), [&](const auto& c) { return c.id == selected; });
    need(it != plan.chunks.end(), "unknown physical home for chunk.only: " + selected);
    only = it - plan.chunks.begin();
  }
  fs::create_directories(manifest.parent_path());
  auto tmp = (manifest.parent_path() / "homes-XXXXXX").string();
  need(mkdtemp(tmp.data()) != nullptr, "cannot create chunk input directory");
  auto        d = rec(jobs_class, design, "fp_plan");
  auto&       a = d.GetAllocator();
  Value       rows(rapidjson::kArrayType);
  const auto& current = *base.select(chunk_plan_class, design).at(0);
  d.AddMember("plan_revision", str(std::format("{:016x}", hash_util::fnv1a64(metadata::canonical(current))), d), a)
      .AddMember("memory_context", str(predictor ? predictor->context : plan.predictor.context, d), a)
      .AddMember("plan_key", plan.input_key, a)
      .AddMember("budget_bytes", plan.budget, a)
      .AddMember("require_fit", strict, a)
      .AddMember("adjust_contacts", adjust_contacts, a)
      .AddMember("dbu_per_um", dbu, a);
  if (only) {
    d.AddMember("selected", str(selected, d), a);
  }
  const fs::path packet_dir = manifest.parent_path() / "chunk-packets";
  if (model.pack_scopes) {
    fs::create_directories(packet_dir);
  }
  std::set<size_t> shared_nets;
  Value  assembly(rapidjson::kObjectType), fixed(rapidjson::kArrayType), timing(rapidjson::kArrayType), rc(rapidjson::kArrayType);
  double fixed_hpwl = 0, fixed_slack = INFINITY, seed_hpwl = 0;
  for (size_t n = 0; n + 1 < physical.flat_net2pin_start.size(); ++n) {
    if (!physical.net_mask.empty() && !physical.net_mask[n]) {
      continue;
    }
    int        first_home = -1;
    bool       crossing   = false;
    Pin_bounds anchors, all;
    for (int k = physical.flat_net2pin_start[n]; k < physical.flat_net2pin_start[n + 1]; ++k) {
      int    p = physical.flat_net2pin[k], i = physical.pin2node[p];
      double x = physical.node_x[i] + physical.pin_offset_x[p], y = physical.node_y[i] + physical.pin_offset_y[p];
      all.add(x, y);
      if (size_t(i) >= owners.size() || owners[i] < 0 || (i >= physical.num_movable && !model.objects[owners[i]].worker_memory)) {
        anchors.add(x, y);
      } else {
        int h       = plan.home[owners[i]];
        crossing   |= first_home >= 0 && first_home != h;
        first_home  = h;
      }
    }
    seed_hpwl          += all.span();
    const double slack  = n < slack_ps.size() ? slack_ps[n] : INFINITY;
    if (first_home < 0) {
      fixed_hpwl  += anchors.span();
      fixed_slack  = std::min(slack, fixed_slack);
    } else if (crossing || anchors.xl <= anchors.xh) {
      shared_nets.insert(n);
      Value item(rapidjson::kArrayType);
      if (anchors.xl <= anchors.xh) {
        fixed.PushBack(item.PushBack(uint64_t(n), a).PushBack(anchors.json(d), a), a);
      }
      if (std::isfinite(slack)) {
        Value sample(rapidjson::kArrayType);
        sample.PushBack(uint64_t(n), a).PushBack(slack, a).PushBack(all.json(d), a);
        if (n < windows.size() && std::isfinite(windows[n][0]) && std::isfinite(windows[n][1])) {
          sample.PushBack(windows[n][0], a).PushBack(windows[n][1], a);
        }
        timing.PushBack(sample, a);
      }
    }
  }
  for (double v : wire_rc) {
    rc.PushBack(v, a);
  }
  assembly.AddMember("fixed_dbu", fixed_hpwl, a)
      .AddMember("anchors", fixed, a)
      .AddMember("timing", timing, a)
      .AddMember("wire_rc", rc, a);
  if (std::isfinite(fixed_slack)) {
    assembly.AddMember("fixed_slack_ps", fixed_slack, a);
  }
  assembly.AddMember("seed_hpwl_dbu", seed_hpwl, a);
  d.AddMember("assembly", assembly, a);
  refine_chunk_seeds(
      physical,
      model,
      plan,
      owners,
      names,
      iterations,
      threads,
      detailed,
      [&](size_t index, Chunk_job& j) {
        auto& c      = plan.chunks[index];
        j.id         = c.id;
        j.design     = design;
        // The managed scheduler checks the complete packet digest. A plan-wide key in every
        // packet would force all sibling handoffs to expire after any source edit.
        j.plan_key   = model.pack_scopes ? 0 : plan.input_key;
        j.budget     = plan.budget;
        j.dbu        = dbu;
        j.box        = c.worker_box();
        j.perimeter  = c.perimeter;
        j.exclusions = c.exclusions;
        j.density    = c.density;
        j.iterations = iterations;
        j.threads    = std::max(1, threads);
        j.detailed   = detailed;
        j.wire_rc    = wire_rc;
        for (size_t i = 0; i < j.parent_nets.size() && !slack_ps.empty(); ++i) {
          const auto n = j.parent_nets[i];
          if (n < slack_ps.size() && std::isfinite(slack_ps[n])) {
            j.timed_nets.emplace_back(i, slack_ps[n], wire_delay_ps(j, i, j.physical.node_x.data(), j.physical.node_y.data()));
          }
        }
        if (orientation && pin_name) {
          for (int i : j.source_nodes) {
            j.orientations.push_back(orientation(i));
          }
          std::map<std::string_view, int> labels;
          for (int i : j.source_pins) {
            if (i < 0) {
              j.pin_labels.push_back(-1);
              continue;
            }
            auto [it, added] = labels.try_emplace(pin_name(i), labels.size());
            if (added) {
              j.pin_dictionary.emplace_back(it->first);
            }
            j.pin_labels.push_back(it->second);
          }
        }
        auto     name   = std::to_string(index) + ".bin";
        auto     digest = write_chunk_job(fs::path(tmp) / name, j);
        fs::path input  = fs::path(tmp).filename() / name;
        if (model.pack_scopes) {
          const fs::path target = packet_dir / (digest + ".bin");
          fs::rename(fs::path(tmp) / name, target);
          input = target.lexically_relative(manifest.parent_path());
        }
        Memory_counts counts{};
        for (int obj : c.objects) {
          const auto& o            = model.objects[obj];
          counts[o.macro ? 1 : 0] += o.cells;
          counts[o.macro ? 3 : 2] += o.pins + o.nets;
        }
        Value features(rapidjson::kArrayType);
        for (auto n : counts) {
          features.PushBack(n, a);
        }
        Value row(rapidjson::kObjectType), box(rapidjson::kArrayType);
        for (double v : j.box) {
          box.PushBack(v, a);
        }
        std::map<int, uint64_t> shared;
        Value                   score_nets(rapidjson::kArrayType);
        for (size_t n = 0; n < j.parent_nets.size(); ++n) {
          if (shared_nets.contains(j.parent_nets[n])) {
            shared.emplace(n, j.parent_nets[n]);
            Value pair(rapidjson::kArrayType);
            score_nets.PushBack(pair.PushBack(int(n), a).PushBack(uint64_t(j.parent_nets[n]), a), a);
          }
        }
        row.AddMember("score_nets", score_nets, a).AddMember("seed_score", chunk_score(j, shared, d), a);
        row.AddMember("counts", features, a)
            .AddMember("checksum", str(digest, d), a)
            .AddMember("id", str(c.id, d), a)
            .AddMember("partition", str(c.partition, d), a)
            .AddMember("implementation", str(c.implementation, d), a)
            .AddMember("estimated_bytes", predictor && predictor->samples ? predictor->peak(counts) : c.bytes, a)
            .AddMember("box_dbu", box, a)
            .AddMember("input", str(input.string(), d), a);
        row.AddMember("packet_plan_key", j.plan_key, a);
        rows.PushBack(row, a);
      },
      only);
  if (model.pack_scopes) {
    fs::remove(tmp);  // now empty: all packets have content-addressed names
  }
  d.AddMember("jobs", rows, a);
  metadata::Bundle out(base.json());
  out.erase_selected(jobs_class, design);
  out.erase_selected(feedback_class, design);
  out.replace(d);
  out.write(manifest.string());
  return manifest.string();
}
// DREAMPlace models fixed objects as the suffix of the node arrays. Keep pin identity
// while moving hard macros behind the cells for the final solve. Names retain the
// same permutation so the result includes every owned instance, including fixed macros.
static void freeze_chunk_macros(Chunk_job& job) {
  auto&            d = job.physical;
  std::vector<int> mapped(d.node_x.size());
  std::iota(mapped.begin(), mapped.end(), 0);
  int cells = 0;
  for (int i = 0; i < d.num_movable; ++i) {
    if (d.node_macro[i]) {
      continue;
    }
    if (cells != i) {
      for (auto* field : {&d.node_x, &d.node_y, &d.node_size_x, &d.node_size_y, &d.node_halo_x, &d.node_halo_y}) {
        if (!field->empty()) {
          std::swap((*field)[i], (*field)[cells]);
        }
      }
      std::swap(d.node_macro[i], d.node_macro[cells]);
      std::swap(job.names[i], job.names[cells]);
      if (!job.orientations.empty()) {
        std::swap(job.orientations[i], job.orientations[cells]);
      }
      // mapped holds new -> old until inverted below.
      std::swap(mapped[i], mapped[cells]);
    }
    ++cells;
  }
  std::vector<int> inverse(mapped.size());
  for (size_t i = 0; i < mapped.size(); ++i) {
    inverse[mapped[i]] = i;
  }
  for (auto& node : d.pin2node) {
    node = inverse.at(node);
  }
  d.num_movable = cells;
}

// Each low-fanout incident net supplies an interval of equally good macro origins on each axis.
static Value macro_suggestions(const Chunk_job& job, Doc& doc) {
  const auto&                           d = job.physical;
  std::map<int, std::vector<Chunk_box>> intervals;
  for (size_t n = 0; n < d.net_weights.size(); ++n) {
    int begin = d.flat_net2pin_start[n], end = d.flat_net2pin_start[n + 1];
    if (end - begin < 2 || end - begin > 16 || (!d.net_mask.empty() && !d.net_mask[n])) {
      continue;
    }
    for (int j = begin; j < end; ++j) {
      int pin = d.flat_net2pin[j], node = d.pin2node[pin];
      if (node >= d.num_movable || !d.node_macro[node]) {
        continue;
      }
      Chunk_box b = {INFINITY, INFINITY, -INFINITY, -INFINITY};
      for (int k = begin; k < end; ++k) {
        int other = d.flat_net2pin[k], peer = d.pin2node[other];
        if (peer == node) {
          continue;
        }
        double x = d.node_x[peer] + d.pin_offset_x[other] - d.pin_offset_x[pin],
               y = d.node_y[peer] + d.pin_offset_y[other] - d.pin_offset_y[pin];
        b        = {std::min(b[0], x), std::min(b[1], y), std::max(b[2], x), std::max(b[3], y)};
      }
      if (std::isfinite(b[0])) {
        intervals[node].push_back(b);
      }
    }
  }
  std::vector<std::tuple<double, std::string, double, double>> ranked;
  for (const auto& [node, boxes] : intervals) {
    std::array<double, 2> current = {d.node_x[node], d.node_y[node]}, target{};
    double                gain    = 0;
    for (int axis = 0; axis < 2; ++axis) {
      std::vector<double> ends;
      for (const auto& b : boxes) {
        ends.push_back(b[axis]);
        ends.push_back(b[axis + 2]);
      }
      auto mid = ends.begin() + ends.size() / 2;
      std::nth_element(ends.begin(), mid, ends.end());
      double      median = (*mid + *std::max_element(ends.begin(), mid)) * .5;
      const auto& halos  = axis ? d.node_halo_y : d.node_halo_x;
      double      halo = halos.empty() ? 0 : halos[node], extent = axis ? d.node_size_y[node] : d.node_size_x[node];
      double      hi = job.box[axis + 2] - job.box[axis] - extent - halo;
      target[axis]   = hi < halo ? current[axis] : std::clamp((current[axis] + median) * .5, halo, hi);
      for (const auto& b : boxes) {
        auto distance  = [&](double x) { return std::max({0., b[axis] - x, x - b[axis + 2]}); };
        gain          += distance(current[axis]) - distance(target[axis]);
      }
    }
    if (gain > 0) {
      ranked.emplace_back(-gain, job.names[node], job.box[0] + target[0], job.box[1] + target[1]);
    }
  }
  size_t count = std::min<size_t>(32, ranked.size());
  std::partial_sort(ranked.begin(), ranked.begin() + count, ranked.end());
  Value result(rapidjson::kArrayType);
  for (size_t i = 0; i < count; ++i) {
    const auto& [cost, name, x, y] = ranked[i];
    auto& a                        = doc.GetAllocator();
    Value item(rapidjson::kObjectType);
    item.AddMember("name", str(name, doc), a).AddMember("x", x, a).AddMember("y", y, a).AddMember("gain_dbu", -cost, a);
    result.PushBack(item, a);
  }
  return result;
}

static void report_demand(Doc& report, const Chunk_job& job, double pitch, Chunk_demand::Pair rates,
                          const std::map<std::string, Chunk_demand::Pair>& blocked) {
  const auto&  d = job.physical;
  Chunk_demand map(job.box, 32 * pitch, rates);
  for (int i = 0; i < d.num_movable; ++i) {
    auto it = blocked.find(job.names[i]);
    if (it != blocked.end()) {
      double x = job.box[0] + d.node_x[i], y = job.box[1] + d.node_y[i];
      map.reserve({x, y, x + d.node_size_x[i], y + d.node_size_y[i]}, it->second);
    }
  }
  std::set<int> cut(job.boundary_nets.begin(), job.boundary_nets.end());
  for (int n = 0; n < int(d.net_weights.size()); ++n) {
    if (cut.contains(n) || (!d.net_mask.empty() && !d.net_mask[n])) {
      continue;
    }
    auto begin = d.flat_net2pin_start[n], end = d.flat_net2pin_start[n + 1];
    if (end - begin < 2) {
      continue;
    }
    Chunk_box b = {INFINITY, INFINITY, -INFINITY, -INFINITY};
    for (int j = begin; j < end; ++j) {
      auto   pin = d.flat_net2pin[j], i = d.pin2node[pin];
      double x = job.box[0] + d.node_x[i] + d.pin_offset_x[pin], y = job.box[1] + d.node_y[i] + d.pin_offset_y[pin];
      b = {std::min(b[0], x), std::min(b[1], y), std::max(b[2], x), std::max(b[3], y)};
    }
    map.internal(b);
  }
  for (auto n : job.boundary_nets) {
    std::vector<Chunk_demand::Pair> pts;
    for (int j = d.flat_net2pin_start[n]; j < d.flat_net2pin_start[n + 1]; ++j) {
      auto pin = d.flat_net2pin[j], i = d.pin2node[pin];
      pts.push_back({job.box[0] + d.node_x[i] + d.pin_offset_x[pin], job.box[1] + d.node_y[i] + d.pin_offset_y[pin]});
    }
    map.route(std::move(pts));
  }
  report.AddMember("coarse_routing", map.record(report, 32), report.GetAllocator());
}

int place_chunk_worker(const fs::path& input, const fs::path& output, const std::string& checksum) {
  auto        start    = Clock::now();
  auto        baseline = process_rss(getpid());
  auto        job      = read_job(input, checksum);
  auto&       d        = job.physical;
  auto        report   = rec(feedback_class, job.design, job.id);
  auto&       a        = report.GetAllocator();
  std::string status   = "failed", reason;
  const auto  macros   = std::count(d.node_macro.begin(), d.node_macro.begin() + d.num_movable, 1);
  report.AddMember("start_bytes", baseline, a)
      .AddMember("movable_macros", uint64_t(macros), a)
      .AddMember("detailed_place", job.detailed, a)
      .AddMember("target_density", job.density, a)
      .AddMember("plan_key", job.plan_key, a)
      .AddMember("movable_nodes", d.num_movable, a)
      .AddMember("local_nets", uint64_t(d.net_weights.size()), a)
      .AddMember("boundary_contacts", uint64_t(job.boundary_nets.size()), a)
      .AddMember("dbu_per_um", job.dbu, a);
  try {
    std::optional<std::map<int, uint64_t>>              score_nets;
    std::string                                         stage;
    bool                                                live_frames = false;
    int                                                 trial_steps = -1;
    std::map<int, std::array<std::optional<double>, 2>> holds;
    bool                                                freeze_macros = true;  // Old final requests always carry a macro handoff.
    struct Slide {
      int    axis;
      double lo, hi;
    };
    double                                    pitch = 0;
    Chunk_demand::Pair                        rates{};
    std::map<std::string, Chunk_demand::Pair> blocked;
    std::map<uint64_t, Slide> slides;
    Centroid_set centroids;
    if (fs::exists(output / "request.json")) {
      auto request = metadata::Bundle::read((output / "request.json").string());
      auto records = request.select("livehd.placement.chunk_request", job.design);
      need(records.size() == 1, "missing chunk stage request");
      const auto& r = *records.front();
      need(
          r.HasMember("id") && r["id"].IsString() && job.id == r["id"].GetString() && r.HasMember("stage") && r["stage"].IsString(),
          "invalid chunk stage request");
      stage = r["stage"].GetString();
      need(stage == "macro" || stage == "final", "unknown chunk stage");
      report.AddMember("stage", str(stage, report), a);
      if (r.HasMember("centroids")) {
        need(r["centroids"].IsArray(), "centroids must be an array");
        for (const auto& record : r["centroids"].GetArray()) {
          auto centroid = parse_centroid(record);
          if (centroid.position == Centroid_position_mode::at) {
            centroid.x_um -= double(job.box[0]) / job.dbu;
            centroid.y_um -= double(job.box[1]) / job.dbu;
          }
          std::string issue;
          if (!centroids.add(std::move(centroid), &issue)) {
            throw std::invalid_argument(issue);
          }
        }
        const auto issues = centroids.validate();
        if (!issues.empty()) {
          throw std::invalid_argument(issues.front().id + ": " + issues.front().reason);
        }
      }
      if (r.HasMember("routing")) {
        const auto& spec = r["routing"];
        need(spec.IsObject() && spec.HasMember("pitch") && spec["pitch"].IsNumber() && spec.HasMember("rates")
                 && spec["rates"].IsArray() && spec["rates"].Size() == 2 && spec.HasMember("blockage")
                 && spec["blockage"].IsObject(),
             "invalid routing grid specification");
        pitch = spec["pitch"].GetDouble();
        for (int k = 0; k < 2; ++k) {
          need(spec["rates"][k].IsNumber() && spec["rates"][k].GetDouble() >= 0, "invalid track density");
          rates[k] = spec["rates"][k].GetDouble();
        }
        need(std::isfinite(pitch) && pitch > 0, "invalid routing pitch");
        for (auto it = spec["blockage"].MemberBegin(); it != spec["blockage"].MemberEnd(); ++it) {
          const auto& v = it->value;
          need(v.IsArray() && v.Size() == 2 && v[0].IsNumber() && v[1].IsNumber(), "invalid blockage");
          blocked[it->name.GetString()] = {v[0].GetDouble(), v[1].GetDouble()};
        }
      }
      if (r.HasMember("score_nets")) {
        need(r["score_nets"].IsArray(), "invalid score net mapping");
        score_nets.emplace();
        for (const auto& pair : r["score_nets"].GetArray()) {
          need(pair.IsArray() && pair.Size() == 2 && pair[0].IsUint() && pair[0].GetUint() < d.net_weights.size()
                   && pair[1].IsUint64(),
               "invalid score net");
          score_nets->emplace(pair[0].GetInt(), pair[1].GetUint64());
        }
      }
      if (r.HasMember("density")) {
        need(r["density"].IsNumber() && r["density"].GetDouble() > 0 && r["density"].GetDouble() <= 1, "density must be in (0, 1]");
        job.density = r["density"].GetDouble();
        report["target_density"].SetDouble(job.density);
      }
      if (r.HasMember("freeze_macros")) {
        need(r["freeze_macros"].IsBool(), "invalid macro handoff flag");
        freeze_macros = r["freeze_macros"].GetBool();
      }
      live_frames = r.HasMember("live_frames") && r["live_frames"].IsBool() && r["live_frames"].GetBool();
      if (r.HasMember("iterations")) {
        need(r["iterations"].IsInt() && r["iterations"].GetInt() >= 0, "trial iterations must be a nonnegative integer");
        trial_steps = r["iterations"].GetInt();
      }
      if (r.HasMember("poses")) {
        need(r["poses"].IsArray(), "poses must be an array");
        std::map<std::string, int> ids;
        for (int i = 0; i < d.num_movable; ++i) {
          ids.emplace(job.names[i], i);
        }
        for (const auto& pose : r["poses"].GetArray()) {
          need(pose.IsArray() && pose.Size() == 5 && pose[0].IsString(), "invalid physical pose");
          auto entry = ids.find(pose[0].GetString());
          need(entry != ids.end(), "pose requires an owned movable object");
          int i = entry->second;
          for (int k = 0; k < 2; ++k) {
            if (pose[k + 1].IsNull()) {
              continue;
            }
            need(pose[k + 1].IsNumber() && pose[k + 3].IsBool(), "invalid axis coordinate or hold");
            double x      = pose[k + 1].GetDouble() - job.box[k];
            double extent = k ? d.node_size_y[i] : d.node_size_x[i];
            need(x >= 0 && x + extent <= job.box[k + 2] - job.box[k], "requested pose exceeds home");
            (k ? d.node_y : d.node_x)[i] = x;
            if (pose[k + 3].GetBool()) {
              holds[i][k] = x;
            }
          }
        }
      }
      if (r.HasMember("shared_edges")) {
        need(r["shared_edges"].IsArray(), "invalid shared edges");
        for (const auto& edge : r["shared_edges"].GetArray()) {
          need(edge.IsArray() && edge.Size() == 4 && edge[0].IsUint64() && edge[1].IsUint() && edge[1].GetUint() < 2
                   && edge[2].IsNumber() && edge[3].IsNumber(),
               "invalid shared edge");
          int    axis = edge[1].GetInt();
          double lo = edge[2].GetDouble(), hi = edge[3].GetDouble();
          need(job.box[axis] <= lo && lo <= hi && hi <= job.box[axis + 2], "shared edge exceeds chunk");
          slides.emplace(edge[0].GetUint64(), Slide{axis, lo - job.box[axis], hi - job.box[axis]});
        }
      }
      if (r.HasMember("contacts")) {
        need(r["contacts"].IsArray(), "invalid chunk contacts");
        std::map<uint64_t, std::array<double, 2>> edits;
        for (const auto& contact : r["contacts"].GetArray()) {
          need(contact.IsArray() && contact.Size() == 3 && contact[0].IsUint64() && contact[1].IsNumber() && contact[2].IsNumber(),
               "invalid contact edit");
          auto       x = contact[1].GetDouble(), y = contact[2].GetDouble();
          std::array xy{x, y};
          bool       on_edge = job.perimeter.empty() ? (x == job.box[0] || x == job.box[2] || y == job.box[1] || y == job.box[3])
                                                     : std::ranges::any_of(job.perimeter, [&](const auto& e) {
                                                   int k = int(e[0]);
                                                   return xy[1 - k] == e[1] && xy[k] >= e[2] && xy[k] <= e[3];
                                                       });
          need(on_edge && x >= job.box[0] && x <= job.box[2] && y >= job.box[1] && y <= job.box[3],
               "contact outside chunk boundary");
          need(edits.emplace(contact[0].GetUint64(), std::array{x - job.box[0], y - job.box[1]}).second, "duplicate cut net");
        }
        for (size_t i = 0; !edits.empty() && i < job.original_nets.size(); ++i) {
          auto edit = edits.find(job.original_nets[i]);
          if (edit != edits.end()) {
            auto end          = d.flat_net2pin_start.at(job.boundary_nets[i] + 1);
            auto node         = d.pin2node.at(d.flat_net2pin.at(end - 1));
            d.node_x.at(node) = edit->second[0];
            d.node_y.at(node) = edit->second[1];
            edits.erase(edit);
          }
        }
        need(edits.empty(), "edited net has no contact in this packet");
      }
    }
    const int owned = d.num_movable;
    if (stage == "final" && freeze_macros && macros) {
      std::map<std::string, std::array<std::optional<double>, 2>> cell_holds;
      for (const auto& [i, axes] : holds) {
        if (!d.node_macro[i]) {
          cell_holds.emplace(job.names[i], axes);
        }
      }
      freeze_chunk_macros(job);
      holds.clear();
      for (int i = 0; !cell_holds.empty() && i < d.num_movable; ++i) {
        if (auto it = cell_holds.find(job.names[i]); it != cell_holds.end()) {
          holds.emplace(i, it->second);
          cell_holds.erase(it);
        }
      }
    }
    if (!centroids.empty()) {
      std::map<std::string, int> nodes;
      for (int i = 0; i < owned; ++i) {
        nodes.emplace(job.names[i], i);
      }
      Centroid_compile_options options;
      options.dbu = job.dbu;
      options.default_strength = 0.1;
      options.resolve = [&](const std::string& name) {
        auto it = nodes.find(name);
        return it == nodes.end() ? -1 : it->second;
      };
      auto compiled = compile_centroids(centroids, d, options);
      for (const auto& record : compiled.results) {
        need(record.unresolved == 0, "unknown centroid member in " + record.id);
      }
      d.cohesion_groups = std::move(compiled.groups);
      report.AddMember("centroid_groups", uint64_t(d.cohesion_groups.size()), a);
    }
    if (!stage.empty()) {
      report.AddMember("solver_movable_nodes", d.num_movable, a);
      report["detailed_place"].SetBool(d.num_movable > 0 && stage == "final" && job.detailed);
    }
    if (job.multirow) {
      throw std::runtime_error("multi-height standard cells require a row-aware local worker");
    }
    if (!d.num_movable) {
      status = "fixed_only";
      report.AddMember("iterations", 0, a);
      if (owned) {
        d.build_maps();
        report.AddMember("hpwl_dbu", dp::hpwl(d), a);
      }
    } else {
      d.build_maps();
      d.validate();
      report.AddMember("initial_hpwl_dbu", dp::hpwl(d), a);
      dp::Options o;
      // A sparse home can otherwise allocate hundreds of thousands of virtual
      // fillers for just a few thousand real cells. The cap coarsens particles;
      // DREAMPlace preserves their total charge and the physical node sizes.
      o.filler_max_count = std::max(d.num_movable, 4 * o.bins_x * o.bins_y);
      report.AddMember("filler_limit", o.filler_max_count, a);
      if (!d.cohesion_groups.empty()) {
        o.cohesion_weight = 1.0 / job.dbu;
      }
      o.num_threads    = job.threads;
      o.max_iterations = job.iterations;
      o.min_iterations = std::min(o.min_iterations, o.max_iterations);
      if (trial_steps >= 0) {
        o.min_iterations = o.max_iterations = trial_steps;
      }
      if (!holds.empty()) {
        o.before_legalize = [&](dp::Design& physical) {
          physical.node_legalize_fixed.resize(physical.node_x.size());
          for (const auto& [i, axes] : holds) {
            auto snap = [](double x, double step, double low, double high) {
              need(step > 0 && std::ceil(low / step) <= std::floor(high / step), "no legal site inside held object's home");
              return std::clamp(std::round(x / step), std::ceil(low / step), std::floor(high / step)) * step;
            };
            const auto& l      = physical.layout;
            double      hx     = physical.node_halo_x.empty() ? 0 : physical.node_halo_x[i];
            double      hy     = physical.node_halo_y.empty() ? 0 : physical.node_halo_y[i];
            physical.node_x[i] = axes[0].value_or(snap(physical.node_x[i], l.site_width, hx, l.xh - physical.node_size_x[i] - hx));
            physical.node_y[i] = axes[1].value_or(snap(physical.node_y[i], l.row_height, hy, l.yh - physical.node_size_y[i] - hy));
            physical.node_legalize_fixed[i] = 1;
          }
        };
        d.node_boxes.assign(d.node_x.size(), {0, 0, d.layout.xh, d.layout.yh});
        for (const auto& [i, axes] : holds) {
          if (axes[0]) {
            d.node_boxes[i].xl = d.node_boxes[i].xh = *axes[0];
          }
          if (axes[1]) {
            d.node_boxes[i].yl = d.node_boxes[i].yh = *axes[1];
          }
        }
      }
      if (trial_steps > 0 || live_frames) {
        o.interact_interval = 10;
        o.interact          = [&, last = Clock::time_point{}](const dp::InteractView& view) mutable {
          const auto now = Clock::now();
          if (view.count >= int(job.physical.node_x.size()) && (view.finishing || now - last >= std::chrono::milliseconds(250))) {
            write_chunk_preview(output / "progress.tmp", job, macros, &view, score_nets ? &*score_nets : nullptr);
            fs::rename(output / "progress.tmp", output / "progress.json");
            last = now;
          }
          return dp::InteractUpdate{};
        };
      }
      std::vector<double> gp_x, gp_y;
      if (live_frames || trial_steps >= 0) {
        auto apply_holds  = o.before_legalize;
        o.before_legalize = [&, apply_holds](dp::Design& physical) {
          gp_x.assign(physical.node_x.begin(), physical.node_x.begin() + owned);
          gp_y.assign(physical.node_y.begin(), physical.node_y.begin() + owned);
          dp::InteractView gp{};
          gp.x = physical.node_x.data();
          gp.y = physical.node_y.data();
          write_chunk_preview(output / "gp.json", job, macros, &gp);
          if (apply_holds) {
            apply_holds(physical);
          }
        };
      }
      o.target_density = job.density;
      o.legalize       = true;
      o.detailed_place = stage != "macro" && job.detailed;
      // Fixed cut terminals and projected obstacles stay outside the movable prefix.
      auto result      = dp::place(d, o);
      report.AddMember("iterations", result.iterations, a).AddMember("overflow", result.overflow, a);
      if (!dp::is_legal(d, &reason) || !result.legal) {
        throw std::runtime_error(reason.empty() ? "local legalization failed" : reason);
      }
      if (!gp_x.empty()) {
        double   maximum = 0, sum = 0;
        unsigned moved = 0;
        for (int i = 0; i < owned; ++i) {
          double delta  = std::abs(gp_x[i] - d.node_x[i]) + std::abs(gp_y[i] - d.node_y[i]);
          sum          += delta;
          maximum       = std::max(maximum, delta);
          moved        += delta > .01;
        }
        report.AddMember("gp_moved", moved, a)
            .AddMember("gp_max_displacement_dbu", maximum, a)
            .AddMember("gp_mean_displacement_dbu", sum / owned, a);
      }
      if (!holds.empty()) {
        for (const auto& [i, axes] : holds) {
          need((!axes[0] || std::abs(d.node_x[i] - *axes[0]) < .01) && (!axes[1] || std::abs(d.node_y[i] - *axes[1]) < .01),
               "legalization could not retain fixed coordinate");
        }
      }
      // On the common edge, any coordinate inside the local pins' bounding interval
      // minimizes this net's local HPWL. Move the contact only as far as necessary.
      unsigned moved = 0;
      for (size_t k = 0; k < job.original_nets.size(); ++k) {
        auto it = slides.find(job.original_nets[k]);
        if (it == slides.end()) {
          continue;
        }
        auto [axis, lo, hi] = it->second;
        auto&       coord   = axis ? d.node_y : d.node_x;
        const auto& offset  = axis ? d.pin_offset_y : d.pin_offset_x;
        auto        net     = job.boundary_nets[k];
        int         first = d.flat_net2pin_start[net], last = d.flat_net2pin_start[net + 1] - 1;
        if (first >= last) {
          continue;
        }
        double low = hi, high = lo;
        for (int j = first; j < last; ++j) {
          auto   pin = d.flat_net2pin[j];
          double x   = std::clamp(coord[d.pin2node[pin]] + offset[pin], lo, hi);
          low        = std::min(low, x);
          high       = std::max(high, x);
        }
        auto& xy    = coord[d.pin2node[d.flat_net2pin[last]]];
        auto  next  = std::clamp(xy, low, high);
        moved       += next != xy;
        xy           = next;
      }
      report.AddMember("adjusted_contacts", moved, a);
      report.AddMember("hpwl_dbu", dp::hpwl(d), a);
    }
    for (const auto& blocked_box : job.exclusions) {
      for (int i = 0; i < owned; ++i) {
        const double x = job.box[0] + d.node_x[i], y = job.box[1] + d.node_y[i];
        const double hx = d.node_halo_x.empty() ? 0 : d.node_halo_x[i], hy = d.node_halo_y.empty() ? 0 : d.node_halo_y[i];
        need(x + d.node_size_x[i] + hx <= blocked_box[0] + .01 || y + d.node_size_y[i] + hy <= blocked_box[1] + .01
                 || x - hx >= blocked_box[2] - .01 || y - hy >= blocked_box[3] - .01,
             "owned footprint crosses the stepped perimeter");
      }
    }
    report_timing(report, job, d.node_x.data(), d.node_y.data());
    d.num_movable = owned;
    if (stage == "macro") {
      report.AddMember("macro_suggestions", macro_suggestions(job, report), a);
    }
    if (pitch > 0) {
      report_demand(report, job, pitch, rates, blocked);
    }
    if (score_nets) {
      report.AddMember("score", chunk_score(job, *score_nets, report), a);
    }
    if (owned) {
      boundary_feedback(report, job);
      put_positions(output / "positions.json", job);
      status = "placed";
    }
    if (!stage.empty()) {
      write_chunk_preview(output / "preview.json", job, macros);
    }
    if (!stage.empty()) {
      report.AddMember("handoff_checksum", str(write_chunk_job(output / "handoff.bin", job), report), a);
    }
  } catch (const std::exception& e) {
    reason = e.what();
    status = "failed";
  }
  report.AddMember("status", str(status, report), a);
  if (!reason.empty()) {
    report.AddMember("reason", str(reason, report), a);
  }
  report.AddMember("worker_seconds", std::chrono::duration<double>(Clock::now() - start).count(), a);
  metadata::Bundle b;
  b.replace(report);
  b.write((output / "feedback.json").string());
  return status == "failed" ? 1 : 0;
}
int place_chunk_batch(const std::string& exe, const fs::path& manifest, const fs::path& output,
                      const std::optional<std::string>& selected) {
  auto                      input = metadata::Bundle::read(manifest.string());
  std::vector<const Value*> records;
  for (const auto& record : input.records().GetArray()) {
    if (record.HasMember("class") && record["class"].IsString() && std::string_view(record["class"].GetString()) == jobs_class) {
      records.push_back(&record);
    }
  }
  need(records.size() == 1, "expected one chunk jobs record in manifest");
  const auto& d            = *records.front();
  auto        string_field = [](const Value& v, const char* k) {
    need(v.IsObject() && v.HasMember(k) && v[k].IsString(), std::string("invalid manifest: ") + k);
    return std::string(v[k].GetString());
  };
  auto design = string_field(d, "design");
  need(d.HasMember("jobs") && d["jobs"].IsArray() && d.HasMember("budget_bytes") && d["budget_bytes"].IsUint64()
           && d.HasMember("require_fit") && d["require_fit"].IsBool() && d.HasMember("plan_key") && d["plan_key"].IsUint64(),
       "invalid chunk manifest");
  auto budget = d["budget_bytes"].GetUint64();
  bool strict = d["require_fit"].GetBool();
  auto base   = fs::canonical(manifest).parent_path();
  bool found  = false;
  for (const auto& j : d["jobs"].GetArray()) {
    if (!selected || *selected == string_field(j, "id")) {
      found = true;
    }
  }
  need(found, "no matching physical chunk");
  if (!output.parent_path().empty()) {
    fs::create_directories(output.parent_path());
  }
  need(fs::create_directory(output), "chunk results directory must be new: " + output.string());
  metadata::Bundle combined(input.json());
  combined.erase_selected(feedback_class, design);
  // base is canonical (symlinks resolved): relate it to the canonical output too, or a symlinked --out gets wrong "../" counts.
  const auto out_dir = fs::canonical(output);
  auto rebase = [&](const std::string& p) { return (base / fs::path(p)).lexically_normal().lexically_relative(out_dir).string(); };
  // A retry changes one measurement. Preserve the other homes' current
  // measurements and their artifact locations when the input already has them.
  if (selected) {
    for (auto* prior : input.select(feedback_class, design)) {
      if (string_field(*prior, "id") == *selected || !prior->HasMember("plan_revision")
          || string_field(*prior, "plan_revision") != string_field(d, "plan_revision")) {
        continue;
      }
      Doc row;
      row.CopyFrom(*prior, row.GetAllocator());
      for (auto field : {"positions", "report", "preview"}) {
        if (row.HasMember(field)) {
          row[field] = str(rebase(string_field(row, field)), row);
        }
      }
      combined.replace(row);
    }
  }
  // Rewrite packet links relative to the output so the merged metadata can also be replayed.
  Doc jobs;
  jobs.CopyFrom(d, jobs.GetAllocator());
  for (auto& j : jobs["jobs"].GetArray()) {
    j["input"] = str(rebase(string_field(j, "input")), jobs);
  }
  combined.replace(jobs);
  int index = -1, failed = 0;
  for (const auto& j : d["jobs"].GetArray()) {
    ++index;
    auto id = string_field(j, "id");
    if (selected && *selected != id) {
      continue;
    }
    auto folder = output / std::to_string(index);
    fs::create_directory(folder);
    auto src = (base / string_field(j, "input")).string(), dst = folder.string();
    if (j.HasMember("request")) {
      metadata::Bundle request;
      request.replace(j["request"]);
      request.write((folder / "request.json").string());
    }
    int         status = 0;
    uint64_t    rss    = 0;
    std::string why;
    if (strict
        && (!j.HasMember("estimated_bytes") || !j["estimated_bytes"].IsUint64() || j["estimated_bytes"].GetUint64() > budget)) {
      why = "predicted peak exceeds chunk budget";
    } else if (!fs::is_regular_file(src)) {
      why = "missing chunk input: " + src;
    } else {
      auto  sum = string_field(j, "checksum");
      char* argv[]
          = {const_cast<char*>(exe.c_str()), const_cast<char*>("--place-one-chunk"), src.data(), dst.data(), sum.data(), nullptr};
      pid_t pid;
      int   rc = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv, environ);
      if (rc) {
        why = std::strerror(rc);
      } else {
        rusage use{};
        pid_t  done;
        bool   killed = false;
        do {
          done = wait4(pid, &status, strict ? WNOHANG : 0, &use);
          if (done == 0) {
            rss = std::max(rss, process_rss(pid));
            if (!killed && rss > budget) {
              kill(pid, SIGKILL);
              killed = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        } while (done == 0 || (done < 0 && errno == EINTR));
        need(done == pid, "could not reap chunk worker");
        rss = std::max(rss, peak_bytes(use));
        if (killed || (strict && rss > budget)) {
          why = "worker exceeded the chunk memory budget";
        } else if (!WIFEXITED(status)) {
          why = "chunk worker terminated by signal " + std::to_string(WTERMSIG(status));
        }
      }
    }
    Doc  r     = rec(feedback_class, design, id);
    bool valid = false;
    if (why.empty() && fs::exists(folder / "feedback.json")) {
      auto        b       = metadata::Bundle::read((folder / "feedback.json").string());
      auto        reports = b.select(feedback_class, design);
      const auto* fb      = reports.size() == 1 ? reports.front() : nullptr;
      if (fb && id == string_field(*fb, "id") && fb->HasMember("plan_key") && (*fb)["plan_key"].IsUint64()
          && (*fb)["plan_key"].GetUint64()
                 == (j.HasMember("packet_plan_key") && j["packet_plan_key"].IsUint64() ? j["packet_plan_key"] : d["plan_key"])
                        .GetUint64()) {
        r.CopyFrom(*reports.front(), r.GetAllocator());
        valid = true;
      }
    }
    if (!valid) {
      auto& a = r.GetAllocator();
      r.AddMember("status", "failed", a).AddMember("reason", str(why.empty() ? "worker returned no matching report" : why, r), a);
    }
    auto& a = r.GetAllocator();
    r.AddMember("plan_revision", str(string_field(d, "plan_revision"), r), a)
        .AddMember("memory_scope", "worker_process", a)
        .AddMember("peak_rss_bytes", rss, a)
        .AddMember("budget_bytes", budget, a)
        .AddMember("over_budget", budget && rss > budget, a);
    auto outcome = string_field(r, "status");
    if (outcome != "placed") {
      fs::remove(folder / "positions.json");
      if (outcome == "failed") {
        fs::remove(folder / "handoff.bin");
        fs::remove(folder / "preview.json");
      }
    }
    if (valid && outcome != "failed" && fs::exists(folder / "preview.json")) {
      r.AddMember("preview", str(std::to_string(index) + "/preview.json", r), a);
    }
    if (outcome == "placed") {
      r.AddMember("positions", str(std::to_string(index) + "/positions.json", r), a);
    }
    if (!valid || outcome == "failed" || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      ++failed;
    }
    metadata::Bundle single;
    single.replace(r);
    if (r.HasMember("positions") || r.HasMember("preview")) {
      auto rr = rec(feedback_class, design, id);
      rr.CopyFrom(r, rr.GetAllocator());
      if (rr.HasMember("positions")) {
        rr["positions"].SetString("positions.json");
      }
      if (rr.HasMember("preview")) {
        rr["preview"].SetString("preview.json");
      }
      single.replace(rr);
    }
    single.write((folder / "feedback.json").string());
    r.RemoveMember("boundary");
    r.RemoveMember("score");
    r.AddMember("report", str(std::to_string(index) + "/feedback.json", r), a);
    if (outcome == "placed" && r.HasMember("start_bytes") && r["start_bytes"].IsUint64() && rss >= r["start_bytes"].GetUint64()) {
      Chunk_predictor evidence;
      evidence.context = string_field(d, "memory_context");
      if (evidence.context.starts_with("home-v1:") && j.HasMember("counts") && j["counts"].IsArray() && j["counts"].Size() == 4
          && std::all_of(j["counts"].Begin(), j["counts"].End(), [](const auto& v) { return v.IsUint64(); })) {
        Memory_counts counts{};
        for (unsigned k = 0; k < 4; ++k) {
          counts[k] = j["counts"][k].GetUint64();
        }
        auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        evidence.evidence.push_back(
            {std::format("{}:{}:{}:{}", evidence.context, stamp, getpid(), index), counts, r["start_bytes"].GetUint64(), rss});
        evidence.append_evidence(combined);
      }
    }
    combined.replace(r);
    combined.write((output / "metadata.json").string());
    std::fprintf(stderr, "chunk %d/%u %s: %s, peak %.1f MB\n", index + 1, d["jobs"].Size(), id.c_str(), outcome.c_str(), rss / 1e6);
  }
  return failed ? 1 : 0;
}
}  // namespace livehd::placement
