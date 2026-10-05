// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>

#include "xag.hpp"

namespace livehd::usyn {
struct Native_cell_cost {
  uint8_t  input_phases = 0;
  double   area         = 0;
  uint32_t gates        = 1;
};
struct Native_cost_model {
  // Exact cell truth tables in the ordered cut basis. Pin permutations and
  // phases are explicit; complemented inputs demand their actual source rail.
  std::array<std::map<uint16_t, std::vector<Native_cell_cost>>, 5> cells;
  double                                                           inverter_area  = 0;
  uint64_t                                                         admitted_cells = 0, skipped_cells = 0;
};
// Uses LiveHD's ABC-free Liberty inventory. Unsupported functions contribute
// no estimates. No interpolation, timing arcs or physical timing guarantee.
std::optional<Native_cost_model> read_native_cost_model(const std::string& libraries, Budget& work);
std::optional<Native_cost_model> read_native_cost_text_model(const std::string& contents, Budget& work);
struct Native_estimate {
  Status   status  = Status::invalid;
  double   area    = 0;
  uint64_t gates   = 0;
  uint32_t depth   = 0;
  bool     limited = false;
};
// Bounded four-input covering estimate, counting each selected cell/rail once
// across all outputs. Overlap is represented by duplicated covering cells,
// rather than giving dead candidates or invisible inverter rails free credit.
// Final ABC mapping, buffering, sizing and STA determine the actual result.
Native_estimate estimate_native_cost(const Xag& graph, std::span<const Xsignal> outputs, const Native_cost_model& model,
                                     Budget& work, bool gate_objective = true);
}  // namespace livehd::usyn
