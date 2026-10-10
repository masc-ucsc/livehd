// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

#include "synth_policy.hpp"
namespace livehd::synth {
class Tune_store {
  std::string                        directory_, export_;
  bool                               persist_;
  bool                               dirty_ = false;
  std::string                        required_;
  std::map<std::string, std::string> decisions_, applied_, pending_;
  std::mutex                         mutex_;
  static void                        atomic(const std::filesystem::path& path, const std::string& data) {
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path());
    }
    auto tmp  = path;
    tmp      += ".tmp";
    {
      std::ofstream out(tmp);
      if (!out || !(out << data << '\n')) {
        throw std::runtime_error("cannot write synthesis tune JSON: " + tmp.string());
      }
    }
    std::filesystem::rename(tmp, path);
  }
  void load(const std::string& file) {
    if (file.empty() || !std::filesystem::exists(file)) {
      return;
    }
    std::ifstream       in(file);
    const std::string   data{std::istreambuf_iterator<char>(in), {}};
    rapidjson::Document d;
    d.Parse(data.c_str());
    if (!d.IsObject() || !d.HasMember("schema_version") || d["schema_version"] != 1 || !d.HasMember("decisions")
        || !d["decisions"].IsObject()) {
      throw std::runtime_error("unsupported synth tune file: " + file);
    }
    for (const auto& m : d["decisions"].GetObject()) {
      if (!m.value.IsObject() || !m.value.HasMember("options") || !m.value["options"].IsObject()) {
        throw std::runtime_error("malformed synth tune decision: " + file);
      }
      const auto& o = m.value["options"];
      for (const auto* key : {"adder", "multiplier", "barrel", "block_size"}) {
        if (!o.HasMember(key)) {
          throw std::runtime_error("incomplete synth tune options: " + file);
        }
        synth_attr::validate(key, synth_attr::json(o[key]));
      }
      decisions_[m.name.GetString()] = synth_attr::json(m.value);
    }
  }
  static std::string object(const std::map<std::string, std::string>& rows) {
    std::string s = "{";
    for (const auto& [k, v] : rows) {
      if (s.size() > 1) {
        s += ",";
      }
      s += synth_attr::quote(k) + ":" + v;
    }
    return s + "}";
  }

public:
  Tune_store(std::string dir, std::string file, std::string exp, bool persist, bool read_store, std::string required = "structural")
      : directory_(std::move(dir)), export_(std::move(exp)), persist_(persist), required_(std::move(required)) {
    if (read_store && persist_ && !directory_.empty()) {
      load(directory_ + "/state.json");
    }
    if (read_store) {
      load(file);
    }
  }
  std::string find(const std::string& key) {
    std::lock_guard lock(mutex_);
    auto            it = decisions_.find(key);
    if (it == decisions_.end()) {
      return {};
    }
    rapidjson::Document d;
    d.Parse(it->second.c_str());
    const std::string level = d.HasMember("validation") && d["validation"].IsString() ? d["validation"].GetString() : "structural";
    if (required_ != "structural" && level != required_ && level != "design") {
      return {};
    }
    return it->second;
  }
  void put(const std::string& key, std::string row) {
    std::lock_guard lock(mutex_);
    if (required_ == "structural") {
      decisions_[key] = std::move(row);
      dirty_          = true;
    } else {
      pending_[key] = std::move(row);
    }
  }
  void authorize(const std::string& key, bool proven) {
    std::lock_guard lock(mutex_);
    auto            it = pending_.find(key);
    if (it == pending_.end()) {
      return;
    }
    rapidjson::Document d;
    d.Parse(it->second.c_str());
    d["validation"].SetString(proven ? required_.c_str() : "unknown", d.GetAllocator());
    const auto row = synth_attr::json(d);
    if (proven) {
      decisions_[key] = row;
      dirty_          = true;
    }
    if (d.HasMember("region")) {
      auto&               applied = applied_[d["region"].GetString()];
      rapidjson::Document current;
      current.Parse(applied.c_str());
      if (current.IsObject()) {
        auto& alloc = current.GetAllocator();
        if (!current.HasMember("validation")) {
          current.AddMember("validation", rapidjson::Value(), alloc);
        }
        current["validation"].SetString(proven ? required_.c_str() : "unknown", alloc);
        applied = synth_attr::json(current);
      } else {
        applied = row;
      }
    }
    pending_.erase(it);
  }
  void authorize_all(bool proven) {
    std::vector<std::string> keys;
    {
      std::lock_guard lock(mutex_);
      for (const auto& [key, _] : pending_) {
        keys.push_back(key);
      }
    }
    for (const auto& key : keys) {
      authorize(key, proven);
    }
  }
  void applied(std::string name, std::string row) {
    std::lock_guard lock(mutex_);
    applied_[std::move(name)] = std::move(row);
  }
  void history(std::string row) {
    std::lock_guard lock(mutex_);
    if (!persist_ || directory_.empty()) {
      return;
    }
    std::filesystem::create_directories(directory_);
    std::ofstream out(directory_ + "/history.jsonl", std::ios::app);
    if (!(out << row << '\n')) {
      throw std::runtime_error("cannot append synthesis tuning history");
    }
  }
  void export_selection(const std::string& file) {
    std::lock_guard lock(mutex_);
    auto            rows = decisions_;
    for (const auto& [key, value] : pending_) {
      rows[key] = value;
    }
    atomic(file, "{\"schema_version\":1,\"decisions\":" + object(rows) + "}");
  }
  void save() {
    std::lock_guard lock(mutex_);
    const auto      state = "{\"schema_version\":1,\"decisions\":" + object(decisions_) + "}";
    if (!directory_.empty()) {
      atomic(directory_ + "/applied.json", "{\"schema_version\":1,\"regions\":" + object(applied_) + "}");
      if (persist_ && dirty_) {
        atomic(directory_ + "/state.json", state);
      }
    }
    if (!export_.empty()) {
      atomic(export_, state);
    }
  }
};
}  // namespace livehd::synth
