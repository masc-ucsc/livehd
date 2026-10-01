// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnet.hpp"
#include "xag.hpp"

namespace livehd::usyn {

// Logical boundary correspondence only. Clock/reset/source-location and
// eligibility metadata must come from Region_blast, never be inferred from
// the reduced Lnet latch records or from names.
struct Xag_region {
  struct State {
    std::string name;
    char        init = 'x';
    Xsignal     q, d;
  };
  struct Output {
    std::string name;
    Xsignal     signal;
  };
  Xag                  graph;
  std::vector<Xsignal> inputs;
  std::vector<State>   state;
  std::vector<Output>  outputs;
  std::vector<Xsignal> original_nodes;
  Status               status = Status::invalid;
  std::string          reason;
};

Xag_region import_lnet(const synth::Lnet& net, Budget& work, uint32_t max_nodes = 2000000);

struct Lnet_result {
  Status                     status = Status::invalid;
  std::optional<synth::Lnet> net;
  std::string                reason;
};

// Logical CMOS emission, with the original state boundary and port order.
// Dead logic is omitted, XOR is retained, and edge polarity is folded into
// two-input LUTs. Opposite boundary outputs share an explicit inverter.
Lnet_result export_lnet(const Xag_region& region, Budget& work, uint32_t max_nodes = 2000000);

}  // namespace livehd::usyn
