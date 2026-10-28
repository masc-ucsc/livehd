// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "custom_sizing.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>

#include "multirow.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair_internal.hpp"
#include "resize_engine.hpp"

namespace livehd::placement {
namespace {
using namespace detail;
namespace fs = std::filesystem;
using Clock  = std::chrono::steady_clock;
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

struct Custom_instance {
  const Custom_variant* variant = nullptr;
  Node                  node;
};
// Every instance of a catalog variant in the placed netlist, by name.
std::map<std::string, Custom_instance> custom_instances(const Result& state, const Custom_catalog& catalog) {
  std::map<std::string, Custom_instance> out;
  for (auto n : state.graph->body().nodes()) {
    const auto io = n.get_subnode_io();
    if (const auto* variant = io ? catalog.find(io->get_name()) : nullptr) {
      out.emplace(std::string(n.get_name()), Custom_instance{variant, n});
    }
  }
  return out;
}

enum class Move_kind { up, down, buffer };
const char* move_name(Move_kind kind) { return kind == Move_kind::up ? "up" : kind == Move_kind::down ? "down" : "buffer"; }
struct Move {
  Move_kind   kind = Move_kind::up;
  std::string instance, from;
  Resize_edit edit;
};
// The worst-slack sink keeps the instance's output; the others move behind a
// buffer seeded at the instance's center. nullopt below two sinks.
std::optional<Resize_edit> output_buffer(const Result& state, const Timing_feedback& score, const std::string& name,
                                         const Custom_instance& instance, const std::string& master,
                                         const std::set<std::string>& taken) {
  const auto                                  io  = instance.node.get_subnode_io();
  const auto                                  out = instance.node.get_driver_pin(io->get_output_port_id(instance.variant->output));
  std::vector<std::pair<double, std::string>> labels;
  for (auto sink : sinks(out)) {
    auto label = physical_label(sink);
    if (label.empty()) {
      continue;
    }
    const auto it = score.find_label(label);
    labels.emplace_back(it != nullptr && it->slack ? *it->slack * score.time_unit_ps : std::numeric_limits<double>::infinity(),
                        std::move(label));
  }
  if (labels.size() < 2) {
    return std::nullopt;
  }
  std::sort(labels.begin(), labels.end());
  const auto cell = std::find_if(state.floorplan.components.begin(), state.floorplan.components.end(), [&](const auto& c) {
    return c.name == name;
  });
  if (cell == state.floorplan.components.end()) {
    throw std::runtime_error("domino sizing lost the placed instance " + name);
  }
  Resize_edit edit;
  edit.kind = Resize_edit::Kind::buffer;
  for (unsigned serial = 0; edit.instance.empty() || taken.contains(edit.instance); ++serial) {
    edit.instance = std::format("{}_obuf{}", name, serial);
  }
  edit.master   = master;
  edit.driver   = physical_label(out);
  edit.owner    = name;
  edit.position = {cell->place.x + cell->place.width / 2., cell->place.y + cell->place.height / 2.};
  for (size_t i = 1; i < labels.size(); ++i) {
    edit.sinks.push_back(labels[i].second);
  }
  return edit;
}

void counts(Writer& w, const std::map<std::string, Custom_instance>& instances) {
  std::map<std::string, unsigned> by_variant;
  for (const auto& [name, instance] : instances) {
    (void)name;
    ++by_variant[instance.variant->name];
  }
  w.StartObject();
  for (const auto& [variant, count] : by_variant) {
    w.Key(variant.c_str());
    w.Uint(count);
  }
  w.EndObject();
}
// C_clk of the custom instances from the catalog's labeled clock_ff;
// nullopt when one is unknown.
std::optional<double> clock_ff(const std::map<std::string, Custom_instance>& instances) {
  double total = 0;
  for (const auto& [name, instance] : instances) {
    (void)name;
    if (!instance.variant->clock_ff) {
      return std::nullopt;
    }
    total += *instance.variant->clock_ff;
  }
  return total;
}
// The two-row ECO of the accepted trials. Each trial's place() reports only
// its own ECO (qor.multirow_eco), so the run's total is summed here.
struct Eco_total {
  unsigned                        accepted_trials = 0;
  uint64_t                        cells = 0, displaced_cells = 0;
  int64_t                         released_sites = 0, grown_sites = 0;
  double                          max_displacement_um = 0;
  std::map<std::string, uint64_t> outcomes;
};
constexpr std::array kEcoOutcomes{Multirow_eco_cell::Outcome::in_place,
                                  Multirow_eco_cell::Outcome::shifted,
                                  Multirow_eco_cell::Outcome::displaced,
                                  Multirow_eco_cell::Outcome::re_anchored};
// Writes a trial's ECO block as "eco" (null without one) and, when `total`
// is given, adds it there.
void                 trial_eco(Writer& w, const std::string& qor, Eco_total* total) {
  rapidjson::Document doc;
  doc.Parse(qor.c_str());
  w.Key("eco");
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("multirow_eco") || !doc["multirow_eco"].IsObject()) {
    w.Null();
    return;
  }
  const auto& block = doc["multirow_eco"];
  block.Accept(w);
  if (!total) {
    return;
  }
  const auto count = [&](std::string_view key) -> int64_t {
    const auto it = block.FindMember(rapidjson::StringRef(key.data(), key.size()));
    return it != block.MemberEnd() && it->value.IsInt64() ? it->value.GetInt64() : 0;
  };
  ++total->accepted_trials;
  total->cells           += static_cast<uint64_t>(count("cells"));
  total->displaced_cells += static_cast<uint64_t>(count("displaced_cells"));
  total->released_sites  += count("released_sites");
  total->grown_sites     += count("grown_sites");
  if (const auto it = block.FindMember("max_displacement_um"); it != block.MemberEnd() && it->value.IsNumber()) {
    total->max_displacement_um = std::max(total->max_displacement_um, it->value.GetDouble());
  }
  for (const auto outcome : kEcoOutcomes) {
    total->outcomes[std::string(multirow_eco_outcome_name(outcome))]
        += static_cast<uint64_t>(count(multirow_eco_outcome_name(outcome)));
  }
}
double period_ps(const Timing_feedback& f) { return f.delay * f.time_unit_ps; }
// No more electrical violations, and with as many no larger an excess:
// resize_accepts' electrical gate.
bool   electrical_holds(const Timing_feedback& before, const Timing_feedback& after) {
  return after.violations < before.violations
         || (after.violations == before.violations && after.electrical_excess <= before.electrical_excess + 1e-6);
}
void real(Writer& w, const char* key, std::optional<double> value) {
  w.Key(key);
  if (value && std::isfinite(*value)) {
    w.Double(*value);
  } else {
    w.Null();
  }
}
void feedback(Writer& w, const char* key, const Timing_feedback& f) {
  w.Key(key);
  w.StartObject();
  real(w, "delay_ps", f.delay * f.time_unit_ps);
  real(w, "wns_ps", f.wns ? std::optional<double>{*f.wns * f.time_unit_ps} : std::nullopt);
  real(w, "tns_ps", f.tns ? std::optional<double>{*f.tns * f.time_unit_ps} : std::nullopt);
  w.Key("violations");
  w.Uint(f.violations);
  real(w, "area", f.area);
  w.EndObject();
}
}  // namespace

