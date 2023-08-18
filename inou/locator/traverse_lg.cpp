//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "traverse_lg.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

#include "lgedgeiter.hpp"
#include "perf_tracing.hpp"

int                VISITED_COLORED = 401;
static Pass_plugin sample("traverse_lg", Traverse_lg::setup);

void Traverse_lg::setup() {
  Eprp_method m1("inou.traverse_lg", "Prints all nodes (and each of their IOs) of LG", &Traverse_lg::travers);
  // m1.add_label_optional("odir", "path to print the text to", ".");
  m1.add_label_required("LGorig", "LG name of the original or pre-synth LG.");
  m1.add_label_required("LGsynth", "LG name of synthesized or post-synth LG.");
  m1.add_label_optional("synth_tool", "if synth done via Design Compiler, then enter synth_tool:DC");
  register_pass(m1);
}

Traverse_lg::Traverse_lg(const Eprp_var& var) : Pass("inou.traverse_lg", var) {}

void Traverse_lg::travers(Eprp_var& var) {
  TRACE_EVENT("inou", "traverse_lg");

  auto lg_orig_name  = var.get("LGorig");
  auto lg_synth_name = var.get("LGsynth");

  Traverse_lg p(var);
  p.synth_tool   = var.get("synth_tool");
  p.orig_lg_name = std::string(lg_orig_name);
  // p.synth_lg_name = std::string(lg_synth_name);
#ifdef DE_DUP
  // Traverse_lg::setMap map_pre_synth;
  Traverse_lg::setMap_pairKey map_post_synth;
  bool                        first_done = false;
  Lgraph*                     synth_lg   = nullptr;
  Lgraph*                     orig_lg    = nullptr;
  for (const auto& l : var.lgs) {
    if (l->get_name() == lg_synth_name) {
      synth_lg   = l;
      first_done = true;
    }
  }
  I(first_done, "\nERROR:\n Synth LG not/incorrectly provided??\n");
  bool sec_done = false;
  for (const auto& l : var.lgs) {
    if (l->get_name() == lg_orig_name) {
      orig_lg  = l;
      sec_done = true;
    }
  }
  I(sec_done, "\nERROR:\n original LG not/incorrectly provided??\n");

  // p.debug_function(orig_lg);
  // p.debug_function(synth_lg);
  // return;
  p.make_io_maps_boundary_only(orig_lg, p.inp_map_of_sets_orig, p.out_map_of_sets_orig, true);  // orig-boundary only
  fmt::print("\n make_io_maps_boundary_only - orig done.\n");
#ifdef BASIC_DBG
  fmt::print("1. p.make_io_maps_boundary_only(orig_lg, p.inp_map_of_sets_orig, p.out_map_of_sets_orig)//orig-boundary only\n");
  p.print_everything();
#endif

  p.netpin_to_origpin_default_match(orig_lg, synth_lg);  // know all the inputs and outputs match by name (known points.)
  fmt::print("\n netpin_to_origpin_default_match done.\n");
#ifdef BASIC_DBG
  fmt::print(
      "2. p.netpin_to_origpin_default_match(orig_lg, synth_lg);//know all the inputs and outputs match by name (known points.)\n");
  p.print_everything();
#endif
  p.make_io_maps_boundary_only(synth_lg, p.inp_map_of_sets_synth, p.out_map_of_sets_synth, false);  // synth-boundary only +
                                                                                                    // matching
  fmt::print("\n make_io_maps_boundary_only - synth done.\n");
#ifdef BASIC_DBG
  fmt::print(
      "3. p.make_io_maps_boundary_only(synth_lg, p.inp_map_of_sets_synth, p.out_map_of_sets_synth);//synth-boundary only + "
      "matching\n");
  p.print_everything();
#endif
  p.do_travers(orig_lg, map_post_synth, true);  // original LG (pre-syn LG)
  fmt::print("\n do_travers - orig done.\n");
#ifdef BASIC_DBG
  fmt::print("4. p.do_travers(orig_lg, map_post_synth, true);  // original LG (pre-syn LG)\n");
  p.print_everything();
#endif
  p.do_travers(synth_lg, map_post_synth, false);  // synth LG
  fmt::print("\n do_travers - synth done.\n");
#ifdef BASIC_DBG
  fmt::print("\n5. p.do_travers(synth_lg, map_post_synth, false);  // synth LG\n");
  p.print_everything();
#endif

  I(p.crit_node_set.empty(), "\n***********\nCHECK:\n\t\t All marked nodes still not resolved??\n***********\n");
  p.report_critical_matches_with_color();

#endif
}

void Traverse_lg::debug_function(Lgraph* lg) {
  lg->dump(true);
  fmt::print("---------------------------------------------------\n");

  lg->each_graph_input([](const Node_pin dpin) {
    fmt::print("   {}({})\n", dpin.has_name() ? dpin.get_name() : (std::to_string(dpin.get_pid())), dpin.get_wire_name());
  });
  lg->each_graph_output([](const Node_pin dpin) {
    fmt::print("   {}({})\n", dpin.has_name() ? dpin.get_name() : (std::to_string(dpin.get_pid())), dpin.get_wire_name());
  });

  for (const auto& node : lg->fast(true)) {
    if (node.has_outputs()) {
      fmt::print("{}(n{})\n", node.debug_name(), node.get_nid());
      if (node.is_type_sub() && node.get_type_sub_node().get_name() == "__fir_const") {
        auto node_sub_name = node.get_type_sub_node().get_name();
        fmt::print("\t\t\t\t {}\n", node_sub_name);
      }
      for (const auto dpin : node.out_connected_pins()) {
        fmt::print("\t {}({})", dpin.has_name() ? dpin.get_name() : (std::to_string(dpin.get_pid())), dpin.get_wire_name());
      }
      fmt::print("\n");
    }
  }
  fmt::print("\n---------------------------------------------------");
}

