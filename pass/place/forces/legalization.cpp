// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "legalization.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "pass/place/require.hpp"

namespace livehd::placement::timeclosure {
namespace {
void costs(const std::vector<double>& values, size_t count, double minimum) {
  require(values.empty() || values.size() == count, "legalization priority cost count differs from physical binding");
  for (double value : values) {
    require(std::isfinite(value) && value >= minimum, "invalid legalization priority cost");
  }
}
}  // namespace
void Legalization_strength::validate() const {
  require(std::isfinite(displacement) && displacement >= 0 && displacement <= 99 && std::isfinite(net) && net >= 0 && net <= 99,
          "native legalization strengths must be finite and in [0,99]");
}
Legalization_priorities legalization_priorities(const Legalization_binding& binding, const Pin_weights& timing,
                                                Legalization_strength strength) {
  strength.validate();
  require(binding.movable <= binding.nodes, "invalid legalization movable node count");
  costs(binding.node_weights, binding.nodes, 1);
  costs(binding.net_weights, binding.drivers.size(), 0);
  std::map<std::string, size_t> identities;
  for (size_t i = 0; i < binding.pins.size(); ++i) {
    const auto& pin = binding.pins[i];
    require(
        !pin.name.empty() && pin.node < binding.nodes && pin.net < binding.drivers.size() && identities.emplace(pin.name, i).second,
        "invalid or duplicate legalization physical pin binding");
  }
  for (size_t net = 0; net < binding.drivers.size(); ++net) {
    require(binding.drivers[net] < binding.pins.size() && binding.pins[binding.drivers[net]].net == net,
            "invalid legalization physical net driver");
  }
  double maximum = 0;
  for (const auto& [name, weight] : timing.pins) {
    const auto pin = identities.find(name), driver = identities.find(weight.driver);
    require(pin != identities.end() && driver != identities.end(), "timing pin absent from legalization physical binding");
    const auto net = binding.pins[pin->second].net;
    require(binding.pins[driver->second].net == net && binding.drivers[net] == driver->second && pin->second != driver->second,
            "timing driver differs from legalization physical net");
    require(std::isfinite(weight.raw) && weight.raw >= 0, "invalid native raw legalization priority");
    maximum = std::max(maximum, weight.raw);
  }
  Legalization_priorities result;
  result.node_weights = binding.node_weights;
  result.net_weights  = binding.net_weights;
  result.node_criticality.assign(binding.nodes, 0);
  result.net_criticality.assign(binding.drivers.size(), 0);
  for (const auto& [name, weight] : timing.pins) {
    if (weight.raw == 0) {
      continue;
    }
    const auto&  pin      = binding.pins[identities.at(name)];
    const auto&  driver   = binding.pins[identities.at(weight.driver)];
    const double priority = weight.raw / maximum;
    if (pin.node < binding.movable) {
      result.node_criticality[pin.node] = std::max(result.node_criticality[pin.node], priority);
    }
    if (driver.node < binding.movable) {
      result.node_criticality[driver.node] = std::max(result.node_criticality[driver.node], priority);
    }
    result.net_criticality[pin.net] = std::max(result.net_criticality[pin.net], priority);
    ++result.weighted_pins;
  }
  if (strength.displacement > 0) {
    if (result.node_weights.empty()) {
      result.node_weights.assign(binding.nodes, 1);
    }
    for (size_t i = 0; i < binding.movable; ++i) {
      result.node_weights[i]   *= 1 + strength.displacement * result.node_criticality[i];
      result.protected_movable += result.node_criticality[i] > 0;
    }
  }
  if (strength.net > 0) {
    if (result.net_weights.empty()) {
      result.net_weights.assign(binding.drivers.size(), 1);
    }
    for (size_t i = 0; i < result.net_weights.size(); ++i) {
      result.net_weights[i] *= 1 + strength.net * result.net_criticality[i];
      result.protected_nets += result.net_criticality[i] > 0 && result.net_weights[i] > 0;
    }
  }
  costs(result.node_weights, binding.nodes, 1);
  costs(result.net_weights, binding.drivers.size(), 0);
  return result;
}
}  // namespace livehd::placement::timeclosure
