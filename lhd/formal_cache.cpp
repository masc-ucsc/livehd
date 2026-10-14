// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "formal_cache.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

#include "file_utils.hpp"
#include "json_util.hpp"
#include "rapidjson/document.h"

namespace livehd::formal {

namespace {

constexpr int kSchema = 1;

std::string esc(std::string_view text) { return json_util::escape(text); }

}  // namespace

Verdict_cache::Verdict_cache(std::string workdir, uint64_t salt) : workdir_(std::move(workdir)), salt_(salt) {
  const auto content = livehd::file_utils::read_file(workdir_ + "/formal_cache.json");
  if (!content) {
    return;
  }
  rapidjson::Document doc;
  doc.Parse(content->c_str());
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
  // The known-timeout ledger. A record must NAME its cause and that cause must
  // be a time limit: the legacy "unknowns" section (no cause field) ledgered
  // every undecided cause -- an encoder refusal, an unpaired correspondence --
  // and replaying one of those as "known timeout" would turn an exit-7 refusal
  // into an exit-0 warning. Such records are dropped, never trusted.
  if (doc.HasMember("timeouts") && doc["timeouts"].IsObject()) {
    for (auto it = doc["timeouts"].MemberBegin(); it != doc["timeouts"].MemberEnd(); ++it) {
      if (!it->value.IsObject() || !it->value.HasMember("cause") || !it->value["cause"].IsString()
          || std::string_view{it->value["cause"].GetString()} != kTimeoutLedgerCause) {
        continue;
      }
      Timeout_attempt a;
      if (it->value.HasMember("timeout") && it->value["timeout"].IsInt()) {
        a.timeout = it->value["timeout"].GetInt();
      }
      if (it->value.HasMember("ms") && it->value["ms"].IsInt64()) {
        a.elapsed_ms = it->value["ms"].GetInt64();
      }
      timeouts_.emplace(it->name.GetString(), a);
    }
  }
  // Cone verdicts: a bare digest list (the digest IS the whole claim), inside
  // the salt gate like the def-pair verdicts.
  if (doc.HasMember("cones") && doc["cones"].IsArray()) {
    for (const auto& d : doc["cones"].GetArray()) {
      if (d.IsString()) {
        cones_.insert(d.GetString());
      }
    }
    cone_hits_ = static_cast<int>(cones_.size());
  }
  if (doc.HasMember("verdicts") && doc["verdicts"].IsObject()) {
    for (auto it = doc["verdicts"].MemberBegin(); it != doc["verdicts"].MemberEnd(); ++it) {
      if (!it->value.IsObject()) {
        continue;
      }
      // Older records omitted proof scope and cannot safely be replayed.
      if (!it->value.HasMember("bounded") || !it->value["bounded"].IsBool()) {
        continue;
      }
      Cached_verdict v;
      v.bounded = it->value["bounded"].GetBool();
      // A bounded proof replays as PROVEN-bounded(n): without its depth the
      // replay cannot say how deep the original run checked, so drop it.
      if (v.bounded) {
        if (!it->value.HasMember("depth") || !it->value["depth"].IsInt() || it->value["depth"].GetInt() <= 0) {
          continue;
        }
        v.depth = it->value["depth"].GetInt();
      }
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
  auto            it = verdicts_.find(key);
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
  auto            it = hints_.find(entity);
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
  auto            it = pair_hints_.find(entity);
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

void Verdict_cache::clear_pair_hint(const std::string& entity) {
  std::lock_guard lock(mutex_);
  if (pair_hints_.erase(entity) > 0) {
    dirty_ = true;
  }
}

bool Verdict_cache::skip_timeout(const std::string& key, int timeout_s) const {
  std::lock_guard lock(mutex_);
  auto            it = timeouts_.find(key);
  if (it == timeouts_.end()) {
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

void Verdict_cache::note_timeout(const std::string& key, Timeout_attempt a) {
  std::lock_guard lock(mutex_);
  auto [it, inserted] = timeouts_.try_emplace(key, a);
  if (!inserted) {
    // Keep the LARGEST budget that ran out of time (0 = unbounded wins).
    // NOTE: operator[] would default-construct timeout=0 == "unbounded" and
    // wrongly suppress future bigger-budget retries — hence try_emplace.
    auto& rec = it->second;
    if (rec.timeout != 0 && (a.timeout == 0 || a.timeout >= rec.timeout)) {
      rec = a;
    }
  }
  dirty_ = true;
}

void Verdict_cache::note_cone_proven(const std::string& digest) {
  std::lock_guard lock(mutex_);
  if (digest.empty()) {
    return;
  }
  if (cones_.insert(digest).second) {
    dirty_ = true;
  }
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
    const auto& v  = verdicts_.at(vkeys[i]);
    out           += std::format("    \"{}\": {{\"engine\": \"{}\", \"detail\": \"{}\", \"ms\": {}, \"bounded\": {}{}}}{}\n",
                                 esc(vkeys[i]),
                                 esc(v.engine),
                                 esc(v.detail),
                                 v.elapsed_ms,
                                 v.bounded,
                                 v.bounded ? std::format(", \"depth\": {}", v.depth) : std::string{},
                                 i + 1 < vkeys.size() ? "," : "");
  }
  out += "  },\n";
  out += "  \"timeouts\": {\n";
  {
    std::vector<std::string> tkeys;
    tkeys.reserve(timeouts_.size());
    for (const auto& [k, _] : timeouts_) {
      tkeys.push_back(k);
    }
    std::sort(tkeys.begin(), tkeys.end());
    for (size_t i = 0; i < tkeys.size(); ++i) {
      const auto& a  = timeouts_.at(tkeys[i]);
      out            += std::format("    \"{}\": {{\"cause\": \"{}\", \"timeout\": {}, \"ms\": {}}}{}\n",
                                    esc(tkeys[i]),
                                    kTimeoutLedgerCause,
                                    a.timeout,
                                    a.elapsed_ms,
                                    i + 1 < tkeys.size() ? "," : "");
    }
  }
  out += "  },\n";
  out += "  \"hints\": {\n";
  for (size_t i = 0; i < hkeys.size(); ++i) {
    const auto& h  = hints_.at(hkeys[i]);
    out            += std::format("    \"{}\": {{\"engine\": \"{}\", \"split\": \"{}\", \"ms\": {}}}{}\n",
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
      // The producer's pair order follows hash-map iteration — sort here so
      // re-runs emit byte-identical files (the section's stated contract).
      auto pairs = pair_hints_.at(pkeys[i]).pairs;
      std::sort(pairs.begin(), pairs.end());
      std::string ps;
      for (size_t j = 0; j < pairs.size(); ++j) {
        ps += std::format("[\"{}\", \"{}\"]{}", esc(pairs[j].first), esc(pairs[j].second), j + 1 < pairs.size() ? ", " : "");
      }
      out += std::format("    \"{}\": {{\"pairs\": [{}]}}{}\n", esc(pkeys[i]), ps, i + 1 < pkeys.size() ? "," : "");
    }
  }
  out += "  },\n";
  out += "  \"cones\": [\n";
  {
    std::vector<std::string> ckeys(cones_.begin(), cones_.end());
    std::sort(ckeys.begin(), ckeys.end());
    for (size_t i = 0; i < ckeys.size(); ++i) {
      out += std::format("    \"{}\"{}\n", esc(ckeys[i]), i + 1 < ckeys.size() ? "," : "");
    }
  }
  out += "  ]\n}\n";

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
