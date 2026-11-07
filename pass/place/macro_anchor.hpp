// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dreamplace/native/placement.hpp"

namespace livehd::placement {
// synth.place.macro.anchor: moving soft anchors inside macro-stage GP. Every
// refresh runs the stage's own end-of-plan macro legalization (native macro LP
// + optional macro snap + the host after_macro_legalize chain: edge snap, track
// snap and any later pass such as dead-space collapse) on a COPY of the current
// GP macro positions. The legal macro centers become the targets of the native
// virtual macro pulls (the warpspace pull objective), so cells settle around the
// macros' final positions and the final hard pass is a small correction.
struct Macro_anchor_options {
  bool                enabled        = false;
  double              weight         = .5;        // peak pull weight 0..8 (x the mean ordinary macro force); 0 measures only
  double              start_overflow = .3;        // first targets once GP overflow is at or below this
  std::vector<double> milestones{.15, .1};        // also refresh when overflow crosses each (descending)
  int                 refresh_interval     = 30;  // periodic refresh (GP iterations) until targets stabilize
  int                 ramp                 = 3;   // refreshes to reach full weight (1 = full at once)
  // After the targets stabilize, keep refreshing and ramp the weight to this over `ramp` more refreshes so macros
  // converge onto their targets (0 or <= weight: stop refreshing at stability, the plain schedule).
  double              final_weight         = 2;
  double              gate                 = 1;    // wirelength-gradient gating strength; 0 disables gating
  double              stable_fraction      = .05;  // stop periodic refreshes when fewer macros change target
  double              stable_distance      = .05;  // target change threshold / smaller macro extent
  int                 max_refreshes        = 40;
  int                 settle               = 20;     // GP iterations kept after every refresh before GP may stop
  bool                swaps                = false;  // exchange targets of crossing, equal-size macro pairs
  double              swap_tolerance       = .05;    // relative width/height difference allowed for a swap pair
  int                 swap_neighbors       = 8;      // nearest same-size candidates examined per macro
  int                 swap_lock            = 2;      // refreshes a swapped pair holds its exchanged targets
  bool                swap_jump            = true;   // also exchange the two macros' GP positions (native jump pull)
  // Refreshes in the last `dead_space_refreshes` refresh windows before the iteration cap are `late`: their targets
  // also run the expensive end-of-plan passes (macro.dead_space collapse). 0: never; -1: every refresh.
  int                 dead_space_refreshes = 2;
  // Early macro density inflation: per-side margin (fraction of the padded macro size) on top of the planning pad,
  // ramped down with GP overflow to exactly zero at the first anchor refresh. 0 disables.
  double              inflate              = 0;
  // Anchor selection. "edge+random" (default): each refresh classifies targets. Edge macros (target within one channel+halo of a
  // core side, or chained to such a macro through gaps of at most the macro spacing) get `edge_weight`; a fresh deterministic
  // `random_fraction` sample of the other macros gets `random_weight`; the rest are free that window. Both weights
  // ramp over `ramp` refreshes; final_weight is not used. `gate` applies only to the random (weak) anchors unless
  // `edge_gate` is set. "all" (round-4 behavior): every movable macro is anchored with `weight` (the schedule above).
  std::string         select               = "edge+random";
  double              edge_weight          = 4;
  double              random_weight        = 1;
  double              random_fraction      = .25;
  bool                edge_gate            = false;
  bool                edge_chain           = true;  // false: only targets within channel+halo of a core side are edge
  // Spring softening length as a fraction of the mean max(width, height) of the macros (0: the native default, 1).
  // Inside it the pull grows linearly with the distance; beyond it the pull is at its cap.
  double              spring_length        = .1;
  // Target hysteresis: the generator legalizes macro centers moved this fraction from GP toward the previous targets.
  double              target_hysteresis    = .5;
  // Final hard pass starts from the last legal target set (the generator's legal output at the last successful
  // refresh, before swaps/locks: a legal macro placement from the same chain) instead of
  // the raw GP pose, when the last refresh succeeded. final_snap still reports GP pose vs legal result.
  bool                final_targets        = true;
  std::int64_t        channel_x = 0, channel_y = 0;  // macro spacing per axis (DBU), technology-owned
  double              dbu_per_um = 1;                // technology-owned, not a user option
};
void validate_macro_anchor_options(const Macro_anchor_options&);

// Legalizes the movable macros of a physical (unpadded) design copy in place.
// `late` marks the last `dead_space_refreshes` refreshes, the only ones that run
// expensive end-of-plan passes (dead-space collapse). Throws on failure.
using Macro_target_generator = std::function<void(DreamPlace::native::Design&, bool late)>;
struct Native_target_config {
  int                                                    bins_x = 32, bins_y = 32;
  bool                                                   macro_snap = false, macro_snap_regular = false;
  DreamPlace::native::FloorplanOptions                   macro_snap_options;
  double                                                 macro_snap_tolerance = .25;
  std::int64_t                                           macro_snap_weight    = 100;
  std::function<void(DreamPlace::native::Design&, bool)> after_macro_legalize;  // host chain (physical geometry, late)
};
// The native end-of-plan macro legalization (as place() runs it on the halo-
// padded design, with macro-only legalization), on a copy.
Macro_target_generator native_target_generator(Native_target_config);

// Per-macro HPWL subgradient, each net weighted 1/(degree-1). A macro that is
// not the unique extreme of a net on an axis gets no pull from it on that axis.
struct Macro_wire_gradient {
  double x = 0, y = 0;
};
std::vector<Macro_wire_gradient> macro_wire_gradients(const DreamPlace::native::Design&, const std::vector<int>& macros);
// Exact HPWL change over the nets incident to a or b when their centers move
// from (ta, tb) to (tb, ta); other nodes stay at the design positions. Negative
// improves wirelength.
double macro_swap_gain(const DreamPlace::native::Design&, int a, int b, std::pair<double, double> ta, std::pair<double, double> tb);
// Gated per-macro pull weight: base / (1 + gate * max(0, g . u) / reference),
// where u points from the center to the target.
double gated_anchor_weight(double base, double gate, const Macro_wire_gradient& g, double dx, double dy, double reference);

struct Macro_anchor_refresh {
  int         iteration = 0;
  double      overflow  = 0;
  int         changed = 0, swaps = 0, locked = 0, macros = 0;
  int         edge = 0, random = 0;  // select=edge+random: anchored edge / random macros
  double      base_weight = 0, mean_weight = 0, mean_distance = 0, max_distance = 0, seconds = 0;
  bool        late = false;
  std::string reason, status;
};

class Macro_anchor {
public:
  Macro_anchor(Macro_anchor_options, std::vector<int> macros, int iteration_budget, Macro_target_generator generator,
               std::string report);
  // The native GP overflow of the latest progress sample (the metric GP stops on).
  void observe_overflow(double overflow) {
    if (initial_overflow < 0) {
      initial_overflow = overflow;
    }
    gp_overflow = overflow;
  }
  // Native macro_density_margin schedule (o.inflate ramped by overflow toward start_overflow, then 0).
  double                                                    density_margin() const;
  // Native macro_pull_update hook (physical snapshot); nullopt keeps the current pulls and momentum.
  std::optional<std::vector<DreamPlace::native::MacroPull>> update(const DreamPlace::native::Design&, int iteration);
  // Records the macro centers entering the final hard legalization; with final_targets, moves them to the last targets.
  void                                                      before_legalize(DreamPlace::native::Design&);
  // final_targets: inside the final hard pass, after the native macro legalization, replaces the macro positions with
  // the last targets (already the output of the same host chain) and returns true; the host then skips its chain.
  // One-shot: false before before_legalize() seeded the pass, after the first call, or without final_targets.
  bool                                                      final_override(DreamPlace::native::Design&);
  // Final legal design: writes the report (final-snap displacement, refreshes, swaps).
  void                                                      finish(const DreamPlace::native::Design&, int iterations);
  const std::vector<Macro_anchor_refresh>&                  refreshes() const { return history; }
  int                                                       swaps() const { return swap_count; }

private:
  void refresh(const DreamPlace::native::Design&, int iteration, double overflow, const std::string& reason);

