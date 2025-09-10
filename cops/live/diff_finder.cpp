//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "diff_finder.hpp"

#include <stdlib.h>

#include <format>
#include <fstream>
#include <iostream>

#include "absl/strings/substitute.h"
#include "graph_library.hpp"
#include "lgedgeiter.hpp"
#include "pass.hpp"

using namespace Live;  // REVIEW: NOT so nice to use "using namespace"

Diff_finder::Diff_finder(Live_pass_options pack) {
  std::ifstream invariant_file(pack.boundaries_name);
  if (!invariant_file.good()) {
    Pass::error("Diff_finder: Error reading boundaries file {}", pack.boundaries_name);
    return;
  }

  try {
    boundaries = Invariant_boundaries::deserialize(invariant_file);
  } catch (const std::exception &e) {
    Pass::warn("Diff_finder: There was an error de-serializing the boundaries file {}", pack.boundaries_name);
  }
  invariant_file.close();

  original = Lgraph_open(pack.original_lgdb, boundaries->top);
  synth    = Lgraph_open(pack.synth_lgdb, boundaries->top);

  if (!original) {
    Pass::error("Diff_finder: not able to open original netlist {} in {}", boundaries->top, pack.original_lgdb);
    return;
  }
  if (!synth) {
    Pass::error("Diff_finder: not able to open synthesized netlist {} in {}", boundaries->top, pack.synth_lgdb);
    return;
  }
}

auto Diff_finder::go_up(const Graph_Node &boundary) {
  Graph_Node bound;
  bound.module             = nullptr;
  std::string current_inst = boundary.instance;

  for (auto &parentID : boundaries->hierarchy_tree[Invariant_boundaries::get_graphID(boundary.module)]) {
    Lgraph *parent = Invariant_boundaries::get_graph(parentID, boundary.module->get_path());
    if (!parent) {
      parent = Invariant_boundaries::get_graph(parentID, original->get_path());
    }

    I(parent);

    for (auto &parent_instance : boundaries->instance_collection[parentID]) {
      // FIXME: can we avoid the string comparison here?
      if (current_inst.substr(0, parent_instance.size()) != parent_instance) {
        continue;
      }

      std::string sub_instance;
      if (parent_instance != "") {
        sub_instance = current_inst.substr(parent_instance.size() + hier_sep.size());
      } else {
        sub_instance = current_inst;
      }

      I(parent->has_instance_name(sub_instance.c_str()));

      id parent_node = parent->get_node_from_instance_name(sub_instance.c_str());

      I(bound.module == nullptr);
      bound.module   = parent;
      bound.idx      = parent_node;
      bound.bit      = boundary.bit;
      bound.instance = parent_instance;
      bound.pid      = boundary.module->get_graph_pid_from_nid(boundary.idx);
      I(boundary.module->has_graph_input(boundary.idx) || boundary.module->has_graph_output(boundary.idx));
    }
  }
  I(bound.module != nullptr);
  return bound;
}

auto Diff_finder::go_down(const Graph_Node &boundary, bool output) {
  Graph_Node bound;

  Lgraph *current = boundary.module;
  id      idx     = boundary.idx;
  Port_ID pid     = boundary.pid;

  auto    subgraph_name = current->get_library().get_name(current->subgraph_id_get(idx));
  Lgraph *child         = Lgraph_open(subgraph_name, current->get_path());

  if (!child) {
    child = Lgraph_open(subgraph_name, original->get_path());
  }

  I(child);

  bound.module = child;

  bound.instance.clear();
  if (boundary.instance != "") {
    bound.instance = boundary.instance + hier_sep;
  }
  bound.instance.append(current->get_node_instancename(boundary.idx));

  Index_id nid = 0;
  if (output) {
    nid = child->get_graph_output_nid_from_pid(pid);
  } else {
    nid = child->get_graph_input_nid_from_pid(pid);
  }

  I(nid);  // pid provided not valid
  bound.idx = nid;
  bound.bit = boundary.bit;
  bound.pid = 0;  // IO pids always 0

  return bound;
}