// FOR SET:
// DE_DUP
void Traverse_lg::do_travers(Lgraph* lg, Traverse_lg::setMap_pairKey& nodeIOmap,
                             bool do_matching) {  // FIXME: change do_matching to is_orig_lg

  bool is_orig_lg        = do_matching;  // FIXME: remove this once do_matching changed to is_orig_lg
  bool req_flops_matched = false;
  bool dealing_flop      = false;
  bool dealing_comb      = false;

  if (!is_orig_lg) {
    make_io_maps(lg, inp_map_of_sets_synth, out_map_of_sets_synth, is_orig_lg);  // has in-place resolution as well.
    fmt::print("\n make_io_maps - synth done.\n");
#ifdef BASIC_DBG
    fmt::print("7.0. Printing before 1st set of resolution -- synth");
    print_everything();
#endif
    resolution_of_synth_map_of_sets(inp_map_of_sets_synth);
    resolution_of_synth_map_of_sets(out_map_of_sets_synth);
#ifdef BASIC_DBG
    fmt::print("7. printing before matching starts (after 1st resolution) -- synth");
    print_everything();
#endif
    /* everything in sets of synth_MoS must have been resolved to some orig lg reference.
       If not then need to know and debug.
       Hence this assertion:
    for (const auto & [k,v_set]: inp_map_of_sets_synth) {
      for (const auto & v : v_set) {
        auto lg_name = Node_pin("lgdb", v).get_top_lgraph()->get_name();
        fmt::print("\n\n{}=={}\n\n",lg_name,orig_lg_name);
        I(lg_name.find("__firrtl_")!=std::string::npos,"\n\n inp-synth-set has some unresolved entry???\n\n ");
      }
    }
    for (const auto & [k,v_set]: out_map_of_sets_synth) {
      for (const auto & v : v_set) {
        auto lg_name = Node_pin("lgdb", v).get_top_lgraph()->get_name();
        I(lg_name.find("__firrtl_")!=std::string::npos,"\n\n out-synth-set has some unresolved entry???\n\n ");
      }
    }
    */

    bool change_done = complete_io_match(true);  // for flop only as matching flop first
    fmt::print("\n complete_io_match - synth - flop only (outside while) done.\n");
    if (crit_node_set.empty()) {
      /*all required matching done*/
      report_critical_matches_with_color();
    }
#ifdef BASIC_DBG
    fmt::print("8. before resolution + matching while loop starts -- synth");
    print_everything();
#endif

    while (change_done && !crit_node_set.empty() && !flop_set.empty()) {  // for flop only as matching flop first
      resolution_of_synth_map_of_sets(inp_map_of_sets_synth);
      resolution_of_synth_map_of_sets(out_map_of_sets_synth);
      change_done = complete_io_match(true);  // alters crit_node_set too
      fmt::print("\n complete_io_match - synth - flop only (inside while) done.\n");
#ifdef BASIC_DBG
      fmt::print("Change done = {}\n", change_done);
#endif
    }
#ifdef BASIC_DBG
    fmt::print("6. Printing after all the flop resolution and matching!");
    print_everything();
#endif

    if (!flop_set.empty()) {
      weighted_match_LoopLastOnly();  // crit_entries_only=f, loopLast_only=t
      // set_theory_match_loopLast_only();
      fmt::print("\n weighted_match_LoopLastOnly - synth done.\n");
#ifdef BASIC_DBG
      fmt::print("9. Printing after flop weighted_match_LoopLastOnly matching!");
      print_everything();
#endif
      remove_resolved_from_orig_MoS();
    }

    I(flop_set.empty(), "\n\n\tCHECK: flops not resolved. Cannot move on to further matching\n\n");
    if (crit_node_set.empty()) {
      /*all required matching done*/
      report_critical_matches_with_color();
    }

    // all flops matched and still some crit cells left to map!
    // move to combinational matching
    do {
      resolution_of_synth_map_of_sets(inp_map_of_sets_synth);
      resolution_of_synth_map_of_sets(out_map_of_sets_synth);
      change_done = complete_io_match(false);  // alters crit_node_set too
      fmt::print("\n complete_io_match - synth - combinational done.\n");
#ifdef BASIC_DBG
      fmt::print("Change done = {}\n", change_done);
      fmt::print("10.0. Printing within do-while for all the combinational resolution and matching!");
      print_everything();
#endif
    } while (change_done && !crit_node_set.empty());
#ifdef BASIC_DBG
    fmt::print("10. Printing after all the combinational resolution and matching!");
    print_everything();
#endif

    if (!crit_node_set.empty()) {  // exact combinational matching could not resolve all crit nodes
      // surrounding cell loc-similarity matching
      change_done = surrounding_cell_match();
      fmt::print("\n surrounding_cell_match - synth done.\n");
#ifdef BASIC_DBG
      fmt::print("Change done = {}\n", change_done);
#endif
    }
#ifdef BASIC_DBG
    fmt::print("11. Printing after surrounding_cell matching!");
    print_everything();
#endif
#if 0
    if(!crit_node_set.empty()) {
      bool unmatched_left;
      do {
        unmatched_left = surrounding_cell_match_final();
        fmt::print("\n surrounding_cell_match_final - synth done.\n");
        fmt::print("unmatched left = {}\n", unmatched_left);
        if(unmatched_left) {
          resolution_of_synth_map_of_sets(inp_map_of_sets_synth);
          resolution_of_synth_map_of_sets(out_map_of_sets_synth);
        }
      } while(unmatched_left && !crit_node_set.empty());
    }
#endif
#if 1
    if (!crit_node_set.empty()) {
      // set_theory_match_final();
      weighted_match();  // crit_entries_only=t, loopLast_only=f
      fmt::print("\n weighted_match - synth (crit_entries_only) done.\n");
    }
#endif

    // fmt::print("\n inp_map_of_sets_synth.size() =  {}\nout_map_of_sets_synth:\n", inp_map_of_sets_synth.size());
    // print_io_map(out_map_of_sets_synth);
    // fmt::print("\n out_map_of_sets_synth.size() =  {}\n", out_map_of_sets_synth.size());
    /*all required matching done*/
#ifdef BASIC_DBG
    fmt::print("20. Printing after crit_node_set.empty assertion checked");
    print_everything();
#endif
    report_critical_matches_with_color();
    I(crit_node_set.empty(), "crit_node_set should have been empty by now!");
    return;  // FIXME: for DBG; remove.
  }

  if (is_orig_lg) {
    make_io_maps(lg, inp_map_of_sets_orig, out_map_of_sets_orig, is_orig_lg);
    fmt::print("\n make_io_maps - orig done.\n");

#ifdef BASIC_DBG
    fmt::print("\n inp_map_of_sets_orig.size() =  {}\nout_map_of_sets_orig:\n", inp_map_of_sets_orig.size());
    fmt::print("\n out_map_of_sets_orig.size() =  {}\n", out_map_of_sets_orig.size());
#endif
    return;  // FIXME: for DBG; remove.
  }

  I(!inp_map_of_sets_orig.empty() && !out_map_of_sets_orig.empty(),
    "\nCHECK: why is either inp_map_of_sets_orig or out_map_of_sets_orig empty??\n");
  // if (do_matching){
  //   exact_matching();
  // }

  I(false, "\nintended exit!\n");

  lg->dump(true);  // FIXME: remove this
  for (const auto& node : lg->fast(true)) {
    dealing_flop = false;
    dealing_comb = false;
    // absl::btree_set<std::string> in_set;
    // absl::btree_set<std::string> out_set;
    std::set<std::string> in_set;
    std::set<std::string> out_set;
    std::set<std::string> io_set;
    std::set<std::string> in_comb_set;
    std::set<std::string> out_comb_set;
    std::set<std::string> io_comb_set;
    // fmt::print("{}\n", node.debug_name());

    /* For post syn LG -> if the node is flop then calc all IOs in in_set and out_set and keep in map*/
    if (node.is_type_flop()
        || (node.is_type_sub() ? ((std::string(node.get_type_sub_node().get_name())).find("_df") != std::string::npos) : false)
        || node.is_type_loop_last()) {
      dealing_flop = true;
      for (const auto& ine : node.inp_edges()) {
        get_input_node(ine.driver, in_set, io_set);
      }
      for (const auto& oute : node.out_edges()) {
        get_output_node(oute.sink, out_set, io_set);
      }

      /*add to crit_flop_list if !do_matching and flop node is colored*/
      if (!do_matching && node.has_color()) {
        auto colr = node.get_color();
        crit_flop_list.emplace_back(node.get_compact_flat());
        crit_flop_map[node.get_compact_flat()] = colr;
        fmt::print("Flop color:\n");
        fmt::print("\t{}\n", colr);
      }

    } else { /*else if node is in crit_cell_list then keep its IO in cellIOMap_synth*/
      dealing_comb = true;
      /*add to crit_cell_list if !do_matching and cell node is colored*/
      if (!do_matching && node.has_color()) {
        auto node_val = node.get_compact_flat();
        auto colr     = node.get_color();
        crit_cell_list.emplace_back(node_val);
        crit_cell_map[node.get_compact_flat()] = colr;
        fmt::print("combo color:\n");
        fmt::print("\t{}\n", colr);

        // calc node's IO
        for (const auto& ine : node.inp_edges()) {
          get_input_node(ine.driver, in_comb_set, io_comb_set, true);
        }
        for (const auto& oute : node.out_edges()) {
          get_output_node(oute.sink, out_comb_set, io_comb_set, true);
        }

        // insert this node IO in the map
      }
    }

    if (in_set.empty() && out_set.empty() && in_comb_set.empty() && out_comb_set.empty()) {  // no i/ps as well as no o/ps
      continue;  // do not keep such nodes in nodeIOmap or cellIOMap_synth
    }

    // print the set formed
    fmt::print("INPUTS:");
    if (dealing_flop) {
      for (const auto& i : in_set) {
        fmt::print("\t{}", i);
      }
    } else {
      for (const auto& i : in_comb_set) {
        fmt::print("\t{}", i);
      }
    }
    fmt::print("\nOUTPUTS:");
    if (dealing_flop) {
      for (const auto& i : out_set) {
        fmt::print("\t{}", i);
      }
    } else {
      for (const auto& i : out_comb_set) {
        fmt::print("\t{}", i);
      }
    }
    fmt::print("\nIOs:");
    if (dealing_flop) {
      for (const auto& i : io_set) {
        fmt::print("\t{}", i);
      }
    } else {
      for (const auto& i : io_comb_set) {
        fmt::print("\t{}", i);
      }
    }
    fmt::print("\n");

    if (!do_matching) {
      // insert in map
      const auto&                     nodeid = node.get_compact_flat();
      std::vector<Node::Compact_flat> tmpVec;
      if (dealing_flop) { /*dealing with flops*/
        if (nodeIOmap.find(std::make_pair(in_set, out_set)) != nodeIOmap.end()) {
          tmpVec.assign((nodeIOmap[std::make_pair(in_set, out_set)]).begin(), (nodeIOmap[std::make_pair(in_set, out_set)]).end());
          tmpVec.emplace_back(nodeid);
        } else {
          tmpVec.emplace_back(nodeid);
        }
        nodeIOmap[std::make_pair(in_set, out_set)] = tmpVec;  // FIXME: make hash of set and change datatype accordingly
      } else if (dealing_comb) {                              /*dealing with combinational*/
        if (cellIOMap_synth.find(std::make_pair(in_comb_set, out_comb_set)) != cellIOMap_synth.end()) {
          tmpVec.assign((cellIOMap_synth[std::make_pair(in_comb_set, out_comb_set)]).begin(),
                        (cellIOMap_synth[std::make_pair(in_comb_set, out_comb_set)]).end());
          tmpVec.emplace_back(nodeid);
        } else {
          tmpVec.emplace_back(nodeid);
        }
        cellIOMap_synth[std::make_pair(in_comb_set, out_comb_set)]
            = tmpVec;  // FIXME: make hash of set and change datatype accordingly
      } else {
        I(false, "not possible to enter this part! node is neither combo nor seq!? Debug! Check!");
      }

      // the IOtoNodeMap_synth making:
      if (dealing_flop && !io_set.empty()) {
        std::vector<Node::Compact_flat> tempnodeVec;
        setMap_pairKey                  internalMap;
        if (IOtoNodeMap_synth.find(io_set) != IOtoNodeMap_synth.end()) {
          // founf IO, insert in internal map against this location.
          internalMap = IOtoNodeMap_synth[io_set];
          if (internalMap.find(std::make_pair(in_set, out_set)) != internalMap.end()) {
            tempnodeVec.assign((internalMap[std::make_pair(in_set, out_set)]).begin(),
                               (internalMap[std::make_pair(in_set, out_set)]).end());
            tempnodeVec.emplace_back(nodeid);
          } else {
            tempnodeVec.emplace_back(nodeid);
          }
          internalMap[std::make_pair(in_set, out_set)] = tempnodeVec;

        } else {
          // make a new entry in the internal map
          tempnodeVec.emplace_back(nodeid);
          internalMap[std::make_pair(in_set, out_set)] = tempnodeVec;
        }
        IOtoNodeMap_synth[io_set] = internalMap;  // FIXME: make hash of set and change datatype accordingly
      }
    }

    if (do_matching && !req_flops_matched) {
      /*if orig graph IO-set pair is as-is found in the synth nodeIOmap, then it is direct match!*/
      if (nodeIOmap.find(std::make_pair(in_set, out_set)) != nodeIOmap.end()) {
        // put this in matching_map.
        matched_map[node.get_compact_flat()]
            = nodeIOmap[std::make_pair(in_set, out_set)];  // FIXME:put this in matching_map. no need of matched_map then

        /*matched_map -> matching_map*/
        const auto synNodes = nodeIOmap[std::make_pair(in_set, out_set)];
        // std::vector<Node::Compact_flat> tmpVec;
        // if(matching_map.find(synNodes) != matching_map.end()) {
        //   tmpVec.assign((matching_map[synNodes]).begin() , (matching_map[synNodes]).end() );
        //   tmpVec.emplace_back(node.get_compact_flat());
        // } else {
        //   tmpVec.emplace_back(node.get_compact_flat());
        // }
        for (const auto& synNode : synNodes) {
          (matching_map[synNode]).emplace_back(node.get_compact_flat());  // matching_map[synNode]=tmpVec;

          /*if synNode in crit_flop_list, remove from crit_flop_list;*/
          for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
            if (*cfl_it == synNode) {
              crit_flop_list.erase(cfl_it);
              cfl_it--;
            }
          }
          /*if synNode in crit_flop_map, remove the entry from crit_flop_map*/
          if (crit_flop_list.empty()) {
            req_flops_matched = true;
          }
          if (crit_flop_map.find(synNode) != crit_flop_map.end()) {
            // if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color
            for (const auto& o_n : matching_map[synNode]) {
              matched_color_map[o_n] = crit_flop_map[synNode];
            }
          }
        }

      } else {
        unmatched_map[node.get_compact_flat()] = std::make_pair(
            in_set,
            out_set);  // FIXME: (no more needed; kept for debug purposes) (old fixme msg: this won't be needed once matched map is
                       // removed and entries put in matching_map are erased from the main maps!
      }
      /*insert in full_orig_map (against same set of IOs)*/
      const auto&                     nodeid = node.get_compact_flat();
      std::vector<Node::Compact_flat> tmpVec;
      if (full_orig_map.find(std::make_pair(in_set, out_set)) != full_orig_map.end()) {
        tmpVec.assign((full_orig_map[std::make_pair(in_set, out_set)]).begin(),
                      (full_orig_map[std::make_pair(in_set, out_set)]).end());
        tmpVec.emplace_back(nodeid);
      } else {
        tmpVec.emplace_back(nodeid);
      }
      full_orig_map[std::make_pair(in_set, out_set)] = tmpVec;  // FIXME: make hash of set and change datatype accordingly
      /*IOtoNodeMap_orig insertion*/
      std::vector<Node::Compact_flat> tempVec;
      if (IOtoNodeMap_orig.find(io_set) != IOtoNodeMap_orig.end()) {
        tempVec.assign((IOtoNodeMap_orig[io_set]).begin(), (IOtoNodeMap_orig[io_set]).end());
        tempVec.emplace_back(nodeid);
      } else {
        tempVec.emplace_back(nodeid);
      }
      if (!io_set.empty()) {
        IOtoNodeMap_orig[io_set] = tempVec;  // FIXME: make hash of set and change datatype accordingly
      }
    }  // end of if(do_matching)

  }  // enf of for lg-> traversal
  if (!do_matching) {
    /*for the sequential part:*/
    I(!nodeIOmap.empty(), "\n\nDEBUG?? \tNO FLOP IN THE SYNTHESISED DESIGN\n\n");
    // print the map
    fmt::print("\n\nMAP FORMED IS:\n");
    for (const auto& [ioPair, n_list] : nodeIOmap) {
      for (const auto& ip : ioPair.first) {
        fmt::print("{}\t", ip);
      }
      fmt::print("||| \t");
      for (const auto& op : ioPair.second) {
        fmt::print("{}\t", op);
      }
      fmt::print("::: \t");
      for (const auto& n : n_list) {
        fmt::print("n{}\t", n.get_nid());
      }
      fmt::print("\n\n");
    }
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nIOtoNodeMap_synth MAP FORMED IS:\n");
    print_IOtoNodeMap_synth(IOtoNodeMap_synth);
    fmt::print("\n\n\n");
    /*for the combo part:*/
    // print the cellIOMap_synth
    fmt::print("\n\nthe cellIOMap_synth FORMED IS:\n");
    print_MapOf_SetPairAndVec(cellIOMap_synth);
  } else if (do_matching) {  // do_matching
    fmt::print("\n\nThe IOtoNodeMap_orig map is:\n");
    for (const auto& [iov, fn] : IOtoNodeMap_orig) {
      for (const auto& ip : iov) {
        fmt::print("{}\t", ip);
      }
      fmt::print("::: \t");
      for (const auto& op : fn) {
        fmt::print("n{}\t", op.get_nid());
      }
      fmt::print("\n\n");
    }
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nThe complete orig map (full_orig_map) is:\n");
    print_MapOf_SetPairAndVec(full_orig_map);
    fmt::print("\n\n===============================\n");

    fmt::print("\n\n The unmatched flops are:\n");
    for (const auto& [fn, iov] : unmatched_map) {
      fmt::print("n{}\n", fn.get_nid());
      for (const auto& ip : iov.first) {
        fmt::print("{}\t", ip);
      }
      fmt::print("||| \t");
      for (const auto& op : iov.second) {
        fmt::print("{}\t", op);
      }
      fmt::print("\n\n");
    }
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nmatched_map (matching done is):\n");
    for (const auto& [k, n_list] : matched_map) {
      fmt::print("n{}\t", k.get_nid());
      fmt::print("::: \t");
      for (const auto& n : n_list) {
        fmt::print("n{}\t", n.get_nid());
      }
      fmt::print("\n");
    }
    fmt::print("\n\n===============================\n");
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nmatching_map (@matching done before pass_1 is):\n");
    for (const auto& [k, n_list] : matching_map) {
      fmt::print("n{}\t", k.get_nid());
      fmt::print("::: \t");
      for (const auto& n : n_list) {
        fmt::print("n{}\t", n.get_nid());
      }
      fmt::print("\n");
    }
    fmt::print("\n\n===============================\n");
  }

  if (do_matching && !req_flops_matched) {
    /*doing the actual matching here*/  //(pass_1)

    // for(const auto& [iov,fn]: IOtoNodeMap_orig)
    for (absl::node_hash_map<std::set<std::string>, std::vector<Node::Compact_flat>>::iterator it = IOtoNodeMap_orig.begin();
         it != IOtoNodeMap_orig.end();) {
      if (req_flops_matched) {
        break;
      }
      auto iov = it->first;
      auto fn  = it->second;
      if (fn.size() != 1) {
        ++it;
        continue;
      }
      auto orig_node = fn.front();
      // go to the best match in IOtoNodeMap_synth
      if (IOtoNodeMap_synth.find(iov) != IOtoNodeMap_synth.end()) {
        for (const auto& [k, synNodes] : IOtoNodeMap_synth[iov]) {
          for (const auto& synNode : synNodes) {
            // std::vector<Node::Compact_flat> vc = synNode;
            /*inserting in matching_map*/
            // const auto& nodeid = node.get_compact_flat();
            std::vector<Node::Compact_flat> tmpVec;
            if (matching_map.find(synNode) != matching_map.end()) {
              tmpVec.assign((matching_map[synNode]).begin(), (matching_map[synNode]).end());
              tmpVec.emplace_back(orig_node);
            } else {
              tmpVec.emplace_back(orig_node);
            }
            matching_map[synNode] = tmpVec;
            /*if synNode in crit_flop_list, remove from crit_flop_list;*/
            for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
              if (*cfl_it == synNode) {
                crit_flop_list.erase(cfl_it);
                cfl_it--;
              }
            }
            /*if synNode in crit_flop_map, remove the entry from crit_flop_map*/
            if (crit_flop_list.empty()) {
              req_flops_matched = true;
            }
            if (crit_flop_map.find(synNode) != crit_flop_map.end()) {
              // if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color
              for (const auto& o_n : matching_map[synNode]) {
                // need o_n_node to be of type Node (not compact/compact_flat)
                // Node o_n_node(lg,o_n);//points to the node in orig LG
                // o_n_node.set_color(crit_flop_map[synNode]);//FIXME: o_n is compact_flat here. we need it of Node type?
                matched_color_map[o_n] = crit_flop_map[synNode];
              }
            }
          }
        }
        IOtoNodeMap_orig.erase(it++);
        IOtoNodeMap_synth.erase(iov);
        // req. for pass_3 //erase the node from full_orig_map  - value has nodes.
        // req. for pass_3 for (auto it_o = full_orig_map.begin(); it_o!= full_orig_map.end();) {
        // req. for pass_3   if (std::find( (it_o->second).begin(), (it_o->second).end(), orig_node) != (it_o->second).end() ) {
        // req. for pass_3     full_orig_map.erase(it_o++);
        // req. for pass_3   } else ++it_o;
        // req. for pass_3 }
      } else {
        ++it;
      }
    }

    I(!matching_map.empty(), "There should be some node in matching_map to further go to pass_2");

    /*entry on the graph IO matched! moving to pass_2 in matching*/
    bool change_done = true;
    while (change_done) {
      change_done = false;
      /*First: resolve the map*/
      for (auto& [k, v_map] : IOtoNodeMap_synth) {
        // for (auto& [iov,n]:v_map)
        for (auto it = v_map.begin(); it != v_map.end();) {
          auto& iv = (it->first).first;   // this is i/p set for [n]
          auto& ov = (it->first).second;  // this is o/p set for [n]
          // auto& n = it->second; //FIXME: is this correct coding sthyle? (auto& var_name = something;) ?

          /*resolve here: if iv contains any entry from matching_map, resolve and make ivResolved. ||ly for ov*/
          std::set<std::string> randSet1;
          for (auto set_it = iv.begin(); set_it != iv.end(); set_it++) {
            if ((*set_it).find("flop:") != std::string::npos) {        // if iv has flop
              auto                     SflopID = (*set_it).substr(5);  // synth flop name captured for comparison
              std::vector<std::string> OflopID = get_map_val(
                  matching_map,
                  SflopID);  // FIXME: pass by reference??//get_map_val will give the orig_flop_ID corresponding toSflopID.
              I(OflopID.size() < 2, "\n\n1 synth flop matched with many orig flops.... look into it... how to process it.\n\n");
              if (!(OflopID).empty()) {
                std::string i_r = "flop:" + OflopID[0];
                randSet1.emplace(i_r);  // resolved entry in randSet1
                // change_done=true;//SG:test (for a bug)
                // iv.erase(*set_it);
              } else {
                randSet1.emplace(*set_it);
              }
            } else {  // iv value does NOT have flop
              randSet1.emplace(*set_it);
            }
          } /*now we have iv resolved in randSet1!*/
          std::set<std::string> randSet2;
          for (auto set_it = ov.begin(); set_it != ov.end(); set_it++) {
            if ((*set_it).find("flop:") != std::string::npos) {        // if ov has flop
              auto                     SflopID = (*set_it).substr(5);  // synth flop name captured for comparison
              std::vector<std::string> OflopID = get_map_val(
                  matching_map,
                  SflopID);  // FIXME: pass by reference??//get_map_val will give the orig_flop_ID corresponding toSflopID.
              I(OflopID.size() < 2, "\n\n1 synth flop matched with many orig flops.... look into it... how to process it.\n\n");
              if (!(OflopID).empty()) {
                std::string o_r = "flop:" + OflopID[0];
                randSet2.emplace(o_r);  // resolved entry in randSet2
                // change_done=true;//SG:test (for a bug)
                // ov.erase(*set_it);
              } else {
                randSet2.emplace(*set_it);
              }
            } else {  // ov value does NOT have flop
              randSet2.emplace(*set_it);
            }
          } /*now we have ov resolved in randSet2!*/
          /*make pair<randSet1,randSet2> and replace <iv,ov> with it:*/
          auto extracted_entry  = v_map.extract(it++);
          extracted_entry.key() = std::make_pair(randSet1, randSet2);
          v_map.insert(std::move(extracted_entry));
        }  // end of for(auto it=v_map.begin(); it!=v_map.end();
      }    // end of for ( auto & [k,v_map]: IOtoNodeMap_synth) //map resolved

      /*printing the resolved map*/
      fmt::print("\n\nIOtoNodeMap_synth MAP RESOLVED IS:\n");
      print_IOtoNodeMap_synth(IOtoNodeMap_synth);

      /*Second: do the matching part post resolution*/
      for (auto& [k, v_map] : IOtoNodeMap_synth) {
        if (req_flops_matched) {
          break;
        }
        // for (auto& [iov,n]:v_map)
        for (auto it = v_map.begin(); it != v_map.end();) {
          if (req_flops_matched) {
            break;
          }
          auto& iv = (it->first).first;   // this is i/p set for [n]
          auto& ov = (it->first).second;  // this is o/p set for [n]
          auto& n  = it->second;
          /*start finding and matching in the resolved map!!:*/
          bool foundFull    = false;
          bool foundPartial = false;
          // for (auto [pairIO, nods]:full_orig_map)
          for (auto ito = full_orig_map.begin(); ito != full_orig_map.end(); ito++) {
            if (req_flops_matched) {
              break;
            }
            auto pairIO = ito->first;
            auto nods   = ito->second;
            if (iv == pairIO.first && ov == pairIO.second) {
              foundFull = true;
              // std::for_each(n.begin(), n.end(), [](const auto& n1) {matching_map[n1]=nods;});
              for (const auto& n1 : n) {
                matching_map[n1]
                    = nods;  // FIXME: see if the entry is already there in matching_map and then append to the pre-existing vector
                /*if synNode in crit_flop_list, remove from crit_flop_list;*/
                for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
                  if (*cfl_it == n1) {
                    crit_flop_list.erase(cfl_it);
                    cfl_it--;
                  }
                }
                if (crit_flop_list.empty()) {
                  req_flops_matched = true;
                }
                if (crit_flop_map.find(n1) != crit_flop_map.end()) {
                  // if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color
                  for (const auto& o_n : matching_map[n1]) {
                    // o_n.set_color(crit_flop_map[n1]);//FIXME: o_n is compact_flat here. we need it of Node type?
                    matched_color_map[o_n] = crit_flop_map[n1];
                  }
                }
              }
              // full_orig_map.erase(ito++);
              continue;
            } else if (iv == pairIO.first || ov == pairIO.second) {
              foundPartial = true;
              for (const auto& n1 : n) {
                matching_map[n1]
                    = nods;  // FIXME: see if the entry is already there in matching_map and then append to the pre-existing vector
                /*if synNode in crit_flop_list, remove from crit_flop_list;*/
                for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
                  if (*cfl_it == n1) {
                    crit_flop_list.erase(cfl_it);
                    cfl_it--;
                  }
                }
                if (crit_flop_list.empty()) {
                  req_flops_matched = true;
                }
                if (crit_flop_map.find(n1) != crit_flop_map.end()) {
                  // if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color
                  for (const auto& o_n : matching_map[n1]) {
                    // o_n.set_color(crit_flop_map[n1]);//FIXME: o_n is compact_flat here. we need it of Node type?
                    matched_color_map[o_n] = crit_flop_map[n1];
                  }
                }
              }
              // full_orig_map.erase(ito++);
              continue;
            }  // else {++ito;}
          }
          if (foundFull || foundPartial) {
            v_map.erase(it++);
            change_done = true;  // FIXME: should change_done be evaluated here only?
          } else {
            ++it;
          }

        }  // end of for(auto it=v_map.begin(); it!=v_map.end();
      }    // end of for ( auto & [k,v_map]: IOtoNodeMap_synth)

      /*if no change_done, i.e. nothing happened in pass_2
       * try pass_3 and then see if some change possible? */
      if (!change_done && !crit_flop_list.empty()) {  // start pass_3
        for (auto& [k, v_map] : IOtoNodeMap_synth) {
          for (auto it = v_map.begin(); it != v_map.end();) {
            auto& map_entry = *it;
            auto  synth_set = getUnion(map_entry.first.first, map_entry.first.second);
            auto& synth_val = map_entry.second;
            change_done     = set_theory_match(synth_set, synth_val, full_orig_map);
            if (crit_flop_list.empty()) {
              req_flops_matched = true;
            }
            if (change_done) {
              v_map.erase(it++);
            } else {
              ++it;
            }
          }
        }
      }  // end of if(!change_done && !crit_flop_list.empty())

    }  // end of while (change_done)

    /*Printing "matching map"*/
    fmt::print("\n THE MATCHING_MAP is:\n");
    for (const auto& [k, v] : matching_map) {
      fmt::print("\nn{}\t:::\t", k.get_nid());
      for (const auto& v1 : v) {
        fmt::print("n{}\t", v1.get_nid());
      }
    }
    fmt::print("\n");

    /*printing changed map*/
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nThe complete orig map (full_orig_map) ALTERED is:\n");
    print_MapOf_SetPairAndVec(full_orig_map);
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nThe IOtoNodeMap_orig map ALTERED is:\n");
    for (const auto& [iov, fn] : IOtoNodeMap_orig) {
      for (const auto& ip : iov) {
        fmt::print("{}\t", ip);
      }
      fmt::print("::: \t");
      for (const auto& op : fn) {
        fmt::print("n{}\t", op.get_nid());
      }
      fmt::print("\n\n");
    }
    fmt::print("\n\n===============================\n");

    fmt::print("\n\nIOtoNodeMap_synth MAP ALTERED IS:\n");
    print_IOtoNodeMap_synth(IOtoNodeMap_synth);

  }  // if(do_matching) closes here

  // FIXME: put some assertion to check if req_flops_matched is still false (when the function completes).
  // coz if it still false at completion, then the combo match will not enter even at the end!
  // Atleast flag it!!
  if (do_matching) {
    // if(!req_flops_matched) { fmt::print("\nMESSAGE: crit_flop_list is not empty. Should have been empty by now.");}
    fmt::print("\n crit_flop_list at this point: \n");
    for (auto& n : crit_flop_list) {
      fmt::print("n{}\t", n.get_nid());
    }
    I(req_flops_matched, "\n crit_flop_list is not empty. Should have been empty by now.\n");
  }
  bool cellIOMap_synth_resolved = false;
  if (do_matching && !cellIOMap_synth.empty()) {
    /*resolve cellIOMap_synth with help of matching_map*/
    std::set<std::pair<std::set<std::string>, std::set<std::string>>> noOverwrite_in_cellIOMapSynth;
    for (auto it = cellIOMap_synth.begin(); it != cellIOMap_synth.end();) {
      auto& iv = (it->first).first;   // this is i/p set for [n]
      auto& ov = (it->first).second;  // this is o/p set for [n]
      /*resolve here: if iv contains any entry from matching_map, resolve and make ivResolved. ||ly for ov*/
      std::set<std::string> randSet1;
      for (auto set_it = iv.begin(); set_it != iv.end(); set_it++) {
        if ((*set_it).find("flop:") != std::string::npos) {        // if iv has flop
          auto                     SflopID = (*set_it).substr(5);  // synth flop name captured for comparison
          std::vector<std::string> OflopID = get_map_val(
              matching_map,
              SflopID);  // FIXME: pass by reference??//get_map_val will give the orig_flop_ID corresponding toSflopID.
          I(OflopID.size() < 2, "\n\n1 synth flop matched with many orig flops.... look into it... how to process it.\n\n");
          if (!(OflopID).empty()) {
            std::string i_r = "flop:" + OflopID[0];
            randSet1.emplace(i_r);  // resolved entry in randSet1
          } else {
            randSet1.emplace(*set_it);
          }
        } else {  // iv value does NOT have flop
          randSet1.emplace(*set_it);
        }
      } /*now we have iv resolved in randSet1!*/
      std::set<std::string> randSet2;
      for (auto set_it = ov.begin(); set_it != ov.end(); set_it++) {
        if ((*set_it).find("flop:") != std::string::npos) {        // if ov has flop
          auto                     SflopID = (*set_it).substr(5);  // synth flop name captured for comparison
          std::vector<std::string> OflopID = get_map_val(
              matching_map,
              SflopID);  // FIXME: pass by reference??//get_map_val will give the orig_flop_ID corresponding toSflopID.
          I(OflopID.size() < 2, "\n\n1 synth flop matched with many orig flops.... look into it... how to process it.\n\n");
          if (!(OflopID).empty()) {
            std::string o_r = "flop:" + OflopID[0];
            randSet2.emplace(o_r);  // resolved entry in randSet2
            // ov.erase(*set_it);
          } else {
            randSet2.emplace(*set_it);
          }
        } else {  // ov value does NOT have flop
          randSet2.emplace(*set_it);
        }
      } /*now we have ov resolved in randSet2!*/
      /*make pair<randSet1,randSet2> and replace <iv,ov> with it:*/
      auto extracted_entry  = cellIOMap_synth.extract(it++);
      extracted_entry.key() = std::make_pair(randSet1, randSet2);
      /*if key already in noOverwrite-Set, then take extracted_entry.mapped() and append to map.find(extracted_entry.key()).
       * else just store this key in noOverwrite_in_cellIOMapSynth.*/
      if (noOverwrite_in_cellIOMapSynth.find(extracted_entry.key()) != noOverwrite_in_cellIOMapSynth.end()) {
        for (const auto& m : cellIOMap_synth[extracted_entry.key()]) {
          (extracted_entry.mapped()).emplace_back(m);
        }
        cellIOMap_synth.erase(extracted_entry.key());  // won't overwrite the same key value!
      } else {
        noOverwrite_in_cellIOMapSynth.insert(extracted_entry.key());
      }
      /*... and insertion to cellIOMap_synth remains same in both cases.*/
      cellIOMap_synth.insert(std::move(extracted_entry));
    }  // end of for(auto it=cellIOMap_synth.begin(); it!=cellIOMap_synth.end();

    // print the cellIOMap_synth
    fmt::print("\n\nthe cellIOMap_synth RESOLVED IS:\n");
    print_MapOf_SetPairAndVec(cellIOMap_synth);
    cellIOMap_synth_resolved = true;
    // print crit_cell_list
    fmt::print("\n crit_cell_list at this point: \n");
    for (auto& n : crit_cell_list) {
      fmt::print("n{}\t", n.get_nid());
    }
  }  // if(do_matching && req_flops_matched && !cellIOMap_synth.empty()) ends here

  fmt::print("\n\n\n");
  if (do_matching && cellIOMap_synth_resolved) {
    /*matching of combo part happens here with help of pre-synth LG!
     * 1. take LGorig and go to the SP for 1st cell in cellIOMap_synth*/
    for (const auto& [k, v] : cellIOMap_synth) {
      auto& allSPs = k.first;
      auto& allEPs = k.second;

      for (const auto& sp : allSPs) {
        fmt::print("{} ", sp);
      }
      fmt::print("\n-\n");
      for (const auto& ep : allEPs) {
        fmt::print("{}\n", ep);
      }
      fmt::print("\n\n\n");

      auto  synth_set = getUnion(allSPs, allEPs);
      auto& synth_val = v;
      /*go to 1st SP of allSPs for 1st entry
       * and start iterating from there*/
      const auto required_node = *(allSPs.begin());
      VISITED_COLORED++;
      // for (const auto &required_node : allSPs) {
      if ((required_node).substr(0, 4) != "flop") {  // then it is graph IO
        // Node startPoint_node(lg, required_node );
        lg->each_graph_input([required_node, synth_set, synth_val, this](Node_pin& dpin) {
          const auto& in_node   = dpin.get_node();
          std::string comp_name = (dpin.has_name() ? dpin.get_name() : dpin.get_pin_name());
          if (comp_name == required_node) {
            Traverse_lg::setMap_pairKey cellIOMap_orig;
            path_traversal(in_node, synth_set, synth_val, cellIOMap_orig);
          }
        });
      } else {
        lg->dump(true);                                                                      // FIXME: remove this
        for (const auto& startPoint_node : lg->fast(true)) {                                 // FIXME:REM
          if (std::to_string(startPoint_node.get_nid().value) == required_node.substr(5)) {  // FIXME:REM
            fmt::print("Found node n{}\n", startPoint_node.get_nid());                       // FIXME:REM
            // keep traversing forward until you hit an EP
            Traverse_lg::setMap_pairKey cellIOMap_orig;
            path_traversal(startPoint_node, synth_set, synth_val, cellIOMap_orig);

          }  // FIXME:REM
        }    // end of for (const auto& startPoint_node : lg->fast())//FIXME:REM
      }
      //}
    }  // for (auto [k,v]: cellIOMap_synth) ends here
    /*Printing "matching map"*/
    fmt::print("\n THE FINAL (combo matched) MATCHING_MAP is:\n");
    for (const auto& [k, v] : matching_map) {
      fmt::print("\nn{}\t:::\t", k.get_nid());
      for (const auto& v1 : v) {
        fmt::print("n{}\t", v1.get_nid());
      }
    }
    fmt::print("\n");
  }  // if(cellIOMap_synth_resolved) ends here

  if (!matched_color_map.empty()) {
    /*Printing "matched_color_map"*/
    fmt::print("\n THE matched_color_map is:\n");
    for (const auto& [k, v] : matched_color_map) {
      fmt::print("\tn{}\t:::\t{}\n", k.get_nid(), v);
    }
    fmt::print("\n");
    // print crit_cell_list
    fmt::print("\n crit_cell_list at this point: \n");
    for (auto& n : crit_cell_list) {
      fmt::print("n{}\t", n.get_nid());
    }
  }
}

