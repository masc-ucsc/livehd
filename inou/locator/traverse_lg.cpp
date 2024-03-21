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
  register_pass(m1);
}

Traverse_lg::Traverse_lg(const Eprp_var& var) : Pass("inou.traverse_lg", var) {}

void Traverse_lg::travers(Eprp_var& var) {
  TRACE_EVENT("inou", "traverse_lg");

  auto lg_orig_name  = var.get("LGorig");
  auto lg_synth_name = var.get("LGsynth");

  Traverse_lg p(var);
  p.orig_lg_name = std::string(lg_orig_name);
  p.start_time_of_algo = std::chrono::system_clock::now();
  p.crossover_count=0;
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

  //p.debug_function(orig_lg);
  //p.debug_function(synth_lg);
  //return;
  
  auto start = std::chrono::system_clock::now();
  p.make_io_maps_boundary_only(orig_lg, p.inp_map_of_sets_orig, p.out_map_of_sets_orig, true);  // orig-boundary only
  fmt::print("\n make_io_maps_boundary_only - orig done.\n");
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = end-start;
  fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: make_io_maps_boundary_only-originalLG\n", elapsed_seconds.count());
#ifdef BASIC_DBG
  fmt::print("1. p.make_io_maps_boundary_only(orig_lg, p.inp_map_of_sets_orig, p.out_map_of_sets_orig)//orig-boundary only\n");
  p.print_everything();
#endif

  start = std::chrono::system_clock::now();
  p.netpin_to_origpin_default_match(orig_lg, synth_lg);  // know all the inputs and outputs match by name (known points.)
  fmt::print("\n netpin_to_origpin_default_match done.\n");
#ifdef BASIC_DBG
  fmt::print(
      "2. p.netpin_to_origpin_default_match(orig_lg, synth_lg);//know all the inputs and outputs match by name (known points.)\n");
  p.print_everything();
#endif
  end = std::chrono::system_clock::now();
  elapsed_seconds = end-start;
  fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: netpin_to_origpin_default_match\n", elapsed_seconds.count());

  start = std::chrono::system_clock::now();
  p.make_io_maps_boundary_only(synth_lg, p.inp_map_of_sets_synth, p.out_map_of_sets_synth, false);  // synth-boundary only + matching
  end = std::chrono::system_clock::now();
  elapsed_seconds = end-start;
  fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: make_io_maps_boundary_only-synth\n", elapsed_seconds.count());
  fmt::print("\n make_io_maps_boundary_only - synth done.\n");
#ifdef BASIC_DBG
  fmt::print(
      "3. p.make_io_maps_boundary_only(synth_lg, p.inp_map_of_sets_synth, p.out_map_of_sets_synth);//synth-boundary only + "
      "matching\n");
  p.print_everything();
#endif
#ifndef FULL_RUN_FOR_EVAL
  start = std::chrono::system_clock::now();
  p.remove_lib_loc_from_orig_lg(orig_lg);
  end = std::chrono::system_clock::now();
  elapsed_seconds = end-start;
  fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: remove_lib_loc_from_orig_lg(orig_lg)\n", elapsed_seconds.count());
#endif

  p.do_travers(orig_lg, synth_lg, true);  // original LG (pre-syn LG)
  fmt::print("\n do_travers - orig done.\n");
#ifdef BASIC_DBG
  fmt::print("4. p.do_travers(orig_lg, true);  // original LG (pre-syn LG)\n");
  p.print_everything();
#endif
  p.do_travers(orig_lg, synth_lg,  false);  // synth LG
  fmt::print("\n do_travers - synth done.\n");
#ifdef BASIC_DBG
  fmt::print("\n5. p.do_travers(synth_lg, false);  // synth LG\n");
  p.print_everything();
#endif

  I(p.crit_node_set.empty(), "\n***********\nCHECK:\n\t\t All marked nodes still not resolved??\n***********\n");
  p.report_critical_matches_with_color();

#endif
}

void Traverse_lg::remove_lib_loc_from_orig_lg(Lgraph* orig_lg) {
	/* This is to remove loc and source file information from nodes having source files like Reg.scala
	 * These files are like liberty files and we never mark DT on these files for Eval*/
  for (auto node : orig_lg->fast(true)) {
		if(node.has_loc()){
			auto fname = node.get_source();
			if ( (fname == "Reg.scala") || (fname == "Interrupts.scala") || (fname == "Mux.scala") ) {
				node.del_source();
			}
		}
	}
}

void Traverse_lg::debug_function(Lgraph* lg) {
  fmt::print("lg->dump(true):\n");
  lg->dump(true);
  fmt::print("---------------------------------------------------\n");
  // fmt::print("lg->dump():\n");
  // lg->dump();
  // fmt::print("---------------------------------------------------\n");

  lg->each_graph_input([this](const Node_pin dpin) {
    fmt::print("   {}({})\n", dpin.has_name() ? dpin.get_name() : (std::to_string(dpin.get_pid())), dpin.get_wire_name());

    auto hdpin = dpin.get_hierarchical();
    for(const auto sink_dpin: hdpin.out_sinks()){
      for (const auto p: sink_dpin.get_node().out_connected_pins()){
	      fmt::print("-GI- ");get_node_pin_compact_flat_details(p.get_compact_flat());
      }
    }
  });
  lg->each_graph_output([this](const Node_pin dpin) {
    fmt::print("   {}({})\n", dpin.has_name() ? dpin.get_name() : (std::to_string(dpin.get_pid())), dpin.get_wire_name());
    auto hdpin = dpin.get_hierarchical();
    auto spin = hdpin.change_to_sink_from_graph_out_driver();
    for(const auto driver_dpin: spin.inp_drivers()){
      fmt::print("-GO- ");get_node_pin_compact_flat_details(driver_dpin.get_compact_flat());
    }
  });

  fmt::print("--------graph IO done -------------------------------------------\n");
  for (const auto& node : lg->forward(true)) {
    if (node.has_outputs()) {
      fmt::print("{}(n{})({})\n", node.debug_name(), node.get_nid(), (node.get_class_lgraph())->get_name());
      if (node.is_type_sub() && node.get_type_sub_node().get_name() == "__fir_const") {
        auto node_sub_name = node.get_type_sub_node().get_name();
        fmt::print("\t\t\t\t {}\n", node_sub_name);
      }
      for (const auto dpin : node.out_connected_pins()) {
        fmt::print("\t {}({})", dpin.has_name() ? dpin.get_name() : (std::to_string(dpin.get_pid())), dpin.get_wire_name());
	get_node_pin_compact_flat_details(dpin.get_compact_flat());
      }
      fmt::print("\n");
    }
    for (const auto dpin : node.out_connected_pins()) {
      get_node_pin_compact_flat_details(dpin.get_compact_flat());
    }
   
    for(const auto e: node.out_edges()){
      for (const auto p: e.sink.get_node().out_connected_pins()){
	      fmt::print("---");get_node_pin_compact_flat_details(p.get_compact_flat());
      }
    }
  }
  fmt::print("\n--------------------fast:-------------------------------");
  for (const auto& node : lg->fast(true)) {
    if (node.has_outputs()) {
      fmt::print("{}(n{})({})\n", node.debug_name(), node.get_nid(), (node.get_class_lgraph())->get_name());
      if (node.is_type_sub() && node.get_type_sub_node().get_name() == "__fir_const") {
        auto node_sub_name = node.get_type_sub_node().get_name();
        fmt::print("\t\t\t\t {}\n", node_sub_name);
      }
      for (const auto dpin : node.out_connected_pins()) {
        fmt::print("\t {}({})", dpin.has_name() ? dpin.get_name() : (std::to_string(dpin.get_pid())), dpin.get_wire_name());
	get_node_pin_compact_flat_details(dpin.get_compact_flat());
      }
      fmt::print("\n");
    }
    for (const auto dpin : node.out_connected_pins()) {
      get_node_pin_compact_flat_details(dpin.get_compact_flat());
    }
   
    for(const auto e: node.out_edges()){
      for (const auto p: e.sink.get_node().out_connected_pins()){
	      fmt::print("---");get_node_pin_compact_flat_details(p.get_compact_flat());
      }
    }
  }
  fmt::print("\n---------------------------------------------------");
}

