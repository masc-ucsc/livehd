// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "rc_cache.hpp"

#include <cmath>
#include <stdexcept>

namespace livehd::placement {
Rc_cache::Snapshot Rc_cache::update(std::shared_ptr<const Rc_layout> layout, std::vector<Rc_geometry> nets, const Rc_policy& policy,
                                    const Builder& build) {
  if (!layout || !build || !std::isfinite(policy.dbu) || policy.dbu <= 0) {
    throw std::invalid_argument("invalid RC cache builder or DBU");
  }
  for (double value : policy.per_um) {
    if (!std::isfinite(value) || value < 0) {
      throw std::invalid_argument("invalid RC cache wire policy");
    }
  }
  // Validate the complete frame before invoking an expensive tree builder.
  std::vector<bool>   drivers(layout->pin_count, false);
  std::vector<size_t> pin_generation(layout->pin_count, 0);
  size_t              generation = 0;
  for (const auto& net : nets) {
    ++generation;
    if (net.driver >= layout->pin_count || net.pins.size() < 2 || drivers[net.driver]) {
      throw std::invalid_argument("invalid or duplicate RC cache driver");
    }
    drivers[net.driver] = true;
    for (const auto& [id, xy] : net.pins) {
      if (id >= layout->pin_count || pin_generation[id] == generation || !std::isfinite(xy.first) || !std::isfinite(xy.second)) {
        throw std::invalid_argument("invalid or duplicate RC cache pin");
      }
      pin_generation[id] = generation;
    }
    if (pin_generation[net.driver] != generation) {
      throw std::invalid_argument("RC cache driver is absent from pins");
    }
  }
  Snapshot                                                    result;
  std::unordered_map<Rc_pin_id, std::shared_ptr<const Entry>> next;
  for (auto& net : nets) {
    ++result.stats.nets_inspected;
    result.stats.pins_inspected += net.pins.size();
    const auto old               = entries.find(net.driver);
    const bool same_geometry     = old != entries.end() && old->second->layout == layout && old->second->geometry.pins == net.pins
                                   && old->second->policy.per_um == policy.per_um && old->second->policy.dbu == policy.dbu
                                   && old->second->policy.identity == policy.identity
                                   && old->second->policy.geometry_generation == policy.geometry_generation;
    const bool same_load         = old != entries.end() && old->second->geometry.load_identity == net.load_identity
                                   && old->second->policy.timing_generation == policy.timing_generation;
    std::shared_ptr<const Entry> entry;
    if (same_geometry && same_load) {
      entry = old->second;
      ++result.stats.reused;
    } else {
      // A load-only revision shares the immutable wire tree without rerunning
      // its builder. Entry ownership keeps older snapshots valid for rollback.
      auto fresh    = std::make_shared<Entry>();
      fresh->layout = layout;
      if (same_geometry) {
        fresh->built = old->second->built;
        ++result.stats.reused;
      } else {
        fresh->built     = std::make_shared<const Built>(build(net, policy));
        const auto& tree = fresh->built->rc;
        if (!std::isfinite(tree.wire_length_um) || tree.wire_length_um < 0 || tree.node_cap_ff.size() < net.pins.size()
            || tree.edges.size() + 1 != tree.node_cap_ff.size()) {
          throw std::invalid_argument("invalid cached RC tree size or wire length");
        }
        std::vector<size_t> roots(tree.node_cap_ff.size());
        for (size_t i = 0; i < roots.size(); ++i) {
          roots[i] = i;
          if (!std::isfinite(tree.node_cap_ff[i]) || tree.node_cap_ff[i] < 0) {
            throw std::invalid_argument("invalid cached wire capacitance");
          }
        }
        const auto root = [&](size_t i) {
          while (roots[i] != i) {
            roots[i] = roots[roots[i]];
            i        = roots[i];
          }
          return i;
        };
        for (const auto& edge : tree.edges) {
          if (edge.from >= roots.size() || edge.to >= roots.size() || !std::isfinite(edge.resistance_ohm)
              || edge.resistance_ohm < 0) {
            throw std::invalid_argument("invalid cached wire edge");
          }
          const auto from = root(edge.from), to = root(edge.to);
          if (from == to) {
            throw std::invalid_argument("cycle in cached RC tree");
          }
          roots[from] = to;
        }
        ++result.stats.rebuilt;
      }
      fresh->geometry = std::move(net);
      fresh->policy   = policy;
      entry           = std::move(fresh);
      ++result.stats.timing_dirty;
      result.timing_dirty_drivers.push_back(entry->geometry.driver);
    }
    next.emplace(entry->geometry.driver, entry);
    result.nets.push_back(std::move(entry));
  }
  for (const auto& [driver, entry] : entries) {
    (void)entry;
    result.stats.removed += !next.contains(driver);
  }
  entries.swap(next);
  return result;
}
}  // namespace livehd::placement
