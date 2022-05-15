//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "traverse_lg.hpp"

#include <iostream>
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "perf_tracing.hpp"
#include <fstream>
#include <bits/stdc++.h>
#include <utility>
#include <string>

static Pass_plugin sample("traverse_lg", Traverse_lg::setup);

void Traverse_lg::setup() {
  Eprp_method m1("inou.traverse_lg", "Prints all nodes of LG", &Traverse_lg::travers);
  //m1.add_label_optional("odir", "path to print the text to", ".");
  register_pass(m1);
}

Traverse_lg::Traverse_lg(const Eprp_var& var) : Pass("inou.traverse_lg", var) {}

void Traverse_lg::travers(Eprp_var& var) {
  //TRACE_EVENT("inou", "LNAST_FROMLG_trans");
  TRACE_EVENT("inou","traverse_lg");
  Lbench b("traverse_lg");

  Traverse_lg p(var);

  for (const auto& l : var.lgs) {
    p.do_travers(l, l->get_name());  // l->get_name gives the name of the top module (generally same as that of file name)
  }
}

void Traverse_lg::do_travers(Lgraph* lg, std::string_view module_name) {
  std::ofstream ofs;
  ofs.open(std::string(module_name), std::ofstream::out | std::ofstream::trunc);
  if(!ofs.is_open()) {
    std::cout<<"unable to open file "<<module_name<<".txt"<<std::endl;
    return ;
  }

  ofs<<std::endl<<std::endl<<module_name<<std::endl<<std::endl<<std::endl;

  for (const auto& node : lg->fast()) {
    ofs<<"===============================\n";
    ofs<<node.debug_name()<<std::endl;
    if (! (node.has_inputs() || node.has_outputs())) {//no i/ps as well as no o/ps
      ofs<<"---------------\n";
    }

    
    if (node.has_inputs()){
    ofs<<"INPUTS:\n";
      for (const auto& indr : node.inp_drivers()) {
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
  }

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
      if(node.is_type_const()){ ofs<<":"<<node.get_type_const().to_pyrope();}
      else if(node.is_type_flop()){ ofs<<":"<<node_pin.get_pin_name()<<"->"<<node.get_driver_pin().get_pin_name()<<"("<<(node.get_driver_pin().has_name()?node.get_driver_pin().get_name():"")<<")";}
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
      if(node.is_type_const()){ ofs<<":"<<node.get_type_const().to_pyrope();}
      else if(node.is_type_flop()){ ofs<<":"<<node_pin.get_pin_name()<<"->"<<node.get_driver_pin().get_pin_name()<<"("<<(node.get_driver_pin().has_name()?node.get_driver_pin().get_name():"")<<")";}
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

