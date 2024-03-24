//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_randomize_dpins.hpp"
#include <utility>
#include "perf_tracing.hpp"


static Pass_plugin sample("randomize_dpins", Pass_randomize_dpins::setup );

void Pass_randomize_dpins::setup(){
  Eprp_method m1("pass.randomize_dpins", "converts LG and randomly selected dpins to *_changedForEval. (for NL2NL testing purposes).", &Pass_randomize_dpins::randomize);

  m1.add_label_required("comb_only","true/false for if you want to annotate flops along with combinational nodes.");
  m1.add_label_required("noise_perc","int value to indicate the percentage of LG nodes to annotate." );
  m1.add_label_required("srcLG", "LG name to annotate and emit new LG_changedForEval");
  register_pass(m1);
}

Pass_randomize_dpins::Pass_randomize_dpins(const Eprp_var& var) : Pass("pass.randomize_dpins", var) {}

void Pass_randomize_dpins::randomize(Eprp_var& var) {
  TRACE_EVENT("pass", "randomize_dpins");

  auto co = var.get("comb_only");
  bool comb_only = co!= "false" && co!="0";
  std::string np = std::string(var.get("noise_perc"));
  auto noise_perc = std::stof(np);
  auto lg_name = var.get("srcLG");
  
  Pass_randomize_dpins p(var);

  Lgraph *lg = nullptr;
  for(const auto& l : var.lgs) {
    if(l->get_name() == lg_name) {
      lg = l;
      break;
    }
  }
  
  p.collect_vectors_from_lg(lg, noise_perc, comb_only);

  p.annotate_lg(lg);

}

void Pass_randomize_dpins::annotate_lg(Lgraph *lg){
  fmt::print("------{}\n",lg->get_name());

  for(const auto& node: lg->fast(true)){
    for (auto& dpin: node.out_connected_pins()) {
      if(std::find(random_selected_pins_vec.begin(), random_selected_pins_vec.end(),dpin.get_compact_flat())!=random_selected_pins_vec.end()){
	if(!dpin.has_name()) {error("random_selected_pins_vec shouldn't have any unnamed pin");}
        auto new_dpin_name = absl::StrCat(dpin.get_name(),"_changedForEval");
	#ifdef FOR_DBG
	fmt::print("randomising:"); print_nodePinCF_details(dpin.get_compact_flat());//dpin.has_name()?dpin.get_name():"-NA-");
	#endif
	dpin.set_name(new_dpin_name);
      }
    }
  }

  //for(auto dpin_cf: random_selected_pins_vec) {
  //  auto dpin = Node_pin("lgdb",dpin_cf);
  //  auto new_dpin_name = absl::StrCat(dpin.get_name(),"_changedForEval");
  //  dpin.reset_name(new_dpin_name);
  //}

}

