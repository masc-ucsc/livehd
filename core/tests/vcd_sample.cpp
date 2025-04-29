// vcd_sample.cpp
#include "vcd_sample.hpp"

#include <fmt/format.h>

Vcd_sample::Vcd_sample(size_t n_buckets) : Vcd_reader(n_buckets) {}

Vcd_sample::~Vcd_sample() {
  fmt::print("=== sample parity results ===\n");
  for (auto& [sig, p] : parity_) {
    fmt::print("  {} : {}\n", sig, p);
  }
}

void Vcd_sample::on_value(const std::string& hier_name, std::string_view val) {
  fmt::print("ON:{}:{}:{}\n", timestamp_, hier_name, val);
  //  only for signals whose name begins with "foo,"
  if (hier_name.rfind("foo,", 0) == 0) {
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
}
