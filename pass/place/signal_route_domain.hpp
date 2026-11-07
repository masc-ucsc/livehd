// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "inou/lefdef/routing_connectivity.hpp"

namespace livehd::placement {
// A private reservation belongs only to boxes in which this net has physical
// endpoint obligations. Transit-only nets receive carriers, never those boxes.
struct Signal_route_region {
  physical::Rect                              bounds;
  std::map<std::string, std::vector<int64_t>> tracks;
};
struct Signal_route_carrier {
  std::string     owner, component;
  // Trusted typed input: caller derives this only from independently admitted
  // immutable Local metal or original same-net IO metal, never pending detail.
  // component is provenance only, not an electrical equivalence relation.
  physical::Shape seed;
  physical::Rect  extension;  // finite collinear extent, clipped to the allowed box/join region
};
struct Signal_route_domain {
  std::string                       owner;
  std::string                       pin_layer;
  std::vector<std::string>          local_layers;
  std::vector<Signal_route_region>  regions;
  std::vector<Signal_route_carrier> carriers;
  // Bounded native staggered handoffs only. Intermediate wire must be the
  // minimal envelope of actual adjacent-layer via landings, with at most this
  // separation of via centers on each intermediate layer. Zero disables them.
  int64_t                           max_stack_bridge_dbu = 0;
};

// Conservative center window for a tap in a validated endpoint-owned domain.
// Every admitted new wire is inside this hull. Centered maze landing pads
// therefore cannot admit a state outside it. This is not a geometry clip:
// original pins, retained metal, native via plates and occupancy stay intact.
// Carrier-only global joins have no such local window.
std::optional<physical::Rect> signal_domain_tap_window(const Signal_route_domain&);

// Search screens, not acceptance certificates. Native DRC is always separate.
bool signal_domain_track(const Signal_route_domain&, const physical::Technology&, const std::string& layer, int64_t coordinate);
bool signal_domain_wire(const Signal_route_domain&, const physical::Technology&, const physical::Floorplan&, const physical::Shape&,
                        bool stack_candidate = false);

// Reconstruct finite wire/via geometry and verify resource/owner/seed coverage.
// Seed provenance is a caller obligation; this does not rescan the full design
// to establish which upstream producer owned an input carrier.
// retained paths are already accepted same-net context, not newly emitted metal.
// This never substitutes for native DRC or complete-net connectivity. Empty
// result means accepted; malformed/unsupported domains fail closed with a reason.
std::string validate_signal_route_domain(const physical::Technology&, const physical::Floorplan&, const physical::Net&,
                                         std::span<const physical::Route_path> proposed, const Signal_route_domain&,
                                         const physical::Route_width_rules*    trial_rules    = nullptr,
                                         const physical::Route_components*     components     = nullptr,
                                         std::span<const physical::Route_path> retained       = {},
                                         std::span<const physical::Route_path> extra_retained = {});
}  // namespace livehd::placement
