// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
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
  struct Pin_index {
    std::vector<std::string> labels;  // sorted, unique physical identities
    size_t                   find(std::string_view name) const {
      const auto it = std::lower_bound(labels.begin(), labels.end(), name);
      return it != labels.end() && *it == name ? size_t(it - labels.begin()) : labels.size();
    }
  };
  struct Dense_pins {
    std::shared_ptr<const Pin_index> index;
    std::vector<Pin>                 values;
    // Filtering a snapshot must not alter the shared identity or another
    // snapshot. Allocate the exclusion bits only when a pin is removed.
    std::vector<bool>                excluded;
    size_t                           excluded_count = 0;
    bool                             contains(size_t i) const { return i < values.size() && (excluded.empty() || !excluded[i]); }
    void                             erase(size_t i) {
      if (!contains(i)) {
        return;
      }
      if (excluded.empty()) {
        excluded.resize(values.size());
      }
      excluded[i] = true;
      ++excluded_count;
    }
  };
  double                     time_unit_ps = 1000;
  // Sparse inputs remain useful for external reports and small what-if views.
  // Production detailed evaluations share one immutable label index and own
  // only dense values, avoiding per-evaluation map nodes and label copies.
  std::map<std::string, Pin, std::less<>> pins;
  std::optional<Dense_pins>               dense;
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
  size_t     pin_count() const { return dense ? dense->values.size() - dense->excluded_count : pins.size(); }
  bool       has_pins() const { return pin_count() != 0; }
  const Pin* find_label(std::string_view name) const {
    if (dense) {
      const auto i = dense->index->find(name);
      return dense->contains(i) ? &dense->values[i] : nullptr;
    }
    const auto it = pins.find(name);
    return it == pins.end() ? nullptr : &it->second;
  }
  const Pin& at_pin(std::string_view name) const {
    if (const auto* pin = find_label(name)) {
      return *pin;
    }
    throw std::out_of_range("timing feedback pin not found: " + std::string(name));
  }
  Pin& at_pin(std::string_view name) { return const_cast<Pin&>(static_cast<const Feedback&>(*this).at_pin(name)); }
  void erase_pin(std::string_view name) {
    if (dense) {
      dense->erase(dense->index->find(name));
    } else {
      pins.erase(std::string(name));
    }
  }
  template <typename Fn>
  void for_each_pin(Fn&& fn) const {
    if (dense) {
      for (size_t i = 0; i < dense->values.size(); ++i) {
        if (dense->contains(i)) {
          fn(dense->index->labels[i], dense->values[i]);
        }
      }
    } else {
      for (const auto& [name, pin] : pins) {
        fn(name, pin);
      }
    }
  }
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
    } else if (dense) {
      const auto& labels = dense->index->labels;
      for (auto it = std::lower_bound(labels.begin(), labels.end(), prefix); it != labels.end() && it->starts_with(prefix); ++it) {
        const auto i = size_t(it - labels.begin());
        if (dense->contains(i) && dense->values[i].slack) {
          include(*dense->values[i].slack, dense->values[i].clock);
        }
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
    auto* value = find_label(instance == "PIN" ? pin : instance + ":" + pin);
    if (!value && instance != "PIN") {
      value = find_label(instance + "/" + pin);
    }
    return value;
  }
};
}  // namespace livehd::timing
