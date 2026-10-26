// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace livehd::domplace {

inline constexpr uint32_t kStageModelVersion = 1;

// One-phase single-terminal schedule (2d-domplace Q1). Launch at posedge nT;
// precharge [nT, nT+T/2); evaluate [nT+T/2, (n+1)T); capture at posedge
// (n+1)T; the next stage launches at (n+1)T + t_cq. Storage is an edge element
// in the functional model; pulse-width and skew stay timing requirements.
// Data only: the timing proxy (subtask E) consumes it, nothing here checks it.
struct Stage_model {
  uint32_t              version = kStageModelVersion;
  std::string           clock;  // the one constrained domain (a port name) in v1
  // Coefficients in ps. std::nullopt is a missing value that must be reported,
  // never an implicit zero.
  std::optional<double> setup_eval_ps, setup_capture_ps, hold_capture_ps;
  std::optional<double> precharge_ps, precharge_margin_ps;
  std::optional<double> uncertainty_first_ps, uncertainty_second_ps, eval_delay_ps;
  char                  storage_init = 'x';  // v1 converts no initialized state
};

}  // namespace livehd::domplace