  Macro_anchor_options                       o;
  std::vector<int>                           macros;
  int                                        budget      = 0;
  double                                     gp_overflow = -1, initial_overflow = -1;
  Macro_target_generator                     generator;
  std::string                                report;
  std::vector<std::pair<double, double>>     targets;        // per macros[] entry, center
  std::vector<std::pair<double, double>>     legal_targets;  // the generator's legal centers at the last ok refresh
  std::vector<int>                           lock;           // refreshes left holding a swapped target
  std::vector<std::pair<double, double>>     pre_legal;
  std::vector<Macro_anchor_refresh>          history;
  std::vector<std::pair<int, int>>           swapped_pairs;  // node ids; a pair is never exchanged twice
  std::vector<bool>                          jump;           // per macros[] entry, for the next pulls
  int                                        last = -1000000, next_milestone = 0, swap_count = 0, failures = 0;
  double                                     seconds          = 0;
  bool                                       stable           = false;
  bool                                       seeded           = false;  // final pass started from the last targets
  bool                                       override_pending = false, overridden = false;
  int                                        converge = 0;  // refreshes since the targets first stabilized
  std::vector<DreamPlace::native::MacroPull> pulls;
  std::vector<int>                           kind;  // per macros[] entry at the last refresh: 0 free, 1 edge, 2 random, 3 all
};
// select=edge+random classification of legal target boxes (physical design, centers `targets`): true for edge macros.
std::vector<bool> edge_anchored_macros(const DreamPlace::native::Design&, const std::vector<int>& macros,
                                       const std::vector<std::pair<double, double>>& targets, std::int64_t channel_x,
                                       std::int64_t channel_y, bool chain = true);
}  // namespace livehd::placement