absl::flat_hash_set<Node_pin::Compact_flat> Traverse_lg::get_matching_map_val(const Node_pin::Compact_flat& dpin_cf) const {
  auto it_match = net_to_orig_pin_match_map.find(dpin_cf);
  if (it_match != net_to_orig_pin_match_map.end()) {
    return it_match->second;
  }
  return absl::flat_hash_set<Node_pin::Compact_flat>();
}

void Traverse_lg::make_io_maps(Lgraph* lg, map_of_sets& inp_map_of_sets, map_of_sets& out_map_of_sets, bool is_orig_lg) {
  /*in fwd, flops are visited last. Thus this fast pass:*/
  fast_pass_for_inputs(lg, inp_map_of_sets, is_orig_lg);
#ifdef BASIC_DBG
  fmt::print("\nPrinting the crit_node_map\n");
  for (const auto& [node_pin_cf, color] : crit_node_map) {
    auto n = Node_pin("lgdb", node_pin_cf).get_node();
    auto p = Node_pin("lgdb", node_pin_cf);
    if (p.has_name()) {
      fmt::print("{},{} \t:: {} ", /*n.get_or_create_name(),*/ p.get_name(), p.get_pid(), std::to_string(color));
    } else {
      fmt::print("n{},{} \t:: {} ", /*n.get_or_create_name(),*/ n.get_nid(), p.get_pid(), std::to_string(color));
    }
    fmt::print("\n");
  }
  fmt::print("\n");

  fmt::print("9.1.0 Printing before fwd traversal!");
  print_everything();
#endif

  /*propagate sets. stop at sequential/IO... (_last)*/
  traverse_order.clear();
  fwd_traversal_for_inp_map(lg, inp_map_of_sets, is_orig_lg);
#ifdef BASIC_DBG
  fmt::print("9.1 Printing after fwd traversal!");
  print_everything();
#endif

  /*propagate sets. stop at sequential/IO/const... (_last & _first). this is like backward traversal*/
  bwd_traversal_for_out_map(out_map_of_sets, is_orig_lg);

  if (!is_orig_lg) {
    /* during the traversals, the nodes which were matched getts re-inserted in MoS(s).
     * Thus, remove those from inp_MoS and out_MoS*/
    /* Doing this before fwd/bwd pass will lead to erroneous results.*/
    for (const auto& [node_pin_cf, set_pins_cf] : net_to_orig_pin_match_map) {
      remove_from_crit_node_set(node_pin_cf);
      inp_map_of_sets.erase(node_pin_cf);
      out_map_of_sets_synth.erase(node_pin_cf);
      for (const auto& orig_pin : set_pins_cf) {
        /* further accuracy attempt: remove the nodes used from orig as well*/
        inp_map_of_sets_orig.erase(orig_pin);
        out_map_of_sets_orig.erase(orig_pin);
      }
      if (flop_set.find(node_pin_cf) != flop_set.end()) {
        flop_set.erase(node_pin_cf);
      }
    }
    /*if nothing left in crit_node_set then return with result*/
    if (crit_node_set.empty()) {
      report_critical_matches_with_color();
    }
  }
}

/*parse the IO of LG. coz fast/fwd pass does not cover IO.*/
void Traverse_lg::make_io_maps_boundary_only(Lgraph* lg, map_of_sets& inp_map_of_sets, map_of_sets& out_map_of_sets,
                                             bool is_orig_lg) {
  /*add inputs of nodes touching the graphInput, to initialize inp_map_of_sets*/
  lg->each_graph_input([&inp_map_of_sets, &is_orig_lg, this](const Node_pin dpin) {
    /*capture the colored nodes in the process.*/
    auto node = dpin.get_node();
    if (!is_orig_lg) {
#ifndef FULL_RUN_FOR_EVAL
      if (node.has_color()) {
        for (const auto dpins : node.out_connected_pins()) {
          crit_node_map[dpins.get_compact_flat()] = node.get_color();  // keep till end for color data
          if (net_to_orig_pin_match_map.find(dpins.get_compact_flat()) == net_to_orig_pin_match_map.end()) {
            crit_node_set.insert(dpins.get_compact_flat());  // keep on deleting as matching takes place
                                                             // need not stor in set if already a matched entry
          }
        }
      }
#else
      for (const auto dpins : node.out_connected_pins()) {
        crit_node_map[dpins.get_compact_flat()] = 0;  // keep till end for color data
        if (net_to_orig_pin_match_map.find(dpins.get_compact_flat()) == net_to_orig_pin_match_map.end()) {
          crit_node_set.insert(dpins.get_compact_flat());  // keep on deleting as matching takes place
                                                           // need not stor in set if already a matched entry
        }
      }
#endif
    }
    for (auto sink_dpin : dpin.out_sinks()) {
      // if ( is_orig_lg && !sink_dpin.get_node().has_loc() ) {
      //   /* In original LG, if a node does NOT have LoC information, then the node should not be kept in map of sets.
      //    * This would decrease the number of nodes to be matched.
      //    * Thus orig_lg time complexity part will have (n - n_wo_LoC) in worst case. */
      //   continue;
      // }
      for (const auto dpins : sink_dpin.get_node().out_connected_pins()) {  // nodes w/o any o/p will NOT appear in map now
        if (dpin.get_pin_name() == "reset_pin")
          continue;  // do not want "reset" in inp-set (gives matching issues)
        if (!net_to_orig_pin_match_map.empty()) {
          auto match_val = get_matching_map_val(dpin.get_compact_flat());  // resolution attempt
          if (!match_val.empty()) {
            inp_map_of_sets[dpins.get_compact_flat()].insert(match_val.begin(), match_val.end());
            //: in synth map_of_sets, insert the equivalent orig_dpin match as IO entry.
          } else {
            inp_map_of_sets[dpins.get_compact_flat()].insert(dpin.get_compact_flat());
          }
        } else {
          inp_map_of_sets[dpins.get_compact_flat()].insert(dpin.get_compact_flat());
        }
      }
    }
  });
#ifdef BASIC_DBG
  fmt::print("\n:::: inp_map_of_sets.size() =  {}\n", inp_map_of_sets.size());
  print_io_map(inp_map_of_sets);
#endif

  /*add outputs of nodes touching the graphOutput, to initialize out_map_of_sets*/
  lg->each_graph_output([&out_map_of_sets, &is_orig_lg, this](const Node_pin dpin) {
    /*capture the colored nodes in the process.*/
    auto node = dpin.get_node();
    if (!is_orig_lg) {
#ifndef FULL_RUN_FOR_EVAL
      if (node.has_color()) {
        for (const auto dpins : node.out_connected_pins()) {
          crit_node_map[dpins.get_compact_flat()] = node.get_color();
          if (net_to_orig_pin_match_map.find(dpins.get_compact_flat()) == net_to_orig_pin_match_map.end()) {
            crit_node_set.insert(dpins.get_compact_flat());  // keep on deleting as matching takes place
                                                             // need not stor in set if already a matched entry
          }
        }
      }
#else
      for (const auto dpins : node.out_connected_pins()) {
        crit_node_map[dpins.get_compact_flat()] = 0;
        if (net_to_orig_pin_match_map.find(dpins.get_compact_flat()) == net_to_orig_pin_match_map.end()) {
          crit_node_set.insert(dpins.get_compact_flat());  // keep on deleting as matching takes place
                                                           // need not stor in set if already a matched entry
        }
      }
#endif
    }
    auto spin = dpin.change_to_sink_from_graph_out_driver();
    for (auto driver_dpin : spin.inp_drivers()) {
      // if (is_orig_lg && !driver_dpin.get_node().has_loc() ) {
      //   /* In original LG, if a node does NOT have LoC, then the node should not be kept in map of sets.*/
      //   continue;
      // }
      if (!net_to_orig_pin_match_map.empty()) {
        auto match_val = get_matching_map_val(dpin.get_compact_flat());
        if (!match_val.empty()) {
          out_map_of_sets[driver_dpin.get_compact_flat()].insert(match_val.begin(), match_val.end());  // resolution attempt
          //: in synth map_of_sets, insert the equivalent orig_dpin match as IO entry.
        } else {
          out_map_of_sets[driver_dpin.get_compact_flat()].insert(dpin.get_compact_flat());
        }
      } else {
        out_map_of_sets[driver_dpin.get_compact_flat()].insert(dpin.get_compact_flat());
      }
    }
  });
#ifdef BASIC_DBG
  fmt::print("\n:::: out_map_of_sets.size() =  {}\n", out_map_of_sets.size());
  print_io_map(out_map_of_sets);
#endif
  /*For synth, set values were replaced using net_to_orig_pin_match_map. Thus these synth map_of_sets can be matched with orig
   * map_of_sets for matching at the boundary of the design.*/
  // if(!net_to_orig_pin_match_map.empty()) {
  //   /* 1. match map_of_sets-synth to map_of_sets-orig (both in and out)
  //    * 2. put matches in net_to_orig_pin_match_map.
  //    * 3. remove the matched entries from map_of_sets-synth; to keep the time complexity of further matching-passes lowered.*/
  //   /*For inputs*/
  //   matching_pass_io_boundary_only(inp_map_of_sets_synth, inp_map_of_sets_orig);
  //   /*Same as above for output*/
  //   matching_pass_io_boundary_only(out_map_of_sets_synth, out_map_of_sets_orig);
  //
  // }
  // ^Commented this because: matching right after boundary traversal gives more matches with less IO to match. example: incorrect
  // matching in case of flops. all flops were directly connected to clock in orig and registered as equivalent to each other just
  // due to the "clock" input.
}

void Traverse_lg::fast_pass_for_inputs(Lgraph* lg, map_of_sets& inp_map_of_sets, bool is_orig_lg) {
  /*in fwd, flops are visited last. Thus this fast pass:
   * (Flops could be considered FIRST (Q pin) or LAST (din pin). In the forward iterator, flops are not marked as loop_first, only
   * constants are. This means that the flop is not visited first.) */
  lg->dump(true);  // FIXME: remove this
  for (const auto& node : lg->fast(true)) {
    if (!is_orig_lg) {
/*capture the colored nodes*/
#ifndef FULL_RUN_FOR_EVAL
      if (node.has_color() && !node.is_type_const()) {
        for (const auto dpins : node.out_connected_pins()) {
          crit_node_map[dpins.get_compact_flat()] = node.get_color();
#ifdef BASIC_DBG
          fmt::print("Inserting in crit_node_map: n{} , {}\n", node.get_nid(), node.get_color());
#endif
          if (net_to_orig_pin_match_map.find(dpins.get_compact_flat()) == net_to_orig_pin_match_map.end()) {
            crit_node_set.insert(dpins.get_compact_flat());  // keep on deleting as matching takes place
                                                             // need not stor in set if already a matched entry
          }
        }
      }
#else
      if (!node.is_type_const()) {
        for (const auto dpins : node.out_connected_pins()) {
          crit_node_map[dpins.get_compact_flat()] = 0;
#ifdef BASIC_DBG
          fmt::print("Inserting in crit_node_map: n{} , 0\n", node.get_nid());
#endif
          if (net_to_orig_pin_match_map.find(dpins.get_compact_flat()) == net_to_orig_pin_match_map.end()) {
            crit_node_set.insert(dpins.get_compact_flat());  // keep on deleting as matching takes place
                                                             // need not stor in set if already a matched entry
          }
        }
      }
#endif
      if (node.is_type_loop_last()) {
        for (const auto dpins : node.out_connected_pins()) {
          flop_set.insert(dpins.get_compact_flat());
        }
      }
    }

    if (!node.is_type_loop_last()) {
      continue;  // process flops only in this lg->fast
    }

    for (const auto dpins : node.out_connected_pins()) {
      const auto node_dpin_cf = dpins.get_compact_flat();
      bool       is_loop_stop = node.is_type_loop_last() || node.is_type_loop_first() || (mark_loop_stop.find(node_dpin_cf)!=mark_loop_stop.end()); //FIXME: need not do this because all nodes reaching here are of type_loop_last?

      const auto self_set = inp_map_of_sets.find(node_dpin_cf);

      for (auto e : node.out_edges()) {
        if (e.sink.get_node().is_type_loop_first()) {
          /*need not keep outputs of const/graphIO in in_map_of_sets*/
          continue;
        }
        for (const auto out_cfs : e.sink.get_node().out_connected_pins()) {
          // if ( is_orig_lg && !out_cfs.get_node().has_loc() ) {
          //   /* In original LG, if a node does NOT have LoC, then the node should not be kept in map of sets.*/
          //   continue;
          // }
          auto out_cf = out_cfs.get_compact_flat();
          if (is_loop_stop) {
            if (!is_orig_lg && !(get_matching_map_val(dpins.get_compact_flat())).empty()) {
              auto match_val = get_matching_map_val(dpins.get_compact_flat());
              inp_map_of_sets[out_cf].insert(match_val.begin(), match_val.end());  // resolution
            } else {
              inp_map_of_sets[out_cf].insert(dpins.get_compact_flat());
            }
          } else {
            if (self_set != inp_map_of_sets.end()) {
              inp_map_of_sets[out_cf].insert(self_set->second.begin(), self_set->second.end());
            }
          }
        }
      }
    }
  }
  if (!is_orig_lg) {
    /*crit_node_set is finally formed at this point and some default matches have already being recorded
     * so remove those matched points from crit_node_set*/
    for (const auto& [node_pin_cf, set_pins_cf] : net_to_orig_pin_match_map) {
      remove_from_crit_node_set(node_pin_cf);
    }
    /*if nothing left in crit_node_set then return with result*/
    if (crit_node_set.empty()) {
      report_critical_matches_with_color();
    }
  }
}

