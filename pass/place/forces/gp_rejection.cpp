// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gp_rejection.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>

#include "pass/place/late_timing_json.hpp"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"

namespace livehd::placement::timeclosure {
namespace {
using Writer = rapidjson::Writer<rapidjson::OStreamWrapper>;
using late::json::write;
void difference(Writer& json, const late::Timing_difference& value) {
  json.StartObject();
  json.Key("pin");
  json.String(value.pin.c_str());
  json.Key("transition");
  json.Uint(value.transition);
  json.Key("prediction");
  json.Double(value.prediction);
  json.Key("reference");
  json.Double(value.reference);
  json.Key("error");
  json.Double(value.error);
  json.Key("tolerance");
  json.Double(value.tolerance);
  json.EndObject();
}
void observation(Writer& json, const late::Result& value) {
  json.StartObject();
  json.Key("certified");
  json.Bool(value.certified);
  json.Key("topology_generation");
  json.Uint64(value.topology_generation);
  json.Key("model_generation");
  json.Uint64(value.model_generation);
  json.Key("characterized_driver_boundary");
  json.Bool(value.characterized_boundary_policy);
  json.Key("bounded_driver_boundary");
  json.Bool(value.bounded_boundary_policy);
  json.Key("wns_ps");
  write(json, value.wns_ps);
  json.Key("achievable_period_ps");
  write(json, value.achievable_period_ps);
  json.Key("clocks");
  late::json::write_clocks(json, value, late::json::Clock_detail::compact);
  json.Key("pins");
  json.StartArray();
  for (const auto& pin : value.pins) {
    json.StartObject();
    json.Key("name");
    json.String(pin.name.c_str());
    json.Key("endpoint");
    json.Bool(pin.endpoint);
    json.Key("clock");
    json.Bool(pin.clock);
    json.Key("transitions");
    late::json::write_transitions(json, pin.transitions, value, late::json::Predecessor_style::index);
    json.EndObject();
  }
  json.EndArray();
  json.EndObject();
}
}  // namespace
void write_gp_timing_rejection(const std::string& prefix, const Gp_timing_rejection& rejected, const physical::Parasitics& rc,
                               const std::map<std::string, std::pair<double, double>>& locations, int iteration, double overflow) {
  if (iteration < 0 || !std::isfinite(overflow) || overflow < 0) {
    throw std::invalid_argument("invalid rejected native timing checkpoint metadata");
  }
  std::ofstream parasitics(prefix + ".rc.json");
  parasitics << physical::serialize_parasitics(rc);
  parasitics.close();
  if (!parasitics) {
    throw std::runtime_error("cannot write rejected native timing RC frame");
  }
  std::ofstream             pins(prefix + ".pins.json");
  rapidjson::OStreamWrapper pin_stream(pins);
  Writer                    pin_json(pin_stream);
  pin_json.StartObject();
  for (const auto& [name, xy] : locations) {
    pin_json.Key(name.c_str());
    pin_json.StartArray();
    pin_json.Double(xy.first);
    pin_json.Double(xy.second);
    pin_json.EndArray();
  }
  pin_json.EndObject();
  pins.close();
  if (!pins) {
    throw std::runtime_error("cannot write rejected native timing pin frame");
  }
  std::ofstream             report(prefix + ".comparison.json");
  rapidjson::OStreamWrapper stream(report);
  Writer                    json(stream);
  json.StartObject();
  json.Key("iteration");
  json.Int(iteration);
  json.Key("density_overflow");
  json.Double(overflow);
  json.Key("diagnostic_only");
  json.Bool(true);
  json.Key("optimization_state_committed");
  json.Bool(false);
  json.Key("attempt");
  json.Uint(rejected.attempt);
  json.Key("retry_planned");
  json.Bool(rejected.retry_planned);
  json.Key("rc_frame");
  json.String((prefix + ".rc.json").c_str());
  json.Key("pin_locations");
  json.String((prefix + ".pins.json").c_str());
  json.Key("comparison");
  json.StartObject();
  const auto& comparison = rejected.comparison;
  for (const auto& [name, value] : {
           std::pair{   "numerical_gates_pass",    comparison.numerical_gates_pass},
           {     "ranking_gates_pass",      comparison.ranking_gates_pass},
           {  "comparison_gates_pass",   comparison.comparison_gates_pass},
           {"supported_coverage_pass", comparison.supported_coverage_pass}
  }) {
    json.Key(name);
    json.Bool(value);
  }
  json.Key("fields");
  json.StartObject();
  const std::array<const char*, 4> fields{"arrival_ps", "required_ps", "slew_ps", "load_ff"};
  for (size_t i = 0; i < fields.size(); ++i) {
    const auto& field = comparison.fields[i];
    json.Key(fields[i]);
    json.StartObject();
    json.Key("compared");
    json.Uint64(field.compared);
    json.Key("coverage_disagreements");
    json.Uint64(field.coverage_disagreements);
    json.Key("tolerance_failures");
    if (field.tolerance_failures) {
      json.Uint64(*field.tolerance_failures);
    } else {
      json.Null();
    }
    json.Key("maximum");
    json.Double(field.maximum);
    json.Key("mean");
    write(json, field.mean);
    json.Key("worst");
    if (field.worst) {
      difference(json, *field.worst);
    } else {
      json.Null();
    }
    json.Key("failure_examples");
    json.StartArray();
    for (const auto& example : field.failure_examples) {
      difference(json, example);
    }
    json.EndArray();
    json.EndObject();
  }
  json.EndObject();
  const auto& rank = comparison.ranking;
  json.Key("ranking");
  json.StartObject();
  for (const auto& [name, count] : {
           std::pair{    "compared_endpoints",     rank.compared_endpoints},
           {"coverage_disagreements", rank.coverage_disagreements},
           {    "reference_critical",     rank.reference_critical},
           {   "prediction_critical",    rank.prediction_critical},
           {            "union_size",             rank.union_size}
  }) {
    json.Key(name);
    json.Uint64(count);
  }
  json.Key("period_ps");
  write(json, rank.period_ps);
  json.Key("jaccard");
  write(json, rank.jaccard);
  json.Key("tau_b");
  write(json, rank.tau_b);
  json.Key("unavailable");
  json.String(rank.unavailable.c_str());
  json.EndObject();
  json.EndObject();
  json.Key("star");
  json.StartObject();
  json.Key("refresh");
  json.Bool(rejected.star.refresh);
  const auto& stats = rejected.star.stats;
  for (const auto& [name, count] : {
           std::pair{            "calibrated",             stats.calibrated},
           {                "reused",                 stats.reused},
           {          "load_rebound",           stats.load_rebound},
           {               "removed",                stats.removed},
           {        "pins_evaluated",         stats.pins_evaluated},
           {     "zero_wire_anchors",      stats.zero_wire_anchors},
           {"displacement_refreshed", stats.displacement_refreshed}
  }) {
    json.Key(name);
    json.Uint64(count);
  }
  json.Key("max_displacement_fraction");
  json.Double(stats.max_displacement_fraction);
  json.EndObject();
  json.Key("reference");
  observation(json, rejected.reference);
  json.Key("continuous");
  observation(json, rejected.continuous);
  json.EndObject();
  report.close();
  if (!report) {
    throw std::runtime_error("cannot write rejected native timing comparison");
  }
}
}  // namespace livehd::placement::timeclosure
