// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Pg_stripe {
  std::string layer;
  double      width = 0, spacing = 0, pitch = 0, offset = 0;  // microns
};
struct Pg_channel {
  std::string    layer, style;  // "skip" removes a complete slot; "gap" cuts its extent.
  physical::Rect region;        // microns, including the complete stripe width
};
// Semicolon-separated style,layer,xl,yl,xh,yh entries, in microns.
// Empty text disables channels; malformed/empty entries fail explicitly.
std::vector<Pg_channel> parse_pg_channels(std::string_view);
struct Pg_profile {
  std::string                             name, reference_revision, reference_file;
  std::vector<std::string>                followpin_layers;
  std::vector<Pg_stripe>                  stripes;
  std::vector<std::array<std::string, 2>> connect;
  std::string                             pin_layer;
  double                                  macro_halo = 0;
  std::vector<std::array<std::string, 2>> macro_connect;
  std::array<std::string, 2>              nets;  // power, ground
  std::array<std::vector<std::string>, 2> pin_names;
  std::vector<Pg_channel>                 channels;  // Empty in reference profiles.
  bool                                    stripes_start_with_power = false;
  std::vector<std::string>                unsupported;
  // Acceptance policy only: geometry generation does not perform IR analysis.
  std::optional<double>                   channel_max_ir_drop_v;
};
// Explicit names or auto detection. Unknown platforms fail; no approximate grid.
Pg_profile power_grid_profile(std::string_view name, const physical::Technology&);
struct Pg_metal {
  std::string    net, layer;
  physical::Rect rect;  // integer floorplan DBU
  bool           followpin = false;
};
struct Power_grid_plan {
  physical::Rect                 core;
  std::vector<Pg_metal>          metal;
  std::map<std::string, int64_t> rail_count, stripe_count;
  std::map<std::string, double>  channel_removed_length_um;
};
// Geometry planning stage of the native generator. Rails merge at shared row
// boundaries; stripes are clipped on macro-obstructed layers around their halos. No vias, trimming, pins,
// or connectivity verdict yet: this plan must NOT be exported as a complete PDN.
Power_grid_plan                 plan_power_grid(const physical::Technology&, const physical::Floorplan&, const Pg_profile&);
// Experimental PG export including fixed macro pin contacts. Constructs via stacks,
// trims stripes to their connections and creates PG terminals. Independent
// OpenROAD connectivity/DRC/IR validation is still required.
// Conservative lower bound: distinct preferred-track centerlines gaining a
// legal ordinary-width corridor across a channel extent, inset by the maximum
// layer clearance at both ends. Includes all fixed occupancy; not via access.
std::map<std::string, uint64_t> count_channel_tracks(const physical::Technology&, const physical::Floorplan& reference,
                                                     const physical::Floorplan& patched, const std::vector<Pg_channel>&);
// Derive whole-slot skips for planned flexible stripes that conflict with
// existing signal wiring. Followpin rails remain immutable. This is a proposal,
// not a PG validity verdict: pass the returned profile to make_power_grid(),
// which must still prove attachment/connectivity and signal clearance.
Pg_profile reserve_power_grid_channels(const physical::Technology&, const physical::Floorplan&, const Pg_profile&);
struct Pg_channel_stats {
  std::map<std::string, double>                  removed_planned_length_um;
  // Keys are lower:upper routing-layer pairs. A stack is identified by net,
  // layer pair and origin; moved stacks count as one removed and one added.
  std::map<std::string, uint64_t>                removed_stacks, added_stacks;
  std::optional<std::map<std::string, uint64_t>> freed_tracks;
  std::string                                    track_count_reason;
};
// Optional accounting compares successfully generated patched and reference grids
// on the same input. Published only on success; no reference rebuild when disabled.
// preserve_signal_terminals checks existing IOs without moving them; enable
// when regenerating PG after final IO legalization. Initial PG precedes IO legalization.
physical::Floorplan make_power_grid(const physical::Technology&, const physical::Floorplan&, const Pg_profile&,
                                    Pg_channel_stats* = nullptr, bool preserve_signal_terminals = false);
void                write_power_grid_report(const physical::Floorplan&, const Pg_profile&, const std::string& output_path,
                                            const Pg_channel_stats* = nullptr);
}  // namespace livehd::placement
