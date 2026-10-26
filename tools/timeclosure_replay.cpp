// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Offline observation only. No synthesis, placement, timer, or graph edits.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "pass/place/timeclosure.hpp"
#include "rapidjson/document.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"

namespace tc = livehd::placement::timeclosure;
namespace {
using Writer = rapidjson::Writer<rapidjson::OStreamWrapper>;
rapidjson::Document parse(const std::string& contents) {
  rapidjson::Document result;
  result.Parse(contents.c_str());
  if (result.HasParseError() || !result.IsObject()) {
    throw std::invalid_argument("expected a JSON object");
  }
  return result;
}
tc::Settings settings(const rapidjson::Value& config) {
  tc::Settings result;
  for (const auto& field : config.GetObject()) {
    const std::string key     = field.name.GetString();
    const auto        integer = [&](int& target) {
      if (!field.value.IsInt()) {
        throw std::invalid_argument(key + " must be an integer");
      }
      target = field.value.GetInt();
    };
    const auto number = [&](double& target) {
      if (!field.value.IsNumber()) {
        throw std::invalid_argument(key + " must be numeric");
      }
      target = field.value.GetDouble();
    };
    if (key == "budget") {
      integer(result.budget);
    } else if (key == "sample_interval") {
      integer(result.sample_interval);
    } else if (key == "window") {
      integer(result.window);
    } else if (key == "horizon") {
      number(result.horizon);
    } else if (key == "range_fraction") {
      number(result.range_fraction);
    } else if (key == "noise_multiplier") {
      number(result.noise_multiplier);
    } else if (key == "endpoint_overlap") {
      number(result.endpoint_overlap);
    } else if (key == "memory_growth") {
      integer(result.memory_growth);
    } else if (key == "memory_settle") {
      integer(result.memory_settle);
    } else if (key == "rudy_dwell") {
      integer(result.rudy_dwell);
    } else if (key == "timing_response") {
      integer(result.timing_response);
    } else if (key == "repair_dwell") {
      integer(result.repair_dwell);
    } else if (key == "correction_max_age") {
      integer(result.correction_max_age);
    } else if (key == "correction_overflow_drop") {
      number(result.correction_overflow_drop);
    } else if (key == "memory" && field.value.IsBool()) {
      result.memory = field.value.GetBool();
    } else {
      throw std::invalid_argument("unknown or invalid controller setting: " + key);
    }
  }
  result.validate();
  return result;
}
std::optional<double> number(const rapidjson::Value& object, const char* key) {
  if (!object.HasMember(key) || object[key].IsNull()) {
    return std::nullopt;
  }
  if (!object[key].IsNumber()) {
    throw std::invalid_argument(std::string(key) + " must be numeric or null");
  }
  return object[key].GetDouble();
}
tc::Sample sample(const rapidjson::Value& object) {
  tc::Sample result;
  if (!object.HasMember("iteration") || !object["iteration"].IsInt()) {
    throw std::invalid_argument("sample requires an integer iteration");
  }
  result.iteration        = object["iteration"].GetInt();
  result.values           = {number(object, "period_ns"),
                             number(object, "window_tns_ns"),
                             number(object, "hpwl_um"),
                             number(object, "opaque_route_overflow")};
  result.density_overflow = number(object, "density_overflow");
  for (const auto& [key, target] : {
           std::pair{    "topology",     &result.topology},
           {    "geometry",     &result.geometry},
           {"timing_model", &result.timing_model}
  }) {
    if (object.HasMember(key)) {
      if (!object[key].IsUint64()) {
        throw std::invalid_argument(std::string(key) + " must be an unsigned generation id");
      }
      *target = object[key].GetUint64();
    }
  }
  if (object.HasMember("virtual_timing")) {
    if (!object["virtual_timing"].IsBool()) {
      throw std::invalid_argument("virtual_timing must be boolean");
    }
    result.virtual_timing = object["virtual_timing"].GetBool();
  }
  if (object.HasMember("endpoints") && !object["endpoints"].IsNull()) {
    if (!object["endpoints"].IsArray()) {
      throw std::invalid_argument("endpoints must be an array or null");
    }
    result.endpoints.emplace();
    for (const auto& endpoint : object["endpoints"].GetArray()) {
      if (!endpoint.IsString()) {
        throw std::invalid_argument("endpoint identities must be strings");
      }
      result.endpoints->insert(endpoint.GetString());
    }
  }
  return result;
}
void optional(Writer& out, std::optional<double> value) {
  if (value) {
    out.Double(*value);
  } else {
    out.Null();
  }
}
void trends(Writer& out, const std::array<tc::Trend, tc::metric_count>& values) {
  out.Key("trends");
  out.StartObject();
  for (size_t i = 0; i < values.size(); ++i) {
    const auto& value = values[i];
    out.Key(tc::name(static_cast<tc::Metric>(i)));
    out.StartObject();
    out.Key("ready");
    out.Bool(value.ready);
    out.Key("pass");
    out.Bool(value.pass);
    out.Key("stable");
    out.Bool(value.stable);
    out.Key("turning");
    out.Bool(value.turning);
    out.Key("samples");
    out.Int(value.samples);
    out.Key("value");
    optional(out, value.ready ? std::optional{value.value} : std::nullopt);
    out.Key("slope");
    optional(out, value.ready ? std::optional{value.slope} : std::nullopt);
    out.Key("range");
    optional(out, value.ready ? std::optional{value.range} : std::nullopt);
    out.Key("noise");
    optional(out, value.ready ? std::optional{value.noise} : std::nullopt);
    out.EndObject();
  }
  out.EndObject();
}
void write(std::ostream& output, int at, const std::optional<tc::Sample>& observation, const tc::Decision& decision) {
  rapidjson::OStreamWrapper stream(output);
  Writer                    out(stream);
  if (observation) {
    out.StartObject();
    out.Key("kind");
    out.String("sample");
    out.Key("iteration");
    out.Int(at);
    out.Key("evaluated");
    out.Bool(decision.evaluated);
    out.Key("dwelling");
    out.Bool(decision.dwelling);
    out.Key("endpoint_jaccard");
    optional(out, decision.endpoint_jaccard);
    out.Key("missing_metrics");
    out.StartArray();
    for (size_t i = 0; i < tc::metric_count; ++i) {
      if (!observation->values[i]) {
        out.String(tc::name(static_cast<tc::Metric>(i)));
      }
    }
    out.EndArray();
    trends(out, decision.trends);
    out.EndObject();
    output << '\n';
    out.Reset(stream);
  }
  for (const auto& event : decision.events) {
    out.StartObject();
    out.Key("kind");
    out.String("event");
    out.Key("gate");
    out.String(tc::name(event.gate));
    out.Key("reason");
    out.String(tc::name(event.reason));
    out.Key("iteration");
    out.Int(event.iteration);
    out.Key("dwell_until");
    out.Int(event.dwell_until);
    out.Key("remaining");
    out.Int(event.remaining);
    out.Key("incomplete");
    out.Bool(event.incomplete);
    out.Key("detail");
    out.String(event.detail.c_str());
    out.Key("topology");
    out.Uint64(event.topology);
    out.Key("geometry");
    out.Uint64(event.geometry);
    out.Key("timing_model");
    out.Uint64(event.timing_model);
    out.Key("virtual_timing");
    out.Bool(event.virtual_timing);
    out.Key("density_overflow");
    optional(out, event.density_overflow);
    trends(out, event.trends);
    out.EndObject();
    output << '\n';
    out.Reset(stream);
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 4) {
      throw std::invalid_argument("usage: timeclosure_replay settings.json samples.jsonl output.jsonl");
    }
    std::ifstream config_file(argv[1]), input(argv[2]);
    if (!config_file || !input) {
      throw std::runtime_error("cannot read replay inputs");
    }
    const auto config = settings(parse({std::istreambuf_iterator<char>(config_file), {}}));
    if (std::filesystem::exists(argv[3])) {
      throw std::runtime_error("replay output exists; use a fresh path");
    }
    std::ofstream output(argv[3]);
    if (!output) {
      throw std::runtime_error("cannot create replay output");
    }
    tc::Controller controller(config);
    std::string    line;
    int            at = 0, observed_until = -1, last_input = -1, ignored = 0;
    while (std::getline(input, line)) {
      if (line.empty()) {
        continue;
      }
      const auto observation = sample(parse(line));
      if (observation.iteration <= last_input || observation.iteration < 0 || observation.iteration % config.sample_interval) {
        throw std::invalid_argument("trace iterations must increase and follow sample cadence");
      }
      last_input = observation.iteration;
      if (controller.done()) {
        ++ignored;
        continue;
      }
      const int until = std::min(observation.iteration, config.budget);
      while (at < until && !controller.done()) {
        write(output, at, std::nullopt, controller.tick(at));
        ++at;
      }
      if (!controller.done() && observation.iteration <= config.budget) {
        write(output, at, observation, controller.tick(at, observation));
        observed_until = at++;
      } else {
        if (!controller.done()) {
          write(output, at, std::nullopt, controller.tick(at));
          ++at;
        }
        ++ignored;
      }
    }
    rapidjson::OStreamWrapper stream(output);
    Writer                    out(stream);
    out.StartObject();
    out.Key("kind");
    out.String("summary");
    out.Key("observation_only");
    out.Bool(true);
    out.Key("observed_until");
    out.Int(observed_until);
    out.Key("controller_until");
    out.Int(at - 1);
    out.Key("finished");
    out.Bool(controller.done());
    out.Key("budget");
    out.Int(config.budget);
    out.Key("ignored_samples_after_finish_or_budget");
    out.Int(ignored);
    out.EndObject();
    output << '\n';
    output.close();
    if (!output) {
      throw std::runtime_error("cannot write replay output");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "timeclosure_replay: " << error.what() << '\n';
    return 1;
  }
}