void Traverse_lg::fwd_traversal_for_inp_map(Lgraph* lg, map_of_sets& inp_map_of_sets, bool is_orig_lg) {
  lg->dump(true);  // FIXME: remove this
#ifdef EXTENSIVE_DBG
  fmt::print("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n");
  fmt::print("\t\tis_orig_lg: {}\n", is_orig_lg);
#endif
  for (const auto& node : lg->forward(true)) {
    if (node.is_type_const() || (node.is_type_sub() && node.get_type_sub_node().get_name() == "__fir_const")) {
      continue;
    }
    for (const auto node_dpins : node.out_connected_pins()) {
      traverse_order.emplace_back(
          node_dpins);  // FIXME: since flops are already matched, do not keep them here for backward matching?
      const auto node_dpin_cf = node_dpins.get_compact_flat();
#ifdef BASIC_DBG
      fmt::print("node obtained from fwd traversal: n{},{} \n",
                 node.get_nid(),
                 node_dpins.has_name() ? node_dpins.get_name() : std::to_string(node_dpins.get_pid()));
#endif
      bool is_loop_stop = node.is_type_loop_last() || node.is_type_loop_first() || (mark_loop_stop.find(node_dpin_cf)!=mark_loop_stop.end());

      const absl::flat_hash_set<Node_pin::Compact_flat>* self_set = nullptr;
      auto                                               it       = inp_map_of_sets.find(node_dpin_cf);
      if (it != inp_map_of_sets.end()) {
        self_set = &it->second;
#ifdef EXTENSIVE_DBG
        fmt::print("\tnode present in inp_map_of_sets. It's SS:");
        print_set(*self_set);
        fmt::print("\n");
#endif
      }
#ifdef EXTENSIVE_DBG
      else {
        fmt::print("\tnode NOT present in inp_map_of_sets already.\n");
      }
#endif
      for (auto e : node.out_edges()) {
        if (e.sink.get_node().is_type_loop_first() /*need not keep outputs of const/graphIO in in_map_of_sets*/
            /*|| e.sink.get_node().is_type_loop_last() is_type_loop_last processed in previous fast pass*/) {
#ifdef EXTENSIVE_DBG
          fmt::print("\tChild node is loop_first. continuing!\n");
#endif
          continue;
        }
        // if ( is_orig_lg && !e.sink.get_node().has_loc() ) {
        //   /* In original LG, if a node does NOT have LoC, then the node should not be kept in map of sets.*/
        //   continue;
        // }
        for (const auto out_cfs : e.sink.get_node().out_connected_pins()) {
#ifdef EXTENSIVE_DBG
          fmt::print("\tChild node's driver:\t\t {}(n{})\n",
                     out_cfs.has_name() ? out_cfs.get_name() : std::to_string(out_cfs.get_pid()),
                     out_cfs.get_node().get_nid());
#endif
          auto out_cf = out_cfs.get_compact_flat();
          if (is_loop_stop) {
            if (!is_orig_lg && !(get_matching_map_val(node_dpin_cf)).empty()) {
              auto match_val = get_matching_map_val(node_dpin_cf);
#ifdef EXTENSIVE_DBG
              fmt::print("\t\t\tmatch_val is the I/P! K[n{}]::V[-match_val_value-]\n", out_cfs.get_node().get_nid());
#endif
              inp_map_of_sets[out_cf].insert(match_val.begin(), match_val.end());  // resolution
              /*} else if (self_set) { //this is a trial for accuracy (inserting the inputs of unresolved loop_stop instead of the
                l9oops top itself) inp_map_of_sets[out_cf].insert(self_set->begin(), self_set->end()); #ifdef EXTENSIVE_DBG
                  fmt::print("\t\t\tSS is the I/P! K[n{}]::V[ss val]\n", out_cfs.get_node().get_nid());
                #endif
              */
            } else {
              inp_map_of_sets[out_cf].insert(node_dpin_cf);
#ifdef EXTENSIVE_DBG
              fmt::print("\t\t\tnode itself is the I/P! K[n{}]::V[n{}]\n", out_cfs.get_node().get_nid(), node.get_nid());
#endif
            }
          } else {
            if (self_set) {
              inp_map_of_sets[out_cf].insert(self_set->begin(), self_set->end());
#ifdef EXTENSIVE_DBG
              fmt::print("\t\t\tSS is the I/P! K[n{}]::V[ss val]\n", out_cfs.get_node().get_nid());
#endif
            }
#ifdef BASIC_DBG
            else {
              fmt::print("\tNot inserting anyhting in inp_map_of_sets\n");
            }
#endif
          }
        }
      }
    }
#ifdef EXTENSIVE_DBG
    print_io_map(inp_map_of_sets);
#endif
  }
}

void Traverse_lg::bwd_traversal_for_out_map(map_of_sets& out_map_of_sets, bool is_orig_lg) {
#ifdef EXTENSIVE_DBG
  fmt::print("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n");
  fmt::print("\t\tis_orig_lg: {}\n", is_orig_lg);
#endif
  absl::flat_hash_set<Node::Compact_flat> traversed_nodes;
  for (std::vector<Node_pin>::const_reverse_iterator rit = traverse_order.rbegin(); rit != traverse_order.rend();
       ++rit) {  // const iter for const vec
    auto node_dpin = *rit;
    auto node      = node_dpin.get_node();
    if (traversed_nodes.find(node.get_compact_flat()) != traversed_nodes.end()) {
      continue;  // we have processed for this node's inputs
    }
    traversed_nodes.insert(node.get_compact_flat());
#ifdef BASIC_DBG
    fmt::print("node obtained from traverse_order: n{},{} \n", node.get_nid(), node_dpin.get_pid());
#endif
    bool is_loop_stop = node.is_type_loop_last() || node.is_type_loop_first() || (mark_loop_stop.find(node_dpin.get_compact_flat())!=mark_loop_stop.end());

    const absl::flat_hash_set<Node_pin::Compact_flat>* self_set = nullptr;
    auto                                               it       = out_map_of_sets.find(node_dpin.get_compact_flat());
    if (it != out_map_of_sets.end()) {
      self_set = &it->second;
#ifdef EXTENSIVE_DBG
      fmt::print("\tnode present in out_map_of_sets. It's SS:");
      print_set(*self_set);
      fmt::print("\n");
#endif
    }
#ifdef EXTENSIVE_DBG
    else {
      fmt::print("\tnode NOT present in out_map_of_sets already.\n");
    }
#endif

    for (auto in_dpin : node.inp_drivers()) {
#ifdef EXTENSIVE_DBG
      fmt::print("\tParent driver of the node:\t\t {}(n{})\n",
                 in_dpin.has_name() ? in_dpin.get_name() : std::to_string(in_dpin.get_pid()),
                 in_dpin.get_node().get_nid());
#endif
      if (in_dpin.get_node().is_type_loop_first() /*need not keep outputs of const/graphIO in out_map_of_sets*/
          /*|| in_dpin.get_node().is_type_loop_last() is_type_loop_last - specifically flops -  processed in hackish matching*/) {
        continue;
      }
      // if ( is_orig_lg && !in_dpin.get_node().has_loc() ) {
      //   /* In original LG, if a node does NOT have LoC, then the node should not be kept in map of sets.*/
      //   continue;
      // }
      auto inp_cf = in_dpin.get_compact_flat();  // get_dpin_cf(in_dpin.get_node());  // cannot do "in_dpin.get_compact_flat()"
                                                 // directly. pid-1 gets registered.
      if (is_loop_stop) {
        if (!is_orig_lg && !(get_matching_map_val(node_dpin.get_compact_flat())).empty()) {
          auto match_val = get_matching_map_val(node_dpin.get_compact_flat());
          out_map_of_sets[inp_cf].insert(match_val.begin(), match_val.end());  // resolution
#ifdef EXTENSIVE_DBG
          fmt::print("\t\t\tmatch_val is the O/P! K[n{}]::V[-match_val_value-]\n", in_dpin.get_node().get_nid());
#endif
          /*
          } else if (self_set) { //this is a trial for accuracy (inserting the inputs of unresolved loop_stop instead of the loops
          top itself) out_map_of_sets[inp_cf].insert(self_set->begin(), self_set->end()); #ifdef EXTENSIVE_DBG fmt::print("\t\t\tSS
          is the O/P! K[n{}]::V[ss val]\n", in_dpin.get_node().get_nid()); #endif
          */
        } else {
          out_map_of_sets[inp_cf].insert(node_dpin.get_compact_flat());
#ifdef EXTENSIVE_DBG
          fmt::print("\t\t\tnode itself is the O/P! K[n{}]::V[n{}]\n", in_dpin.get_node().get_nid(), node.get_nid());
#endif
        }
      } else {
        if (self_set) {
          out_map_of_sets[inp_cf].insert(self_set->begin(), self_set->end());
#ifdef EXTENSIVE_DBG
          fmt::print("\t\t\tSS is the O/P! K[n{}]::V[ss val]\n", in_dpin.get_node().get_nid());
#endif
        }
      }
    }
#ifdef EXTENSIVE_DBG
    print_io_map(out_map_of_sets);
#endif
  }
#ifdef EXTENSIVE_DBG
  fmt::print("\n&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n\n");
#endif
}

void Traverse_lg::remove_pound_and_bus(std::string& dpin_name) {
  // auto dpin_name = dpin.get_name();
  if (dpin_name.find('#') == std::size_t(0)) {  // if dpin_name has # as start char
    dpin_name.erase(dpin_name.begin());         // remove it
  }
  if (dpin_name.find("otup_") == std::size_t(0)) {  // if dpin_name has otup_ in the beginning
    dpin_name.erase(std::size_t(0), 5);             // remove it
  }
	if (dpin_name.find("otup_") != std::string::npos) {  //if dpin_name has otup_ in the middle
		dpin_name.erase(dpin_name.find("otup_"), 5);       //like otup_frontend.otup_btb.io_ras_head_valid: otup_ with tbt should be removed too
	}
  if (dpin_name.find('[') != std::string::npos) {              // if dpin_name has bus
    dpin_name.erase(dpin_name.find('['), dpin_name.length());  // remove it
  }
  // if(dpin_name.find('|')!=std::string::npos) {                // if dpin_name has "|"
  // 	dpin_name.erase(dpin_name.find('|'), dpin_name.length()); // convert it to bus ???????
  // }
  if (dpin_name.find("_p") != std::string::npos
      && ((dpin_name.length() - dpin_name.rfind("_p" + std::string(1, dpin_name.back()))) == 3) && isdigit(dpin_name.back())) {
    dpin_name.erase(dpin_name.rfind("_p"), dpin_name.length());
  }
  if (isdigit(dpin_name.back()) && (dpin_name[dpin_name.size() - 2] == '.')) {  // for cases like pc.0
    dpin_name.erase(dpin_name.end() - 2, dpin_name.end());
  }
  if (dpin_name.find("%") != std::string::npos) {  // for cases like registers.%io_readdata2|63
    dpin_name.erase(dpin_name.find("%"), 1);
  }
  if (synth_tool == "DC"
      && dpin_name.find(".")
             != std::string::npos) {  // in case the synth tool is DC, then "." in LGorig will have to be converted to "_"
    dpin_name[dpin_name.find(".")] = '_';
  }
}

void Traverse_lg::netpin_to_origpin_default_match(Lgraph* orig_lg, Lgraph* synth_lg) {
  /*keep top graph IO as well on the net_to_orig_pin_match_map */
  // orig_lg->each_graph_input([synth_lg, this](const Node_pin dpin) {
  //   auto synth_node_dpin = Node_pin::find_driver_pin( synth_lg , dpin.get_name() );//synth LG with same name as that of orig node
  //   net_to_orig_pin_match_map[synth_node_dpin.get_compact_flat()].insert(dpin.get_compact_flat());
  //   #ifdef FOR_EVAL
  //   fmt::print("Inserting in netpin_to_origpin_default_match : {}  :::
  //   {}\n",synth_node_dpin.has_name()?synth_node_dpin.get_name():("n"+std::to_string(synth_node_dpin.get_node().get_nid())),
  //   dpin.has_name()?dpin.get_name():("n"+std::to_string(dpin.get_node().get_nid()))); #endif
  // #ifdef EXTENSIVE_DBG
  //   fmt::print("DEFAULT INSERTION OF: {}, {}\n",
  //   synth_node_dpin.has_name()?synth_node_dpin.get_name():std::to_string(synth_node_dpin.get_pid()),
  //   std::to_string(synth_node_dpin.get_pid()) );
  // #endif
  //   remove_from_crit_node_set(synth_node_dpin.get_compact_flat());
  // });

  /* map to capture all possible dpin names in the hierarchy*/  // TODO with instance name API
  absl::flat_hash_map<std::string, Node_pin::Compact_flat>                      name2dpin;
  absl::flat_hash_map<std::string, absl::flat_hash_set<Node_pin::Compact_flat>> name2dpins;

  orig_lg->each_graph_input([&name2dpin](const Node_pin& dpin) {
    auto orig_in_dpin_name       = dpin.get_name();
    name2dpin[orig_in_dpin_name] = dpin.get_compact_flat();
#ifdef FOR_EVAL
    fmt::print("Inserting orig-in-dpin {} in name2dpin\n", orig_in_dpin_name);
#endif
  });
  synth_lg->each_graph_input([&name2dpin, this](const Node_pin& dpin) {
    auto synth_in_dpin_name = dpin.get_name();
    if (name2dpin.find(synth_in_dpin_name) != name2dpin.end()) {
      net_to_orig_pin_match_map[dpin.get_compact_flat()].insert(name2dpin.find(synth_in_dpin_name)->second);
			mark_loop_stop.insert(dpin.get_compact_flat());
			mark_loop_stop.insert(name2dpin.find(synth_in_dpin_name)->second);
#ifdef FOR_EVAL
      fmt::print("DEFAULT INSERTION OF: {}, {}\n",
                 dpin.has_name() ? dpin.get_name() : std::to_string(dpin.get_pid()),
                 std::to_string(dpin.get_pid()));
#endif
      remove_from_crit_node_set(dpin.get_compact_flat());
    }
#ifdef FOR_EVAL
    else {
      fmt::print("NOT inserting {}, {}\n",
                 dpin.has_name() ? dpin.get_name() : std::to_string(dpin.get_pid()),
                 std::to_string(dpin.get_pid()));
    }
#endif
  });

#ifdef FOR_EVAL
  fmt::print("\n OUT:\n");
#endif
  orig_lg->each_graph_output([&name2dpin](const Node_pin& dpin) {
    auto orig_out_dpin_name       = dpin.get_name();
    name2dpin[orig_out_dpin_name] = dpin.get_compact_flat();
#ifdef FOR_EVAL
    fmt::print("Inserting orig-out-dpin {} in name2dpin\n", dpin.get_name());
#endif
  });
  synth_lg->each_graph_output([&name2dpin, this](const Node_pin& dpin) {
    auto synth_out_dpin_name = dpin.get_name();
    if (name2dpin.find(synth_out_dpin_name) != name2dpin.end()) {
      net_to_orig_pin_match_map[dpin.get_compact_flat()].insert(name2dpin.find(synth_out_dpin_name)->second);
			mark_loop_stop.insert(dpin.get_compact_flat());
			mark_loop_stop.insert(name2dpin.find(synth_out_dpin_name)->second);
#ifdef FOR_EVAL
      fmt::print("DEFAULT INSERTION OF: {}, {}\n",
                 dpin.has_name() ? dpin.get_name() : std::to_string(dpin.get_pid()),
                 std::to_string(dpin.get_pid()));
#endif
      remove_from_crit_node_set(dpin.get_compact_flat());
    }
#ifdef FOR_EVAL
    else {
      fmt::print("NOT inserting {}, {}\n",
                 dpin.has_name() ? dpin.get_name() : std::to_string(dpin.get_pid()),
                 std::to_string(dpin.get_pid()));
    }
#endif
  });

  // orig_lg->each_graph_output([synth_lg, this](const Node_pin dpin) {
  //   auto synth_node_dpin = Node_pin::find_driver_pin( synth_lg , dpin.get_name() );//synth LG with same name as that of orig node
  //   net_to_orig_pin_match_map[synth_node_dpin.get_compact_flat()].insert(dpin.get_compact_flat());
  //   #ifdef FOR_EVAL
  //   fmt::print("Inserting in netpin_to_origpin_default_match : {}  :::
  //   {}\n",synth_node_dpin.has_name()?synth_node_dpin.get_name():("n"+std::to_string(synth_node_dpin.get_node().get_nid())),
  //   dpin.has_name()?dpin.get_name():("n"+std::to_string(dpin.get_node().get_nid()))); #endif #ifdef EXTENSIVE_DBG
  //     fmt::print("DEFAULT INSERTION OF: {}, {}\n",
  //     synth_node_dpin.has_name()?synth_node_dpin.get_name():std::to_string(synth_node_dpin.get_pid()),
  //     std::to_string(synth_node_dpin.get_pid()) );
  //   #endif
  //   remove_from_crit_node_set(synth_node_dpin.get_compact_flat());
  // });

  orig_lg->dump(true);
  for (const auto& original_node : orig_lg->fast(true)) {
    if (original_node.is_type_sub() && original_node.get_type_sub_node().get_name() == "__fir_const") {
      continue;
    }
    for (const auto& original_node_dpin : original_node.out_connected_pins()) {
      if (original_node_dpin.has_name()) {
#ifdef BASIC_DBG
        fmt::print("orig_node_dpin.wire: {} for pin: {} lg: {}\n",
                   original_node_dpin.get_wire_name(),
                   original_node_dpin.get_name(),
                   (original_node.get_class_lgraph())->get_name());
#endif
        auto original_node_dpin_wire = original_node_dpin.get_wire_name();
        remove_pound_and_bus(original_node_dpin_wire);

        if (original_node_dpin_wire.find('|') != std::string::npos) {  // if original_node_dpin_wire has "|"
          /* In case of buses like reg|2, reg|3, reg|7; all will have "reg"key for diff values.*/
          original_node_dpin_wire.erase(original_node_dpin_wire.find('|'), original_node_dpin_wire.length());
          name2dpins[original_node_dpin_wire].insert(original_node_dpin.get_compact_flat());
#ifdef BASIC_DBG
          fmt::print("\t\t\t inserting {} in name2dpinss.\n", original_node_dpin_wire);
#endif
        } else {
#ifdef BASIC_DBG
          if (name2dpin.find(original_node_dpin_wire) != name2dpin.end()) {
            fmt::print("WARNING: overwriting!\n");
          }
#endif
          name2dpin[original_node_dpin_wire] = original_node_dpin.get_compact_flat();

#ifdef BASIC_DBG
          fmt::print("\t\t\t inserting {} in name2dpin.\n", original_node_dpin_wire);
#endif
        }
      }
    }
  }

#ifdef BASIC_DBG
  print_name2dpin(name2dpin);
  print_name2dpins(name2dpins);
#endif

  /*known points matching*/
  synth_lg->dump(true);                           // FIXME: remove this
  for (auto synth_node : synth_lg->fast(true)) {  // FIXME : do NOT use hier true here !?

    if (synth_node.is_type_sub() && synth_node.get_type_sub_node().get_name() == "__fir_const") {
      continue;
    }

    for (const auto synth_node_dpin : synth_node.out_connected_pins()) {  // might be multi driver node
      if (synth_node_dpin.has_name()) {
#ifdef BASIC_DBG
        fmt::print("synth_node_dpin_name: {}\n", synth_node_dpin.get_name());
#endif
        /*see if the name matches to any in original LG.
         * if module gets instantiated in 2 places, find_driver_pin won't work with fast(true); as in who it points to - with same
         * name - you don't know. you have to provide for what LG you are trying to find this thing. get the current graph using
         * get_class_lgraph . so instead of orig_lg, use synth_node.get_class_lgraph().get_name -- find equivalent orig for this
         * guy!
         * */
        auto synth_node_dpin_wire = synth_node_dpin.get_wire_name();
#ifdef BASIC_DBG
        // fmt::print("\t\tFinding dpin for orig_sub_lg_name {}\n", orig_sub_lg->get_name());
        fmt::print("\t**  synth_node_dpin_wire {}  -->  ", synth_node_dpin_wire);
#endif
        // auto orig_node_dpin = Node_pin::find_driver_pin( orig_sub_lg , synth_node_dpin_name);//orig LG with same name as that of
        // synth node
        auto map_it = name2dpin.find(synth_node_dpin_wire);
        if (map_it == name2dpin.end()) {
          /* reason with example case:
             both regs_12 and regs_12[0] might be present. so for accuracy, first try without removing bus
             then if that does not work out, try with removing bus.*/
          remove_pound_and_bus(synth_node_dpin_wire);
        }
        auto map_itt = name2dpin.find(synth_node_dpin_wire);
#ifdef BASIC_DBG
        fmt::print(" {}  .**\n", synth_node_dpin_wire);
#endif
        if (map_itt != name2dpin.end()) {
#ifdef BASIC_DBG
          auto orig_node_dpin = Node_pin("lgdb", map_itt->second);
          fmt::print("\t\tFound orig_node_dpin {}\n",
                     orig_node_dpin.has_name() ? orig_node_dpin.get_name() : std::to_string(orig_node_dpin.get_pid()));
          fmt::print("\tDEFAULT INSERTION OF: {}, {}\n",
                     synth_node_dpin.has_name() ? synth_node_dpin.get_name() : std::to_string(synth_node_dpin.get_pid()),
                     std::to_string(synth_node_dpin.get_pid()));
#endif
          net_to_orig_pin_match_map[synth_node_dpin.get_compact_flat()].insert(map_itt->second);
					mark_loop_stop.insert(synth_node_dpin.get_compact_flat());
					mark_loop_stop.insert(map_itt->second);
#ifdef FOR_EVAL
          auto orig_node_dpin1 = Node_pin("lgdb", map_itt->second);
          fmt::print("Inserting in netpin_to_origpin_default_match : {}  :::  {}\n",
                     synth_node_dpin.has_name() ? synth_node_dpin.get_name()
                                                : ("n" + std::to_string(synth_node_dpin.get_node().get_nid())),
                     orig_node_dpin1.has_name() ? orig_node_dpin1.get_name()
                                                : ("n" + std::to_string(orig_node_dpin1.get_node().get_nid())));
#endif
          remove_from_crit_node_set(synth_node_dpin.get_compact_flat());
          out_map_of_sets_synth.erase(synth_node_dpin.get_compact_flat());
          inp_map_of_sets_synth.erase(synth_node_dpin.get_compact_flat());
        } else if (name2dpins.find(synth_node_dpin_wire) != name2dpins.end()) {
          /* map_itt was not found in name2dpin; maybe it can be found in name2dpins instead. If so then use this!*/
          auto map_itt_s = name2dpins.find(synth_node_dpin_wire);
#ifdef BASIC_DBG
          fmt::print("\t\tFound orig_node_dpin");
          for (const auto& orig_node_dpin_cf : map_itt_s->second) {
            auto orig_node_dpin = Node_pin("lgdb", orig_node_dpin_cf);
            fmt::print("  {}  ", orig_node_dpin.has_name() ? orig_node_dpin.get_name() : std::to_string(orig_node_dpin.get_pid()));
          }
          fmt::print("\n");
          fmt::print("\tDEFAULT INSERTION OF: {}, {}\n",
                     synth_node_dpin.has_name() ? synth_node_dpin.get_name() : std::to_string(synth_node_dpin.get_pid()),
                     std::to_string(synth_node_dpin.get_pid()));
#endif
          net_to_orig_pin_match_map[synth_node_dpin.get_compact_flat()].insert((map_itt_s->second).begin(),
                                                                               (map_itt_s->second).end());
					mark_loop_stop.insert(synth_node_dpin.get_compact_flat());
					mark_loop_stop.insert((map_itt_s->second).begin(),(map_itt_s->second).end());
#ifdef FOR_EVAL
          fmt::print("Inserting in netpin_to_origpin_default_match s : {}  ",
                     synth_node_dpin.has_name() ? synth_node_dpin.get_name()
                                                : ("n" + std::to_string(synth_node_dpin.get_node().get_nid())));
          for (const auto& orig_node_dpin1_cf : (map_itt_s)->second) {
            auto orig_node_dpin1 = Node_pin("lgdb", orig_node_dpin1_cf);
            fmt::print("  {}  ",
                       orig_node_dpin1.has_name() ? orig_node_dpin1.get_name()
                                                  : ("n" + std::to_string(orig_node_dpin1.get_node().get_nid())));
          }
          fmt::print("\n");
#endif
          remove_from_crit_node_set(synth_node_dpin.get_compact_flat());
          out_map_of_sets_synth.erase(synth_node_dpin.get_compact_flat());
          inp_map_of_sets_synth.erase(synth_node_dpin.get_compact_flat());
        }
      }
#ifdef EXTENSIVE_DBG
      else {
        fmt::print("IN DEFAULT MATCH: dpin not named for {}\n", synth_node_dpin.get_wire_name());
      }
#endif
    }
  }

  remove_resolved_from_orig_MoS();
}

