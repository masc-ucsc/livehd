//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "invariant_finder.hpp"
#include "lgedgeiter.hpp"

#include "live_common.hpp"

using namespace Live;

void Invariant_finder::get_topology() {

  std::vector<LGraph *> discovered;
  std::set<LGraph *>    visited;

  discovered.push_back(elab_graph);

  //top module
  boundaries.instance_collection[Invariant_boundaries::get_graphID(elab_graph)].insert("");
  boundaries.instance_type_map["##TOP##"] = Invariant_boundaries::get_graphID(elab_graph);
  boundaries.top                          = elab_graph->get_name();

  while(discovered.size()) {
    LGraph *current = discovered.back();
    discovered.pop_back();
    if(visited.find(current) != visited.end()) {
      continue;
    }

    visited.insert(current);

    for(auto &idx : current->fast()) {

      //filter out primitives, we're only interested in user defined modules
      if(current->node_type_get(idx).op != SubGraph_Op)
        continue;

      LGraph *subgraph = LGraph::open(current->get_path(), current->get_subgraph_name(idx));
      assert(subgraph);

      boundaries.hierarchy_tree[Invariant_boundaries::get_graphID(subgraph)].insert(Invariant_boundaries::get_graphID(current));

      for(auto &prefix : boundaries.instance_collection[Invariant_boundaries::get_graphID(current)]) {
        std::string instance_name;
        if(current->get_instance_name_id(idx) == 0) {
#ifdef DEBUG
          console->info("RTP got node with no instance name {}\n", idx);
#endif
          continue;
        }

        if(prefix != "") {
          instance_name = prefix + boundaries.hierarchical_separator + current->get_node_instancename(idx);
        } else {
          instance_name = current->get_node_instancename(idx);
        }
        boundaries.instance_collection[Invariant_boundaries::get_graphID(subgraph)].insert(instance_name);
        boundaries.instance_type_map[instance_name] = Invariant_boundaries::get_graphID(subgraph);
      }

      if(visited.find(subgraph) == visited.end()) {
        discovered.push_back(subgraph);
      }
    }
  }
}

