// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Word-level simulation of an LGraph cone for satopt's candidate filters
// (todo/livehd/2s-satopt C). Every value is a column of Dlops, one per
// pattern: a few corner patterns (0, all ones, alternating bits, signed
// min/max, 1), then seeded random patterns, then counterexamples fed back
// from the prover. A leaf's pattern depends only on its instance path, node,
// port and column, never on evaluation order, so two simulators of the same
// graph agree and an edited graph keeps the patterns of its unedited leaves.
//
// This is only a rejection filter: an unsupported operation (or a value past
// the caps) makes a value unavailable, which can reject nothing. Signatures and
// hashes built from these values only nominate candidates; every rewrite is
// proven. State and opaque outputs are independent leaves, as in the prover.
#include <cstdint>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "dlop.hpp"
#include "hhds/graph.hpp"
#include "prove.hpp"

namespace livehd::satopt::detail {

class Word_sim {
public:
  using Pin  = hhds::Pin_class;
  using Node = hhds::Node_class;

  struct Options {
    uint32_t samples          = 64;     // corner + random patterns
    bool     descend          = false;  // walk into called definitions (virtual flat, like a descending Prover)
    bool     reject_unstamped = false;  // an unstamped width makes a value unavailable
    uint64_t salt             = 0;      // pattern seed
    uint32_t max_models       = 64;     // counterexample columns add_model may append
  };
  static constexpr uint32_t kCorners = 7;

  explicit Word_sim(const Options& opts);

  // Every column of `p` (graph scope), or null when unavailable. The vector
  // stays valid (and grows by one column per add_model) until the simulator dies.
  const std::vector<Dlop>* values(const Pin& p) { return values(0, p); }
  [[nodiscard]] uint32_t   columns() const { return columns_; }
  // Column values computed so far (the work a budgeted caller charges).
  [[nodiscard]] uint64_t   work() const { return work_; }
  // Every definition a value so far descended into (descend), once each: a
  // cached result filtered by these values depends on their bodies.
  [[nodiscard]] std::vector<hhds::Graph*> descended() const;

  // Appends one column from a prover counterexample: each leaf the model
  // names takes its value, every other leaf its seeded pattern, and every
  // value already computed is extended. False once max_models columns were
  // appended (the model is dropped).
  bool add_model(const formal::Model& model);

  // The packed bit signature of `p`'s bit `bit`: bit j of word j/64 is that
  // bit in column j. Empty when `p` is unavailable.
  std::vector<uint64_t> bit_signature(const Pin& p, int bit);

  // The simulated twin of formal::Prover::unchanged_under: in every column,
  // with `target`'s bits in `mask` replaced by those of `value`, does every
  // exit keep its value? nullopt when a value it needs is unavailable.
  std::optional<bool> unchanged_under(const Pin& target, const Dlop& mask, const Dlop& value, const std::vector<Node>& window,
                                      const std::vector<Pin>& exits);
  // Forget these values (graph scope): their cells were rewritten or deleted.
  // A deleted cell's pins must be collected before the delete.
  void invalidate(const std::vector<Pin>& pins);

private:
  using Key = std::pair<uint32_t, Pin>;  // (instance scope, pin); scope 0 = the graph
  struct Scope {
    uint32_t parent = 0;
    Node     inst;
    int      depth = 0;
    uint64_t path  = 0;  // hash of the instance path: part of every leaf's seed
  };
  const std::vector<Dlop>* values(uint32_t scope, const Pin& p);
  // The operands a value needs before it can be computed, or false when it is
  // unavailable; a leaf needs none.
  bool                     deps(const Key& k, std::vector<Key>& out);
  std::optional<Dlop>      column(const Key& k, uint32_t j);  // one column, operands already computed
  std::optional<Dlop>      leaf(const Key& k, uint32_t j);
  std::optional<std::pair<uint32_t, Pin>> cross(uint32_t scope, const Pin& p);
  uint32_t                 child(uint32_t scope, const Node& inst);
  bool                     is_leaf(const Key& k) const;

  Options                                                  opts_;
  uint32_t                                                 columns_ = 0;
  uint64_t                                                 work_    = 0;
  uint64_t                                                 stored_  = 0;  // Dlops held
  absl::node_hash_map<Key, std::vector<Dlop>>             memo_;    // pointer-stable: callers hold values
  absl::flat_hash_set<Key>                                 failed_;
  absl::flat_hash_map<Key, Key>                            copies_;  // a value copied across an instance boundary
  const absl::flat_hash_map<Key, std::vector<Dlop>>*       overlay_ = nullptr;  // unchanged_under's replaced values
  std::vector<Key>                                         order_;  // completion order: operands before users
  std::vector<Scope>                                       scopes_{Scope{}};
  absl::flat_hash_map<std::pair<uint32_t, Node>, uint32_t> children_;
  // Counterexample columns: column -> leaf -> value.
  absl::flat_hash_map<uint32_t, absl::flat_hash_map<Key, Dlop>> models_;
};

}  // namespace livehd::satopt::detail
