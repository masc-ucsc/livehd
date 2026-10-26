// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Offline native-proxy/STA comparison driver. Never synthesizes the input.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>
#include <vector>

#include "inou/lefdef/mapped_graph.hpp"
#include "pass/place/late_timing.hpp"
#include "pass/place/objective/proxy_bridge.hpp"
#include "pass/place/sdc_subset.hpp"
#include "rapidjson/document.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"
#include "zlib.h"

namespace late     = livehd::placement::late;
namespace physical = livehd::physical;
namespace tc       = livehd::placement::timeclosure;
namespace {
std::string read(const std::string& path) {
  if (path.ends_with(".gz")) {
    gzFile input = gzopen(path.c_str(), "rb");
    if (!input) {
      throw std::runtime_error("cannot read " + path);
    }
    std::string result;
    char        bytes[65536];
    int         count = 0;
    while ((count = gzread(input, bytes, sizeof(bytes))) > 0) {
      result.append(bytes, count);
    }
    const int closed = gzclose(input);
    if (count < 0 || closed != Z_OK) {
      throw std::runtime_error("cannot decompress " + path);
    }
    return result;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read " + path);
  }
  return {std::istreambuf_iterator<char>(input), {}};
}
std::string string(const rapidjson::Value& object, const char* key) {
  if (!object.HasMember(key) || !object[key].IsString()) {
    throw std::invalid_argument(std::string("missing string: ") + key);
  }
  return object[key].GetString();
}
late::Pair pair(const rapidjson::Value& object, const char* key) {
  if (!object.HasMember(key)) {
    return {};
  }
  const auto& value = object[key];
  if (!value.IsArray() || value.Size() != 2 || !value[0].IsNumber() || !value[1].IsNumber()) {
    throw std::invalid_argument(std::string(key) + " must contain rise/fall numbers");
  }
  return {value[0].GetDouble(), value[1].GetDouble()};
}
double number(const rapidjson::Value& object, const char* key, double fallback = 0) {
  if (!object.HasMember(key)) {
    return fallback;
  }
  if (!object[key].IsNumber()) {
    throw std::invalid_argument(std::string(key) + " must be numeric");
  }
  return object[key].GetDouble();
}
std::set<std::string> strings(const rapidjson::Value& object, const char* key) {
  std::set<std::string> result;
  if (!object.HasMember(key)) {
    return result;
  }
  if (!object[key].IsArray()) {
    throw std::invalid_argument(std::string(key) + " must be an array");
  }
  for (const auto& value : object[key].GetArray()) {
    if (!value.IsString()) {
      throw std::invalid_argument(std::string(key) + " requires string values");
    }
    result.insert(value.GetString());
  }
  return result;
}
late::Ideal_clock ideal_clock(const rapidjson::Value& value) {
  if (!value.IsObject()) {
    throw std::invalid_argument("clock must be an object");
  }
  return {number(value, "period_ps"),
          number(value, "uncertainty_ps"),
          pair(value, "slew_ps"),
          strings(value, "roots"),
          number(value, "phase_ps")};
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: late_timing_probe config.json");
    }
    const auto          started = std::chrono::steady_clock::now();
    const auto          elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
    rapidjson::Document config;
    config.Parse(read(argv[1]).c_str());
    if (config.HasParseError() || !config.IsObject()) {
      throw std::invalid_argument("invalid native timing config");
    }
    const auto output_path = string(config, "output");
    if (std::filesystem::exists(output_path)) {
      throw std::invalid_argument("native timing output exists; use a fresh path");
    }
    if (!config.HasMember("liberty") || !config["liberty"].IsArray()) {
      throw std::invalid_argument("liberty must be an array of paths");
    }
    std::string contents;
    for (const auto& path : config["liberty"].GetArray()) {
      if (!path.IsString()) {
        throw std::invalid_argument("Liberty path must be a string");
      }
      contents += read(path.GetString());
      contents += '\n';
    }
    auto library = std::make_shared<livehd::liberty::timing::Library>(livehd::liberty::timing::parse(contents));
    contents.clear();
    contents.shrink_to_fit();
    const double             library_seconds = elapsed();
    std::vector<std::string> lefs;
    if (!config.HasMember("lefs") || !config["lefs"].IsArray()) {
      throw std::invalid_argument("lefs must be an array");
    }
    for (const auto& path : config["lefs"].GetArray()) {
      if (!path.IsString()) {
        throw std::invalid_argument("LEF path must be a string");
      }
      lefs.emplace_back(path.GetString());
    }
    const auto         technology = physical::read_lef(lefs);
    const auto         floorplan  = physical::read_def(string(config, "def"));
    hhds::GraphLibrary owner;
    const auto         graph          = physical::mapped_graph(owner, floorplan, technology);
    const double       import_seconds = elapsed() - library_seconds;
    late::Constraints  constraints;
    if (config.HasMember("constraints_complete")) {
      if (!config["constraints_complete"].IsBool()) {
        throw std::invalid_argument("constraints_complete must be boolean");
      }
      constraints.complete = config["constraints_complete"].GetBool();
    }
    constraints.cuts = strings(config, "cuts");
    if (config.HasMember("constants")) {
      if (!config["constants"].IsObject()) {
        throw std::invalid_argument("constants must be an object");
      }
      for (const auto& entry : config["constants"].GetObject()) {
        if (!entry.value.IsBool()) {
          throw std::invalid_argument("case-analysis value must be boolean");
        }
        constraints.constants[entry.name.GetString()] = entry.value.GetBool();
      }
    }
    const auto unsupported = strings(config, "unsupported_constraints");
    constraints.unsupported.assign(unsupported.begin(), unsupported.end());
    if (config.HasMember("inputs")) {
      if (!config["inputs"].IsObject()) {
        throw std::invalid_argument("inputs must be an object");
      }
      for (const auto& entry : config["inputs"].GetObject()) {
        if (!entry.value.IsObject()) {
          throw std::invalid_argument("input boundary must be an object");
        }
        constraints.inputs[entry.name.GetString()] = {pair(entry.value, "arrival_ps"), pair(entry.value, "slew_ps")};
        if (entry.value.HasMember("clock")) {
          constraints.inputs[entry.name.GetString()].clock = string(entry.value, "clock");
        }
        if (entry.value.HasMember("driving_cell")) {
          const auto& drive = entry.value["driving_cell"];
          if (!drive.IsObject()) {
            throw std::invalid_argument("driving_cell must be an object");
          }
          constraints.inputs[entry.name.GetString()].driving_cell
              = late::Driving_cell{string(drive, "cell"),
                                   string(drive, "output"),
                                   drive.HasMember("related_pin") ? string(drive, "related_pin") : "",
                                   pair(drive, "input_slew_ps")};
        }
      }
    }
    if (config.HasMember("outputs")) {
      if (!config["outputs"].IsObject()) {
        throw std::invalid_argument("outputs must be an object");
      }
      for (const auto& entry : config["outputs"].GetObject()) {
        if (!entry.value.IsObject() || !entry.value.HasMember("required_ps")) {
          throw std::invalid_argument("output boundary requires required_ps");
        }
        constraints.outputs[entry.name.GetString()] = {pair(entry.value, "required_ps"), pair(entry.value, "load_ff")};
        if (entry.value.HasMember("clock")) {
          constraints.outputs[entry.name.GetString()].clock = string(entry.value, "clock");
        }
      }
    }
    if (config.HasMember("clock")) {
      constraints.clock = ideal_clock(config["clock"]);
    }
    if (config.HasMember("clocks")) {
      if (!config["clocks"].IsObject()) {
        throw std::invalid_argument("clocks must be a named object");
      }
      for (const auto& entry : config["clocks"].GetObject()) {
        constraints.clocks[entry.name.GetString()] = ideal_clock(entry.value);
      }
    }
    if (config.HasMember("clock_aliases")) {
      if (!config["clock_aliases"].IsObject()) {
        throw std::invalid_argument("clock_aliases must be a named object");
      }
      for (const auto& entry : config["clock_aliases"].GetObject()) {
        if (!entry.value.IsObject()) {
          throw std::invalid_argument("clock alias must be an object");
        }
        constraints.clock_aliases[entry.name.GetString()] = {string(entry.value, "source"), strings(entry.value, "roots")};
      }
    }
    if (config.HasMember("asynchronous")) {
      if (!config["asynchronous"].IsArray()) {
        throw std::invalid_argument("asynchronous must contain clock-name pairs");
      }
      for (const auto& entry : config["asynchronous"].GetArray()) {
        if (!entry.IsArray() || entry.Size() != 2 || !entry[0].IsString() || !entry[1].IsString()) {
          throw std::invalid_argument("asynchronous must contain clock-name pairs");
        }
        constraints.asynchronous.emplace(entry[0].GetString(), entry[1].GetString());
      }
    }
    if (config.HasMember("sdc")) {
      for (const auto* key : {"clock",
                              "clocks",
                              "clock_aliases",
                              "asynchronous",
                              "inputs",
                              "outputs",
                              "constants",
                              "cuts",
                              "constraints_complete",
                              "unsupported_constraints"}) {
        if (config.HasMember(key)) {
          throw std::invalid_argument(std::string("saved SDC and explicit constraints conflict: ") + key);
        }
      }
      if (!config.HasMember("sdc_units") || !config["sdc_units"].IsObject() || !config["sdc_units"].HasMember("time_ps")
          || !config["sdc_units"].HasMember("capacitance_ff")) {
        throw std::invalid_argument("saved SDC requires explicit sdc_units.time_ps and sdc_units.capacitance_ff");
      }
      constraints = late::read_saved_sdc(read(string(config, "sdc")),
                                         floorplan.design,
                                         {number(config["sdc_units"], "time_ps"), number(config["sdc_units"], "capacitance_ff")});
    }
    bool include_domains = false;
    if (config.HasMember("include_domains")) {
      if (!config["include_domains"].IsBool()) {
        throw std::invalid_argument("include_domains must be boolean");
      }
      include_domains = config["include_domains"].GetBool();
    }
    bool include_predecessors = false;
    if (config.HasMember("include_predecessors")) {
      if (!config["include_predecessors"].IsBool()) {
        throw std::invalid_argument("include_predecessors must be boolean");
      }
      include_predecessors = config["include_predecessors"].GetBool();
    }
    bool observe_objective = false;
    if (config.HasMember("period_objective")) {
      if (!config["period_objective"].IsBool()) {
        throw std::invalid_argument("period_objective must be boolean");
      }
      observe_objective = config["period_objective"].GetBool();
    }
    std::optional<physical::Parasitics> rc;
    if (config.HasMember("parasitics")) {
      rc = physical::deserialize_parasitics(read(string(config, "parasitics")));
    }
    const double         bind_started = elapsed();
    late::Driver_options driver;
    if (config.HasMember("characterized_driver_boundary")) {
      if (!config["characterized_driver_boundary"].IsBool()) {
        throw std::invalid_argument("characterized_driver_boundary must be boolean");
      }
      driver.characterized_boundary = config["characterized_driver_boundary"].GetBool();
    }
    if (config.HasMember("bounded_driver_boundary")) {
      if (!config["bounded_driver_boundary"].IsBool()) {
        throw std::invalid_argument("bounded_driver_boundary must be boolean");
      }
      driver.bounded_boundary = config["bounded_driver_boundary"].GetBool();
    }
    late::Proxy  proxy(*graph, library, std::move(constraints));
    const double binding_seconds    = elapsed() - bind_started;
    const double evaluation_started = elapsed();
    const auto   result
        = proxy.evaluate(rc ? &*rc : nullptr, nullptr, {.capture_feasible_graph = observe_objective, .driver = driver});
    const double                        evaluation_seconds = elapsed() - evaluation_started;
    std::optional<tc::Period_objective> objective;
    const double                        objective_started = elapsed();
    if (observe_objective && result.certified && result.achievable_period_ps) {
      objective = tc::proxy_objective(result);
    }
    const double objective_seconds = elapsed() - objective_started;
    auto         selected          = strings(config, "pins");
    if (include_predecessors && !selected.empty()) {
      std::vector<size_t> pending;
      for (size_t i = 0; i < result.pins.size(); ++i) {
        if (selected.contains(result.pins[i].name)) {
          pending.push_back(i);
        }
      }
      for (size_t i = 0; i < pending.size(); ++i) {
        for (const auto& state : result.pins[pending[i]].transitions) {
          if (state.predecessor && selected.insert(result.pins[state.predecessor->pin].name).second) {
            pending.push_back(state.predecessor->pin);
          }
        }
      }
    }
    std::ofstream output(output_path);
    if (!output) {
      throw std::runtime_error("cannot create native timing output");
    }
    rapidjson::OStreamWrapper                    stream(output);
    rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream);
    const auto                                   optional = [&](std::optional<double> value) {
      if (value) {
        writer.Double(*value);
      } else {
        writer.Null();
      }
    };
    writer.StartObject();
    writer.Key("model");
    writer.String("native_late_nldm_charge_matched_pi_elmore");
    writer.Key("time_unit_ps");
    writer.Double(1);
    writer.Key("predecessor_paths_included");
    writer.Bool(include_predecessors);
    writer.Key("certified");
    writer.Bool(result.certified);
    writer.Key("wns_ps");
    optional(result.wns_ps);
    writer.Key("tns_ps");
    optional(result.tns_ps);
    writer.Key("achievable_period_ps");
    optional(result.achievable_period_ps);
    if (observe_objective) {
      writer.Key("period_objective");
      writer.StartObject();
      writer.Key("supported");
      writer.Bool(bool(objective));
      writer.Key("optimizer_feedback_enabled");
      writer.Bool(false);
      writer.Key("seconds");
      writer.Double(objective_seconds);
      writer.Key("vertices");
      writer.Uint64(result.feasible_graph->vertices.size());
      writer.Key("arcs");
      writer.Uint64(result.feasible_graph->arcs.size());
      if (objective) {
        writer.Key("achievable_period_ps");
        writer.Double(objective->achievable_period);
        writer.Key("optimization_period_ps");
        writer.Double(objective->optimization_period);
        writer.Key("soft_period_ps");
        writer.Double(objective->soft_period);
        writer.Key("temperature_ps");
        writer.Double(objective->temperature);
        writer.Key("endpoint_mass");
        writer.Double(std::accumulate(objective->endpoint_mass.begin(), objective->endpoint_mass.end(), 0.));
        writer.Key("boundary_mass");
        writer.Double(std::accumulate(objective->boundary_mass.begin(), objective->boundary_mass.end(), 0.));
        writer.Key("window_endpoints");
        writer.Uint64(std::count(objective->in_window.begin(), objective->in_window.end(), true));
        std::vector<late::Pair> mass(result.pins.size());
        for (size_t i = 0; i < result.feasible_graph->vertices.size(); ++i) {
          const auto& vertex                   = result.feasible_graph->vertices[i];
          mass[vertex.pin][vertex.transition] += objective->vertex_mass[i];
        }
        writer.Key("pins");
        writer.StartObject();
        for (size_t i = 0; i < result.pins.size(); ++i) {
          if (!selected.empty() && !selected.contains(result.pins[i].name)) {
            continue;
          }
          writer.Key(result.pins[i].name.c_str());
          writer.StartArray();
          writer.Double(mass[i][0]);
          writer.Double(mass[i][1]);
          writer.EndArray();
        }
        writer.EndObject();
      } else {
        writer.Key("unavailable");
        writer.String("native timing lacks supported single-period coverage");
      }
      writer.EndObject();
    }
    writer.Key("clocks");
    writer.StartObject();
    for (const auto& [name, summary] : result.clocks) {
      writer.Key(name.c_str());
      writer.StartObject();
      writer.Key("period_ps");
      writer.Double(summary.period_ps);
      writer.Key("wns_ps");
      optional(summary.wns_ps);
      writer.Key("tns_ps");
      optional(summary.tns_ps);
      writer.Key("achievable_period_ps");
      optional(summary.achievable_period_ps);
      writer.Key("independent_period");
      writer.Bool(summary.independent_period);
      writer.Key("timed_endpoints");
      writer.Uint64(summary.timed_endpoints);
      writer.EndObject();
    }
    writer.EndObject();
    writer.Key("launch_domains");
    writer.StartArray();
    for (const auto& domain : result.domains) {
      if (domain.launch_clock) {
        writer.String(domain.launch_clock->c_str());
      } else {
        writer.Null();
      }
    }
    writer.EndArray();
    if (include_domains) {
      writer.Key("domains");
      writer.StartArray();
      for (const auto& domain : result.domains) {
        writer.StartObject();
        writer.Key("launch_clock");
        if (domain.launch_clock) {
          writer.String(domain.launch_clock->c_str());
        } else {
          writer.Null();
        }
        writer.Key("pins");
        writer.StartObject();
        for (size_t i = 0; i < result.pins.size(); ++i) {
          if (!selected.empty() && !selected.contains(result.pins[i].name)) {
            continue;
          }
          writer.Key(result.pins[i].name.c_str());
          writer.StartArray();
          for (const auto& state : domain.pins[i]) {
            writer.StartObject();
            writer.Key("arrival_ps");
            optional(state.arrival_ps);
            writer.Key("required_ps");
            optional(state.required_ps);
            writer.Key("predecessor");
            if (state.predecessor) {
              writer.StartObject();
              writer.Key("pin");
              writer.String(result.pins[state.predecessor->pin].name.c_str());
              writer.Key("transition");
              writer.Uint(state.predecessor->transition);
              writer.EndObject();
            } else {
              writer.Null();
            }
            writer.EndObject();
          }
          writer.EndArray();
        }
        writer.EndObject();
        writer.EndObject();
      }
      writer.EndArray();
    }
    writer.Key("signal_instances");
    writer.Uint64(floorplan.components.size());
    writer.Key("placement_blockages");
    writer.Uint64(floorplan.blockages.size());
    size_t blockage_rectangles = 0;
    for (const auto& blockage : floorplan.blockages) {
      blockage_rectangles += blockage.rectangles.size();
    }
    writer.Key("placement_blockage_rectangles");
    writer.Uint64(blockage_rectangles);
    writer.Key("bound_pins");
    writer.Uint64(result.pins.size());
    writer.Key("visited_pins");
    writer.Uint64(result.visited_pins);
    writer.Key("evaluated_arcs");
    writer.Uint64(result.evaluated_arcs);
    writer.Key("waveform_pins");
    writer.Uint64(result.waveform_pins);
    writer.Key("waveform_arcs");
    writer.Uint64(result.waveform_arcs);
    writer.Key("required_pins");
    writer.Uint64(result.required_pins);
    writer.Key("delay_only_wire_drivers");
    writer.Uint64(result.delay_only_wire_drivers);
    writer.Key("driver_fits");
    writer.Uint64(result.driver_fits);
    writer.Key("distributed_driver_fits");
    writer.Uint64(result.distributed_driver_fits);
    writer.Key("lumped_driver_fits");
    writer.Uint64(result.lumped_driver_fits);
    writer.Key("driver_fit_failures");
    writer.Uint64(result.driver_fit_failures);
    writer.Key("characterized_driver_boundary");
    writer.Bool(result.characterized_boundary_policy);
    writer.Key("bounded_driver_boundary");
    writer.Bool(result.bounded_boundary_policy);
    writer.Key("full_load_boundary_driver_fits");
    writer.Uint64(result.full_load_boundary_driver_fits);
    writer.Key("crossing_boundary_driver_fits");
    writer.Uint64(result.crossing_boundary_driver_fits);
    writer.Key("table_boundary_driver_fits");
    writer.Uint64(result.table_boundary_driver_fits);
    writer.Key("library_seconds");
    writer.Double(library_seconds);
    writer.Key("import_seconds");
    writer.Double(import_seconds);
    writer.Key("binding_seconds");
    writer.Double(binding_seconds);
    writer.Key("evaluation_seconds");
    writer.Double(evaluation_seconds);
    writer.Key("wire_rc");
    writer.String(rc ? "serialized_tree" : "zero");
    writer.Key("warnings");
    writer.StartArray();
    for (const auto& value : result.warnings) {
      writer.String(value.c_str());
    }
    writer.EndArray();
    writer.Key("unsupported");
    writer.StartArray();
    for (const auto& value : result.unsupported) {
      writer.String(value.c_str());
    }
    writer.EndArray();
    writer.Key("pins");
    writer.StartObject();
    for (const auto& pin : result.pins) {
      if (!selected.empty() && !selected.contains(pin.name)) {
        continue;
      }
      writer.Key(pin.name.c_str());
      writer.StartObject();
      writer.Key("endpoint");
      writer.Bool(pin.endpoint);
      writer.Key("declaration_only");
      writer.Bool(pin.graph_pin.is_invalid());
      writer.Key("clock");
      writer.Bool(pin.clock);
      writer.Key("constant");
      if (pin.constant) {
        writer.Bool(*pin.constant);
      } else {
        writer.Null();
      }
      writer.Key("transitions");
      writer.StartArray();
      for (const auto& state : pin.transitions) {
        writer.StartObject();
        writer.Key("arrival_ps");
        optional(state.arrival_ps);
        writer.Key("required_ps");
        optional(state.required_ps);
        writer.Key("slew_ps");
        optional(state.slew_ps);
        writer.Key("load_ff");
        optional(state.load_ff);
        writer.Key("predecessor");
        if (state.predecessor) {
          writer.StartObject();
          writer.Key("pin");
          writer.String(result.pins[state.predecessor->pin].name.c_str());
          writer.Key("transition");
          writer.Uint(state.predecessor->transition);
          writer.EndObject();
        } else {
          writer.Null();
        }
        writer.EndObject();
      }
      writer.EndArray();
      writer.EndObject();
    }
    writer.EndObject();
    writer.EndObject();
    output << '\n';
    output.close();
    if (!output) {
      throw std::runtime_error("cannot write native timing output");
    }
    std::cout << "native timing: " << result.pins.size() << " pins, " << result.unsupported.size()
              << " coverage issues, evaluation " << evaluation_seconds << " s\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "late_timing_probe: " << error.what() << '\n';
    return 1;
  }
}
