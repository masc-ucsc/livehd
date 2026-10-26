// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "star_rc.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace livehd::placement::late {
namespace {
using Pair = std::array<double, 2>;
void nonnegative(double value) {
  if (!std::isfinite(value) || value < 0) {
    throw std::invalid_argument("continuous star requires finite nonnegative RC/loads");
  }
}
void validate(const std::vector<Star_pin>& pins) {
  if (pins.size() < 2 || pins.front().load_ff != Pair{}) {
    throw std::invalid_argument("continuous star requires a driver and sinks; driver load must be zero");
  }
  std::set<std::string> names;
  for (const auto& pin : pins) {
    if (pin.name.empty() || !names.insert(pin.name).second || !std::isfinite(pin.x) || !std::isfinite(pin.y)) {
      throw std::invalid_argument("invalid continuous star pin identity/position");
    }
    for (double load : pin.load_ff) {
      nonnegative(load);
    }
  }
}
bool same_membership(const std::vector<Star_pin>& a, const std::vector<Star_pin>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].name != b[i].name) {
      return false;
    }
  }
  return true;
}
bool same_policy(const Rc_policy& a, const Rc_policy& b) {
  return a.per_um == b.per_um && a.dbu == b.dbu && a.identity == b.identity && a.geometry_generation == b.geometry_generation;
}
struct Raw {
  Rc_tree_state timing;
  double        wire_cap = 0;
};
Raw star(const std::vector<Star_pin>& pins, const Rc_policy& policy, double wire_ratio, double root_wire_cap) {
  physical::Rc_net net;
  net.driver = pins.front().name;
  std::vector<Pair> caps;
  for (const auto& pin : pins) {
    net.nodes.push_back({pin.name, 0});
    caps.push_back(pin.load_ff);
  }
  Raw result;
  caps[0]         = {root_wire_cap, root_wire_cap};
  result.wire_cap = root_wire_cap;
  for (unsigned i = 1; i < pins.size(); ++i) {
    const double dx         = std::abs(pins[i].x - pins[0].x) / policy.dbu;
    const double dy         = std::abs(pins[i].y - pins[0].y) / policy.dbu;
    const double resistance = dx * policy.per_um[0] + dy * policy.per_um[2];
    const double wire_cap   = wire_ratio * (dx * policy.per_um[1] + dy * policy.per_um[3]);
    nonnegative(resistance);
    nonnegative(wire_cap);
    net.edges.push_back({0, i, resistance});
    result.wire_cap += wire_cap;
    for (unsigned t = 0; t < 2; ++t) {
      caps[0][t] += wire_cap / 2;
      caps[i][t] += wire_cap / 2;
    }
  }
  result.timing = analyze_rc_tree(net, 0, caps);
  return result;
}
}  // namespace

struct Star_bank::Calibration {
  struct Geometry {
    Rc_policy             policy;
    std::vector<Star_pin> anchor;
    physical::Rc_net      reference;
    std::vector<size_t>   reference_nodes;
    double                wire_ratio = 1, root_wire_cap = 0;
    bool                  zero_wire_anchor = false;
  };
  std::shared_ptr<const Geometry> geometry;
  std::vector<Pair>               loads, scale, offset;
  uint64_t                        timing_generation = 0;
  std::array<Pi_model, 2>         reference_pi, anchor_pi;
  Pair                            reference_cap{};

