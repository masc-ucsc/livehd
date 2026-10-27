// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Replay the production whole-color remapper with externally measured IO timing.
// The caller supplies STA boundary times and is responsible for clock/reset
// exclusions. This tool performs no placement or parasitic estimation.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

#include "attrs.hpp"
#include "inou/cgen/cgen_verilog.hpp"
#include "pass/synth/io_resynth.hpp"
#include "rapidjson/document.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"

namespace {
rapidjson::Document read(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot read " + path);
  }
  std::string         contents{std::istreambuf_iterator<char>(input), {}};
  rapidjson::Document d;
  d.Parse(contents.c_str());
  if (d.HasParseError() || !d.IsObject()) {
    throw std::runtime_error("invalid object " + path);
  }
  return d;
}
livehd::synth::Io_environment environment(const rapidjson::Value& d) {
  if (!d.IsObject()) {
    throw std::invalid_argument("environment must be an object");
  }
  livehd::synth::Io_environment env;
  for (auto [field, target] : {
           std::pair{  "inputs",   &env.inputs},
           std::pair{"required", &env.required}
  }) {
    if (!d.HasMember(field)) {
      continue;
    }
    if (!d[field].IsObject()) {
      throw std::runtime_error("invalid timing map");
    }
    for (const auto& m : d[field].GetObject()) {
      if (!m.value.IsNumber() || !std::isfinite(m.value.GetDouble())) {
        throw std::runtime_error("nonfinite boundary time");
      }
      (*target)[m.name.GetString()] = m.value.GetDouble();
    }
  }
  return env;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: synth_io_replay config.json");
    }
    auto       config = read(argv[1]);
    const auto text   = [&](const char* key) -> std::string {
      if (!config.HasMember(key) || !config[key].IsString()) {
        throw std::invalid_argument(std::string("missing ") + key);
      }
      return config[key].GetString();
    };
    const auto output = text("output");
    if (std::filesystem::exists(output)) {
      throw std::runtime_error("output must be a fresh directory");
    }
    if (config.HasMember("period_ps") && !config["period_ps"].IsNumber()) {
      throw std::invalid_argument("period_ps must be numeric");
    }
    const double period = config.HasMember("period_ps") ? config["period_ps"].GetDouble() : 100;
    if (!std::isfinite(period) || period <= 0) {
      throw std::invalid_argument("invalid clock period");
    }
    hhds::GraphLibrary library;
    library.load(text("lg"));
    auto io = library.find_io(text("top"));
    if (!io || !io->get_graph()) {
      throw std::runtime_error("missing top");
    }
    std::map<std::string, std::shared_ptr<hhds::Graph>> regions;
    for (const auto& g : io->get_graph()->definitions().graphs()) {
      if (g->get_input_node().attr(livehd::attrs::synth_region).has()) {
        regions.emplace(std::string(g->get_name()), g);
      }
    }
    if (regions.empty()) {
      throw std::runtime_error("no mapped synthesis regions");
    }
    std::map<std::string, std::string> options{
        {"library",        text("liberty")},
        {  "delay", std::to_string(period)}
    };
    if (config.HasMember("mapper_options")) {
      if (!config["mapper_options"].IsObject()) {
        throw std::invalid_argument("mapper_options must be an object of strings");
      }
      for (const auto& m : config["mapper_options"].GetObject()) {
        if (!m.value.IsString()) {
          throw std::invalid_argument("mapper_options values must be strings");
        }
        options[m.name.GetString()] = m.value.GetString();
      }
    }
    auto                mapper = livehd::synth::make_io_mapper("abc", options);
    rapidjson::Document measurements;
    if (config.HasMember("measurements")) {
      measurements = read(text("measurements"));
    }
    std::filesystem::create_directories(output + "/verilog");
    std::ofstream                                report(output + "/report.json");
    rapidjson::OStreamWrapper                    stream(report);
    rapidjson::Writer<rapidjson::OStreamWrapper> w(stream);
    w.StartObject();
    w.Key("period_ps");
    w.Double(period);
    w.Key("regions");
    w.StartArray();
    bool accepted = false;
    for (const auto& [name, graph] : regions) {
      const auto bindings = mapper.bindings(*graph);
      w.StartObject();
      w.Key("module");
      w.String(name.c_str());
      w.Key("bindings");
      w.StartArray();
      for (const auto& b : bindings) {
        w.StartObject();
        w.Key("port");
        w.String(b.port.c_str());
        w.Key("instance");
        w.String(b.instance.c_str());
        w.Key("pin");
        w.String(b.pin.c_str());
        w.Key("input");
        w.Bool(b.input);
        w.EndObject();
      }
      w.EndArray();
      if (measurements.IsObject() && measurements.HasMember(name.c_str())) {
        const auto& values  = measurements[name.c_str()];
        const auto  env     = environment(values);
        const auto  used    = values.HasMember("used") ? environment(values["used"]) : livehd::synth::Io_environment{};
        const auto  result  = mapper(*graph, env, used, period, 5, true);
        accepted           |= result.accepted;
        w.Key("checked");
        w.Bool(result.checked);
        w.Key("fired");
        w.Bool(result.fired);
        w.Key("accepted");
        w.Bool(result.accepted);
        w.Key("decision");
        w.String(result.decision.c_str());
        w.Key("before_slack_ps");
        w.Double(result.slack_before_ps);
        w.Key("after_slack_ps");
        w.Double(result.slack_after_ps);
        w.Key("tightening_ps");
        w.Double(result.delta_ps);
      }
      w.EndObject();
    }
    w.EndArray();
    w.Key("any_accepted");
    w.Bool(accepted);
    if (accepted && mapper.refine) {
      mapper.refine(library, text("top"));
    }
    // Refinement can rename cells and resize adjacent regions. Export bindings
    // from the final graphs, not the pre-remap snapshots used for decisions.
    w.Key("final_regions");
    w.StartArray();
    for (const auto& [name, graph] : regions) {
      w.StartObject();
      w.Key("module");
      w.String(name.c_str());
      w.Key("bindings");
      w.StartArray();
      for (const auto& b : mapper.bindings(*graph)) {
        w.StartObject();
        w.Key("port");
        w.String(b.port.c_str());
        w.Key("instance");
        w.String(b.instance.c_str());
        w.Key("pin");
        w.String(b.pin.c_str());
        w.Key("input");
        w.Bool(b.input);
        w.EndObject();
      }
      w.EndArray();
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    report.flush();
    if (!report) {
      throw std::runtime_error("report write failed");
    }
    library.save(output + "/lg");
    const std::string verilog_dir = output + "/verilog";
    Cgen_verilog      emit(verilog_dir);
    for (const auto& g : io->get_graph()->definitions().graphs()) {
      emit.do_from_graph(g);
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "synth_io_replay: " << e.what() << '\n';
    return 1;
  }
}
