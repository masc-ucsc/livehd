// vcd_sample.cpp
#include "vcd_sample.hpp"

#include <iostream>
#include <format>

Vcd_sample::Vcd_sample(size_t n_buckets) : Vcd_reader(n_buckets) {}

Vcd_sample::~Vcd_sample() {
  std::cout << "=== sample parity results ===\n";
  for (auto& [sig, p] : parity_) {
    std::cout << std::format("  {} : {}\n", sig, p);
    for (const auto& n : get_alias(sig)) {
      std::cout << std::format("     : {}\n", n);
    }
  }
}

void Vcd_sample::on_value(const std::string& hier_name, std::string_view val) {
  //  only for signals whose name begins with "foo,"
  if (hier_name.find("uart") != std::string_view::npos) {
    // toggle parity
    parity_[hier_name] = !parity_[hier_name];
  }
}

int main() {
  Vcd_sample vs;
  if (!vs.open("foo_signals.vcd")) {
    return 1;
  }
  vs.process();
  // destructor prints parity summary

  return 0;
}
