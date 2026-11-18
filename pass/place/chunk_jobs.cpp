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
#include <thread>

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
  need(bool(f) && bytes.starts_with(magic), "invalid or incompatible chunk job");
  need(std::format("{:016x}", hash_util::fnv1a64(bytes)) == expected, "chunk packet does not match the exported job");
  binary_message::Decoder decoder{std::string_view(bytes).substr(magic.size())};
  Chunk_job               j;
  packet(decoder, j);
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
// A bounded view for the browser. The full position list stays on disk even for a very large home.
void write_chunk_preview(const fs::path& path, const Chunk_job& job, int macros) {
  auto               view = rec("livehd.placement.chunk_preview", job.design, job.id);
  auto&              a    = view.GetAllocator();
  const auto&        d    = job.physical;
  const std::array   count{d.num_movable - macros, macros};
  std::array<int, 2> seen{};
  Value              boxes(rapidjson::kArrayType);
  for (int i = 0; i < d.num_movable; ++i) {
    const int kind   = d.node_macro[i] != 0;
    const int stride = 1 + (count[kind] - 1) / 1024;
    if (seen[kind]++ % stride == 0) {
      Value box(rapidjson::kArrayType);
      box.PushBack(d.node_x[i] + job.box[0], a)
          .PushBack(d.node_y[i] + job.box[1], a)
          .PushBack(d.node_size_x[i], a)
          .PushBack(d.node_size_y[i], a)
          .PushBack(kind, a);
      boxes.PushBack(box, a);
    }
  }
  view.AddMember("boxes_dbu", boxes, a).AddMember("owned", d.num_movable, a);
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
  en.bytes = magic;
  packet(en, j);
  std::ofstream f(path, std::ios::binary);
  f.write(en.bytes.data(), en.bytes.size());
  f.close();
  need(bool(f), "cannot write chunk job " + path.string());
  return std::format("{:016x}", hash_util::fnv1a64(en.bytes));
}
std::string write_chunk_jobs(const fs::path& manifest, const metadata::Bundle& base, const Chunk_model& model,
                             const Chunk_plan& plan, dp::Design& physical, std::span<const int> owners,
                             std::span<const std::string> names, const std::string& design, int dbu, int iterations, int threads,
                             bool detailed, bool strict, const std::string& selected, const Chunk_predictor* predictor) {
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
      .AddMember("dbu_per_um", dbu, a);
  if (only) {
    d.AddMember("selected", str(selected, d), a);
  }
  const fs::path packet_dir = manifest.parent_path() / "chunk-packets";
  if (model.pack_scopes) {
    fs::create_directories(packet_dir);
  }
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
        auto& c         = plan.chunks[index];
        j.id            = c.id;
        j.design        = design;
        // The managed scheduler checks the complete packet digest. A plan-wide key in every
        // packet would force all sibling handoffs to expire after any source edit.
        j.plan_key      = model.pack_scopes ? 0 : plan.input_key;
        j.budget        = plan.budget;
        j.dbu           = dbu;
        j.box           = c.box;
        j.density       = c.density;
        j.iterations    = iterations;
        j.threads       = std::max(1, threads);
        j.detailed      = detailed;
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
        for (double v : c.box) {
          box.PushBack(v, a);
        }
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
    std::string stage;
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
      if (r.HasMember("contacts")) {
        need(r["contacts"].IsArray(), "invalid chunk contacts");
        std::map<uint64_t, std::array<double, 2>> edits;
        for (const auto& contact : r["contacts"].GetArray()) {
          need(contact.IsArray() && contact.Size() == 3 && contact[0].IsUint64() && contact[1].IsNumber() && contact[2].IsNumber(),
               "invalid contact edit");
          auto x = contact[1].GetDouble(), y = contact[2].GetDouble();
          need(x >= job.box[0] && x <= job.box[2] && y >= job.box[1] && y <= job.box[3]
                   && (x == job.box[0] || x == job.box[2] || y == job.box[1] || y == job.box[3]),
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
    if (stage == "final" && macros) {
      freeze_chunk_macros(job);
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
      o.num_threads    = job.threads;
      o.max_iterations = job.iterations;
      o.min_iterations = std::min(o.min_iterations, o.max_iterations);
      o.target_density = job.density;
      o.legalize       = true;
      o.detailed_place = stage != "macro" && job.detailed;
      // Fixed cut terminals and projected obstacles stay outside the movable prefix.
      auto result      = dp::place(d, o);
      report.AddMember("iterations", result.iterations, a).AddMember("overflow", result.overflow, a);
      if (!dp::is_legal(d, &reason) || !result.legal) {
        throw std::runtime_error(reason.empty() ? "local legalization failed" : reason);
      }
      report.AddMember("hpwl_dbu", dp::hpwl(d), a);
    }
    d.num_movable = owned;
    if (owned) {
      boundary_feedback(report, job);
      put_positions(output / "positions.json", job);
      status = "placed";
    }
    if (!stage.empty()) {
      write_chunk_preview(output / "preview.json", job, macros);
    }
    if (stage == "macro") {
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
    auto        src = (base / string_field(j, "input")).string(), dst = folder.string();
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
