// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "sta_cache.hpp"

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

#include "diag.hpp"
#include "rapidjson/document.h"

namespace livehd::opentimer {

namespace {

namespace fs = std::filesystem;

// At most this many analyses per workdir. A synthesis-option sweep mints one
// record per distinct netlist and nothing else collects them; the oldest
// insertion is dropped first (docs/opt_loop_incr.md open question 3, cache
// size is a real cost).
constexpr size_t kMaxRecords = 32;

constexpr uint64_t mix64(uint64_t x) {
  x ^= x >> 33U;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33U;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33U;
  return x;
}
constexpr uint64_t hcombine(uint64_t h, uint64_t v) { return mix64(h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U))); }

uint64_t hstr(std::string_view s) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a
  for (char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ULL;
  }
  return h;
}

// FNV-1a over a file's bytes. A Liberty is ~12 MB and the whole point is to
// notice a re-characterized library, so hash CONTENT, never path+mtime (a
// rebuilt PDK keeps both). Read in blocks: a 12 MB string per timing file is
// pure peak RSS in the phase whose memory the resource policy is watching.
uint64_t hfile(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return hstr("\x01missing");
  }
  uint64_t          h = 1469598103934665603ULL;
  std::vector<char> buf(1U << 16U);
  while (ifs.read(buf.data(), static_cast<std::streamsize>(buf.size())) || ifs.gcount() != 0) {
    const auto n = static_cast<size_t>(ifs.gcount());
    for (size_t i = 0; i < n; ++i) {
      h ^= static_cast<unsigned char>(buf[i]);
      h *= 1099511628211ULL;
    }
  }
  return h;
}

std::string jesc(std::string_view s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"' : o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default  : o += c; break;
    }
  }
  return o;
}

std::string jstr(const rapidjson::Value& v, const char* key) {
  auto it = v.FindMember(key);
  if (it == v.MemberEnd() || !it->value.IsString()) {
    return {};
  }
  return std::string{it->value.GetString(), it->value.GetStringLength()};
}

double jnum(const rapidjson::Value& v, const char* key, double def) {
  auto it = v.FindMember(key);
  if (it == v.MemberEnd() || !it->value.IsNumber()) {
    return def;
  }
  return it->value.GetDouble();
}

void jstrs(const rapidjson::Value& v, const char* key, std::vector<std::string>& out) {
  auto it = v.FindMember(key);
  if (it == v.MemberEnd() || !it->value.IsArray()) {
    return;
  }
  for (const auto& e : it->value.GetArray()) {
    if (e.IsString()) {
      out.emplace_back(e.GetString(), e.GetStringLength());
    }
  }
}

}  // namespace

Sta_cache::Sta_cache(std::string dir, uint64_t salt) : dir_(std::move(dir)), salt_(salt) {
  const auto    path = (fs::path(dir_) / "sta_cache.json").string();
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return;
  }
  std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  ifs.close();
  rapidjson::Document d;
  d.Parse(text.data(), text.size());
  if (d.HasParseError() || !d.IsObject()) {
    return;  // corrupt file: start over, a cache is never an oracle of record
  }
  if (jstr(d, "salt") != std::format("{:016x}", salt_)) {
    return;  // a different timing engine / environment schema: re-time
  }
  auto regs = d.FindMember("analyses");
  if (regs == d.MemberEnd() || !regs->value.IsObject()) {
    return;
  }
  for (const auto& m : regs->value.GetObject()) {
    if (!m.value.IsObject()) {
      continue;
    }
    Sta_record r;
    r.time_unit       = jstr(m.value, "time_unit");
    r.max_delay       = jnum(m.value, "max_delay", 0.0);
    r.max_pin         = jstr(m.value, "max_pin");
    r.block           = jstr(m.value, "block");
    r.opaque_nodes    = static_cast<uint64_t>(jnum(m.value, "opaque_nodes", 0.0));
    r.ambiguous_nodes = static_cast<uint64_t>(jnum(m.value, "ambiguous_nodes", 0.0));
    jstrs(m.value, "opaque_examples", r.opaque_examples);
    jstrs(m.value, "or_examples", r.or_examples);
    if (r.block.empty()) {
      continue;  // no report to replay: treat as absent
    }
    if (auto cols = m.value.FindMember("colors"); cols != m.value.MemberEnd() && cols->value.IsArray()) {
      for (const auto& c : cols->value.GetArray()) {
        if (!c.IsObject()) {
          continue;
        }
        Sta_record::Color_row row;
        row.module       = jstr(c, "module");
        row.color        = static_cast<int>(jnum(c, "color", 0.0));
        row.cells        = static_cast<uint64_t>(jnum(c, "cells", 0.0));
        row.max_arrival  = jnum(c, "max_arrival", -1.0);
        row.critical_pin = jstr(c, "critical_pin");
        row.critical_src = jstr(c, "critical_src");
        r.colors.push_back(std::move(row));
      }
    }
    std::string key{m.name.GetString(), m.name.GetStringLength()};
    order_.push_back(key);
    recs_.emplace(std::move(key), std::move(r));
  }
}

