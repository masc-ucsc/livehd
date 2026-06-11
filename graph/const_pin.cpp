//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <mutex>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "attrs.hpp"
#include "cell.hpp"
#include "dlop.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace {

// Per-Graph state for the big-const deduplication path. Lifetime tracks the
// process, keyed by Graph*. The state is advisory only — both the dedup map
// and the pid cursor are re-validated against the graph's own pin attrs in
// create_const (a loaded graph already has const pins the registry never saw;
// a destroyed graph reallocated at the same address leaves stale entries).
// Correctness comes from the pin_const_value attr being the identity.
struct Per_graph_const_state {
  absl::flat_hash_map<std::string, hhds::Port_id> serialized_to_pid;
  hhds::Port_id                                   next_big_pid = livehd::graph_util::Const_small_pid_count;
};

absl::flat_hash_map<const hhds::Graph*, Per_graph_const_state>& registry() {
  static absl::flat_hash_map<const hhds::Graph*, Per_graph_const_state> r;
  return r;
}

std::mutex& registry_mutex() {
  static std::mutex m;
  return m;
}

}  // namespace

namespace livehd::graph_util {

hhds::Pin_class create_const(hhds::Graph& g, const Dlop& value) {
  auto const_node = g.get_constant_node();

  // Small-int fast path: integer values in [-16, 15] get a pid-encoded pin
  // on CONST_NODE. No payload, no registry entry — the pid IS the value.
  if (value.is_just_i64()) {
    auto iv = value.to_just_i64();
    if (is_small_const_int(iv)) {
      return const_node.create_driver_pin(encode_small_const(iv));
    }
  }

  // Slow path: serialize + dedup via the per-graph reverse map. Returns the
  // existing pin if the same value is already materialised; otherwise
  // allocates a fresh port_id >= Const_small_pid_count.
  auto serialized = value.serialize();

  std::lock_guard<std::mutex> lk(registry_mutex());
  auto&                       reg = registry()[&g];

  if (auto it = reg.serialized_to_pid.find(serialized); it != reg.serialized_to_pid.end()) {
    // Verify against the graph itself: the entry may be stale (a previous
    // Graph at this address). The attr is the source of truth.
    auto pin = const_node.create_driver_pin(it->second);
    if (auto a = pin.attr(livehd::attrs::pin_const_value); a.has() && a.get() == serialized) {
      return pin;
    }
    reg.serialized_to_pid.erase(it);
  }

  // Probe for a free pid. A loaded graph carries const pins the registry has
  // never seen — blindly taking next_big_pid would overwrite their
  // pin_const_value payload (silently swapping one constant for another, e.g.
  // cprop on a freshly loaded lgraph). A pid >= Const_small_pid_count is
  // occupied iff it carries the payload attr; seed the dedup map as we skip.
  auto pid = reg.next_big_pid;
  for (;; ++pid) {
    auto pin = const_node.create_driver_pin(pid);
    auto a   = pin.attr(livehd::attrs::pin_const_value);
    if (!a.has()) {
      reg.next_big_pid = pid + 1;
      pin.attr(livehd::attrs::pin_const_value).set(serialized);
      reg.serialized_to_pid.emplace(std::move(serialized), pid);
      return pin;
    }
    if (a.get() == serialized) {  // loaded graph already has this value
      reg.next_big_pid = pid + 1;
      reg.serialized_to_pid.emplace(std::move(serialized), pid);
      return pin;
    }
    reg.serialized_to_pid.emplace(a.get(), pid);
  }
}

Dlop hydrate_const(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return *Dlop::create_integer(0);
  }
  auto master = pin.get_master_node();
  if (master.get_debug_nid() == hhds::Graph::CONST_NODE) {
    auto pid = pin.get_port_id();
    if (is_small_const_pid(pid)) {
      return *Dlop::create_integer(decode_small_const(pid));
    }
    auto a = pin.attr(livehd::attrs::pin_const_value);
    if (a.has()) {
      auto p = Dlop::unserialize(a.get());
      if (p) {
        return *p;
      }
    }
    return *Dlop::create_integer(0);
  }
  // Legacy: regular Ntype_op::Nconst node with the value carried in the
  // per-node `const_value` attribute. Drops out when lgraph/ dies.
  return hydrate_const(master);
}

Dlop hydrate_const(const hhds::Node_class& node) {
  if (node.is_invalid()) {
    return *Dlop::create_integer(0);
  }
  auto a = node.attr(livehd::attrs::const_value);
  if (!a.has()) {
    return *Dlop::create_integer(0);
  }
  auto p = Dlop::unserialize(a.get());
  if (!p) {
    return *Dlop::create_integer(0);
  }
  return *p;
}

}  // namespace livehd::graph_util
