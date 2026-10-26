// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <optional>
#include "preroute.hpp"
#include "repair_internal.hpp"

namespace livehd::placement::detail {
struct Repeater_snapshot {
  Result                         state;
  std::vector<Preroute_binding>  branches;
  std::vector<Preroute_repeater> repeaters;
};
// Materialize the private graph proposal into matching DEF, RC and RSMT demand
// views. Existing placements and unrelated nets stay unchanged. Estimated RC
// uses explicit routing-layer characterization and must be replaced by actual
// routed RC before publication. No graph attributes or timer state are edited.
Repeater_snapshot repeater_snapshot(const Result&, const Repeater_graph&, const Preroute_binding&, const physical::Technology&,
                                    Layer_rc);
struct Preroute_stage_result {
  Result                        placed;
  std::vector<Preroute_record>  records;
  std::map<std::string, double> timings_ms;
  size_t                        repeater_trees = 0, repeater_cells = 0;
  std::optional<double>         repeater_length_limit_um;
  size_t                        repeater_candidates = 0, repeater_margin_eligible = 0, repeater_attempted = 0;
  bool                          repeater_classification_complete = false;
};
// Short-net batch first, then long-net tree trials sorted by length and slack.
// Uses one whole-stage deadline, and includes planning in each long-net budget.
// Input graph/geometry and caller timing edits are preserved on every exit.
Preroute_stage_result preroute_stage(const Result&, const physical::Technology&, const Route_technology_rc&,
                                     const std::vector<Preroute_binding>&, timing::Whatif&, const Options&, const Preroute_options&,
                                     const Timing_feedback&);
}  // namespace livehd::placement::detail
