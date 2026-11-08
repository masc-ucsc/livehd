// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "inou/lefdef/tech_profile.hpp"

namespace livehd::placement {
// What a power-grid strategy (physical::Pg_strategy) requires from the clear
// spacing between two macro bodies, derived only from the description and the
// LEF, so every technology gets its rule from its own profile.
//
// A channel between two macros runs perpendicular to the gap axis (a gap along
// y is a horizontal channel). A supply shape that crosses the channel on a
// layer the macros block (a vertical core strap across a horizontal channel,
// standard-cell rails across a vertical one) is cut into an isolated segment.
// Following the strategy's connect pairs upward from that layer to the first
// layer running along the channel gives the segment's feed:
//  * feeder: the chain starts at the followpin rails. Generators insert feeder
//    straps for isolated rails (pdngen channel repair, the native generator's
//    rail feeders), so the channel needs room for one strap per net beside a
//    possible existing strap, clear of both halos.
//  * lattice: any other crossing strap. Its segment connects only where a
//    periodic strap of the feed layer, at that layer's fixed pitch/offset,
//    lies wholly inside the channel. The channel must contain one strap of
//    every net: always true when the clear band is at least pitch + width
//    (any_phase); with a band as narrow as one strap group, true only when
//    the macro edges are aligned to the strap lattice (aligned).
//  * none: no isolated segment (the crossing layers pass over the macros, or
//    no crossing layer is blocked).
// A layer is blocked when any hard macro master has OBS, conservative
// obstruction or pin geometry on it; a virtual memory proxy blocks every layer
// (as in the generator). The band excludes, per side, the largest
// macro-grid halo plus the spacing of the via stack's layers (pdngen keeps vias
// out of macro-grid areas by that spacing).
// Clearance (microns) a supply shape of `width` needs on `layer`: the plain spacing rules and the parallel-run
// table row of that width at its longest run (pdngen bloats obstructions by the same table); 0 without rules.
double pg_layer_spacing(const physical::Layer& layer, double width);
// Clearance (microns) the generator keeps between rail feeder straps of `stripe`: the strap family's spacing, or the
// LEF spacing (pg_layer_spacing) when that is tighter.
double pg_feeder_spacing(const physical::Layer& layer, const physical::Pg_stripe& stripe);
struct Pg_gap_rule {
  enum class Kind { none, feeder, lattice };
  Kind                               kind = Kind::none;
  std::string                        source, feed;   // crossing (blocked) layer and the layer that feeds it
  double                             any_phase = 0;  // microns: spacing that is always buildable
  double                             aligned   = 0;  // microns: spacing buildable with aligned edges (== any_phase unless lattice)
  double                             margin    = 0;  // microns per side: macro-grid halo plus via-stack clearance
  // lattice only, microns relative to the strap grid origin: the feed strap family's pitch and, within one pitch,
  // every group of consecutive straps that holds one strap of each net, as [low edge, high edge].
  double                             period    = 0;
  std::vector<std::array<double, 2>> groups;
};
struct Pg_macro_rules {
  std::array<Pg_gap_rule, 2> gap;  // [0]: gaps along x (macros side by side), [1]: gaps along y (stacked)
};
// Only hard macro masters among `masters` count (unknown names throw). A strategy naming layers the LEF routing
// stack lacks (or without a preferred direction) yields no rule.
Pg_macro_rules pg_macro_rules(const physical::Technology&, const physical::Pg_strategy&, const std::set<std::string>& masters);
// The strategy macro.pdn follows for synth.place.pg = `pg`: a named profile's power-grid strategy (nullptr when it
// has none); for none/off/auto the LEF-detected power-grid profile's (nullptr when none matches). Unknown names throw.
const physical::Pg_strategy* pg_macro_strategy(const physical::Technology&, std::string_view pg);

// Integer form of a lattice rule for one axis, used to align movable macros.
// A macro edge position e (DBU, along the axis) is allowed when the clear band
// on its low side, [max(core_lo, e - channel + margin), e - margin], wholly
// contains a strap group (a macro below keeps at least `channel` of spacing, so
// its band is at least that one), and, when the macro's high edge h is closer
// than `channel` to core_hi, [h + margin, core_hi] contains one as well. Bands
// of at most zero length (the macro within its margin of the core edge) have
// no rails to feed and pass.
struct Pg_edge_lattice {
  int64_t                             origin = 0, period = 0, channel = 0, margin = 0;
  std::vector<std::array<int64_t, 2>> groups;                              // relative to origin, low edge in [0, period)
  bool                                fits(int64_t lo, int64_t hi) const;  // a group lies inside [lo, hi]
  bool                                allows(int64_t low_edge, int64_t high_edge, int64_t core_lo, int64_t core_hi) const;
};
// Per-axis lattices for macro alignment; an axis without a lattice rule is unconstrained.
struct Pg_macro_alignment {
  std::array<std::optional<Pg_edge_lattice>, 2> axis;
  bool                                          active() const { return axis[0] || axis[1]; }
};
// `channel` is the resolved macro spacing per axis (DBU), `origin` the strap grid origin (DBU, normally the core
// lower-left corner). `tolerance` (DBU) widens the margin on each side so a downstream tool that re-snaps the
// coordinates (to its manufacturing grid, through floating-point offsets) cannot push an edge off the lattice.
Pg_macro_alignment pg_macro_alignment(const Pg_macro_rules&, std::array<int64_t, 2> channel, std::array<int64_t, 2> origin,
                                      int64_t dbu, int64_t tolerance = 0);
// macro.pdn levels, strongest first. gap: the any-phase spacing (buildable wherever the macros land). align: the
// aligned spacing plus a tenth of the feed strap pitch (a window of lattice-aligned edge positions; the placer also
// snaps edges onto the lattice). off: no power-grid requirement (only the metal-spacing channel). macro.pdn=auto
// starts at gap and degrades to align, then off, when the macros cannot be legalized with the stronger channel.
enum class Pg_macro_level { gap, align, off };
const char* pg_macro_level_name(Pg_macro_level);
// Spacing (microns) one axis needs at `level` (0 for off and for an axis without a rule).
double      pg_macro_requirement(const Pg_gap_rule&, Pg_macro_level);
// A macro footprint for the level feasibility check (DBU): the padded box a legalizer must keep disjoint.
struct Pg_macro_box {
  int64_t width = 0, height = 0;
};
// Necessary conditions for packing `boxes` without overlap in a core_w x core_h core, with no rotation: every box
// fits the core; the summed area fits; and per axis, since a line across the core crosses at most
// floor(extent / smallest box size) disjoint boxes, the summed box widths are at most
// core_w * floor(core_h / min height) (and symmetrically for heights). False proves no legal packing exists; true does
// not promise a legalizer will find one.
bool pg_macro_boxes_fit(const std::vector<Pg_macro_box>& boxes, int64_t core_w, int64_t core_h);
// pdngen's channel condition on a metal plan: core strap segments (not followpins, not macro-grid straps) that no
// same-net shape on a layer they connect up to (`connect`) overlaps. pdngen must repair each one (PDN-0178/0179 when
// it cannot); the native generator accepts them when rails tie them to the grid. Counts per layer.
struct Power_grid_plan;
std::map<std::string, uint64_t> pg_unfed_strap_segments(const Power_grid_plan&, const physical::Pg_strategy&);
}  // namespace livehd::placement
