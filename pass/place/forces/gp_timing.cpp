// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gp_timing.hpp"

#include <cmath>
#include <stdexcept>

namespace livehd::placement::timeclosure {
namespace {
Gp_timing_settings checked(Gp_timing_settings value) {
  value.validate();
  return value;
}
// Native timing is usable only with certified period coverage.
bool            covered(const late::Result& timing) { return timing.certified && timing.achievable_period_ps; }
// Legal-frame comparisons use their own density stage, never the GP epoch.
Objective_epoch legal_epoch(const late::Result& timing) {
  auto epoch          = timing_epoch(timing);
  epoch.density_stage = 1;
  epoch.legal         = true;
  return epoch;
}
// The diagnostic, extended by the first unsupported timing construct.
std::string coverage_message(const char* message, const late::Result& timing) {
  return message + (timing.unsupported.empty() ? std::string{} : ": " + timing.unsupported.front());
}
}  // namespace
void Gp_timing_settings::validate() const {
  weights.validate();
  legalization.validate();
  if (legalize_timing_enabled() && !legal_output_guard) {
    throw std::invalid_argument("native legalization priorities require the final legal timing guard");
  }
  if (!models || !constraints.complete || !constraints.unsupported.empty() || interval <= 0 || start < 0 || correction_max_age <= 0
      || !std::isfinite(correction_overflow_drop) || correction_overflow_drop <= 0 || !std::isfinite(coefficient)
      || coefficient < 0) {
    throw std::invalid_argument("invalid native GP timing models/constraints/cadence/force policy");
  }
  if (maximum_density_overflow && (!std::isfinite(*maximum_density_overflow) || *maximum_density_overflow < 0)) {
    throw std::invalid_argument("invalid native GP density acceptance bound");
  }
  if (!std::isfinite(startup_coefficient) || startup_coefficient < 0 || startup_coefficient > coefficient
      || !std::isfinite(startup_path_fraction) || startup_path_fraction <= 0 || startup_path_fraction > 1) {
    throw std::invalid_argument("invalid native startup timing bias");
  }
  if (startup_average_wire_um && (!std::isfinite(*startup_average_wire_um) || *startup_average_wire_um < 0)) {
    throw std::invalid_argument("invalid startup average wire length");
  }
  Period_guard_settings limits;
  limits.repeatability_noise  = repeatability_noise_ps;
  limits.regression_tolerance = regression_tolerance_ps;
  limits.temperature_fraction = weights.objective.temperature_fraction;
  limits.validate();
}
Gp_timing_session::Gp_timing_session(const hhds::Graph& graph, Gp_timing_settings config, Rc_policy policy)
    : settings(checked(std::move(config)))
    , proxy(graph, settings.models, settings.constraints)
    , bank(settings.weights)
    , guard({.repeatability_noise  = settings.repeatability_noise_ps,
             .regression_tolerance = settings.regression_tolerance_ps,
             .temperature_fraction = settings.weights.objective.temperature_fraction}) {
  settings.validate();
  star.policy  = std::move(policy);
  star.options = settings.wire;
}
Startup_weights Gp_timing_session::startup_weights(const physical::Parasitics& rc) const {
  if (last_iteration >= 0) {
    throw std::invalid_argument("startup bias must precede GP timing updates");
  }
  const double mean
      = settings.startup_average_wire_um ? *settings.startup_average_wire_um : startup_mean_branch_length(rc, star.policy.per_um);
  const auto prior  = startup_average_parasitics(rc, star.policy.per_um, mean);
  const auto timing = proxy.evaluate(&prior, nullptr, {.capture_feasible_graph = true, .driver = settings.driver});
  if (!covered(timing) || !timing.feasible_graph) {
    Startup_weights result;
    result.driver_fit_failures   = timing.driver_fit_failures;
    result.mean_branch_length_um = mean;
    result.reason                = coverage_message("startup prior lacks certified period coverage", timing);
    return result;
  }
  auto result                  = startup_path_weights(timing, settings.startup_path_fraction);
  result.driver_fit_failures   = timing.driver_fit_failures;
  result.mean_branch_length_um = mean;
  return result;
}
Gp_legalization_update Gp_timing_session::legalization_weights(const physical::Parasitics& rc) const {
  const auto timing = proxy.evaluate(&rc, nullptr, {.capture_feasible_graph = true, .driver = settings.driver});
  if (!covered(timing)) {
    throw std::invalid_argument(coverage_message("native legalization timing lacks supported period coverage", timing));
  }
  auto config                           = settings.weights;
  config.objective.temperature_fraction = guard.temperature_fraction();
  Pin_weight_bank fresh(config);
  return {fresh.update(timing), *timing.achievable_period_ps};
}
Gp_timing_update Gp_timing_session::update(const physical::Parasitics&                             rc,
                                           const std::map<std::string, std::pair<double, double>>& locations, int iteration,
                                           double overflow, const Gp_timing_rejection_observer& rejected,
                                           const Gp_timing_prepare& prepare) {
  if (iteration < 0 || iteration <= last_iteration || !std::isfinite(overflow) || overflow < 0) {
    throw std::invalid_argument("native GP timing requires increasing iterations and finite density overflow");
  }
  if (settings.maximum_density_overflow && overflow > *settings.maximum_density_overflow) {
    Gp_timing_update result;
    result.deferred             = !guard.best_period();
    result.rollback             = !result.deferred;
    result.reason               = "density_overflow";
    result.decision             = Period_decision::incomparable;
    result.retained_period_ps   = guard.best_period().value_or(0);
    result.temperature_fraction = guard.temperature_fraction();
    if (result.rollback && !settings.continuing_rollback) {
      throw std::invalid_argument("native GP density acceptance bound rejected checkpoint; trial aborted");
    }
    // No native timing, calibration or multiplier update is evaluated or
    // accepted in an incompatible density regime. Only spent policy time moves.
    last_iteration = iteration;
    return result;
  }
  const auto supported = [](const late::Result& value) {
    if (!covered(value)) {
      throw std::invalid_argument(coverage_message("native GP timing lacks supported period coverage", value));
    }
  };
  const auto reference = proxy.evaluate(&rc, nullptr, {.driver = settings.driver});
  supported(reference);
  auto next_star      = star;
  next_star.locations = locations;
  next_star.refresh   = corrected_at < 0 || iteration - corrected_at >= settings.correction_max_age
                      || (corrected_overflow && *corrected_overflow - overflow >= settings.correction_overflow_drop);
  auto continuous = proxy.evaluate(&rc, &next_star, {.capture_feasible_graph = true, .driver = settings.driver});
  supported(continuous);
  auto comparison   = late::compare_timing(continuous, reference);
  bool recalibrated = false;
  if (!comparison.comparison_gates_pass || !comparison.supported_coverage_pass) {
    if (rejected) {
      rejected({reference, continuous, comparison, next_star, 0, settings.recalibrate_on_mismatch});
    }
    if (settings.recalibrate_on_mismatch) {
      // A comparison failure is a dirty-calibration event, not authorization
      // to use the failed surrogate. Recompute exactly once at these same
      // coordinates. Native coverage, every numerical/ranking gate, and the
      // stationary Steiner period guard remain mandatory.
      next_star.refresh = true;
      continuous        = proxy.evaluate(&rc, &next_star, {.capture_feasible_graph = true, .driver = settings.driver});
      supported(continuous);
      comparison   = late::compare_timing(continuous, reference);
      recalibrated = true;
      if ((!comparison.comparison_gates_pass || !comparison.supported_coverage_pass) && rejected) {
        rejected({reference, continuous, comparison, next_star, 1, false});
      }
    }
    if (!comparison.comparison_gates_pass || !comparison.supported_coverage_pass) {
      throw std::invalid_argument("native GP continuous/Steiner numerical, ranking or coverage gate failed");
    }
  }
  auto            next_guard = guard;
  const auto      epoch      = timing_epoch(reference);
  Period_decision decision   = Period_decision::improve;
  if (!next_guard.best_period()) {
    next_guard.rebase(*reference.achievable_period_ps, epoch, true);
  } else {
    decision = next_guard.consider(*reference.achievable_period_ps, epoch, true);
  }
  Gp_timing_update result;
  result.reference_period_ps         = *reference.achievable_period_ps;
  result.continuous_period_ps        = *continuous.achievable_period_ps;
  result.calibration                 = next_star.stats;
  result.decision                    = decision;
  result.retained_period_ps          = *next_guard.best_period();
  result.temperature_fraction        = next_guard.temperature_fraction();
  result.recalibrated_after_mismatch = recalibrated;
  if (decision == Period_decision::regression && settings.continuing_rollback) {
    auto next_bank = bank;
    next_bank.set_temperature_fraction(result.temperature_fraction);
    bank            = std::move(next_bank);
    guard           = std::move(next_guard);
    last_iteration  = iteration;
    result.rollback = true;
    result.reason   = "hard_period";
    return result;
  }
  if (decision == Period_decision::regression || decision == Period_decision::incomparable
      || decision == Period_decision::unsupported) {
    throw std::invalid_argument("native GP hard-period guard rejected checkpoint; trial aborted without accepting placement");
  }
  auto next_bank = bank;
  next_bank.set_temperature_fraction(result.temperature_fraction);
  result.weights = next_bank.update(continuous);
  if (prepare) {
    prepare(continuous, result.weights);
  }
  if (next_star.refresh) {
    corrected_at       = iteration;
    corrected_overflow = overflow;
  }
  star           = std::move(next_star);
  bank           = std::move(next_bank);
  guard          = std::move(next_guard);
  last_iteration = iteration;
  ++successful_updates;
  if (first_force_iteration < 0) {
    first_force_iteration = iteration;
  } else {
    response_evaluated = response_evaluated || iteration - first_force_iteration >= settings.interval;
  }
  return result;
}
double Gp_timing_session::set_legal_reference(const physical::Parasitics& rc, bool legal) {
  if (!legal || legal_reference_period || last_iteration >= 0) {
    throw std::invalid_argument("native legal timing reference requires one proven legal frame before GP");
  }
  const auto timing = proxy.evaluate(&rc, nullptr, {.driver = settings.driver});
  if (!covered(timing)) {
    throw std::invalid_argument(coverage_message("native legal reference lacks supported period coverage", timing));
  }
  legal_reference_period = *timing.achievable_period_ps;
  legal_reference_epoch  = legal_epoch(timing);
  return *legal_reference_period;
}
Legal_timing_check Gp_timing_session::check_legal_output(const physical::Parasitics& rc, bool legal) const {
  Legal_timing_check result;
  result.reference_period_ps        = legal_reference_period;
  result.supported_gp_updates       = successful_updates;
  result.response_interval_complete = response_evaluated;
  if (!legal || !legal_reference_period) {
    result.decision = Period_decision::incomparable;
    result.reason   = "legal output requires a compatible proven legal reference and candidate";
    return result;
  }
  const auto timing          = proxy.evaluate(&rc, nullptr, {.driver = settings.driver});
  result.candidate_period_ps = timing.achievable_period_ps;
  if (!covered(timing)) {
    result.reason = coverage_message("native legal output lacks supported period coverage", timing);
    return result;
  }
  Period_guard legal_guard(
      {.repeatability_noise = settings.repeatability_noise_ps, .regression_tolerance = settings.regression_tolerance_ps});
  legal_guard.rebase(*legal_reference_period, legal_reference_epoch, true);
  result.decision = legal_guard.consider(*timing.achievable_period_ps, legal_epoch(timing), true);
  if (!result.passed()) {
    result.reason = "final legal hard period regresses against the imported legal reference";
  }
  if (result.passed() && !response_evaluated) {
    result.decision = Period_decision::unsupported;
    result.reason   = "native GP feedback has not had a supported response interval";
  }
  return result;
}
}  // namespace livehd::placement::timeclosure
