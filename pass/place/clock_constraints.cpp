// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_constraints.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace livehd::placement::late {
namespace {
int64_t ticks(double value) {
  const double scaled = value * 1e6;
  if (!std::isfinite(value) || value < 0 || scaled > static_cast<double>(std::numeric_limits<int64_t>::max() / 2)) {
    throw std::invalid_argument("invalid ideal clock time");
  }
  const auto result = std::llround(scaled);
  if (std::abs(static_cast<double>(result) / 1e6 - value) > std::max(1e-9, value * 1e-14)) {
    throw std::invalid_argument("ideal clock time is not representable on the 1e-6 ps edge grid");
  }
  return result;
}
void validate(const Ideal_clock& clock) {
  if (ticks(clock.period_ps) <= 0 || clock.roots.empty() || ticks(clock.phase_ps) >= ticks(clock.period_ps)) {
    throw std::invalid_argument("ideal clock needs a positive period, roots and a phase within its period");
  }
  for (double value : {clock.uncertainty_ps, clock.slew_ps[0], clock.slew_ps[1]}) {
    if (!std::isfinite(value) || value < 0) {
      throw std::invalid_argument("invalid ideal clock uncertainty/slew");
    }
  }
}
}  // namespace

Clock_system::Clock_system(const std::optional<Ideal_clock>& legacy, const std::map<std::string, Ideal_clock>& named,
                           const std::map<std::string, Clock_alias>&            aliases,
                           const std::set<std::pair<std::string, std::string>>& async) {
  if (legacy && (!named.empty() || !aliases.empty() || !async.empty())) {
    throw std::invalid_argument("legacy clock and named-clock constraints cannot be combined");
  }
  const auto add = [&](const std::string& name, const Ideal_clock& model) {
    if (name.empty()) {
      throw std::invalid_argument("ideal clock name cannot be empty");
    }
    validate(model);
    identities.emplace(name, clocks.size());
    clocks.push_back({name, model});
  };
  if (legacy) {
    add("default", *legacy);
  }
  for (const auto& [name, clock] : named) {
    add(name, clock);
  }
  std::set<std::string>                     resolving;
  std::function<size_t(const std::string&)> resolve = [&](const std::string& name) {
    if (identities.contains(name)) {
      return identities.at(name);
    }
    const auto alias = aliases.find(name);
    if (alias == aliases.end() || !resolving.insert(name).second) {
      throw std::invalid_argument("unknown or cyclic ideal clock alias: " + name);
    }
    if (name.empty() || alias->second.roots.empty()) {
      throw std::invalid_argument("forwarded clock alias needs a name and roots");
    }
    const size_t source = resolve(alias->second.source);
    identities.emplace(name, source);
    for (const auto& root : alias->second.roots) {
      if (const auto old = alias_roots.find(root); old != alias_roots.end() && old->second != source) {
        throw std::invalid_argument("multiple clock waveforms at alias root: " + root);
      }
      alias_roots[root] = source;
    }
    resolving.erase(name);
    return source;
  };
  for (const auto& [name, alias] : aliases) {
    (void)alias;
    if (named.contains(name)) {
      throw std::invalid_argument("clock alias duplicates a primary clock name: " + name);
    }
    resolve(name);
  }
  for (const auto& [first, second] : async) {
    const size_t a = find(first), b = find(second);
    if (a == b) {
      throw std::invalid_argument("a clock and its waveform alias cannot be asynchronous");
    }
    asynchronous.emplace(std::min(a, b), std::max(a, b));
  }
}
size_t Clock_system::find(const std::string& name) const {
  const auto found = identities.find(name);
  if (found == identities.end()) {
    throw std::invalid_argument("unknown ideal clock: " + name);
  }
  return found->second;
}
std::optional<double> Clock_system::separation(size_t launch, size_t capture) const {
  const auto& from = clocks.at(launch).model;
  const auto& to   = clocks.at(capture).model;
  if (asynchronous.contains({std::min(launch, capture), std::max(launch, capture)})) {
    return {};
  }
  const auto interval   = std::gcd(ticks(from.period_ps), ticks(to.period_ps));
  auto       difference = (ticks(to.phase_ps) - ticks(from.phase_ps)) % interval;
  if (difference <= 0) {
    difference += interval;
  }
  return static_cast<double>(difference) / 1e6;
}
}  // namespace livehd::placement::late
