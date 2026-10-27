// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <utility>

#include "ot/timer/timer.hpp"

namespace livehd::opentimer {
// Bound transient SPEF storage by electrical records, not by design size.
// One large net remains indivisible. Explicit flush keeps exceptions out of
// the destructor; timing propagation happens only after the whole update.
class Rc_batch {
  ot::Timer& timer;
  spef::Spef data;
  size_t     records = 0, nets = 0;

public:
  explicit Rc_batch(ot::Timer& value) : timer(value) {}
  void flush() {
    if (data.nets.empty()) {
      return;
    }
    data.capacitance_unit = "1 FF";
    data.resistance_unit  = "1 OHM";
    timer.update_parasitics(std::move(data));
    data    = {};
    records = 0;
  }
  void add(spef::Net net) {
    records += net.caps.size() + net.ress.size();
    ++nets;
    data.nets.push_back(std::move(net));
    if (records >= 65536) {
      flush();
    }
  }
  size_t size() const { return nets; }
};
}  // namespace livehd::opentimer
