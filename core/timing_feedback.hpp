// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace livehd::physical {
struct Parasitics;
}
namespace livehd::timing {
// In-process physical timing feedback. Reports are optional consumers of this
// data, never the transport between timing and placement/repair.
struct Feedback {
  // GP transports only path ordering, in a dense array. The immutable layout
  // is shared across geometry updates; only a topology change rebuilds it.
  struct Ranking {
    struct Pin {
      double slack = std::numeric_limits<double>::infinity();
      bool   clock = false;
    };
    struct Layout {
      std::vector<std::string> labels;  // sorted, unique physical pin identities
      std::vector<size_t>      net_offsets, net_pins;
      size_t                   find(std::string_view name) const {
        const auto it = std::lower_bound(labels.begin(), labels.end(), name);
        return it != labels.end() && *it == name ? size_t(it - labels.begin()) : labels.size();
      }
    };
    std::shared_ptr<const Layout>               layout;
    // Own the exact immutable RC snapshot: a reordered/different netlist must
    // use label lookup rather than interpreting another snapshot's net indices.
    std::shared_ptr<const physical::Parasitics> parasitics;
    std::vector<Pin>                            pins;
    const Pin*                                  find(std::string_view name) const {
      const auto index = layout->find(name);
      return index < pins.size() ? &pins[index] : nullptr;
    }
  };
  struct Pin {
    std::optional<double> arrival, required, slew, load_ff, slack, capacitance_ff;
    unsigned              violations        = 0;
    double                electrical_excess = 0;
    bool                  clock             = false;
    std::optional<double> hold_slack{};  // MIN-corner slack, in time_unit_ps units
  };
  double                     time_unit_ps = 1000;
  std::map<std::string, Pin> pins;
  double                     delay = 0, area = 0, electrical_excess = 0;
  unsigned                   violations              = 0;
  bool                       electrical_pin_feedback = false;
  std::string                source_report;  // optional external-report provenance only
  std::optional<double>      wns, tns, hold_wns;
  std::vector<std::string>   path;
  // Approximate path ordering only; no sign-off WNS, electrical or pin-time
  // breakdown is present. Consumers may use pin slacks to rank net weights.
  bool                       ranking_only = false;
  std::optional<Ranking>     ranking;
  Ranking::Pin               pin_rank(const std::string& instance, const std::string& pin) const {
    if (ranking) {
      auto* value = ranking->find(instance == "PIN" ? pin : instance + ":" + pin);
      if (!value && instance != "PIN") {
        value = ranking->find(instance + "/" + pin);
      }
      return value ? *value : Ranking::Pin{};
    }
    const auto* value = find_pin(instance, pin);
    return value ? Ranking::Pin{value->slack.value_or(std::numeric_limits<double>::infinity()), value->clock} : Ranking::Pin{};
  }
  bool is_clock_pin(const std::string& instance, const std::string& pin) const { return pin_rank(instance, pin).clock; }
  std::optional<double> cell_slack(const std::string& instance) const {
    const auto            prefix = instance + ":";
    std::optional<double> slack;
    const auto            include = [&](double value, bool clock) {
      if (!clock && std::isfinite(value)) {
        slack = slack ? std::min(*slack, value) : value;
      }
    };
    if (ranking) {
      const auto& labels = ranking->layout->labels;
      for (auto it = std::lower_bound(labels.begin(), labels.end(), prefix); it != labels.end() && it->starts_with(prefix); ++it) {
        const auto& value = ranking->pins[it - labels.begin()];
        include(value.slack, value.clock);
      }
    } else {
      for (auto it = pins.lower_bound(prefix); it != pins.end() && it->first.starts_with(prefix); ++it) {
        if (it->second.slack) {
          include(*it->second.slack, it->second.clock);
        }
      }
    }
    return slack;
  }
  // Physical connection lookup: a top port (instance "PIN") is keyed by its
  // name; an instance pin by "inst:pin", else the "inst/pin" spelling.
  const Pin* find_pin(const std::string& instance, const std::string& pin) const {
    auto it = pins.find(instance == "PIN" ? pin : instance + ":" + pin);
    if (it == pins.end() && instance != "PIN") {
      it = pins.find(instance + "/" + pin);
    }
    return it == pins.end() ? nullptr : &it->second;
  }
};
}  // namespace livehd::timing
