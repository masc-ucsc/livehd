// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Offline domino stage timing (2d-domplace E): the lhd synth.place.stage_timing
// report on a saved placement, with coefficient overrides for sweeps that
// must not re-place. Never synthesizes or places the input.
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "attrs.hpp"
#include "core/physical_rc.hpp"
#include "hhds/graph.hpp"
#include "inou/lefdef/mapped_graph.hpp"
#include "pass/domplace/timing/stage_timing.hpp"
#include "rapidjson/document.h"

namespace stage    = livehd::domplace::timing;
namespace physical = livehd::physical;
namespace {
std::string read(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read " + path);
  }
  return {std::istreambuf_iterator<char>(input), {}};
}
std::optional<std::string> text(const rapidjson::Value& object, const char* key) {
  if (!object.HasMember(key)) {
    return std::nullopt;
  }
  if (!object[key].IsString()) {
    throw std::invalid_argument(std::string(key) + " must be a string");
  }
  return std::string(object[key].GetString());
}
std::optional<double> number(const rapidjson::Value& object, const char* key) {
  if (!object.HasMember(key)) {
    return std::nullopt;
  }
  if (!object[key].IsNumber()) {
    throw std::invalid_argument(std::string(key) + " must be numeric");
  }
  return object[key].GetDouble();
}
bool flag(const rapidjson::Value& object, const char* key, bool fallback) {
  if (!object.HasMember(key)) {
    return fallback;
  }
  if (!object[key].IsBool()) {
    throw std::invalid_argument(std::string(key) + " must be boolean");
  }
  return object[key].GetBool();
}
std::vector<std::string> strings(const rapidjson::Value& object, const char* key) {
  std::vector<std::string> out;
  if (!object.HasMember(key)) {
    return out;
  }
  if (!object[key].IsArray()) {
    throw std::invalid_argument(std::string(key) + " must be an array of paths");
  }
  for (const auto& value : object[key].GetArray()) {
    if (!value.IsString()) {
      throw std::invalid_argument(std::string(key) + " requires string values");
    }
    out.emplace_back(value.GetString());
  }
  return out;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: stage_timing_probe config.json");
    }
    rapidjson::Document config;
    config.Parse(read(argv[1]).c_str());
    if (config.HasParseError() || !config.IsObject()) {
      throw std::invalid_argument("invalid stage timing config");
    }
    const auto out = text(config, "out");
    if (!out) {
      throw std::invalid_argument("missing string: out");
    }
    if (std::filesystem::exists(*out)) {
      throw std::invalid_argument("stage timing output exists; use a fresh path");
    }
    // The model and constraint inputs: the Liberty, the bundle with any
    // overrides, the saved SDC or the named clock.
    stage::Stage_timer_inputs inputs;
    for (const auto& path : strings(config, "liberty")) {
      inputs.liberty += read(path) + "\n";
    }
    if (inputs.liberty.empty()) {
      throw std::invalid_argument("liberty must list at least one path");
    }
    const auto lefs = strings(config, "lefs");
    if (!lefs.empty()) {
      inputs.technology = physical::read_lef(lefs);
    }
    if (const auto bundle = text(config, "bundle")) {
      inputs.bundle = *bundle;
    }
    if (config.HasMember("overrides")) {
      const auto& value = config["overrides"];
      if (!value.IsObject()) {
        throw std::invalid_argument("overrides must be an object");
      }
      auto& overrides = inputs.overrides;
      if (value.HasMember("stage_ps")) {
        if (!value["stage_ps"].IsObject()) {
          throw std::invalid_argument("overrides.stage_ps must be an object");
        }
        for (const auto& entry : value["stage_ps"].GetObject()) {
          if (!entry.value.IsNumber()) {
            throw std::invalid_argument("stage coefficient overrides must be numeric");
          }
          overrides.stage_ps[entry.name.GetString()] = entry.value.GetDouble();
        }
      }
      overrides.eval_fo4             = number(value, "eval_fo4");
      overrides.precharge_fo4        = number(value, "precharge_fo4");
      overrides.setup_eval_fo4       = number(value, "setup_eval_fo4");
      overrides.precharge_margin_fo4 = number(value, "precharge_margin_fo4");
    }
    if (const auto path = text(config, "sdc")) {
      inputs.sdc = read(*path);
      if (!config.HasMember("sdc_units") || !config["sdc_units"].IsObject()) {
        throw std::invalid_argument("sdc requires sdc_units.time_ps and sdc_units.capacitance_ff");
      }
      inputs.units
          = {number(config["sdc_units"], "time_ps").value_or(0), number(config["sdc_units"], "capacitance_ff").value_or(0)};
    }
    if (const auto port = text(config, "clock_port")) {
      if (config.HasMember("clock")) {
        throw std::invalid_argument("give clock_port and period_ps, or clock, not both");
      }
      inputs.clock.emplace();
      inputs.clock->port      = *port;
      inputs.clock->period_ps = number(config, "period_ps").value_or(0);
    } else if (config.HasMember("clock")) {
      const auto& value = config["clock"];
      if (!value.IsObject()) {
        throw std::invalid_argument("clock must be an object");
      }
      auto& clock     = inputs.clock.emplace();
      clock.name      = text(value, "name").value_or("");
      clock.port      = text(value, "port").value_or("");
      clock.period_ps = number(value, "period_ps").value_or(0);
      if (value.HasMember("waveform_ps")) {
        const auto& edges = value["waveform_ps"];
        if (!edges.IsArray() || edges.Size() != 2 || !edges[0].IsNumber() || !edges[1].IsNumber()) {
          throw std::invalid_argument("clock.waveform_ps must hold the rise and fall edges");
        }
        clock.waveform_ps = std::array{edges[0].GetDouble(), edges[1].GetDouble()};
      }
    }
    inputs.clock_uncertainty_ps = number(config, "clock_uncertainty_ps").value_or(0);

    // The netlist: the placed lg (its placement_rc attribute is the RC), and/or
    // the bit-level DEF import, which a netlist with word-valued ports needs.
    hhds::GraphLibrary                          owner, bits;
    std::shared_ptr<hhds::Graph>                graph;
    std::shared_ptr<const physical::Parasitics> rc;
    std::optional<physical::Floorplan>          floorplan;
    stage::Stage_timing_options                 options;
    if (const auto lg = text(config, "lg_dir")) {
      const auto top = text(config, "top");
      if (!top) {
        throw std::invalid_argument("lg_dir needs top");
      }
      owner.load(*lg);
      const auto io = owner.find_io(*top);
      if (!io || !io->get_graph()) {
        throw std::invalid_argument("lg_dir has no graph " + *top);
      }
      graph         = io->get_graph();
      inputs.design = *top;
      if (auto stored = graph->get_input_node().attr(livehd::attrs::placement_rc); stored.has()) {
        rc = std::make_shared<const physical::Parasitics>(physical::deserialize_parasitics_exact(stored.get()));
      }
    }
    if (const auto parasitics = text(config, "parasitics")) {
      rc = std::make_shared<const physical::Parasitics>(physical::deserialize_parasitics_exact(read(*parasitics)));
    }
    const auto def   = text(config, "def");
    const bool words = graph && stage::word_valued_ports(*graph);
    if (def || words) {
      if (!def || !inputs.technology) {
        throw std::invalid_argument(words ? "the netlist keeps word-valued ports: give def and lefs for its bit-level import"
                                          : "def needs lefs");
      }
      floorplan     = physical::read_def(*def);
      inputs.design = floorplan->design;
      if (!words) {  // a word-port netlist is imported by the timer itself
        graph           = physical::mapped_graph(bits, *floorplan, *inputs.technology);
        options.netlist = "DEF import";
      }
    }
    if (!graph) {
      throw std::invalid_argument("give lg_dir and top, or def and lefs");
    }
    const stage::Stage_timer timer(std::move(inputs));
    options.classes      = flag(config, "classes", true);
    const auto    timing = timer.evaluate(*graph, rc.get(), floorplan ? &*floorplan : nullptr, options);
    std::ofstream output(*out);
    output << timing.json;
    output.close();
    if (!output) {
      throw std::runtime_error("cannot write " + *out);
    }
    std::cout << "stage timing: " << timing.status << ", period "
              << (timing.period_ps ? std::to_string(*timing.period_ps) + " ps" : std::string("unmeasured")) << ", "
              << timing.excluded << " excluded, rc " << (rc ? "placement" : "none") << "\n";
    if (flag(config, "require_complete", false)) {
      stage::require_complete(timing);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "stage_timing_probe: " << error.what() << '\n';
    return 1;
  }
}
