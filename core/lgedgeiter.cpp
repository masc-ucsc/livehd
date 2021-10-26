//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"

#include <cassert>

#include "lgedge.hpp"
#include "lgraph.hpp"

void Fast_edge_iterator::Fast_iter::advance_if_deleted() {
  if (likely(nid == 0 || current_g->is_valid_node(nid)))
    return;

  go_next();
}

void Fast_edge_iterator::Fast_iter::go_next() {
  I(nid != 0);

  nid = current_g->fast_next(nid);
  if (!visit_sub)
    return;

  while (!nid.is_invalid()) {  // Skip SubGraph present
    Node node(current_g, current_g, Hierarchy::hierarchical_root(), nid);
    if (!node.is_type_sub_present())
      break;

    nid = current_g->fast_next(nid);
  }

  if (!nid.is_invalid())
    return;

  std::tie(hidx, current_g) =  top_g->get_htree().get_next(hidx);
  while (current_g != top_g) {
    nid = current_g->fast_first();
    if (!nid.is_invalid()) {
      return;
    }
    std::tie(hidx, current_g) =  top_g->get_htree().get_next(hidx);
  }
  I(current_g == top_g);
  nid = 0;
  hidx = Hierarchy::non_hierarchical(); // invalidate hidx
}

Fast_edge_iterator::Fast_iter &Fast_edge_iterator::Fast_iter::operator++() {
  go_next();

  return *this;
}

Fast_edge_iterator::Fast_iter Fast_edge_iterator::begin() const {
  auto nid = top_g->fast_first();

  if (nid) {
    Fast_iter it(top_g, top_g, visit_sub ? Hierarchy::hierarchical_root() : Hierarchy::non_hierarchical(), nid, visit_sub);
    if (visit_sub) {  // && top_g->is_type_sub(nid)) {
      Node node(top_g, top_g, Hierarchy::hierarchical_root(), nid);
      if (node.is_type_sub_present())
        ++it;
    }
    return it;
  }

  return end();
}

Flow_base_iterator::Flow_base_iterator(bool _visit_sub)
    : global_it(Fast_edge_iterator::Fast_iter(_visit_sub)), visit_sub(_visit_sub) {
  linear_first_phase = true;
  linear_last_phase  = false;
}

Flow_base_iterator::Flow_base_iterator(Lgraph *lg, bool _visit_sub)
    : global_it(lg->fast(_visit_sub).begin()), visit_sub(_visit_sub) {
  linear_first_phase = true;
  linear_last_phase  = false;
}

void Fwd_edge_iterator::Fwd_iter::topo_add_chain_down(const Node_pin &dst_pin) {
  I(dst_pin.is_hierarchical());
  I(dst_pin.get_node().is_type_sub_present());

  auto down_pin = dst_pin.get_down_pin();
  I(down_pin.is_sink());  // fwd

  // fmt::print("topo       down node:{} down_pin:{}\n", down_pin.get_node().debug_name(), down_pin.debug_name());

  for (auto &edge2 : down_pin.inp_edges()) {  // fwd
    I(edge2.sink.get_pid() == down_pin.get_pid());
    if (!unvisited.count(edge2.driver.get_node().get_compact()))
      continue;

    topo_add_chain_fwd(edge2.driver);
  }
}

void Fwd_edge_iterator::Fwd_iter::topo_add_chain_fwd(const Node_pin &dst_pin) {
  const auto dst_node = dst_pin.get_node();
  I(unvisited.count(dst_node.get_compact()));

  if (visit_sub) {
    if (dst_node.is_type_sub_present()) {  // DOWN??
      topo_add_chain_down(dst_pin);
      return;
    } else if (dst_node.is_graph_input()) {  // fwd: UP??
      if (!dst_node.is_root()) {             // fwd: UP??
        auto up_pin = dst_pin.get_up_pin();
        if (up_pin.is_invalid())
          return;  // Pin is not connected

        I(up_pin.is_sink());  // fwd

        for (auto &edge2 : up_pin.inp_edges()) {  // fwd
          I(edge2.sink.get_pid() == up_pin.get_pid());
          if (!unvisited.count(edge2.driver.get_node().get_compact()))
            continue;

          topo_add_chain_fwd(edge2.driver);
        }
      }
      return;
    }
  }

  pending_stack.push_back(dst_node);
}

void Fwd_edge_iterator::Fwd_iter::fwd_get_from_linear_first(Lgraph *top) {
  I(linear_first_phase);

  current_node.invalidate();
  global_it.advance_if_deleted();

  while (linear_first_phase && !global_it.is_invalid()) {
    auto next_node = *global_it;
    ++global_it;

    if (next_node.is_type_loop_last() || !next_node.has_outputs()) {
      if (!visit_sub || !next_node.is_type_sub_present()) {
        continue;  // Keep it for linear_last_phase
      }
    }

    bool is_topo_sorted = true;
    if (!next_node.is_type_loop_first()) {              // NOTE: !next_node.has_inputs() may be nicer but a bit slower
      for (const auto &edge : next_node.inp_edges()) {  // NOTE: possible to have a "inp_edges_forward" iterator 1/2 nodes on avg
        auto driver_node = edge.driver.get_node();

        if (driver_node.is_graph_input())
          continue;  // If input while in linear mode, we are still in linear mode

        // NOTE: For hierarchical, if the driver_node is an IO (input). It
        // could try to go up to see if the node is pipelined or not visited

        if (driver_node.is_down_node() || driver_node.get_nid() > next_node.get_nid()) {
          is_topo_sorted = false;
          break;
        }

        if (unvisited.count(driver_node.get_compact())) {  // fwd
          is_topo_sorted = false;
          break;
        }
      }
    }

    if (is_topo_sorted) {
      current_node.update(next_node);
      return;
    }
    unvisited.insert(next_node.get_compact());
  }

  if (current_node.is_invalid()) {
    linear_first_phase = false;
    I(linear_last_phase == false);
    global_it = top->fast(visit_sub).begin();
  }
}

