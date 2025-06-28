// vcd_power.cpp
#include "vcd_power.hpp"

#include <cassert>
#include <cstdio>
#include <format>
#include <iostream>
#include <print>

#include "absl/strings/str_cat.h"

Vcd_power::Vcd_power(size_t n_buckets) : Vcd_reader(n_buckets), power_total_(0.0), power_samples_(0) {}

void Vcd_power::add(std::string_view hier_name, double energy_per_transition) {
  // store double the provided energy (to cover both rising and falling edges)
  double energy = 2.0 * energy_per_transition;
  net_power_.insert_or_assign(std::string(hier_name), energy);

  // initialize empty transition bins
  channels_[std::string(hier_name)].transitions.assign(n_buckets_, 0);
}

void Vcd_power::on_value(const std::string& hier_name, std::string_view /*value*/) {
  // increment the count for this signal in the current time‐bucket
  auto it = channels_.find(hier_name);
  if (it == channels_.end()) {
    return;  // this signal wasn’t registered via add()
  }

  size_t bucket = get_current_bucket();
  // ensure we didn’t overflow our bucket array
  assert(bucket < it->second.transitions.size());
  it->second.transitions[bucket] += 1;
}

void Vcd_power::compute(const std::string& out_dir) {
  // for each registered signal, produce its .power.trace
  for (auto& [name, channel] : channels_) {
    auto p_it = net_power_.find(name);
    if (p_it == net_power_.end()) {
      continue;  // no energy defined
    }
    double energy = p_it->second;

    // build per-bucket power = transitions×energy
    std::vector<double> power_trace;
    power_trace.reserve(n_buckets_);
    for (size_t cnt : channel.transitions) {
      power_trace.push_back(static_cast<double>(cnt) * energy);
    }

    // open output file
    auto  fname = absl::StrCat(out_dir, "/", filename_, "_", name, ".power.trace");
    FILE* f     = std::fopen(fname.c_str(), "w");
    if (!f) {
      std::cerr << std::format("ERROR: cannot open {}\n", fname);
      continue;
    }

    double dt  = timescale_ * static_cast<double>(max_timestamp_) / n_buckets_;
    double t   = 0.0;
    double sum = 0.0;
    for (double p : power_trace) {
      t += dt;
      sum += p;
      auto str = std::format("{} {}\n", t, p);
      fputs(str.c_str(), f);
    }
    std::fclose(f);

    // accumulate for overall average
    power_total_ += (sum / static_cast<double>(n_buckets_));
    ++power_samples_;
  }
}

double Vcd_power::get_power_average() const {
  if (power_samples_ == 0) {
    return 0.0;
  }
  return power_total_ / static_cast<double>(power_samples_);
}
