// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace livehd::placement::timeclosure {
// Each vertex is one feasible pin/transition/launch-domain state. The caller
// must apply clock/state cuts and sensitization before constructing this DAG.
// All time values use one common unit. No timer or placer is called here.
// Signed characterized arc delays and launch/required offsets are permitted.
struct Objective_arc {
  size_t from = 0, to = 0;
  double delay = 0;
};
struct Objective_boundary {
  size_t vertex  = 0;
  double arrival = 0;
};
struct Objective_endpoint {
  size_t vertex   = 0;
  double required = 0;
};
struct Objective_network {
  size_t                          vertices         = 0;
  double                          reference_period = 0;
  std::vector<Objective_arc>      arcs;
  std::vector<Objective_boundary> boundaries;
  std::vector<Objective_endpoint> endpoints;
};
struct Objective_settings {
  double window_fraction      = .1;
  // Dimensionless: tau = temperature_fraction * max(T_reference, T*).
  // Scaling every time value preserves all dimensionless multipliers.
  double temperature_fraction = .02;
  void   validate() const;
};
struct Period_objective {
  double              achievable_period = 0, fixed_wns = 0, soft_period = 0, temperature = 0;
  double              optimization_period = 0;
  // Input order is retained. Arc weights are d(soft_period)/d(arc delay).
  // At each vertex, incoming arc mass + boundary mass = vertex mass
  // = outgoing arc mass + endpoint mass. Endpoint mass sums to one.
  std::vector<double> arrivals, soft_arrivals, vertex_mass, arc_mass;
  std::vector<double> boundary_mass, endpoint_mass;
  std::vector<bool>   in_window;
};
// An empty/unconstrained endpoint set, cycle, unreachable vertex, missing
// boundary or nonfinite value is an explicit error.
// A hard max defines T* and the window; the smooth objective only prices paths
// within that fixed hard window. This result alone cannot certify timing.
// Hold the checkpoint's window and temperature fixed when interpreting arc
// multipliers as derivatives; temperature selection itself is not differentiated.
Period_objective period_objective(const Objective_network& network, Objective_settings settings = {});

struct Objective_epoch {
  uint64_t topology = 0, timing_model = 0, calibration = 0, density_stage = 0;
  bool     legal                                    = false;
  bool     characterized_boundary_policy            = false;
  bool     bounded_boundary_policy                  = false;
  bool     operator==(const Objective_epoch&) const = default;
};
enum class Period_decision { improve, within_noise, regression, incomparable, unsupported };
struct Period_guard_settings {
  double repeatability_noise = 0, regression_tolerance = 1e-6;
  double temperature_fraction = .02, minimum_temperature_fraction = 1e-5;
  double maximum_temperature_fraction = .5, rejection_scale = .5;
  void   validate() const;
};
// The retained placement must be reevaluated under a new epoch before rebase.
// Geometry coordinates can change within an epoch; density/legality regimes,
// topology, timing and calibration must be comparable. The caller retains the
// actual placement. No soft objective or moving required-time target is used.
// The supported argument must come from the timing coverage gate; computing
// an objective on supplied numbers does not establish supported timing.
class Period_guard {
  Period_guard_settings settings;
  std::optional<double> best;
  Objective_epoch       epoch;

public:
  explicit Period_guard(Period_guard_settings settings = {});
  void                  rebase(double retained_period, Objective_epoch epoch, bool supported);
  Period_decision       consider(double candidate_period, Objective_epoch epoch, bool supported);
  std::optional<double> best_period() const { return best; }
  double                temperature_fraction() const { return settings.temperature_fraction; }
};
}  // namespace livehd::placement::timeclosure
