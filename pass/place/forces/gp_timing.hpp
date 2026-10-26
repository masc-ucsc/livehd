// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>

#include "legalization.hpp"
#include "pass/place/timing_comparison.hpp"
#include "pin_weights.hpp"
#include "startup.hpp"

namespace livehd::placement::timeclosure {
// Experimental fixed-footprint GP binding. The seven-phase controller and
// mutable-policy rollback are separate integration work. No legacy timer,
// netlist editing callback or persistent placement cache participates.
struct Gp_timing_settings {
  std::shared_ptr<const liberty::timing::Library> models;
  late::Constraints                               constraints;
  late::Driver_options                            driver;
  Pin_weight_settings                             weights;
  int                                             interval = 10, start = 200, correction_max_age = 50;
  double                                          correction_overflow_drop = .1, coefficient = .25;
  double                                          repeatability_noise_ps = 0, regression_tolerance_ps = 1e-6;
  late::Star_options wire{.calibrate_admittance = true, .max_pin_displacement_fraction = .01, .branch_relative_displacement = true};
  bool               continuing_rollback = false;
  // Absent preserves the diagnostic GP policy. A fixed bound defines the
  // density regime in which points may be retained for timing comparison.
  std::optional<double> maximum_density_overflow;
  bool                  legal_output_guard = false;
  // Fresh priorities at the exact retained GP frame, independently off.
  // Positive strengths require the immutable reference/final-legal guard.
  Legalization_strength legalization;
  bool                  legalize_observe        = false;
  // Independently off: one full native calibration retry at the same frame
  // after a supported continuous/Steiner comparison fails. All gates and the
  // stationary hard-period guard still have to pass before state is committed.
  bool                  recalibrate_on_mismatch = false;
  double                startup_coefficient = 0, startup_path_fraction = .30;
  std::optional<double> startup_average_wire_um;
  bool                  legalize_timing_enabled() const { return legalize_observe || legalization.enabled(); }
  void                  validate() const;
};
struct Gp_timing_update {
  Pin_weights           weights;
  std::optional<double> reference_period_ps, continuous_period_ps;
  late::Star_stats      calibration;
  Period_decision       decision = Period_decision::improve;
  bool                  rollback = false, deferred = false;
  bool                  recalibrated_after_mismatch = false;
  std::string           reason;
  double                retained_period_ps = 0, temperature_fraction = 0;
};
struct Legal_timing_check {
  std::optional<double> reference_period_ps, candidate_period_ps;
  Period_decision       decision = Period_decision::unsupported;
  std::string           reason;
  size_t                supported_gp_updates       = 0;
  bool                  response_interval_complete = false;
  bool                  passed() const { return decision == Period_decision::improve || decision == Period_decision::within_noise; }
};
struct Gp_legalization_update {
  Pin_weights weights;
  double      period_ps = 0;
};
// Borrowed only for the duration of the synchronous rejection callback. The
// proposed calibration/history has not been committed. This observation may
// save diagnostics, but cannot authorize a rejected update or change its gates.
struct Gp_timing_rejection {
  const late::Result&            reference;
  const late::Result&            continuous;
  const late::Timing_comparison& comparison;
  const late::Star_context&      star;
  unsigned                       attempt       = 0;
  bool                           retry_planned = false;
};
using Gp_timing_rejection_observer = std::function<void(const Gp_timing_rejection&)>;
// Runs only after all native coverage/comparison/hard-period gates pass, but
// before calibration, multipliers, period history or response latches commit.
// Prepare a complete physical checkpoint in local state; throwing aborts the
// proposal atomically. It may not run another timer or mutate the graph.
using Gp_timing_prepare            = std::function<void(const late::Result&, const Pin_weights&)>;

class Gp_timing_session {
  Gp_timing_settings    settings;
  late::Proxy           proxy;
  late::Star_context    star;
  Pin_weight_bank       bank;
  Period_guard          guard;
  int                   last_iteration = -1, corrected_at = -1;
  std::optional<double> corrected_overflow;
  std::optional<double> legal_reference_period;
  Objective_epoch       legal_reference_epoch;
  size_t                successful_updates    = 0;
  int                   first_force_iteration = -1;
  bool                  response_evaluated    = false;

public:
  Gp_timing_session(const hhds::Graph&, Gp_timing_settings, Rc_policy);
  // Fully native, synchronous, single-thread evaluation. The continuous model
  // must pass the declared numerical/ranking/coverage gates against fresh
  // Steiner timing. The hard-period guard uses that stationary Steiner model,
  // not a period from changing continuous calibration. A regression aborts
  // the trial by default. With continuing_rollback, a regression leaves star
  // and weight history intact, shrinks temperature and requests restoration
  // of the paired retained optimizer state. Spent iteration numbers advance.
  // Failed validation leaves session state unchanged.
  // A legal reference is frozen once, before any GP update. It never shares
  // the GP guard or moving timing target. Callers supply actual legality proof.
  double                 set_legal_reference(const physical::Parasitics&, bool legal);
  Legal_timing_check     check_legal_output(const physical::Parasitics&, bool legal) const;
  // Read-only fresh Steiner timing and unblended LR/path weights. No history,
  // calibration, GP guard, response latch or spent iteration is updated.
  Gp_legalization_update legalization_weights(const physical::Parasitics&) const;
  Startup_weights        startup_weights(const physical::Parasitics&) const;
  Gp_timing_update       update(const physical::Parasitics&, const std::map<std::string, std::pair<double, double>>& locations,
                                int iteration, double density_overflow, const Gp_timing_rejection_observer& rejected = {},
                                const Gp_timing_prepare& prepare = {});
};
}  // namespace livehd::placement::timeclosure
