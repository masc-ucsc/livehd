// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The region driver's vocabulary shared with every backend: a region's QoR
// row, the stitched-design score of the ware trials, and the per-region option
// overrides (region_opts).
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "arith.hpp"

namespace livehd::synth {

// Area without a target; Liberty delay (then area) with a target. Endpoint
// delays are sorted worst first, so improving a tied critical path counts.
struct Ware_qor {
  double             area = 0.0;
  std::vector<float> delays;
};
bool  ware_qor_better(const Ware_qor& baseline, const Ware_qor& candidate, bool timing);
float ware_delay_target(std::string_view value);

// A backend's score of the stitched design (Region_backend::score).
struct Ware_score : Ware_qor {
  bool                                    valid = false;
  absl::flat_hash_set<std::string>        critical_regions;
  absl::flat_hash_map<std::string, float> region_path_delay;
};

// Per-region (color-keyed) overrides of the mapping options that vary per
// region (2opt-freq C). Unset fields inherit the run-level options. Two
// sources, later wins: a "region_opts" member inside the source graph's
// coloring_info JSON (the block-attribute channel, 2opt-freq B), then the
// --set pass.abc.region_opts CLI JSON. `flow` and `load` are the backend's own
// command strings; the driver carries them to the backend unread.
struct Region_opts {
  std::optional<bool>              ware;
  std::optional<std::string>       flow;
  std::optional<std::string>       delay;
  std::optional<std::string>       load;
  std::optional<arith::Adder_kind> adder;
  std::optional<int>               block_size;
  std::optional<arith::Mult_kind>  multiplier;
  std::optional<bool>              reverse_barrel;
};
using Region_opts_map = std::map<int, Region_opts>;

// Parse {"<color>":{"flow":…,"delay":…,"load":…,"adder":…,"block_size":…,
// "multiplier":…},…}. Unknown keys and malformed values are hard errors (a
// mistyped hint must never silently no-op). Returns nullopt after a diag.
std::optional<Region_opts_map> parse_region_opts(std::string_view json, std::string_view where);

// Per-region quality of results (2opt-freq A). The region driver fills the
// identity and input-size fields, the backend the mapped result: `gates`/
// `area`/`logic_depth`/`delay`/`crit_*` (the base the region cache stores),
// and the objective and boundary-refinement fields it reports. With ABC,
// `delay` is its mapped-delay estimate from the Liberty pin-to-pin data (unit
// logic depth untimed; picoseconds from its SCL timer with a delay target and
// an NLDM Liberty). Per-region only: paths crossing region/blackbox
// boundaries are invisible here (pass.opentimer is the whole-design scorer).
struct Region_qor {
  std::shared_ptr<const std::string> hook_evidence;  // Region_rewrite::evidence
  std::string                        module;  // region module name (<top>__c<color>)
  int                                color       = 0;
  bool                               ctrl        = false;
  int                                ware_trials = 0;
  std::string                        ware_selected;
  uint64_t                           input_nodes = 0;  // source-region nodes before bit blasting
  uint64_t                           input_ge    = 0;  // graph_util synthesis-GE estimate before ABC
  // Predicted generic-AIG size of the same cone (graph/predict_abc_size.hpp),
  // the unit `pass.color synth --set pass.color.synth.mode=cones` thresholds on. Reported
  // NEXT TO input_ge, never instead of it: the two are different estimates of
  // the same input, and the only ground truth for either is `gates` below --
  // there is no per-op post-ABC attribution, so a region sum is the whole
  // measurement. Every production run therefore validates both predictors.
  uint64_t                           pred_aig    = 0;
  int                                gates       = 0;    // mapped standard cells actually minted (bypassed buffers excluded)
  double                             area        = 0.0;  // sum of their Liberty cell areas
  // Identity buffers ABC minted to decouple a CI->CO / gate->many-CO edge that
  // the read-back aliased away (pass 1b): not in `gates`/`area`, not in the
  // netlist. Diagnostic only -- a cache hit reports 0 (the row is not
  // persisted with it; its gates/area are already net of the bypass).
  int                                bypassed    = 0;
  int         logic_depth = -1;     // mapped ABC gate levels between region/state boundaries, before read-back buffer bypass
  float       delay       = -1.0f;  // critical arrival (unit-delay depth, or ps with an NLDM GENLIB); <0 => unavailable
  std::string crit_output;          // region output port with the worst arrival
  std::string crit_src;             // "file:line" of that output's original driver (may be empty)
  // The mapping-objective decision (map_region's budget ladder), diagnostic
  // only -- a cache hit reports none of it. `budget` is the region's delay
  // budget in ps (target minus the register margin when it holds flops; <0 =
  // no target). `candidate` names which mapping the region kept: "delay" (the
  // `&nf`-based delay flow, sized to the budget) or "area" (the `amap`-based
  // area flow, kept only when it ALSO met the budget with less SCL area);
  // empty when no comparison ran (no target, custom flow, dummy-PO region,
  // area_flow=none, over large_ge). The two pairs are the SCL timer's numbers
  // for each candidate at decision time (<0 = not run).
  float       budget = -1.0f;
  std::string candidate;
  std::string baseline_worker;  // current invocation only; cache hits leave this empty
  float       delay_flow_delay   = -1.0f;
  double      delay_flow_area    = -1.0;
  float       area_flow_delay    = -1.0f;
  double      area_flow_area     = -1.0;
  // Blackboxed div/mod nodes in this region: their cones are NOT mapped, so
  // gates/area/delay under-report — the score is partial until the div is
  // strength-reduced away. Surfaced so an agent never trusts a blind score.
  int         div_blackbox       = 0;
  // Where this region's wall time went, and whether the incremental cache was
  // able to take it. Without these two, "the cache hit 199 of 264 regions" says
  // nothing about whether the run got faster — the misses can hold all the time.
  double      ms                 = 0.0;   // wall ms this region spent in map_region
  uint64_t    peak_rss_kb        = 0;     // whole-process high-water after this color; 0 = unavailable/cache hit
  uint64_t    color_peak_rss_kb  = 0;     // per-color peak growth over its entry RSS baseline
  const char* cache              = "";    // "" (no cache) | hit | mapped | uncacheable | store-failed
  bool        resynth            = true;  // this invocation rebuilt the region (false = incremental cache hit)
  // Partition-boundary refinement (abc_boundary.cpp): how many of this region's
  // port bits cross to another region or a primary IO, how many cells the
  // exact re-size changed, and the SCL timer's view of the region BEFORE the
  // re-size under the exact environment (<0 = the refinement did not run on
  // it). `delay`/`area` above are updated to the re-sized netlist.
  int         boundary_bits      = 0;
  int         boundary_resized   = 0;
  float       boundary_delay_pre = -1.0f;
  double      boundary_area_pre  = -1.0;
};

}  // namespace livehd::synth