// FOR SET:
// DE_DUP
void Traverse_lg::do_travers(Lgraph *orig_lg, Lgraph *synth_lg, bool is_orig_lg) {  

  if (!is_orig_lg) {
    auto start = std::chrono::system_clock::now();
    make_io_maps(synth_lg, inp_map_of_sets_synth, out_map_of_sets_synth, is_orig_lg);  // has in-place resolution as well.
    fmt::print("\n make_io_maps - synth done.\n");
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end-start;
    fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: make_io_maps-synthLG\n", elapsed_seconds.count());
    #ifdef BASIC_DBG
    fmt::print("7.0. Printing before 1st set of resolution -- synth");
    print_everything();
    #endif
    // start = std::chrono::system_clock::now();
    // resolution_of_synth_map_of_sets(inp_map_of_sets_synth); /*Not required because make_io_maps has in-place resolution*/
    // resolution_of_synth_map_of_sets(out_map_of_sets_synth);
    // end = std::chrono::system_clock::now();
    // elapsed_seconds = end-start;
    // fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: resolution_of_synth_map_of_sets-after-make_io_mapsSynth\n", elapsed_seconds.count()); 
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
    bool change_done=false;
    if (!crit_node_set.empty() && !flop_set_synth.empty()){
      start = std::chrono::system_clock::now();
      //fmt::print("Before complete_io_match(flop only)"); print_SynthSet_sizes();
      change_done = complete_io_match(true);  // for flop only as matching flop first
      fmt::print("\n complete_io_match - synth - flop only (outside while) done.\n");
      end = std::chrono::system_clock::now();
      elapsed_seconds = end-start;
      fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: complete_io_match-flopsOnly-1stTime\n", elapsed_seconds.count());
    }
    if (crit_node_set.empty()) {
      /*all required matching done*/
      report_critical_matches_with_color();
    }
#ifdef BASIC_DBG
    fmt::print("8. before resolution + matching while loop starts -- synth");
    print_everything();
#endif

    while (change_done && !crit_node_set.empty() && !flop_set_synth.empty()) {  // for flop only as matching flop first
      start = std::chrono::system_clock::now();
      make_io_maps(synth_lg, inp_map_of_sets_synth, out_map_of_sets_synth, is_orig_lg);
      make_io_maps(orig_lg, inp_map_of_sets_orig, out_map_of_sets_orig, true);
      /*resolution_of_synth_map_of_sets(inp_map_of_sets_synth); //if make_io_maps takes way less time, why not replace resolution_of_synth_map_of_sets with make_io_maps!
      resolution_of_synth_map_of_sets(out_map_of_sets_synth);*/
      end = std::chrono::system_clock::now();
      elapsed_seconds = end-start;
      fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: resolution_of_synth_map_of_sets-inWhile-flopOnly\n", elapsed_seconds.count());

      start = std::chrono::system_clock::now();
      //fmt::print("Before complete_io_match(flop only)"); print_SynthSet_sizes();
      change_done = complete_io_match(true);  // alters crit_node_set too
      end = std::chrono::system_clock::now();
      elapsed_seconds = end-start;
      fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: complete_io_match-inWhile-flopOnly\n", elapsed_seconds.count());
      fmt::print("\n complete_io_match - synth - flop only (inside while) done.\n");

#ifdef BASIC_DBG
      fmt::print("Change done = {}\n", change_done);
#endif
    }
#ifdef BASIC_DBG
    fmt::print("6. Printing after all the flop resolution and matching!");
    print_everything();
#endif

    if (!flop_set_synth.empty()) {
      if(change_done){
        make_io_maps(synth_lg, inp_map_of_sets_synth, out_map_of_sets_synth, is_orig_lg);
        make_io_maps(orig_lg, inp_map_of_sets_orig, out_map_of_sets_orig, true);
      }
      start = std::chrono::system_clock::now();
      weighted_match_LoopLastOnly();  // crit_entries_only=f, loopLast_only=t
      // set_theory_match_loopLast_only();
      end = std::chrono::system_clock::now();
      elapsed_seconds = end-start;
      fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: weighted_match_LoopLastOnly\n", elapsed_seconds.count());
      fmt::print("\n weighted_match_LoopLastOnly - synth done.\n");
#ifdef BASIC_DBG
      fmt::print("9. Printing after flop weighted_match_LoopLastOnly matching!");
      print_everything();
#endif
      //remove_resolved_from_orig_MoS();
    }

    I(flop_set_synth.empty(), "\n\n\tCHECK: flops not resolved. Cannot move on to further matching\n\n");
    if (crit_node_set.empty()) {
      /*all required matching done*/
      report_critical_matches_with_color();
    }

    // all flops matched and still some crit cells left to map!
    // move to combinational matching
    start = std::chrono::system_clock::now();
    make_io_maps(synth_lg, inp_map_of_sets_synth, out_map_of_sets_synth, is_orig_lg);
    make_io_maps(orig_lg, inp_map_of_sets_orig, out_map_of_sets_orig, true);
    /*resolution_of_synth_map_of_sets(inp_map_of_sets_synth);//if make_io_maps takes way less time, why not replace resolution_of_synth_map_of_sets with make_io_maps!
    resolution_of_synth_map_of_sets(out_map_of_sets_synth);*/
    end = std::chrono::system_clock::now();
    elapsed_seconds = end-start;
    fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: resolution_of_synth_map_of_sets-inLoopForCombo\n", elapsed_seconds.count());

    /* Since all flops are matched now, remove these from orig_mos*/
    start = std::chrono::system_clock::now();
    for (const auto& origpin_cf: flop_set_orig) {
      inp_map_of_sets_orig.erase(origpin_cf);
      out_map_of_sets_orig.erase(origpin_cf);
    }
    end = std::chrono::system_clock::now();
    elapsed_seconds = end-start;
    fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: Removing all flops from origMoS\n", elapsed_seconds.count());
    #ifdef BASIC_DBG
    fmt::print("\n------after removing flops from inp_map_of_sets_orig and out_map_of_sets_orig-------------\n");
    print_everything();
    //fmt::print("\norig lg in map:\n");  print_io_map(inp_map_of_sets_orig);
    //fmt::print("\norig lg out map:\n"); print_io_map(out_map_of_sets_orig);
    //fmt::print("\n");
    #endif

    start = std::chrono::system_clock::now();
    //fmt::print("Before complete_io_match(crit comb only)"); print_SynthSet_sizes();
    change_done = complete_io_match(false);  // alters crit_node_set too
    end = std::chrono::system_clock::now();
    elapsed_seconds = end-start;
    
    fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: complete_io_match-combo\n", elapsed_seconds.count());
    fmt::print("\n complete_io_match - synth - combinational done.\n");
    //fmt::print("AFTER complete_io_match(crit comb only)"); print_SynthSet_sizes();
    #ifdef BASIC_DBG
    fmt::print("10. Printing after all the combinational resolution and matching!");
    print_everything();
    #endif

    if(change_done) {
      make_io_maps(synth_lg, inp_map_of_sets_synth, out_map_of_sets_synth, is_orig_lg);
      make_io_maps(orig_lg, inp_map_of_sets_orig, out_map_of_sets_orig, true);
      for (const auto& origpin_cf: flop_set_orig) {
        inp_map_of_sets_orig.erase(origpin_cf);
        out_map_of_sets_orig.erase(origpin_cf);
      }
    }

    #ifndef FULL_RUN_FOR_EVAL
    if (!crit_node_set.empty()) {  // exact combinational matching could not resolve all crit nodes
      // surrounding cell loc-similarity matching
      start = std::chrono::system_clock::now();
      change_done = surrounding_cell_match();
      end = std::chrono::system_clock::now();
      elapsed_seconds = end-start;
      fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: surrounding_cell_match\n", elapsed_seconds.count());
      fmt::print("\n surrounding_cell_match - synth done.\n");
      #ifdef BASIC_DBG
      fmt::print("Change done = {}\n", change_done);
      #endif
    }
    #endif
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
  	  make_io_maps(lg, inp_map_of_sets_synth, out_map_of_sets_synth, is_orig_lg);
          /*resolution_of_synth_map_of_sets(inp_map_of_sets_synth);//if make_io_maps takes way less time, why not replace resolution_of_synth_map_of_sets with make_io_maps!
          resolution_of_synth_map_of_sets(out_map_of_sets_synth);*/
        }
      } while(unmatched_left && !crit_node_set.empty());
    }
#endif
#if 1
    if (!crit_node_set.empty()) {
      // set_theory_match_final();
      start = std::chrono::system_clock::now();
      weighted_match();  // crit_entries_only=t, loopLast_only=f
      end = std::chrono::system_clock::now();
      elapsed_seconds = end-start;
      fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: weighted_match\n", elapsed_seconds.count());
      fmt::print("\n weighted_match - synth (crit_entries_only) done.\n");
    }
#endif

    // fmt::print("\n inp_map_of_sets_synth.size() =  {}\nout_map_of_sets_synth:\n", inp_map_of_sets_synth.size());
    // print_io_map(out_map_of_sets_synth);
    // fmt::print("\n out_map_of_sets_synth.size() =  {}\n", out_map_of_sets_synth.size());
    /*all required matching done*/
#ifdef BASIC_DBG
    I(crit_node_set.empty(), "crit_node_set should have been empty by now!");
    fmt::print("20. Printing after crit_node_set.empty assertion checked");
    print_everything();
#endif
    I(crit_node_set.empty(), "crit_node_set should have been empty by now!!");
    report_critical_matches_with_color();
    return;  // FIXME: for DBG; remove.
  }

  if (is_orig_lg) {
    auto start = std::chrono::system_clock::now();
    make_io_maps(orig_lg, inp_map_of_sets_orig, out_map_of_sets_orig, is_orig_lg);	//remove_resolved_from_orig_MoS();//so that the nodes matched in default matching do not go for matching again!
    fmt::print("\n make_io_maps - orig done.\n");
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end-start;
    fmt::print("ELAPSED_SEC: {}s, FOR_FUNC: make_io_maps-originalLG\n", elapsed_seconds.count());
#ifdef BASIC_DBG
    fmt::print("\n inp_map_of_sets_orig.size() =  {}\nout_map_of_sets_orig:\n", inp_map_of_sets_orig.size());
    fmt::print("\n out_map_of_sets_orig.size() =  {}\n", out_map_of_sets_orig.size());
#endif
    return;  // FIXME: for DBG; remove.
  }

  I(!inp_map_of_sets_orig.empty() && !out_map_of_sets_orig.empty(),
    "\nCHECK: why is either inp_map_of_sets_orig or out_map_of_sets_orig empty??\n");
  // if (is_orig_lg){
  //   exact_matching();
  // }

  I(false, "\nintended exit!\n");
}

