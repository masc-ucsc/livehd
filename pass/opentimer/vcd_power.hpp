// vcd_power.hpp
#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "vcd_reader.hpp"

// Vcd_power: counts transitions per signal into time-buckets and
// writes out per-signal “.power.trace” files.  Inherits from Vcd_reader.
class Vcd_power : public Vcd_reader {
public:
  explicit Vcd_power(size_t n_buckets = 100);

  // Register a signal and its energy-per-transition (will be doubled internally).
  void add(std::string_view hier_name, double energy_per_transition);

  // After open() && process(), writes traces to out_dir and updates the average.
  void compute(const std::string& out_dir);

  [[nodiscard]] double get_power_average() const;

protected:
  // Callback from Vcd_reader for every sample.  We ignore 'value' here.
  void on_value(const std::string& hier_name, std::string_view /*value*/) override;

private:
  struct Channel {
    std::vector<size_t> transitions;  // per-bucket edge counts
  };

  // signal → bucketed transition counts
  absl::node_hash_map<std::string, Channel> channels_;
  // signal → (2 × energy per transition)
  absl::flat_hash_map<std::string, double> net_power_;

  double power_total_;
  size_t power_samples_;
};