const Sta_record* Sta_cache::lookup(const std::string& key) const {
  auto it = recs_.find(key);
  return it == recs_.end() ? nullptr : &it->second;
}

void Sta_cache::insert(const std::string& key, Sta_record rec) {
  if (!recs_.contains(key)) {
    order_.push_back(key);
  }
  recs_.insert_or_assign(key, std::move(rec));
  while (order_.size() > kMaxRecords) {
    recs_.erase(order_.front());
    order_.erase(order_.begin());
  }
  dirty_ = true;
}

void Sta_cache::save() {
  if (!dirty_) {
    return;
  }
  std::error_code ec;
  fs::create_directories(dir_, ec);

  std::string out   = std::format("{{\"schema\":1,\"salt\":\"{:016x}\",\"analyses\":{{", salt_);
  bool        first = true;
  for (const auto& key : order_) {
    auto it = recs_.find(key);
    if (it == recs_.end()) {
      continue;
    }
    const auto& r = it->second;
    if (!first) {
      out += ",";
    }
    first  = false;
    out   += std::format("\"{}\":{{\"time_unit\":\"{}\",\"max_delay\":{:.9g},\"max_pin\":\"{}\",\"block\":\"{}\"",
                         jesc(key),
                         jesc(r.time_unit),
                         r.max_delay,
                         jesc(r.max_pin),
                         jesc(r.block));
    out   += ",\"colors\":[";
    for (size_t i = 0; i < r.colors.size(); ++i) {
      const auto& c  = r.colors[i];
      out           += std::format(
          "{}{{\"module\":\"{}\",\"color\":{},\"cells\":{},\"max_arrival\":{:.9g},\"critical_pin\":\"{}\","
          "\"critical_src\":\"{}\"}}",
          i != 0 ? "," : "",
          jesc(c.module),
          c.color,
          c.cells,
          c.max_arrival,
          jesc(c.critical_pin),
          jesc(c.critical_src));
    }
    out += std::format("],\"opaque_nodes\":{},\"ambiguous_nodes\":{},\"opaque_examples\":[", r.opaque_nodes, r.ambiguous_nodes);
    for (size_t i = 0; i < r.opaque_examples.size(); ++i) {
      out += std::format("{}\"{}\"", i != 0 ? "," : "", jesc(r.opaque_examples[i]));
    }
    out += "],\"or_examples\":[";
    for (size_t i = 0; i < r.or_examples.size(); ++i) {
      out += std::format("{}\"{}\"", i != 0 ? "," : "", jesc(r.or_examples[i]));
    }
    out += "]}";
  }
  out += "}}\n";

  const auto path = (fs::path(dir_) / "sta_cache.json").string();
  const auto tmp  = path + ".tmp";
  {
    std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
    if (!ofs) {
      livehd::diag::warn("pass.opentimer", "sta-cache-write", "io")
          .msg("pass.opentimer: cannot write the STA reuse cache '{}' (this run is unaffected)", path)
          .emit();
      return;
    }
    ofs << out;
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    fs::remove(tmp, ec);
    livehd::diag::warn("pass.opentimer", "sta-cache-write", "io")
        .msg("pass.opentimer: cannot publish the STA reuse cache '{}' (this run is unaffected)", path)
        .emit();
  }
  dirty_ = false;
}

uint64_t Sta_cache::env_hash(const std::vector<std::string>& files, std::string_view top, std::string_view hier, int margin,
                             bool stats) {
  // ORDER-SENSITIVE, deliberately: the pass classifies by extension but the
  // FIRST .lib is the max corner and the second the min, so swapping two
  // Liberty arguments is a different analysis. Hash in the given order.
  uint64_t h = hstr("sta-env-v1");
  for (const auto& f : files) {
    h = hcombine(h, hcombine(hstr(fs::path(f).filename().string()), hfile(f)));
  }
  h = hcombine(h, hstr(top));
  h = hcombine(h, hstr(hier));
  h = hcombine(h, static_cast<uint64_t>(margin));
  // `--stats` decides whether the per-color rows exist at all, so a record made
  // without them must not satisfy a run that asks for them.
  return hcombine(h, stats ? 1U : 0U);
}

}  // namespace livehd::opentimer
