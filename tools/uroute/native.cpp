// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Offline bridge for feature parity checks and routed-layout data ingestion.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "dreamplace/native/placement.hpp"
#include "dreamplace/native/rudy.hpp"
#include "dreamplace/native/rudy_tree.hpp"
#include "dreamplace/native/uroute.hpp"
#include "dreamplace/native/uroute_weights.hpp"
#include "pass/place/steiner_topology.hpp"
#include "rapidjson/document.h"

namespace dp = DreamPlace::native;
namespace {
using Value = rapidjson::Value;
// Raw prediction maps; each channel's range is its own f32 extrema.
dp::UrouteMaps raw_maps(int bins_x, int bins_y, const std::vector<std::vector<float>>& channels) {
  dp::UrouteMaps result{int(channels.size()), bins_x, bins_y, false, {}, {}};
  for (const auto& values : channels) {
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    result.ranges.push_back({*lo, *hi});
    result.values.insert(result.values.end(), values.begin(), values.end());
  }
  return result;
}
double number(const Value& value) {
  if (!value.IsNumber()) {
    throw std::invalid_argument("expected numeric geometry value");
  }
  return value.GetDouble();
}
const Value& field(const Value& value, const char* key) {
  if (!value.IsObject() || !value.HasMember(key)) {
    throw std::invalid_argument(std::string("missing geometry field: ") + key);
  }
  return value[key];
}
bool flag(const Value& value, const char* key) {
  if (!value.HasMember(key)) {
    return false;
  }
  if (!value[key].IsBool()) {
    throw std::invalid_argument(std::string("expected boolean geometry field: ") + key);
  }
  return value[key].GetBool();
}
double     optional_number(const Value& value, const char* key) { return value.HasMember(key) ? number(value[key]) : 0; }
dp::Design geometry(const Value& doc) {
  dp::Design  d;
  const auto& core = field(doc, "core");
  if (!core.IsArray() || core.Size() != 4) {
    throw std::invalid_argument("core must contain four coordinates");
  }
  d.layout          = {number(core[0]), number(core[1]), number(core[2]), number(core[3]), 1, 1};
  const auto& nodes = field(doc, "nodes");
  const auto& nets  = field(doc, "nets");
  if (!nodes.IsArray() || !nets.IsArray()) {
    throw std::invalid_argument("nodes and nets must be arrays");
  }
  for (const auto& n : nodes.GetArray()) {
    // Offline snapshots can also mark standard-cell fillers for exclusion.
    // Pins below are independent zero-area terminals, so omitted node IDs
    // cannot change connectivity. Do not turn fillers into placement masks.
    if (flag(n, "mask")) {
      continue;
    }
    d.node_x.push_back(number(field(n, "x")));
    d.node_y.push_back(number(field(n, "y")));
    d.node_size_x.push_back(number(field(n, "width")));
    d.node_size_y.push_back(number(field(n, "height")));
    d.node_macro.push_back(flag(n, "macro"));
    d.node_placement_mask.push_back(dp::PlacementMask::none);
    d.node_pad_x.push_back(optional_number(n, "pad_x"));
    d.node_pad_y.push_back(optional_number(n, "pad_y"));
  }
  // Independent zero-area terminals preserve the absolute routed pin geometry.
  for (const auto& net : nets.GetArray()) {
    const auto& pins = field(net, "pins");
    if (!pins.IsArray()) {
      throw std::invalid_argument("pins must be an array");
    }
    for (const auto& pin : pins.GetArray()) {
      if (!pin.IsArray() || pin.Size() != 2) {
        throw std::invalid_argument("pin must contain two coordinates");
      }
      d.pin2node.push_back(d.node_x.size());
      d.node_x.push_back(number(pin[0]));
      d.node_y.push_back(number(pin[1]));
      d.node_size_x.push_back(0);
      d.node_size_y.push_back(0);
      d.node_macro.push_back(0);
      d.node_placement_mask.push_back(dp::PlacementMask::none);
      d.node_pad_x.push_back(0);
      d.node_pad_y.push_back(0);
      d.pin_offset_x.push_back(0);
      d.pin_offset_y.push_back(0);
      d.flat_net2pin.push_back(d.flat_net2pin.size());
    }
    d.flat_net2pin_start.push_back(d.flat_net2pin.size());
    d.net_weights.push_back(1);
    d.net_mask.push_back((!net.HasMember("enabled") || flag(net, "enabled")) && pins.Size() > 1);
  }
  d.build_maps();
  return d;
}

void ladder_check(const std::shared_ptr<const dp::UrouteWeights>& weights, const dp::UrouteMaps& features,
                  const dp::UrouteMaps& labels) {
  const auto check = [](bool ok, const char* message) {
    if (!ok) {
      throw std::runtime_error(message);
    }
  };
  const auto fixture = [] {
    dp::Design d;
    d.layout             = {0, 0, 100, 100, 1, 1};
    d.num_movable        = 2;
    d.node_x             = {10, 80};
    d.node_y             = {10, 80};
    d.node_size_x        = {1, 1};
    d.node_size_y        = {1, 1};
    d.pin2node           = {0, 1};
    d.pin_offset_x       = {0, 0};
    d.pin_offset_y       = {0, 0};
    d.flat_net2pin       = {0, 1};
    d.flat_net2pin_start = {0, 2};
    d.net_weights        = {1};
    d.build_maps();
    return d;
  };
  const std::vector<dp::UrouteSupport> samples{
      {"ladder-fixture", features, labels}
  };
  auto        context = std::make_shared<const dp::UrouteContext>(weights, samples);
  dp::Options o;
  o.routability = true;
  o.legalize = o.detailed_place = o.add_fillers = false;
  o.max_iterations = o.min_iterations = o.schedule_iterations = 100;
  o.bins_x = o.bins_y = 8;
  o.route_capacity_h = o.route_capacity_v = 100;
  o.route.model                           = dp::RouteModel::automatic;
  o.route.hold                            = 2;
  o.route.uroute_hold                     = 2;
  o.route.uroute_ladder                   = true;
  o.uroute                                = context;
  auto       d                            = fixture();
  const auto full                         = dp::place(d, o);
  check(full.route_switch_iteration == 21 && full.route_uroute_switch_iteration == 61,
        "URoute automatic hold/latch timing mismatch");
  check(!full.converged, "legacy stopping ignored the active URoute target");
  check(full.route_model == dp::RouteModel::uroute && full.route_switch_uroute_rsmt == 0
            && std::abs(full.route_switch_uroute - 1.) < 1e-10,
        "URoute switch did not record both scales");
  check(full.uroute_sample_ids == std::vector<std::string>{"ladder-fixture"}, "missing selected support IDs");
  bool latched = false;
  for (const auto& sample : full.history) {
    if (sample.route_model == dp::RouteModel::uroute) {
      latched = true;
      check(sample.route_uroute_seconds > 0, "missing URoute sample timing");
    }
    if (latched) {
      check(sample.route_model == dp::RouteModel::uroute, "URoute latch went backwards");
    }
  }
  dp::GpState state;
  auto        continued = fixture();
  o.max_iterations      = 60;
  const auto before     = dp::place(continued, o, &state);
  check(before.route_model == dp::RouteModel::rsmt && state.route_uroute_hold == 1,
        "continuation lost the URoute qualifying sample count");
  o.max_iterations   = 40;
  const auto resumed = dp::place(continued, o, &state);
  check(resumed.route_model == dp::RouteModel::uroute && resumed.route_uroute_switch_iteration == 61,
        "continuation lost the URoute latch");
  check(resumed.route_switch_uroute == full.route_switch_uroute, "continuation changed switch calibration");
  o.route.uroute_hold = 3;
  bool rejected       = false;
  try {
    dp::place(continued, o, &state);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  check(rejected, "continuation accepted changed URoute controls");
  o.route.uroute_hold = 2;
  o.max_iterations    = 40;
  o.uroute.reset();
  o.route.model      = dp::RouteModel::uroute;
  d                  = fixture();
  const auto skipped = dp::place(d, o);
  check(skipped.route_model == dp::RouteModel::rsmt && !skipped.uroute_skipped.empty(),
        "explicit URoute did not skip to RSMT without support");
  o.uroute                 = context;
  o.route.model            = dp::RouteModel::rsmt;
  o.route.probe            = true;
  o.route.probe_model      = dp::RouteModel::uroute;
  o.route.probe_iterations = 20;
  o.route.target_overflow  = 0;
  o.max_iterations         = 100;
  o.min_iterations         = 20;
  d                        = fixture();
  const auto failed        = dp::place(d, o);
  check(failed.route_probed && failed.route_model == dp::RouteModel::uroute && failed.route_uroute_switch_iteration > 0
            && failed.route_probe_overflow > 0
            && failed.iterations <= failed.route_uroute_switch_iteration + o.route.probe_iterations,
        "failed URoute probe did not use bounded continuation");
  auto low_labels = labels;
  std::fill(low_labels.values.begin(), low_labels.values.end(), .25f);
  std::fill(low_labels.ranges.begin(), low_labels.ranges.end(), std::array<double, 2>{.25, .25});
  o.uroute          = std::make_shared<const dp::UrouteContext>(weights,
                                                                std::vector<dp::UrouteSupport>{
                                                                    {"passing-probe", features, low_labels}
  });
  d                 = fixture();
  const auto passed = dp::place(d, o);
  check(passed.route_probed && passed.route_probe_overflow == 0 && passed.route_uroute_switch_iteration < 0
            && passed.route_model == dp::RouteModel::rsmt,
        "passing URoute probe unnecessarily latched feedback");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc >= 7 && std::string(argv[1]) == "predict" && (argc - 5) % 2 == 0) {
      const auto                     weights = std::make_shared<const dp::UrouteWeights>(argv[2]);
      const auto                     query   = dp::read_uroute_maps(argv[3]);
      std::vector<dp::UrouteSupport> support;
      for (int i = 5; i < argc; i += 2) {
        support.push_back({std::to_string(i), dp::read_uroute_maps(argv[i]), dp::read_uroute_maps(argv[i + 1])});
      }
      const dp::UrouteContext context(weights, std::move(support));
      const auto              prediction = context.predict(query);
      const auto f32 = [](const std::vector<double>& values) { return std::vector<float>(values.begin(), values.end()); };
      dp::write_uroute_maps(
          argv[4],
          raw_maps(prediction.bins_x, prediction.bins_y, {f32(prediction.utilization), f32(prediction.drc_density)}));
      return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "trees") {
      std::ifstream       input(argv[2]);
      const std::string   text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
      rapidjson::Document doc;
      doc.Parse(text.c_str());
      if (!input || doc.HasParseError()) {
        throw std::invalid_argument("invalid geometry JSON");
      }
      dp::RudyOptions options;
      options.degree     = 4096;
      options.initialize = livehd::placement::initialize_flute_tables;
      std::ofstream output(argv[3]);
      output << std::setprecision(17) << '[';
      bool first = true;
      for (const auto& net : field(doc, "nets").GetArray()) {
        if (!first) {
          output << ',';
        }
        first = false;
        std::vector<std::pair<double, double>> pins;
        for (const auto& pin : field(net, "pins").GetArray()) {
          pins.emplace_back(number(pin[0]), number(pin[1]));
        }
        if ((!net.HasMember("enabled") || flag(net, "enabled")) && pins.size() >= 2) {
          if (pins.size() > size_t(options.degree)) {
            throw std::invalid_argument("detour FLUTE degree exceeds 4096");
          }
          double length = 0;
          for (const auto& edge : dp::detail::route_tree(pins, options)) {
            length += edge.xh - edge.xl + edge.yh - edge.yl;
          }
          output << length;
        } else {
          output << "null";
        }
      }
      output << "]\n";
      if (!output) {
        throw std::runtime_error("cannot write FLUTE lengths");
      }
      return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "ladder-check") {
      ladder_check(std::make_shared<const dp::UrouteWeights>(argv[2]),
                   dp::read_uroute_maps(argv[3]),
                   dp::read_uroute_maps(argv[4]));
      return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "route-check") {
      auto       weights  = std::make_shared<const dp::UrouteWeights>(argv[2]);
      auto       features = dp::read_uroute_maps(argv[3]);
      auto       labels   = dp::read_uroute_maps(argv[4]);
      // Constant physical labels make calibration analytic even with arbitrary
      // test weights. A full-height macro blocks the two-pin physical net.
      dp::Design d;
      d.layout             = {0, 0, 16, 16, 1, 1};
      d.node_x             = {1, 15, 6};
      d.node_y             = {2, 14, 0};
      d.node_size_x        = {0, 0, 4};
      d.node_size_y        = {0, 0, 16};
      d.node_macro         = {0, 0, 1};
      d.pin2node           = {0, 1};
      d.pin_offset_x       = {0, 0};
      d.pin_offset_y       = {0, 0};
      d.flat_net2pin       = {0, 1};
      d.flat_net2pin_start = {0, 2};
      d.net_weights        = {1};
      d.build_maps();
      dp::RudyOptions options{2, 2, .1, .2, true, true};
      options.model      = dp::RudyModel::rsmt;
      const auto base    = dp::rudy(d, options);
      options.model      = dp::RudyModel::uroute;
      const auto skipped = dp::rudy(d, options);
      if (skipped.horizontal != base.horizontal || skipped.vertical != base.vertical || skipped.overflow != base.overflow
          || skipped.pin_utilization != base.pin_utilization || skipped.uroute_skipped.empty()) {
        throw std::runtime_error("URoute skip changed RSMT behavior");
      }
      double off_overflow = 0;
      for (double drc_level : {0., 1.}) {
        auto lower = labels, upper = labels;
        for (size_t i = 0; i < labels.values.size(); ++i) {
          lower.values[i] *= .5f;
          upper.values[i] *= 1.5f;
        }
        for (size_t c = 0; c < labels.ranges.size(); ++c) {
          for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
            lower.ranges[c][endpoint] *= .5;
            upper.ranges[c][endpoint] *= 1.5;
          }
        }
        const std::vector<dp::UrouteSupport> samples{
            {"lower", features, lower},
            {"upper", features, upper}
        };
        options.uroute    = std::make_shared<const dp::UrouteContext>(weights, samples, drc_level);
        const auto result = dp::rudy(d, options);
        if (result.pin_utilization != base.pin_utilization || result.blocked_pins != base.blocked_pins
            || result.unroutable_nets != base.unroutable_nets || result.detour_wirelength != base.detour_wirelength
            || result.blocked_fraction != base.blocked_fraction || result.drc_density != std::vector<double>(4, 4.)) {
          throw std::runtime_error("URoute changed physical RSMT terms or DRC calibration");
        }
        double expected = double(base.blocked_pins + base.unroutable_nets) / 4;
        for (size_t i = 0; i < 4; ++i) {
          if (std::abs(std::max(result.horizontal[i], result.vertical[i]) - 2.) > 1e-10) {
            throw std::runtime_error("URoute directional calibration mismatch");
          }
          if (base.horizontal[i] > 0 && base.vertical[i] > 0
              && std::abs(result.horizontal[i] / result.vertical[i] - base.horizontal[i] / base.vertical[i]) > 1e-10) {
            throw std::runtime_error("URoute lost the RSMT directional ratio");
          }
          expected += std::max(0., std::max(2., base.pin_utilization[i]) - 1) / 4;
        }
        if (drc_level > 0) {
          expected += 3.;
        }
        if (std::abs(result.overflow - expected) > 1e-10) {
          throw std::runtime_error("URoute overflow calibration mismatch");
        }
        if (drc_level == 0) {
          off_overflow = result.overflow;
        } else if (std::abs(result.overflow - off_overflow - 3.) > 1e-10) {
          throw std::runtime_error("URoute DRC report-only gating failed");
        }
      }
      return 0;
    }
    if (argc >= 6 && (std::string(argv[1]) == "infer" || std::string(argv[1]) == "infer-reuse")) {
      const dp::UrouteWeights     weights(argv[2]);
      const auto                  query = dp::read_uroute_maps(argv[3]);
      std::vector<dp::UrouteMaps> support;
      for (int i = 4; i < argc - 1; ++i) {
        support.push_back(dp::read_uroute_maps(argv[i]));
      }
      std::vector<float> values;
      if (std::string(argv[1]) == "infer-reuse") {
        using Clock                   = std::chrono::steady_clock;
        const auto              start = Clock::now();
        const dp::UrouteNetwork network(weights, support);
        const auto              initialized = Clock::now();
        values                              = network.infer(query);
        const auto first                    = Clock::now();
        auto       changed                  = query;
        std::reverse(changed.values.begin(), changed.values.end());
        const auto reused = network.infer(changed);
        const auto second = Clock::now();
        const auto fresh  = dp::uroute_infer(weights, changed, support);
        const auto third  = Clock::now();
        // The cached encoder owns its support snapshot. It must remain valid
        // after caller-owned maps disappear and must not cache a query result.
        support.clear();
        if (reused != fresh || network.infer(query) != values) {
          throw std::runtime_error("URoute immutable support reuse changed predictions");
        }
        const auto seconds = [](auto a, auto b) { return std::chrono::duration<double>(b - a).count(); };
        std::cout << std::setprecision(9) << "{\"support_encoding_s\":" << seconds(start, initialized)
                  << ",\"first_query_s\":" << seconds(initialized, first) << ",\"reused_query_s\":" << seconds(first, second)
                  << ",\"fresh_encoding_and_query_s\":" << seconds(second, third) << "}\n";
      } else {
        values = dp::uroute_infer(weights, query, support);
      }
      dp::write_uroute_maps(argv[argc - 1], raw_maps(query.bins_x, query.bins_y, {values}));
      return 0;
    }
    if (argc != 8 || std::string(argv[1]) != "features") {
      throw std::invalid_argument("usage: native features geometry.json output.bin route_x route_y output_x output_y");
    }
    std::ifstream input(argv[2]);
    if (!input) {
      throw std::runtime_error("cannot open geometry");
    }
    const std::string   text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    if (doc.HasParseError()) {
      throw std::invalid_argument("invalid geometry JSON");
    }
    const auto maps
        = dp::uroute_features(geometry(doc), std::stoi(argv[4]), std::stoi(argv[5]), std::stoi(argv[6]), std::stoi(argv[7]));
    dp::write_uroute_maps(argv[3], maps);
    const auto loaded = dp::read_uroute_maps(argv[3]);
    if (loaded.values != maps.values || loaded.ranges != maps.ranges) {
      throw std::runtime_error("native map round trip mismatch");
    }
  } catch (const std::exception& e) {
    std::cerr << "uroute: " << e.what() << '\n';
    return 1;
  }
}
