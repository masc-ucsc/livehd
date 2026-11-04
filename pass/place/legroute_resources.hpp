// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace livehd::placement::lr {
// These are the exclusions used by the existing coarse capacity calculation,
// not a detailed metal/spacing/via legality certificate. In particular, a
// perpendicular obstacle can block part of a track without removing its coarse
// capacity; its geometry remains in Grid::obstacles.
enum class Track_exclusion : uint8_t {
  generic_reservation = 1,
  power_metal         = 2,
  region_obstacle     = 4,
  macro_footprint     = 8,
  blocked_tile        = 16,
};
struct Track_resource_exclusion {
  size_t  track   = 0;  // index into this tile's band's physical coordinate array
  uint8_t reasons = 0;
};
struct Track_resource_tile {
  size_t                                band = 0;
  std::vector<Track_resource_exclusion> exclusions;
  uint8_t                               whole_band_exclusions = 0;
  // Legacy M2 reserves a count after PG filtering, not particular coordinates.
  // Never turn this headroom into invented excluded tracks or group identities.
  unsigned                              anonymous_headroom    = 0;
  // The cellbox budget (Legroute_options::box_reserve): a count of usable tracks kept for Final's in-box routing,
  // after PG and obstacles; Final picks the actual tracks. No coordinates are invented here either.
  unsigned                              cellbox_budget        = 0;
  int                                   coarse_capacity       = 0;
};
struct Track_resource_layer {
  std::string                       name;
  bool                              horizontal = true;
  int                               grid_layer = -1;  // -1 is the separate box layer (M2 in ASAP7)
  std::vector<std::vector<int64_t>> bands;            // sorted unique actual track coordinates in DEF DBU
  std::vector<Track_resource_tile>  tiles;

  void exclude(size_t tile, size_t track, Track_exclusion);
  void exclude_all(size_t tile, Track_exclusion);
  // Number of represented track slots left by these coarse exclusions and
  // anonymous headroom. The latter does not identify which track is unavailable.
  int  represented_capacity(size_t tile) const;
};
struct Track_resource_catalog {
  // Upper layers retain Grid::layers order; the separate box layer is last.
  std::vector<Track_resource_layer> layers;
  // The pre-SAT clock allowance is directional, anonymous headroom. Actual
  // clock layers/spans/tracks are chosen later; these arrays do not invent them.
  std::vector<unsigned>             anonymous_clock_h, anonymous_clock_v;

  // Canonicalize duplicate exclusions (preserving all reasons) and verify that
  // every recorded capacity exactly matches the unchanged coarse calculation.
  void finalize();
};
}  // namespace livehd::placement::lr