void Diff_finder::find_fwd_boundaries(Graph_Node &start_boundary, std::set<Graph_Node> &discovered, bool went_up) {
  if (fwd_visited.find(start_boundary) != fwd_visited.end()) {
    return;
  }

  fwd_visited.insert(start_boundary);

  std::print("fwd bound {} {} {} {} {}\n",
             start_boundary.module->get_name(),
             start_boundary.idx,
             start_boundary.pid,
             start_boundary.bit,
             start_boundary.instance);

  stack.insert(start_boundary);

  id      idx     = start_boundary.idx;
  Lgraph *current = start_boundary.module;
  Port_ID pid     = start_boundary.pid;

  if (current->node_type_get(idx).op == SubGraph_Op && !went_up) {
    Graph_Node child = go_down(start_boundary, false);

    if (stack.find(child) == stack.end()) {
      find_fwd_boundaries(child, discovered);
      stack.erase(start_boundary);
      return;
    }
  } else if (current->has_graph_output(idx)) {
    if (start_boundary.instance == "") {
      // global output, we're done.
      stack.erase(start_boundary);
      return;
    }
    Graph_Node parent = go_up(start_boundary);

    if (stack.find(parent) == stack.end()) {
      find_fwd_boundaries(parent, discovered, true);
      stack.erase(start_boundary);
      return;
    }
  }

  for (auto &out : current->out_edges(idx)) {
    // FIXME: what other cases do we need to take into account for propagation?
    if (current->node_type_get(idx).op == SubGraph_Op && out.get_out_pin().get_pid() != pid) {
      continue;
    }
    id next = current->get_node(out.get_inp_pin()).get_nid();

    absl::flat_hash_set<uint32_t> relevant_bits;
    int found = resolve_bit_fwd(current, next, start_boundary.bit, out.get_inp_pin().get_pid(), relevant_bits);
    if (found < 0) {
      continue;
    }

    for (uint32_t bit : relevant_bits) {
      Graph_Node bound(current, next, bit, start_boundary.instance, out.get_inp_pin().get_pid());
      if (set_invariant(bound)) {
        discovered.insert(bound);
      } else {
        if (stack.find(bound) == stack.end()) {
          find_fwd_boundaries(bound, discovered);
        }
      }
    }
  }

  stack.erase(start_boundary);
}

bool Diff_finder::is_user_def(Lgraph *current, id idx, Port_ID pid) const {
  return current->has_graph_output(idx) || current->has_graph_input(idx) || (current->get_wid(idx) != 0);
}

bool Diff_finder::set_invariant(Graph_Node node) {
  if (node.module->get_wid(node.idx) == 0) {
    return false;
  }

  uint32_t bit      = node.bit;
  auto     instance = node.instance;
  auto     net_name = node.module->get_node_wirename(node.idx);

  std::string hierarchical_name;
  if (instance.empty()) {
    hierarchical_name = net_name;
  } else {
    hierarchical_name = absl::StrCat(instance, hier_sep, net_name);
  }

  id          synth_idx;
  WireName_ID wire_id;
  if (!synth->has_wirename(hierarchical_name.c_str())) {
    return false;
  }

  synth_idx = synth->get_node_id(hierarchical_name.c_str());
  wire_id   = synth->get_wid(synth_idx);
  Net_ID id = std::make_pair(wire_id, bit);

  I(synth_map.find(node) == synth_map.end() || synth_map[node].first == 0 || synth_map[node] == id);
  synth_map[node] = id;
  bound2net[node] = net_name;

  return boundaries->is_invariant_boundary(id);
}

