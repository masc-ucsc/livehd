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
  // Pair hints travel with the strategy hints: they survive a salt mismatch
  // (they are re-validated at injection and keep the uncertain discipline, so
  // a prover change never makes them unsound — just possibly stale).
  if (doc.HasMember("pair_hints") && doc["pair_hints"].IsObject()) {
    for (auto it = doc["pair_hints"].MemberBegin(); it != doc["pair_hints"].MemberEnd(); ++it) {
      if (!it->value.IsObject() || !it->value.HasMember("pairs") || !it->value["pairs"].IsArray()) {
        continue;
      }
      Pair_hint h;
      for (const auto& p : it->value["pairs"].GetArray()) {
        if (p.IsArray() && p.Size() == 2 && p[0].IsString() && p[1].IsString()) {
          h.pairs.emplace_back(p[0].GetString(), p[1].GetString());
        }
      }
      if (!h.pairs.empty()) {
        pair_hints_.emplace(it->name.GetString(), std::move(h));
      }
    }
  }
  // Verdicts only under the exact engine-identity salt (rule F).
  const std::string want_salt = std::format("{:016x}", salt_);
  if (!doc.HasMember("salt") || !doc["salt"].IsString() || want_salt != doc["salt"].GetString()) {
    return;  // prover changed: every cached verdict is stale
  }
  if (doc.HasMember("unknowns") && doc["unknowns"].IsObject()) {
    for (auto it = doc["unknowns"].MemberBegin(); it != doc["unknowns"].MemberEnd(); ++it) {
      if (!it->value.IsObject()) {
        continue;
      }
      Unknown_attempt a;
      if (it->value.HasMember("timeout") && it->value["timeout"].IsInt()) {
        a.timeout = it->value["timeout"].GetInt();
      }
      if (it->value.HasMember("ms") && it->value["ms"].IsInt64()) {
        a.elapsed_ms = it->value["ms"].GetInt64();
      }
      unknowns_.emplace(it->name.GetString(), a);
    }
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
  std::lock_guard lock(mutex_);
  auto it = verdicts_.find(key);
  if (it == verdicts_.end()) {
    return std::nullopt;
  }
  ++hits_;
  return it->second;
}

void Verdict_cache::insert(const std::string& key, Cached_verdict v) {
  std::lock_guard lock(mutex_);
  verdicts_[key] = std::move(v);
  dirty_         = true;
  ++stores_;
}

std::optional<Strategy_hint> Verdict_cache::hint(const std::string& entity) const {
  std::lock_guard lock(mutex_);
  auto it = hints_.find(entity);
  if (it == hints_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void Verdict_cache::set_hint(const std::string& entity, Strategy_hint h) {
  std::lock_guard lock(mutex_);
  hints_[entity] = std::move(h);
  dirty_         = true;
}

std::optional<Pair_hint> Verdict_cache::pair_hint(const std::string& entity) const {
  std::lock_guard lock(mutex_);
  auto it = pair_hints_.find(entity);
  if (it == pair_hints_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void Verdict_cache::set_pair_hint(const std::string& entity, Pair_hint h) {
  std::lock_guard lock(mutex_);
  pair_hints_[entity] = std::move(h);
  dirty_              = true;
}

bool Verdict_cache::skip_unknown(const std::string& key, int timeout_s) const {
  std::lock_guard lock(mutex_);
  auto it = unknowns_.find(key);
  if (it == unknowns_.end()) {
    return false;
  }
  // A re-attempt only makes sense with MORE budget than the recorded attempt
  // had: 0 = unbounded dominates everything; otherwise skip when the current
  // budget is no larger.
  const bool skip = it->second.timeout == 0 || (timeout_s != 0 && timeout_s <= it->second.timeout);
  if (skip) {
    ++skips_;
  }
  return skip;
}

void Verdict_cache::note_unknown(const std::string& key, Unknown_attempt a) {
  std::lock_guard lock(mutex_);
  auto [it, inserted] = unknowns_.try_emplace(key, a);
  if (!inserted) {
    // Keep the LARGEST budget that came back Unknown (0 = unbounded wins).
    // NOTE: operator[] would default-construct timeout=0 == "unbounded" and
    // wrongly suppress future bigger-budget retries — hence try_emplace.
    auto& rec = it->second;
    if (rec.timeout != 0 && (a.timeout == 0 || a.timeout >= rec.timeout)) {
      rec = a;
    }
  }
  dirty_ = true;
}

void Verdict_cache::save() const {
  std::lock_guard lock(mutex_);
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
                                 esc(vkeys[i]),
                                 esc(v.engine),
                                 esc(v.detail),
                                 v.elapsed_ms,
                       i + 1 < vkeys.size() ? "," : "");
  }
  out += "  },\n";
  out += "  \"unknowns\": {\n";
  {
    std::vector<std::string> ukeys;
    ukeys.reserve(unknowns_.size());
    for (const auto& [k, _] : unknowns_) {
      ukeys.push_back(k);
    }
    std::sort(ukeys.begin(), ukeys.end());
    for (size_t i = 0; i < ukeys.size(); ++i) {
      const auto& a = unknowns_.at(ukeys[i]);
      out += std::format("    \"{}\": {{\"timeout\": {}, \"ms\": {}}}{}\n",
                                   esc(ukeys[i]),
                                   a.timeout,
                                   a.elapsed_ms,
                                   i + 1 < ukeys.size() ? "," : "");
    }
  }
  out += "  },\n";
  out += "  \"hints\": {\n";
  for (size_t i = 0; i < hkeys.size(); ++i) {
    const auto& h = hints_.at(hkeys[i]);
    out += std::format("    \"{}\": {{\"engine\": \"{}\", \"split\": \"{}\", \"ms\": {}}}{}\n",
                                 esc(hkeys[i]),
                                 esc(h.engine),
                                 esc(h.split),
                                 h.elapsed_ms,
                       i + 1 < hkeys.size() ? "," : "");
  }
  out += "  },\n";
  out += "  \"pair_hints\": {\n";
  {
    std::vector<std::string> pkeys;
    pkeys.reserve(pair_hints_.size());
    for (const auto& [k, _] : pair_hints_) {
      pkeys.push_back(k);
    }
    std::sort(pkeys.begin(), pkeys.end());
    for (size_t i = 0; i < pkeys.size(); ++i) {
      const auto& h = pair_hints_.at(pkeys[i]);
      std::string ps;
      for (size_t j = 0; j < h.pairs.size(); ++j) {
        ps += std::format("[\"{}\", \"{}\"]{}", esc(h.pairs[j].first), esc(h.pairs[j].second), j + 1 < h.pairs.size() ? ", " : "");
      }
      out += std::format("    \"{}\": {{\"pairs\": [{}]}}{}\n", esc(pkeys[i]), ps, i + 1 < pkeys.size() ? "," : "");
    }
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
