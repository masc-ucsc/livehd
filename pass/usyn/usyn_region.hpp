// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>

#include "lnet.hpp"
#include "region_backend.hpp"
#include "unate.hpp"

namespace livehd::usyn {
// One region through unate synthesis (pass.usyn's region hook): the region's
// RAW Lnet rebuilt STRASH (livehd::synth::strash), covered with domino gates
// and static LUTs (lut_cover), and handed to the backend as the cover network
// (cover_network) -- technology mapping only (tmap) or the backend's
// optimize-and-map flow (opt). `only`, an exhausted budget and a refused cover
// leave the region to the backend's own flow. `report` receives the region's
// JSON row (the region-cache evidence).
livehd::synth::Region_rewrite rewrite_region(const livehd::synth::Lnet& net, const livehd::synth::Region_ctx& ctx,
                                             const Search_options& search, std::string& report);
}  // namespace livehd::usyn
