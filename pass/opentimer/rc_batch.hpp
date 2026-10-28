// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "ot/timer/timer.hpp"

namespace livehd::opentimer {
// Bound transient indexed RC storage by electrical records, not by design size.
// One large net remains indivisible. Explicit flush keeps exceptions out of
// the destructor; timing propagation happens only after the whole update.
class Rc_batch {
  ot::Timer& timer;
  std::vector<ot::BoundRc> data;
  size_t     records = 0, nets = 0;

public:
  explicit Rc_batch(ot::Timer& value) : timer(value) {}
  void flush() {
    if (data.empty()) {
      return;
    }
    timer.update_bound_parasitics(std::move(data), ot::farad_t(1e-15), ot::ohm_t(1));
    data    = {};
    records = 0;
  }
  void add(ot::BoundRc net) {
    records += net.nodes.size() + net.edges.size();
    ++nets;
    data.push_back(std::move(net));
    if (records >= 65536) {
      flush();
    }
  }
  size_t size() const { return nets; }
};
}  // namespace livehd::opentimer
