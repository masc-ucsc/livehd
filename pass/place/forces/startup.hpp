// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "pin_weights.hpp"
namespace livehd::placement::timeclosure {
struct Startup_weights {
  bool                  supported = false;
  std::string           reason;
  size_t                driver_fit_failures = 0;
  std::optional<double> mean_branch_length_um;
  Pin_weights           weights;
  size_t                endpoints = 0, selected = 0;
};
// Fixed broad startup hint: rank constrained endpoint-transition/domain paths
// by fixed-SDC setup cost, select ceil(fraction*N), and trace their actual worst
// predecessors. Each selected path contributes equal total wire mass. Normalize
// physical sink weights to [0,1]; the caller supplies a small force coefficient.
// No LR history, period guard, calibration or timing acceptance is updated.
// A wire-only mean branch prior using explicit technology R/C. Original pin
// membership and ideal/zero-wire nets are preserved; pin loads remain in the
// timing library. This approximate prior is never used by later RC guards.
double               startup_mean_branch_length(const physical::Parasitics&, const std::array<double, 4>& directional_rc);
physical::Parasitics startup_average_parasitics(const physical::Parasitics&, const std::array<double, 4>& directional_rc,
                                                double mean_branch_length_um);
Startup_weights      startup_path_weights(const late::Result&, double fraction = .30);
}  // namespace livehd::placement::timeclosure
