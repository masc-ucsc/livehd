// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/physical_rc.hpp"

namespace livehd::placement {
struct Rc_geometry {
  std::string                                                    driver;
  // Absolute physical pin positions in DBU, including orientation/pin offsets.
  std::vector<std::pair<std::string, std::pair<double, double>>> pins;
  // Electrical changes can dirty timing while retaining unchanged wire geometry.
  std::string                                                    load_identity;
};
struct Rc_policy {
  // Horizontal R,C and vertical R,C, in ohm/um and fF/um.
  std::array<double, 4> per_um{};
  double                dbu = 1;
  std::string           identity;
  // Caller revisions cover geometry not represented by pin positions (e.g.
  // macro/halo routing policy) and library/load policy changes respectively.
  uint64_t              geometry_generation = 0, timing_generation = 0;
};
struct Rc_reuse_stats {
  size_t nets_inspected = 0, pins_inspected = 0;
  size_t rebuilt = 0, reused = 0, timing_dirty = 0, removed = 0;
};
// Wire-only cache: pin capacitance is never stored in Rc_net. Every update
// inspects all submitted pins; only tree construction is incremental. This is
// not a sublinear graph walk or an incremental timing propagation engine.
class Rc_cache {
public:
  struct Built {
    physical::Rc_net rc;
    bool             approximate = false;
  };
  struct Entry {
    Rc_geometry                  geometry;
    Rc_policy                    policy;
    std::shared_ptr<const Built> built;
  };
  struct Snapshot {
    std::vector<std::shared_ptr<const Entry>> nets;  // submitted order
    std::vector<std::string>                  timing_dirty_drivers;
    Rc_reuse_stats                            stats;
  };
  using Builder = std::function<Built(const Rc_geometry&, const Rc_policy&)>;
  // A complete snapshot replaces membership. Failed validation/construction
  // leaves the previous generation intact, including nets removed in the trial.
  Snapshot update(std::vector<Rc_geometry> nets, const Rc_policy& policy, const Builder& build);
  size_t   size() const { return entries.size(); }

private:
  std::map<std::string, std::shared_ptr<const Entry>> entries;
};
}  // namespace livehd::placement
