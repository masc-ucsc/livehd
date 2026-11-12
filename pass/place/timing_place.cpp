// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "placement_technology.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair.hpp"
namespace livehd::placement {
namespace {
std::map<std::string, double> data_timing_weights(const physical::Parasitics& nets, const Timing_feedback& timing, double min_weight,
                                                  double max_weight, const Prects_networks* controls) {
  if (!std::isfinite(max_weight) || max_weight < 1 || max_weight > max_net_weight) {
    throw std::invalid_argument("maximum timing weight must be finite and in [1, 100]");
  }
  if (!std::isfinite(min_weight) || min_weight <= 0 || min_weight > 1) {
    throw std::invalid_argument("minimum timing weight must be finite and in (0, 1]");
  }
  std::vector<std::pair<double, Pin_label>> ranked;
  ranked.reserve(nets.nets.size());
  const auto*       indexed = timing.ranking && timing.ranking->parasitics.get() == &nets ? &*timing.ranking : nullptr;
  // Borrow feedback: filtering controls must not duplicate all detailed pin
  // records or ranking values. Indexed rankings need only one exclusion bit
  // per pin; a different RC snapshot retains the label-based fallback below.
  std::vector<bool> excluded;
  if (indexed && controls && !controls->pins.empty()) {
    excluded.resize(indexed->pins.size());
    for (const auto& label : controls->pins) {
      const auto index = indexed->layout->find(label);
      if (index < excluded.size()) {
        excluded[index] = true;
      }
    }
  }
  for (size_t net_index = 0; net_index < nets.nets.size(); ++net_index) {
    const auto& net   = nets.nets[net_index];
    double      slack = std::numeric_limits<double>::infinity();
    bool        clock = false;
    if (indexed) {
      const auto& layout = *indexed->layout;
      for (size_t i = layout.net_offsets.at(net_index); i < layout.net_offsets.at(net_index + 1); ++i) {
        const auto index = layout.net_pins[i];
        if (!excluded.empty() && excluded.at(index)) {
          continue;
        }
        const auto& pin  = indexed->pins.at(index);
        clock           |= pin.clock;
        slack            = std::min(slack, pin.slack);
      }
    } else {
      for (const auto& node : net.nodes) {
        if (controls && controls->pins.contains(node.pin)) {
          continue;
        }
        if (timing.ranking) {
          if (const auto* pin = timing.ranking->find(node.pin)) {
            clock |= pin->clock;
            slack  = std::min(slack, pin->slack);
          }
        } else if (const auto it = timing.find_label(node.pin); it != nullptr) {
          clock |= it->clock;
          if (it->slack) {
            slack = std::min(slack, *it->slack);
          }
        }
      }
    }
    if (!clock && std::isfinite(slack)) {
      ranked.emplace_back(slack, net.driver);
    }
  }
  if (ranked.empty()) {
    return {};
  }
  // Every timed non-clock net gets a weight from its slack RANK, never from the sign or scale of the slack: designs
  // where all nets fail (or all pass) still order their nets. Piecewise linear in the rank percentile p (0 = worst
  // slack, 1 = best): the worst net weighs max_weight, the median exactly 1, the best min_weight. Nets with equal slack
  // share their mean rank, so ties never split.
  std::sort(ranked.begin(), ranked.end());
  const size_t                  n = ranked.size();
  std::map<std::string, double> target;
  for (size_t begin = 0; begin < n;) {
    size_t end = begin + 1;
    while (end < n && ranked[end].first == ranked[begin].first) {
      ++end;
    }
    const double p = n > 1 ? .5 * double(begin + end - 1) / double(n - 1) : .5;  // mean rank / (n-1)
    const double w = p <= .5 ? max_weight - (max_weight - 1) * (p / .5) : 1 - (1 - min_weight) * ((p - .5) / .5);
    for (size_t i = begin; i < end; ++i) {
      target[std::string(ranked[i].second)] = w;
    }
    begin = end;
  }
  return target;
}
}  // namespace
std::map<std::string, double> timing_weights(const physical::Parasitics& nets, const Timing_feedback& timing, double min_weight,
                                             double max_weight) {
  return data_timing_weights(nets, timing, min_weight, max_weight, nullptr);
}
void refresh_timing_weights(Result& state, const Options& options, const Timing_feedback& timing) {
  auto roles = state.floorplan.nets.empty() ? Prects_library{} : prects_library(options.technology().cells, options.control_ports);
  roles.reset_is_data = pcrs_active(options);
  refresh_timing_weights(state, options, timing, roles);
}
void refresh_timing_weights(Result& state, const Options& options, const Timing_feedback& timing,
                            const Prects_library& library_roles) {
  std::optional<Prects_networks> controls;
  if (!state.floorplan.nets.empty()) {
    const auto& tech  = options.technology().lef;
    auto        roles = library_roles;
    if (!roles.override_controls) {
      roles.ports.insert(state.prects.ports.begin(), state.prects.ports.end());
      roles.pins.insert(state.prects.pins.begin(), state.prects.pins.end());
    }
    controls = prects_networks(tech, state.floorplan, roles, timing);
  }
  const auto                    target = data_timing_weights(*state.parasitics,
                                                             timing,
                                                             options.timing_min_weight,
                                                             options.timing_max_weight,
                                                             controls ? &*controls : nullptr);
  std::map<std::string, double> current;
  for (const auto& net : state.parasitics->nets) {
    auto       key  = net.driver.str();  // one label string per net, not one per lookup
    const auto next = target.find(key);
    const auto old  = state.net_weights.find(key);
    const auto weight = next == target.end() ? 1
                                             : (1 - options.timing_smoothing) * (old == state.net_weights.end() ? 1 : old->second)
                                                   + options.timing_smoothing * next->second;
    current[std::move(key)] = weight;
  }
  state.net_weights = std::move(current);
}
void inherit_buffer_weights(std::map<std::string, double>& weights, const std::string& driver,
                            const std::vector<std::string>& outputs) {
  if (outputs.empty()) {
    return;
  }
  const auto   old     = weights.find(driver);
  const double reduced = 1 + .5 * ((old == weights.end() ? 1 : old->second) - 1);
  weights[driver]      = reduced;
  for (const auto& output : outputs) {
    weights[output] = reduced;
  }
}
Repair_result timing_place(Result initial, const Options& requested, const Evaluate& evaluate, unsigned rounds,
                           std::optional<Timing_feedback> prior, const Trial_admission& admit_trial) {
  std::optional<Options> resolved_options;
  if (requested.cell_halo) {
    resolved_options = final_cell_options(requested);
  }
  const auto& options = resolved_options ? *resolved_options : requested;
  if (options.cell_halo) {
    check_placed_legality(initial, options);
  }
  if (!options.full || rounds == 0 || rounds > 8) {
    throw std::invalid_argument("timing placement requires full mode and 1..8 rounds");
  }
  Repair_result result;
  result.placed             = std::move(initial);
  auto         best         = prior && !prior->ranking_only ? std::move(*prior) : evaluate(result.placed);
  const double initial_area = best.area;
  const auto   hpwl         = [](const Result& placed) {
    rapidjson::Document report;
    report.Parse(placed.qor.c_str());
    if (!report.IsObject() || !report.HasMember("hpwl_um") || !report["hpwl_um"].IsNumber()) {
      throw std::invalid_argument("timing refinement requires measured placement HPWL");
    }
    return report["hpwl_um"].GetDouble();
  };
  const double        initial_hpwl = hpwl(result.placed);
  rapidjson::Document initial_qor;
  initial_qor.Parse(result.placed.qor.c_str());
  unsigned                total_gp_iterations = initial_qor.HasMember("gp_iterations") ? initial_qor["gp_iterations"].GetUint() : 0;
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("model");
  w.String("slack_weighted_placement");
  w.Key("timing_min_weight");
  w.Double(options.timing_min_weight);
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
  w.Key("skipped_setup_met");
  w.Bool(best.wns && *best.wns >= 0);
  w.Key("before_delay");
  w.Double(best.delay);
  w.Key("rounds");
  w.StartArray();
  std::map<std::string, double> weights   = result.placed.net_weights;
  unsigned                      accepted  = 0;
  bool                          unconverged_refusal = false;
  const auto                    directory = options.output_dir + "/timing_place";
  std::filesystem::create_directories(directory);
  // An accepted round already refreshed toward the new best; smoothing again
  // at the next round start would apply the step twice.
  bool refreshed = false;
  // Positive setup slack already meets the SDC objective. A cold or warm
  // timing round has no setup violation to fix in this case.
  for (unsigned round = 0; round < rounds && (!best.wns || *best.wns < 0); ++round) {
    if (admit_trial && !admit_trial()) {
      break;
    }
    auto target = timing_weights(*result.placed.parasitics, best, options.timing_min_weight, options.timing_max_weight);
    if (target.empty()) {
      break;
    }
    // pass.pcrs: a reset path on the worst setup path is fixed by a deeper reset tree, so the round's placement
    // starts from it. The last round may instead take a level away from a domain with slack to spare.
    std::string              pcrs_grew;
    std::vector<std::string> pcrs_shrunk;
    // The depth decisions describe the placement that is kept: a round that is not accepted takes its decisions
    // (and its one-time regroup) back, or a later zero-GP call would rebuild the trees to match them, unplaced.
    const auto pcrs_before = options.pcrs_depth ? std::optional{*options.pcrs_depth} : std::nullopt;
    if (pcrs_active(options) && options.pcrs_depth && result.placed.pcrs_stats) {
      pcrs_grew = pcrs::retarget_for_wns(*result.placed.pcrs_stats, best.path, *options.pcrs_depth);
      if (pcrs_grew.empty() && round + 1 == rounds) {
        pcrs_shrunk = pcrs::retarget_shrink(
            *result.placed.pcrs_stats,
            [&](std::string_view label) {
              const auto* pin = best.find_label(label);
              return pin ? pcrs::Pin_timing{pin->slack, pin->arrival} : pcrs::Pin_timing{};
            },
            0.0,
            *options.pcrs_depth);
      }
    }
    if (!refreshed) {
      refresh_timing_weights(result.placed, options, best);
    }
    refreshed  = false;
    weights    = result.placed.net_weights;
    auto trial = options;
    trial.cache_dir.clear();
    trial.previous.reset();
    trial.changed_cells.clear();
    trial.net_weights      = weights;
    trial.iterations       = std::clamp(options.iterations / static_cast<int>(rounds), 1, 100);
    trial.min_iterations   = std::min(trial.iterations, 20);
    trial.final_iterations = trial.iterations;
    trial.fp_place = trial.final_cells = trial.fp_auto = false;
    trial.cell_stage                   = true;
    trial.fp_evaluate                  = {};
    trial.gp_rank                      = {};
    trial.gp_resize                    = false;
    trial.warm_refinement              = true;
    trial.density_weight_seed          = result.placed.gp_density_weight;
    trial.output_dir                   = directory + "/round_" + std::to_string(round);
    trial.floorplan                    = directory + "/floorplan.def";
    auto frame                         = result.placed.floorplan;
    for (auto& terminal : frame.terminals) {
      terminal.status = Ann_place::Status::FIXED;
    }
    physical::write_def(frame, trial.floorplan);
    trial.restart_seed = std::make_shared<physical::Floorplan>(std::move(frame));
    w.StartObject();
    if (!pcrs_grew.empty() || !pcrs_shrunk.empty()) {
      w.Key("pcrs");
      w.StartObject();
      w.Key("deeper");
      w.String(pcrs_grew.c_str());
      w.Key("shallower");
      w.StartArray();
      for (const auto& domain : pcrs_shrunk) {
        w.String(domain.c_str());
      }
      w.EndArray();
      w.EndObject();
    }
    w.Key("weighted_nets");
    w.Uint64(target.size());
    w.Key("weights");
    w.StartObject();
    for (const auto& [name, weight] : weights) {
      w.Key(name.c_str());
      w.Double(weight);
    }
    w.EndObject();
    bool keep      = false;
    bool exhausted = false;
    try {
      auto                owner          = std::make_unique<hhds::GraphLibrary>();
      auto                candidate      = place(*result.placed.graph, *owner, trial);
      const double        candidate_hpwl = hpwl(candidate);
      bool                bounded        = candidate_hpwl <= initial_hpwl * 1.03 + 1e-9;
      rapidjson::Document geometry;
      geometry.Parse(candidate.qor.c_str());
      const bool converged     = geometry.HasMember("gp_converged") && geometry["gp_converged"].GetBool();
      // A short warm solve need not reach a stationary objective before STA
      // can judge its legal result. Require density feasibility; routability
      // flows retain the solver's complete convergence gate.
      const bool density_ready = converged
                                 || (!options.routability && geometry.HasMember("gp_overflow") && geometry["gp_overflow"].IsNumber()
                                     && geometry["gp_overflow"].GetDouble() <= options.stop_overflow);
      // A rejected geometric retry would restart from the same accepted state.
      exhausted                = !density_ready || !bounded;
      unconverged_refusal      = unconverged_refusal || !density_ready;
      w.Key("density_ready_for_timing");
      w.Bool(density_ready);
      for (const auto& member : geometry.GetObject()) {
        if (std::string_view(member.name.GetString()).starts_with("gp_")) {
          w.Key(member.name.GetString());
          member.value.Accept(w);
        }
      }
      if (geometry.HasMember("gp_iterations")) {
        total_gp_iterations += geometry["gp_iterations"].GetUint();
      }
      w.Key("hpwl_um");
      w.Double(candidate_hpwl);
      // Geometry already rules these candidates out. Avoid a detailed STA
      // update for a placement that cannot pass the acceptance guard.
      std::optional<Timing_feedback> score;
      if (density_ready && bounded) {
        score   = evaluate(candidate);
        bounded = score->area <= initial_area * 1.03 + 1e-9;
        w.Key("delay");
        w.Double(score->delay);
        if (score->wns) {
          w.Key("setup_wns");
          w.Double(*score->wns);
        }
        if (score->tns) {
          w.Key("setup_tns");
          w.Double(*score->tns);
        }
      }
      w.Key("timing_evaluated");
      w.Bool(score.has_value());
      w.Key("growth_bounded");
      w.Bool(bounded);
      keep = score && bounded && timing_improves(best, *score);
      if (keep) {
        result.placed = std::move(candidate);
        result.libraries.push_back(std::move(owner));
        best = std::move(*score);
        refresh_timing_weights(result.placed, options, best);
        refreshed = true;
        ++accepted;
      }
    } catch (const std::exception& error) {
      w.Key("reason");
      w.String(error.what());
    }
    if (!keep && pcrs_before) {
      *options.pcrs_depth = *pcrs_before;
    }
    w.Key("accepted");
    w.Bool(keep);
    w.Key("stopped_unconverged");
    w.Bool(exhausted);
    w.EndObject();
    if (exhausted) {
      break;
    }
  }
  w.EndArray();
  w.Key("accepted");
  w.Uint(accepted);
  w.Key("timing_budget_exhausted");
  w.Bool(admit_trial && !admit_trial());
  w.Key("after_delay");
  w.Double(best.delay);
  w.EndObject();
  result.report         = buffer.GetString();
  result.gp_unconverged = unconverged_refusal && accepted == 0;
  rapidjson::Document q;
  q.Parse(result.placed.qor.c_str());
  // The primary GP's convergence and cost must survive an accepted short
  // refinement. Each attempted round reports its own GP metrics above.
  for (const auto& member : initial_qor.GetObject()) {
    if (std::string_view(member.name.GetString()).starts_with("gp_")) {
      q.RemoveMember(member.name.GetString());
      q.AddMember(rapidjson::Value(member.name, q.GetAllocator()),
                  rapidjson::Value(member.value, q.GetAllocator()),
                  q.GetAllocator());
    }
  }
  q.AddMember("gp_total_iterations", total_gp_iterations, q.GetAllocator());
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
