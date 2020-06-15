//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"

#include <cassert>

#include "lgedge.hpp"
#include "lgraph.hpp"

void Fast_edge_iterator::Fast_iter::advance_if_deleted() {

  if (likely(nid==0 || current_g->is_valid_node(nid)))
    return;

  go_next();
}

void Fast_edge_iterator::Fast_iter::go_next() {
  I(nid != 0);

  nid = current_g->fast_next(nid);

  if (nid.is_invalid()) {
    if (visit_sub) {
      auto next_hidx = top_g->ref_htree()->get_depth_preorder_next(hidx);
      while (!next_hidx.is_invalid()) {
        hidx      = next_hidx;
        current_g = top_g->ref_htree()->ref_lgraph(hidx);
        nid       = current_g->fast_first();
        if (!nid.is_invalid()) return;
        next_hidx = top_g->ref_htree()->get_depth_preorder_next(hidx);
      }
      current_g = top_g;
      I(nid == 0);
      hidx.invalidate();
    } else {
      I(nid == 0);
      hidx.invalidate();
    }
  }
}

Fast_edge_iterator::Fast_iter &Fast_edge_iterator::Fast_iter::operator++() {
  go_next();

  return *this;
}

Fast_edge_iterator::Fast_iter Fast_edge_iterator::begin() const {
  auto nid = top_g->fast_first();

  if (nid) return Fast_edge_iterator::Fast_iter(top_g, top_g, Hierarchy_tree::root_index(), nid, visit_sub);

  return end();
}

Flow_base_iterator::Flow_base_iterator(bool _visit_sub)
    : global_it(Fast_edge_iterator::Fast_iter(_visit_sub)), visit_sub(_visit_sub) {
  linear_phase = true;
}

Flow_base_iterator::Flow_base_iterator(LGraph *lg, bool _visit_sub)
    : global_it(lg->fast(_visit_sub).begin()), visit_sub(_visit_sub) {
  linear_phase = true;
}

void Fwd_edge_iterator::Fwd_iter::topo_add_chain_down(const Node_pin &dst_pin) {
  I(dst_pin.get_node().is_type_sub_present());

  auto down_pin = dst_pin.get_down_pin();
  I(down_pin.is_sink());  // fwd

  // fmt::print("topo       down node:{} down_pin:{}\n", down_pin.get_node().debug_name(), down_pin.debug_name());

  for (auto &edge2 : down_pin.inp_edges()) {  // fwd
    I(edge2.sink.get_pid() == down_pin.get_pid());
    topo_add_chain_fwd(edge2.driver);
  }
}

void Fwd_edge_iterator::Fwd_iter::topo_add_chain_fwd(const Node_pin &dst_pin) {
  const auto dst_node = dst_pin.get_node();
  if (visited.count(dst_node.get_compact())) return;

  if (visit_sub) {
    if (dst_node.is_type_sub_present()) {  // DOWN??
      topo_add_chain_down(dst_pin);
      return;
    } else if (dst_node.is_graph_input()) {  // fwd: UP??
      if (!dst_node.is_root()) {             // fwd: UP??
        auto up_pin = dst_pin.get_up_pin();
        if (up_pin.is_invalid()) return;  // Pin is not connected

        I(up_pin.is_sink());  // fwd

        for (auto &edge2 : up_pin.inp_edges()) {  // fwd
          I(edge2.sink.get_pid() == up_pin.get_pid());
          topo_add_chain_fwd(edge2.driver);
        }
      }
      return;
    }
  }

  pending_stack.push_back(dst_node);
}

