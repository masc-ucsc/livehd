// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_schedule.hpp"

#include <cmath>
#include <format>
#include <ranges>

#include "inou/lefdef/mapped_graph.hpp"
#include "pass/place/pcrs/clock_tree.hpp"
#include "pass/timing/physical_rank.hpp"

namespace livehd::placement {
namespace {
struct Clock_path_target {
  std::string                  launch_pin, endpoint_pin;
  std::optional<Schedule_leaf> launch, capture;
  double                       delay_needed_ps = 0, delay_period_ps = 0;
};
struct Measurement {
  std::vector<uint64_t>                            leaf_pins{0};
  std::vector<double>                              leaf_periods{0};
  bool                                             ranked_paths = false, port_source_pair = false;
  std::vector<Leaf_slack_edge>                     slack_edges;
  std::vector<std::string>                         internal_sdc;
  Clock_score                                      score;
  Clock_path_target                                setup_path, hold_path;
  std::vector<std::pair<Clock_path_target, bool>>  alternative_paths;
  std::map<Pin_label, Schedule_leaf>               hold_captures;
  Endpoint_hold                                    hold_pins;
  Endpoint_hold                                    setup_pins;
  Sink_arrivals                                    arrivals;
  std::map<Pin_label, std::pair<unsigned, double>> electrical;
  std::vector<std::string>                         unsupported_checks;
  bool constraints_supported() const { return unsupported_checks.empty(); }
  size_t                                           slew, cap, fanout;
};
// A timing pin is numeric within a bound graph. Only the pins compared across physical
// trial graphs need a stable instance/port identity. Keep this binding through journal edits.
class Clock_pin_binding {
  std::unordered_map<uint32_t, Pin_label> labels;
  std::unordered_map<uint32_t, Name>      ports;

public:
  void bind(const timing::fast::Netlist& netlist) {
    labels.clear();
    ports.clear();
    for (const auto& port : netlist.ports()) {
      ports.emplace(port.cell, port.name);
    }
  }
  Pin_label get(const timing::fast::Bound_graph& graph, uint32_t pin) {
    if (const auto it = labels.find(pin); it != labels.end()) {
      return it->second;
    }
    const auto&     netlist = *graph.netlist;
    const auto      cell    = netlist.cell_of(pin);
    const Pin_label value   = graph.cell_node[cell].is_invalid()
                                  ? Pin_label(ports.at(cell))
                                  : Pin_label(Name(graph.name(cell)), Name(netlist.pin_type(pin).name));
    labels.emplace(pin, value);
    return value;
  }
};
Measurement measure(timing::fast::Physical_ranker& timer, const Clock_trial_layout& layout, Clock_pin_binding& binding,
                    bool electrical_detail, bool schedule_paths, double hold_margin) {
  const auto& engine  = *timer.engine();
  const auto& bound   = *timer.bound();
  const auto& netlist = *bound.netlist;
  Measurement result{};
  result.port_source_pair = layout.port_source_pair;
  const auto  label = [&](uint32_t pin) { return binding.get(bound, pin); };
  uint32_t    worst = timing::fast::no_index, worst_hold = timing::fast::no_index;

  for (const auto pin : engine.endpoint_pins()) {
    const auto   name  = label(pin);
    const double setup = engine.endpoint_setup_slack(pin), hold = engine.hold_slack(pin);
    if (std::isfinite(setup)) {
      result.setup_pins.emplace(name, setup);
      result.score.setup_tns += std::min(0., setup);
      if (setup < result.score.setup || (setup == result.score.setup && name < result.setup_path.endpoint_pin)) {
        result.score.setup             = setup;
        worst                          = pin;
        result.setup_path.endpoint_pin = name;
      }
    }
    if (std::isfinite(hold)) {
      result.hold_pins.emplace(name, hold);
      if (hold < result.score.hold || (hold == result.score.hold && name < result.hold_path.endpoint_pin)) {
        result.score.hold             = hold;
        worst_hold                    = pin;
        result.hold_path.endpoint_pin = name;
      }
      result.score.hold_tns += std::min(0., hold);
    }
  }
  if (electrical_detail) {
    for (const auto& violation : engine.electrical_pins()) {
      result.electrical.emplace(label(violation.pin), std::pair{violation.count, violation.excess});
    }
  }
  for (auto pin : netlist.order()) {
    if (!engine.propagated_clock_sink(pin)) {
      continue;
    }
    const auto& clock    = engine.clock_at(pin);
    auto&       waveform = result.arrivals.emplace(label(pin), Sink_waveform{clock.name, clock.period_ps, {}}).first->second;
    waveform.arrivals.reserve(4 * engine.corners());
    for (size_t corner = 0; corner < engine.corners(); ++corner) {
      for (bool early : {true, false}) {
        for (unsigned transition = 0; transition < 2; ++transition) {
          waveform.arrivals.push_back(engine.clock_edge(pin, corner, transition, early).arrival);
        }
      }
    }
  }
  result.slew                  = engine.report().slew_violations;
  result.cap                   = engine.report().capacitance_violations;
  result.fanout                = engine.report().fanout_violations;
  result.unsupported_checks = engine.constraints().unsupported;
  if (!layout.sdc_supported) {
    result.unsupported_checks.emplace_back("unresolved_trial_sdc");
  }
  if (engine.report().unsupported_clock_phase) {
    result.unsupported_checks.emplace_back("unsupported_clock_phase");
  }
  if (netlist.has_unchecked_clock_gate()) {
    result.unsupported_checks.emplace_back("unchecked_clock_gate");
  }
  result.internal_sdc = layout.internal_sdc;
  if (!result.constraints_supported()) {
    return result;
  }
  const auto leaf = [&](uint32_t pin) -> std::optional<Schedule_leaf> {
    if (pin == timing::fast::no_index) {
      return {};
    }
    const auto it = layout.sinks.find(label(pin));
    if (it == layout.sinks.end() || it->second.fixed) {
      return {};
    }
    auto group      = it->second;
    group.period_ps = engine.clock_at(pin).period_ps;
    return group;
  };
  const auto target = [&](uint32_t pin, bool hold) {
    Clock_path_target t;
    t.endpoint_pin = label(pin);
    if (hold) {
      const auto path = timer.worst_hold_path(pin);
      if (path.launch != timing::fast::no_index) {
        t.launch_pin = label(path.launch);
      }
      t.launch  = leaf(path.launch);
      t.capture = leaf(path.capture);
    } else if (engine.endpoint_path_covers_worst(pin)) {
      const auto path = engine.worst_path(pin);
      if (!path.empty()) {
        t.launch_pin = label(path.front().pin);
        t.launch     = leaf(path.front().pin);
      }
      const auto cell = netlist.cell_of(pin);
      for (const auto& arc : netlist.types().arcs_into(netlist.type_of(cell), netlist.local_of(pin))) {
        if (arc.kind != timing::fast::Arc_kind::setup_rise) {
          continue;
        }
        const auto parent = leaf(netlist.pin(cell, arc.from));
        if (!parent || (t.capture && t.capture->anchor != parent->anchor)) {
          t.capture.reset();
          break;
        }
        t.capture = parent;
      }
    }
    const auto& delayed = hold ? t.launch : t.capture;
    if (delayed) {
      t.delay_period_ps  = delayed->period_ps;
      const double slack = hold ? engine.hold_slack(pin) - hold_margin : engine.endpoint_setup_slack(pin);
      t.delay_needed_ps  = std::max(0., -slack);
    }
    return t;
  };
  if (result.score.hold < 0) {
    result.hold_path = target(worst_hold, true);
  }
  if (result.score.setup < 0) {
    result.setup_path = target(worst, false);
  }
  if (schedule_paths && (result.score.hold < 0 || result.score.setup < 0)) {
    result.ranked_paths = true;
    std::vector<std::tuple<double, std::string, bool, uint32_t>> failing;
    std::map<uint64_t, size_t>                                   leaves;
    std::map<std::tuple<size_t, size_t, bool>, double>           slacks;
    auto                                                         vertex = [&](const std::optional<Schedule_leaf>& group) -> size_t {
      if (!group) {
        return 0;
      }
      auto [it, added] = leaves.try_emplace(group->anchor, result.leaf_pins.size());
      if (added) {
        result.leaf_pins.push_back(group->anchor);
        result.leaf_periods.push_back(group->period_ps);
      }
      return it->second;
    };
    for (auto pin : engine.endpoint_pins()) {
      for (bool hold : {false, true}) {
        const double slack = hold ? engine.hold_slack(pin) : engine.endpoint_setup_slack(pin);
        if (!std::isfinite(slack)) {
          continue;
        }
        auto         path   = target(pin, hold);
        const size_t launch = vertex(path.launch), capture = vertex(path.capture);
        if (hold && path.capture && !path.capture->parent.empty()) {
          result.hold_captures.emplace(label(pin), *path.capture);
        }
        if (launch != capture) {
          auto key         = std::tuple{hold ? capture : launch, hold ? launch : capture, hold};
          auto [it, added] = slacks.try_emplace(key, slack);
          if (!added) {
            it->second = std::min(slack, it->second);
          }
        }
        if (slack < 0 && pin != (hold ? worst_hold : worst)) {
          failing.emplace_back(slack, path.endpoint_pin, hold, pin);
        }
      }
    }
    for (const auto& [key, slack] : slacks) {
      const auto [from, to, hold] = key;
      result.slack_edges.push_back({from, to, slack, hold ? hold_margin : 0.});
    }
    std::ranges::sort(failing);
    std::set<std::tuple<bool, uint64_t, uint64_t>> pairs;
    const auto                                     key = [](bool hold, const Clock_path_target& t) {
      return std::tuple{hold, t.launch ? t.launch->anchor : 0, t.capture ? t.capture->anchor : 0};
    };
    pairs.insert(key(false, result.setup_path));
    pairs.insert(key(true, result.hold_path));
    for (const auto& [slack, name, hold, pin] : failing) {
      auto t = target(pin, hold);
      if (pairs.insert(key(hold, t)).second) {
        result.alternative_paths.emplace_back(std::move(t), hold);
      }
    }
  }
  return result;
}
// Compare all later trials under the pin names of the original design. Restore
// aliases in reverse order so a gate that survives one merge can itself retire.
void restore_gate_checks(Measurement& trial, const std::vector<pcrs::Gate_merge>& active) {
  for (const auto& edit : active | std::views::reverse) {
    const auto alias = [&](auto& pins, const std::string& pin) {
      if (const auto it = pins.find(edit.keep + ":" + pin); it != pins.end()) {
        pins.emplace(edit.drop + ":" + pin, it->second);
      }
    };
    alias(trial.arrivals, edit.clock_pin);
    alias(trial.setup_pins, edit.enable_pin);
    alias(trial.hold_pins, edit.enable_pin);
    alias(trial.hold_captures, edit.enable_pin);
    if (!edit.test_pin.empty()) {
      alias(trial.setup_pins, edit.test_pin);
      alias(trial.hold_pins, edit.test_pin);
      alias(trial.hold_captures, edit.test_pin);
    }
  }
}
std::string gate_edit_decision(const Measurement& before, const Measurement& after, const Sink_arrivals& original_arrivals,
                               const Clock_gate_trial& proposal, double period_fraction, double hold_margin,
                               std::optional<Clock_bound_violation>* violation = nullptr) {
  if (!before.constraints_supported() || !after.constraints_supported()) {
    return "unsupported_constraints";
  }
  if (before.internal_sdc != after.internal_sdc) {
    return "constraint_targets_changed";
  }
  for (const auto& pin : proposal.clock_sinks) {
    if (!original_arrivals.contains(pin) || !after.arrivals.contains(pin)) {
      return "missing_clock_arrivals";
    }
  }
  for (const auto& pin : proposal.controls) {
    if (!before.hold_pins.contains(pin) || !before.setup_pins.contains(pin) || !after.hold_pins.contains(pin)
        || !after.setup_pins.contains(pin)) {
      return "missing_gate_checks";
    }
  }
  const auto& setup = after.setup_pins;
  const auto  bounds
      = check_clock_bounds(before.hold_pins, after.hold_pins, original_arrivals, after.arrivals, period_fraction, hold_margin);
  if (!bounds.same_coverage || before.setup_pins.size() != setup.size()) {
    return "changed_timing_coverage";
  }
  if (bounds.offset) {
    if (violation) {
      *violation = bounds.offset;
    }
    return "clock_offset";
  }
  if (bounds.hold) {
    if (violation) {
      *violation = bounds.hold;
    }
    return "hold_margin";
  }
  std::optional<Clock_bound_violation> worst_setup;
  for (const auto& [pin, slack] : before.setup_pins) {
    const auto it = setup.find(pin);
    if (it == setup.end()) {
      return "changed_timing_coverage";
    }
    const double limit = std::min(slack, 0.f), excess = limit - it->second;
    if (excess > slack_epsilon_ps && (!worst_setup || excess > worst_setup->excess_ps)) {
      worst_setup = Clock_bound_violation{pin, slack, it->second, limit, excess};
    }
  }
  if (worst_setup) {
    if (violation) {
      *violation = std::move(worst_setup);
    }
    return "setup_regression";
  }
  if (after.slew > before.slew || after.cap > before.cap || after.fanout > before.fanout) {
    return "electrical_regression";
  }
  return "lower_ranked";
}
struct Proposal {
  Clock_schedule           schedule;
  const Clock_path_target* path;
  bool                     hold;
  bool                     compensating_setup = false;
  bool                     joint              = false;
};
void propose(std::vector<Proposal>& proposals, const Clock_schedule& initial, const Clock_path_target& path, bool hold,
             double period_fraction) {
  // Setup wants earlier launch and later capture; hold wants later launch and earlier capture.
  if (path.capture && path.launch && path.capture->anchor == path.launch->anchor) {
    const auto& delayed = hold ? *path.launch : *path.capture;
    if (path.capture->member != path.launch->member && !initial.separate.contains(delayed.member)) {
      proposals.push_back({initial, &path, hold});
      proposals.back().schedule.separate.insert(delayed.member);
      const double delay = std::min(path.delay_needed_ps, period_fraction * path.delay_period_ps);
      if (delay > slack_epsilon_ps) {
        auto split_and_delay                               = proposals.back();
        split_and_delay.schedule.delay_ps[delayed.member] += delay;
        proposals.push_back(std::move(split_and_delay));
      }
    }
    return;
  }
  auto       both  = initial;
  const auto start = proposals.size();
  const auto step  = [&](const std::optional<Schedule_leaf>& leaf, bool faster) {
    if (!leaf || (faster ? leaf->steps == std::numeric_limits<unsigned>::max() : leaf->steps == 0)) {
      return;
    }
    const auto value = faster ? leaf->steps + 1 : leaf->steps - 1;
    proposals.push_back({initial, &path, hold});
    proposals.back().schedule.drive_steps[leaf->anchor] = value;
    both.drive_steps[leaf->anchor]                      = value;
  };
  step(path.launch, !hold);
  step(path.capture, hold);
  if (proposals.size() == start + 2) {
    proposals.push_back({std::move(both), &path, hold});
  }
}
// A faster leaf can slow a sibling through its higher input load. If a hold
// repair failed only setup, retime its combination with either adjacent drive
// size on the limiting setup path before giving up.
void compensate_setup(std::vector<Proposal>& candidates, const Proposal& hold_proposal, const Clock_schedule& held_edits,
                      const Clock_path_target& setup_path) {
  const auto hold = hold_proposal;
  for (const auto& leaf : {setup_path.launch, setup_path.capture}) {
    if (!leaf) {
      continue;
    }
    if (held_edits.drive_steps.contains(leaf->anchor)) {
      continue;
    }
    for (bool faster : {true, false}) {
      if (faster ? leaf->steps == std::numeric_limits<unsigned>::max() : leaf->steps == 0) {
        continue;
      }
      auto schedule                      = hold.schedule;
      schedule.drive_steps[leaf->anchor] = faster ? leaf->steps + 1 : leaf->steps - 1;
      if (std::ranges::none_of(candidates, [&](const auto& other) { return other.schedule == schedule; })) {
        candidates.push_back({std::move(schedule), hold.path, true, true});
      }
    }
  }
}
}  // namespace

Clock_schedule_result choose_clock_schedule(const Clock_schedule& initial, unsigned rounds, double period_fraction,
                                            double hold_margin_ps, const Hold_repair_options& hold_repair,
                                            const physical::Technology& tech, const Net_wire_rc& wires,
                                            std::unique_ptr<timing::fast::Physical_ranker>                  ranker,
                                            const std::function<Clock_trial_layout(const Clock_schedule&)>& build,
                                            const std::function<void(Clock_trial_layout&)>& resolve_sdc, Clock_gate_search* gates,
                                            bool incremental) {
  // Keep the previous graph alive through the next bind. The ranker caches the
  // graph address and epoch, and a fresh library can reuse both.
  std::unique_ptr<hhds::GraphLibrary> library;
  std::shared_ptr<hhds::Graph>        graph;
  auto                                timer = std::move(ranker);
  timer->propagate_clocks();
  Clock_schedule_result              out{initial};
  std::optional<physical::Floorplan> bound_floorplan;
  std::map<std::string, Wire_rc>     bound_branch_rc;
  Clock_pin_binding                  pin_binding;
  const auto                         read_measurement = [&](const Clock_trial_layout& layout, bool rank_paths) {
    auto result = measure(*timer,
                          layout,
                          pin_binding,
                          hold_repair.area_um2 > 0 || (gates && gates->clone_candidates),
                          rank_paths,
                          hold_margin_ps);
    if (gates) {
      restore_gate_checks(result, gates->active);
    }
    return result;
  };
  const auto time_layout = [&](Clock_trial_layout& layout, bool rank_paths = false) {
    resolve_sdc(layout);
    const auto& fp = layout.floorplan;
    // Same physical instances and connectivity permit exact journal edits.
    // Branch or gate insertion and deletion bind a fresh graph and establish a new baseline.
    if (incremental && bound_floorplan && fp.components.size() == bound_floorplan->components.size()
        && fp.terminals == bound_floorplan->terminals && fp.nets == bound_floorplan->nets && layout.branch_rc == bound_branch_rc) {
      std::set<Name>      changed;
      std::vector<size_t> resize;
      bool                same = true;
      for (size_t i = 0; i < fp.components.size(); ++i) {
        const auto& a = bound_floorplan->components[i];
        const auto& b = fp.components[i];
        if (a.name != b.name) {
          same = false;
          break;
        }
        if (a.macro != b.macro) {
          resize.push_back(i);
        }
        if (a.place != b.place || a.macro != b.macro) {
          changed.insert(a.name);
        }
      }
      if (same) {
        auto&      edits = timer->whatif();
        const auto mark  = edits.mark();
        try {
          for (auto i : resize) {
            edits.resize(fp.components[i].name.str(), fp.components[i].macro.str());
          }
          std::vector<size_t> touched_nets;
          for (size_t i = 0; i < fp.nets.size(); ++i) {
            if (std::ranges::any_of(fp.nets[i].connections, [&](const auto& pin) { return changed.contains(pin.instance); })) {
              touched_nets.push_back(i);
            }
          }
          const auto rc = estimate_floorplan_rc(fp, tech, wires, layout.branch_rc, std::span<const size_t>(touched_nets));
          for (const auto& net : rc.nets) {
            edits.set_net(net);
          }
          timer->evaluate_edits();
          auto result = read_measurement(layout, rank_paths);
          edits.rollback(mark);
          ++out.journal_trials;
          return result;
        } catch (const std::domain_error&) {
          edits.rollback(mark);
        } catch (const std::invalid_argument&) {
          edits.rollback(mark);
        } catch (...) {
          edits.rollback(mark);
          throw;
        }
      }
    }
    auto lib = std::make_unique<hhds::GraphLibrary>();
    auto g   = physical::mapped_graph(*lib, fp, tech, nullptr, true);
    auto rc  = std::make_shared<physical::Parasitics>(estimate_floorplan_rc(fp, tech, wires, layout.branch_rc));
    (void)timer->evaluate(g, rc, {});
    pin_binding.bind(*timer->bound()->netlist);
    graph           = std::move(g);
    library         = std::move(lib);
    bound_floorplan = fp;
    bound_branch_rc = layout.branch_rc;
    ++out.full_evaluations;
    return read_measurement(layout, rank_paths);
  };
  const auto evaluate = [&](const Clock_schedule& schedule, bool rank_paths = false) {
    auto layout = build(schedule);
    ++out.timed;
    return time_layout(layout, rank_paths);
  };
  auto nominal           = initial;
  nominal.balance        = false;
  auto current           = evaluate(nominal, rounds > 0);
  out.schedule           = nominal;
  out.unsupported_checks = current.unsupported_checks;
  auto original_arrivals = std::move(current.arrivals);
  if (initial.balance) {
    out.unbalanced_score = current.score;
    if (!current.constraints_supported()) {
      out.balance_decision = "unsupported_constraints";
    } else {
      const auto try_balance = [&](const Clock_schedule&                 schedule,
                                   std::optional<Clock_score>&           score,
                                   std::string&                          decision,
                                   std::optional<Clock_bound_violation>& bound) {
        try {
          auto trial             = evaluate(schedule, rounds > 0);
          out.unsupported_checks = trial.unsupported_checks;
          score                  = trial.score;
          decision = gate_edit_decision(current, trial, original_arrivals, {}, period_fraction, hold_margin_ps, &bound);
          if (decision != "lower_ranked") {
            return;
          }
          const auto& before = current.score;
          const auto& after  = trial.score;
          const auto  gain   = std::max({std::min(0., after.setup) - std::min(0., before.setup),
                                         std::min(0., after.hold) - std::min(0., before.hold),
                                         after.setup_tns - before.setup_tns,
                                         after.hold_tns - before.hold_tns});
          if (!std::isfinite(before.setup) || !std::isfinite(before.hold)) {
            decision = "missing_setup_or_hold_checks";
          } else if (after.setup + slack_epsilon_ps < std::min(0., before.setup)
                     || after.setup_tns + slack_epsilon_ps < before.setup_tns
                     || after.hold_tns + slack_epsilon_ps < before.hold_tns) {
            decision = "timing_regression";
          } else if (schedule.port_source_stages && gain <= slack_epsilon_ps) {
            decision = "no_timing_gain";
          } else {
            decision                  = "accepted";
            out.selected_max_shift_ps = check_clock_bounds(current.hold_pins,
                                                           trial.hold_pins,
                                                           original_arrivals,
                                                           trial.arrivals,
                                                           period_fraction,
                                                           hold_margin_ps)
                                            .max_shift_ps;
            current                   = std::move(trial);
            out.schedule              = schedule;
          }
        } catch (const pcrs::Clock_infeasible&) {
          decision = "infeasible_clock_tree";
        }
      };
      try_balance(initial, out.balanced_score, out.balance_decision, out.balance_bound);
      if (!out.schedule.balance && current.port_source_pair && (current.score.setup < 0 || current.score.hold < 0)) {
        for (unsigned stages : {2u, 1u}) {
          auto source_only               = nominal;
          source_only.port_source_stages = stages;
          auto& trial                    = out.source_drive_trials.emplace_back(Clock_source_drive_trial{stages, {}, {}, {}});
          try_balance(source_only, trial.score, trial.decision, trial.bound);
          if (out.schedule.port_source_stages) {
            break;
          }
        }
      }
      if (!out.schedule.balance && !out.schedule.port_source_stages) {
        (void)build(out.schedule);
      }
    }
  }
  double area = gates && (!gates->candidates.empty() || !gates->leaves.empty() || gates->leaf_candidates) ? gates->clock_area() : 0;
  double switched      = area > 0 ? gates->switched_cap() : 0;
  const auto gate_cost = [&](Clock_gate_trial& event, const Measurement& trial) {
    event.saved_area_um2    = area - gates->clock_area();
    event.saved_switched_ff = switched - gates->switched_cap();
    auto control_slack      = [&](const Measurement& m) {
      double worst = std::numeric_limits<double>::infinity();
      for (const auto& pin : event.controls) {
        for (const auto* pins : {&m.setup_pins, &m.hold_pins}) {
          if (const auto it = pins->find(pin); it != pins->end()) {
            worst = std::min<double>(worst, it->second);
          }
        }
      }
      return std::isfinite(worst) ? worst : 0.;
    };
    event.gating_slack_ps = control_slack(trial);
    double period         = 0;
    for (const auto& pin : event.clock_sinks) {
      if (auto it = original_arrivals.find(pin); it != original_arrivals.end()) {
        period = it->second.period_ps;
        break;
      }
    }
    event.cost_gain = (area > 0 ? event.saved_area_um2 / area : 0) + (switched > 0 ? event.saved_switched_ff / switched : 0)
                      + (period > 0 ? (std::min(0., event.gating_slack_ps) - std::min(0., control_slack(current))) / period : 0);
  };
  if (gates && !gates->candidates.empty()) {
    gates->baseline = current.score;
    // Compare both survivor choices for the closest pair against the same
    // accepted tree, then carry its selected topology into the next pair.
    for (size_t pair = 0; pair < gates->candidates.size() && gates->trials.size() < gates->budget; pair += 2) {
      std::optional<size_t>      best;
      std::optional<Measurement> selected;
      for (size_t i = pair; i < pair + 2 && gates->trials.size() < gates->budget; ++i) {
        Clock_merge_trial candidate;
        candidate.gate = gates->candidates.at(i);
        if (!gates->prepare(candidate)) {
          continue;
        }
        auto& event = gates->trials.emplace_back(std::move(candidate));
        gates->apply(event.gate);
        try {
          auto trial = evaluate(out.schedule);
          gate_cost(event, trial);
          event.score    = trial.score;
          event.decision = event.cost_gain > 1e-9
                               ? gate_edit_decision(current, trial, original_arrivals, event, period_fraction, hold_margin_ps)
                               : "no_gate_cost_gain";
          if (event.decision == "lower_ranked") {
            // Without observed activity, use the estimate for admission and keep the
            // characterized timing and area ordering for equivalent legal directions.
            const auto merit = [&](const Clock_merge_trial& e) {
              return std::tuple{gates->measured_activity ? e.cost_gain : e.saved_area_um2,
                                e.score->setup,
                                e.score->hold,
                                e.gating_slack_ps};
            };
            if (!best || merit(event) > merit(gates->trials[*best])) {
              best     = gates->trials.size() - 1;
              selected = std::move(trial);
            }
          }
        } catch (const pcrs::Clock_infeasible&) {
          event.decision = "infeasible_clock_tree";
        }
        gates->undo();
      }
      if (best) {
        auto& event = gates->trials[*best];
        gates->apply(event.gate);
        event.decision  = "accepted";
        area           -= event.saved_area_um2;
        switched       -= event.saved_switched_ff;
        current         = std::move(*selected);
      }
    }
  }
  if (gates && gates->leaf_candidates) {
    gates->leaves = gates->leaf_candidates();
  }
  if (gates && !gates->leaves.empty()) {
    gates->before_leaves = current.score;
    for (auto& leaf : gates->leaves) {
      gates->direct_leaf(leaf.distribution, true);
      try {
        auto trial = evaluate(out.schedule);
        gate_cost(leaf, trial);
        leaf.score    = trial.score;
        leaf.decision = leaf.cost_gain > 1e-9
                            ? gate_edit_decision(current, trial, original_arrivals, leaf, period_fraction, hold_margin_ps)
                            : "no_gate_cost_gain";
        if (leaf.decision == "lower_ranked") {
          leaf.decision  = "accepted";
          current        = std::move(trial);
          area          -= leaf.saved_area_um2;
          switched      -= leaf.saved_switched_ff;
          continue;
        }
      } catch (const pcrs::Clock_infeasible&) {
        leaf.decision = "infeasible_clock_tree";
      }
      gates->direct_leaf(leaf.distribution, false);
    }
  }
  if (!current.constraints_supported()) {
    out.skew_stop = "unsupported_constraints";
    if (hold_repair.area_um2 > 0) {
      out.delay_stop = "unsupported_constraints";
    }
    return out;
  }
  if (current.setup_pins.empty() || current.hold_pins.empty()) {
    out.skew_stop = "missing_setup_or_hold_checks";
    if (hold_repair.area_um2 > 0) {
      out.delay_stop = "missing_setup_or_hold_checks";
    }
    return out;
  }
  out.baseline = out.selected = current.score;
  out.skew_stop               = !rounds ? "no_rounds_requested" : "slack_met";
  constexpr double eps        = slack_epsilon_ps;
  for (unsigned round = 0; round < rounds && (current.score.setup < 0 || current.score.hold < 0); ++round) {
    // Only a selected layout supplies the path graph for the next search round.
    // Rejected trials need slacks and bounds, never every launch/capture trace.
    if (!current.ranked_paths) {
      auto selected = build(out.schedule);
      current       = time_layout(selected, true);
    }
    std::vector<Proposal> candidates;
    if (current.score.hold < 0) {
      propose(candidates, out.schedule, current.hold_path, true, period_fraction);
    }
    if (current.score.setup < 0) {
      propose(candidates, out.schedule, current.setup_path, false, period_fraction);
    }
    std::vector<double> budgets = current.leaf_periods;
    for (size_t i = 1; i < budgets.size(); ++i) {
      const auto prior = out.schedule.delay_ps.find(current.leaf_pins[i]);
      budgets[i]       = std::max(0., budgets[i] * period_fraction - (prior == out.schedule.delay_ps.end() ? 0 : prior->second));
    }
    const auto wanted = solve_leaf_arrivals(budgets, current.slack_edges);
    auto       joint  = out.schedule;
    for (size_t i = 1; i < wanted.size(); ++i) {
      if (wanted[i] > slack_epsilon_ps) {
        joint.delay_ps[current.leaf_pins[i]] += wanted[i];
      }
    }
    if (joint != out.schedule) {
      const bool hold = current.score.hold < 0;
      candidates.push_back({std::move(joint), hold ? &current.hold_path : &current.setup_path, hold, false, true});
    }
    std::optional<Measurement> best;
    size_t                     best_index = 0, best_event = 0;
    const auto                 first_trial = out.trials.size();
    std::optional<size_t>      blocked_hold;
    Clock_path_target          blocked_setup;
    const auto                 direct_trials = candidates.size();
    size_t                     alternative   = 0;
    std::set<Clock_schedule>   attempted;
    for (size_t index = 0;; ++index) {
      if (index == direct_trials && !best && blocked_hold) {
        compensate_setup(candidates, candidates[*blocked_hold], out.trials[first_trial + *blocked_hold].changes, blocked_setup);
      }
      // When both worst endpoints are fixed or their edits are rejected, try
      // the next distinct failing leaf pair. Stop after the first pair with a gain.
      while (index == candidates.size() && !best && alternative < current.alternative_paths.size()) {
        const auto& [path, hold] = current.alternative_paths[alternative++];
        propose(candidates, out.schedule, path, hold, period_fraction);
      }
      if (index >= candidates.size()) {
        break;
      }
      const auto& proposal  = candidates[index];
      const auto& candidate = proposal.schedule;
      // Keep direct-trial indices aligned with their events for compensating edits.
      const bool  fresh     = attempted.insert(candidate).second;
      if (!fresh && index >= direct_trials) {
        continue;
      }
      auto& event = out.trials.emplace_back(
          Clock_schedule_trial{round + 1, proposal.path->launch_pin, proposal.path->endpoint_pin, proposal.hold});
      if (proposal.compensating_setup) {
        event.compensated_endpoint = blocked_setup.endpoint_pin;
      }
      for (const auto& [pin, steps] : candidate.drive_steps) {
        const auto it = out.schedule.drive_steps.find(pin);
        if (it == out.schedule.drive_steps.end() || it->second != steps) {
          event.changes.drive_steps.emplace(pin, steps);
        }
      }
      event.joint = proposal.joint;
      for (const auto& [pin, delay] : candidate.delay_ps) {
        const auto it = out.schedule.delay_ps.find(pin);
        if (it == out.schedule.delay_ps.end() || it->second != delay) {
          event.changes.delay_ps.emplace(pin, delay);
        }
      }
      for (const auto& pin : candidate.separate) {
        if (!out.schedule.separate.contains(pin)) {
          event.changes.separate.insert(pin);
        }
      }
      Measurement trial;
      try {
        trial = evaluate(candidate);
      } catch (const pcrs::Clock_drive_unavailable&) {
        event.decision = "unavailable_leaf_size";
        ++out.infeasible;
        continue;
      } catch (const pcrs::Clock_infeasible&) {
        event.decision = "infeasible_clock_tree";
        ++out.infeasible;
        continue;
      }
      const auto bounds = check_clock_bounds(current.hold_pins,
                                             trial.hold_pins,
                                             original_arrivals,
                                             trial.arrivals,
                                             period_fraction,
                                             hold_margin_ps);
      const auto count  = [](const auto& violation, unsigned& rejected, auto& worst) {
        if (violation) {
          ++rejected;
          if (!worst || violation->excess_ps > worst->excess_ps) {
            worst = violation;
          }
        }
      };
      if (bounds.same_coverage) {
        count(bounds.hold, out.hold_rejections, out.worst_hold);
        count(bounds.offset, out.offset_rejections, out.worst_offset);
      }
      const bool same_setup_pins = std::ranges::equal(current.setup_pins | std::views::keys, trial.setup_pins | std::views::keys);
      if (!trial.constraints_supported() || trial.internal_sdc != current.internal_sdc || !bounds.same_coverage || !same_setup_pins) {
        event.decision = "coverage_changed";
        continue;
      }
      event.score = trial.score;
      if (bounds.hold) {
        event.decision = "endpoint_hold_margin";
        event.bound    = bounds.hold;
      } else if (bounds.offset) {
        event.decision = "arrival_shift";
        event.bound    = bounds.offset;
      } else if (trial.slew > current.slew || trial.cap > current.cap || trial.fanout > current.fanout) {
        event.decision = "electrical_violations";
      } else if (trial.score.hold < std::min(current.score.hold, hold_margin_ps) - eps
                 || trial.score.hold_tns < current.score.hold_tns - eps) {
        event.decision = "hold_regression";
      } else if (trial.score.setup_tns < current.score.setup_tns - eps) {
        event.decision = "setup_tns_regression";
      } else if (std::ranges::any_of(current.setup_pins, [&](const auto& endpoint) {
                   return trial.setup_pins.at(endpoint.first) < std::min(endpoint.second, 0.f) - eps;
                 })) {
        event.decision = "endpoint_setup_regression";
      } else if (proposal.hold
                 && (event.changes.delay_ps.empty() || !proposal.joint
                     || (trial.score.setup <= current.score.setup + eps && trial.score.setup_tns <= current.score.setup_tns + eps))
                 && trial.score.hold <= current.score.hold + eps && trial.score.hold_tns <= current.score.hold_tns + eps) {
        event.decision = "no_hold_gain";
      } else if (!proposal.hold && trial.score.setup <= current.score.setup + eps
                 && trial.score.setup_tns <= current.score.setup_tns + eps) {
        event.decision = "no_setup_gain";
      } else {
        event.decision = "lower_ranked";
      }
      // A stronger capture leaf can slow its siblings by loading their common driver.
      // Retry the affected capture leaves together, using the same bounds as a single edit.
      if (index < direct_trials && proposal.hold && bounds.hold && proposal.path->capture
          && event.changes.drive_steps.size() == 1 && event.changes.drive_steps.contains(proposal.path->capture->anchor)
          && (trial.score.hold > current.score.hold + eps || trial.score.hold_tns > current.score.hold_tns + eps)) {
        const auto extra = sibling_hold_drives(*proposal.path->capture, current.hold_captures, current.hold_pins,
                                               trial.hold_pins, hold_margin_ps);
        if (!extra.empty()) {
          auto retry = candidate;
          for (const auto& [anchor, steps] : extra) {
            retry.drive_steps[anchor] = steps;
          }
          candidates.push_back({std::move(retry), proposal.path, true, false, true});
          continue;
        }
      }
      if (index < direct_trials && proposal.hold
          && (event.decision == "setup_tns_regression" || event.decision == "endpoint_setup_regression")
          && (trial.score.hold > current.score.hold + eps || trial.score.hold_tns > current.score.hold_tns + eps)) {
        const auto& previous = blocked_hold ? *out.trials[first_trial + *blocked_hold].score : current.score;
        if (!blocked_hold || std::tie(trial.score.hold, trial.score.hold_tns) > std::tie(previous.hold, previous.hold_tns)) {
          blocked_hold  = index;
          blocked_setup = trial.setup_path;
        }
      }
      if (event.decision != "lower_ranked") {
        // A continuous delay can round up to a whole buffer stage. Back off
        // the proposed increments together and recheck the physical tree.
        if (!best && proposal.joint && event.decision != "no_hold_gain" && event.decision != "no_setup_gain") {
          auto reduced = out.schedule;
          for (const auto& [pin, delay] : event.changes.delay_ps) {
            const auto prior = out.schedule.delay_ps.find(pin);
            const auto base  = prior == out.schedule.delay_ps.end() ? 0. : prior->second;
            const auto step  = (delay - base) / 2;
            if (step > eps) {
              reduced.delay_ps[pin] = base + step;
            }
          }
          if (reduced != out.schedule) {
            candidates.push_back({std::move(reduced), proposal.path, proposal.hold, false, true});
          }
        }
        continue;
      }
      if (best) {
        const auto merit = [&](const Clock_score& score) {
          return std::tuple{std::min(0., score.hold), score.hold_tns, score.setup, score.setup_tns};
        };
        if (merit(trial.score) <= merit(best->score)) {
          continue;
        }
      }
      out.selected_max_shift_ps = bounds.max_shift_ps;
      best                      = std::move(trial);
      best_index                = index;
      best_event                = out.trials.size() - 1;
    }
    if (!best) {
      out.skew_stop = candidates.empty() ? "no_movable_leaf_pair" : "no_legal_improving_edit";
      break;
    }
    out.skew_stop                   = best->score.setup < 0 || best->score.hold < 0 ? "round_limit" : "slack_met";
    out.schedule                    = std::move(candidates[best_index].schedule);
    out.trials[best_event].decision = "accepted";
    current                         = std::move(*best);
    out.selected                    = current.score;
    ++out.accepted;
  }
  if (gates && gates->clone_candidates && current.constraints_supported()) {
    // Restore the selected topology after skew trials, which may have ended
    // with a rejected alternative. Each copy edits just one buffered leaf.
    auto       layout = build(out.schedule);
    const auto leaves = gates->clone_candidates(current.setup_pins, current.hold_pins);
    if (!leaves.empty()) {
      gates->before_clones = current.score;
    }
    struct Clone_choice {
      size_t                    leaf;
      bool                      upstream;
      const pcrs::Clock_driver* drive;
    };
    std::vector<Clone_choice> choices;
    for (size_t i = 0; i < leaves.size(); ++i) {
      choices.push_back({i, false, nullptr});
    }
    std::vector<bool> accepted(leaves.size());
    unsigned          timed_copies = 0;
    for (size_t i = 0; i < choices.size(); ++i) {
      if (timed_copies == gates->clone_budget) {
        break;
      }
      const auto [index, upstream, drive] = choices[i];
      if (accepted[index]) {
        continue;
      }
      const auto& leaf  = leaves[index];
      auto&       event = gates->clone_trials.emplace_back();
      event.gate        = leaf.gate;
      event.leaf        = leaf.path.front().instance;
      event.loads       = leaf.sinks.size();
      if (upstream) {
        event.upstream_tap = leaf.upstream_tap;
      }
      auto edit = plan_clock_clone(layout.floorplan,
                                   tech,
                                   leaf,
                                   gates->clone_limits,
                                   gates->input_limits,
                                   event.decision,
                                   upstream,
                                   drive);
      if (!edit) {
        continue;
      }
      event.area_delta_um2 = edit->area_delta_um2;
      event.cell           = edit->cell;
      if (edit->clock_driver.instance == "PIN" && gates->external_clock_legal
          && !gates->external_clock_legal(layout.floorplan, *edit)) {
        event.decision = "clone_source_drive_limit";
        if (!upstream && !drive && leaf.upstream_tap) {
          choices.push_back({index, true, nullptr});
        }
        continue;
      }
      const auto clock = original_arrivals.find(leaf.gate + ":" + leaf.clk);
      if (clock == original_arrivals.end()) {
        event.decision = "missing_gate_clock";
        continue;
      }
      Clock_gate_trial         checks;
      std::vector<std::string> added_controls;
      for (const auto& control : {leaf.enable, leaf.test}) {
        if (control.empty()) {
          continue;
        }
        const auto old = leaf.gate + ":" + control;
        if (!current.setup_pins.contains(old) || !current.hold_pins.contains(old)) {
          event.decision = "missing_gate_checks";
          break;
        }
        const auto added = edit->cell.name.str() + ":" + control;
        added_controls.push_back(added);
        checks.controls.push_back(old);
        checks.controls.push_back(added);
      }
      if (!event.decision.empty()) {
        continue;
      }
      const auto added_clk = edit->cell.name.str() + ":" + leaf.clk;
      checks.clock_sinks.push_back(added_clk);
      for (const auto& sink : leaf.sinks) {
        checks.clock_sinks.push_back(sink.name);
      }
      original_arrivals.emplace(added_clk, clock->second);
      for (const auto& pin : added_controls) {
        current.setup_pins.emplace(pin, 0);
        current.hold_pins.emplace(pin, hold_margin_ps);
      }
      gates->copies.push_back(std::move(*edit));
      auto candidate = build(out.schedule);
      auto trial     = time_layout(candidate);
      ++out.timed;
      event.score = trial.score;
      ++timed_copies;
      event.decision = gate_edit_decision(current, trial, original_arrivals, checks, period_fraction, hold_margin_ps, &event.bound);
      if (event.decision == "lower_ranked") {
        for (const auto& [pin, e] : trial.electrical) {
          auto before = current.electrical.find(pin);
          if (before == current.electrical.end() || e.first > before->second.first || e.second > before->second.second + 1e-5) {
            event.decision = "pin_electrical_regression";
            break;
          }
        }
      }
      if (event.decision == "lower_ranked") {
        const auto& a = current.score;
        const auto& b = trial.score;
        const bool  gain
            = b.setup > a.setup + eps || b.hold > a.hold + eps || b.setup_tns > a.setup_tns + eps || b.hold_tns > a.hold_tns + eps;
        if (event.area_delta_um2 < 0 || (gain && b.setup + eps >= a.setup && b.hold + eps >= a.hold)) {
          event.decision  = "accepted";
          out.selected    = trial.score;
          current         = std::move(trial);
          layout          = std::move(candidate);
          accepted[index] = true;
          continue;
        }
        event.decision = "no_area_or_timing_gain";
      }
      if (!drive) {
        if (!upstream && leaf.upstream_tap
            && (event.decision == "hold_margin" || event.decision == "clock_offset" || event.decision == "pin_electrical_regression"
                || event.decision == "electrical_regression")) {
          choices.push_back({index, true, nullptr});
        }
        // A gate can meet its leaf load yet miss propagated endpoint timing.
        // Try the remaining pin- and function-equivalent sizes on the same tap.
        for (const auto& size : leaf.alternative_drives) {
          if (size.master != event.cell->macro) {
            choices.push_back({index, upstream, &size});
          }
        }
      }
      gates->copies.pop_back();
      original_arrivals.erase(added_clk);
      for (const auto& pin : added_controls) {
        current.setup_pins.erase(pin);
        current.hold_pins.erase(pin);
      }
    }
  }
  if (hold_repair.area_um2 <= 0) {
    return out;
  }
  out.after_delays = current.score;
  if (hold_repair.drivers.empty() || !current.constraints_supported()) {
    out.delay_stop = hold_repair.drivers.empty() ? "no_eligible_buffer" : "unsupported_constraints";
    return out;
  }
  if (current.score.hold >= 0) {
    out.delay_stop = "hold_met";
    return out;
  }
  auto layout = build(out.schedule);
  auto free_rows
      = unoccupied_rows(tech, layout.floorplan, hold_repair.padding_sites, hold_repair.halo_x_dbu, hold_repair.halo_y_dbu);
  if (hold_repair.pin_access) {
    reserve_hold_pin_access(free_rows, *hold_repair.pin_access);
  }
  const auto try_delays = [&](std::span<const Hold_candidate> choices, bool batch) {
    auto                    candidate      = layout;
    auto                    candidate_rows = free_rows;
    std::vector<Hold_delay> edits;
    std::set<size_t>        nets;
    double                  added_area = 0;
    for (const auto& choice : choices) {
      if (nets.contains(choice.net) || out.added_area_um2 + added_area + choice.area > hold_repair.area_um2 + 1e-12) {
        continue;
      }
      auto edit = plan_hold_delay(candidate.floorplan,
                                  tech,
                                  *choice.driver,
                                  current.hold_pins,
                                  choice.net,
                                  &candidate_rows,
                                  hold_repair.padding_sites);
      if (!edit) {
        continue;
      }
      insert_hold_delay(candidate.floorplan, *edit);
      reserve_gaps(candidate_rows, padded_rect(tech, edit->cell, hold_repair.padding_sites));
      added_area += edit->area_um2;
      nets.insert(choice.net);
      edits.push_back(std::move(*edit));
      if (edits.size() == 4) {
        break;
      }
    }
    if (edits.empty() || (batch && edits.size() == 1)) {
      out.delay_stop = "no_eligible_net_or_site";
      return false;
    }
    auto trial = time_layout(candidate);
    ++out.delay_trials;
    if (batch) {
      ++out.delay_batch_trials;
    }
    const auto bounds = check_clock_bounds(current.hold_pins,
                                           trial.hold_pins,
                                           original_arrivals,
                                           trial.arrivals,
                                           period_fraction,
                                           hold_margin_ps);
    if (!trial.constraints_supported() || trial.internal_sdc != current.internal_sdc || !bounds.allowed()
        || !std::ranges::equal(current.setup_pins | std::views::keys, trial.setup_pins | std::views::keys)) {
      out.delay_stop = "timing_bounds";
      return false;
    }
    if (trial.slew > current.slew || trial.cap > current.cap || trial.fanout > current.fanout
        || std::ranges::any_of(trial.electrical, [&](const auto& pin) {
             const auto it = current.electrical.find(pin.first);
             return it == current.electrical.end() || pin.second.first > it->second.first
                    || pin.second.second > it->second.second + 1e-5;
           })) {
      out.delay_stop = "electrical_violations";
      return false;
    }
    if (std::ranges::any_of(current.setup_pins, [&](const auto& endpoint) {
          return trial.setup_pins.at(endpoint.first) < std::min(endpoint.second, 0.f) - eps;
        })) {
      out.delay_stop = "setup_regression";
      return false;
    }
    if (trial.score.hold <= current.score.hold + eps && trial.score.hold_tns <= current.score.hold_tns + eps) {
      out.delay_stop = "no_hold_gain";
      return false;
    }
    for (const auto& edit : edits) {
      double recovered = 0;
      for (const auto& sink : edit.sinks) {
        const auto pin  = physical::connection_label(sink);
        recovered      += std::min(0.f, trial.hold_pins.at(pin)) - std::min(0.f, current.hold_pins.at(pin));
      }
      if (recovered <= eps) {
        out.delay_stop = "no_endpoint_hold_gain";
        return false;
      }
    }
    free_rows           = std::move(candidate_rows);
    out.added_area_um2 += added_area;
    if (batch) {
      ++out.delay_batches;
    }
    for (auto& edit : edits) {
      out.delays.push_back(std::move(edit));
    }
    current          = std::move(trial);
    layout           = std::move(candidate);
    out.after_delays = current.score;
    return true;
  };
  std::set<std::pair<size_t, std::string>> rejected_delays;
  unsigned                                 batch_backoff = 1, single_steps_left = 0;
  while (current.score.hold < 0) {
    const double remaining = hold_repair.area_um2 - out.added_area_um2;
    const auto   choices
        = rank_hold_delays(layout.floorplan, tech, hold_repair, current.hold_pins, remaining, &current.setup_pins, rejected_delays);
    if (choices.empty()) {
      const bool affordable = std::ranges::any_of(hold_repair.drivers, [&](const auto* driver) {
        const auto& m = tech.macros.at(driver->master);
        return m.width * m.height <= remaining + 1e-12;
      });
      if (!affordable || rejected_delays.empty()) {
        out.delay_stop = affordable ? "no_eligible_net_or_cell" : "area_budget";
      }
      break;
    }
    // Edits on distinct nets still interact through timing and placement.
    // Check them together first, then retry individual candidates on rejection.
    if (single_steps_left) {
      --single_steps_left;
    } else {
      const auto attempted = out.delay_batch_trials;
      if (try_delays(choices, true)) {
        rejected_delays.clear();
        batch_backoff = 1;
        continue;
      }
      // A tight arrival/setup bound can reject the same joint edit repeatedly
      // while serial repair works elsewhere. Space out subsequent group trials.
      if (out.delay_batch_trials != attempted) {
        single_steps_left = batch_backoff;
        batch_backoff     = std::min(32u, batch_backoff * 2);
      }
    }
    for (const auto& choice : choices) {
      if (try_delays(std::span{&choice, 1}, false)) {
        rejected_delays.clear();  // A changed layout may make an earlier edit feasible.
        break;
      }
      rejected_delays.emplace(choice.net, choice.driver->master);
    }
  }
  if (current.score.hold >= 0) {
    out.delay_stop = "hold_met";
  }
  return out;
}
}  // namespace livehd::placement
