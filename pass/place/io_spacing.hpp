// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

namespace livehd::placement {
// One physical spacing policy for die pins and shared chunk boundary contracts.
struct Io_spread_policy {
  static constexpr unsigned wide_count = 64, tracks = 2, corner_tracks = 15;
  static constexpr double   share = .25, max_pitch_um = .5, max_corner_um = 1;
};
}  // namespace livehd::placement