void Traverse_lg::remove_resolved_from_orig_MoS() {
  for (const auto& [synth_np_cf, orig_np_cf_set] : net_to_orig_pin_match_map) {
    for (const auto& orig_np_cf : orig_np_cf_set) {
      inp_map_of_sets_orig.erase(orig_np_cf);
      out_map_of_sets_orig.erase(orig_np_cf);
    }
  }
}

void Traverse_lg::matching_pass_io_boundary_only(map_of_sets& map_of_sets_synth,
                                                 map_of_sets& map_of_sets_orig) {  // FIXME: no more used-- remove?

  for (auto it = map_of_sets_synth.begin(); it != map_of_sets_synth.end();) {
    auto n_s = Node_pin("lgdb", it->first).get_node();

    bool matched = false;
    for (const auto& [orig_np, orig_set_np] : map_of_sets_orig) {
      auto o_s = Node_pin("lgdb", orig_np).get_node();
      if (!((!n_s.is_type_loop_last() && !o_s.is_type_loop_last()) || (n_s.is_type_loop_last() && o_s.is_type_loop_last()))) {
        continue;  // REASON: flop/combo node should be matched with flop/combo node only! else datatype mismatch!
      }

      if ((it->second) == orig_set_np) {  // POSSIBLE FIXME: unrdered sets compared with ==
        matched = true;
        net_to_orig_pin_match_map[it->first].insert(orig_np);
#ifdef FOR_EVAL
        auto np_s = Node_pin("lgdb", it->first);
        auto np_o = Node_pin("lgdb", orig_np);
        fmt::print("Inserting in matching_pass_io_boundary_only: {}  :::  {}\n",
                   np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())),
                   np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
#endif
        remove_from_crit_node_set(it->first);
      }
    }
    if (matched) {
      map_of_sets_synth.erase(it++);  // Reason: if synth_np in matching map then why keep it in map_of_sets. Smaller the map of
                                      // sets, lesser iterations in further matching passes.
      // FIXME: add erase from orig Maps here also?
    } else {
      it++;
    }
  }
}

float Traverse_lg::get_matching_weight(const absl::flat_hash_set<Node_pin::Compact_flat>& synth_set,
                                       const absl::flat_hash_set<Node_pin::Compact_flat>& orig_set) const {
  const auto& smallest = synth_set.size() < orig_set.size() ? synth_set : orig_set;
  const auto& largest  = synth_set.size() >= orig_set.size() ? synth_set : orig_set;
  int         matches  = 0;
  for (const auto& it_set : smallest) {
    if (largest.contains(it_set)) {
      ++matches;
#ifdef FOR_EVAL
      fmt::print("\t\t\t\t ++ matches\n");
#endif
    }
  }

  /* 5 points for a full match for this set part
     5 for ins and 5 for outs will make a 10 for complete IO match.*/
  float matching_weight = 5 * (float(2 * matches) / float(synth_set.size() + orig_set.size()));
#ifdef FOR_EVAL
  fmt::print("\t\t\t\t matching_weight = {} (set1_size={}, set2_size={})\n", matching_weight, synth_set.size(), orig_set.size());
#endif
  return matching_weight;
}

bool Traverse_lg::complete_io_match(bool flop_only) {
#ifdef BASIC_DBG
  fmt::print("\n\n In complete_io_match : \n");
#endif
  bool io_matched        = false;
  bool any_matching_done = false;
  for (auto it = inp_map_of_sets_synth.begin(); it != inp_map_of_sets_synth.end();) {
    io_matched = false;
    auto n_s   = Node_pin("lgdb", it->first).get_node();
#ifdef BASIC_DBG
    auto p_s = Node_pin("lgdb", it->first);
    fmt::print("running for : {},n{}\n", p_s.has_name() ? p_s.get_name() : std::to_string(p_s.get_pid()), n_s.get_nid());
#endif
    if (flop_only) {
      if (!n_s.is_type_loop_last()) {
        it++;
        continue;  // if flop node, then only do matching; else continue with other entry.
      }
    }
    bool out_matched                    = false;  // FIXME: declaration can be shifted IN the following for loop?
    auto keep_partial_match_checking_on = true;
    std::map<float, absl::flat_hash_set<Node_pin::Compact_flat>> partial_out_match_map;
    for (const auto& [orig_in_np, orig_in_set_np] : inp_map_of_sets_orig) {
      auto o_s = Node_pin("lgdb", orig_in_np).get_node();
      if (flop_only) {
        if (!o_s.is_type_loop_last()) {
          continue;  // flop node should be matched with flop node only! else datatype mismatch!
        }
      }

      out_matched              = false;
      auto partial_out_matched = false;
      auto matching_wt         = 0.0;

      if (it->second == orig_in_set_np) {  // in_matched
#ifdef FOR_EVAL
        fmt::print("\t\t Inputs matched \n");
#endif
        /*it->first == orig_in_np for inputs. see if their output sets match as well.
         * 1. both might not have outputs and thus not be present in out_map_of_sets_<>
         * 2. if both are present, then compare the output sets.*/
        if (out_map_of_sets_synth.find(it->first) != out_map_of_sets_synth.end()
            && out_map_of_sets_orig.find(orig_in_np) != out_map_of_sets_orig.end()) {  // both present
#ifdef FOR_EVAL
          fmt::print("\t\t Outputs present for both \n");
#endif
          if (out_map_of_sets_synth[it->first] == out_map_of_sets_orig[orig_in_np]) {
            out_matched                    = true;
            keep_partial_match_checking_on = false;
            partial_out_matched            = false;
#ifdef FOR_EVAL
            fmt::print("\t\t Outputs exactly matched \n");
#endif
          } else if (keep_partial_match_checking_on) {
// input have been exact match . but output is not exact match! get best partial match for output?
#ifdef FOR_EVAL
            fmt::print("\t\t Outputs not exactly matched \n");
#endif
            matching_wt         = get_matching_weight(out_map_of_sets_synth[it->first], out_map_of_sets_orig[orig_in_np]);
            partial_out_matched = true;
          }
        } else if (out_map_of_sets_synth.find(it->first) == out_map_of_sets_synth.end()
                   && out_map_of_sets_orig.find(orig_in_np) == out_map_of_sets_orig.end()) {  // both absent. thus a match!?
          out_matched = true;
#ifdef FOR_EVAL
          fmt::print("\t\t matching due to absence !!\n");
#endif
        }
#ifdef BASIC_DBG
        else {  // inputs did not match
          auto p_o = Node_pin("lgdb", orig_in_np);
          fmt::print("\t\tMatch? : {},n{}\n",
                     p_o.has_name() ? p_o.get_name() : std::to_string(p_o.get_pid()),
                     std::to_string(o_s.get_nid()));
        }
#endif
      }

      if (out_matched) {  // in+out matched. complete exact match. put in matching map
        net_to_orig_pin_match_map[it->first].insert(orig_in_np);
#ifdef FOR_EVAL
        auto np_s = Node_pin("lgdb", it->first);
        auto np_o = Node_pin("lgdb", orig_in_np);
        fmt::print("Inserting in complete_io_match : n{},{}  :::  n{},{}\n",
                   np_s.get_node().get_nid(),
                   np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())),
                   np_o.get_node().get_nid(),
                   np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
#endif
        io_matched        = true;
        any_matching_done = true;
        partial_out_match_map.clear();
      } else if (partial_out_matched) {
        partial_out_match_map[matching_wt].insert(orig_in_np);
#ifdef FOR_EVAL
        auto np_o = Node_pin("lgdb", orig_in_np);
        fmt::print("\t\t For matching_wt {}, inserting : n{}({})\n",
                   matching_wt,
                   np_o.get_node().get_nid(),
                   np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
#endif
      }
    }

    if (io_matched) {  // in+out matched. complete exact match.  remove from synth map_of_sets
      remove_from_crit_node_set(it->first);
      if (flop_set.find(it->first) != flop_set.end()) {
        flop_set.erase(it->first);
      }
      out_map_of_sets_synth.erase(it->first);
      inp_map_of_sets_synth.erase(it++);
      // FIXME: add erase from orig Maps here also?
    } else if (partial_out_match_map.size() /*&& (((partial_out_match_map.end())->first)!=0) */) {
      net_to_orig_pin_match_map[it->first].insert(((partial_out_match_map.rbegin())->second).begin(),
                                                  ((partial_out_match_map.rbegin())->second).end());
#ifdef FOR_EVAL
      fmt::print("Partial_out_match_map:\n");
      for (auto [a, b] : partial_out_match_map) {
        fmt::print("{} ---- ", a);
        for (auto b_ : b) {
          auto b__ = Node_pin("lgdb", b_);
          fmt::print("\t {}, ", b__.has_name() ? b__.get_name() : ("n" + std::to_string(b__.get_node().get_nid())));
        }
        fmt::print("\n");
      }
      auto np_s = Node_pin("lgdb", it->first);
      fmt::print("Inserting in complete_io_match (parttial output matched) : n{},{}  :::  ",
                 np_s.get_node().get_nid(),
                 np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())));
      for (auto xx : (partial_out_match_map.rbegin())->second) {
        auto np_o = Node_pin("lgdb", xx);
        fmt::print("n{},{}\t\t",
                   np_o.get_node().get_nid(),
                   np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
      }
      fmt::print("\n");

#endif
      remove_from_crit_node_set(it->first);
      if (flop_set.find(it->first) != flop_set.end()) {
        flop_set.erase(it->first);
      }
      out_map_of_sets_synth.erase(it->first);
      inp_map_of_sets_synth.erase(it++);
    } else {
      it++;
    }
  }
  return any_matching_done;
}

