// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "steiner_net.hpp"

namespace livehd::placement {
using Rc_pin_id = uint32_t;

// Dense physical-pin IDs belong to one flattened connectivity layout. The
// caller computes its content identity while binding the design, before RC.
// A rebuilt layout gets a new owner even when it reuses the same ordinals.
// There are no pin names or graph-name lookups inside RC.
struct Rc_layout {
  size_t   pin_count = 0;
  uint64_t identity  = 0;
};

struct Rc_geometry {
  Rc_pin_id                                                    driver = 0;
  // Absolute physical pin positions in DBU, including orientation/pin offsets.
  std::vector<std::pair<Rc_pin_id, std::pair<double, double>>> pins;
  // Electrical changes can dirty timing while retaining unchanged wire geometry.
  uint64_t                                                     load_identity = 0;
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
// Wire-only cache: cell pin capacitance is never stored in the tree. Every update
// inspects all submitted pins; only tree construction is incremental. This is
// not a sublinear graph walk or an incremental timing propagation engine.
class Rc_cache {
public:
  // Tree nodes start with geometry.pins in submission order, then junctions.
  // The builder cannot rename/rebind pins: it receives and returns only numbers.
  struct Built {
    Steiner_net rc;
    bool        approximate = false;
  };
  struct Entry {
    std::shared_ptr<const Rc_layout> layout;
    Rc_geometry                      geometry;
    Rc_policy                        policy;
    std::shared_ptr<const Built>     built;
  };
  struct Snapshot {
    std::vector<std::shared_ptr<const Entry>> nets;  // submitted order
    std::vector<Rc_pin_id>                    timing_dirty_drivers;
    Rc_reuse_stats                            stats;
  };
  using Builder = std::function<Built(const Rc_geometry&, const Rc_policy&)>;
  // A complete snapshot replaces membership. Failed validation/construction
  // leaves the previous generation intact, including nets removed in the trial.
  Snapshot update(std::shared_ptr<const Rc_layout> layout, std::vector<Rc_geometry> nets, const Rc_policy& policy,
                  const Builder& build);
  size_t   size() const { return entries.size(); }

private:
  std::unordered_map<Rc_pin_id, std::shared_ptr<const Entry>> entries;
};
}  // namespace livehd::placement
