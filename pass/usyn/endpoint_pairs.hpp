// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <deque>
#include <limits>
#include <set>

#include "xag.hpp"

namespace livehd::usyn {

inline constexpr uint32_t unknown_clock_domain = std::numeric_limits<uint32_t>::max();

struct Pair_endpoint {
  // Ordinary discovery uses actual static inputs. Joint functional synthesis
  // may instead seed complete selected next-state functions, including absorbed logic.
  std::vector<Xsignal> inputs;
  uint32_t             domain = unknown_clock_domain;
};

struct Endpoint_pairs {
  Status                               status = Status::invalid;
  std::vector<std::array<uint32_t, 2>> pairs;
  uint64_t                             shared_nodes = 0, more_than_two = 0, domain_skips = 0;
  uint64_t                             source_pairs = 0;
  bool                                 capped       = false;
};

// One reverse topological propagation. Each node retains at most two distinct
// endpoint labels, saturating at three; sources do not seed ordinary pairs.
// shared_source_pairs additionally admits two distinct sources shared by exactly
// the same two endpoints, with capped pending-source and candidate storage.
// Non-endpoint readers are protected by the caller's joint cost ledger.
// Pair indices refer to endpoints, with deterministic ordering and bounded
// candidate storage. Unknown domains never establish clock compatibility.
Endpoint_pairs find_endpoint_pairs(const Xag& graph, std::span<const Pair_endpoint> endpoints, uint32_t max_pairs,
                                   uint32_t max_nodes, Budget& work, bool shared_source_pairs = false);

struct Pair_windows {
  Status                    status    = Status::invalid;
  bool                      exhausted = false;
  std::array<Xag_window, 2> windows;
  std::vector<Id>           basis;
  // Non-source boundary signals whose implementations remain live outside the
  // pair. Positive identities; the caller retains every consumer's polarity.
  std::vector<Id>           outside_boundary;
  std::string               reason;
};

// Grow one common structural boundary for two roots. Internal boundary signals
// are independent formal inputs: preserving all assignments is conservative
// even if their actual upstream implementations are correlated. Both per-root
// support and the union support/node counts are bounded. Outside readers are
// accounted for by the caller, not removed by window construction.
Pair_windows grow_pair_windows(const Xag& graph, std::array<Xsignal, 2> roots, const Window_limits& endpoint_limits,
                               uint32_t joint_inputs, Budget& work);

// Maximal private joint cone relative to unchanged outside implementations.
// Trace the actual outside roots, not historical Node::fanouts. Stop at their
// live cones and sources; the two roots may share private interior nodes.
// Explicit outside ports remain in the basis and cannot earn deletion credit.
// Refuse oversized windows without publishing a partial boundary. max_nodes
// bounds graph-sized traversal storage independently of the window-node cap.
Pair_windows fanout_free_pair_windows(const Xag& graph, std::array<Xsignal, 2> roots, std::span<const Xsignal> outside_roots,
                                      const Window_limits& endpoint_limits, uint32_t joint_inputs, uint32_t max_nodes,
                                      Budget& work);

struct Pair_dependents {
  Status               status = Status::invalid;
  std::vector<uint8_t> endpoints;
};

// Conservatively invalidate endpoint costs that can share a paid gate or a
// demanded inverse rail with changed old/new static inputs. Positive sources
// alone do not make unrelated logic affected. The graph is append-only across
// a pair commit, so old signal identities remain valid in the new snapshot.
Pair_dependents pair_dependents(const Xag& graph, std::span<const Xsignal> changed_inputs, std::span<const Pair_endpoint> endpoints,
                                uint32_t max_nodes, Budget& work);

using Endpoint_pair = std::array<uint32_t, 2>;

struct Pair_queue_report {
  uint64_t trials = 0, requeues = 0, stale_skips = 0;
  bool     exhausted = false;
};

// Failed unchanged trials are not repeated. Affected trials are retried, and
// already pending work survives unrelated commits. Refresh removes pairs
// absent from the new sharing snapshot. Both storage and total trials are capped.
class Pair_queue {
public:
  Pair_queue(uint32_t capacity_arg, uint32_t trials) : capacity(capacity_arg), trial_limit(trials) {}
  Status                       refresh(std::span<const Endpoint_pair> candidates, std::span<const uint8_t> affected, Budget& work);
  std::optional<Endpoint_pair> pop(Budget& work);
  const Pair_queue_report&     report() const { return stats; }

private:
  uint32_t                  capacity, trial_limit;
  std::deque<Endpoint_pair> pending;
  std::set<Endpoint_pair>   attempted;
  Pair_queue_report         stats;
};

}  // namespace livehd::usyn