  Calibration(const Star_net& net, const Rc_policy& policy) {
    if (!net.reference || net.reference->driver != net.pins.front().name) {
      throw std::invalid_argument("continuous star calibration requires matching wire-only reference RC");
    }
    auto data       = std::make_shared<Geometry>();
    data->policy    = policy;
    data->anchor    = net.pins;
    data->reference = *net.reference;
    physical::validate_rc_tree(data->reference);
    std::map<std::string, size_t> nodes;
    double                        reference_wire = 0;
    for (size_t i = 0; i < data->reference.nodes.size(); ++i) {
      const auto& node = data->reference.nodes[i];
      nonnegative(node.capacitance_ff);
      reference_wire += node.capacitance_ff;
      if (!node.pin.empty() && !nodes.emplace(node.pin, i).second) {
        throw std::invalid_argument("duplicate continuous star reference pin");
      }
    }
    if (nodes.size() != net.pins.size()) {
      throw std::invalid_argument("continuous star reference membership differs");
    }
    for (const auto& pin : net.pins) {
      if (!nodes.contains(pin.name)) {
        throw std::invalid_argument("continuous star reference omits pin: " + pin.name);
      }
      data->reference_nodes.push_back(nodes.at(pin.name));
    }
    const double raw_wire = star(net.pins, policy, 1, 0).wire_cap;
    if (raw_wire > 0) {
      data->wire_ratio = reference_wire / raw_wire;
      nonnegative(data->wire_ratio);
    } else {
      // A coincident-pin/routed-detour reference can have nonzero wire load.
      // Retain it as root wire capacitance; no zero-length division is used.
      data->root_wire_cap    = reference_wire;
      data->zero_wire_anchor = true;
    }
    geometry = std::move(data);
    bind_loads(net.pins, policy.timing_generation);
  }
  bool same_loads(const std::vector<Star_pin>& pins, uint64_t generation) const {
    if (generation != timing_generation) {
      return false;
    }
    for (size_t i = 0; i < pins.size(); ++i) {
      if (pins[i].load_ff != loads[i]) {
        return false;
      }
    }
    return true;
  }
  void bind_loads(const std::vector<Star_pin>& pins, uint64_t generation) {
    const auto& data   = *geometry;
    auto        anchor = data.anchor;
    loads.clear();
    for (size_t i = 0; i < pins.size(); ++i) {
      anchor[i].load_ff = pins[i].load_ff;
      loads.push_back(pins[i].load_ff);
    }
    std::vector<Pair> cap;
    for (const auto& node : data.reference.nodes) {
      cap.push_back({node.capacitance_ff, node.capacitance_ff});
    }
    for (size_t i = 1; i < pins.size(); ++i) {
      for (unsigned t = 0; t < 2; ++t) {
        cap[data.reference_nodes[i]][t] += loads[i][t];
      }
    }
    const auto reference = analyze_rc_tree(data.reference, data.reference_nodes[0], cap);
    const auto raw       = star(anchor, data.policy, data.wire_ratio, data.root_wire_cap);
    reference_pi         = reference.pi;
    reference_cap        = reference.total_cap_ff;
    anchor_pi            = raw.timing.pi;
    scale.assign(pins.size(), {1, 1});
    offset.assign(pins.size(), {});
    for (size_t i = 1; i < pins.size(); ++i) {
      for (unsigned t = 0; t < 2; ++t) {
        const double target = reference.elmore_ps[data.reference_nodes[i]][t], base = raw.timing.elmore_ps[i][t];
        if (base > 0) {
          scale[i][t] = target / base;
          nonnegative(scale[i][t]);
        } else {
          offset[i][t] = target;
        }
      }
    }
    timing_generation = generation;
  }
  double displacement(const std::vector<Star_pin>& pins, bool branch_relative) const {
    const auto& anchor = geometry->anchor;
    if (branch_relative) {
      const double driver_move = std::abs(pins[0].x - anchor[0].x) + std::abs(pins[0].y - anchor[0].y);
      double       fraction    = 0;
      for (size_t i = 1; i < pins.size(); ++i) {
        const double moved  = std::abs(pins[i].x - anchor[i].x) + std::abs(pins[i].y - anchor[i].y);
        const double length = std::abs(anchor[i].x - anchor[0].x) + std::abs(anchor[i].y - anchor[0].y);
        fraction            = std::max(fraction, std::max(moved, driver_move) / std::max(1., length));
      }
      nonnegative(fraction);
      return fraction;
    }
    double xl = anchor[0].x, xh = xl, yl = anchor[0].y, yh = yl, moved = 0;
    for (size_t i = 0; i < pins.size(); ++i) {
      xl    = std::min(xl, anchor[i].x);
      xh    = std::max(xh, anchor[i].x);
      yl    = std::min(yl, anchor[i].y);
      yh    = std::max(yh, anchor[i].y);
      moved = std::max(moved, std::abs(pins[i].x - anchor[i].x) + std::abs(pins[i].y - anchor[i].y));
    }
    const double fraction = moved / std::max(1., xh - xl + yh - yl);
    nonnegative(fraction);
    return fraction;
  }
  Star_state evaluate(const std::vector<Star_pin>& pins, const Star_options& options) const {
    const auto& data = *geometry;
    auto        raw  = star(pins, data.policy, data.wire_ratio, data.root_wire_cap);
    Star_state  result;
    result.wire_cap_ff    = raw.wire_cap;
    result.wire_cap_ratio = data.wire_ratio;
    result.timing         = std::move(raw.timing);
    if (options.calibrate_admittance) {
      bool at_anchor = true;
      for (size_t i = 0; i < pins.size(); ++i) {
        at_anchor &= pins[i].x == data.anchor[i].x && pins[i].y == data.anchor[i].y;
      }
      for (unsigned t = 0; t < 2; ++t) {
        auto&       pi     = result.timing.pi[t];
        const auto& base   = anchor_pi[t];
        const auto& target = reference_pi[t];
        if (at_anchor) {
          // Preserve the calibrated reference's exact rounding at the anchor.
          pi                            = target;
          result.timing.total_cap_ff[t] = reference_cap[t];
          continue;
        }
        const auto fraction = [](const Pi_model& model) {
          const double c = model.near_cap_ff + model.far_cap_ff;
          return c > 0 ? model.far_cap_ff / c : 0.;
        };
        const double x = fraction(pi), x0 = fraction(base), y0 = fraction(target);
        double       y = y0;
        if (x0 > 0 && x0 < 1 && y0 > 0 && y0 < 1) {
          // An odds-ratio correction is continuous and stays in the passive
          // interval, unlike unconstrained correction of admittance moments.
          const double a = x * y0 * (1 - x0), b = (1 - x) * (1 - y0) * x0;
          y = a / (a + b);
        } else if (x0 == y0) {
          y = x;  // shared degenerate limit retains the geometry's response
        }
        const double c    = result.timing.total_cap_ff[t];
        pi.far_cap_ff     = c * y;
        pi.near_cap_ff    = c - pi.far_cap_ff;
        pi.resistance_ohm = base.resistance_ohm > 0 ? pi.resistance_ohm * (target.resistance_ohm / base.resistance_ohm)
                                                    : pi.resistance_ohm + target.resistance_ohm;
        nonnegative(pi.near_cap_ff);
        nonnegative(pi.far_cap_ff);
        nonnegative(pi.resistance_ohm);
      }
    }
    for (size_t i = 0; i < pins.size(); ++i) {
      result.pins.push_back(pins[i].name);
      for (unsigned t = 0; t < 2; ++t) {
        result.timing.elmore_ps[i][t] = result.timing.elmore_ps[i][t] * scale[i][t] + offset[i][t];
        nonnegative(result.timing.elmore_ps[i][t]);
      }
    }
    return result;
  }
};

