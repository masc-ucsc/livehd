// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

#include "core/metadata.hpp"
#include "file_utils.hpp"
#include "synth_policy.hpp"
namespace livehd::synth {
class Tune_store {
  std::string                        directory_, export_;
  metadata::Bundle                   metadata_;
  std::string                        design_;
  bool                               persist_;
  bool                               imported_ = false;
  std::string                        required_;
  using Rows = std::map<std::string, std::string>;
  Rows                               decisions_, applied_, pending_, updates_;
  std::mutex                         mutex_;
  static void                        atomic(const std::filesystem::path& path, const std::string& data) {
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path());
    }
    if (const auto error = file_utils::write_file_atomic(path.string(), data + '\n'); error) {
      throw std::system_error(error, "writing synthesis tune JSON: " + path.string());
    }
  }
  void lock_store(file_utils::File_lock& lock) const {
    std::string why;
    if (!lock.open(directory_ + "/tune.lock", why) || !lock.exclusive()) {
      throw std::runtime_error(why.empty() ? "cannot lock synthesis tune store: " + directory_ : why);
    }
  }
  static Rows load_file(const std::string& file, bool recover = false) {
    if (file.empty() || !std::filesystem::exists(file)) {
      return {};
    }
    const auto data = file_utils::read_file(file);
    if (!data) {
      throw std::runtime_error("cannot read synth tune file: " + file);
    }
    rapidjson::Document d;
    d.Parse(data->data(), data->size());
    try {
      return load_record(d, file);
    } catch (const std::invalid_argument&) {
      if (!recover) {
        throw;
      }
    }
    // Shared state is restartable. Explicit selections and metadata still
    // fail on malformed input, and the damaged state is kept for diagnosis.
    std::filesystem::rename(file, file + ".bad");
    return {};
  }
  static Rows load_record(const rapidjson::Value& d, const std::string& file) {
    Rows rows;
    if (!d.IsObject() || !d.HasMember("schema_version") || d["schema_version"] != 1 || !d.HasMember("decisions")
        || !d["decisions"].IsObject()) {
      throw std::invalid_argument("unsupported synth tune file: " + file);
    }
    for (const auto& m : d["decisions"].GetObject()) {
      if (!m.value.IsObject() || !m.value.HasMember("options") || !m.value["options"].IsObject()) {
        throw std::invalid_argument("malformed synth tune decision: " + file);
      }
      const auto& o = m.value["options"];
      for (const auto* key : {"adder", "multiplier", "barrel", "block_size"}) {
        if (!o.HasMember(key)) {
          throw std::invalid_argument("incomplete synth tune options: " + file);
        }
        synth_attr::validate(key, synth_attr::json(o[key]));
      }
      rows[m.name.GetString()] = metadata::canonical(m.value);
    }
    return rows;
  }
  static void overlay(Rows& base, Rows changes) {
    base.merge(changes);
    for (auto& [key, row] : changes) {
      base[key] = std::move(row);
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
  Tune_store(std::string dir, std::string file, std::string exp, bool persist, bool read_store, std::string required = "structural",
             std::string metadata_json = "[]", std::string design = "")
      : directory_(std::move(dir))
      , export_(std::move(exp))
      , metadata_(metadata_json)
      , design_(std::move(design))
      , persist_(persist)
      , required_(std::move(required)) {
    if (read_store && persist_ && !directory_.empty()) {
      file_utils::File_lock lock;
      lock_store(lock);
      decisions_ = load_file(directory_ + "/state.json", /*recover=*/true);
    }
    if (read_store) {
      std::map<std::string, std::string> imported;
      for (const auto* record : metadata_.select("livehd.synth.tuning", design_)) {
        if (!record->HasMember("decisions") || !(*record)["decisions"].IsObject()) {
          throw std::runtime_error("synthesis tuning metadata needs decisions");
        }
        for (const auto& row : (*record)["decisions"].GetObject()) {
          const auto [it, added] = imported.emplace(row.name.GetString(), metadata::canonical(row.value));
          if (!added && it->second != metadata::canonical(row.value)) {
            throw std::runtime_error("conflicting synthesis tuning decision: " + std::string(row.name.GetString()));
          }
        }
        overlay(decisions_, load_record(*record, "livehd.synth.tuning"));
        imported_ = true;
      }
      // Internal validated trial selection supersedes the imported incumbent.
      overlay(decisions_, load_file(file));
    }
  }
  bool        imported() const { return imported_; }
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
      decisions_[key] = row;
      updates_[key]   = std::move(row);
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
      updates_[key]   = row;
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
    file_utils::File_lock disk_lock;
    lock_store(disk_lock);
    if (const auto error = file_utils::append_file(directory_ + "/history.jsonl", row + '\n', /*repair_partial_line=*/true);
        error) {
      throw std::system_error(error, "appending synthesis tuning history");
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
      file_utils::File_lock disk_lock;
      lock_store(disk_lock);
      atomic(directory_ + "/applied.json", "{\"schema_version\":1,\"regions\":" + object(applied_) + "}");
      if (persist_ && !updates_.empty()) {
        const auto path   = directory_ + "/state.json";
        auto       shared = load_file(path, /*recover=*/true);
        overlay(shared, updates_);
        atomic(path, "{\"schema_version\":1,\"decisions\":" + object(shared) + "}");
        updates_.clear();
      }
    }
    if (!export_.empty()) {
      rapidjson::Document record;
      record.Parse(state.c_str());
      auto& a = record.GetAllocator();
      record.AddMember("class", "livehd.synth.tuning", a);
      record.AddMember("id", "selection", a);
      record.AddMember("design", rapidjson::Value(design_.c_str(), a), a);
      metadata_.erase_selected("livehd.synth.tuning", design_);
      metadata_.replace(record);
      metadata_.write(export_);
    }
  }
};
}  // namespace livehd::synth
