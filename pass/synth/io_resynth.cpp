// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "io_resynth.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "semdiff.hpp"

namespace livehd::synth {
namespace {
std::map<std::string, Io_mapper_factory>& factories() {
  static std::map<std::string, Io_mapper_factory> value;
  return value;
}
std::mutex& factory_mutex() {
  static std::mutex value;
  return value;
}
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;
void require_state(bool valid) {
  if (!valid) {
    throw std::runtime_error("invalid placement synthesis state");
  }
}
void times(Writer& w, const std::map<std::string, double>& values) {
  w.StartObject();
  for (const auto& [name, time] : values) {
    w.Key(name.c_str());
    w.Double(time);
  }
  w.EndObject();
}
void wire_json(Writer& w, const Wire_load& wire) {
  w.StartObject();
  w.Key("capacitance_ff_um");
  w.Double(wire.capacitance_ff_um);
  w.Key("resistance_ohm_um");
  w.Double(wire.resistance_ohm_um);
  w.Key("fanout_length_um");
  w.StartObject();
  for (const auto& [fanout, length] : wire.length_um) {
    w.Key(std::to_string(fanout).c_str());
    w.Double(length);
  }
  w.EndObject();
  w.Key("samples");
  w.StartObject();
  for (const auto& [fanout, count] : wire.samples) {
    w.Key(std::to_string(fanout).c_str());
    w.Uint(count);
  }
  w.EndObject();
  w.EndObject();
}
void env_json(Writer& w, const Io_environment& env) {
  w.StartObject();
  w.Key("in");
  times(w, env.inputs);
  w.Key("required");
  times(w, env.required);
  w.Key("wire");
  wire_json(w, env.wire);
  w.EndObject();
}
std::map<std::string, double> read_times(const rapidjson::Value& object) {
  std::map<std::string, double> result;
  if (!object.IsObject()) {
    throw std::runtime_error("invalid IO timing object");
  }
  for (const auto& m : object.GetObject()) {
    if (!m.value.IsNumber() || !std::isfinite(m.value.GetDouble())) {
      throw std::runtime_error("invalid IO timing value");
    }
    result[m.name.GetString()] = m.value.GetDouble();
  }
  return result;
}
Wire_load read_wire(const rapidjson::Value& v) {
  Wire_load result;
  require_state(v.IsObject() && v.HasMember("capacitance_ff_um") && v["capacitance_ff_um"].IsNumber()
                && v.HasMember("resistance_ohm_um") && v["resistance_ohm_um"].IsNumber() && v.HasMember("fanout_length_um"));
  result.capacitance_ff_um = v["capacitance_ff_um"].GetDouble();
  result.resistance_ohm_um = v["resistance_ohm_um"].GetDouble();
  require_state(std::isfinite(result.capacitance_ff_um) && result.capacitance_ff_um >= 0 && std::isfinite(result.resistance_ohm_um)
                && result.resistance_ohm_um >= 0);
  for (const auto& [name, length] : read_times(v["fanout_length_um"])) {
    size_t     end = 0;
    const auto n   = std::stoul(name, &end);
    require_state(end == name.size() && n > 0 && n <= 100000000 && length >= 0);
    result.length_um[n] = length;
  }
  if (v.HasMember("samples")) {
    require_state(v["samples"].IsObject());
    for (const auto& m : v["samples"].GetObject()) {
      require_state(m.value.IsUint());
      result.samples[std::stoul(m.name.GetString())] = m.value.GetUint();
    }
  }
  return result;
}
Io_environment read_env(const rapidjson::Value& v) {
  Io_environment result;
  require_state(v.IsObject() && v.HasMember("in") && v.HasMember("required"));
  result.inputs   = read_times(v["in"]);
  result.required = read_times(v["required"]);
  if (v.HasMember("wire")) {
    result.wire = read_wire(v["wire"]);
  }
  return result;
}
rapidjson::Document read_json(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::string         text((std::istreambuf_iterator<char>(input)), {});
  rapidjson::Document d;
  d.Parse(text.c_str());
  if (d.HasParseError() || !d.IsObject() || !d.HasMember("schema") || !d["schema"].IsInt() || d["schema"].GetInt() != 1) {
    throw std::runtime_error("invalid placement synthesis state: " + path);
  }
  return d;
}
void publish(const std::string& path, const rapidjson::StringBuffer& buffer) {
  const auto temporary = path + ".tmp";
  {
    std::ofstream out(temporary);
    if (!(out << buffer.GetString() << '\n')) {
      throw std::runtime_error("cannot save placement synthesis state");
    }
  }
  std::filesystem::rename(temporary, path);
}
}  // namespace
bool register_io_mapper(std::string name, Io_mapper_factory factory) {
  std::lock_guard lock(factory_mutex());
  return factories().emplace(std::move(name), std::move(factory)).second;
}
Io_mapper make_io_mapper(const std::string& name, const std::map<std::string, std::string>& options) {
  Io_mapper_factory factory;
  {
    std::lock_guard lock(factory_mutex());
    const auto      found = factories().find(name);
    if (found == factories().end()) {
      throw std::runtime_error("no placement synthesis backend: " + name);
    }
    factory = found->second;
  }
  return factory(options);
}
int  io_stage_rank(const std::string& stage) { return stage == "final" ? 3 : stage == "macro" ? 2 : stage == "fp" ? 1 : 0; }
bool io_trigger(double slack, double tightening, double period, double percent) {
  return percent > 0 && period > 0 && std::isfinite(slack) && std::isfinite(tightening) && slack <= period * percent / 100
         && tightening > period * percent / 100;
}
std::string mapped_identity(hhds::Graph& graph) {
  const auto d = semdiff::canonical_digest(&graph, {}, semdiff::Sub_fold::interface, true);
  if (!d.valid) {
    throw std::runtime_error("cannot identify mapped color: " + std::string(graph.get_name()));
  }
  return std::format("{:016x}{:016x}", d.h0, d.h1);
}
std::string describe_environment(const Io_environment& env) {
  rapidjson::StringBuffer buffer;
  Writer                  w(buffer);
  env_json(w, env);
  return std::string(buffer.GetString(), buffer.GetSize());
}
std::string environment_identity(const Io_environment& env) {
  const auto description = describe_environment(env);
  uint64_t   hash        = 14695981039346656037ULL;
  for (const unsigned char c : description) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return std::format("{:016x}", hash);
}
Io_store::Io_store(std::string dir, double period, bool persistent, std::string timing_identity)
    : directory(std::move(dir)), period_ps(period), persistent_(persistent), timing_identity_(std::move(timing_identity)) {
  run_id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
  if (directory.empty() || !std::isfinite(period) || period <= 0) {
    throw std::invalid_argument("IO resynthesis needs a workdir and clock period");
  }
  if (!persistent_) {
    return;
  }
  auto timing = read_json(directory + "/io_timing.json");
  if (!timing.IsNull()) {
    require_state(timing.HasMember("period_ps") && timing["period_ps"].IsNumber() && timing.HasMember("regions")
                  && timing["regions"].IsObject());
  }
  if (!timing.IsNull() && timing["period_ps"].GetDouble() == period) {
    if (timing.HasMember("timing_engine")) {
      require_state(timing["timing_engine"].IsString());
    }
    const bool compatible = timing_identity_.empty()
                            || (timing.HasMember("timing_engine") && timing["timing_engine"].GetString() == timing_identity_);
    for (const auto& m : timing["regions"].GetObject()) {
      const auto& v = m.value;
      require_state(v.IsObject() && v.HasMember("netlist") && v["netlist"].IsString() && v.HasMember("stage")
                    && v["stage"].IsString() && v.HasMember("run") && v["run"].IsString() && v.HasMember("used")
                    && v.HasMember("placed"));
      rows[m.name.GetString()]
          = {v["netlist"].GetString(), v["stage"].GetString(), v["run"].GetString(), read_env(v["used"]), read_env(v["placed"])};
      if (!compatible) {
        // Keep the constraints actually used to map this body, but never
        // trigger a pre-placement remap from an obsolete timer's measurement.
        auto& row = rows.at(m.name.GetString());
        row.stage.clear();
        row.placed = {};
      }
    }
  }
  auto wire = read_json(directory + "/wire_load.json");
  if (!wire.IsNull()) {
    require_state(wire.HasMember("global") && wire.HasMember("regions") && wire["regions"].IsObject());
    global_wire = read_wire(wire["global"]);
    for (const auto& m : wire["regions"].GetObject()) {
      wires[m.name.GetString()] = read_wire(m.value);
    }
  }
}
void Io_store::invalidate(const std::string& module) {
  std::lock_guard lock(mapping_mutex_);
  rows.erase(module);
}
void Io_store::mapping(const std::string& module, Mapping value) {
  std::lock_guard lock(mapping_mutex_);
  mappings[module] = std::move(value);
}
bool Io_store::record(const std::string& module, std::string identity, std::string stage, Io_environment env) {
  auto& row = rows[module];
  if (row.netlist != identity) {
    row = {};
  }
  if (row.run == run_id && io_stage_rank(row.stage) > io_stage_rank(stage)) {
    return false;
  }
  row.netlist = std::move(identity);
  row.stage   = std::move(stage);
  row.run     = run_id;
  row.placed  = std::move(env);
  if (!row.placed.wire.empty()) {
    wires[module] = row.placed.wire;
  }
  return true;
}
void Io_store::used(const std::string& module, std::string identity, const Io_environment& env) {
  auto& row = rows[module];
  if (row.netlist != identity) {
    row.stage.clear();
    row.placed = {};
  }
  row.netlist = std::move(identity);
  row.used    = env;
}
const Wire_load& Io_store::wire(const std::string& module) const {
  const auto found = wires.find(module);
  return found == wires.end() ? global_wire : found->second;
}
void Io_store::save() const {
  if (!persistent_) {
    return;
  }
  std::filesystem::create_directories(directory);
  rapidjson::StringBuffer timing;
  Writer                  t(timing);
  t.StartObject();
  t.Key("schema");
  t.Int(1);
  t.Key("period_ps");
  t.Double(period_ps);
  t.Key("timing_engine");
  t.String(timing_identity_.c_str());
  t.Key("regions");
  t.StartObject();
  for (const auto& [module, row] : rows) {
    t.Key(module.c_str());
    t.StartObject();
    t.Key("netlist");
    t.String(row.netlist.c_str());
    t.Key("stage");
    t.String(row.stage.c_str());
    t.Key("run");
    t.String(row.run.c_str());
    t.Key("used");
    env_json(t, row.used);
    t.Key("placed");
    env_json(t, row.placed);
    t.EndObject();
  }
  t.EndObject();
  t.EndObject();
  publish(directory + "/io_timing.json", timing);
  rapidjson::StringBuffer wire;
  Writer                  w(wire);
  w.StartObject();
  w.Key("schema");
  w.Int(1);
  w.Key("global");
  wire_json(w, global_wire);
  w.Key("regions");
  w.StartObject();
  for (const auto& [module, model] : wires) {
    w.Key(module.c_str());
    wire_json(w, model);
  }
  w.EndObject();
  w.EndObject();
  publish(directory + "/wire_load.json", wire);
}
}  // namespace livehd::synth
