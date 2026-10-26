// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gap_forces.hpp"

#include <cmath>

#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
using detail::require;
}  // namespace
Gap_force_result bind_gap_forces(const DreamPlace::native::Design& design, const Gap_force_payload& base,
                                 const std::map<std::string, int>& drivers, const std::vector<Gap_entry_pull>& pulls, double units,
                                 double maximum_fraction) {
  require(std::isfinite(units) && units > 0 && std::isfinite(maximum_fraction) && maximum_fraction >= 0 && maximum_fraction <= 1,
          "invalid gap force units/bound");
  Gap_force_result result;
  result.timing = base;
#if DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
  result.supported_api = true;
  result.timing.gap_pulls.clear();
  result.timing.maximum_gap_fraction = maximum_fraction;
  auto checked                       = design;
  checked.pin_timing                 = result.timing;
  checked.validate();
  std::map<int, std::vector<size_t>> sinks;
  for (size_t p = 0; p < base.weight.size(); ++p) {
    if (base.weight[p] > 0) {
      sinks[base.driver[p]].push_back(p);
    }
  }
  const auto position = [&](size_t pin) {
    const size_t node = design.pin2node[pin];
    return Point{design.node_x[node] + design.pin_offset_x[pin], design.node_y[node] + design.pin_offset_y[pin]};
  };
  std::set<size_t> suppressed;
  for (const auto& pull : pulls) {
    require(!pull.owner.empty() && std::isfinite(pull.target.x) && std::isfinite(pull.target.y) && std::isfinite(pull.fraction)
                && pull.fraction > 0
                && (pull.priority == Net_class::critical || pull.priority == Net_class::window
                    || pull.priority == Net_class::outside_window),
            "invalid physical gap pull");
    if (pull.driver_stage) {
      ++result.virtual_driver;
      continue;
    }
    const auto binding = drivers.find(pull.owner);
    if (binding == drivers.end()) {
      ++result.unbound_driver;
      continue;
    }
    require(binding->second >= 0 && size_t(binding->second) < design.pin2node.size(), "invalid physical gap driver binding");
    const size_t driver = binding->second;
    if (!sinks.contains(driver) || base.coefficient == 0) {
      ++result.no_active_force;
      continue;
    }
    if (design.pin2node[driver] >= design.num_movable) {
      ++result.fixed_driver;
      continue;
    }
    DreamPlace::native::PinGapPull target;
    target.driver   = driver;
    target.x        = pull.target.x * units;
    target.y        = pull.target.y * units;
    target.fraction = pull.fraction;
    target.critical = pull.priority != Net_class::outside_window;
    require(std::isfinite(target.x) && std::isfinite(target.y), "gap target coordinate overflow");
    if (target.critical) {
      const auto origin = position(driver);
      for (size_t p : sinks.at(driver)) {
        if (suppressed.contains(p)) {
          continue;
        }
        const auto        sink = position(p);
        const long double dot  = (static_cast<long double>(sink.x) - origin.x) * (static_cast<long double>(target.x) - origin.x)
                                + (static_cast<long double>(sink.y) - origin.y) * (static_cast<long double>(target.y) - origin.y);
        require(std::isfinite(dot), "gap conflict projection overflow");
        if (dot < 0) {
          target.suppressed_sinks.push_back(p);
          suppressed.insert(p);
        }
      }
    }
    result.timing.gap_pulls.push_back(std::move(target));
    ++result.applied;
  }
  result.suppressed_pairs = suppressed.size();
  checked.pin_timing      = result.timing;
  checked.validate();
#else
  (void)design;
  (void)drivers;
  (void)pulls;
#endif
  return result;
}
}  // namespace livehd::placement::buffering
