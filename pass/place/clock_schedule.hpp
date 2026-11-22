// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "clock_clone.hpp"
#include "clock_hold.hpp"
#include "pass/place/pcrs/clock_gate.hpp"
#include "technology_rc.hpp"

namespace livehd::timing::fast {
class Physical_ranker;
}
namespace livehd::placement {
struct Clock_schedule {
  std::map<uint64_t, unsigned> drive_steps;
  std::map<uint64_t, double>   delay_ps;
  std::set<uint64_t>           separate;
  bool                         balance                                  = false;
  unsigned                     port_source_stages                       = 0;
  auto                         operator<=>(const Clock_schedule&) const = default;
};
struct Clock_trial_layout {
  physical::Floorplan                floorplan;
  std::map<Pin_label, Schedule_leaf> sinks;
  // Resolved Tcl commands when the SDC queries internal design objects.
  std::vector<std::string>           internal_sdc;
  bool                               sdc_supported = true;
  std::map<std::string, Wire_rc>     branch_rc{};
  bool                               port_source_pair = false;
};
struct Clock_score {
  double setup = std::numeric_limits<double>::infinity(), hold = std::numeric_limits<double>::infinity();
  double setup_tns = 0, hold_tns = 0;
};
struct Clock_source_drive_trial {
  unsigned                             stages;
  std::string                          decision;
  std::optional<Clock_score>           score;
  std::optional<Clock_bound_violation> bound;
};
struct Clock_gate_trial {
  std::vector<std::string>   controls;
  std::vector<std::string>   clock_sinks;
  double                     saved_area_um2 = 0, saved_switched_ff = 0, gating_slack_ps = 0, cost_gain = 0;
  std::optional<Clock_score> score;
  std::string                decision;
};
struct Clock_merge_trial : Clock_gate_trial {
  pcrs::Gate_merge gate;
};
struct Gate_leaf_trial : Clock_gate_trial {
  size_t      distribution;
  std::string instance;
  bool        raised = false;
};
struct Clock_clone_trial {
  std::string                          gate, leaf, decision;
  std::optional<physical::Connection>  upstream_tap;
  unsigned                             loads          = 0;
  double                               area_delta_um2 = 0;
  std::optional<physical::Component>   cell;
  std::optional<Clock_bound_violation> bound;
  std::optional<Clock_score>           score;
};
struct Clock_gate_search {
  std::function<std::vector<Clock_clone_leaf>(const Endpoint_hold&, const Endpoint_hold&)> clone_candidates;
  std::function<bool(const physical::Floorplan&, const Clock_clone&)>                      external_clock_legal;
  unsigned                                                                                 clone_budget = 0;
  pcrs::Clock_limits                                                                       clone_limits;
  Output_fanout_limits                                                                     input_limits;
  std::vector<Clock_clone_trial>                                                           clone_trials;
  std::vector<Clock_clone>                                                                 copies;
  std::optional<Clock_score>                                                               before_clones;
  std::vector<Gate_leaf_trial>                                                             leaves;
  std::function<std::vector<Gate_leaf_trial>()>                                            leaf_candidates;
  std::function<void(size_t, bool)>                                                        direct_leaf;
  std::optional<Clock_score>                                                               baseline, before_leaves;
  std::vector<Clock_merge_trial>                                                           trials;
  std::vector<pcrs::Gate_merge>                                                            candidates, active;
  size_t                                                                                   budget = 0;
  std::function<bool(Clock_merge_trial&)>                                                  prepare;
  std::function<void(const pcrs::Gate_merge&)>                                             apply;
  std::function<void()>                                                                    undo;
  std::function<double()>                                                                  clock_area, switched_cap;
  bool                                                                                     measured_activity = false;
};
struct Clock_schedule_trial {
  unsigned                             round;
  std::string                          launch_pin, endpoint_pin;
  bool                                 targets_hold;
  Clock_schedule                       changes;
  std::optional<Clock_score>           score;
  std::string                          decision, compensated_endpoint;
  bool                                 joint = false;
  std::optional<Clock_bound_violation> bound;
};
struct Clock_schedule_result {
  Clock_schedule                       schedule;
  std::optional<Clock_score>           baseline, selected;
  unsigned                             offset_rejections = 0, hold_rejections = 0;
  std::optional<Clock_bound_violation>  worst_offset, worst_hold;
  std::vector<Hold_delay>               delays;
  double                                added_area_um2 = 0;
  unsigned                              delay_trials = 0, delay_batch_trials = 0, delay_batches = 0;
  std::string                           delay_stop, skew_stop, balance_decision;
  std::vector<Clock_source_drive_trial> source_drive_trials;
  std::vector<std::string>              unsupported_checks;
  std::optional<Clock_score>            unbalanced_score, balanced_score;
  std::optional<Clock_bound_violation> balance_bound;
  std::optional<Clock_score>           after_delays;
  double                               selected_max_shift_ps = 0;
  std::vector<Clock_schedule_trial>    trials;
  unsigned                             timed = 0, infeasible = 0, accepted = 0;
  unsigned                             journal_trials = 0, full_evaluations = 0;
};
Clock_schedule_result choose_clock_schedule(const Clock_schedule&, unsigned rounds, double period_fraction, double hold_margin_ps,
                                            const Hold_repair_options& hold_repair, const physical::Technology&, const Net_wire_rc&,
                                            std::unique_ptr<timing::fast::Physical_ranker>,
                                            const std::function<Clock_trial_layout(const Clock_schedule&)>& build,
                                            const std::function<void(Clock_trial_layout&)>&                 resolve_sdc,
                                            Clock_gate_search* gates = nullptr, bool incremental = true);
}  // namespace livehd::placement