bool Diff_finder::compare_cone(const Graph_Node &start_boundary, const Graph_Node &original_boundary, bool went_up) {
  std::string instance = start_boundary.instance;

  if (different.find(start_boundary) != different.end()) {
    I(cones.find(start_boundary) != cones.end());
    I(endpoints.find(start_boundary) != endpoints.end());
    return different[start_boundary];
  }

  I(cones.find(start_boundary) == cones.end());
  I(endpoints.find(start_boundary) == endpoints.end());

  endpoints[start_boundary] = std::set<Graph_Node>();
  cones[start_boundary]     = std::set<Graph_Node>();
  different[start_boundary] = false;

  cones[start_boundary].insert(start_boundary);

  id      idx     = start_boundary.idx;
  Lgraph *current = start_boundary.module;
  Port_ID pid     = start_boundary.pid;

  if (current->get_wid(idx)) {
    std::print("{} {} {} {} {} \n", start_boundary.module->get_name(), idx, pid, instance, current->get_node_wirename(idx));
  } else {
    std::print("{} {} {} {} nowirename \n", start_boundary.module->get_name(), idx, pid, instance);
  }

  Lgraph *current_original = original_boundary.module;
  id      original_idx     = original_boundary.idx;

  stack.insert(start_boundary);

  I(current_original->node_type_get(original_idx).op != TechMap_Op);
  I(current->node_type_get(idx).op != TechMap_Op);

  if (current_original->node_type_get(original_idx).op != current->node_type_get(idx).op) {
    different[start_boundary] = true;
  }

  if (!went_up && current->node_type_get(idx).op == SubGraph_Op) {
    Graph_Node child = go_down(start_boundary, true);
    Graph_Node child_original;

    if (current_original->node_type_get(original_idx).op != SubGraph_Op
        || current->subgraph_id_get(idx) != current_original->subgraph_id_get(original_idx)) {
      different[start_boundary] = true;
      child_original            = original_boundary;
    } else {
      Graph_Node this_original(current_original, original_idx, start_boundary.bit, instance, pid);
      child_original = go_down(this_original, true);
    }

    cones[start_boundary].insert(child);

    bool diff = compare_cone(child, child_original);

    if (diff) {
      different[start_boundary] = true;
    }
    cones[start_boundary].insert(cones[child].begin(), cones[child].end());
    endpoints[start_boundary].insert(endpoints[child].begin(), endpoints[child].end());

    stack.erase(start_boundary);
    return different[start_boundary];
  } else if (current->has_graph_input(idx)
             && boundaries->hierarchy_tree[Invariant_boundaries::get_graphID(start_boundary.module)].size() > 0) {
    Graph_Node parent = go_up(start_boundary);
    Graph_Node parent_original;

    if (!current_original->has_graph_input(original_idx)
        || current->get_node_wirename(idx) != current_original->get_node_wirename(original_idx)) {
      different[start_boundary] = true;
      parent_original           = original_boundary;
    } else {
      // all pids on IO nodes is 0
      Graph_Node this_original(current_original, original_idx, start_boundary.bit, instance, 0);
      parent_original = go_up(this_original);
      // what about cases where module IOs change?
      I(parent.pid == parent_original.pid);
    }

    cones[start_boundary].insert(parent);
    bool diff = compare_cone(parent, parent_original, true);
    if (diff) {
      different[start_boundary] = true;
    }
    cones[start_boundary].insert(cones[parent].begin(), cones[parent].end());
    endpoints[start_boundary].insert(endpoints[parent].begin(), endpoints[parent].end());

    stack.erase(start_boundary);
    return different[start_boundary];
  }

  for (auto &inp : current->inp_edges(idx)) {
    // in cases like join/pick we only propagate to a specific bit
    absl::flat_hash_set<uint32_t> useful_bits;
    int found_bit = resolve_bit(current, idx, start_boundary.bit, inp.get_inp_pin().get_pid(), useful_bits);
    if (found_bit == -1) {  // do not propagate through this pid
      continue;
    }
    id previous = current->get_node(inp.get_out_pin()).get_nid();

    // for subgraphs, only propagate through the input we exited through
    if (pid != inp.get_inp_pin().get_pid() && start_boundary.module->node_type_get(start_boundary.idx).op == SubGraph_Op) {
      continue;
    }

    Port_ID n_pid = inp.get_inp_pin().get_pid();

    // just assign a valid idx while we look for the right one
    id   previous_original = original_idx;
    bool found             = false;

    if (!different[start_boundary]) {
      // if the cone is already different, don't bother comparing it anymore
      // just propagate over the new graph
      for (auto &cinp : current_original->inp_edges(original_idx)) {
        if (cinp.get_inp_pin().get_pid() == n_pid) {
          if (!found) {
            previous_original = current->get_node(cinp.get_out_pin()).get_nid();
          } else {
            // there is more than one edge to the same pid, like Sum
            // let's try to figure out which one we need to follow
            if (previous == previous_original) {
              continue;
            } else if (previous == current_original->get_node(cinp.get_out_pin()).get_nid()) {
              previous_original = current->get_node(cinp.get_out_pin()).get_nid();
            } else if (std::abs(static_cast<int64_t>(previous) - static_cast<int64_t>(previous_original))
                       < std::abs(static_cast<int64_t>(previous)
                                  - static_cast<int64_t>(current->get_node(cinp.get_out_pin()).get_nid()))) {
              previous_original = current->get_node(cinp.get_out_pin()).get_nid();
            } else if (std::abs(static_cast<int64_t>(previous) - static_cast<int64_t>(previous_original))
                       > std::abs(static_cast<int64_t>(previous)
                                  - static_cast<int64_t>(current->get_node(cinp.get_out_pin()).get_nid()))) {
              continue;
            } else {
              // is there any way to distinguish?
              // at this point any bet is as good as any.
              // Taking the wrong fan in may cause more mismatches than there
              // are, but it should be functionally correct
              continue;
            }
          }
          found = true;
        }
      }
      if (!found) {
        different[start_boundary] = true;
      }
    }

    for (uint32_t bit : useful_bits) {
      // some operators like Sum and Mult may have inputs with different
      // bitwidths
      if (current->get_bits_pid(previous, inp.get_out_pin().get_pid()) < bit) {
        continue;
      }

      Graph_Node bound(current, previous, bit, instance, inp.get_out_pin().get_pid());
      Graph_Node orig(current_original, previous_original, bit, instance, inp.get_out_pin().get_pid());

      if (set_invariant(bound)) {
        endpoints[start_boundary].insert(bound);
        // cones[start_boundary].insert(bound);
      } else {
        cones[start_boundary].insert(bound);

        if (stack.find(bound) == stack.end()) {
          bool diff = compare_cone(bound, orig);
          if (diff) {
            different[start_boundary] = true;
          }
          cones[start_boundary].insert(cones[bound].begin(), cones[bound].end());
          endpoints[start_boundary].insert(endpoints[bound].begin(), endpoints[bound].end());
        }
      }
    }
  }

  stack.erase(start_boundary);
  return different[start_boundary];
}

