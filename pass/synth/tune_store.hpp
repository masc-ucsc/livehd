// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include "synth_policy.hpp"
namespace livehd::synth {
class Tune_store {
  std::string directory_, export_;
  bool persist_;
  std::map<std::string,std::string> decisions_, applied_;
  std::mutex mutex_;
  static void atomic(const std::filesystem::path& path,const std::string& data) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto tmp=path; tmp+=".tmp";
    {std::ofstream out(tmp);if (!out||!(out<<data<<'\n')) throw std::runtime_error("cannot write synthesis tune JSON: "+tmp.string());}
    std::filesystem::rename(tmp,path);
  }
  void load(const std::string& file) {
    if (file.empty()||!std::filesystem::exists(file)) return;
    std::ifstream in(file);const std::string data{std::istreambuf_iterator<char>(in),{}};
    rapidjson::Document d;d.Parse(data.c_str());
    if (!d.IsObject()||!d.HasMember("schema_version")||d["schema_version"]!=1||!d.HasMember("decisions")||!d["decisions"].IsObject()) throw std::runtime_error("unsupported synth tune file: "+file);
    for (const auto& m:d["decisions"].GetObject()) decisions_[m.name.GetString()]=synth_attr::json(m.value);
  }
  static std::string object(const std::map<std::string,std::string>& rows) {
    std::string s="{";for (const auto& [k,v]:rows) {if(s.size()>1)s+=",";s+=synth_attr::quote(k)+":"+v;}return s+"}";
  }
public:
  Tune_store(std::string dir,std::string file,std::string exp,bool persist,bool read_store)
    :directory_(std::move(dir)),export_(std::move(exp)),persist_(persist) {
    if (read_store && persist_ && !directory_.empty()) load(directory_+"/state.json");
    if (read_store) load(file);
  }
  std::string find(const std::string& key) {
    std::lock_guard lock(mutex_);auto it=decisions_.find(key);return it==decisions_.end()?std::string{}:it->second;
  }
  void put(const std::string& key,std::string row) {std::lock_guard lock(mutex_);decisions_[key]=std::move(row);}
  void applied(std::string name,std::string row) {std::lock_guard lock(mutex_);applied_[std::move(name)]=std::move(row);}
  void history(std::string row) {
    std::lock_guard lock(mutex_);
    if (!persist_||directory_.empty()) return;
    std::filesystem::create_directories(directory_);
    std::ofstream out(directory_+"/history.jsonl",std::ios::app);
    if (!(out<<row<<'\n')) throw std::runtime_error("cannot append synthesis tuning history");
  }
  void save() {
    std::lock_guard lock(mutex_);
    const auto state="{\"schema_version\":1,\"decisions\":"+object(decisions_)+"}";
    if (!directory_.empty()) {
      atomic(directory_+"/applied.json","{\"schema_version\":1,\"regions\":"+object(applied_)+"}");
      if (persist_) atomic(directory_+"/state.json",state);
    }
    if (!export_.empty()) atomic(export_,state);
  }
};
} // namespace livehd::synth
