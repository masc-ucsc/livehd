//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "traverse_lg.hpp"

#include <iostream>
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "perf_tracing.hpp"
#include <fstream>
#include <utility>
#include <string>
#include <algorithm>


static Pass_plugin sample("traverse_lg", Traverse_lg::setup);

void Traverse_lg::setup() {
  Eprp_method m1("inou.traverse_lg", "Prints all nodes (and each of their IOs) of LG", &Traverse_lg::travers);
  //m1.add_label_optional("odir", "path to print the text to", ".");
  register_pass(m1);
}

Traverse_lg::Traverse_lg(const Eprp_var& var) : Pass("inou.traverse_lg", var) {}

void Traverse_lg::travers(Eprp_var& var) {
  //TRACE_EVENT("inou", "LNAST_FROMLG_trans");
  TRACE_EVENT("inou","traverse_lg");
  Lbench b("traverse_lg");

  Traverse_lg p(var);

  I(var.lgs.size()==2,"\n\nINPUT ERROR:\n\t provide only 2 lgraphs to inou.traverse_lg. First LG will be the pre-synth LG and Second LG should be the one from synth netlist.\n\n");
  I(var.lgs.front()->get_name()!=var.lgs.back()->get_name(), "\n\nINPUT ERROR:\n\t The 2 lgraphs provided to inou.traverse_lg should be of different names. First LG will be the pre-synth LG and Second LG should be the one from synth netlist.\n\n");

#ifdef KEEP_DUP
  Traverse_lg::vecMap map_pre_synth;
  Traverse_lg::vecMap map_post_synth;
  p.do_travers(var.lgs.front(), map_pre_synth);
  p.do_travers(var.lgs.back(), map_post_synth);
#endif

#ifdef DE_DUP
  Traverse_lg::setMap map_pre_synth;
  Traverse_lg::setMap map_post_synth;
  p.do_travers(var.lgs.front(), map_pre_synth);
  p.do_travers(var.lgs.back(), map_post_synth);
#endif

#ifdef DEBUG
  for (const auto& l : var.lgs) {
    p.do_travers(l);
  }
#endif

}

//FOR DEBUG:
void Traverse_lg::do_travers(Lgraph* lg) {
  //std::string_view module_name = lg->get_name();
  std::ofstream ofs;
  ofs.open(std::string(lg->get_name()), std::ofstream::out | std::ofstream::trunc);
  if(!ofs.is_open()) {
    fmt::print("unable to open file {}.txt\n",lg->get_name());
    return ;
  }
  ofs<<std::endl<<std::endl<<lg->get_name()<<std::endl<<std::endl<<std::endl;

  for (const auto& node : lg->forward()) {
    ofs<<"===============================\n";
    ofs<<node.debug_name()<<std::endl; //this is the main node for which we will record IO
    if (! (node.has_inputs() || node.has_outputs())) {//no i/ps as well as no o/ps
      ofs<<"---------------\n";
      continue; //do not keep such nodes in nodeIOmap
    }

    
    if (node.has_inputs()){
    ofs<<"INPUTS:\n";
      for (const auto& indr : node.inp_drivers()) {
        if(node.get_type_op()==Ntype_op::AttrSet && indr.get_node().get_type_op()==Ntype_op::Const) {continue;}
        //auto inp_node = indr.get_node();
        get_input_node(indr, ofs);
      }
    }

    if (node.has_outputs()) {
    ofs<<"OUTPUTS:\n";
      for (const auto& outdr : node.out_sinks() ) {//outdr is the pin of the output node.
        //auto out_node = outdr.get_node();
        get_output_node(outdr,ofs);
      }
    }

  }//enf of for lg-> traversal

  if(ofs.is_open()) {
    ofs.close();
  }
}


void Traverse_lg::get_input_node(const Node_pin &node_pin, std::ofstream& ofs) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_type_const() || node.is_graph_input() ) {
    if(node.is_graph_io()) {
      ofs<<node_pin.get_pin_name()<<std::endl;
    } else {
      ofs<<node.get_type_name();
      //if(node.is_type_const()){ ofs<<":"<<node.get_type_const().to_pyrope();}
      //else 
      if(node.is_type_flop()){ ofs<<":"<<node_pin.get_pin_name()<<"->"<<node.get_driver_pin().get_wire_name();}
      ofs<<std::endl;
    }
    return;
  } else {
      for (const auto& indr : node.inp_drivers()) {
        //auto inp_node = indr.get_node();
        get_input_node(indr,ofs);
      }
  }
}

void Traverse_lg::get_output_node(const Node_pin &node_pin, std::ofstream& ofs) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_graph_output() ) {
    if(node.is_graph_io()) {
      ofs<<node_pin.get_pin_name()<<std::endl;
    } else {
      ofs<<node.get_type_name();
      //if(node.is_type_const()){ ofs<<":"<<node.get_type_const().to_pyrope();}
      //else 
      if(node.is_type_flop()){ ofs<<":"<<node_pin.get_pin_name()<<"->"<<node.get_driver_pin().get_wire_name();}
      ofs<<std::endl;
    }
    return;
  } else {
      for (const auto& outdr : node.out_sinks() ) {
        //auto out_node = outdr.get_node();
        get_output_node(outdr,ofs);
      }
  }
}

