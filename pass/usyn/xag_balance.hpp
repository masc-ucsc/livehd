// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "xag.hpp"

namespace livehd::usyn {

struct Balance_result {
  Status               status = Status::invalid;
  Xag                  graph;
  std::vector<Xsignal> outputs;
  uint64_t             groups       = 0;
  uint64_t             duplications = 0;
};

// Rebuild an immutable snapshot, balancing associative AND/XOR groups. A shared
// node or protected root is normally a boundary. Explicit duplication admits
// at most duplication_limit gates in shared crossings on critical paths, with
// at most four gates owned exclusively by each crossing's shared root;
// protected outputs remain boundaries. The caller must reprice the full result
// and guard its area/depth before publishing. XOR phases/cancellation are
// exact identities on independent symbolic leaves, including computed leaves.
// No incomplete rebuild is published. Source order includes unused inputs.
Balance_result balance_xag(const Xag& graph, std::span<const Xsignal> outputs, Budget& work, uint32_t max_nodes = 2000000,
                           uint32_t group_limit = 128, uint32_t duplication_limit = 0);

// A 2:1 mux recognized in either XAG encoding: the XOR form Xag::mux builds,
// f ^ (s & (t ^ f)), or the AND/OR form the Lnet import produces,
// ~(~(s & t) & ~(~s & f)). Every edge phase is folded into the arms: the value
// is `s ? t : f`, complemented when `inverted`. `then_reads`/`else_reads` count
// how many edges inside the mux read each arm (the XOR form reads f twice).
struct Mux_match {
  Xsignal select, when_true, when_false;
  bool    inverted   = false;
  uint8_t then_reads = 1, else_reads = 2;
};
std::optional<Mux_match> match_mux(const Xag& graph, Id node);

struct Mux_balance_result {
  Status               status = Status::invalid;
  Xag                  graph;
  std::vector<Xsignal> outputs;
  uint64_t             chains = 0;  // priority chains rebuilt
  uint64_t             arms   = 0;  // (condition, value) pairs in those chains
};

// Priority-mux chains `c1 ? v1 : (c2 ? v2 : (... cn ? vn : d))` have depth
// linear in n. Adjacent pairs combine associatively, (c1, v1) . (c2, v2) =
// (c1 | c2, c1 ? v1 : v2), so a chain is rebuilt as an order-preserving tree,
// merging the adjacent pair whose operands arrive earliest first. The chain
// continues through an arm that is itself a mux owned only by this chain (one
// reference, not a protected output); either arm may continue, the select being
// complemented to keep the chain on the else side. Chains shorter than
// min_arms stay as built. The rewrite is an exact identity whatever the
// conditions, so no exclusivity proof is needed. It trades about one OR per
// arm for logarithmic depth; the caller reprices the full result and decides.
// Same snapshot contract as balance_xag: source order, all ports, no partial
// publication, fixed work and node bounds.
Mux_balance_result balance_mux_chains(const Xag& graph, std::span<const Xsignal> outputs, Budget& work,
                                      uint32_t max_nodes = 2000000, uint32_t min_arms = 3, uint32_t chain_limit = 256);

}  // namespace livehd::usyn
