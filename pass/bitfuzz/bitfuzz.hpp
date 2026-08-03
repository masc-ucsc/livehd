//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

// Bit fuzzing: strip the per-pin width/sign annotations and make pass.bitwidth
// reconstruct them.
//
// LGraph cells are UNLIMITED width and ALWAYS signed. `bits` / `pin_signed` are
// derived metadata that exist so cgen can emit Verilog and C++ with concrete
// widths; for anything internal (formal, translation, rewriting) they must
// carry no meaning. Any narrowing that is semantically load-bearing has to be
// an explicit cell -- Get_mask / Set_mask / Sext -- never the annotation.
//
// This pass enforces that mechanically: delete the annotations, re-infer, and
// report every pin whose recovered width disagrees with what the front end
// stamped. A downstream LEC (the v2prp2v harness) then decides whether the
// disagreement is behavioural.
namespace livehd::bitfuzz {

enum class Mode {
  Off,    // no-op
  Wires,  // clear every eligible combinational driver pin
  All,    // Wires + register (Flop/Fflop/Latch) q pins
};

[[nodiscard]] std::string_view mode_name(Mode m);
// Returns false when `s` is not a known mode spelling (`*out` untouched).
[[nodiscard]] bool mode_from_string(std::string_view s, Mode* out);

struct Options {
  Mode     mode           = Mode::Off;
  uint64_t seed           = 0;
  // Percentage of eligible register q pins to clear in Mode::All. Selection is
  // a pure function of (seed, pin identity), so it is stable across runs and
  // independent of traversal order -- which is what makes the M3 bisect
  // reproducible.
  int      reg_pct        = 100;
  int      max_iterations = 10;
  // Restore the front-end annotation on any pin inference left PATHOLOGICAL
  // (bits==0, or the overflow sentinel), then re-run so the restored anchors
  // propagate. A pathological width is an inference FAILURE, not a
  // disagreement: leaving it in place poisons cgen instead of testing
  // anything. Disagreements that produced a concrete width are always left as
  // inferred -- that is the experiment.
  bool     repair         = true;
  bool     verbose        = false;
};

// How a snapshotted pin came back.
struct Finding {
  std::string kind;   // same | narrower | wider | sign | unrecovered | vanished
  std::string pin;    // wire_name
  std::string op;     // cell type
  int32_t     was_bits   = 0;
  int32_t     now_bits   = 0;
  bool        was_unsign = false;
  bool        now_unsign = false;
  bool        is_state   = false;
  bool        no_rule    = false;  // the cell type has no bitwidth inference rule at all
  // Only meaningful for `unrecovered`. Repair runs in ROUNDS and restores only
  // pins whose own inputs are already sized, so an unrecovered finding is a
  // ROOT CAUSE: inference had everything it needed and still could not bound
  // it. A pin that was merely starved by an unrecoverable input resolves in a
  // later round and is classified normally instead of padding the failure
  // count. `cyclic` marks the one exception -- a combinational ring where every
  // member is blocked by another, broken by restoring the whole ring.
  bool        cyclic     = false;
};

struct Stats {
  int cleared       = 0;
  int cleared_state = 0;
  int same          = 0;
  int narrower      = 0;  // benign over-declaration by the front end
  int wider         = 0;  // THE target bug class on a non-state pin
  int sign_changed  = 0;
  int unrecovered   = 0;  // still pathological after inference
  int no_rule       = 0;  // subset of unrecovered: no inference rule for that cell
  int repaired      = 0;
  int vanished      = 0;  // const-collapsed by bitwidth's adjust_bw
  int repair_rounds = 0;

  std::vector<Finding> findings;
};

// Runs the whole clear -> infer -> classify -> repair cycle on `g` in place.
Stats fuzz(const std::shared_ptr<hhds::Graph>& g, const Options& opts);

}  // namespace livehd::bitfuzz
