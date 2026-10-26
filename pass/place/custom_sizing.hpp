// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// Post-legal sizing of custom sequential variants (todo/livehd/2d-domplace.html,
// G): instances of Options::custom_cells (the provisional domino masters)
// change drive variant within their family. A critical instance (negative
// output slack) steps up; once its family's top variant is in place, or its
// up move was rejected, its output net is buffered (the smallest inventory
// buffer takes every sink but the worst; buffers=false never buffers). An
// instance with spare slack steps down to recover area.
//
// Every trial is one edit materialized on a clone, re-placed through the
// two-row ECO (Options::multirow_eco, nearest-whitespace legalization of the
// cells it displaces) and fully evaluated, so geometry, pins, RC and timing
// are fresh. A trial is accepted when the period (Feedback::delay) is not
// worse and resize_accepts holds; a rejected trial or a packing failure
// (Multirow_failure, or a variant wider than every row, which is rejected
// without a trial) discards the clone and leaves the state untouched. The
// netlist is never resynthesized. Writes domino_sizing.json and the
// qor.domino_sizing block in options.output_dir.
#include <cstdint>

#include "multirow_eco.hpp"
#include "repair.hpp"

namespace livehd::placement {
struct Custom_sizing_options {
  unsigned            rounds = 4, max_trials = 64;
  bool                buffers    = true;
  double              epsilon_ps = 1e-3;
  Multirow_eco_bounds eco;
};
// `legal` is a legal full placement after final legalization
// (optimization_pending false) whose options carry the custom catalog;
// `evaluate` gives the period as Feedback::delay, output slack per
// "instance:pin" and the area basis of acceptance.
Repair_result size_custom_placed(Result legal, const Options& options, const Evaluate& evaluate,
                                 const Custom_sizing_options& sizing);
}  // namespace livehd::placement