//FOR VECTOR:
void Traverse_lg::do_travers(Lgraph* lg, Traverse_lg::vecMap &nodeIOmap) {

  for (const auto& node : lg->forward()) {
    std::vector<std::string> in_vec;
    std::vector<std::string> out_vec;
    fmt::print("{}\n", node.debug_name());
    if (! (node.has_inputs() || node.has_outputs())) {//no i/ps as well as no o/ps
      continue; //do not keep such nodes in nodeIOmap
    }

    
    if (node.has_inputs()){
      for (const auto& indr : node.inp_drivers()) {
        if(node.get_type_op()==Ntype_op::AttrSet && indr.get_node().get_type_op()==Ntype_op::Const) {continue;}
        //auto inp_node = indr.get_node();
        get_input_node(indr, in_vec);
      }
    }

    if (node.has_outputs()) {
      for (const auto& outdr : node.out_sinks() ) {//outdr is the pin of the output node.
        //auto out_node = outdr.get_node();
        get_output_node(outdr, out_vec);
      }
    }

    //print the vectors formed
    fmt::print("INPUTS:\n");
    for (const auto& i:in_vec) {
      fmt::print("\t{}\n",i);
    }
    fmt::print("OUTPUTS:\n");
    for (const auto& i:out_vec) {
      fmt::print("\t{}\n",i);
    }

    if (in_vec.empty() && out_vec.empty()) {//no i/ps as well as no o/ps
      continue;//do not keep such nodes in nodeIOmap
    }
    //insert in map
    const auto& nodeid = node.get_compact_flat();
    nodeIOmap[nodeid]= std::make_pair(in_vec,out_vec);//FIXME: make hash of vectors and change datatype accordingly
  }//enf of for lg-> traversal

  //print the map
  fmt::print("\n\nMAP FORMED IS:\n");
  for(auto& [n, ioPair]: nodeIOmap) {
    fmt::print("{} has INPUTS:  \t",n.get_nid());
    for (auto& ip: ioPair.first) {
      fmt::print("{}\t",ip);
    }
    fmt::print("\nand OUTPUTS: \t");
    for (auto& op:ioPair.second) {
      fmt::print("{}\t", op);
    }
    fmt::print("\n\n\n");
  }

}


void Traverse_lg::get_input_node(const Node_pin &node_pin, std::vector<std::string>& in_vec) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_type_const() || node.is_graph_input() ) {
    if(node.is_graph_io()) {
      in_vec.emplace_back(node_pin.has_name()?node_pin.get_name():node_pin.get_pin_name());
    } else {
      std::string temp_str (node.get_type_name());
      //if(node.is_type_const()){ 
      //  temp_str+=":"; 
      //  temp_str+=node.get_type_const().to_pyrope();
      //}
      //else 
      if(node.is_type_flop()){ 
        temp_str+=":";
        temp_str+=node_pin.get_pin_name();
        temp_str+="->";
        //temp_str+=node.get_driver_pin().get_pin_name();
        //temp_str+="(";
        temp_str+=(node.get_driver_pin().get_wire_name());
        //temp_str+=")";
      }
      in_vec.emplace_back(temp_str);
    }
    return;
  } else {
      for (const auto& indr : node.inp_drivers()) {
        //auto inp_node = indr.get_node();
        get_input_node(indr, in_vec);
      }
  }
}

void Traverse_lg::get_output_node(const Node_pin &node_pin, std::vector<std::string>& out_vec) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_graph_output() ) {
    if(node.is_graph_io()) {
      //out_vec.emplace_back(node_pin.has_name()?node_pin.get_name():node_pin.get_pin_name());
      out_vec.emplace_back(node_pin.get_pin_name());
    } else {
      std::string temp_str(node.get_type_name());
      //if(node.is_type_const()){ 
      //  temp_str+=":"; 
      //  temp_str+=node.get_type_const().to_pyrope();
      //}
      //else 
      if(node.is_type_flop()){ 
        temp_str+=":";
        temp_str+=node_pin.get_pin_name();
        temp_str+="->";
        //temp_str+=node.get_driver_pin().get_pin_name();
        //temp_str+="(";
        temp_str+=(node.get_driver_pin().get_wire_name());
        //temp_str+=")";
      }
      out_vec.emplace_back(temp_str);
    }
    return;
  } else {
      for (const auto& outdr : node.out_sinks() ) {
        //auto out_node = outdr.get_node();
        get_output_node(outdr, out_vec);
      }
  }
}

//FOR SET:

