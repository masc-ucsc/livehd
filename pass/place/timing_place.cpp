// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair.hpp"
namespace livehd::placement {
std::map<std::string, double> timing_weights(const physical::Parasitics& nets, const Timing_feedback& timing, double fraction,
                                             double max_weight) {
  if (!std::isfinite(fraction) || fraction <= 0 || fraction > 1) {
    throw std::invalid_argument("timing fraction must be in (0, 1]");
  }
  if (!std::isfinite(max_weight) || max_weight < 1 || max_weight > max_net_weight) {
    throw std::invalid_argument("maximum timing weight must be finite and in [1, 100]");
  }
  std::vector<std::pair<double, std::string>> ranked;
  for (const auto& net : nets.nets) {
    double slack = std::numeric_limits<double>::infinity();
    bool   clock = false;
    for (const auto& node : net.nodes) {
      if (const auto it = timing.pins.find(node.pin); it != timing.pins.end()) {
        clock |= it->second.clock;
        if (it->second.slack) {
          slack = std::min(slack, *it->second.slack);
        }
      }
    }
    if (!clock && std::isfinite(slack)) {
      ranked.emplace_back(slack, net.driver);
    }
  }
  std::sort(ranked.begin(), ranked.end());
  if (ranked.empty()) {
    return {};
  }
  // Preserve the existing default exactly. One-ulp product roundoff (e.g.
  // 0.14 * 100) must not select an extra net at an integer cutoff.
  const auto                    count    = std::ceil(std::nextafter(fraction * ranked.size(), 0.));
  const size_t                  selected = fraction == .2 ? std::max<size_t>(1, (ranked.size() + 4) / 5)
                                                          : std::clamp<size_t>(static_cast<size_t>(count), 1, ranked.size());
  const double                  worst = ranked.front().first, cutoff = ranked[selected - 1].first;
  std::map<std::string, double> target;
  for (size_t i = 0; i < selected; ++i) {
    const double priority    = cutoff > worst + 1e-12 ? (cutoff - ranked[i].first) / (cutoff - worst) : 1;
    target[ranked[i].second] = 1 + (max_weight - 1) * priority;
  }
  return target;
}
Repair_result timing_place(Result initial, const Options& options, const Evaluate& evaluate, unsigned rounds) {
  if (!options.full || rounds == 0 || rounds > 8) {
    throw std::invalid_argument("timing placement requires full mode and 1..8 rounds");
  }
  Repair_result result;
  result.placed                                   = std::move(initial);
  auto                                       best = evaluate(result.placed);
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("model");
  w.String("slack_weighted_placement");
  w.Key("timing_fraction");
  w.Double(options.timing_fraction);
  w.Key("timing_max_weight");
  w.Double(options.timing_max_weight);
  w.Key("timing_smoothing");
  w.Double(options.timing_smoothing);
  w.Key("initial_weights");
  w.StartObject();
  for (const auto& [name, weight] : result.placed.net_weights) {
    w.Key(name.c_str());
    w.Double(weight);
  }
  w.EndObject();
  w.Key("initial_timing_source");
  w.String(result.placed.net_weights.empty() ? "first_placement" : "post_synthesis_zero_wire");
  w.Key("before_delay");
  w.Double(best.delay);
  w.Key("rounds");
  w.StartArray();
  std::map<std::string, double> weights   = result.placed.net_weights;
  unsigned                      accepted  = 0;
  const auto                    directory = options.output_dir + "/timing_place";
  std::filesystem::create_directories(directory);
  for (unsigned round = 0; round < rounds; ++round) {
    auto target = timing_weights(*result.placed.parasitics, best, options.timing_fraction, options.timing_max_weight);
    if (target.empty()) {
      break;
    }
    for (const auto& net : result.placed.parasitics->nets) {
      weights.try_emplace(net.driver, 1);
    }
    for (auto& [name, weight] : weights) {
      weight = (1 - options.timing_smoothing) * weight + options.timing_smoothing * (target.contains(name) ? target.at(name) : 1);
    }
    auto trial = options;
    trial.cache_dir.clear();
    trial.previous.reset();
    trial.changed_cells.clear();
    trial.net_weights = weights;
    trial.iterations  = std::max(1, options.iterations / static_cast<int>(rounds));
    trial.output_dir  = directory + "/round_" + std::to_string(round);
    trial.floorplan   = directory + "/floorplan.def";
    auto frame        = result.placed.floorplan;
    std::erase_if(frame.components, [](const auto& c) {
      return c.place.status != Ann_place::Status::FIXED && c.place.status != Ann_place::Status::LOCKED;
    });
    for (auto& terminal : frame.terminals) {
      terminal.status = Ann_place::Status::FIXED;
    }
    physical::write_def(frame, trial.floorplan);
    w.StartObject();
    w.Key("weighted_nets");
    w.Uint64(target.size());
    w.Key("weights");
    w.StartObject();
    for (const auto& [name, weight] : weights) {
      w.Key(name.c_str());
      w.Double(weight);
    }
    w.EndObject();
    bool keep = false;
    try {
      auto       owner     = std::make_unique<hhds::GraphLibrary>();
      auto       candidate = place(*result.placed.graph, *owner, trial);
      const auto score     = evaluate(candidate);
      w.Key("delay");
      w.Double(score.delay);
      keep = timing_improves(best, score);
      if (keep) {
        result.placed = std::move(candidate);
        result.libraries.push_back(std::move(owner));
        best = score;
        ++accepted;
      }
    } catch (const std::exception& error) {
      w.Key("reason");
      w.String(error.what());
    }
    w.Key("accepted");
    w.Bool(keep);
    w.EndObject();
  }
  w.EndArray();
  w.Key("accepted");
  w.Uint(accepted);
  w.Key("after_delay");
  w.Double(best.delay);
  w.EndObject();
  result.report = buffer.GetString();
  rapidjson::Document q;
  q.Parse(result.placed.qor.c_str());
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  q.AddMember("timing_driven", rapidjson::Value(report, q.GetAllocator()), q.GetAllocator());
  rapidjson::StringBuffer                    out;
  rapidjson::Writer<rapidjson::StringBuffer> writer(out);
  q.Accept(writer);
  result.placed.qor = out.GetString();
  physical::write_def(result.placed.floorplan, options.output_dir + "/out.def");
  std::ofstream(options.output_dir + "/qor.json") << result.placed.qor << '\n';
  std::ofstream(options.output_dir + "/timing_driven.json") << result.report << '\n';
  result.timing = std::move(best);
  return result;
}
}  // namespace livehd::placement