void Fwd_edge_iterator::Fwd_iter::fwd_get_from_linear(LGraph *top) {
  I(linear_phase);

  global_it.advance_if_deleted();
  if (global_it.is_invalid()) {
    current_node.invalidate();
    linear_phase = false;
    global_it    = top->fast(visit_sub).begin();
    return;
  }

  while (true) {
    auto next_node = *global_it;
    ++global_it;
    if (global_it.is_invalid()) {
      global_it    = top->fast(visit_sub).begin();
      linear_phase = false;
      current_node.invalidate();
      return;
    }

    bool is_topo_sorted = true;
    if (next_node.is_type_loop_breaker()) {
      if (visit_sub && next_node.is_type_sub()) is_topo_sorted = false;
    } else {
      for (const auto edge : next_node.inp_edges()) {
        if (edge.driver.is_graph_input()) continue;  // If input while in linear mode, we are still in linear mode

        auto driver_node = edge.driver.get_node();
        // NOTE: For hierarchical, if the driver_node is an IO (input). It
        // could try to go up to see if the node is pipelined or not visited

        if (!visited.count(driver_node.get_compact())) {  // fwd
          is_topo_sorted = false;
          break;
        }
      }
    }

    if (is_topo_sorted) {
      visited.insert(next_node.get_compact());
      current_node.update(next_node);
      return;
    }
  }
}

void Fwd_edge_iterator::Fwd_iter::fwd_get_from_pending() {
  do {
    while (!pending_stack.empty()) {
      auto node = pending_stack.back();

#if 1
      if (visited.count(node.get_compact())) {
        pending_stack.pop_back();
        continue;
      }
#endif
      //      if (node.debug_name() == "node_372_xor_lg_test_2")
      //        fmt::print("HERE\n");
      //

      // fmt::print("trying {}\n",node.debug_name());

      bool any_propagated = false;
      if (visit_sub && node.is_type_sub_present()) {
        for (auto &pin : node.out_connected_pins()) {  // fwd
          topo_add_chain_down(pin);
          any_propagated = true;
        }
      }

      bool can_be_visited = false;
      if (likely(!any_propagated && !node.is_graph_io() && (!visit_sub || !node.is_type_sub_present()))) {
        can_be_visited = true;
        for (auto &edge2 : node.inp_edges()) {  // fwd
          // fmt::print("adding {} from {}\n",edge2.driver.get_node().debug_name(), node.debug_name());
          topo_add_chain_fwd(edge2.driver);
        }
      }

      if (pending_stack.back() != node) continue;
      visited.insert(node.get_compact());
      pending_stack.pop_back();

      if (can_be_visited) {
        current_node.update(node);
        return;
      }
    }

    global_it.advance_if_deleted();
    if (global_it.is_invalid()) {
      current_node.invalidate();
      return;
    }

    I(!(*global_it).is_graph_io());  // NOTE: should we propagate IO for going up?
    if (!visited.count((*global_it).get_compact())) {
      pending_stack.push_back(*global_it);
      for (auto &edge2 : (*global_it).inp_edges()) {  // fwd
        // fmt::print("chain  {} from {}\n",edge2.driver.get_node().debug_name(), (*global_it).debug_name());
        topo_add_chain_fwd(edge2.driver);
      }
    }

    ++global_it;

  } while (true);
}

void Fwd_edge_iterator::Fwd_iter::fwd_first(LGraph *lg) {
  I(!lg->is_empty());
  I(current_node.is_invalid());
  I(linear_phase);

  fwd_get_from_linear(lg);
  if (current_node.is_invalid()) {
    I(!linear_phase);
    fwd_get_from_pending();
  }

  I(!current_node.is_invalid());
}

void Fwd_edge_iterator::Fwd_iter::fwd_next() {
  I(!current_node.is_invalid());

  if (linear_phase) {
    fwd_get_from_linear(current_node.get_top_lgraph());
    GI(current_node.is_invalid(), !linear_phase);
    if (current_node.is_invalid()) fwd_get_from_pending();
    return;
  }

  fwd_get_from_pending();
}

void Bwd_edge_iterator::Bwd_iter::bwd_first(LGraph *lg) {
  (void)lg;
  I(pending_stack.empty());
  I(visited.empty());
}

void Bwd_edge_iterator::Bwd_iter::bwd_next() {
  I(false);  // FIXME: forward works, now do backward
}
