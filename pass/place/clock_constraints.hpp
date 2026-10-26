// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace livehd::placement::late {
struct Ideal_clock {
  double                period_ps = 0, uncertainty_ps = 0;
  std::array<double, 2> slew_ps{};
  std::set<std::string> roots;
  double                phase_ps = 0;
};
// Same-waveform forwarded clock. Its roots must be reached by the source's
// supported positive-buffer clock network; arbitrary dividers/gates are not
// inferred from this declaration.
struct Clock_alias {
  std::string           source;
  std::set<std::string> roots;
};
class Clock_system {
public:
  struct Clock {
    std::string name;
    Ideal_clock model;
  };
  std::vector<Clock>                  clocks;
  std::map<std::string, size_t>       identities, alias_roots;
  std::set<std::pair<size_t, size_t>> asynchronous;
  Clock_system(const std::optional<Ideal_clock>& legacy, const std::map<std::string, Ideal_clock>& named,
               const std::map<std::string, Clock_alias>& aliases, const std::set<std::pair<std::string, std::string>>& async);
  size_t                find(const std::string& name) const;
  // Smallest strictly positive ideal launch-to-capture edge separation for
  // commensurate periods/phases. Asynchronous pairs have no setup check.
  // Time-grid precision is 1e-6 ps; unrepresentable inputs are rejected.
  std::optional<double> separation(size_t launch, size_t capture) const;
};
}  // namespace livehd::placement::late
