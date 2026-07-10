// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "formal_cache.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

#include "rapidjson/document.h"

namespace livehd::formal {

namespace {

constexpr int kSchema = 1;

// Minimal JSON string escape (mirror of lhd_kernel's json_escape_min, kept
// local so the cache stays a leaf module).
std::string esc(std::string_view s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          o += std::format("\\u{:04x}", static_cast<unsigned char>(c));
        } else {
          o += c;
        }
    }
  }
  return o;
}

}  // namespace

Verdict_cache::Verdict_cache(std::string workdir, uint64_t salt) : workdir_(std::move(workdir)), salt_(salt) {
  std::ifstream in(workdir_ + "/formal_cache.json", std::ios::binary);
  if (!in) {
    return;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  rapidjson::Document doc;
  doc.Parse(ss.str().c_str());
  if (doc.HasParseError() || !doc.IsObject()) {
    return;  // corrupt cache: start empty (it is only ever a speedup)
  }
  if (!doc.HasMember("schema") || !doc["schema"].IsInt() || doc["schema"].GetInt() != kSchema) {
    return;
  }
  // Hints first: they survive a salt mismatch by design (heuristic-only).
  if (doc.HasMember("hints") && doc["hints"].IsObject()) {
    for (auto it = doc["hints"].MemberBegin(); it != doc["hints"].MemberEnd(); ++it) {
      if (!it->value.IsObject()) {
        continue;
      }
      Strategy_hint h;
      if (it->value.HasMember("engine") && it->value["engine"].IsString()) {
        h.engine = it->value["engine"].GetString();
      }
      if (it->value.HasMember("split") && it->value["split"].IsString()) {
        h.split = it->value["split"].GetString();
      }
      if (it->value.HasMember("ms") && it->value["ms"].IsInt64()) {
        h.elapsed_ms = it->value["ms"].GetInt64();
      }
      hints_.emplace(it->name.GetString(), std::move(h));
    }
  }
  // Verdicts only under the exact engine-identity salt (rule F).
  const std::string want_salt = std::format("{:016x}", salt_);
  if (!doc.HasMember("salt") || !doc["salt"].IsString() || want_salt != doc["salt"].GetString()) {
    return;  // prover changed: every cached verdict is stale
  }
  if (doc.HasMember("verdicts") && doc["verdicts"].IsObject()) {
    for (auto it = doc["verdicts"].MemberBegin(); it != doc["verdicts"].MemberEnd(); ++it) {
      if (!it->value.IsObject()) {
        continue;
      }
      Cached_verdict v;
      if (it->value.HasMember("engine") && it->value["engine"].IsString()) {
        v.engine = it->value["engine"].GetString();
      }
      if (it->value.HasMember("detail") && it->value["detail"].IsString()) {
        v.detail = it->value["detail"].GetString();
      }
      if (it->value.HasMember("ms") && it->value["ms"].IsInt64()) {
        v.elapsed_ms = it->value["ms"].GetInt64();
      }
      verdicts_.emplace(it->name.GetString(), std::move(v));
    }
  }
}

std::optional<Cached_verdict> Verdict_cache::lookup(const std::string& key) {
  auto it = verdicts_.find(key);
  if (it == verdicts_.end()) {
    return std::nullopt;
  }
  ++hits_;
  return it->second;
}

void Verdict_cache::insert(const std::string& key, Cached_verdict v) {
  verdicts_[key] = std::move(v);
  dirty_         = true;
  ++stores_;
}

std::optional<Strategy_hint> Verdict_cache::hint(const std::string& entity) const {
  auto it = hints_.find(entity);
  if (it == hints_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void Verdict_cache::set_hint(const std::string& entity, Strategy_hint h) {
  hints_[entity] = std::move(h);
  dirty_         = true;
}

void Verdict_cache::save() const {
  if (!dirty_) {
    return;
  }
  // Deterministic emission (sorted keys) so re-runs produce byte-identical
  // files and the cache diffs cleanly.
  std::vector<std::string> vkeys, hkeys;
  vkeys.reserve(verdicts_.size());
  hkeys.reserve(hints_.size());
  for (const auto& [k, _] : verdicts_) {
    vkeys.push_back(k);
  }
  for (const auto& [k, _] : hints_) {
    hkeys.push_back(k);
  }
  std::sort(vkeys.begin(), vkeys.end());
  std::sort(hkeys.begin(), hkeys.end());

  std::string out;
  out += "{\n";
  out += std::format("  \"schema\": {},\n", kSchema);
  out += std::format("  \"salt\": \"{:016x}\",\n", salt_);
  out += "  \"verdicts\": {\n";
  for (size_t i = 0; i < vkeys.size(); ++i) {
    const auto& v = verdicts_.at(vkeys[i]);
    out += std::format("    \"{}\": {{\"engine\": \"{}\", \"detail\": \"{}\", \"ms\": {}}}{}\n",
                       esc(vkeys[i]), esc(v.engine), esc(v.detail), v.elapsed_ms,
                       i + 1 < vkeys.size() ? "," : "");
  }
  out += "  },\n";
  out += "  \"hints\": {\n";
  for (size_t i = 0; i < hkeys.size(); ++i) {
    const auto& h = hints_.at(hkeys[i]);
    out += std::format("    \"{}\": {{\"engine\": \"{}\", \"split\": \"{}\", \"ms\": {}}}{}\n",
                       esc(hkeys[i]), esc(h.engine), esc(h.split), h.elapsed_ms,
                       i + 1 < hkeys.size() ? "," : "");
  }
  out += "  }\n}\n";

  const std::string path = workdir_ + "/formal_cache.json";
  const std::string tmp  = path + ".tmp";
  {
    std::ofstream of(tmp, std::ios::binary | std::ios::trunc);
    if (!of) {
      return;  // unwritable workdir: the cache is only ever a speedup
    }
    of << out;
  }
  std::rename(tmp.c_str(), path.c_str());
}

}  // namespace livehd::formal