Repair_result size_custom_placed(Result legal, const Options& requested, const Evaluate& evaluate,
                                 const Custom_sizing_options& sizing) {
  if (!requested.custom_cells) {
    throw std::invalid_argument("custom variant sizing needs the custom cell catalog");
  }
  if (!requested.full) {
    throw std::invalid_argument("custom variant sizing requires legal full placement");
  }
  if (requested.optimization_pending) {
    throw std::invalid_argument("custom variant sizing runs on the final legal placement (no optimization pending)");
  }
  if (!std::isfinite(sizing.epsilon_ps) || sizing.epsilon_ps < 0) {
    throw std::invalid_argument("custom variant sizing needs a non-negative epsilon");
  }
  auto options         = requested;
  options.multirow_eco = sizing.eco;
  const auto& catalog  = *options.custom_cells;
  const auto  start    = Clock::now();
  const auto  prior    = legal.qor;
  const auto  dir      = options.output_dir + "/domino_sizing";
  fs::create_directories(dir);
  Repair_result result;
  result.placed = std::move(legal);
  auto score    = evaluate(result.placed);
  if (!(score.delay > 0)) {
    throw std::invalid_argument("custom variant sizing needs a measured period");
  }
  const auto first      = score;
  const auto initial    = custom_instances(result.placed, catalog);
  int64_t    widest_row = 0;  // the frame's rows never change in an ECO
  for (const auto& row : result.placed.floorplan.rows) {
    widest_row = std::max(widest_row, row.count * row.step);
  }
  const auto buffer = sizing.buffers ? smallest_buffer(options) : std::string{};

  // Per instance: the moves tried or taken, so no move repeats or undoes another.
  std::set<std::string>           up_rejected, down_rejected, buffered, moved_up, moved_down;
  std::map<std::string, unsigned> tally;
  unsigned                        trials = 0;
  Eco_total                       eco;
  rapidjson::StringBuffer         log;
  Writer                          w(log);
  w.StartArray();
  for (unsigned round = 0; round < sizing.rounds && trials < sizing.max_trials; ++round) {
    // Plan on this state: critical instances (most negative output slack
    // first) step up, else buffer; instances with spare slack (most first)
    // step down.
    auto                                         instances = custom_instances(result.placed, catalog);
    std::vector<std::tuple<double, std::string>> critical, spare;
    std::set<std::string>                        names;  // instance names taken, for new buffers
    const auto                                   index = [&] {
      instances = custom_instances(result.placed, catalog);
      for (auto n : result.placed.graph->body().nodes()) {
        names.emplace(n.get_name());
      }
    };
    index();
    for (const auto& [name, instance] : instances) {
      const auto* pin = score.find_pin(name, instance.variant->output);
      if (!pin || !pin->slack) {
        continue;  // unconstrained output: nothing to gain or check
      }
      const double slack = *pin->slack * score.time_unit_ps;
      (slack < 0 ? critical : spare).emplace_back(slack, name);
    }
    std::sort(critical.begin(), critical.end());
    std::sort(spare.begin(), spare.end(), [](const auto& a, const auto& b) {
      return std::get<0>(a) != std::get<0>(b) ? std::get<0>(a) > std::get<0>(b) : std::get<1>(a) < std::get<1>(b);
    });
    std::vector<Move> moves;
    const auto variant_move = [&](Move_kind kind, const std::string& name, const Custom_variant& from, const Custom_variant& to) {
      Move move{kind, name, from.name, {}};
      move.edit.kind     = Resize_edit::Kind::variant;
      move.edit.instance = name;
      move.edit.master   = to.name;
      moves.push_back(std::move(move));
    };
    for (const auto& [slack, name] : critical) {
      (void)slack;
      const auto& instance = instances.at(name);
      const auto* up       = catalog.step(instance.variant->name, 1);
      if (up && !up_rejected.contains(name) && !moved_down.contains(name)) {
        variant_move(Move_kind::up, name, *instance.variant, *up);
      } else if (!buffer.empty() && !buffered.contains(name)) {
        if (auto edit = output_buffer(result.placed, score, name, instance, buffer, names)) {
          names.insert(edit->instance);
          moves.push_back({Move_kind::buffer, name, instance.variant->name, std::move(*edit)});
        }
      }
    }
    for (const auto& [slack, name] : spare) {
      (void)slack;
      const auto& instance = instances.at(name);
      const auto* down     = catalog.step(instance.variant->name, -1);
      if (down && !down_rejected.contains(name) && !moved_up.contains(name)) {
        variant_move(Move_kind::down, name, *instance.variant, *down);
      }
    }

    bool accepted = false;
    for (size_t m = 0; m < moves.size(); ++m) {
      if (trials >= sizing.max_trials) {
        break;
      }
      const auto move = moves[m];  // a copy: a rejected up move appends its buffer
      ++trials;
      const auto                  t = Clock::now();
      std::optional<Resize_trial> trial;
      std::string                 failure;
      const char*                 failed_as = "rejected_packing";
      // A variant wider than every row can never be packed (placement would
      // refuse it before the two-row ECO): rejected without a trial.
      const auto*                 to        = move.kind == Move_kind::buffer ? nullptr : catalog.find(move.edit.master);
      if (to && to->width_dbu > widest_row) {
        failure = std::format("custom variant {} ({} DBU) is wider than every row ({} DBU); no output committed",
                              to->name,
                              to->width_dbu,
                              widest_row);
      } else {
        try {
          trial = run_resize_trial(result.placed,
                                   options,
                                   {move.edit},
                                   evaluate,
                                   std::format("{}/trial_{}", dir, trials),
                                   dir + "/floorplan.def");
          if (!trial->rejected.empty()) {
            failure   = trial->rejected;
            failed_as = "rejected_growth";
            trial.reset();
          }
        } catch (const Multirow_failure& error) {
          failure = error.what();  // the two-row ECO found no legal pose: rejected
        } catch (const std::runtime_error& error) {
          // Any other failure of one trial (an unmeasured period, say a
          // variant without a characterized register, or a failed legality
          // check) rejects that trial: the clone is dropped, the state stays.
          failure   = error.what();
          failed_as = "rejected_failed";
        }
      }
      // The period (Feedback::delay, compared in ps) is the objective: a
      // strictly shorter one is kept whenever the electrical checks hold,
      // whatever WNS/TNS do (a half-cycle ps and a full-cycle ps cost
      // different period); an equal one needs resize_accepts.
      const bool keep
          = trial && electrical_holds(score, trial->timing)
            && (period_ps(trial->timing) < period_ps(score) - sizing.epsilon_ps
                || (period_ps(trial->timing) <= period_ps(score) + sizing.epsilon_ps && resize_accepts(score, trial->timing)));
      const auto outcome = keep ? "accepted" : trial ? "rejected_timing" : failed_as;
      ++tally[std::string(move_name(move.kind)) + (keep ? "_accepted" : "")];
      ++tally[outcome];
      w.StartObject();
      w.Key("trial");
      w.Uint(trials);
      w.Key("round");
      w.Uint(round);
      w.Key("move");
      w.String(move_name(move.kind));
      w.Key("instance");
      w.String(move.instance.c_str());
      w.Key("from");
      w.String(move.from.c_str());
      w.Key("to");
      w.String(move.edit.master.c_str());
      if (move.kind == Move_kind::buffer) {
        w.Key("buffer");
        w.String(move.edit.instance.c_str());
        w.Key("moved_sinks");
        w.Uint(static_cast<unsigned>(move.edit.sinks.size()));
      }
      w.Key("outcome");
      w.String(outcome);
      if (trial) {
        feedback(w, "timing", trial->timing);
        trial_eco(w, trial->placed.qor, keep ? &eco : nullptr);
      } else {
        w.Key("failure");
        w.String(failure.c_str());
      }
      real(w, "seconds", std::chrono::duration<double>(Clock::now() - t).count());
      w.EndObject();
      if (keep) {
        result.placed = std::move(trial->placed);
        result.libraries.clear();
        result.libraries.push_back(std::move(trial->owner));
        score = trial->timing;
        refresh_repair_weights(result.placed, options, score);
        accepted = true;
        (move.kind == Move_kind::up ? moved_up : move.kind == Move_kind::down ? moved_down : buffered).insert(move.instance);
        index();
        continue;
      }
      (move.kind == Move_kind::up ? up_rejected : move.kind == Move_kind::down ? down_rejected : buffered).insert(move.instance);
      // A rejected up move leaves the output to a buffer, tried now on this state.
      const auto it = instances.find(move.instance);
      if (move.kind == Move_kind::up && !buffer.empty() && !buffered.contains(move.instance) && it != instances.end()) {
        if (auto edit = output_buffer(result.placed, score, move.instance, it->second, buffer, names)) {
          names.insert(edit->instance);
          moves.push_back({Move_kind::buffer, move.instance, it->second.variant->name, std::move(*edit)});
        }
      }
    }
    if (!accepted) {
      break;  // nothing left that helps on this state
    }
  }
  w.EndArray();

  const auto              final_instances = custom_instances(result.placed, catalog);
  const auto              c_before = clock_ff(initial), c_after = clock_ff(final_instances);
  rapidjson::StringBuffer report;
  Writer                  r(report);
  r.StartObject();
  r.Key("engine");
  r.String("custom_sizing");
  r.Key("model");
  r.String("post-legal variant swaps and output buffers, each re-placed through the two-row ECO and fully evaluated");
  feedback(r, "before", first);
  feedback(r, "after", score);
  r.Key("trials");
  r.RawValue(log.GetString(), log.GetSize(), rapidjson::kArrayType);
  r.Key("counts");
  r.StartObject();
  r.Key("trials");
  r.Uint(trials);
  for (const auto* key : {"up_accepted",
                          "down_accepted",
                          "buffer_accepted",
                          "rejected_timing",
                          "rejected_packing",
                          "rejected_growth",
                          "rejected_failed"}) {
    r.Key(key);
    r.Uint(tally[key]);
  }
  r.EndObject();
  // The two-row ECO over the accepted trials (qor.multirow_eco is only the
  // last place() call's).
  r.Key("eco");
  r.StartObject();
  r.Key("accepted_trials");
  r.Uint(eco.accepted_trials);
  r.Key("cells");
  r.Uint64(eco.cells);
  for (const auto outcome : kEcoOutcomes) {
    const auto name = multirow_eco_outcome_name(outcome);
    r.Key(name.data(), static_cast<rapidjson::SizeType>(name.size()));
    r.Uint64(eco.outcomes[std::string(name)]);
  }
  r.Key("released_sites");
  r.Int64(eco.released_sites);
  r.Key("grown_sites");
  r.Int64(eco.grown_sites);
  r.Key("displaced_cells");
  r.Uint64(eco.displaced_cells);
  r.Key("max_displacement_um");
  r.Double(eco.max_displacement_um);
  r.EndObject();
  r.Key("variants");
  r.StartObject();
  r.Key("before");
  counts(r, initial);
  r.Key("after");
  counts(r, final_instances);
  r.EndObject();
  r.Key("clock");
  r.StartObject();
  r.Key("basis");
  r.String("catalog clock_ff (a labeled estimate) summed over the custom instances; no CTS power or skew");
  real(r, "before_ff", c_before);
  real(r, "after_ff", c_after);
  real(r, "delta_ff", c_before && c_after ? std::optional<double>{*c_after - *c_before} : std::nullopt);
  r.EndObject();
  r.Key("buffer");
  r.String(buffer.c_str());
  r.Key("bounds");
  r.StartObject();
  r.Key("rounds");
  r.Uint(sizing.rounds);
  r.Key("max_trials");
  r.Uint(sizing.max_trials);
  r.Key("shift_sites");
  r.Uint(sizing.eco.shift_sites);
  r.Key("max_displaced");
  r.Uint(sizing.eco.max_displaced);
  r.EndObject();
  real(r, "seconds", std::chrono::duration<double>(Clock::now() - start).count());
  r.EndObject();
  result.report = report.GetString();

  if (!result.libraries.empty()) {  // an accepted trial replaced the placement
    physical::write_def(result.placed.floorplan, options.output_dir + "/out.def");
  }
  publish_engine_qor(result.placed, prior, {"multirow"}, {}, "domino_sizing", result.report, options.output_dir);
  std::ofstream(options.output_dir + "/domino_sizing.json") << result.report << '\n';
  result.timing = std::move(score);
  return result;
}
}  // namespace livehd::placement