void Diff_finder::add_ios_up(Lgraph *module, const Node_pin &io_pin, Name2graph_type &name2graph) {
  for (auto &parent : boundaries->hierarchy_tree[Invariant_boundaries::get_graphID(module)]) {
    I(parent == "Lgraph_");

    // not in delta, do nothing
    if (name2graph.find(parent) == name2graph.end()) {
      continue;
    }

    Lgraph *nparent = name2graph[parent];

    // FIXME: inefficient solution, it would be better to be able to get the node
    // from a map or something like that
    std::setid > parent_ids;
    auto module_name = module->get_name();
    I(!absl::StartsWith(module_name, "Lgraph_"));

    for (auto node : nparent->fast()) {
      if (nparent->node_type_get(node).op != SubGraph_Op) {
        continue;
      }

      uint32_t subgraph_id = nparent->subgraph_id_get(node);

      if (subgraph_id == nparent->get_library().get_id(module_name)) {
        parent_ids.insert(node);
      }
    }

    I(parent_ids.size() > 0);

    Node_pin p_pin;
    if (module->has_graph_input(io_pin.get_idx())) {
      for (auto idx_in_parent : parent_ids) {
        I(nparent->get_instance_name_id(idx_in_parent) != 0);
        auto wire_name = absl::StrCat("Lgraph_hier_",
                                      nparent->get_node_instancename(idx_in_parent),
                                      hier_sep,
                                      module->get_node_wirename(io_pin.get_idx()));
        if (nparent->has_graph_input(wire_name)) {
          std::print("input {} already exists in parent module {}\n", wire_name, nparent->get_name());
          return;
        }
        auto dpin = nparent->add_graph_input(wire_name, module->get_bits(io_pin), module->get_offset(io_pin));
        nparent->add_edge(dpin, nparent->get_node(idx_in_parent).setup_sink_pin(dpin.get_pid()));
        p_pin = dpin;
      }

    } else {
      I(module->has_graph_output(io_pin.get_idx()));
      for (auto idx_in_parent : parent_ids) {
        I(nparent->get_instance_name_id(idx_in_parent) != 0);
        auto wire_name = absl::StrCat("Lgraph_hier_",
                                      nparent->get_node_instancename(idx_in_parent),
                                      hier_sep,
                                      module->get_node_wirename(io_pin.get_idx()));
        if (nparent->has_graph_output(wire_name)) {
          std::print("output {} already exists in parent module {}\n", wire_name, nparent->get_name());
          return;
        }
        auto spin = nparent->add_graph_output(wire_name, module->get_bits(io_pin), module->get_offset(io_pin));
        nparent->add_edge(nparent->get_node(idx_in_parent).setup_driver_pin(spin.get_pid()), spin);
        p_pin = spin;
      }
    }
    add_ios_up(nparent, p_pin, name2graph);
  }
}