bool Traverse_lg::surrounding_cell_match() {
  bool                                any_matching_done = false;
  std::vector<Node_pin::Compact_flat> erase_values_vec;
  for (auto it = inp_map_of_sets_synth.begin(); it != inp_map_of_sets_synth.end();) {
    auto n_s                             = Node_pin("lgdb", it->first).get_node();
    bool orig_connected_cells_vec_formed = true;
    bool cell_collapsed                  = true;

    /* for each surrounding cell,
     * if all resolved using net_to_orig_pin_match_map then check LoC;
     * else report n_s couldn't be confidently resolved.*/
    auto connected_cells_synth_vec = get_surrounding_pins(n_s);  // list of all surrounding cells_node_dpins
    absl::flat_hash_set<Node_pin::Compact_flat> connected_cells_orig_set;
    Node_pin::Compact_flat                      connected_same_cell;
    for (const auto& cc_s : connected_cells_synth_vec) {
#ifdef BASIC_DBG
      auto p = Node_pin("lgdb", cc_s);                                                             // FIXME: for debug only
      fmt::print("  {}, {}, {}\n", p.get_node().get_or_create_name(), p.has_name(), p.get_pid());  // FOR DEBUG
#endif

      if (net_to_orig_pin_match_map.find(cc_s) != net_to_orig_pin_match_map.end()) {  // connected_cell_synth is resolved.
        connected_cells_orig_set.insert(net_to_orig_pin_match_map[cc_s].begin(), net_to_orig_pin_match_map[cc_s].end());
      } else {  // connected_cell_synth is NOT reoslved. if its IO is same to that of n_s, consider them collpased and then resolve.
        connected_same_cell = cc_s;
        auto p_n            = Node_pin("lgdb", cc_s).get_node();
        auto it_syn_inp_cc  = inp_map_of_sets_synth.find(cc_s);
        auto it_syn_out_cc  = out_map_of_sets_synth.find(cc_s);
        auto it_out_syn     = out_map_of_sets_synth.find(it->first);
        if ((it != inp_map_of_sets_synth.end() && it_syn_inp_cc != inp_map_of_sets_synth.end()
             && it->second == it_syn_inp_cc->second)
            && ((it_syn_out_cc != out_map_of_sets_synth.end() && it_out_syn != out_map_of_sets_synth.end()
                 && it_out_syn->second == it_syn_out_cc->second) /*checkes both in & out*/
                || (it_syn_out_cc == out_map_of_sets_synth.end()
                    || it_out_syn == out_map_of_sets_synth.end()) /*no Outs available for matching*/)) {
          //^Not both In and Out are required. checking if both out are not available then atleast both in should be same.

          /*IO of the 2 cells matches. thus they can be collapsed as 1.*/

          auto it_rem = std::find(connected_cells_synth_vec.begin(), connected_cells_synth_vec.end(), cc_s);
          if (it_rem != connected_cells_synth_vec.end()) {
            connected_cells_synth_vec.erase(it_rem);
          }  // do not keep in connected_cells_synth_vec. coz cc_s is now collapsed with n_s. cc_s's LoC will not be checked and it
             // need not be converted to its orig.

          // cells surrounding cc_s - now collapsed with n_s - will be the surrounding cells for n_s too.
          auto same_cell_conn_cells_synth_vec = get_surrounding_pins(p_n, it->first);
          for (const auto& same_cc_s : same_cell_conn_cells_synth_vec) {
            if (net_to_orig_pin_match_map.find(same_cc_s) != net_to_orig_pin_match_map.end()) {
              connected_cells_orig_set.insert(net_to_orig_pin_match_map[same_cc_s].begin(),
                                              net_to_orig_pin_match_map[same_cc_s].end());
            } else {
#ifdef BASIC_DBG
              fmt::print("$$ Reporting cell n{}.\n", n_s.get_nid());
#endif
              orig_connected_cells_vec_formed
                  = false;  // if even 1 neighbor doesnt match, this will be false! so ALL have to be resolved.
              cell_collapsed = false;
            }
          }

        } else {
#ifdef BASIC_DBG
          fmt::print("$$ Reporting cell n{} due to no same IO with the connected n{}.\n", n_s.get_nid(), p_n.get_nid());
#endif
          orig_connected_cells_vec_formed = false;
          cell_collapsed                  = false;
        }
      }
    }

    if (orig_connected_cells_vec_formed) {
      auto connected_cells_loc_vec = get_loc_vec(connected_cells_orig_set);
      if (std::adjacent_find(connected_cells_loc_vec.begin(), connected_cells_loc_vec.end(), std::not_equal_to<>())
          == connected_cells_loc_vec.end()) {  // All elements are equal each other
        net_to_orig_pin_match_map[it->first].insert(connected_cells_orig_set.begin(), connected_cells_orig_set.end());
#ifdef FOR_EVAL
        auto np_s = Node_pin("lgdb", it->first);
        fmt::print("Inserting in surrounding_cell_match : {}  :::  ",
                   np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())));
        for (auto np_o_set : connected_cells_orig_set) {
          auto np_o = Node_pin("lgdb", np_o_set);
          fmt::print("    {} ", np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
        }
        fmt::print("\n");
#endif
        any_matching_done = true;
        remove_from_crit_node_set(it->first);
        erase_values_vec.emplace_back(it->first);
        if (cell_collapsed && !connected_same_cell.is_invalid()) {  // connected_same_cell present
          net_to_orig_pin_match_map[connected_same_cell].insert(connected_cells_orig_set.begin(), connected_cells_orig_set.end());
#ifdef FOR_EVAL
          fmt::print("Inserting in surrounding_cell_match : {}  :::  ",
                     np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())));
          for (auto np_o_set : connected_cells_orig_set) {
            auto np_o = Node_pin("lgdb", np_o_set);
            fmt::print("    {} ", np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
          }
          fmt::print("\n");
#endif
          remove_from_crit_node_set(connected_same_cell);
          erase_values_vec.emplace_back(connected_same_cell);
        }
        it++;
      } else {
#ifdef BASIC_DBG
        fmt::print("  $$ Reporting cell n{}.\n", n_s.get_nid());
#endif
        any_matching_done = false;
        it++;
      }
    } else {
      it++;
      any_matching_done = false;
    }
  }

  for (const auto& val_to_erase : erase_values_vec) {
    out_map_of_sets_synth.erase(val_to_erase);
    inp_map_of_sets_synth.erase(val_to_erase);
  }
  remove_resolved_from_orig_MoS();
  return any_matching_done;
}

bool Traverse_lg::surrounding_cell_match_final() {
  bool unmatched_left = false;
  for (auto it = inp_map_of_sets_synth.begin(); it != inp_map_of_sets_synth.end();) {
    auto n_s = Node_pin("lgdb", it->first).get_node();

    /* for each surrounding cell,
     * whichever is resolved, match with all those.*/
    auto connected_cells_synth_vec = get_surrounding_pins(n_s);  // list of all surrounding cells_node_dpins
    absl::flat_hash_set<Node_pin::Compact_flat> connected_cells_orig_set;
    for (const auto& cc_s : connected_cells_synth_vec) {
      if (net_to_orig_pin_match_map.find(cc_s) != net_to_orig_pin_match_map.end()) {  // connected_cell_synth is resolved.
        connected_cells_orig_set.insert(net_to_orig_pin_match_map[cc_s].begin(), net_to_orig_pin_match_map[cc_s].end());
      }
    }

    if (!connected_cells_orig_set.empty()) {
      net_to_orig_pin_match_map[it->first].insert(connected_cells_orig_set.begin(), connected_cells_orig_set.end());
#ifdef FOR_EVAL
      auto np_s = Node_pin("lgdb", it->first);
      fmt::print("Inserting in surrounding_cell_match_final : {}  :::  ",
                 np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())));
      for (auto np_o_set : connected_cells_orig_set) {
        auto np_o = Node_pin("lgdb", np_o_set);
        fmt::print("    {} ", np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
      }
      fmt::print("\n");
#endif
      remove_from_crit_node_set(it->first);
      forced_match_vec.emplace_back(it->first);
      out_map_of_sets_synth.erase(it->first);
      inp_map_of_sets_synth.erase(it++);
      // FIXME: add erase from orig maps here also?
    } else {  // no resolved connected cell present.
      it++;
      unmatched_left = true;
    }
  }

  if (!out_map_of_sets_synth.empty()) {  // some unmatched left in out_map_of_sets_synth

    for (auto it = out_map_of_sets_synth.begin(); it != out_map_of_sets_synth.end();) {
      auto n_s = Node_pin("lgdb", it->first).get_node();

      /* for each surrounding cell,
       * whichever is resolved, match with all those.*/
      auto connected_cells_synth_vec = get_surrounding_pins(n_s);  // list of all surrounding cells_node_dpins
      absl::flat_hash_set<Node_pin::Compact_flat> connected_cells_orig_set;
      for (const auto& cc_s : connected_cells_synth_vec) {
        if (net_to_orig_pin_match_map.find(cc_s) != net_to_orig_pin_match_map.end()) {  // connected_cell_synth is resolved.
          connected_cells_orig_set.insert(net_to_orig_pin_match_map[cc_s].begin(), net_to_orig_pin_match_map[cc_s].end());
        }
      }

      if (!connected_cells_orig_set.empty()) {
        net_to_orig_pin_match_map[it->first].insert(connected_cells_orig_set.begin(), connected_cells_orig_set.end());
#ifdef FOR_EVAL
        auto np_s = Node_pin("lgdb", it->first);
        fmt::print("Inserting in surrounding_cell_match_final : {}  :::  ",
                   np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())));
        for (auto np_o_set : connected_cells_orig_set) {
          auto np_o = Node_pin("lgdb", np_o_set);
          fmt::print("    {} ", np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
        }
        fmt::print("\n");
#endif
        remove_from_crit_node_set(it->first);
        forced_match_vec.emplace_back(it->first);
        out_map_of_sets_synth.erase(it++);
        // FIXME: add erase from orig Maps here also?
      } else {  // no resolved connected cell present.
        it++;
        unmatched_left = true;
      }
    }
  }

  return unmatched_left;
}

std::vector<Node_pin::Compact_flat> Traverse_lg::get_surrounding_pins(Node& node, Node_pin::Compact_flat main_node_dpin) const {
  std::vector<Node_pin::Compact_flat> dpin_vec;
  for (const auto& in_pin : node.inp_drivers()) {
    auto n = in_pin.get_node();
    if (n.is_type_io()) {
      dpin_vec.emplace_back(in_pin.get_compact_flat());
      continue;
    }
    if (!n.is_type_const() && main_node_dpin != in_pin.get_compact_flat()
        && !(node.is_type_sub() && node.get_type_sub_node().get_name() == "__fir_const")) {  // get_dpin_cf(n)) {
      dpin_vec.emplace_back(in_pin.get_compact_flat());
    }
  }
  for (const auto& out_spin : node.out_sinks()) {
    if (out_spin.get_node().is_type_io()) {
      dpin_vec.emplace_back(out_spin.change_to_driver_from_graph_out_sink().get_compact_flat());
      continue;
    }
    bool broken = false;
    for (const auto& out_dpin : out_spin.get_node().out_connected_pins()) {
      if (main_node_dpin == out_dpin.get_compact_flat()) {
        broken = true;
        break;  // shouldn't match with any dpin of that node
      }
    }
    if (!broken) {
      for (const auto& out_dpin : out_spin.get_node().out_connected_pins()) {
        dpin_vec.emplace_back(out_dpin.get_compact_flat());
      }
    }
  }

  return dpin_vec;
}

std::vector<std::pair<uint64_t, uint64_t>> Traverse_lg::get_loc_vec(
    absl::flat_hash_set<Node_pin::Compact_flat>& orig_node_pin_vec) const {
  std::vector<std::pair<uint64_t, uint64_t>> loc_vec;
  for (auto& dpin : orig_node_pin_vec) {
    auto n_o     = Node_pin("lgdb", dpin).get_node();
    auto loc_val = std::make_pair(0, 0);
    if (n_o.has_loc()) {
      loc_val = n_o.get_loc();
      loc_vec.emplace_back(loc_val);
    } else {
#ifdef BASIC_DBG  // inserted this ifdef to speed up and lessen log dump
      fmt::print("FIXME: No Location found for n{}.\n", n_o.get_nid());
#endif
    }
  }
  return loc_vec;
}

void Traverse_lg::remove_from_crit_node_set(const Node_pin::Compact_flat& dpin_cf) {
  /* if this dpin_cf is found in crit_node_set,
   * 1.  delete from crit_node_set
   * 2.  if upon deletion, vec becomes empty (all crit nodes matched) return true else return false*/
  if (crit_node_set.find(dpin_cf) != crit_node_set.end()) {
#ifdef EXTENSIVE_DBG
    auto p = Node_pin("lgdb", dpin_cf);
    fmt::print("\t\tREMOVING FROM crit_node_set: {}, {}\n",
               p.has_name() ? p.get_name() : std::to_string(p.get_pid()),
               std::to_string(p.get_pid()));
#endif
    crit_node_set.erase(dpin_cf);
  }
#ifdef EXTENSIVE_DBG
  else {
    auto p = Node_pin("lgdb", dpin_cf);
    fmt::print("\t\tNOT REMOVING FROM crit_node_set: {}, {}\n",
               p.has_name() ? p.get_name() : std::to_string(p.get_pid()),
               std::to_string(p.get_pid()));
    fmt::print("\t crit_node_set: ");
    print_set(crit_node_set);
    fmt::print("\n");
    for (auto& pin_cf : crit_node_set) {
      auto p_ = Node_pin("lgdb", pin_cf);
      // fmt::print("\t COMPARING: {}, {} \t", p.has_name()?p.get_name():std::to_string(p.get_pid()),
      // p_.has_name()?p_.get_name():std::to_string(p_.get_pid()));
      fmt::print("\t COMPARING {} {}: {}, {} \t",
                 p.get_top_lgraph()->get_name(),
                 p_.get_top_lgraph()->get_name(),
                 p.get_wire_name(),
                 p_.get_wire_name());
      if (pin_cf == dpin_cf) {
        fmt::print("=>EQUAL!");
      } else {
        fmt::print("=>NOT EQUAL!");
      }
    }
    fmt::print("\n");
  }
#endif
}

void Traverse_lg::report_critical_matches_with_color() {
#ifdef FOR_EVAL
  fmt::print("\nmatching map:\n");
  print_io_map(net_to_orig_pin_match_map);
#endif
#ifdef FULL_RUN_FOR_EVAL
  fmt::print("\nmatching map:\n");
  print_io_map(net_to_orig_pin_match_map);
#endif
  fmt::print(
      "\n\nReporting final critical resolved matches: \nsynth node and dpin     :- original node and dpin      -- color val -- "
      "source loc\n");
  for (const auto& [synth_np, color_val] : crit_node_map) {
    auto        orig_NPs   = net_to_orig_pin_match_map[synth_np];
    auto        synth_pin  = Node_pin("lgdb", synth_np);
    auto        synth_dpin = synth_pin.has_name() ? synth_pin.get_name() : ("p" + std::to_string(synth_pin.get_pid()));
    std::string synth_node = absl::StrCat("n", std::to_string(synth_pin.get_node().get_nid()));
    for (const auto& orig_np : orig_NPs) {
      auto orig_pin  = Node_pin("lgdb", orig_np);
      auto orig_node = orig_pin.get_node();
      auto orig_dpin = orig_pin.has_name() ? orig_pin.get_name() : ("p" + std::to_string(orig_pin.get_pid()));
      auto loc_start = orig_node.has_loc() ? (std::to_string(orig_node.get_loc().first + 1)) : "xxx";
      auto loc_end   = orig_node.has_loc() ? (std::to_string(orig_node.get_loc().second + 1)) : "xxx";
      fmt::print("{},{}       :-    n{},{}    --   {}   --  [{},{}]{}\n",
                 synth_node,
                 synth_dpin,
                 orig_node.get_nid(),
                 orig_dpin,
                 color_val,
                 loc_start,
                 loc_end,
                 orig_node.get_source());  // FIXME: referring to nid for understandable message.
    }
  }

  fmt::print("\n");

  exit(2);
}

void Traverse_lg::resolution_of_synth_map_of_sets(Traverse_lg::map_of_sets& synth_map_of_set) {
#ifdef EXTENSIVE_DBG
  fmt::print("\n In resolution_of_synth_map_of_sets:\n");
#endif
  for (auto& [synth_np, synth_set_np] : synth_map_of_set) {
    // auto xx = Node_pin("lgdb", synth_np).get_node().get_nid();
    // fmt::print("------{} ({})\n",xx, synth_set_np.size());
    absl::flat_hash_set<Node_pin::Compact_flat> tmp_set;
    for (auto it = synth_set_np.begin(); it != synth_set_np.end();) {
      const auto set_np_val = *it;
      if (net_to_orig_pin_match_map.find(set_np_val) != net_to_orig_pin_match_map.end()) {
#ifdef EXTENSIVE_DBG
        fmt::print("Found match \n");
#endif
        synth_set_np.erase(it++);
        // FIXME: add erase from orig Maps here also?
        auto equiv_val = net_to_orig_pin_match_map[set_np_val];
        tmp_set.insert(equiv_val.begin(), equiv_val.end());
      } else {
#ifdef EXTENSIVE_DBG
        auto snv = Node_pin("lgdb", set_np_val);
        fmt::print("Tried finding {},n{},p{}({}) in mm \n",
                   snv.has_name() ? snv.get_name() : ("n" + std::to_string(snv.get_node().get_nid())),
                   snv.get_node().get_nid(),
                   snv.get_pid(),
                   snv.get_top_lgraph()->get_name());
#endif
        ++it;
      }
    }
    synth_set_np.insert(tmp_set.begin(), tmp_set.end());
  }
}

void Traverse_lg::print_everything() {
  fmt::print("\n-------------------\n");
  fmt::print("\norig lg in map:\n");
  print_io_map(inp_map_of_sets_orig);
  fmt::print("\norig lg out map:\n");
  print_io_map(out_map_of_sets_orig);
  fmt::print("\nsynth lg in map:\n");
  print_io_map(inp_map_of_sets_synth);
  fmt::print("\nsynth lg out map:\n");
  print_io_map(out_map_of_sets_synth);
  fmt::print("\nmatching map:\n");
  print_io_map(net_to_orig_pin_match_map);
  fmt::print("\ncrit nodes set:\n");
  print_set(crit_node_set);
  fmt::print("\nflop set:\n");
  print_set(flop_set);
  fmt::print("\nforced_match_vec:\n");
  {
    for (const auto& v : forced_match_vec) {
      auto n = Node_pin("lgdb", v).get_node();
      auto p = Node_pin("lgdb", v);
      if (p.has_name()) {
        fmt::print("{}\t ", p.get_name());
      } else {
        fmt::print("n{}\t ", n.get_nid());
      }
    }
  }
  fmt::print("\n-------------------\n");
}

void Traverse_lg::set_theory_match_loopLast_only() {
  auto io_map_of_sets_orig  = make_in_out_union(inp_map_of_sets_orig, out_map_of_sets_orig, true, false);
  auto io_map_of_sets_synth = make_in_out_union(inp_map_of_sets_synth, out_map_of_sets_synth, true, false);
#ifdef BASIC_DBG
  fmt::print("io_map_of_sets_orig: loop_last only\n");
  print_io_map(io_map_of_sets_orig);
  fmt::print("io_map_of_sets_synth: loop_last only\n");
  print_io_map(io_map_of_sets_synth);
#endif

  bool some_matching_done = false;
  do {
    resolution_of_synth_map_of_sets(io_map_of_sets_synth);
#ifdef BASIC_DBG
    fmt::print("\nINTERMEDIATE after resolution LL io_map_of_sets_synth: \n");
    print_io_map(io_map_of_sets_synth);
#endif
    some_matching_done = set_theory_match(io_map_of_sets_synth, io_map_of_sets_orig);  // loop last only maps
  } while (some_matching_done && !crit_node_set.empty());
// FIXME: what is the point of having do while here...set_theory will always match something to something!!
#ifdef BASIC_DBG
  fmt::print("\nFINAL LL io_map_of_sets_orig: \n");
  print_io_map(io_map_of_sets_orig);
  fmt::print("\nFINAL LL io_map_of_sets_synth: \n");
  print_io_map(io_map_of_sets_synth);
#endif
}

void Traverse_lg::set_theory_match_final() {
  auto io_map_of_sets_orig
      = make_in_out_union(inp_map_of_sets_orig, out_map_of_sets_orig, false, false);  // MoS, false: not loop_last
  auto io_map_of_sets_synth
      = make_in_out_union(inp_map_of_sets_synth, out_map_of_sets_synth, false, true);  // true for union of critical entries only
#ifdef BASIC_DBG
  fmt::print("\nio_map_of_sets_orig: \n");
  print_io_map(io_map_of_sets_orig);
  fmt::print("\nio_map_of_sets_synth: \n");
  print_io_map(io_map_of_sets_synth);
#endif

  set_theory_match(io_map_of_sets_synth, io_map_of_sets_orig);  // not loop last only maps

#ifdef BASIC_DBG
  fmt::print("\nFINAL io_map_of_sets_orig: \n");
  print_io_map(io_map_of_sets_orig);
  fmt::print("\nFINAL io_map_of_sets_synth: \n");
  print_io_map(io_map_of_sets_synth);
#endif
}

Traverse_lg::map_of_sets Traverse_lg::convert_io_MoS_to_node_MoS_LLonly(const map_of_sets& io_map_of_sets) {
#ifdef BASIC_DBG
  fmt::print("converting in/out map_of_sets to the shortened MoS with LoopLast only nodes in values(set).");
#endif

  Traverse_lg::map_of_sets node_map_of_set_LoopLastOnly;
  for (const auto& [node_key, io_vals_set] : io_map_of_sets) {
    /*if(loop_last_only):for only those keys that are is_type_loop_last*/
    auto node = Node_pin("lgdb", node_key).get_node();
    if (node.is_type_loop_last()) {  // flop node should be matched with flop node only! else datatype mismatch!
      // we need flop nodes only in this case
      for (const auto& io_val : io_vals_set) {
        node_map_of_set_LoopLastOnly[io_val].insert(node_key);
      }
    }
  }

#ifdef BASIC_DBG
  fmt::print("\n Printing node_map_of_set_LoopLastOnly \n");
  print_io_map(node_map_of_set_LoopLastOnly);
#endif

  return node_map_of_set_LoopLastOnly;
}

