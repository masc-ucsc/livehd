// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "rc_cache.hpp"

#include <cmath>
#include <set>
#include <stdexcept>

namespace livehd::placement {
Rc_cache::Snapshot Rc_cache::update(std::vector<Rc_geometry> nets, const Rc_policy& policy, const Builder& build) {
  if (!build || !std::isfinite(policy.dbu) || policy.dbu <= 0) {
    throw std::invalid_argument("invalid RC cache builder or DBU");
  }
  for (double value : policy.per_um) {
    if (!std::isfinite(value) || value < 0) {
      throw std::invalid_argument("invalid RC cache wire policy");
    }
  }
  // Validate the complete frame before invoking an expensive tree builder.
  std::set<std::string> drivers;
  for (const auto& net : nets) {
    if (net.driver.empty() || net.pins.size() < 2 || !drivers.insert(net.driver).second) {
      throw std::invalid_argument("invalid or duplicate RC cache driver");
    }
    std::set<std::string> pins;
    for (const auto& [name, xy] : net.pins) {
      if (name.empty() || !pins.insert(name).second || !std::isfinite(xy.first) || !std::isfinite(xy.second)) {
        throw std::invalid_argument("invalid or duplicate RC cache pin");
      }
    }
    if (!pins.contains(net.driver)) {
      throw std::invalid_argument("RC cache driver is absent from pins");
    }
  }
  Snapshot                                            result;
  std::map<std::string, std::shared_ptr<const Entry>> next;
  for (auto& net : nets) {
    ++result.stats.nets_inspected;
    result.stats.pins_inspected += net.pins.size();
    const auto old               = entries.find(net.driver);
    const bool same_geometry     = old != entries.end() && old->second->geometry.pins == net.pins
                               && old->second->policy.per_um == policy.per_um && old->second->policy.dbu == policy.dbu
                               && old->second->policy.identity == policy.identity
                               && old->second->policy.geometry_generation == policy.geometry_generation;
    const bool same_load = old != entries.end() && old->second->geometry.load_identity == net.load_identity
                           && old->second->policy.timing_generation == policy.timing_generation;
    std::shared_ptr<const Entry> entry;
    if (same_geometry && same_load) {
      entry = old->second;
      ++result.stats.reused;
    } else {
      // A load-only revision shares the immutable wire tree without rerunning
      // its builder. Entry ownership keeps older snapshots valid for rollback.
      auto fresh = std::make_shared<Entry>();
      if (same_geometry) {
        fresh->built = old->second->built;
        ++result.stats.reused;
      } else {
        fresh->built = std::make_shared<const Built>(build(net, policy));
        if (fresh->built->rc.driver != net.driver) {
          throw std::invalid_argument("RC cache builder changed driver");
        }
        if (!std::isfinite(fresh->built->rc.wire_length_um) || fresh->built->rc.wire_length_um < 0) {
          throw std::invalid_argument("invalid cached wire length");
        }
        for (const auto& node : fresh->built->rc.nodes) {
          if (!std::isfinite(node.capacitance_ff) || node.capacitance_ff < 0) {
            throw std::invalid_argument("invalid cached wire capacitance");
          }
        }
        for (const auto& edge : fresh->built->rc.edges) {
          if (!std::isfinite(edge.resistance_ohm) || edge.resistance_ohm < 0) {
            throw std::invalid_argument("invalid cached wire resistance");
          }
        }
        physical::validate_rc_tree(fresh->built->rc);
        std::set<std::string> actual;
        for (const auto& node : fresh->built->rc.nodes) {
          if (!node.pin.empty() && !actual.insert(node.pin).second) {
            throw std::invalid_argument("RC cache builder duplicated a pin");
          }
        }
        std::set<std::string> expected;
        for (const auto& pin : net.pins) {
          expected.insert(pin.first);
        }
        if (actual != expected) {
          throw std::invalid_argument("RC cache builder changed pin membership");
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