void Diff_finder::generate_modules(std::set<Graph_Node> &different_nodes, const std::string &out_lgdb) {
  Name2graph_type          name2graph;
  std::map<Graph_Node, id> node2idx;

  std::map < Lgraph *, std::mapid, id >> old2newidx;

  std::set<std::pair<Lgraph *, id>> visited_idx;

  for (auto node : different_nodes) {
    Lgraph *original = node.module;

    // FIXME: this is happening due to multibit nodes, is there a way to prevent it?
    if (visited_idx.find(std::make_pair(original, node.idx)) != visited_idx.end()) {
      continue;
    }
    visited_idx.insert(std::make_pair(original, node.idx));
    if (name2graph.find(original->get_name()) == name2graph.end()) {
      auto name   = original->get_name();
      auto source = original->get_library().get_source(name);
      std::print("creating graph for {}\n", name);

      I(name.substr(0, 6) != "Lgraph");

      name2graph[original->get_name()] = Lgraph_create(out_lgdb, name, source);
    }
    Lgraph *new_module = name2graph[original->get_name()];

    Node_pin pin;
    id       idx = 0;
    if (original->has_graph_input(node.idx)) {
      if (!new_module->has_graph_input(original->get_node_wirename(node.idx))) {
        pin = new_module->add_graph_input(original->get_node_wirename(node.idx),
                                          original->get_bits(node.idx),
                                          original->get_offset(node.idx));
      } else {
        // input already created
        pin = new_module->get_graph_input(original->get_node_wirename(node.idx));
      }
      idx = pin.get_idx();
    } else if (original->has_graph_output(node.idx)) {
      if (!new_module->has_graph_output(original->get_node_wirename(node.idx))) {
        pin = new_module->add_graph_output(original->get_node_wirename(node.idx),
                                           original->get_bits(node.idx),
                                           original->get_offset(node.idx));
      } else {
        // output already created
        pin = new_module->get_graph_output(original->get_node_wirename(node.idx));
      }
      idx = pin.get_idx();
    } else {
      idx = new_module->create_node().get_nid();  // FIXME: This code assumes that driver pin is 0!!!

      if (original->get_instance_name_id(node.idx) != 0) {
        new_module->set_node_instance_name(idx, original->get_node_instancename(node.idx));
      }

      if (original->get_wid(node.idx) != 0) {
        new_module->set_node_wirename(idx, original->get_node_wirename(node.idx));
      }

      if (original->node_type_get(node.idx).op < GraphIO_Op || original->node_type_get(node.idx).op == BlackBox_Op) {
        new_module->node_type_set(idx, original->node_type_get(node.idx).op);
        new_module->set_bits(idx, original->get_bits(node.idx));
      } else {
        switch (original->node_type_get(node.idx).op) {
          case SubGraph_Op:
            // I(name2graph.find(original->get_subgraph_name(node.idx)) != name2graph.end());
            new_module->node_sub_set(idx, original->subgraph_id_get(node.idx));
            break;
          case TechMap_Op:
            new_module->node_tmap_set(idx, original->tmap_id_get(node.idx));
            new_module->set_bits(idx, original->get_bits(node.idx));
            break;
          case U32Const_Op: new_module->node_u32type_set(idx, original->node_value_get(node.idx)); break;
          case StrConst_Op: new_module->node_const_type_set(idx, original->node_const_value_get(node.idx)); break;
          default:
            Pass::error("Diff_finder::generate_modules: node type unrecognized {}", original->node_type_get(node.idx).op);
            break;
        }
      }
    }
    node2idx[node]                 = idx;
    old2newidx[original][node.idx] = idx;
  }

  std::set<std::pair<Lgraph *, int>> visited;
  std::map<Lgraph *, int>            wire_id;
  for (auto &node : different_nodes) {
    Lgraph *new_module = name2graph[node.module->get_name()];
    id      idx        = node2idx[node];

    // this skips multiple bits from the same node
    if (visited.find(std::make_pair(node.module, node.idx)) != visited.end()) {
      continue;
    }

    visited.insert(std::make_pair(node.module, node.idx));

    for (auto &inp : node.module->inp_edges(node.idx)) {
      if (old2newidx[node.module].find(node.module->get_node(inp.get_out_pin()).get_nid()) != old2newidx[node.module].end()) {
        // input included in delta
        Port_ID outpid = inp.get_out_pin().get_pid();
        Port_ID inppid = inp.get_inp_pin().get_pid();
        if (new_module->has_graph_input(old2newidx[node.module][node.module->get_node(inp.get_out_pin()).get_nid()])
            || new_module->has_graph_output(old2newidx[node.module][node.module->get_node(inp.get_out_pin()).get_nid()])) {
          outpid = 0;
        }
        if (new_module->has_graph_input(idx) || new_module->has_graph_output(idx)) {
          inppid = 0;
        }
        if (new_module->node_type_get(old2newidx[node.module][node.module->get_node(inp.get_inp_pin()).get_nid()]).op
            == SubGraph_Op) {
          // edge load is a subgraph
          const auto sub_id
              = new_module->subgraph_id_get(old2newidx[node.module][node.module->get_node(inp.get_inp_pin()).get_nid()]);
          const auto subgraph_name = new_module->get_library().get_name(sub_id);
          I(name2graph.find(subgraph_name) != name2graph.end());
          Lgraph *nsubgraph = name2graph[subgraph_name];
          Lgraph *osubgraph = Lgraph_open(subgraph_name, node.module->get_path());
          id      inpnid    = osubgraph->get_graph_input_nid_from_pid(inp.get_inp_pin().get_pid());
          I(inpnid);

          if (nsubgraph->has_graph_input(osubgraph->get_node_wirename(inpnid))) {
            inppid = nsubgraph->get_graph_input(osubgraph->get_node_wirename(inpnid)).get_pid();
          } else {
            // input port (load) not present in the delta graph, can safely skip
            continue;
          }
        }
        if (new_module->node_type_get(old2newidx[node.module][node.module->get_node(inp.get_out_pin()).get_nid()]).op
            == SubGraph_Op) {
          // edge driver is a subgraph
          const auto sub_id
              = new_module->subgraph_id_get(old2newidx[node.module][node.module->get_node(inp.get_out_pin()).get_nid()]);
          const auto subgraph_name = new_module->get_library().get_name(sub_id);
          I(name2graph.find(subgraph_name) != name2graph.end());
          Lgraph *nsubgraph = name2graph[subgraph_name];
          Lgraph *osubgraph = Lgraph_open(subgraph_name, node.module->get_path());
          id      outnid    = osubgraph->get_graph_output_nid_from_pid(inp.get_out_pin().get_pid());

          I(outnid);
          if (nsubgraph->has_graph_output(osubgraph->get_node_wirename(outnid))) {
            outpid = nsubgraph->get_graph_output(osubgraph->get_node_wirename(outnid)).get_pid();

            // FIXME: refactor to reduce code replication
          } else {
            if (node.module->node_type_get(idx).op == GraphIO_Op) {
              continue;
            }

            // driver submodule is present in delta, but driver pin is not,
            // promote load input to module input
            id name_idx = inp.get_out_pin().get_idx();
            I(osubgraph->get_graph_output_nid_from_pid(inp.get_out_pin().get_pid()));
            I(!new_module->has_graph_output(osubgraph->get_graph_output_name_from_pid(inp.get_inp_pin().get_pid())));

            Node_pin inp_pin;
            if (new_module->has_graph_input(osubgraph->get_graph_output_name_from_pid(inp.get_out_pin().get_pid()))) {
              // node has previously been promoted
              inp_pin = new_module->get_graph_input(osubgraph->get_graph_output_name_from_pid(inp.get_out_pin().get_pid()));
            } else {
              inp_pin = new_module->add_graph_input(
                  osubgraph->get_graph_output_name_from_pid(inp.get_out_pin().get_pid()),
                  osubgraph->get_bits(osubgraph->get_graph_output_nid_from_pid(inp.get_out_pin().get_pid())),
                  0);
              add_ios_up(new_module, inp_pin, name2graph);
              I(old2newidx[node.module].find(name_idx) == old2newidx[node.module].end());
              old2newidx[node.module][name_idx] = inp_pin.get_idx();
            }
            new_module->add_edge(inp_pin, new_module->get_node(idx).setup_sink_pin(inppid));
            continue;
          }
        }
        Node_pin dpin = new_module->get_node(old2newidx[node.module][node.module->get_node(inp.get_out_pin()).get_nid()])
                            .setup_driver_pin(outpid);
        Node_pin spin = new_module->get_node(idx).setup_sink_pin(inppid);
        new_module->add_edge(dpin, spin);

      } else {
        Port_ID subgraph_inpid = inp.get_inp_pin().get_pid();
        if (node.module->node_type_get(node.idx).op == SubGraph_Op) {
          const auto sub_id
              = new_module->subgraph_id_get(old2newidx[node.module][node.module->get_node(inp.get_inp_pin()).get_nid()]);
          const auto subgraph_name = new_module->get_library().get_name(sub_id);
          I(name2graph.find(subgraph_name) != name2graph.end());
          Lgraph *osubgraph = Lgraph_open(subgraph_name, node.module->get_path());
          Lgraph *nsubgraph = name2graph[subgraph_name];

          id subgraph_innid = osubgraph->get_graph_input_nid_from_pid(inp.get_inp_pin().get_pid());

          if (old2newidx.find(osubgraph) == old2newidx.end()
              || old2newidx[osubgraph].find(subgraph_innid) == old2newidx[osubgraph].end()) {
            // for subgraphs we only want to go over inputs that are in the
            // resynthesis region
            continue;
          }

          subgraph_inpid = nsubgraph->get_graph_input(osubgraph->get_node_wirename(subgraph_innid)).get_pid();
        }

        // FIXME refactor to reduce code replication
        if (node.module->node_type_get(node.module->get_node(inp.get_out_pin()).get_nid()).op == U32Const_Op) {
          // node not included but simple constant
          id const_id = new_module->create_node().get_nid();
          new_module->node_u32type_set(const_id, node.module->node_value_get(node.module->get_node(inp.get_out_pin()).get_nid()));
          if (new_module->has_graph_output(idx)) {
            new_module->add_edge(new_module->get_node(const_id).setup_driver_pin(), new_module->get_node(idx).setup_sink_pin(idx));
          } else {
            new_module->add_edge(new_module->get_node(const_id).setup_driver_pin(),
                                 new_module->get_node(idx).setup_sink_pin(subgraph_inpid));
          }
        } else if (node.module->node_type_get(node.module->get_node(inp.get_out_pin()).get_nid()).op == StrConst_Op) {
          // node not included but simple constant
          id const_id = new_module->create_node().get_nid();
          new_module->node_const_type_set(const_id,
                                          node.module->node_const_value_get(node.module->get_node(inp.get_out_pin()).get_nid()));
          if (new_module->has_graph_output(idx)) {
            new_module->add_edge(new_module->get_node(const_id).setup_driver_pin(), new_module->get_node(idx).setup_sink_pin(idx));
          } else {
            new_module->add_edge(new_module->get_node(const_id).setup_driver_pin(),
                                 new_module->get_node(idx).setup_sink_pin(subgraph_inpid));
          }

        } else {
          // input not included in delta, promote this node to input
          // for joins, we can skip remaining wires
          if (node.module->node_type_get(node.idx).op == Join_Op || set_invariant(node)) {
            continue;
          }

          id name_idx = inp.get_out_pin().get_idx();
          I(node.module->get_wid(name_idx) != 0);
          auto inp_pin = new_module->add_graph_input(node.module->get_node_wirename(name_idx), node.module->get_bits(name_idx), 0);
          add_ios_up(new_module, inp_pin, name2graph);
          I(old2newidx[node.module].find(name_idx) == old2newidx[node.module].end());
          old2newidx[node.module][name_idx] = inp_pin.get_idx();
          if (new_module->has_graph_output(idx)) {
            new_module->add_edge(inp_pin, new_module->get_node(idx).setup_sink_pin(idx));
          } else {
            new_module->add_edge(inp_pin, new_module->get_node(idx).setup_sink_pin(subgraph_inpid));
          }
        }
      }
    }

    // if there is at least one output from the delta region to a node outside
    // the delta region, this node needs to be made an output
    for (auto &out : node.module->out_edges(node.idx)) {
      if (node.module->node_type_get(node.idx).op == SubGraph_Op) {
        // if node is a subgraph, I need to first check if the output port is in the delta
        const std::string subgraph_name(
            new_module->get_library().get_name(new_module->subgraph_id_get(old2newidx[node.module][node.idx])));
        I(name2graph.find(subgraph_name) != name2graph.end());
        Lgraph *nsubgraph = name2graph[subgraph_name];
        Lgraph *osubgraph = Lgraph_open(subgraph_name, node.module->get_path());
        if (!nsubgraph->has_graph_output(osubgraph->get_graph_output_name_from_pid(out.get_out_pin().get_pid()))) {
          continue;
        }
      }
      if (node.module->has_graph_input(node.idx) || node.module->has_graph_output(node.idx)) {
        continue;
      }
      if (old2newidx[node.module].find(node.module->get_node(out.get_inp_pin()).get_nid()) == old2newidx[node.module].end()) {
        std::print("module {}, from cell {}, to_cell {}, from_pin {}, to pin {}\n",
                   node.module->get_name(),
                   node.module->get_node(out.get_out_pin()).get_nid(),
                   node.module->get_node(out.get_inp_pin()).get_nid(),
                   out.get_out_pin().get_pid(),
                   out.get_inp_pin().get_pid());

        std::string_view wirename;
        if (node.module->get_wid(node.idx) != 0) {
          // wire has user defined name
          I(new_module->has_wirename(
              node.module->get_node_wirename(node.idx)));  // wirename should already be in graph unless we messed up something
          wirename = absl::StrCat(node.module->get_node_wirename(node.idx), "_output");
        } else {
          // wire does not have user defined name
          wire_id[new_module] = wire_id[new_module] + 1;
          wirename            = absl::StrCat("__Lgraph__generated_wire_",
                                  new_module->get_name(),
                                  "__driver__",
                                  std::to_string(node.module->get_node(out.get_inp_pin()).get_nid()),
                                  "__pin__",
                                  std::to_string(out.get_inp_pin().get_pid()),
                                  "__id__",
                                  std::to_string(wire_id[new_module]));
          I(!new_module->has_graph_output(wirename));
        }

        auto out_pin = new_module->add_graph_output(wirename, node.module->get_bits(node.idx), 0);
        add_ios_up(new_module, out_pin, name2graph);
        old2newidx[node.module][node.module->get_node(out.get_out_pin()).get_nid()] = out_pin.get_idx();
        if (new_module->has_graph_input(idx) || new_module->has_graph_output(idx)) {
          new_module->add_edge(new_module->get_node(idx).setup_driver_pin(), out_pin);
        } else {
          new_module->add_edge(new_module->get_node(idx).setup_driver_pin(out.get_out_pin().get_pid()), out_pin);
        }

        // only one output per node is needed
        break;
      }
    }
  }

  // sync all graphs to disk
  for (auto &name_graph : name2graph) {
    std::print("sync'ing graph {}\n", name_graph.first);
    name_graph.second->sync();
  }
}