Traverse_lg::map_of_sets Traverse_lg::obtain_MoS_LLonly(const map_of_sets& io_map_of_sets) {
#ifdef BASIC_DBG
  fmt::print("obtain_MoS_LLonly:  MoS with LoopLast only nodes.");
#endif

  Traverse_lg::map_of_sets map_of_set_LoopLastOnly;
  for (const auto& [node_key, io_vals_set] : io_map_of_sets) {
    /*if(loop_last_only):for only those keys that are is_type_loop_last*/
    auto node = Node_pin("lgdb", node_key).get_node();
    if (node.is_type_loop_last()) {  // flop node should be matched with flop node only! else datatype mismatch!
      // we need flop nodes only in this case
      map_of_set_LoopLastOnly.insert({node_key, io_vals_set});
    }
  }

#ifdef BASIC_DBG
  fmt::print("\n Printing map_of_set_LoopLastOnly \n");
  print_io_map(map_of_set_LoopLastOnly);
#endif

  return map_of_set_LoopLastOnly;
}

void Traverse_lg::weighted_match_LoopLastOnly() {
#ifdef BASIC_DBG
  fmt::print("In weighted_match_LoopLastOnly:\n\n");
#endif

  /* map : | node | < inputs >             |
   * convert to
   * map : |input | <nodes (flop only)>    | */
  Traverse_lg::map_of_sets inp_map_of_node_sets_orig = convert_io_MoS_to_node_MoS_LLonly(inp_map_of_sets_orig);

  Traverse_lg::map_of_sets inp_map_of_sets_synth_LLonlly = obtain_MoS_LLonly(inp_map_of_sets_synth);
  // for (auto it = inp_map_of_sets_synth.begin(); it != inp_map_of_sets_synth.end();) {
  for (const auto& [node_ll, node_io] : inp_map_of_sets_synth_LLonlly) {
    // const auto &synth_key = it->first;
    // const auto &synth_set = it->second;
    const auto& synth_key = (inp_map_of_sets_synth.find(node_ll))->first;
    const auto& synth_set = (inp_map_of_sets_synth.find(node_ll))->second;

    /*if(loop_last_only):for only those keys that are is_type_loop_last
      time complexity with loop_last_only will be num_of_flops-synth into num_of_flops-orig
      otherwise, time complexity will be num_of_combi-synth into num_of_combi-orig*/
    // auto node_s = Node_pin("lgdb", synth_key).get_node();
    // if ( !node_s.is_type_loop_last() ) {
    //   #ifdef BASIC_DBG
    //     fmt::print("\t\t continuing due to datatype mismatch in loop_last_only (node is looplast:{})
    //     \n",node_s.is_type_loop_last());
    //   #endif
    //   continue; //flop node should be matched with flop node only! else datatype mismatch!
    // }

    /* which orig_MoS entries do we need to match it with?
     * I want those flop nodes only which has atleast 1 input matching with this synth_set.
     */
    absl::flat_hash_set<Node_pin::Compact_flat> relevant_orig_nodes;
    for (const auto& synth_in : synth_set) {
      auto orig_entry_it = inp_map_of_node_sets_orig.find(synth_in);
      if (orig_entry_it != inp_map_of_node_sets_orig.end()) {
        relevant_orig_nodes.insert((orig_entry_it->second).begin(), (orig_entry_it->second).end());
      }
    }
#ifdef BASIC_DBG
    fmt::print("\n relevant_orig_nodes = ");
    for (const auto& relevant_orig_node_cf : relevant_orig_nodes) {
      auto relevant_orig_node = Node_pin("lgdb", relevant_orig_node_cf);
      fmt::print(" {}\t",
                 relevant_orig_node.has_name() ? relevant_orig_node.get_name()
                                               : ("n" + std::to_string(relevant_orig_node.get_node().get_nid())));
    }
    fmt::print("\n");
#endif

    float                                       match_prev = 0.0;
    absl::flat_hash_set<Node_pin::Compact_flat> matched_node_pins;
#ifdef BASIC_DBG
    fmt::print("\nrelevant_orig_nodes.size() = {}, inp_map_of_sets_orig = {}\n",
               relevant_orig_nodes.size(),
               inp_map_of_sets_orig.size());
#endif
    // for(const auto &[ orig_key, orig_set ] : inp_map_of_sets_orig) {
    for (const auto& relevant_orig_node : relevant_orig_nodes) {
      auto orig_entry = inp_map_of_sets_orig.find(relevant_orig_node);
      auto orig_key   = orig_entry->first;
      auto orig_set   = orig_entry->second;
      // /*if(loop_last_only):for only those keys that are is_type_loop_last*/
      // auto node_o = Node_pin("lgdb", orig_key).get_node();
      // if ( !node_o.is_type_loop_last()  ) {
      //   continue; //flop node should be matched with flop node only! else datatype mismatch!
      // }

      const auto& in_match  = get_matching_weight(synth_set, orig_set);
      auto        out_match = 0.0;
      if (out_map_of_sets_synth.find(synth_key) != out_map_of_sets_synth.end()
          && out_map_of_sets_orig.find(orig_key) != out_map_of_sets_orig.end()) {  // both outs are present
        out_match = get_matching_weight(out_map_of_sets_synth[synth_key], out_map_of_sets_orig[orig_key]);
      } else if (out_map_of_sets_synth.find(synth_key) != out_map_of_sets_synth.end()
                 || out_map_of_sets_orig.find(orig_key) != out_map_of_sets_orig.end()) {  // only 1 out is present
        out_match = 0.0;                                                                  // no match at all
      } else {                                                                            // no out is present
        out_match = 5.0;                                                                  // complete match
      }

      float match_curr = in_match + out_match;

      if (match_curr > match_prev) {
        matched_node_pins.clear();
        matched_node_pins.insert(orig_key);
        match_prev = match_curr;
      } else if (match_curr == match_prev) {
        matched_node_pins.insert(orig_key);
      }
    }

    if (!matched_node_pins.empty()) {
      net_to_orig_pin_match_map[synth_key].insert(matched_node_pins.begin(), matched_node_pins.end());
#ifdef FOR_EVAL
      auto np_s = Node_pin("lgdb", synth_key);
      fmt::print("Inserting in weighted_match_LoopLastOnly : {}  :::  ",
                 np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())));
      for (auto np_o_set : matched_node_pins) {
        auto np_o = Node_pin("lgdb", np_o_set);
        fmt::print("    {} ", np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
      }
      fmt::print("\n");
#endif
      forced_match_vec.emplace_back(synth_key);
      if (flop_set.find(synth_key) != flop_set.end()) {
        flop_set.erase(synth_key);
      }
      remove_from_crit_node_set(synth_key);
      out_map_of_sets_synth.erase(synth_key);
      // inp_map_of_sets_synth.erase(it++);
      inp_map_of_sets_synth.erase(synth_key);
    }
    // else {
    //   it++;
    // }
  }
}

void Traverse_lg::weighted_match() {  // only for the crit_node_entries remaining!
#ifdef BASIC_DBG
  fmt::print("In weighted_match:\n\n");
#endif
  for (const auto& synth_key : crit_node_set) {
    I(inp_map_of_sets_synth.find(synth_key) != inp_map_of_sets_synth.end(), "\n synth_key NOT in inp_Map_of_sets_synth!! check!\n");
    const auto& synth_set = inp_map_of_sets_synth[synth_key];

    float                                       match_prev = 0.0;
    absl::flat_hash_set<Node_pin::Compact_flat> matched_node_pins;
    for (const auto& [orig_key, orig_set] : inp_map_of_sets_orig) {
      const auto& in_match  = get_matching_weight(synth_set, orig_set);
      auto        out_match = 0.0;
      if (out_map_of_sets_synth.find(synth_key) != out_map_of_sets_synth.end()
          && out_map_of_sets_orig.find(orig_key) != out_map_of_sets_orig.end()) {  // both outs are present
        out_match = get_matching_weight(out_map_of_sets_synth[synth_key], out_map_of_sets_orig[orig_key]);
      } else if (out_map_of_sets_synth.find(synth_key) != out_map_of_sets_synth.end()
                 || out_map_of_sets_orig.find(orig_key) != out_map_of_sets_orig.end()) {  // only 1 out is present
        out_match = 0.0;                                                                  // no match at all
      } else {                                                                            // no out is present
        out_match = 5.0;                                                                  // complete match
      }

      float match_curr = in_match + out_match;

      if (match_curr > match_prev) {
        matched_node_pins.clear();
        matched_node_pins.insert(orig_key);
        match_prev = match_curr;
      } else if (match_curr == match_prev) {
        matched_node_pins.insert(orig_key);
      }
    }

    if (!matched_node_pins.empty() && (match_prev>0.00) ) {
      net_to_orig_pin_match_map[synth_key].insert(matched_node_pins.begin(), matched_node_pins.end());
#ifdef FOR_EVAL
      auto np_s = Node_pin("lgdb", synth_key);
      fmt::print("Inserting in weighted_match : {} with wt {}  :::  ",
                 np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())), match_prev);
      for (auto np_o_set : matched_node_pins) {
        auto np_o = Node_pin("lgdb", np_o_set);
        fmt::print("    {} ", np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
      }
      fmt::print("\n");
#endif
      forced_match_vec.emplace_back(synth_key);
      if (flop_set.find(synth_key) != flop_set.end()) {
        flop_set.erase(synth_key);
      }
      // remove_from_crit_node_set(synth_key); // might lead to the error:
      //  && \"operator++ called on invalid iterator.\""' failed.
      //out_map_of_sets_synth.erase(synth_key);//commenting this should not impact anything because it is not iterated
      //inp_map_of_sets_synth.erase(synth_key);
    } else if (!matched_node_pins.empty()) {
			auto np_s = Node_pin("lgdb", synth_key);
			fmt::print("\nReporting {} entry does not match with any orig_node.\n", np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())) );//If you want to check what was not matched with anything, this is the place.
		} else {
      fmt::print("In weighted_match -- unexpected entry to else...Is it a no match whatsoever??");
    }
  }
  for (const auto& np : forced_match_vec) {
    remove_from_crit_node_set(np);
  }
}

bool Traverse_lg::set_theory_match(Traverse_lg::map_of_sets& io_map_of_sets_synth, Traverse_lg::map_of_sets& io_map_of_sets_orig) {
  bool some_matching_done = false;

  for (auto it = io_map_of_sets_synth.begin(); it != io_map_of_sets_synth.end();) {
    unsigned long match_count    = 0;
    unsigned long mismatch_count = 0;
    const auto&   synth_key      = it->first;
    const auto&   synth_set      = it->second;
    some_matching_done           = false;
    absl::flat_hash_set<Node_pin::Compact_flat> matched_node_pins;
    auto                                        counter       = 0;
    auto                                        counter_total = 0;
    for (const auto& [orig_key, orig_set] : io_map_of_sets_orig) {
#ifdef BASIC_DBG
      auto synth_key_name = Node_pin("lgdb", synth_key);
      auto orig_key_name  = Node_pin("lgdb", orig_key);
      fmt::print(
          "-- checking for synth key: {} VS. orig key: {} -- ",
          synth_key_name.has_name() ? synth_key_name.get_name() : ('n' + std::to_string(synth_key_name.get_node().get_nid())),
          orig_key_name.has_name() ? orig_key_name.get_name() : ('n' + std::to_string(orig_key_name.get_node().get_nid())));
#endif
      unsigned long matches  = 0;
      const auto&   smallest = synth_set.size() < orig_set.size() ? synth_set : orig_set;
      const auto&   largest  = synth_set.size() >= orig_set.size() ? synth_set : orig_set;

      for (const auto& it_set : smallest) {
        if (largest.contains(it_set)) {
          ++matches;
        }
      }
      unsigned long mismatches = synth_set.size() + orig_set.size() - matches - matches;
      if (matches > match_count) {
        matched_node_pins.clear();
        matched_node_pins.insert(orig_key);
        match_count    = matches;
        mismatch_count = mismatches;
#ifdef BASIC_DBG
        fmt::print(" more matches (m:{}, mm:{}).\n", matches, mismatches);
#endif
      } else if ((matches == match_count) && (mismatches < mismatch_count)) {
        matched_node_pins.clear();
        matched_node_pins.insert(orig_key);
        match_count    = matches;
        mismatch_count = mismatches;
#ifdef BASIC_DBG
        fmt::print(" lesser mismatches (m:{}, mm:{}).\n", matches, mismatches);
#endif
      } else if ((matches == match_count) && (mismatches == mismatch_count)) {  // matches and mismatches are same as prev.
                                                                                // iteration
        matched_node_pins.insert(orig_key);
#ifdef BASIC_DBG
        fmt::print(" same num of matches/mismatches (m:{}, mm:{}).\n", matches, mismatches);
#endif
      } else {
        counter++;
        (void)counter;
#ifdef BASIC_DBG
        fmt::print(" counter incremented (m:{}, mm:{}). \n", matches, mismatches);
#endif
      }
      counter_total++;
      (void)counter_total;
#ifdef BASIC_DBG
      if (synth_key_name.has_name()) {
        fmt::print("\t\t\t ss: \t\t\t ");
        for (auto ss : synth_set) {
          fmt::print(" {}, ",
                     Node_pin("lgdb", ss).has_name() ? Node_pin("lgdb", ss).get_name()
                                                     : std::to_string(Node_pin("lgdb", ss).get_node().get_nid()));
        }
        fmt::print("\n");
        fmt::print("\t\t\t Os: \t\t\t ");
        for (auto os : orig_set) {
          fmt::print(" {}, ",
                     Node_pin("lgdb", os).has_name() ? Node_pin("lgdb", os).get_name()
                                                     : std::to_string(Node_pin("lgdb", os).get_node().get_nid()));
        }
        fmt::print("\n");
      }
#endif
    }
    if (!matched_node_pins.empty()) {
#ifdef BASIC_DBG
      fmt::print("1-- matches:{}, mism:{}, counter:{}, c_t:{}\n", match_count, mismatch_count, counter, counter_total);
#endif
      net_to_orig_pin_match_map[synth_key].insert(matched_node_pins.begin(), matched_node_pins.end());
#ifdef FOR_EVAL
      auto np_s = Node_pin("lgdb", synth_key);
      fmt::print("Inserting in set_theory_match : {}  :::  ",
                 np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())));
      for (auto np_o_set : matched_node_pins) {
        auto np_o = Node_pin("lgdb", np_o_set);
        fmt::print("    {} ", np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
      }
      fmt::print("\n");
#endif
      forced_match_vec.emplace_back(synth_key);
      if (flop_set.find(synth_key) != flop_set.end()) {
        flop_set.erase(synth_key);
      }
      remove_from_crit_node_set(synth_key);
      inp_map_of_sets_synth.erase(synth_key);
      out_map_of_sets_synth.erase(synth_key);
      io_map_of_sets_synth.erase(it++);
      // FIXME: add erase from orig Maps here also?
      some_matching_done = true;
    } else {
      it++;
#ifdef BASIC_DBG
      fmt::print("2-- matches:{}, mism:{}, counter:{}, c_t:{}\n", match_count, mismatch_count, counter, counter_total);
#endif
    }
  }

  return some_matching_done;
}

Traverse_lg::map_of_sets Traverse_lg::make_in_out_union(const map_of_sets& inp_map_of_sets, const map_of_sets& out_map_of_sets,
                                                        bool loop_last_only, bool union_of_crit_entries_only) const {
  /* make union of inp_map_of_sets and out_map_of_sets
   * if(loop_last_only):for only those keys that are is_type_loop_last
   * */

  Traverse_lg::map_of_sets io_map;
  for (const auto& [in_key, in_val_set] : inp_map_of_sets) {
    if (union_of_crit_entries_only) {
      if (crit_node_set.find(in_key) == crit_node_set.end()) {  // not found in crit_node_set
        /*need to work on this in_key only if it is ppresent in crit_node_set*/
        continue;
      }
    }

    auto o_s = Node_pin("lgdb", in_key).get_node();
    if ((loop_last_only && !o_s.is_type_loop_last()) || (!loop_last_only && o_s.is_type_loop_last())) {
      continue;  // flop node should be matched with flop node only! else datatype mismatch!
    }

    auto                                        it = out_map_of_sets.find(in_key);
    absl::flat_hash_set<Node_pin::Compact_flat> io_set;
    if (it != out_map_of_sets.end()) {
      const auto out_val_set = it->second;
      io_set                 = get_union(in_val_set, out_val_set);
    } else {
      io_set = in_val_set;
    }
    io_map[in_key].insert(io_set.begin(), io_set.end());
  }
  return io_map;
}

void Traverse_lg::path_traversal(const Node& start_node, const std::set<std::string> synth_set,
                                 const std::vector<Node::Compact_flat>& synth_val, Traverse_lg::setMap_pairKey& cellIOMap_orig) {
  Node this_node = start_node;
  for (const auto& oute : this_node.out_edges()) {
    const auto s = oute.sink;
    this_node    = s.get_node();
    if (matched_color_map.find(this_node.get_compact_flat()) != matched_color_map.end()) {
      continue;
    } else if ((this_node.has_color() ? (this_node.get_color() != VISITED_COLORED) : true) && (!is_endpoint(this_node))) {
      /*it is unvisited combinational cell in orig graph*/
      this_node.set_color(VISITED_COLORED);

      std::set<std::string> nodes_in_set;
      std::set<std::string> nodes_out_set;
      std::set<std::string> nodes_io_set;
      get_input_node(s, nodes_in_set, nodes_io_set);
      get_output_node(s, nodes_out_set, nodes_io_set);

      fmt::print("\n -- Entering check_in_cellIOMap_synth( n{} ) with: --\n", this_node.get_nid());
      fmt::print("\t in_set:");
      for (const auto& i : nodes_in_set) {
        fmt::print("\t\t{}, ", i);
      }
      fmt::print("\n\t out_set:");
      for (const auto& i : nodes_out_set) {
        fmt::print("\t\t{}, ", i);
      }
      fmt::print("\n");

      // insert to cellIOMap_orig
      const auto&                     nodeid = this_node.get_compact_flat();
      std::vector<Node::Compact_flat> tmpVec;
      if (cellIOMap_orig.find(std::make_pair(nodes_in_set, nodes_out_set)) != cellIOMap_orig.end()) {
        tmpVec.assign((cellIOMap_orig[std::make_pair(nodes_in_set, nodes_out_set)]).begin(),
                      (cellIOMap_orig[std::make_pair(nodes_in_set, nodes_out_set)]).end());
        tmpVec.emplace_back(nodeid);
      } else {
        tmpVec.emplace_back(nodeid);
      }
      cellIOMap_orig[std::make_pair(nodes_in_set, nodes_out_set)] = tmpVec;

      auto val = check_in_cellIOMap_synth(nodes_in_set, nodes_out_set, this_node);
      fmt::print("Found the match for n{} node?: {}", this_node.get_compact_flat().get_nid(), val);
      // recursively go to this_node's sinks now (moving fwd in the path)
      path_traversal(this_node, synth_set, synth_val, cellIOMap_orig);

      // if this_node is now in matching_map, (so node resolved), delete cellIOMap_orig and move on
      if (matched_color_map.find(this_node.get_compact_flat()) != matched_color_map.end() /*&& !cellIOMap_orig.empty()*/) {
        fmt::print("\ncellIOMap_orig for resolved node is:\n");
        print_MapOf_SetPairAndVec(cellIOMap_orig);
        // cellIOMap_orig.clear();
      } else {
        //  set_theory_match(iterator to cellIOMap_synth.this entry , cellIOMap_orig)
        set_theory_match(synth_set, synth_val, cellIOMap_orig);
        fmt::print("\ncellIOMap_orig for unresolved node is:\n");
        print_MapOf_SetPairAndVec(cellIOMap_orig);
        // cellIOMap_orig.clear();
      }

    } else if (this_node.has_color() ? (this_node.get_color() == VISITED_COLORED) : false) {
      continue;
    } else if (is_endpoint(this_node)) {
      continue;
    } else {
      I(false, "\nISSUE TO DEBUG!\n");
    }
  }
  return;
}

