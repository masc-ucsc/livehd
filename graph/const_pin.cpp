//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "attrs.hpp"
#include "cell.hpp"
#include "dlop.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

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

  // HHDS owns the payload index and the monotonic CONST_NODE pin tail. After
  // the one-time rebuild needed for a loaded graph, lookups and appends are
  // O(1) average and never probe or manufacture candidate pins.
  return g.intern_constant(value.serialize(), Const_small_pid_count);
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
