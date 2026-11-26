// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "inou/lefdef/physical.hpp"

namespace livehd::routing {
// What the track-interval router needs to know about a technology, read from the technology LEF.
//
// The router's model: every signal wire is a default-width interval on a track of a unidirectional layer, and every
// via is one default via at a crossing of two tracks. A layer then needs six lengths, all in DEF database units:
//   w       default wire width
//   e2e     gap two wire ends on one track keep (end-of-line and end-to-end spacing)
//   cs      along-track gap facing ends on neighbouring tracks keep (corner spacing); 0 when the tracks are far enough
//           apart for the ordinary spacing
//   minlen  shortest piece (minimum area over the width, or MINSIZE)
//   ext     how far a wire runs past the centre of a via (the via's metal along the wire, or half of MINSIZE)
//   wide    spacing kept from metal wider than a wire
// and one across the track:
//   bump    how far the widest default via pad sticks out beside a wire; 0 when the pads are as wide as the wire
struct Tech_layer {
  std::string name;
  bool        horiz   = false;
  int         w = 0, spacing = 0, e2e = 0, cs = 0, minlen = 0, ext = 0, wide = 0, bump = 0;
  int         pitch = 0;  // LEF pitch across the wires; the router replaces it with the DEF track pitch
  int64_t     min_area = 0;  // minimum area in squared database units, 0 when the LEF states none
  // Spacing kept from metal at least this wide (LEF width table), ascending; empty when the LEF has none.
  std::vector<std::pair<int, int>> width_spacing;
};
// The default via between two neighbouring layers. Rectangles are relative to the via origin, in DEF units.
struct Tech_cut {
  std::string       name, via;   // cut layer and LEF via name
  std::string       lower, upper;  // metal layers it joins
  int               hx = 0, hy = 0, space = 0;  // half sizes of the cut and the cut-to-cut spacing
  physical::Rect    lower_pad, upper_pad;       // via metal on the two layers
};
struct Tech_stack {
  std::string             pin_layer;  // the layer standard-cell pins are on; not routed
  std::vector<Tech_layer> layers;     // routed layers, bottom up
  std::vector<Tech_cut>   cuts;       // cuts[k] joins layers[k - 1] (the pin layer for k == 0) and layers[k]
  int                     grid = 1;   // manufacturing grid
  std::vector<std::string> unused;    // top layers left out because their via pad does not fit the tracks below
};
// Derives the stack from the LEF. `has_tracks(layer, horizontal)` says whether the frame has tracks for that layer in
// its preferred direction. The routed layers are those of the registered technology profile when the LEF matches one
// that states a routing policy, and otherwise every routing layer above the lowest one that has tracks. Throws
// std::runtime_error naming what is missing when the LEF cannot be served by the model.
Tech_stack derive_tech_stack(const physical::Technology& tech, int64_t dbu,
                             const std::function<bool(const std::string&, bool)>& has_tracks);
}  // namespace livehd::routing