void Invariant_finder::propagate_until_boundary(Index_ID nid, uint32_t bit_selection) {

  Node_bit nid_bit = std::make_pair(nid, bit_selection);

  assert(synth_graph->get_bits(nid) > bit_selection);

  Index_ID master_id = synth_graph->get_master_nid(nid);
  if(synth_graph->is_graph_input(nid) ||
     synth_graph->node_type_get(master_id).op == U32Const_Op ||
     synth_graph->node_type_get(master_id).op == StrConst_Op) {
    return;
  }

  if(partial_endpoints.find(nid_bit) != partial_endpoints.end()) {
    return;
  }

  Node_bit mnid_bit = std::make_pair(master_id, bit_selection);
  if(partial_endpoints.find(mnid_bit) != partial_endpoints.end()) {
    partial_endpoints[nid_bit] = partial_endpoints[mnid_bit];
    partial_cone_cells[nid_bit] = partial_cone_cells[mnid_bit];
    cached.insert(nid_bit);
    return;
  }

  assert(partial_endpoints.find(nid_bit) == partial_endpoints.end());
  assert(partial_cone_cells.find(nid_bit) == partial_cone_cells.end());

#ifndef NDEBUG
  assert(deleted.find(nid_bit) == deleted.end());
#endif

  partial_endpoints[nid_bit]  = Net_set();
  partial_cone_cells[nid_bit] = Gate_set();

  stack.set_bit(nid);

  for(auto &edge : synth_graph->inp_edges(master_id)) {

    //in cases like join/pick we only propagate to a specific bit
    std::set<uint32_t> bit_selections;
    int                propagate = resolve_bit(synth_graph, nid, bit_selection, edge.get_inp_pin().get_pid(), bit_selections);
    if(propagate == -1)
      continue;

    for(uint32_t t_bit_selection : bit_selections) {
      Index_ID driver_cell = edge.get_out_pin().get_nid();
      Node_bit driver_bit  = std::make_pair(driver_cell, t_bit_selection);
      Net_ID   net_bit     = std::make_pair(synth_graph->get_wid(driver_cell), t_bit_selection);

      partial_cone_cells[nid_bit].insert(driver_cell);

      // FIXME: for registers don't propagate through clk, rst, enable
      // FIXME: for fflops propagate data to data, valid to valid and retry to retry only

      if(stack.get_bit(driver_cell)) {
        continue;
      }

      // no net name set, thus this is not a invariant boundary
      if(synth_graph->get_wid(driver_cell) == 0 ||
         !boundaries.is_invariant_boundary(net_bit)) {
        //recurse
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
  for(auto &edge : synth_graph->inp_edges(master_id)) {
    for(uint32_t t_bit_selection = 0; t_bit_selection < synth_graph->get_bits(edge.get_out_pin().get_nid()); t_bit_selection++) {
      Index_ID driver_cell = edge.get_out_pin().get_nid();
      Node_bit driver_bit  = std::make_pair(driver_cell, t_bit_selection);
      clear_cache(driver_bit);
    }
  }

  stack.clear_bit(nid);
}


void Invariant_finder::clear_cache(const Node_bit &entry) {

  if(partial_endpoints.find(entry) == partial_endpoints.end() || stack.get_bit(entry.first))
    return;

  for(auto & oedge : synth_graph->out_edges(entry.first)) {
    Index_ID port_id = synth_graph->get_idx_from_pid(entry.first, oedge.get_out_pin().get_pid());
    for(uint32_t bit = 0; bit < synth_graph->get_bits(port_id); bit++) {
      if(cached.find(std::make_pair(port_id, bit)) == cached.end())
        return;
    }

    for(uint32_t bit = 0; bit < synth_graph->get_bits(oedge.get_inp_pin().get_nid()); bit++) {
      if(cached.find(std::make_pair(oedge.get_inp_pin().get_nid(), bit)) == cached.end())
        return;
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

  std::map<Net_ID, Index_ID> invariant_boundaries;
  for(auto &_inst : boundaries.instance_type_map) {
    Instance_name inst = _inst.first;
    if(inst == "##TOP##")
      inst = "";
    else
      inst = inst + boundaries.hierarchical_separator;

    LGraph *lg = Invariant_boundaries::get_graph(_inst.second, path);
    assert(lg);

    for(auto &node : lg->forward()) {
      std::string net_name;

      //FIXME: when testing with synopsys, bit gets merged into name, we need to
      //take that into account here.
      if(lg->is_graph_output(node)) {
        net_name = lg->get_graph_output_name(node);
      } else if(lg->is_graph_input(node)) {
        net_name = lg->get_graph_input_name(node);
      } else if(lg->get_wid(node) != 0) {
        net_name = lg->get_node_wirename(node);
      } else {
        continue;
      }

      std::string hierarchical_name = inst + net_name;

      Index_ID    idx;
      WireName_ID wire_id;
      if(synth_graph->has_name(hierarchical_name.c_str())) {
        idx     = synth_graph->get_node_id(hierarchical_name.c_str());
        wire_id = synth_graph->get_wid(idx);

#ifndef NDEBUG
      } else {
        assert(!synth_graph->has_name(("\\" + hierarchical_name).c_str()));
#endif
        continue;
      }

      for(int bit = 0; bit < synth_graph->get_bits(idx); bit++) {
        Net_ID id                      = std::make_pair(wire_id, bit);
        boundaries.invariant_cones[id] = Net_set();
        invariant_boundaries.insert(std::make_pair(id, idx));
      }
    }
  }

  for(auto &invar : invariant_boundaries) {

    Index_ID idx = invar.second;
    uint32_t bit = invar.first.second;

    propagate_until_boundary(idx, bit);

    Node_bit nid_bit = std::make_pair(idx, bit);

    boundaries.invariant_cones[invar.first].insert(partial_endpoints[nid_bit].begin(), partial_endpoints[nid_bit].end());
    boundaries.invariant_cone_cells[invar.first].insert(partial_cone_cells[nid_bit].begin(), partial_cone_cells[nid_bit].end());

    for(auto &cell : partial_cone_cells[nid_bit]) {
      if(boundaries.gate_appearances.find(cell) == boundaries.gate_appearances.end()) {
        boundaries.gate_appearances[cell] = 0;
      }
      boundaries.gate_appearances[cell]++;
    }
  }
}
