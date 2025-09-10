//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "invariant_finder.hpp"

#include "lgedgeiter.hpp"
#include "live_common.hpp"

using namespace Live;

void Invariant_finder::get_topology() {
  std::vector<Lgraph *>         discovered;
  absl::flat_hash_set<Lgraph *> visited;

  discovered.push_back(elab_graph);

  // top module
  boundaries.instance_collection[Invariant_boundaries::get_graphID(elab_graph)].insert("");
  boundaries.instance_type_map["##TOP##"] = Invariant_boundaries::get_graphID(elab_graph);
  boundaries.top                          = elab_graph->get_name();

  while (discovered.size()) {
    Lgraph *current = discovered.back();
    discovered.pop_back();
    if (visited.find(current) != visited.end()) {
      continue;
    }

    visited.insert(current);

    for (auto &idx : current->fast()) {
      // filter out primitives, we're only interested in user defined modules
      if (current->node_type_get(idx).op != SubGraph_Op) {
        continue;
      }

      Lgraph *subgraph = Lgraph_open(current->get_path(), current->subgraph_id_get(idx));
      I(subgraph);

      boundaries.hierarchy_tree[Invariant_boundaries::get_graphID(subgraph)].insert(Invariant_boundaries::get_graphID(current));

      for (auto &prefix : boundaries.instance_collection[Invariant_boundaries::get_graphID(current)]) {
        if (current->get_instance_name_id(idx) == 0) {
          Pass::info("RTP got node with no instance name {}", idx);
          continue;
        }

        std::string instance_name;
        if (prefix != "") {
          instance_name = prefix + boundaries.hierarchical_separator;
        }
        instance_name.append(current->get_node_instancename(idx));

        boundaries.instance_collection[Invariant_boundaries::get_graphID(subgraph)].insert(instance_name);
        boundaries.instance_type_map[instance_name] = Invariant_boundaries::get_graphID(subgraph);
      }

      if (visited.find(subgraph) == visited.end()) {
        discovered.push_back(subgraph);
      }
    }
  }
}

void Invariant_finder::propagate_until_boundary(Index_id nid, uint32_t bit_selection) {
  Index_id    master_id = synth_graph->get_master_nid(nid);
  const auto &op        = synth_graph->node_type_get(master_id).op;
  if (op == GraphIO_Op || op == U32Const_Op || op == StrConst_Op) {
    return;
  }

  I(synth_graph->get_bits(nid) > bit_selection);

  const Node_bit nid_bit = std::make_pair(nid, bit_selection);
  if (partial_endpoints.find(nid_bit) != partial_endpoints.end()) {
    return;
  }

  const Node_bit mnid_bit = std::make_pair(master_id, bit_selection);
  if (partial_endpoints.find(mnid_bit) != partial_endpoints.end()) {
    partial_endpoints[nid_bit]  = partial_endpoints[mnid_bit];
    partial_cone_cells[nid_bit] = partial_cone_cells[mnid_bit];
    cached.insert(nid_bit);
    return;
  }

  I(partial_endpoints.find(nid_bit) == partial_endpoints.end());
  I(partial_cone_cells.find(nid_bit) == partial_cone_cells.end());

  I(deleted.find(nid_bit) == deleted.end());

  partial_endpoints[nid_bit]  = Net_set();
  partial_cone_cells[nid_bit] = Gate_set();

  stack.set_bit(nid);

  for (auto &edge : synth_graph->inp_edges(master_id)) {
    // in cases like join/pick we only propagate to a specific bit
    absl::flat_hash_set<uint32_t> bit_selections;
    int propagate = resolve_bit(synth_graph, nid, bit_selection, edge.get_inp_pin().get_pid(), bit_selections);
    if (propagate == -1) {
      continue;
    }

    for (uint32_t t_bit_selection : bit_selections) {
      Index_id driver_cell = synth_graph->get_node(edge.get_out_pin()).get_nid();
      Node_bit driver_bit  = std::make_pair(driver_cell, t_bit_selection);
      Net_ID   net_bit     = std::make_pair(synth_graph->get_wid(driver_cell), t_bit_selection);

      if (synth_graph->get_bits(driver_cell) <= t_bit_selection && synth_graph->get_bits(driver_cell) == 1) {
        // FIXME: is there a better way to detect control signals on multibit cells? (eg memory, registers)
        t_bit_selection = 0;
      }

      partial_cone_cells[nid_bit].insert(driver_cell);

      // FIXME: for registers don't propagate through clk, rst, enable
      // FIXME: for fflops propagate data to data, valid to valid and retry to retry only

      if (stack.get_bit(driver_cell)) {
        continue;
      }

      // no net name set, thus this is not a invariant boundary
      if (synth_graph->get_wid(driver_cell) == 0 || !boundaries.is_invariant_boundary(net_bit)) {
        // recurse
        propagate_until_boundary(driver_cell, t_bit_selection);
        partial_endpoints[nid_bit].insert(partial_endpoints[driver_bit].begin(), partial_endpoints[driver_bit].end());
        partial_cone_cells[nid_bit].insert(partial_cone_cells[driver_bit].begin(), partial_cone_cells[driver_bit].end());

        continue;
      } else {
        partial_endpoints[nid_bit].insert(driver_bit);
      }
    }
  }

  cached.insert(nid_bit);
  for (auto &edge : synth_graph->inp_edges(master_id)) {
    for (uint32_t t_bit_selection = 0; t_bit_selection < synth_graph->get_bits(edge.get_out_pin()); t_bit_selection++) {
      Index_id driver_cell = synth_graph->get_node(edge.get_out_pin()).get_nid();
      Node_bit driver_bit  = std::make_pair(driver_cell, t_bit_selection);
      clear_cache(driver_bit);
    }
  }

  stack.clear_bit(nid);
}

