// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "chunk_memory.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>

#include "core/job_manager.hpp"

namespace livehd::placement {
namespace {
uint64_t self_high_water() {
  rusage u{};
  if (getrusage(RUSAGE_SELF, &u) != 0) {
    return 0;
  }
#ifdef __APPLE__
  return u.ru_maxrss;
#else
  return uint64_t(u.ru_maxrss) * 1024;
#endif
}
constexpr double margin        = 1.25;
constexpr size_t history_limit = 32;
uint64_t         bytes(long double value) {
  if (!(value >= 0)) {
    throw std::invalid_argument("invalid chunk memory prediction");
  }
  return value >= UINT64_MAX ? UINT64_MAX : static_cast<uint64_t>(std::ceil(value));
}
std::vector<Memory_sample> read_samples(const std::string& context, const metadata::Bundle& b) {
  std::vector<Memory_sample> out;
  for (auto* r : b.select(memory_sample_class, "")) {
    if (!r->HasMember("context") || !(*r)["context"].IsString() || context != (*r)["context"].GetString()) {
      continue;
    }
    if (!r->HasMember("scope") || !(*r)["scope"].IsString()
        || std::string_view((*r)["scope"].GetString()) != (context.starts_with("home-v1:") ? "isolated_chunk_placement" : "headless_single_chunk_placement_and_downstream")
        || !r->HasMember("metric") || !(*r)["metric"].IsString()
        || std::string_view((*r)["metric"].GetString()) != (context.starts_with("home-v1:") ? "worker_peak_rss" : "sampled_process_tree_rss")) {
      continue;
    }
    for (auto key : {"start_bytes", "peak_bytes"}) {
      if (!r->HasMember(key) || !(*r)[key].IsUint64()) {
        throw std::invalid_argument("invalid placement memory sample");
      }
    }
    if (!r->HasMember("counts") || !(*r)["counts"].IsArray() || (*r)["counts"].Size() != 4 || !r->HasMember("id")
        || !(*r)["id"].IsString()) {
      throw std::invalid_argument("invalid placement memory sample features");
    }
    Memory_sample s{(*r)["id"].GetString(), {}, (*r)["start_bytes"].GetUint64(), (*r)["peak_bytes"].GetUint64()};
    if (!s.start || s.peak < s.start) {
      throw std::invalid_argument("invalid placement memory peak");
    }
    for (int j = 0; j < 4; ++j) {
      const auto& x = (*r)["counts"][j];
      if (!x.IsUint64()) {
        throw std::invalid_argument("invalid memory count");
      }
      s.counts[j] = x.GetUint64();
    }
    out.push_back(std::move(s));
  }
  return out;
}
std::vector<Memory_sample> recent(std::vector<Memory_sample> samples) {
  std::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  samples.erase(std::unique(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.id == b.id; }),
                samples.end());
  if (samples.size() > history_limit) {
    samples.erase(samples.begin(), samples.end() - history_limit);
  }
  return samples;
}
rapidjson::Document record(const std::string& context, const Memory_sample& s) {
  rapidjson::Document r(rapidjson::kObjectType);
  auto&               a   = r.GetAllocator();
  auto                str = [&](const std::string& v) { return rapidjson::Value(v.c_str(), a); };
  rapidjson::Value    n(rapidjson::kArrayType);
  for (auto x : s.counts) {
    n.PushBack(x, a);
  }
  r.AddMember("class", rapidjson::StringRef(memory_sample_class), a)
      .AddMember("id", str(s.id), a)
      .AddMember("context", str(context), a)
      .AddMember("counts", n, a)
      .AddMember("start_bytes", s.start, a)
      .AddMember("peak_bytes", s.peak, a)
      .AddMember("scope", rapidjson::StringRef(context.starts_with("home-v1:") ? "isolated_chunk_placement" : "headless_single_chunk_placement_and_downstream"), a)
      .AddMember("metric", rapidjson::StringRef(context.starts_with("home-v1:") ? "worker_peak_rss" : "sampled_process_tree_rss"), a);
  return r;
}
}  // namespace
void Chunk_predictor::append_evidence(metadata::Bundle& bundle) const {
  for (const auto& s : evidence) {
    bundle.replace(record(context, s));
  }
}
bool Chunk_predictor::covers(const Memory_counts& n) const {
  return std::any_of(observed.begin(), observed.end(), [&](const auto& o) {
    for (unsigned i = 0; i < 4; ++i) {
      if (n[i] > o[i]) {
        return false;
      }
    }
    return true;
  });
}
uint64_t Chunk_predictor::variable(const Memory_counts& n) const {
  long double sum = 0;
  for (int j = 0; j < 4; ++j) {
    sum += static_cast<long double>(n[j]) * rate[j];
  }
  return bytes(sum);
}
uint64_t Chunk_predictor::peak(const Memory_counts& n) const {
  auto v = variable(n);
  return UINT64_MAX - baseline < v ? UINT64_MAX : baseline + v;
}
Chunk_predictor Chunk_predictor::fit(const std::string& ctx, const std::vector<Memory_sample>& all) {
  Chunk_predictor out;
  out.context    = ctx;
  const auto obs = recent(all);
  out.samples    = obs.size();
  out.evidence   = obs;
  if (obs.empty()) {
    return out;
  }
  // Nonnegative ridge fit to the growth above each run's starting footprint. Scale
  // each row relative to its measured peak: a large design must not erase small ones.
  // Keep poorly identified inputs close to the cold prior (e.g. no macros observed).
  std::array<double, 4>              beta{1, 1, 1, 1};
  std::vector<std::array<double, 4>> xs;
  std::vector<double>                ys;
  for (const auto& s : obs) {
    out.observed.push_back(s.counts);
    if (!s.start || s.peak < s.start || s.counts == Memory_counts{}) {
      throw std::invalid_argument("invalid placement memory observation");
    }
    out.baseline = std::max(out.baseline, bytes(margin * s.start));
    std::array<double, 4> x{};
    for (int k = 0; k < 4; ++k) {
      x[k]                = s.counts[k] * out.rate[k] / s.peak;
      out.observed_max[k] = std::max(out.observed_max[k], s.counts[k]);
    }
    xs.push_back(x);
    ys.push_back(double(s.peak - s.start) / s.peak);
  }
  for (int it = 0; it < 40; ++it) {
    for (int k = 0; k < 4; ++k) {
      double num = .025, den = .025;
      for (size_t i = 0; i < xs.size(); ++i) {
        double residual = ys[i];
        for (int j = 0; j < 4; ++j) {
          if (j != k) {
            residual -= xs[i][j] * beta[j];
          }
        }
        num += xs[i][k] * residual;
        den += xs[i][k] * xs[i][k];
      }
      beta[k] = std::max(1., num / den);  // No training sample can reduce a cold feature allowance.
    }
  }
  for (int k = 0; k < 4; ++k) {
    out.rate[k] *= beta[k];
  }
  // Raise the fitted surface to bound every retained observed peak, then leave
  // headroom above it. These are upper bounds on observations, not on unseen designs.
  double scale = 1;
  for (const auto& s : obs) {
    auto v = out.variable(s.counts);
    if (v) {
      scale = std::max(scale, double(s.peak - s.start) / v);
    }
  }
  for (auto& rate : out.rate) {
    rate *= scale * margin;
  }
  // The slope fit uses rounded byte counts. Close any final rounding gap,
  // including on platforms where long double has the precision of double.
  for (const auto& s : obs) {
    const auto target   = bytes(static_cast<long double>(s.peak) * margin);
    const auto forecast = out.peak(s.counts);
    if (target > forecast) {
      out.baseline += target - forecast;
    }
  }
  return out;
}
Chunk_predictor load_chunk_predictor(const std::string& ctx, const std::string& path, const metadata::Bundle* input) {
  std::vector<Memory_sample> samples;
  if (!path.empty() && std::filesystem::exists(path)) {
    samples = read_samples(ctx, metadata::Bundle::read(path));
  }
  if (input) {
    auto extra = read_samples(ctx, *input);
    samples.insert(samples.end(), extra.begin(), extra.end());
  }
  return Chunk_predictor::fit(ctx, samples);
}
void save_chunk_sample(const std::string& ctx, const Memory_counts& counts, uint64_t start, uint64_t peak, const std::string& path,
                       const std::string& bundle_path) {
  if (!start || peak < start) {
    return;
  }
  auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  Memory_sample s{ctx + ":" + std::to_string(now) + ":" + std::to_string(getpid()), counts, start, peak};
  auto          r = record(ctx, s);
  if (!path.empty()) {
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }
    struct Lock {
      int fd;
      ~Lock() {
        if (fd >= 0) {
          ::close(fd);
        }
      }
    } lock{open((path + ".lock").c_str(), O_CREAT | O_RDWR, 0600)};
    if (lock.fd < 0 || flock(lock.fd, LOCK_EX) < 0) {
      throw std::runtime_error("cannot lock chunk memory profile: " + path);
    }
    auto history = std::filesystem::exists(path) ? metadata::Bundle::read(path) : metadata::Bundle{};
    history.replace(r);
    // Keep unrelated technologies and other metadata classes in a shared file.
    auto                                                        records = history.select(memory_sample_class, "");
    std::map<std::string, std::vector<const rapidjson::Value*>> buckets;
    for (auto* v : records) {
      if (v->HasMember("context") && (*v)["context"].IsString() && v->HasMember("id") && (*v)["id"].IsString()) {
        buckets[(*v)["context"].GetString()].push_back(v);
      }
    }
    metadata::Bundle kept;
    for (auto& [key, vs] : buckets) {
      std::sort(vs.begin(), vs.end(), [](auto a, auto b) {
        return std::string_view((*a)["id"].GetString()) < (*b)["id"].GetString();
      });
      for (size_t i = vs.size() > history_limit ? vs.size() - history_limit : 0; i < vs.size(); ++i) {
        kept.replace(*vs[i]);
      }
    }
    history.replace_selected(memory_sample_class, "", kept);
    history.write(path);
  }
  if (!bundle_path.empty()) {
    auto bundle = metadata::Bundle::read(bundle_path);
    bundle.replace(r);
    bundle.write(bundle_path);
  }
}
Placement_peak::Placement_peak()
    : start(process_tree_rss(getpid())), initial_self_peak(self_high_water()), high(start), sampler([this] {
      std::unique_lock lock(mutex);
      while (!changed.wait_for(lock, std::chrono::milliseconds(50), [&] { return stopped; })) {
        lock.unlock();
        auto v = process_tree_rss(getpid());
        lock.lock();
        high = std::max(high, v);
      }
    }) {}
Placement_peak::~Placement_peak() { finish(); }
uint64_t Placement_peak::finish() {
  if (sampler.joinable()) {
    {
      std::lock_guard lock(mutex);
      stopped = true;
    }
    changed.notify_one();
    sampler.join();
    high                 = std::max(high, process_tree_rss(getpid()));
    // If this phase set a new process high-water mark, retain even a spike
    // that fell between samples. Earlier synthesis peaks do not train placement.
    const auto self_peak = self_high_water();
    if (self_peak > initial_self_peak) {
      high = std::max(high, self_peak);
    }
  }
  return high;
}
}  // namespace livehd::placement
