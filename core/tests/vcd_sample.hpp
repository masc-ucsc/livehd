// vcd_sample.hpp
#pragma once

#include <string>
#include <unordered_map>

#include "vcd_reader.hpp"

// Vcd_sample: toggles a parity bit on each transition for any signal whose
// full hierarchical name starts with "foo,".  At destruction it prints a summary.
class Vcd_sample : public Vcd_reader {
public:
  explicit Vcd_sample(size_t n_buckets = 100);
  ~Vcd_sample() override;

protected:
  void on_value(const std::string& hier_name, std::string_view value) override;

private:
  // parity per signal: 0 or 1
  std::unordered_map<std::string, bool> parity_;
};
