#include "sample.cc"

uint64_t global_time   = 0;

void advance_clock(cxxrtl_design::p_sample &top) {

  top.p_clk.next = value<1> {1u};
  top.step();
  top.p_clk.next = value<1> {0u};
  top.step();

  global_time++;
}

int main() {
  cxxrtl_design::p_sample sample;

  sample.p_clk.next = value<1> {0u};
  sample.p_reset.next = value<1> {1u};
  while(global_time<1000) {
    advance_clock(sample);
  }
  sample.p_reset.next = value<1> {0u};

  while(global_time < 100000000) {
    advance_clock(sample);
  }
}