bool Traverse_lg::check_in_cellIOMap_synth(std::set<std::string>& in_set, std::set<std::string>& out_set, Node& start_node) {
  /*if (this node is NOT a subset of any entry in cellIOMap_synth) return false
   *  -currently a complete match is considered; subset is a future FIXME-
   * returning F because : stop iterating for its sink pins as this path is not going anywhere in criticality
   * else return T
   * Also: put the data in matching_map? or some other map to reflect matched combo cells?
   * */

  // make io pair of the 2 sets from LGorig
  auto pair_to_find = std::make_pair(in_set, out_set);
  // find the pair in keys of the cellIOMap_synth
  if (cellIOMap_synth.find(pair_to_find) != cellIOMap_synth.end()) {
    // if found, put this start_node of LGorig against value from cellIOMap_synth for LGsynth to matching_map AND return T
    auto found_synth_cell_vals = cellIOMap_synth[pair_to_find];
    for (const auto& n : found_synth_cell_vals) {
      std::vector<Node::Compact_flat> tmpVec;
      if (matching_map.find(n) != matching_map.end()) {
        tmpVec.assign((matching_map[n]).begin(), (matching_map[n]).end());
        tmpVec.emplace_back(start_node.get_compact_flat());
      } else {
        tmpVec.emplace_back(start_node.get_compact_flat());
      }
      matching_map[n] = tmpVec;
      // FIXME: remove from crit_cell_list; better still: remove crit_cell_list (not required)
      for (auto cfl_it = crit_cell_list.begin(); cfl_it != crit_cell_list.end(); cfl_it++) {
        if (*cfl_it == n) {
          crit_cell_list.erase(cfl_it);
          cfl_it--;
        }
      }
      // put orig combo node in matched_color_map. color value will be crit_cell_map[n]:
      matched_color_map[start_node.get_compact_flat()] = crit_cell_map[n];  // FIXME: check if node in map already then add to the
                                                                            // color or keep larger color. do not just overwrite
    }

    return true;
  }
  // if not found, return F
  return false;
}

bool Traverse_lg::is_startpoint(const Node& node_to_eval) const {
  /*if (node is flop or graph input) return true else return false*/
  if (node_to_eval.is_type_flop()
      || (node_to_eval.is_type_sub() ? ((std::string(node_to_eval.get_type_sub_node().get_name())).find("_df") != std::string::npos)
                                     : false)
      || node_to_eval.is_graph_input() || node_to_eval.is_type_loop_last() || node_to_eval.is_type_loop_first()) {
    return true;
  }
  return false;
}

bool Traverse_lg::is_endpoint(const Node& node_to_eval) const {
  /*if (node is flop or graph output) return true else return false*/
  if (node_to_eval.is_type_flop()
      || (node_to_eval.is_type_sub() ? ((std::string(node_to_eval.get_type_sub_node().get_name())).find("_df") != std::string::npos)
                                     : false)
      || node_to_eval.is_graph_output() || node_to_eval.is_type_loop_last() || node_to_eval.is_type_loop_first()) {
    return true;
  }
  return false;
}

std::vector<std::string> Traverse_lg::get_map_val(
    absl::node_hash_map<Node::Compact_flat, std::vector<Node::Compact_flat>>& find_in_map, std::string key_str) {
  std::vector<std::string> ret_vec;
  for (auto& [k, v] : find_in_map) {
    auto node_str = std::to_string(k.get_nid().value);
    // fmt::print("**{}, {}\n", node_str, key_str);
    if (node_str == key_str) {
      for (auto v1 : v) {
        ret_vec.emplace_back(std::to_string(v1.get_nid().value));
      }
    }
  }
  return ret_vec;
}

// void Traverse_lg::get_input_node(const Node_pin &node_pin, absl::btree_set<std::string>& in_set) {
// Node_pin/*FIXME?: ::Compact_flat*/ Traverse_lg::get_input_node(const Node_pin &node_pin, std::set<std::string>& in_set,
// std::set<std::string>& io_set, bool addToCFL) {
void Traverse_lg::get_input_node(const Node_pin& node_pin, std::set<std::string>& in_set, std::set<std::string>& io_set,
                                 bool addToCFL) {
  auto node = node_pin.get_node();

  if (node.is_type_flop() || (!node.has_inputs())
      || (node.is_type_sub() ? ((std::string(node.get_type_sub_node().get_name())).find("_df") != std::string::npos) : false)
      || node.is_type_loop_last() || node.is_type_loop_first()) {
    if (node.is_type_const() || (node.is_type_sub() && node.get_type_sub_node().get_name() == "__fir_const")) {
      // do not keep const for future reference
      // return Node::Compact_flat();
      return;
    } else if (node.is_graph_io()) {
      auto tmp_x = node_pin.has_name() ? node_pin.get_name() : node_pin.get_pin_name();
      if (tmp_x != "reset_pin" && tmp_x != "clock_pin") {
        in_set.insert(node_pin.has_name() ? node_pin.get_name() : node_pin.get_pin_name());
        io_set.insert(node_pin.has_name() ? node_pin.get_name() : node_pin.get_pin_name());
        // return node.get_compact_flat();
      }
      // return Node::Compact_flat();
    } else {
      bool        isFlop = (node.is_type_flop()
                     || (node.is_type_sub() ? ((std::string(node.get_type_sub_node().get_name())).find("_df") != std::string::npos)
                                                   : false));
      std::string temp_str(isFlop ? "flop"
                                  : (node.is_type_sub() ? (std::string(node.get_type_sub_node().get_name()))
                                                        : node.get_type_name()));  // if it is a flop, write "flop" else evaluate

      if (isFlop) {
        temp_str += ":";
        // temp_str+=node_pin.get_pin_name();//in inputs, this is always Y/Q.
        // temp_str+="->";
        // temp_str+=node.get_driver_pin().get_pin_name();
        // temp_str+="(";
        // temp_str+=(node.get_driver_pin().get_wire_name());
        // temp_str+=(node.has_name()?node.get_name():node.out_connected_pins()[0].get_wire_name());//FIXME:changed to line below
        // temporarily for debugging. revert back!
        temp_str += std::to_string(node.get_compact_flat().get_nid());
        // temp_str+=")";
        if (addToCFL
            && (std::find(crit_flop_list.begin(), crit_flop_list.end(), (node.get_compact_flat())) == crit_flop_list.end())) {
          crit_flop_list.emplace_back(node.get_compact_flat());
          crit_flop_map[node.get_compact_flat()]
              = node.has_color() ? node.get_color()
                                 : 0;  // keeping here 0 for no color. for now.
                                       // node.set_color(0);//setting color to this flop node so that it gets capture as colored
                                       // node and IO is calculated for this now so-called-crit-flop.
        }
      }
      in_set.insert(temp_str);
      if (!isFlop) {
        io_set.insert(temp_str);
      }  // do not want flops in pure io_set
    }
    return;  // get_dpin_cf(node);
  } else {
    for (const auto& ine : node.inp_edges()) {
      get_input_node(ine.driver, in_set, io_set, addToCFL);
    }
  }
}

// void Traverse_lg::get_output_node(const Node_pin &node_pin, absl::btree_set<std::string>& out_set) {
void Traverse_lg::get_output_node(const Node_pin& node_pin, std::set<std::string>& out_set, std::set<std::string>& io_set,
                                  bool addToCFL) {
  auto node = node_pin.get_node();
  if (node.is_type_flop() || (!node.has_outputs())
      || (node.is_type_sub() ? ((std::string(node.get_type_sub_node().get_name())).find("_df") != std::string::npos) : false)
      || node.is_type_loop_last() || node.is_type_loop_first()) {
    if (node.is_graph_io()) {
      // out_set.emplace_back(node_pin.has_name()?node_pin.get_name():node_pin.get_pin_name());
      out_set.insert(node_pin.get_pin_name());
      io_set.insert(node_pin.get_pin_name());
    } else {
      bool        isFlop = (node.is_type_flop()
                     || (node.is_type_sub() ? ((std::string(node.get_type_sub_node().get_name())).find("_df") != std::string::npos)
                                                   : false));
      std::string temp_str(isFlop ? "flop"
                                  : (node.is_type_sub() ? (std::string(node.get_type_sub_node().get_name()))
                                                        : node.get_type_name()));  // if it is a flop, write "flop" else evaluate

      if (isFlop) {
        temp_str += ":";
        // temp_str+=node_pin.get_pin_name();//in outputs, this is always din/D
        // temp_str+="->";
        // temp_str+=node.get_driver_pin().get_pin_name();
        // temp_str+="(";
        // temp_str+=(node.get_driver_pin().get_wire_name());
        // temp_str+=(node.has_name()?node.get_name():node.out_connected_pins()[0].get_wire_name());//FIXME:changed to line below
        // temporarily for debugging. revert back!
        temp_str += std::to_string(node.get_compact_flat().get_nid());
        // temp_str+=")";
        if (addToCFL
            && (std::find(crit_flop_list.begin(), crit_flop_list.end(), (node.get_compact_flat())) == crit_flop_list.end())) {
          crit_flop_list.emplace_back(node.get_compact_flat());
          crit_flop_map[node.get_compact_flat()]
              = node.has_color() ? node.get_color()
                                 : 0;  // keeping here 0 for no color. for now.
                                       // node.set_color(0);//setting color to this flop node so that it gets capture as colored
                                       // node and IO is calculated for this now so-called-crit-flop.
        }
      }
      out_set.insert(temp_str);
      if (!isFlop) {
        io_set.insert(temp_str);
      }  // do not want flops in pure io_set
    }
    return;
  } else {
    for (const auto& oute : node.out_edges()) {
      get_output_node(oute.sink, out_set, io_set, addToCFL);
    }
  }
}

void Traverse_lg::print_set(const absl::flat_hash_set<Node_pin::Compact_flat>& set_of_dpins) const {
  for (const auto& v : set_of_dpins) {
    auto n = Node_pin("lgdb", v).get_node();
    auto p = Node_pin("lgdb", v);
    if (p.has_name()) {
      fmt::print("\t {},{} ", p.get_name(), p.get_pid());
    } else {
      fmt::print("\t n{},{} ", n.get_nid(), p.get_pid());
    }
  }
}
void Traverse_lg::print_name2dpin(const absl::flat_hash_map<std::string, Node_pin::Compact_flat>& name2dpin) const {
  fmt::print("\n Printing name2dpin \n");
  for (const auto& [str, node_pin_cf] : name2dpin) {
    auto p = Node_pin("lgdb", node_pin_cf);
    fmt::print("\t\t {} -:- {},n{}\n", str, p.get_wire_name(), p.get_node().get_nid());
  }
}
void Traverse_lg::print_name2dpins(
    const absl::flat_hash_map<std::string, absl::flat_hash_set<Node_pin::Compact_flat>>& name2dpins) const {
  fmt::print("\n Printing name2dpins \n");
  for (const auto& [str, node_pin_cfs] : name2dpins) {
    fmt::print("\t\t {} -:- ", str);
    for (const auto& node_pin_cf : node_pin_cfs) {
      auto p = Node_pin("lgdb", node_pin_cf);
      fmt::print(" {},n{}\t\t", p.get_wire_name(), p.get_node().get_nid());
    }
    fmt::print("\n");
  }
}
void Traverse_lg::print_io_map(const Traverse_lg::map_of_sets& the_map_of_sets) const {
  for (const auto& [node_pin_cf, set_pins_cf] : the_map_of_sets) {
    auto p = Node_pin("lgdb", node_pin_cf);
#ifndef FULL_RUN_FOR_EVAL
    auto n = Node_pin("lgdb", node_pin_cf).get_node();
    if (p.has_name()) {
      fmt::print("{},n{},{}({}) \t::: ",
                 /*n.get_or_create_name(),*/ p.get_name(),
                 n.get_nid(),
                 p.get_pid(),
                 p.get_top_lgraph()->get_name());
    } else {
      fmt::print("n{},{}({}) \t::: ", /*n.get_or_create_name(),*/ n.get_nid(), p.get_pid(), p.get_top_lgraph()->get_name());
    }
#else
    if (p.has_name()) {
      fmt::print("{} ::: ", p.get_name());
    }
#endif
    for (const auto& pin_cf : set_pins_cf) {
      const auto pin = Node_pin("lgdb", pin_cf);
#ifndef FULL_RUN_FOR_EVAL
      auto n_s = Node_pin("lgdb", pin_cf).get_node();
      if (pin.has_name()) {
        fmt::print("{},n{},{}({}) \t",
                   /* n_s.get_or_create_name(),*/ pin.get_name(),
                   n_s.get_nid(),
                   pin.get_pid(),
                   pin.get_top_lgraph()->get_name());
      } else {
        fmt::print("n{},{}({}) \t", /* n_s.get_or_create_name(),*/ n_s.get_nid(), pin.get_pid(), pin.get_top_lgraph()->get_name());
      }
#else
      if (pin.has_name()) {
        fmt::print("{} ", pin.get_name());
      }
#endif
    }
    fmt::print("\n");
  }
}

void Traverse_lg::print_IOtoNodeMap_synth(const absl::node_hash_map<std::set<std::string>, setMap_pairKey>& mapInMap) {
  for (const auto& [ioval, inMap] : mapInMap) {
    for (const auto& ip : ioval) {
      fmt::print("{}\t", ip);
    }
    fmt::print("\n");

    for (const auto& [ioPair, n_list] : inMap) {
      fmt::print("\t\t\t\t");
      for (const auto& ip : ioPair.first) {
        fmt::print("{}\t", ip);
      }
      fmt::print("||| \t");
      for (const auto& op : ioPair.second) {
        fmt::print("{}\t", op);
      }
      fmt::print("::: \t");
      for (const auto& n : n_list) {
        fmt::print("{}\t", n.get_nid());
      }
      fmt::print("\n");
    }
    fmt::print("\n");
  }
}

void Traverse_lg::print_MapOf_SetPairAndVec(const setMap_pairKey& MapOf_SetPairAndVec) {
  for (const auto& [iov, fn] : MapOf_SetPairAndVec) {
    for (const auto& ip : iov.first) {
      fmt::print("{}\t", ip);
    }
    fmt::print("||| \t");
    for (const auto& op : iov.second) {
      fmt::print("{}\t", op);
    }
    fmt::print("::: \t");
    for (const auto& op : fn) {
      fmt::print("n{}\t", op.get_nid());
    }
    fmt::print("\n\n");
  }
}

// bool Traverse_lg::set_theory_match(setMap_pairKey::iterator &map_it, setMap_pairKey &orig_map)
bool Traverse_lg::set_theory_match(std::set<std::string> synth_set, const std::vector<Node::Compact_flat>& synth_val,
                                   setMap_pairKey& orig_map) {
  // auto map_entry = *map_it;
  // auto synth_set = getUnion(map_entry.first.first, map_entry.first.second);
  unsigned long match_count = 0;
  // auto mismatch_count = synth_set.size(); mismatch_count = 0;
  unsigned long                   mismatch_count = 0;
  std::vector<Node::Compact_flat> orig_nodes_matched;

  for (auto& [k, v] : orig_map) {
    auto orig_set = getUnion(k.first, k.second);

    std::vector<std::string>           setIntersectionVec(synth_set.size() + orig_set.size());
    std::vector<std::string>::iterator ls;
    ls = std::set_intersection(synth_set.begin(), synth_set.end(), orig_set.begin(), orig_set.end(), setIntersectionVec.begin());
    auto intersectionVal = ls - setIntersectionVec.begin();  //(v3.resize(ls-v3.begin())).size()

    auto unionSet = getUnion(synth_set, orig_set);
    auto unionVal = unionSet.size();

    unsigned long new_match_count = intersectionVal;
    unsigned long new_mismatch_count
        = 3 * (unionVal - intersectionVal);  // 3 is the penalty number. coz mismatch is worse than less matches. (i.e we want a
                                             // subset rather than the synth node with added inputs)

    if (new_match_count > match_count) {
      /*this is a better match. keep "v" in orig_nodes_matched. update match_count=new_match_count*/
      orig_nodes_matched = v;
      match_count        = new_match_count;
    } else if (new_match_count == match_count) {
      if (new_mismatch_count < mismatch_count) {
        /*this is a better match. keep "v" in orig_nodes_matched. update match_count=new_match_count*/
        orig_nodes_matched = v;
        mismatch_count     = new_mismatch_count;
      }
    }  // else keep iterating

  }  // end of iterating orig_map
  if (orig_nodes_matched.size() > 0) {
    /*put in matching_map and matched_color_map*/
    for (auto& n : synth_val) {
      for (const auto& orig_node : orig_nodes_matched) {
        std::vector<Node::Compact_flat> tmpVec;
        if (matching_map.find(n) != matching_map.end()) {
          tmpVec.assign((matching_map[n]).begin(), (matching_map[n]).end());
          tmpVec.emplace_back(orig_node);
        } else {
          tmpVec.emplace_back(orig_node);
        }
        matching_map[n] = tmpVec;
      }

      if (crit_flop_map.find(n) != crit_flop_map.end()) {
        /*if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color*/
        for (const auto& o_n : matching_map[n]) {
          matched_color_map[o_n] = crit_flop_map[n];
        }
      }
      /*if synNode in crit_flop_list, remove from crit_flop_list;*/
      for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
        if (*cfl_it == n) {
          crit_flop_list.erase(cfl_it);
          cfl_it--;
        }
      }
      if (crit_cell_map.find(n) != crit_cell_map.end()) {
        /*if synnode in crit_cell_map, color corresponding orig nodes with this synnode's color*/
        for (const auto& o_n : matching_map[n]) {
          matched_color_map[o_n] = crit_cell_map[n];
        }
      }
      /*if synNode in crit_cell_list, remove from crit_cell_list;*/
      for (auto cfl_it = crit_cell_list.begin(); cfl_it != crit_cell_list.end(); cfl_it++) {
        if (*cfl_it == n) {
          crit_cell_list.erase(cfl_it);
          cfl_it--;
        }
      }
    }
    return true;
  } else {
    return false;
  }
}