Star_bank::Snapshot Star_bank::evaluate(const std::vector<Star_net>& nets, const Rc_policy& policy, bool refresh,
                                        const Star_options& options) {
  if (options.branch_relative_displacement && !options.max_pin_displacement_fraction) {
    throw std::invalid_argument("branch-relative displacement requires a displacement limit");
  }
  if (options.max_pin_displacement_fraction
      && (!std::isfinite(*options.max_pin_displacement_fraction) || *options.max_pin_displacement_fraction <= 0)) {
    throw std::invalid_argument("continuous star displacement limit must be positive");
  }
  if (!std::isfinite(policy.dbu) || policy.dbu <= 0) {
    throw std::invalid_argument("continuous star requires positive DBU");
  }
  for (double value : policy.per_um) {
    nonnegative(value);
  }
  Snapshot                                                  result;
  std::map<std::string, std::shared_ptr<const Calibration>> next;
  for (const auto& net : nets) {
    validate(net.pins);
    const auto& driver = net.pins.front().name;
    if (next.contains(driver)) {
      throw std::invalid_argument("duplicate continuous star driver");
    }
    const auto                         found = entries.find(driver);
    std::shared_ptr<const Calibration> calibration;
    const bool                         compatible = found != entries.end() && same_policy(found->second->geometry->policy, policy)
                            && same_membership(found->second->geometry->anchor, net.pins);
    const double moved = compatible ? found->second->displacement(net.pins, options.branch_relative_displacement) : 0;
    result.stats.max_displacement_fraction = std::max(result.stats.max_displacement_fraction, moved);
    const bool displaced = options.max_pin_displacement_fraction && moved > *options.max_pin_displacement_fraction;
    if (!refresh && compatible && !displaced) {
      calibration = found->second;
      ++result.stats.reused;
      if (!calibration->same_loads(net.pins, policy.timing_generation)) {
        auto rebound = std::make_shared<Calibration>(*calibration);
        rebound->bind_loads(net.pins, policy.timing_generation);
        calibration = std::move(rebound);
        ++result.stats.load_rebound;
      }
    } else {
      calibration = std::make_shared<Calibration>(net, policy);
      ++result.stats.calibrated;
      result.stats.displacement_refreshed += !refresh && compatible && displaced;
    }
    result.stats.pins_evaluated    += net.pins.size();
    result.stats.zero_wire_anchors += calibration->geometry->zero_wire_anchor;
    result.nets.push_back(calibration->evaluate(net.pins, options));
    next.emplace(driver, std::move(calibration));
  }
  for (const auto& [driver, calibration] : entries) {
    (void)calibration;
    result.stats.removed += !next.contains(driver);
  }
  entries.swap(next);
  return result;
}
}  // namespace livehd::placement::late
