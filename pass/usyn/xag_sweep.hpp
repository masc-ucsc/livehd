// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "xag.hpp"

namespace livehd::usyn {
struct Sweep_result {
  Status               status = Status::invalid;
  Xag                  graph;
  std::vector<Xsignal> outputs;
  uint64_t             confirmations = 0;
  uint64_t             table_words   = 0;
  bool                 limited       = false;
};

// Exact bounded functional hashing on at most sixteen primary boundary signals
// (default six). table_words_limit bounds retained tables and hash-key copies.
// Both ordered support and the complete truth table participate in the key;
// complementary matches use an explicit output phase. Larger support is kept
// structurally, never approximated. Random signatures play no role. Rebuilds
// preserve all ports, publish atomically, and use fixed work/node bounds.
Sweep_result sweep_xag(const Xag& source, std::span<const Xsignal> outputs, Budget& work, uint32_t max_nodes = 2000000,
                       uint32_t support_limit = 6, uint64_t table_words_limit = 65536);
}  // namespace livehd::usyn