void Traverse_lg::print_SynthSet_sizes() const {
  fmt::print("set sizes for out_map_of_sets_synth:\n");
  int numOfKeys_out = 0;
  int zero_sets_out=0;
  int one_sets_out=0;
  int two_sets_out=0;
  int three_sets_out=0;
  for (const auto& [np, np_set]:out_map_of_sets_synth) {
    numOfKeys_out++;
    if(np_set.size()==0) zero_sets_out++;
    if(np_set.size()==1) one_sets_out++;
    if(np_set.size()==2) two_sets_out++;
    if(np_set.size()==3) three_sets_out++;
    fmt::print("{}, ", np_set.size());
  }
  fmt::print("\nset sizes for inp_map_of_sets_synth:\n");
  int numOfKeys_in = 0;
  int zero_sets_in=0;
  int one_sets_in=0;
  int two_sets_in=0;
  int three_sets_in=0;
  for (const auto& [np, np_set]:inp_map_of_sets_synth) {
    numOfKeys_in++;
    if(np_set.size()==0) zero_sets_in++;
    if(np_set.size()==1) one_sets_in++;
    if(np_set.size()==2) two_sets_in++;
    if(np_set.size()==3) three_sets_in++;
    fmt::print("{}, ", np_set.size());
  }
  fmt::print("\n");
  fmt::print("{}, {}, {}, {} out of {}\n", zero_sets_out, one_sets_out, two_sets_out, three_sets_out, numOfKeys_out);
  fmt::print("{}, {}, {}, {} in of {}\n", zero_sets_in, one_sets_in, two_sets_in, three_sets_in, numOfKeys_in);
  fmt::print("\n");
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

  fmt::print("9.1.0 Printing before fwd traversal! (with {} origLG) ",is_orig_lg);
  print_everything();
  #endif

  /*propagate sets. stop at sequential/IO... (_last)*/
  traverse_order.clear();
  fwd_traversal_for_inp_map(lg, inp_map_of_sets, is_orig_lg);
#ifdef BASIC_DBG
  fmt::print("9.1 Printing after fwd traversal! (with {} origLG) ",is_orig_lg);
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
      // for (const auto& orig_pin : set_pins_cf) {
      //   /* further accuracy attempt: remove the nodes used from orig as well*/
      //   inp_map_of_sets_orig.erase(orig_pin);
      //   out_map_of_sets_orig.erase(orig_pin);
      // }
      if (flop_set_synth.find(node_pin_cf) != flop_set_synth.end()) {
        flop_set_synth.erase(node_pin_cf);
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
  lg->each_graph_input([&inp_map_of_sets, &is_orig_lg, this](const Node_pin non_h_dpin) {
    /*capture the colored nodes in the process.*/
    const auto& dpin = non_h_dpin.get_hierarchical();//to get hierarchical traversal
    auto node = dpin.get_node();
    if (!is_orig_lg) {
      #ifndef FULL_RUN_FOR_EVAL
      if (node.has_color()) {
        for (const auto dpins : node.out_connected_pins()) {
          crit_node_map[dpins.get_compact_flat()] = node.get_color();  // keep till end for color data
          if (net_to_orig_pin_match_map.find(dpins.get_compact_flat()) == net_to_orig_pin_match_map.end()) {
            #ifdef BASIC_DBG
            fmt::print("inserting in crit_node_set: {},n{},{}\n", dpins.has_name()?dpins.get_name():(std::to_string(dpins.get_pid())), dpins.get_node().get_nid(), dpins.get_node().get_class_lgraph()->get_name() );
            #endif
            crit_node_set.insert(dpins.get_compact_flat());  // keep on deleting as matching takes place
                                                             // need not stor in set if already a matched entry
          }
        }
      }
      #else
      for (const auto &dpins : node.out_connected_pins()) {
        auto cf = dpins.get_compact_flat();
        crit_node_map[cf] = 0;  // keep till end for color data
        if (net_to_orig_pin_match_map.find(cf) == net_to_orig_pin_match_map.end()) {
          #ifdef BASIC_DBG
          fmt::print("inserting in crit_node_set: {},n{},{}\n", dpins.has_name()?dpins.get_name():(std::to_string(dpins.get_pid())), dpins.get_node().get_nid(), dpins.get_node().get_class_lgraph()->get_name() );
          #endif
          crit_node_set.insert(cf);  // keep on deleting as matching takes place
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
  lg->each_graph_output([&out_map_of_sets, &is_orig_lg, this](const Node_pin non_h_dpin) {
    /*capture the colored nodes in the process.*/
    const auto& dpin = non_h_dpin.get_hierarchical();//for hierarchical traversal
    auto node = dpin.get_node();
    if (!is_orig_lg) {
#ifndef FULL_RUN_FOR_EVAL
      if (node.has_color()) {
        for (const auto dpins : node.out_connected_pins()) {
          crit_node_map[dpins.get_compact_flat()] = node.get_color();
          if (net_to_orig_pin_match_map.find(dpins.get_compact_flat()) == net_to_orig_pin_match_map.end()) {
            #ifdef BASIC_DBG
            fmt::print("inserting in crit_node_set: {},n{},{}\n", dpins.has_name()?dpins.get_name():(std::to_string(dpins.get_pid())), dpins.get_node().get_nid(), dpins.get_node().get_class_lgraph()->get_name() );
            #endif
            crit_node_set.insert(dpins.get_compact_flat());  // keep on deleting as matching takes place
                                                             // need not stor in set if already a matched entry
          }
        }
      }
#else
      for (const auto dpins : node.out_connected_pins()) {
        crit_node_map[dpins.get_compact_flat()] = 0;
        if (net_to_orig_pin_match_map.find(dpins.get_compact_flat()) == net_to_orig_pin_match_map.end()) {
          #ifdef BASIC_DBG
          fmt::print("inserting in crit_node_set: {},n{},{}\n", dpins.has_name()?dpins.get_name():(std::to_string(dpins.get_pid())), dpins.get_node().get_nid(), dpins.get_node().get_class_lgraph()->get_name() );
          #endif
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
  #ifdef BASIC_DBG
  fmt::print("In fast -- lg->dump(true):\n");
  lg->dump(true);  // FIXME: remove this
  #endif
  fmt::print("\nIn fast pass for inputs for orig lg {} \n", is_orig_lg);
  for (const auto& node : lg->fast(true)) {
    #ifdef BASIC_DBG
    fmt::print("main node: {}(n{})\n",node.get_type_name() ,node.get_nid());
    #endif
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
            #ifdef BASIC_DBG
            fmt::print("inserting in crit_node_set: {},n{},{}\n", dpins.has_name()?dpins.get_name():(std::to_string(dpins.get_pid())), dpins.get_node().get_nid(), dpins.get_node().get_class_lgraph()->get_name() );
            #endif
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
            #ifdef BASIC_DBG
            fmt::print("inserting in crit_node_set: {},n{},{}\n", dpins.has_name()?dpins.get_name():(std::to_string(dpins.get_pid())), dpins.get_node().get_nid(), dpins.get_node().get_class_lgraph()->get_name() );
            #endif
            crit_node_set.insert(dpins.get_compact_flat());  // keep on deleting as matching takes place
                                                             // need not stor in set if already a matched entry
          }
        }
      }
      #endif
      if (node.is_type_loop_last()) {
        for (const auto dpins : node.out_connected_pins()) {
          flop_set_synth.insert(dpins.get_compact_flat());
        }
      }
    }

    if(is_orig_lg) {
      auto node_op_type = node.get_type_op();
      if(node_op_type==Ntype_op::Get_mask || node_op_type==Ntype_op::Set_mask || node_op_type==Ntype_op::TupAdd || node_op_type==Ntype_op::TupGet || (!node.has_loc())) {
        for (const auto dpins : node.out_connected_pins()){
          unwanted_orig_NPs.insert(dpins.get_compact_flat());
          #ifdef FOR_EVAL
	  fmt::print("Inserting in unwanted_orig_NPs: "); get_node_pin_compact_flat_details(dpins.get_compact_flat());
          #endif
      }}
    }
    if (!node.is_type_loop_last() || (is_orig_lg && node.is_type_sub())) {
      #ifdef BASIC_DBG
      fmt::print("\t\t\t CONTINUING (node is not LL {}, or node is type sub {})... \n", !node.is_type_loop_last(), node.is_type_sub());
      #endif
      continue;               // process flops only in this lg->fast (sub type loopLast are not flops)
    } else if (is_orig_lg) {  // flops in orig_lg to be inserted in flop_set_orig
      #ifdef BASIC_DBG
      fmt::print("Inserting in flop_set_orig, dpins of node: {} \n", node.get_nid());
      #endif
      for (const auto dpins : node.out_connected_pins()) {
        flop_set_orig.insert(dpins.get_compact_flat());
      }
    }
    #ifdef BASIC_DBG
    fmt::print("\nNOT CONTINUING...\n");
    #endif
    for (const auto dpins : node.out_connected_pins()) {
      const auto node_dpin_cf = dpins.get_compact_flat();
      bool       is_loop_stop
          = node.is_type_loop_last() || node.is_type_loop_first()
            || (mark_loop_stop.find(node_dpin_cf) != mark_loop_stop.end());

      const auto self_set = inp_map_of_sets.find(node_dpin_cf);
      #ifdef BASIC_DBG
      fmt::print("\t Working on node's dpin: {},p{} (is LL?{}) \n", dpins.has_name()?dpins.get_name():std::to_string(dpins.get_pid()), std::to_string(dpins.get_pid()), is_loop_stop);
      #endif
      for (auto e : node.out_edges()) {
        if (e.sink.get_node().is_type_loop_first()) {
          /*need not keep outputs of const/graphIO in in_map_of_sets*/
          #ifdef BASIC_DBG
          fmt::print("\t\t Child node is type Loop first. continuing...\n");
          #endif
          continue;
        }
        for (const auto out_cfs : e.sink.get_node().out_connected_pins()) {
          // if ( is_orig_lg && !out_cfs.get_node().has_loc() ) {
          //   /* In original LG, if a node does NOT have LoC, then the node should not be kept in map of sets.*/
          //   continue;
          // }
          auto out_cf = out_cfs.get_compact_flat();
	  #ifdef BASIC_DBG
          fmt::print("\t\t\t Child node's (n{})  dpin:{}\n",std::to_string(out_cfs.get_node().get_nid()), out_cfs.has_name()?out_cfs.get_name():std::to_string(out_cfs.get_pid()));
          #endif
          if (is_loop_stop) {
            if (!is_orig_lg && !(get_matching_map_val(dpins.get_compact_flat())).empty()) {
              auto match_val = get_matching_map_val(dpins.get_compact_flat());
              #ifdef BASIC_DBG
              fmt::print("\t\t\t\t\t inserting in inp_map_of_sets this \"child node's dpin\" = [some match_val]\n" );
              #endif
              inp_map_of_sets[out_cf].insert(match_val.begin(), match_val.end());  // resolution
            } else {
              #ifdef BASIC_DBG
              fmt::print("\t\t\t\t\t inserting in inp_map_of_sets this \"child node's dpin\" = [{}]\n",dpins.has_name()?dpins.get_name():std::to_string(dpins.get_pid()) );
              #endif
              inp_map_of_sets[out_cf].insert(dpins.get_compact_flat());
            }
          } else {
            if (self_set != inp_map_of_sets.end()) {
              #ifdef BASIC_DBG
              fmt::print("\t\t\t\t\t inserting in inp_map_of_sets this \"child node's dpin\" = [self set]\n");
              #endif
              inp_map_of_sets[out_cf].insert(self_set->second.begin(), self_set->second.end());
            }
            #ifdef BASIC_DBG
            else {fmt::print("\t\t\t\t\t IN LAST ELSE\n");}
            #endif
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
  #ifdef BASIC_DBG
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
      bool is_loop_stop
          = node.is_type_loop_last() || node.is_type_loop_first() || (mark_loop_stop.find(node_dpin_cf) != mark_loop_stop.end());

      #ifdef BASIC_DBG
      fmt::print("\t\tnode is loop stop: LL?{}, LF?{}, MLS?{} \n", node.is_type_loop_last(), node.is_type_loop_first(), (mark_loop_stop.find(node_dpin_cf) != mark_loop_stop.end()) );
      #endif
      const absl::flat_hash_set<Node_pin::Compact_flat>* self_set = nullptr;
      auto                                               it       = inp_map_of_sets.find(node_dpin_cf);
      if (it != inp_map_of_sets.end()) {
        self_set = &it->second;
        #ifdef BASIC_DBG
        fmt::print("\tnode present in inp_map_of_sets. It's SS:");
        print_set(*self_set);
        fmt::print("\n");
        #endif
      }
      #ifdef BASIC_DBG
      else {
        fmt::print("\tnode NOT present in inp_map_of_sets already.\n");
      }
      #endif
      for (auto e : node.out_edges()) {
        if (e.sink.get_node().is_type_loop_first() /*need not keep outputs of const/graphIO in in_map_of_sets*/
            /*|| e.sink.get_node().is_type_loop_last() is_type_loop_last processed in previous fast pass*/) {
          #ifdef BASIC_DBG
          fmt::print("\tChild node is loop_first. continuing!\n");
          #endif
          continue;
        }
        // if ( is_orig_lg && !e.sink.get_node().has_loc() ) {
        //   /* In original LG, if a node does NOT have LoC, then the node should not be kept in map of sets.*/
        //   continue;
        // }
        for (const auto out_cfs : e.sink.get_node().out_connected_pins()) {
#ifdef BASIC_DBG
          fmt::print("\tChild node's dpin:\t\t {}(n{})\n",
                     out_cfs.has_name() ? out_cfs.get_name() : std::to_string(out_cfs.get_pid()),
                     out_cfs.get_node().get_nid());
#endif
          auto out_cf = out_cfs.get_compact_flat();
          if (is_loop_stop) {
            if (!is_orig_lg && !(get_matching_map_val(node_dpin_cf)).empty()) {
              auto match_val = get_matching_map_val(node_dpin_cf);
#ifdef BASIC_DBG
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
#ifdef BASIC_DBG
              fmt::print("\t\t\tnode itself is the I/P! K[n{}]::V[n{}]\n", out_cfs.get_node().get_nid(), node.get_nid());
#endif
            }
          } else {
            if (self_set) {
              inp_map_of_sets[out_cf].insert(self_set->begin(), self_set->end());
#ifdef BASIC_DBG
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
    bool is_loop_stop = node.is_type_loop_last() || node.is_type_loop_first()
                        || (mark_loop_stop.find(node_dpin.get_compact_flat()) != mark_loop_stop.end());

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
#ifdef FULL_RUN_FOR_EVAL
	return;
#endif
  // auto dpin_name = dpin.get_name();
  if (dpin_name.find('#') == std::size_t(0)) {  // if dpin_name has # as start char
    dpin_name.erase(dpin_name.begin());         // remove it
  }
  if (dpin_name.find("otup_") == std::size_t(0)) {  // if dpin_name has otup_ in the beginning
    dpin_name.erase(std::size_t(0), 5);             // remove it
  }
  if (dpin_name.find("otup_") != std::string::npos) {  // if dpin_name has otup_ in the middle
    dpin_name.erase(dpin_name.find("otup_"),
                    5);  // like otup_frontend.otup_btb.io_ras_head_valid: otup_ with tbt should be removed too
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
  if (dpin_name.find(".") != std::string::npos) { // to address: The "pmp.io.addr" in DT2 yosys trial
    std::replace( dpin_name.begin(), dpin_name.end(), '.', '_');// convert all '.' to '_'
  }
}

void Traverse_lg::netpin_to_origpin_default_match(Lgraph* orig_lg, Lgraph* synth_lg) {
  /*keep top graph IO as well on the net_to_orig_pin_match_map */


  const auto num_of_matches = net_to_orig_pin_match_map.size();
  /* map to capture all possible dpin names in the hierarchy*/  // TODO with instance name API
  absl::flat_hash_map<std::string, Node_pin::Compact_flat>                      name2dpin;
  absl::flat_hash_map<std::string, absl::flat_hash_set<Node_pin::Compact_flat>> name2dpins;

  orig_lg->each_graph_input([&name2dpin](const Node_pin& non_h_dpin) {
    const auto& dpin = non_h_dpin.get_hierarchical();// to get hierarchical traversal
    auto orig_in_dpin_name       = dpin.get_name();
    name2dpin[orig_in_dpin_name] = dpin.get_compact_flat();
#ifdef BASIC_DBG
    fmt::print("Inserting orig-in-dpin {} in name2dpin\n", orig_in_dpin_name);
#endif
  });
  synth_lg->each_graph_input([&name2dpin, this](const Node_pin& non_h_dpin) {
    const auto& dpin = non_h_dpin.get_hierarchical();// to get hierarchical traversal
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
#ifdef BASIC_DBG
    else {
      fmt::print("NOT inserting {}, {}\n",
                 dpin.has_name() ? dpin.get_name() : std::to_string(dpin.get_pid()),
                 std::to_string(dpin.get_pid()));
    }
#endif
  });


#ifdef BASIC_DBG
  fmt::print("\n OUT:\n");
#endif
  orig_lg->each_graph_output([&name2dpin](const Node_pin& non_h_dpin) {
    const auto& dpin = non_h_dpin.get_hierarchical();//for hierarchical traversal
    auto orig_out_dpin_name       = dpin.get_name();
    name2dpin[orig_out_dpin_name] = dpin.get_compact_flat();
#ifdef BASIC_DBG
    fmt::print("Inserting orig-out-dpin {} in name2dpin\n", dpin.get_name());
#endif
  });
  synth_lg->each_graph_output([&name2dpin, this](const Node_pin& non_h_dpin) {
    const auto& dpin = non_h_dpin.get_hierarchical();//for hierarchical traversal
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
#ifdef BASIC_DBG
    else {
      fmt::print("NOT inserting {}, {}\n",
                 dpin.has_name() ? dpin.get_name() : std::to_string(dpin.get_pid()),
                 std::to_string(dpin.get_pid()));
    }
#endif
  });


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
          /* correction: |2 is not bus. It is SSA form*/
          original_node_dpin_wire.erase(original_node_dpin_wire.find('|'), original_node_dpin_wire.length());
          name2dpins[original_node_dpin_wire].insert(original_node_dpin.get_compact_flat());
          auto name2dpin_it = name2dpin.find(original_node_dpin_wire);
          if (name2dpin_it != name2dpin.end()) {
            /* after removing SSA, pin in name2dpin, then remove from name2dpin and put here*/
            #ifdef BASIC_DBG
              //fmt::print("\t\t\t\t\terasing from name2dpin: {}", name2dpin_it->first);
						  fmt::print("\t\t\t\t\tputiing in name2dpins: {}", name2dpin_it->first);
            #endif
            name2dpins[original_node_dpin_wire].insert(name2dpin_it->second);
            name2dpin.erase(name2dpin_it);
          }
          #ifdef BASIC_DBG
            fmt::print("\t\t\t inserting {} in name2dpinss.\n", original_node_dpin_wire);
          #endif
        } else {
          if (name2dpins.find(original_node_dpin_wire) != name2dpins.end()) {
            /* if entry already in name2dpins, then append to that only*/
            name2dpins[original_node_dpin_wire].insert(original_node_dpin.get_compact_flat());
            #ifdef BASIC_DBG
            fmt::print("\t\t\t inserting {} in name2dpinss.\n", original_node_dpin_wire);
            #endif
          } else if (name2dpin.find(original_node_dpin_wire) != name2dpin.end()) {
						/* entry already in name2dpin. Instead of overwriting, move it to name2dpins*/
						auto name2dpin_it = name2dpin.find(original_node_dpin_wire);
						name2dpins[original_node_dpin_wire].insert(name2dpin_it->second);
            name2dpin.erase(name2dpin_it);
						/* now write the new entry as well to name2dpins */
						name2dpins[original_node_dpin_wire].insert(original_node_dpin.get_compact_flat());
            #ifdef BASIC_DBG
					  	fmt::print("\t\t\t moving {} to name2dpinss.\n", original_node_dpin_wire);
            #endif
					} else {
            name2dpin[original_node_dpin_wire] = original_node_dpin.get_compact_flat();
            #ifdef BASIC_DBG
            fmt::print("\t\t\t inserting {} in name2dpin.\n", original_node_dpin_wire);
            #endif
          }
        }
      }
    }
  }

#ifdef BASIC_DBG
  print_name2dpin(name2dpin);
  print_name2dpins(name2dpins);
#endif

  /*known points matching*/
  //synth_lg->dump(true);                           // FIXME: remove this
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
          mark_loop_stop.insert((map_itt_s->second).begin(), (map_itt_s->second).end());
#ifdef FOR_EVAL
          fmt::print("Inserting in netpin_to_origpin_default_match s : {} ::: ",
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

  fmt::print("num_of_matches: {}, IN_FUNC: netpin_to_origpin_default_match \n",net_to_orig_pin_match_map.size()-num_of_matches);

  //remove_resolved_from_orig_MoS();
}

void Traverse_lg::remove_resolved_from_orig_MoS() {
#ifdef BASIC_DBG
	fmt::print("\n\nIn remove_resolved_from_orig_MoS.\n\n");
#endif
  for (const auto& [synth_np_cf, orig_np_cf_set] : net_to_orig_pin_match_map) {
    for (const auto& orig_np_cf : orig_np_cf_set) {
      inp_map_of_sets_orig.erase(orig_np_cf);
      out_map_of_sets_orig.erase(orig_np_cf);
    }
  }
}

void Traverse_lg::matching_pass_io_boundary_only(map_of_sets& map_of_sets_synth,
                                                 map_of_sets& map_of_sets_orig) {  // FIXME: no more used-- remove?

  const auto num_of_matches = net_to_orig_pin_match_map.size();

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

  fmt::print("num_of_matches: {}, IN_FUNC: matching_pass_io_boundary_only \n",net_to_orig_pin_match_map.size()-num_of_matches);

}

bool Traverse_lg::common_element_present(const absl::flat_hash_set<Node_pin::Compact_flat>& set1, const absl::flat_hash_set<Node_pin::Compact_flat>& set2) const {

  for (const auto& np: set1) {
    if(set2.contains(np)) return true;
  }
  return false;

}

float Traverse_lg::get_matching_weight(const absl::flat_hash_set<Node_pin::Compact_flat>& synth_set,
                                       const absl::flat_hash_set<Node_pin::Compact_flat>& orig_set) const {
  const auto& smallest = synth_set.size() < orig_set.size() ? synth_set : orig_set;
  const auto& largest  = synth_set.size() >= orig_set.size() ? synth_set : orig_set;
  int         matches  = 0;
  for (const auto& it_set : smallest) {
    if (largest.contains(it_set)) {
      ++matches;
    }
  }

  /* 5 points for a full match for this set part
     5 for ins and 5 for outs will make a 10 for complete IO match.*/
  float mismatches = smallest.size() - matches ;
  float matching_weight = 5 * (float((2 * matches) - mismatches) / float(synth_set.size() + orig_set.size()));
  #ifdef FULL_RUN_FOR_EVAL_TESTING
    fmt::print("{}, {}, ", matches, mismatches);
  #endif
  return matching_weight;
  // float mismatches = synth_set.size() - matches ;
  // if (mismatches<=0.0) mismatches=1;
  // #ifdef BASIC_DBG
  // fmt::print("\t\t\t\t (synth_set_size={}, orig_set_size={}) matches:{}, mismatches:{},returning:{}\n",  synth_set.size(), orig_set.size(),matches, mismatches,(matches/mismatches));
  // #endif
  // return (matches/mismatches);
}

Traverse_lg::inverted_map_arr Traverse_lg::convert_set_to_sorted_array(const absl::flat_hash_set<Node_pin::Compact_flat>& np_set) const{

  inverted_map_arr inv_arr;
  auto it = np_set.begin();
  for(size_t i=0; i<inv_arr.size() && it!=np_set.end(); ++i,++it){
    inv_arr[i]=*it;
  } // since set and arr could be of diff sizes, = op won't work
  std::sort(inv_arr.begin(), inv_arr.end());

  return inv_arr;
}

void Traverse_lg::create_inverted_map(map_of_sets& mapOfSets, inverted_map_of_sets& small_set_inv_map, map_of_sets& big_set_map) {
  
  for(const auto& [np, np_set]: mapOfSets){
    if(np_set.size()<4) {
      inverted_map_arr inv_arr = convert_set_to_sorted_array(np_set);
      small_set_inv_map[inv_arr].insert(np);
    } else {
      //large sets will be appended to big_set_map
      big_set_map[np]=np_set;
    }
  }
  
}

bool Traverse_lg::complete_io_match(bool flop_only) {

  const auto num_of_matches = net_to_orig_pin_match_map.size();

  Traverse_lg::inverted_map_of_sets inp_invMoS_orig;
  Traverse_lg::map_of_sets inp_big_set_Mos_orig;
  create_inverted_map(inp_map_of_sets_orig, inp_invMoS_orig, inp_big_set_Mos_orig);
  fmt::print("\nsize of inv map: {}, size of inp_big_set_Mos_orig: {}, instead of  inp_map_of_sets_orig: {} \n", inp_invMoS_orig.size(), inp_big_set_Mos_orig.size(), inp_map_of_sets_orig.size());

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
    auto node_iter_to_outMoSsynth = out_map_of_sets_synth.find(it->first);

    auto keep_partial_match_checking_on = true;
    std::map<float, absl::flat_hash_set<Node_pin::Compact_flat>> partial_out_match_map;

    if( (it->second).size() < 4 ) {//if synth_set is small, work with inp_invMoS_orig
      inverted_map_arr sorted_synth_in_arr = convert_set_to_sorted_array(it->second);

      auto iter_to_origInpInvMap_match = inp_invMoS_orig.find(sorted_synth_in_arr);
      if (iter_to_origInpInvMap_match == inp_invMoS_orig.end()) {
        /*no input match for this node. move to other node*/
	it++;
	continue;
      }

      /* Found input match for this synth node. process for further complete io matching*/
      for(const auto& orig_in_np: iter_to_origInpInvMap_match->second) {
      
        if(unwanted_orig_NPs.contains(orig_in_np) ) { 
          #ifdef BASIC_DBG
	  fmt::print("\t   orig node in unwanted_orig_NPs:"); get_node_pin_compact_flat_details(orig_in_np);
          #endif
          continue; }
        #ifdef BASIC_DBG
        fmt::print("\tworking on: "); get_node_pin_compact_flat_details(orig_in_np);
        #endif
        if (flop_only) {
          if (!flop_set_orig.contains(orig_in_np)) {
            continue;  // orig_in_np is not in flop_set_orig, then the node is not type loop last
          }
        }

        out_matched              = false;
        auto partial_out_matched = false;
        auto matching_wt         = 0.0;

        /*see if their output sets match as well.
         * 1. both might not have outputs and thus not be present in out_map_of_sets_<>
         * 2. if both are present, then compare the output sets.*/
        auto node_iter_to_outMoSorig = out_map_of_sets_orig.find(orig_in_np);
        if (node_iter_to_outMoSsynth != out_map_of_sets_synth.end()
            && node_iter_to_outMoSorig != out_map_of_sets_orig.end()) {  // both present
          #ifdef BASIC_DBG
          fmt::print("\t\t Outputs present for both \n");
          #endif
          if (node_iter_to_outMoSsynth->second  == out_map_of_sets_orig[orig_in_np]) {
            out_matched                    = true;
            keep_partial_match_checking_on = false;
            partial_out_matched            = false;
            #ifdef BASIC_DBG
            fmt::print("\t\t Outputs exactly matched \n");
            #endif
          } else if (keep_partial_match_checking_on) {
            // input have been exact match . but output is not exact match! get best partial match for output?
            #ifdef BASIC_DBG
            fmt::print("\t\t Outputs not exactly matched \n");
            #endif
            matching_wt         = get_matching_weight(node_iter_to_outMoSsynth->second , node_iter_to_outMoSorig->second);
            partial_out_matched = true;
          }
        } else if (node_iter_to_outMoSsynth == out_map_of_sets_synth.end()
                   && node_iter_to_outMoSorig == out_map_of_sets_orig.end()) {  // both absent. thus a match!?
          out_matched = true;
          #ifdef BASIC_DBG
          fmt::print("\t\t matching due to absence !!\n");
          #endif
        }
        #ifdef BASIC_DBG
        else {  // outputs did not match
          auto p_o = Node_pin("lgdb", orig_in_np);
          auto o_s = Node_pin("lgdb", orig_in_np).get_node();
          fmt::print("\t\tMatch? : {},n{}\n",
                     p_o.has_name() ? p_o.get_name() : std::to_string(p_o.get_pid()),
                     std::to_string(o_s.get_nid()));
        }
        #endif

        if (out_matched) {  // in+out matched. complete exact match. put in matching map
          net_to_orig_pin_match_map[it->first].insert(orig_in_np);
          mark_loop_stop.insert(it->first);
          mark_loop_stop.insert(orig_in_np);
          #ifdef FOR_EVAL
          auto np_s = Node_pin("lgdb", it->first);
          auto np_o = Node_pin("lgdb", orig_in_np);
          fmt::print("Inserting in complete_io_match (small set) : n{},{}  :::  n{},{}\n",
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
          #ifdef BASIC_DBG
          auto np_o = Node_pin("lgdb", orig_in_np);
          fmt::print("\t\t For matching_wt {}, inserting small-set: n{}({})\n",
                     matching_wt,
                     np_o.get_node().get_nid(),
                     np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
          #endif
        }
      }

    } else { //synth_set is large, work with the inp_big_set_Mos_orig
      for (const auto& [orig_in_np, orig_in_set_np] : inp_big_set_Mos_orig) {
        if(unwanted_orig_NPs.contains(orig_in_np) ) { 
          #ifdef BASIC_DBG
          fmt::print("\t   orig node in unwanted_orig_NPs:"); get_node_pin_compact_flat_details(orig_in_np);
          #endif
          continue; }
        #ifdef BASIC_DBG
        fmt::print("\tworking on: "); get_node_pin_compact_flat_details(orig_in_np);
        #endif
        if (flop_only) {
          if (!flop_set_orig.contains(orig_in_np)) {
            continue;  // orig_in_np is not in flop_set_orig, then the node is not type loop last
          }
        }

        out_matched              = false;
        auto partial_out_matched = false;
        auto matching_wt         = 0.0;

        if (it->second == orig_in_set_np) {  // in_matched
          #ifdef BASIC_DBG
          fmt::print("\t\t Inputs matched \n");
          #endif
          /*it->first == orig_in_np for inputs. see if their output sets match as well.
           * 1. both might not have outputs and thus not be present in out_map_of_sets_<>
           * 2. if both are present, then compare the output sets.*/
          auto node_iter_to_outMoSorig = out_map_of_sets_orig.find(orig_in_np);
          if (node_iter_to_outMoSsynth != out_map_of_sets_synth.end()
              && node_iter_to_outMoSorig != out_map_of_sets_orig.end()) {  // both present
            #ifdef BASIC_DBG
            fmt::print("\t\t Outputs present for both \n");
            #endif
            if (node_iter_to_outMoSsynth->second  == out_map_of_sets_orig[orig_in_np]) {
              out_matched                    = true;
              keep_partial_match_checking_on = false;
              partial_out_matched            = false;
              #ifdef BASIC_DBG
              fmt::print("\t\t Outputs exactly matched \n");
              #endif
            } else if (keep_partial_match_checking_on) {
              // input have been exact match . but output is not exact match! get best partial match for output?
              #ifdef BASIC_DBG
              fmt::print("\t\t Outputs not exactly matched \n");
              #endif
              matching_wt         = get_matching_weight(node_iter_to_outMoSsynth->second , out_map_of_sets_orig[orig_in_np]);
              partial_out_matched = true;
            }
          } else if (node_iter_to_outMoSsynth == out_map_of_sets_synth.end()
                     && node_iter_to_outMoSorig == out_map_of_sets_orig.end()) {  // both absent. thus a match!?
            out_matched = true;
            #ifdef BASIC_DBG
            fmt::print("\t\t matching due to absence !!\n");
            #endif
          }
          #ifdef BASIC_DBG
          else {  // outputs did not match
            auto p_o = Node_pin("lgdb", orig_in_np);
            auto o_s = Node_pin("lgdb", orig_in_np).get_node();
            fmt::print("\t\tMatch? : {},n{}\n",
                       p_o.has_name() ? p_o.get_name() : std::to_string(p_o.get_pid()),
                       std::to_string(o_s.get_nid()));
          }
          #endif
        }

        if (out_matched) {  // in+out matched. complete exact match. put in matching map
          net_to_orig_pin_match_map[it->first].insert(orig_in_np);
          mark_loop_stop.insert(it->first);
          mark_loop_stop.insert(orig_in_np);
          //count++;
          #ifdef FOR_EVAL
          auto np_s = Node_pin("lgdb", it->first);
          auto np_o = Node_pin("lgdb", orig_in_np);
          fmt::print("Inserting in complete_io_match large-set: n{},{}  :::  n{},{}\n",
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
          #ifdef BASIC_DBG
          auto np_o = Node_pin("lgdb", orig_in_np);
          fmt::print("\t\t For matching_wt {}, inserting (large-set) : n{}({})\n",
                     matching_wt,
                     np_o.get_node().get_nid(),
                     np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
          #endif
        }
      }
    }


    if (io_matched) {  // in+out matched. complete exact match.  remove from synth map_of_sets
      remove_from_crit_node_set(it->first);
      if (flop_set_synth.find(it->first) != flop_set_synth.end()) {
        flop_set_synth.erase(it->first);
      }
      out_map_of_sets_synth.erase(it->first);
      inp_map_of_sets_synth.erase(it++);
      // FIXME: add erase from orig Maps here also?
    } else if (partial_out_match_map.size() /*&& (((partial_out_match_map.end())->first)!=0) */) {
      net_to_orig_pin_match_map[it->first].insert(((partial_out_match_map.rbegin())->second).begin(),
                                                  ((partial_out_match_map.rbegin())->second).end());
      mark_loop_stop.insert(it->first);
      mark_loop_stop.insert(((partial_out_match_map.rbegin())->second).begin(),((partial_out_match_map.rbegin())->second).end());
      //count++;
      #ifdef BASIC_DBG
      fmt::print("Partial_out_match_map:\n");
      for (auto [a, b] : partial_out_match_map) {
        fmt::print("{} ---- ", a);
        for (auto b_ : b) {
          auto b__ = Node_pin("lgdb", b_);
          fmt::print("\t {}, ", b__.has_name() ? b__.get_name() : ("n" + std::to_string(b__.get_node().get_nid())));
        }
        fmt::print("\n");
      }
      #endif
      #ifdef FOR_EVAL
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
      if (flop_set_synth.find(it->first) != flop_set_synth.end()) {
        flop_set_synth.erase(it->first);
      }
      out_map_of_sets_synth.erase(it->first);
      inp_map_of_sets_synth.erase(it++);
    } else {
      it++;
    }
  }
  fmt::print("num_of_matches: {}, IN_FUNC: complete_io_match \n",net_to_orig_pin_match_map.size()-num_of_matches);
  return any_matching_done;
}

bool Traverse_lg::surrounding_cell_match() {
  const auto num_of_matches = net_to_orig_pin_match_map.size();

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
  fmt::print("num_of_matches: {}, IN_FUNC: surrounding_cell_match \n",net_to_orig_pin_match_map.size()-num_of_matches);
  //remove_resolved_from_orig_MoS();
  return any_matching_done;
}

bool Traverse_lg::surrounding_cell_match_final() {
  const auto num_of_matches = net_to_orig_pin_match_map.size();
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
  fmt::print("num_of_matches: {}, IN_FUNC: surrounding_cell_match_final \n",net_to_orig_pin_match_map.size()-num_of_matches);
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
    fmt::print("\t\tREMOVING FROM crit_node_set: {},n{},{}\n",
               p.has_name() ? p.get_name() : std::to_string(p.get_pid()),
               p.get_node().get_nid(), p.get_node().get_class_lgraph()->get_name());
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

void Traverse_lg::get_node_pin_compact_flat_details(const Node_pin::Compact_flat &np_cf) const {
  auto np = Node_pin("lgdb",np_cf);
  if ( np.is_graph_io() ) { 
    fmt::print("-IO node-\t");
  }
  fmt::print("Details of node nid:{}", np.get_node().get_nid());
  fmt::print(",pin:{} are:: type:{}, lg:{}\n", np.has_name()?np.get_name():("p"+std::to_string(np.get_pid())), np.get_node().get_type_name(),  (np.get_node().get_class_lgraph())->get_name() );
}

void Traverse_lg::report_critical_matches_with_color() {
#ifdef BASIC_DBG
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
#ifdef BASIC_DBG
	Node from_node_synth, from_node_orig;
  Node to_node_synth, to_node_orig, orig_33, orig_34, orig_35, orig_36;
#endif
  for (const auto& [synth_np, color_val] : crit_node_map) {
    auto        orig_NPs   = net_to_orig_pin_match_map[synth_np];
    auto        synth_pin  = Node_pin("lgdb", synth_np);
    auto        synth_dpin = synth_pin.has_name() ? synth_pin.get_name() : ("p" + std::to_string(synth_pin.get_pid()));
    std::string synth_node = absl::StrCat("n", std::to_string(synth_pin.get_node().get_nid()));
    for (const auto& orig_np : orig_NPs) {
      auto orig_pin  = Node_pin("lgdb", orig_np);
      auto orig_node = orig_pin.get_node();
      auto orig_dpin = orig_pin.has_name() ? orig_pin.get_name() : ("p" + std::to_string(orig_pin.get_pid()));
      auto loc_start = orig_node.has_loc() ? (std::to_string(orig_node.get_loc().first)) : "xxx";
      auto loc_end   = orig_node.has_loc() ? (std::to_string(orig_node.get_loc().second)) : "xxx";
      fmt::print("{},{}       :-    n{},{}({},{},{})    --   {}   --  [{},{}]{}\n",
                 synth_node,
                 synth_dpin,
                 orig_node.get_nid(),
                 orig_dpin,
								 orig_node.get_type_name(), 
								 orig_node.has_name()?orig_node.get_name():"",
								 (orig_node.get_class_lgraph())->get_name(),
                 color_val,
                 loc_start,
                 loc_end,
                 orig_node.get_source());  // FIXME: referring to nid for understandable message.

#ifdef BASIC_DBG
			if(color_val==37){
				to_node_synth = synth_pin.get_node();
				to_node_orig = orig_node;
			}
			if(color_val==38) {
				from_node_synth = synth_pin.get_node();
				from_node_orig = orig_node;
			}
			if(color_val==36) {	orig_36 = orig_node;		}
			if(color_val==35) {	orig_35 = orig_node;		}
			if (color_val==34){orig_34=orig_node;}
			if (color_val==33){orig_33=orig_node;}
#endif
    }
  }

  fmt::print("\n");

#if 0
	absl::flat_hash_set<Node> traversed_nodes = {};
	fmt::print("\ncolor 38 in synth LG is connected to color 37 in synth LG ? {}\n",is_combinationally_connected(from_node_synth, to_node_synth, traversed_nodes) );
	traversed_nodes.clear();
  fmt::print("\ncolor 38 in orig LG is connected to color 37 in orig LG ? {}\n\n",is_combinationally_connected(from_node_orig, to_node_orig, traversed_nodes) );
	traversed_nodes.clear();
	fmt::print("\ncolor 37 in orig LG is connected to color 38 in orig LG ? {}\n\n",is_combinationally_connected(to_node_orig, from_node_orig, traversed_nodes) );
	traversed_nodes.clear();
  fmt::print("\ncolor 37 in orig LG is connected to color 36 in orig LG ? {}\n\n",is_combinationally_connected(to_node_orig, orig_36, traversed_nodes) );
	traversed_nodes.clear();
	fmt::print("\ncolor 36 in orig LG is connected to color 35 in orig LG ? {}\n\n",is_combinationally_connected(orig_36, orig_35, traversed_nodes) );
	traversed_nodes.clear();
	fmt::print("\ncolor 35 in orig LG is connected to color 34 in orig LG ? {}\n\n",is_combinationally_connected(orig_35, orig_34, traversed_nodes) );
	traversed_nodes.clear();
	fmt::print("\ncolor 34 in orig LG is connected to color 33 in orig LG ? {}\n\n",is_combinationally_connected(orig_34, orig_33, traversed_nodes) );
#endif
  fmt::print("Crossovers in this design: {}\n", crossover_count);
  auto end_time_of_algo = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = end_time_of_algo-start_time_of_algo;
  fmt::print("TOTAL_TIME_OF_ALGO: {}s\n", elapsed_seconds.count());
  exit(0);
}

void Traverse_lg::resolution_of_synth_map_of_sets(Traverse_lg::map_of_sets& synth_map_of_set) {
  fmt::print("\n In resolution_of_synth_map_of_sets:\n");
  for (auto& [synth_np, synth_set_np] : synth_map_of_set) {
    // auto xx = Node_pin("lgdb", synth_np).get_node().get_nid();
    // fmt::print("------{} ({})\n",xx, synth_set_np.size());
    absl::flat_hash_set<Node_pin::Compact_flat> tmp_set;
    #ifdef BASIC_DBG
    fmt::print("Working on : "); get_node_pin_compact_flat_details(synth_np);
    #endif
    for (auto it = synth_set_np.begin(); it != synth_set_np.end();) {
      const auto set_np_val = *it;
      if (net_to_orig_pin_match_map.find(set_np_val) != net_to_orig_pin_match_map.end()) {
        #ifdef EXTENSIVE_DBG
        fmt::print("Found match \n");
        #endif
        synth_set_np.erase(it++);
        // FIXME: add erase from orig Maps here also?
        const auto &equiv_val = net_to_orig_pin_match_map[set_np_val];
        tmp_set.insert(equiv_val.begin(), equiv_val.end());
        #ifdef BASIC_DBG
	fmt::print("replacing "); get_node_pin_compact_flat_details(set_np_val);
	fmt::print("\t with : "); for(auto i:equiv_val) {get_node_pin_compact_flat_details(i);}
        #endif
      } else {
        #ifdef BASIC_DBG
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
  print_set(flop_set_synth);
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
  fmt::print("converting in/out map_of_sets to the shortened MoS with LoopLast only nodes in values(set).\n");
#endif

  Traverse_lg::map_of_sets node_map_of_set_LoopLastOnly;
  for (const auto& [node_key, io_vals_set] : io_map_of_sets) {
    /*if(loop_last_only):for only those keys that are is_type_loop_last*/
    auto node = Node_pin("lgdb", node_key).get_node();
    #ifdef BASIC_DBG
    fmt::print("### working on node: "); get_node_pin_compact_flat_details(node_key);
    #endif
    if (node.is_type_loop_last() && (!node.is_type_sub()) ) {  // flop node should be matched with flop node only! else datatype mismatch!
      // we need flop nodes only in this case
      // #ifndef FULL_RUN_FOR_EVAL
      if (node.has_loc()) {// also, nodes with valid LoC should be used for matching
        for (const auto& io_val : io_vals_set) {
	  #ifdef BASIC_DBG
	  fmt::print("\t\t\t inserting node against: "); get_node_pin_compact_flat_details(io_val);
	  #endif
          node_map_of_set_LoopLastOnly[io_val].insert(node_key);
        }
      }
      #ifdef BASIC_DBG
      else {fmt::print("\t\t\t not processing this node (no loc in node)!\n");}
      #endif
      // #else
      // for (const auto& io_val: io_vals_set) {
      //   node_map_of_set_LoopLastOnly[io_val].insert(node_key);
      // }
      // #endif
    } 
    #ifdef BASIC_DBG
    else {
      fmt::print("\t\t\t not processing this node (not LL)!\n");
    }
    #endif
  }

#ifdef BASIC_DBG
  fmt::print("\n Printing node_map_of_set_LoopLastOnly \n");
  print_io_map(node_map_of_set_LoopLastOnly);
	fmt::print("\n\n");
#endif

  return node_map_of_set_LoopLastOnly;
}

Traverse_lg::map_of_sets Traverse_lg::obtain_MoS_LLonly(const map_of_sets& io_map_of_sets) {
#ifdef BASIC_DBG
  fmt::print("obtain_MoS_LLonly:  MoS with LoopLast only nodes.\n");
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
  const auto num_of_matches = net_to_orig_pin_match_map.size();

  /* map : | node | < inputs >             |
   * convert to
   * map : |input | <nodes (flop only)>    | */
  Traverse_lg::map_of_sets inp_map_of_node_sets_orig = convert_io_MoS_to_node_MoS_LLonly(inp_map_of_sets_orig);

  Traverse_lg::map_of_sets inp_map_of_sets_synth_LLonlly = obtain_MoS_LLonly(inp_map_of_sets_synth);
  // for (auto it = inp_map_of_sets_synth.begin(); it != inp_map_of_sets_synth.end();) {
  for (const auto& [synth_key, synth_set] : inp_map_of_sets_synth_LLonlly) {
    // const auto &synth_key = it->first;
    // const auto &synth_set = it->second;
    /*const auto& synth_key = (inp_map_of_sets_synth.find(node_ll))->first;
    const auto& synth_set = (inp_map_of_sets_synth.find(node_ll))->second;*/

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
      #ifdef BASIC_DBG
        auto np_o = Node_pin("lgdb", orig_key);
        fmt::print("\t\t\t matching with: {}, n{}\n",np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())),np_o.get_node().get_nid() );
      #endif
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
        #ifdef BASIC_DBG
	fmt::print("\t\t\t\t----Beter match found! match num: {} \n", match_curr);
        #endif
      } else if (match_curr == match_prev) {
        matched_node_pins.insert(orig_key);
        #ifdef BASIC_DBG
	fmt::print("\t\t\t\t----SAME match found! match num: {} \n", match_curr);
        #endif
      } 
      #ifdef BASIC_DBG
      else { fmt::print("\t\t\t\t----worse match found!match num: {} \n", match_curr);}
      #endif
    }

    if (!matched_node_pins.empty()) {
      net_to_orig_pin_match_map[synth_key].insert(matched_node_pins.begin(), matched_node_pins.end());
      mark_loop_stop.insert(synth_key);
      mark_loop_stop.insert(matched_node_pins.begin(), matched_node_pins.end());
#ifdef FOR_EVAL
      auto np_s = Node_pin("lgdb", synth_key);
      fmt::print("Inserting in weighted_match_LoopLastOnly : {}(n{})  :::  ",
                 np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())),std::to_string(np_s.get_node().get_nid()) );
      for (auto np_o_set : matched_node_pins) {
        auto np_o = Node_pin("lgdb", np_o_set);
        fmt::print("    {}({},{},{}) ", np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())), np_o.get_node().is_type_loop_last(), np_o.get_node().get_type_name(), np_o.get_node().has_name()?np_o.get_node().get_name():"" );
      }
      fmt::print("\n");
#endif
      forced_match_vec.emplace_back(synth_key);
      if (flop_set_synth.find(synth_key) != flop_set_synth.end()) {
        flop_set_synth.erase(synth_key);
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
  fmt::print("num_of_matches: {}, IN_FUNC: weighted_match_LoopLastOnly \n",net_to_orig_pin_match_map.size()-num_of_matches);
}

void Traverse_lg::weighted_match() {  // only for the crit_node_entries remaining!
#ifdef BASIC_DBG
  fmt::print("In weighted_match:\n\n");
#endif
  #ifdef FULL_RUN_FOR_EVAL_TESTING
  fmt::print("SynthNode, SrcNode, OUTmatches, OUTmismatches, INmatches, INmismatches, crossovers, loops, match\n");
  #endif
  const auto num_of_matches = net_to_orig_pin_match_map.size();
  for (const auto& synth_key : crit_node_set) {
    #ifdef BASIC_DBG
    auto synth_key_pin = Node_pin("lgdb", synth_key);
    fmt::print("\nsynth_key_pin name:{}, pid:{}\n", synth_key_pin.has_name()?synth_key_pin.get_name():("p"+std::to_string(synth_key_pin.get_pid())), std::to_string(synth_key_pin.get_pid()));
    #endif
    I(inp_map_of_sets_synth.find(synth_key) != inp_map_of_sets_synth.end(), "\n synth_key NOT in inp_Map_of_sets_synth!! check!\n");
    const auto& synth_set = inp_map_of_sets_synth[synth_key];
    #ifdef BASIC_DBG
    fmt::print("|||||||| synth_set|||||||\n");
    print_set(synth_set);
    fmt::print("\n||||---||||\n");
    #endif

    const auto synth_out_set = out_map_of_sets_synth.find(synth_key);
    bool loop_detected=false; //some common point in synth in set and out set means loop! we don't want to bother any such key.
    if(synth_out_set != out_map_of_sets_synth.end() ){
      loop_detected = common_element_present(synth_out_set->second,synth_set);
    }

    float                                       match_prev = 0.0;
    absl::flat_hash_set<Node_pin::Compact_flat> matched_node_pins;
    for (const auto &[orig_key, orig_set] : inp_map_of_sets_orig) {
      if(unwanted_orig_NPs.contains(orig_key)) { continue; }
      //#ifndef FULL_RUN_FOR_EVAL
      const auto np_o =Node_pin("lgdb", orig_key);
      if( !((np_o.get_node()).has_loc()) ) {continue;}
      //#endif
      #ifdef BASIC_DBG
      fmt::print("\t\t\t matching with: {}, n{} ({})\n",np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())),np_o.get_node().get_nid(), np_o.get_node().get_type_name() );
      #endif

      #ifdef FULL_RUN_FOR_EVAL_TESTING
	const auto& synth_node = Node_pin("lgdb",synth_key).get_node();
	const auto& orig_node = Node_pin("lgdb",orig_key).get_node();
	const auto& synth_node_name = synth_node.has_name() ? synth_node.get_name() : "N.A" ;
	const auto& orig_node_name = orig_node.has_name() ? orig_node.get_name() : "N.A" ;
	fmt::print("{}, {}, ",synth_node_name, orig_node_name);
      #endif
      float match_curr = 0.0;
      auto        out_match = 0.0;
      if (synth_out_set != out_map_of_sets_synth.end()
          && out_map_of_sets_orig.find(orig_key) != out_map_of_sets_orig.end()) {  // both outs are present
        out_match = get_matching_weight(out_map_of_sets_synth[synth_key], out_map_of_sets_orig[orig_key]);
      } else if (synth_out_set != out_map_of_sets_synth.end()
                 || out_map_of_sets_orig.find(orig_key) != out_map_of_sets_orig.end()) {  // only 1 out is present
        out_match = 0.0;                                                                  // no match at all
        #ifdef FULL_RUN_FOR_EVAL_TESTING
        fmt::print("0, 0, ");
        #endif
      } else {                                                                            // no out is present
        out_match = 5.0;                                                                  // complete match
        #ifdef FULL_RUN_FOR_EVAL_TESTING
        fmt::print("F, 0, ");
        #endif
      }
      const auto in_match  = get_matching_weight(synth_set, orig_set);
      match_curr = in_match + out_match;

      if (match_curr >= match_prev) {
        if(!loop_detected && synth_out_set != out_map_of_sets_synth.end() ){ //no loop in synth and synth-out set present
          if(common_element_present(orig_set,synth_out_set->second)){ //some pin of synth-out-set is in orig-in-set
	    match_curr = 0;
	    crossover_count++;
	    #ifdef FULL_RUN_FOR_EVAL_TESTING
	      fmt::print("Y, ");//crossover detected
            #endif
	  }
	  #ifdef FULL_RUN_FOR_EVAL_TESTING
	    else { fmt::print("N, "); } //crossover Not detected
          #endif
        } 
	#ifdef FULL_RUN_FOR_EVAL_TESTING
	  else { fmt::print("N, "); } //crossover Not detected
        #endif
      }
      #ifdef FULL_RUN_FOR_EVAL_TESTING
        else { fmt::print("N, "); } //crossover Not detected
      #endif

      #ifdef FULL_RUN_FOR_EVAL_TESTING
        fmt::print("{}, ",loop_detected);
      #endif
      
      if (match_curr > match_prev) {
        matched_node_pins.clear();
        matched_node_pins.insert(orig_key);
        match_prev = match_curr;
        #ifdef BASIC_DBG
        fmt::print("\t\t\t\t -- Found better match\n");
        #endif
	#ifdef FULL_RUN_FOR_EVAL_TESTING
	fmt::print("OverwritingPrevious \n");
        #endif
      } else if (match_curr == match_prev) {
        matched_node_pins.insert(orig_key);
        #ifdef BASIC_DBG
        fmt::print("\t\t\t\t -- Found similar match\n");
        #endif
	#ifdef FULL_RUN_FOR_EVAL_TESTING
	fmt::print("AddingToPrevious \n");
        #endif
      } 
      #if defined(BASIC_DBG) || defined(FULL_RUN_FOR_EVAL_TESTING)
      else{fmt::print("\t--Worse \n");}
      #endif
    }

    // fmt::print("matched node pins :\n");
    // print_set(matched_node_pins);
    // fmt::print("\n");

    if (!matched_node_pins.empty() && (match_prev > 0.00)) {
      net_to_orig_pin_match_map[synth_key].insert(matched_node_pins.begin(), matched_node_pins.end());
#ifdef FOR_EVAL
      auto np_s = Node_pin("lgdb", synth_key);
      fmt::print("Inserting in weighted_match : {} with wt {}  :::  ",
                 np_s.has_name() ? np_s.get_name() : ("n" + std::to_string(np_s.get_node().get_nid())),
                 match_prev);
      for (auto np_o_set : matched_node_pins) {
        auto np_o = Node_pin("lgdb", np_o_set);
        fmt::print("    {} ", np_o.has_name() ? np_o.get_name() : ("n" + std::to_string(np_o.get_node().get_nid())));
      }
      fmt::print("\n");
#endif
      forced_match_vec.emplace_back(synth_key);
      if (flop_set_synth.find(synth_key) != flop_set_synth.end()) {
        flop_set_synth.erase(synth_key);
      }
      // remove_from_crit_node_set(synth_key); // might lead to the error:
      //  && \"operator++ called on invalid iterator.\""' failed.
      // out_map_of_sets_synth.erase(synth_key);//commenting this should not impact anything because it is not iterated
      // inp_map_of_sets_synth.erase(synth_key);
    } else if (!matched_node_pins.empty()) {
      auto np_s = Node_pin("lgdb", synth_key);
      fmt::print("\nReporting {} entry does not match with any orig_node.\n",
                 np_s.has_name() ? np_s.get_name()
                                 : ("n" + std::to_string(np_s.get_node().get_nid())));  // If you want to check what was not matched
                                                                                        // with anything, this is the place.
    } else {
      fmt::print("In weighted_match -- unexpected entry to else...Is it a no match whatsoever??\n");
    }
  }
  for (const auto& np : forced_match_vec) {
    remove_from_crit_node_set(np);
  }
  fmt::print("num_of_matches: {}, IN_FUNC: weighted_match \n",net_to_orig_pin_match_map.size()-num_of_matches);
}

bool Traverse_lg::set_theory_match(Traverse_lg::map_of_sets& io_map_of_sets_synth, Traverse_lg::map_of_sets& io_map_of_sets_orig) {
  bool some_matching_done = false;
  const auto num_of_matches = net_to_orig_pin_match_map.size();
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
      if (flop_set_synth.find(synth_key) != flop_set_synth.end()) {
        flop_set_synth.erase(synth_key);
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
  fmt::print("num_of_matches: {}, IN_FUNC: set_theory_match \n",net_to_orig_pin_match_map.size()-num_of_matches);
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

bool Traverse_lg::is_combinationally_connected(const Node &n1, const Node &n2, absl::flat_hash_set<Node> &traversed_nodes) {
  //This checks that the node n2 is somewhere in the output of n1 
  //And the path to n2 is purely combinational
	traversed_nodes.insert(n1);
	bool is_comb_conn = false;
  for (const auto &e : n1.out_edges()) {
    auto n = e.sink.get_node();
    if ( n == n2 ) {
      //n2 IS connected to the node's out.
      is_comb_conn = true;
      break;
    } else if ( n.is_type_flop() || (n.is_type_loop_last() && (!n.is_type_sub())) ) {
      is_comb_conn = false;
    } else if (traversed_nodes.find(n)!= traversed_nodes.end()) {
			//combinational loop?
			is_comb_conn=false;
			break;
		} else {
      is_comb_conn = is_combinationally_connected(n,n2, traversed_nodes);
    }
  }
  return is_comb_conn;
}


void Traverse_lg::print_set(const absl::flat_hash_set<Node_pin::Compact_flat> &set_of_dpins) const {
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