void Traverse_lg::do_travers(Lgraph* lg, Traverse_lg::setMap &nodeIOmap) {

  for (const auto& node : lg->forward()) {
    absl::flat_hash_set<std::string> in_set;
    absl::flat_hash_set<std::string> out_set;
    fmt::print("{}\n", node.debug_name());
    if (! (node.has_inputs() || node.has_outputs())) {//no i/ps as well as no o/ps
      continue; //do not keep such nodes in nodeIOmap
    }

    
    if (node.has_inputs()){
      for (const auto& indr : node.inp_drivers()) {
        //auto inp_node = indr.get_node();
        if(node.get_type_op()==Ntype_op::AttrSet && indr.get_node().get_type_op()==Ntype_op::Const) {continue;}
        get_input_node(indr, in_set);
      }
    }

    if (node.has_outputs()) {
      for (const auto& outdr : node.out_sinks() ) {//outdr is the pin of the output node.
        //auto out_node = outdr.get_node();
        get_output_node(outdr, out_set);
      }
    }

    //print the set formed
    fmt::print("INPUTS:\n");
    for (const auto& i:in_set) {
      fmt::print("\t{}\n",i);
    }
    fmt::print("OUTPUTS:\n");
    for (const auto& i:out_set) {
      fmt::print("\t{}\n",i);
    }

    if (in_set.empty() && out_set.empty()) {//no i/ps as well as no o/ps
      continue;//do not keep such nodes in nodeIOmap
    }
    //insert in map
    const auto& nodeid = node.get_compact_flat();
    nodeIOmap[nodeid]= std::make_pair(in_set,out_set);//FIXME: make hash of set and change datatype accordingly
  }//enf of for lg-> traversal

  //print the map
  fmt::print("\n\nMAP FORMED IS:\n");
  for(auto& [n, ioPair]: nodeIOmap) {
    fmt::print("{} has INPUTS:  \t",n.get_nid());
    for (auto& ip: ioPair.first) {
      fmt::print("{}\t",ip);
    }
    fmt::print("\nand OUTPUTS: \t");
    for (auto& op:ioPair.second) {
      fmt::print("{}\t", op);
    }
    fmt::print("\n\n\n");
  }

}

void Traverse_lg::get_input_node(const Node_pin &node_pin, absl::flat_hash_set<std::string>& in_set) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_type_const() || node.is_graph_input() ) {
    if(node.is_graph_io()) {
      in_set.insert(node_pin.has_name()?node_pin.get_name():node_pin.get_pin_name());
    } else {
      std::string temp_str (node.get_type_name());
      //if(node.is_type_const()){ 
      //  temp_str+=":"; 
      //  temp_str+=node.get_type_const().to_pyrope();
      //}
      //else 
      if(node.is_type_flop()){ 
        temp_str+=":";
        temp_str+=node_pin.get_pin_name();
        temp_str+="->";
        //temp_str+=node.get_driver_pin().get_pin_name();
        //temp_str+="(";
        temp_str+=(node.get_driver_pin().get_wire_name());
        //temp_str+=")";
      }
      in_set.insert(temp_str);
    }
    return;
  } else {
      for (const auto& indr : node.inp_drivers()) {
        //auto inp_node = indr.get_node();
        get_input_node(indr, in_set);
      }
  }
}

void Traverse_lg::get_output_node(const Node_pin &node_pin, absl::flat_hash_set<std::string>& out_set) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_graph_output() ) {
    if(node.is_graph_io()) {
      //out_set.emplace_back(node_pin.has_name()?node_pin.get_name():node_pin.get_pin_name());
      out_set.insert(node_pin.get_pin_name());
    } else {
      std::string temp_str(node.get_type_name());
      //if(node.is_type_const()){ 
      //  temp_str+=":"; 
      //  temp_str+=node.get_type_const().to_pyrope();
      //}
      //else 
      if(node.is_type_flop()){ 
        temp_str+=":";
        temp_str+=node_pin.get_pin_name();
        temp_str+="->";
        //temp_str+=node.get_driver_pin().get_pin_name();
        //temp_str+="(";
        temp_str+=(node.get_driver_pin().get_wire_name());
        //temp_str+=")";
      }
      out_set.insert(temp_str);
    }
    return;
  } else {
      for (const auto& outdr : node.out_sinks() ) {
        //auto out_node = outdr.get_node();
        get_output_node(outdr, out_set);
      }
  }
}
// p node.inp_connected_pins().front().get_pin_name()
// void Traverse_lg::do_travers(Lgraph* lg, std::string_view module_name) {
//   std::cout<<module_name<<std::endl;
//   for (const auto& node : lg->fast()) {
//     std::cout<<node.debug_name()<<std::endl;
//     node.dump();
//     for (const auto& dpin : node.out_connected_pins()) {
//       auto dpin_editable = dpin;
//       //auto ntype         = dpin.get_node().get_type_op();
//       //node.get_type_const().to_pyrope()
//       if (!dpin_editable.has_name() ){//&& !((ntype == Ntype_op::IO) || (ntype == Ntype_op::Const))) {
//            std::cout<<"no name"<<std::endl;
//       } else {
//         std::cout<<"name"<<std::endl;
//       }
//     }
// 
//   }
// 
// }

