// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "legroute_resources.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace livehd::placement::lr {
void Track_resource_layer::exclude(size_t tile, size_t track, Track_exclusion reason) {
  tiles.at(tile).exclusions.push_back({track, static_cast<uint8_t>(reason)});
}

void Track_resource_layer::exclude_all(size_t tile, Track_exclusion reason) {
  tiles.at(tile).whole_band_exclusions |= static_cast<uint8_t>(reason);
}

int Track_resource_layer::represented_capacity(size_t tile) const {
  const auto& resource = tiles.at(tile);
  if (resource.whole_band_exclusions) {
    return 0;
  }
  const auto count = bands.at(resource.band).size();
  // finalize() validates unique, in-range exclusions before this calculation.
  if (resource.exclusions.size() > count) {
    throw std::logic_error("legroute resource catalog has too many track exclusions");
  }
  const auto remaining = count - resource.exclusions.size();
  const auto kept      = size_t(resource.anonymous_headroom) + resource.cellbox_budget;
  return remaining <= kept ? 0 : int(remaining - kept);
}

void Track_resource_catalog::finalize() {
  if (anonymous_clock_h.size() != anonymous_clock_v.size()) {
    throw std::logic_error("legroute resource catalog has inconsistent clock headroom arrays");
  }
  for (auto& layer : layers) {
    if (layer.tiles.size() != anonymous_clock_h.size()) {
      throw std::logic_error("legroute resource catalog has inconsistent tile arrays");
    }
    for (const auto& band : layer.bands) {
      if (band.size() > size_t(std::numeric_limits<int>::max()) || !std::ranges::is_sorted(band)
          || std::adjacent_find(band.begin(), band.end()) != band.end()) {
        throw std::logic_error("legroute resource catalog has invalid physical track coordinates");
      }
    }
    for (size_t t = 0; t < layer.tiles.size(); ++t) {
      auto&      resource = layer.tiles[t];
      const auto count    = layer.bands.at(resource.band).size();
      std::ranges::sort(resource.exclusions, {}, &Track_resource_exclusion::track);
      size_t kept = 0;
      for (const auto exclusion : resource.exclusions) {
        if (exclusion.track >= count || !exclusion.reasons) {
          throw std::logic_error("legroute resource catalog has an invalid track exclusion");
        }
        if (kept && resource.exclusions[kept - 1].track == exclusion.track) {
          resource.exclusions[kept - 1].reasons |= exclusion.reasons;
        } else {
          resource.exclusions[kept++] = exclusion;
        }
      }
      resource.exclusions.resize(kept);
      if (resource.coarse_capacity != layer.represented_capacity(t)) {
        throw std::logic_error("legroute resource identities disagree with coarse capacity on layer " + layer.name);
      }
    }
  }
}
}  // namespace livehd::placement::lr