void Diff_finder::generate_delta(const std::string &modified_lgdb, const std::string &out_lgdb, std::set<Net_ID> &diffs) {
  std::set<Lgraph *>   discovered_modules;
  std::set<Graph_Node> discovered_boundaries, visited_boundaries, all_diff;

  Graph_library *modified_library = Graph_library::instance(modified_lgdb);
#if 0
  for(int id = 0; id < modified_library->Lgraph_count(); id++) {
    I(modified_library->get_graph(id));
    discovered_modules.insert(modified_library->get_graph(id));
    std::print("discovered module {} \n", modified_library->get_graph(id)->get_name());
  }
#else
  modified_library->each_type([&discovered_modules, modified_lgdb](Lg_type_id id, std::string_view name) {
    Lgraph *lg = Lgraph_open(modified_lgdb, name);
    I(lg);
    discovered_modules.insert(lg);
    std::print("discovered module {} \n", name);
  });
#endif

  while (discovered_modules.size() > 0) {
    Lgraph *current = *(discovered_modules.begin());
    discovered_modules.erase(current);

    std::print("current module {} \n", current->get_name());

    for (auto &instance : boundaries->instance_collection[Invariant_boundaries::get_graphID(current)]) {
      for (auto &ridx : current->fast()) {
        std::set<Port_ID> out_pids;
        if (current->has_graph_output(ridx)) {
          // graph outputs may not have out_edges
          out_pids.insert(0);
        } else {
          for (auto &cout : current->out_edges(ridx)) {
            out_pids.insert(cout.get_out_pin().get_pid());
          }
        }

        for (Port_ID pid : out_pids) {
          auto dpin = current->get_node(ridx).get_driver_pin(pid);

          if (!is_user_def(current, ridx, pid)) {
            continue;
          }

          for (int bit = 0; bit < current->get_bits(dpin); bit++) {
            Graph_Node bound(current, ridx, bit, instance, pid);

            I(current->get_name().substr(7) != "Lgraph_");
            Lgraph *current_original = Lgraph_open(current->get_name(), original->get_path());
            I(current_original);

            if (set_invariant(bound)) {
              visited_boundaries.insert(bound);

              Graph_Node orig(current_original, current_original->get_node_id(bound2net[bound]), bit, instance, pid);
              bool       diff = compare_cone(bound, orig);
              I(stack.size() == 0);
              visited_boundaries.insert(bound);

              if (diff) {
#ifdef DEBUG
                for (auto foo : cones[bound]) {
                  std::print("    diff mod {} {}:{}\n", foo.module->get_name(), foo.idx, foo.pid);
                }
#endif
                all_diff.insert(cones[bound].begin(), cones[bound].end());
                diffs.insert(synth_map[bound]);
              }

            } else if (current->has_graph_output(dpin)) {
              // propagate fwd from non-boundary outputs to get the next boundary
              find_fwd_boundaries(bound, discovered_boundaries);
            }
          }
        }
      }
    }
  }

  for (auto &bound : discovered_boundaries) {
    if (visited_boundaries.find(bound) == visited_boundaries.end()) {
      visited_boundaries.insert(bound);

      I(bound.module->get_name().substr(7) != "Lgraph_");
      Lgraph *current_original = Lgraph_open(bound.module->get_name(), original->get_path());
      I(current_original);

      Graph_Node orig(current_original, current_original->get_node_id(bound2net[bound]), bound.bit, bound.instance, bound.pid);

      bool diff = compare_cone(bound, orig);
      I(stack.size() == 0);
      visited_boundaries.insert(bound);
      if (diff) {
        all_diff.insert(cones[bound].begin(), cones[bound].end());
        diffs.insert(synth_map[bound]);
      }
    }
  }

#ifdef DEBUG
  for (auto foo : all_diff) {
    std::print("alldiff mod {}\n", foo.module->get_name());
  }
#endif
  generate_modules(all_diff, out_lgdb);
}