void Pass_randomize_dpins::collect_vectors_from_lg(Lgraph *lg, float noise_perc, bool comb_only) {
  #ifdef FOR_DBG
  fmt::print("\nnoise perc is: {}\n", noise_perc);
  fmt::print("\nLG name  is: {}\n", lg->get_name());
  fmt::print("\n comb_only: {}\n", comb_only);
  fmt::print("Printing io_pins_vec:\n"); print_vec(io_pins_vec);
  #endif
 
  /* Creating io_pins_vec*/
  lg->each_graph_input([this](const Node_pin non_h_dpin){
    const auto& dpin = non_h_dpin.get_hierarchical();
    io_pins_vec.emplace_back(dpin.get_compact_flat());
  });

  lg->each_graph_output([this](const Node_pin non_h_dpin){
    const auto& dpin = non_h_dpin.get_hierarchical();
    auto spin = dpin.change_to_sink_from_graph_out_driver();
    io_pins_vec.emplace_back(spin.get_compact_flat());
    io_pins_vec.emplace_back(dpin.get_compact_flat());
  });

  #ifdef FOR_DBG
  fmt::print("Printing io_pins_vec: size:{}\n", io_pins_vec.size()); print_vec(io_pins_vec);
  #endif

  /* Creating comb_pins_vec if comb_only==T; else create combNflop_pins_vec*/
  auto node_count = 0;
  for (const auto& node: lg->fast(true)) { 
    node_count++;
    if(!node.is_type_const()) {
      if(node.is_type_loop_last() && !comb_only){
        for(const auto& dpins: node.out_connected_pins()){
	  if(!dpins.has_name()) {continue;}
          combNflop_pins_vec.emplace_back(dpins.get_compact_flat());
          flop_pins_vec.emplace_back(dpins.get_compact_flat());
        }
      } else if (!node.is_type_loop_last() ) {
        for(const auto& dpins: node.out_connected_pins()){
	  if(!dpins.has_name()) {continue;}
          combNflop_pins_vec.emplace_back(dpins.get_compact_flat());
          comb_pins_vec.emplace_back(dpins.get_compact_flat());
        }
      }

    }
  }

  #ifdef FOR_DBG
  fmt::print("total_nodes in fast pass: {}\n", node_count);
  fmt::print("Printing comb_pins_vec: size:{}", comb_pins_vec.size()); print_vec(comb_pins_vec);
  fmt::print("Printing flop_pins_vec: size:{}", flop_pins_vec.size()); print_vec(flop_pins_vec);
  fmt::print("Printing combNflop_pins_vec: size:{}", combNflop_pins_vec.size()); print_vec(combNflop_pins_vec);
  #endif

  /* For random_selected_pins_vec*/
  auto total_dpins = combNflop_pins_vec.size();//total_dpins will be equal to only the considered nodes and NOT totalLGnodes.
  float perc_change = noise_perc/float(100);
  auto num_of_dpins_to_change = int(perc_change * total_dpins );
  /* if !comb_only : select num_of_dpins_to_change dpins from combNflop_pins_vec and store in random_selected_pins_vec 
   * else          : select num_of_dpins_to_change dpins from comb_pins_vec      and store in random_selected_pins_vec*/
  if(comb_only){
    random_selected_pins_vec = chooseMRandomElements(comb_pins_vec, num_of_dpins_to_change);
  } else {
    random_selected_pins_vec = chooseMRandomElements(combNflop_pins_vec, num_of_dpins_to_change);
  }
  #ifdef FOR_DBG
  fmt::print("total_dpins: {}, perc_change: {}, num_of_dpins_to_change : {}\n", total_dpins, perc_change, num_of_dpins_to_change);
  fmt::print("Printing random_selected_pins_vec: size:{}\n", random_selected_pins_vec.size()); print_vec(random_selected_pins_vec);
  #endif

}

void Pass_randomize_dpins::print_vec(std::vector<Node_pin::Compact_flat>& vec_to_print) const {
  fmt::print("\n-SOV -v-v-v-v-v-v-v-v-v-v-v-v-\n");
  for(auto np_cf: vec_to_print){
    auto np = Node_pin("lgdb", np_cf);
    if ( np.is_graph_io() ) {
      fmt::print("-IO node-\t");
    }
    fmt::print("nid:{} ({}) ", np.get_node().get_nid(), np.get_node().debug_name());
    fmt::print(",pin:{}({}) are:: type:{}, lg:{}\n", "p"+std::to_string(np.get_pid()), np.has_name()?np.get_name():("p"+std::to_string(np.get_pid())), np.get_node().get_type_name(),  (np.get_node().get_class_lgraph())->get_name() );
  }
  fmt::print("\n-EOV -v-v-v-v-v-v-v-v-v-v-v-v-\n");
}
void Pass_randomize_dpins::print_nodePinCF_details(const Node_pin::Compact_flat& np_cf  ) const {
    auto np = Node_pin("lgdb", np_cf);
    if ( np.is_graph_io() ) {
      fmt::print("-IO node-\t");
    }
    fmt::print("nid:{} ({}) ", np.get_node().get_nid(), np.get_node().debug_name());
    fmt::print(",pin:{}({}) are:: type:{}, lg:{}\n", "p"+std::to_string(np.get_pid()), np.has_name()?np.get_name():"N.A", np.get_node().get_type_name(),  (np.get_node().get_class_lgraph())->get_name() );
 }
