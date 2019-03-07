//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>

#include "lgedgeiter.hpp"

#include "stitcher.hpp"

Live_stitcher::Live_stitcher(Stitch_pass_options &pack) {
  std::ifstream invariant_file(pack.boundaries_name);

  if (!invariant_file.good()) {
    Pass::error(fmt::format("Live_stitcher: Error reading boundaries file {}", pack.boundaries_name));
    return;
  }

  boundaries = Invariant_boundaries::deserialize(invariant_file);
  invariant_file.close();

  original = LGraph::open(pack.osynth_lgdb, boundaries->top);

  if (!original) {
    Pass::error(fmt::format("Live_stitcher: I was not able to open original synthesized netlist {} in {}", boundaries->top,
                            pack.osynth_lgdb));
  }
}

void Live_stitcher::stitch(LGraph *nsynth, const std::set<Net_ID> &diffs) {
  std::map<Index_ID, Index_ID> nsynth2originalid;
  std::map<Index_ID, Index_ID> inp2originalid;
  std::map<Index_ID, Index_ID> out2originalid;

  // add new cells
  for (auto &idx : nsynth->fast()) {
    if (nsynth->node_type_get(idx).op == GraphIO_Op) {
      // FIXME: how to check if there are new global IOs?
      // FIXME: how to check if I need to delete global IOs?

      auto name = nsynth->get_node_wirename(idx);

      if (original->is_graph_input(name)) {
        inp2originalid[idx] = original->get_graph_input(name).get_idx();
      } else if (original->is_graph_output(name)) {
        out2originalid[idx] = original->get_graph_output(name).get_idx();
      } else if (original->has_wirename(name)) {
        inp2originalid[idx] = original->get_node_id(name);
      } else {
        // Pass::>error("Wire {} not found in original synthesized graph\n",name);
      }

    } else {
      Index_ID nidx          = original->create_node().get_nid();
      nsynth2originalid[idx] = nidx;

      switch (nsynth->node_type_get(idx).op) {
        case TechMap_Op: original->node_tmap_set(nidx, nsynth->tmap_id_get(idx)); break;
        case Join_Op:
        case Pick_Op: original->node_type_set(nidx, nsynth->node_type_get(idx).op); break;
        case U32Const_Op: original->node_u32type_set(nidx, nsynth->node_value_get(idx)); break;
        case StrConst_Op: original->node_const_type_set(nidx, nsynth->node_const_value_get(idx)); break;

        default: Pass::error("live.stitcher: unsupported synthesized type");
      }
    }
  }

  // connect new cells
  for (auto &idx : nsynth->fast()) {
    if (!nsynth->is_graph_output(idx)) {
      for (auto &c : nsynth->inp_edges(idx)) {
        // if driver is in the delta region
        if (nsynth2originalid.find(nsynth->get_node(c.get_out_pin()).get_nid()) != nsynth2originalid.end()) {
          auto dnode = original->get_node(nsynth2originalid[nsynth->get_node(c.get_out_pin()).get_nid()]);
          auto snode = original->get_node(nsynth2originalid[nsynth->get_node(c.get_inp_pin()).get_nid()]);
          auto dpin  = dnode.setup_driver_pin(c.get_out_pin().get_pid());
          auto spin  = snode.setup_sink_pin(c.get_inp_pin().get_pid());

          original->add_edge(dpin, spin);
        } else {
          if (inp2originalid.find(idx) != inp2originalid.end()) {
            auto dnode = original->get_node(inp2originalid[idx]);
            auto snode = original->get_node(nsynth2originalid[nsynth->get_node(c.get_inp_pin()).get_nid()]);
            auto spin  = snode.setup_sink_pin(c.get_inp_pin().get_pid());
            auto dpin  = dnode.setup_driver_pin(); // FIXME: is there a case where I need to consider the out pid?
            original->add_edge(dpin, spin);
          }
        }
      }
    } else {
      if (out2originalid.find(idx) != out2originalid.end()) {
        // global output
        // FIXME: I need to consider the inp PID
        for (auto &c : nsynth->inp_edges(idx)) {
          Node_pin dpin = original->get_node(nsynth2originalid[nsynth->get_node(c.get_out_pin()).get_nid()]).setup_driver_pin(c.get_out_pin().get_pid());
          Node_pin spin = original->get_node(out2originalid[idx]).setup_sink_pin(out2originalid[idx]);
          original->add_edge(dpin, spin);
        }
      } else {
        // invariant boundary
        auto name = nsynth->get_node_wirename(idx);
        if (!original->has_wirename(name)) continue;
        Index_ID oidx = original->get_node_id(name);
        for (auto &edge : original->out_edges(oidx)) {
          Node_pin dpin = original->get_node(nsynth2originalid[idx]).setup_driver_pin(edge.get_out_pin().get_pid());

          original->add_edge(dpin, edge.get_inp_pin());
          original->del_edge(edge);
        }
      }
    }
  }

  // removed original graph
  for (auto &diff : diffs) {
    I(boundaries->invariant_cone_cells.find(diff) != boundaries->invariant_cone_cells.end());
    for (auto &gate : boundaries->invariant_cone_cells[diff]) {
      I(boundaries->gate_appearances.find(gate) != boundaries->gate_appearances.end());
      boundaries->gate_appearances[gate]--;
      if (boundaries->gate_appearances[gate] <= 0) {
        // original->del_node(gate);
      }
    }
  }

  original->sync();
}