void Fwd_edge_iterator::Fwd_iter::fwd_get_from_linear_last() {
  I(linear_last_phase);

  current_node.invalidate();
  global_it.advance_if_deleted();

  while (!global_it.is_invalid()) {
    auto next_node = *global_it;
    ++global_it;

    if (next_node.is_type_loop_last() || !next_node.has_outputs()) {
      if (visit_sub && next_node.is_type_sub_present()) {
        continue;
      }
      current_node.update(next_node);
      return;
    }
  }
}

void Fwd_edge_iterator::Fwd_iter::fwd_get_from_pending(Lgraph *top) {
  do {
    while (!pending_stack.empty()) {
      auto node = pending_stack.back();

      if (!unvisited.count(node.get_compact())) {
        pending_stack.pop_back();
        continue;
      }
      I(!node.is_type_flop());

      if (unlikely(!node.get_class_lgraph()->is_valid_node(node.get_nid()))) {
        // The iterator can delete nodes
        pending_stack.pop_back();
        continue;
      }

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

        auto dpin_list = node.inp_drivers();

        if (!dpin_list.empty()) {         // Something got added, track potential combinational loops
          for (auto &dpin : dpin_list) {  // fwd
            if (unvisited.contains(dpin.get_node().get_compact()))
              topo_add_chain_fwd(dpin);
          }

          auto it = pending_loop_detect.find(node.get_compact());
          if (it == pending_loop_detect.end()) {
            pending_loop_detect[node.get_compact()] = node.get_num_out_edges();
          } else {
            it->second--;
            if (it->second <= 0) {  // Loop
              pending_loop_detect.clear();
              pending_stack.push_back(node);  // to force loop break
            }
          }
        }
      }

      if (pending_stack.back() != node)
        continue;
      unvisited.erase(node.get_compact());
      pending_stack.pop_back();
      if (pending_stack.size() <= 1 && !pending_loop_detect.empty()) {
        pending_loop_detect.clear();
      }

      if (can_be_visited) {
        I(node.get_class_lgraph()->is_valid_node(node.get_nid()));
        current_node.update(node);
        return;
      }
    }

    global_it.advance_if_deleted();
    if (global_it.is_invalid()) {
      current_node.invalidate();
      linear_last_phase = true;
      I(linear_first_phase == false);
      global_it = top->fast(visit_sub).begin();
      return;
    }

    I(!(*global_it).is_graph_io());  // NOTE: should we propagate IO for going up?
    if (unvisited.count((*global_it).get_compact())) {
      I(!(*global_it).is_type_flop());
      pending_stack.push_back(*global_it);
      for (auto &dpin : (*global_it).inp_drivers()) {  // fwd
        if (unvisited.contains(dpin.get_node().get_compact()))
          topo_add_chain_fwd(dpin);
      }
    }

    ++global_it;

  } while (true);
}

void Fwd_edge_iterator::Fwd_iter::fwd_first(Lgraph *lg) {
  I(!lg->is_empty());
  I(current_node.is_invalid());
  I(linear_first_phase);

  fwd_get_from_linear_first(lg);
  if (current_node.is_invalid()) {
    I(!linear_first_phase);
    fwd_get_from_pending(lg);
    if (current_node.is_invalid()) {
      I(linear_last_phase);
      fwd_get_from_linear_last();
    }
  }

  I(!current_node.is_invalid());  // method not called if empty, so something must be here
  I(current_node.get_class_lgraph()->is_valid_node(current_node.get_nid()));
}

void Fwd_edge_iterator::Fwd_iter::fwd_next() {
  auto *top_g2 = current_node.get_top_lgraph();
  if (linear_first_phase) {
    fwd_get_from_linear_first(top_g2);
    GI(current_node.is_invalid(), !linear_first_phase);
    if (!current_node.is_invalid())
      return;
  }

  if (!linear_last_phase) {
    fwd_get_from_pending(top_g2);
    if (!current_node.is_invalid())
      return;

    GI(!current_node.is_invalid(), current_node.get_class_lgraph()->is_valid_node(current_node.get_nid()));
    GI(current_node.is_invalid(), linear_last_phase);
  }

  fwd_get_from_linear_last();
}

void Bwd_edge_iterator::Bwd_iter::bwd_first(Lgraph *lg) {
  (void)lg;
  I(pending_stack.empty());
  I(unvisited.empty());
}

void Bwd_edge_iterator::Bwd_iter::bwd_next() {
  I(false);  // FIXME: forward works, now do backward
}