void Invariant_finder::clear_cache(const Node_bit &entry) {
  if (partial_endpoints.find(entry) == partial_endpoints.end() || stack.get_bit(entry.first)) {
    return;
  }

  for (auto &oedge : synth_graph->out_edges(entry.first)) {
    for (uint32_t bit = 0; bit < synth_graph->get_bits(oedge.get_out_pin()); bit++) {
      if (cached.find(std::make_pair(oedge.get_idx(), bit)) == cached.end()) {
        return;
      }
    }
  }
  partial_endpoints[entry].clear();
  partial_endpoints.erase(entry);
  partial_cone_cells.erase(entry);

#ifndef NDEBUG
  deleted.insert(entry);
#endif
}

void Invariant_finder::find_invariant_boundaries() {
  std::string path = elab_graph->get_path();
  get_topology();

  absl::flat_hash_map<Net_ID, Index_id> invariant_boundaries;
  for (auto &_inst : boundaries.instance_type_map) {
    Instance_name inst = _inst.first;
    if (inst == "##TOP##") {
      inst = "";
    } else {
      inst = inst + boundaries.hierarchical_separator;
    }

    Lgraph *lg = Invariant_boundaries::get_graph(_inst.second, path);
    I(lg);

    for (auto &nid : lg->forward()) {
      // FIXME: when testing with synopsys, bit gets merged into name, we need to
      // take that into account here.
      if (lg->get_wid(nid) == 0) {
        continue;
      }

      auto net_name = lg->get_node_wirename(nid);

      auto hierarchical_name = absl::StrCat(inst, net_name);

      Index_id    idx;
      WireName_ID wire_id;
      if (synth_graph->has_wirename(hierarchical_name)) {
        idx     = synth_graph->get_node_id(hierarchical_name);
        wire_id = synth_graph->get_wid(idx);

      } else {
        I(!synth_graph->has_wirename(("\\" + hierarchical_name).c_str()));
        continue;
      }

      for (int bit = 0; bit < synth_graph->get_bits(idx); bit++) {
        Net_ID id                      = std::make_pair(wire_id, bit);
        boundaries.invariant_cones[id] = Net_set();
        invariant_boundaries.insert(std::make_pair(id, idx));
      }
    }
  }

  for (auto &invar : invariant_boundaries) {
    Index_id idx = invar.second;
    uint32_t bit = invar.first.second;

    propagate_until_boundary(idx, bit);

    Node_bit nid_bit = std::make_pair(idx, bit);

    boundaries.invariant_cones[invar.first].insert(partial_endpoints[nid_bit].begin(), partial_endpoints[nid_bit].end());
    boundaries.invariant_cone_cells[invar.first].insert(partial_cone_cells[nid_bit].begin(), partial_cone_cells[nid_bit].end());

    for (auto &cell : partial_cone_cells[nid_bit]) {
      if (boundaries.gate_appearances.find(cell) == boundaries.gate_appearances.end()) {
        boundaries.gate_appearances[cell] = 0;
      }
      boundaries.gate_appearances[cell]++;
    }
  }
}
