//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_json.hpp"
#include "core/edge.hpp"
#include "core/lgraph.hpp"
#include "inou_json.hpp"
#include "../../core/lgraph.hpp"
#include "core/node_pin.hpp"
#include "lgedgeiter.hpp"
#include "../../task/lbench.hpp"
#include "../../elab/lnast.hpp"

#include "lg_to_yjson.hpp"
#include "yosys_json.hpp"

#include <iostream>
#include <fstream>
#include <map>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

vector<Port*> LGtoYJson::primitive_2inp_ABY {new Port("A", pdInput), new Port("B", pdInput), new Port("Y", pdOutput) };
vector<Port*> LGtoYJson::primitive_1inp_AY {new Port("A", pdInput), new Port("Y", pdOutput) };
vector<Port*> LGtoYJson::primitive_mux {new Port("A", pdInput), new Port("B", pdInput), new Port("S", pdInput), new Port("Y", pdOutput) };

Prototype LGtoYJson::primitives[] = {
  Prototype(Ntype_op::Invalid, NULL), // DO NOT DELETE THIS LINE !!
  Prototype(Ntype_op::Sum,  &LGtoYJson::primitive_2inp_ABY),
  Prototype(Ntype_op::Mult, &LGtoYJson::primitive_2inp_ABY),
  Prototype(Ntype_op::Div,  &LGtoYJson::primitive_2inp_ABY),
  
  Prototype(Ntype_op::And,  &LGtoYJson::primitive_2inp_ABY),
  Prototype(Ntype_op::Or,   &LGtoYJson::primitive_2inp_ABY),
  Prototype(Ntype_op::Xor,  &LGtoYJson::primitive_2inp_ABY),
  Prototype(Ntype_op::Ror,  &LGtoYJson::primitive_2inp_ABY),
  Prototype(Ntype_op::Not,  &LGtoYJson::primitive_1inp_AY),
  Prototype(Ntype_op::Sext, NULL),
  Prototype(Ntype_op::GT,   &LGtoYJson::primitive_2inp_ABY),  // Greater Than, also LE = !GT
  Prototype(Ntype_op::EQ,   &LGtoYJson::primitive_2inp_ABY),  // Equal       , also NE = !EQ
  Prototype(Ntype_op::LT,   &LGtoYJson::primitive_2inp_ABY),  // Less Than   , also GE = !LT

  Prototype(Ntype_op::SHL,  &LGtoYJson::primitive_2inp_ABY),  // Shift Left Logical
  Prototype(Ntype_op::SRA,  &LGtoYJson::primitive_2inp_ABY),  // Shift Right Arithmetic

  Prototype(Ntype_op::LUT,  NULL),  // LUT
  Prototype(Ntype_op::Mux,  &LGtoYJson::primitive_mux),  // Multiplexor with many options

  Prototype(Ntype_op::IO,   NULL),  // Graph Input or Output

  //------------------BEGIN PIPELINED (break LOOPS)
  Prototype(Ntype_op::Memory,   NULL),

  Prototype(Ntype_op::Flop,   NULL),   // Asynchronous & sync reset flop
  Prototype(Ntype_op::Latch,  NULL),  // Latch
  Prototype(Ntype_op::Fflop,  NULL),  // Fluid flop

  Prototype(Ntype_op::Sub,  NULL),  // Sub module instance
  //------------------END PIPELINED (break LOOPS)
  Prototype(Ntype_op::Const,  NULL),  // Constant

  // High Level Lgraph constructs

  Prototype(Ntype_op::TupAdd,   NULL),
  Prototype(Ntype_op::TupGet,   NULL),

  Prototype(Ntype_op::AttrSet,  NULL),
  Prototype(Ntype_op::AttrGet,  NULL),

  Prototype(Ntype_op::CompileErr,   NULL),  // Indicate a compile error during a pass

  Prototype(Ntype_op::Last_invalid, NULL)
  
} ;

void LGtoYJson::create_all_wires (Lgraph *lg, Module* module) {
  for (auto node : lg->forward()) {
      auto op = node.get_type_op();
      if(op == Ntype_op::Get_mask) 
        continue;
      else if(op == Ntype_op::Const) {
        for(auto e : node.out_edges()) {
          // printf("^^^^^^^^^^^^^^^^^^: %s\n\n", e.driver.get_wire_name().to_s().c_str());
          if(e.sink.get_node().get_type_op() != Ntype_op::Get_mask) {
            // TODO: if const value is not -1
            module->create_wires(node.out_edges());
            // TODO: get proper const value as int
            //int value = stoi(node.get_type_const().to_pyrope().to_s(), 0, 16);
            int value = node.get_type_const().to_i();
            module->get_wire(e.driver.get_wire_name().to_s())->set_const_value(value);
            break;
          }
          // TODO: else for Get_mask const calculation
        }
      }
      else { // node is neither Get_mask nor Const
        module->create_wires(node.out_edges());
      }
    }
    for (auto node : lg->forward()) { // add aliases for MASK outputs
      if(node.get_type_op() == Ntype_op::Get_mask) {
        module->add_wire_alias(OUT_DRV_NAME((&node), 0), node.get_sink_pin("a").get_driver_pin().get_wire_name().to_s());
      }
    }
}

void LGtoYJson::Reset(){
}

Prototype* LGtoYJson::find_primitive(Ntype_op op){
  for(int i = 0 ; primitives[i].get_type() != Ntype_op::Last_invalid ; i++){
    if ( primitives[i].get_type() == op){
      return &primitives[i];
    }
  }
  return NULL;
}

void LGtoYJson::conncet_cell(Module *module, Cell *cell, Node *node){
  auto op = node->get_type_op();
  if (op == Ntype_op::Sum) {
    cell->add_connection(module->get_wire(INP_DRV_NAME(node, 0)));
    cell->add_connection(module->get_wire(INP_DRV_NAME(node, 1)));
    cell->add_connection(module->get_wire(OUT_DRV_NAME(node, 0)));
    cell->Info().is_subtract = (node->inp_edges()[1].sink.get_pid() == 1 || node->inp_edges()[0].sink.get_pid() == 1);//Subtract
  } else if (op == Ntype_op::Div || op == Ntype_op::Sext || op == Ntype_op::SRA){
    cell->add_connection(PIN_WIRE("a"));
    cell->add_connection(PIN_WIRE("b"));
    cell->add_connection(module->get_wire(OUT_DRV_NAME(node, 0)));
  } else if (op == Ntype_op::Set_mask){
  } else if (op == Ntype_op::LT || op == Ntype_op::GT){
  } else if (op == Ntype_op::SHL) {
    cell->add_connection(PIN_WIRE("a"));
    cell->add_connection(PIN_WIRE("B"));
    cell->add_connection(module->get_wire(OUT_DRV_NAME(node, 0)));
  } else if (op == Ntype_op::Not){
    cell->add_connection(module->get_wire(node->inp_edges()[0].driver.get_wire_name().to_s()));
    cell->add_connection(module->get_wire(node->out_edges()[0].driver.get_wire_name().to_s()));
  } else if (op == Ntype_op::Sub) {
    const auto &sub   = node->get_type_sub_node();
    for (auto &io_pin : sub.get_sorted_io_pins()) {
      Node_pin dpin;
      if (io_pin.is_input()) {
        auto spin = node->get_sink_pin(io_pin.name);
        dpin      = spin.get_driver_pin();   
      } else {
        dpin = node->get_driver_pin(io_pin.name);
        if (!dpin.is_connected())
          dpin.invalidate();
      }
      cell->add_connection(module->get_wire(dpin.get_wire_name().to_s()));
      if (!dpin.is_invalid()) {
      }
    }
  } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::Mult || op == Ntype_op::Ror || op == Ntype_op::EQ){
    cell->add_connection(module->get_wire(INP_DRV_NAME(node, 0)));
    cell->add_connection(module->get_wire(INP_DRV_NAME(node, 1)));
    cell->add_connection(module->get_wire(OUT_DRV_NAME(node, 0)));
  } else if (op == Ntype_op::Mux) {  
    auto ordered_inp = node->inp_edges_ordered();
    cell->add_connection(module->get_wire(ordered_inp[1].driver.get_wire_name().to_s()));
    cell->add_connection(module->get_wire(ordered_inp[2].driver.get_wire_name().to_s()));
    cell->add_connection(module->get_wire(ordered_inp[0].driver.get_wire_name().to_s()));
    cell->add_connection(module->get_wire(node->get_driver_pin().get_wire_name().to_s()));
  } else if (op == Ntype_op::TupAdd || op == Ntype_op::TupGet || op == Ntype_op::AttrSet || op == Ntype_op::AttrGet) {
    node->dump();
    Pass::error("Cannot generate JSON unless it is low level Lgraph node:{} is type {}\n",
                node->debug_name(),
                Ntype::get_name(op));
    return;
  }
}

yjson::Module* LGtoYJson::add_new_module(yjson::StrTyp name) {
    _modules.push_back(new yjson::Module(name.to_s()));
    return _modules.back();
}

void LGtoYJson::import_io_ports(Lgraph *lg, yjson::Module *module) 
{
  absl::flat_hash_map<mmap_lib::str, uint32_t> unique_inputs;
  auto inp_io_node = lg->get_graph_input_node();
  // map each unique INPUT to its width
  for (const auto& edge : inp_io_node.out_edges()) { 
    I(edge.driver.has_name());
    auto pin_name = edge.driver.get_name();
    if (unique_inputs.contains(pin_name)) 
      continue;    
    unique_inputs[pin_name] = edge.get_bits();
  }  
  for (auto& inp : unique_inputs) {
    auto port_wire = module->create_single_wire(inp.first.to_s(), inp.second);
    module->add_port(&inp.first, pdInput, port_wire);
  }

  // add aliases for newly created input wires 
  auto internal_inp_edges = lg->get_graph_input_node().out_edges();
  for (auto& inp_edge : internal_inp_edges) {
    module->add_wire_alias(inp_edge.driver.get_wire_name().to_s(), inp_edge.driver.get_name().to_s());
  }

  // map each unique output to its width
  absl::flat_hash_map<mmap_lib::str, uint32_t> unique_outputs;
  auto out_io_node = lg->get_graph_output_node();
   for (const auto& edge : out_io_node.inp_edges()) {
    auto sink_pid = edge.sink.get_pid();
    auto out_pin  = edge.sink.get_node().get_driver_pin_raw(sink_pid);
    I(out_pin.has_name());
    auto pin_name = out_pin.get_name();
    if (unique_outputs.contains(pin_name)) 
      continue;
    unique_outputs[pin_name] = edge.get_bits();
  }
  for (auto& outp : unique_outputs) {
    auto port_wire = module->create_single_wire(outp.first.to_s(), outp.second);
    module->add_port (&outp.first, pdOutput, port_wire);
  }

  // add aliases for newly created output wires 
  auto internal_out_edges = lg->get_graph_output_node().inp_edges();
  for (auto& out_edge : internal_out_edges) { 
    module->add_wire_alias(out_edge.driver.get_wire_name().to_s(), out_edge.sink.get_name().to_s());
  }
}

yjson::Module* LGtoYJson::find_module(std::string module_name) {
  for(auto m : _modules) 
    if(strcmp(m->get_name(), module_name.c_str())==0)
      return m;
  return NULL;
}

yjson::Prototype* LGtoYJson::find_node_prototype(Node* node, Ntype_op op) {
  Module* cell_type = NULL;
  if (op == Ntype_op::Sub || op == Ntype_op::Memory) {
    const auto &sub   = node->get_type_sub_node();
    cell_type = find_module(sub.get_name().to_s());
    if(cell_type == NULL) { // Module not found
      cell_type = add_new_module(sub.get_name());
      for (auto &io_pin : sub.get_sorted_io_pins()) { // add module ports
        cell_type->add_port(&io_pin.name, io_pin.is_input() ? pdInput : pdOutput, NULL);
      }
    }
  }
  return (yjson::Prototype*)cell_type;
}

bool is_node_invalid(const Node& node) {
  if (node.get_num_out_edges() == 0 || !node.has_outputs() || node.is_type_flop())
    return true;
  if (node.get_driver_pin().get_bits()==0) {
    node.dump();
    Pass::error("node:{} does not have bits set. It needs bits to generate correct JSON", node.debug_name());
    return true;
  }      
  return false;
}

void LGtoYJson::import_module(Lgraph *lg){
  auto newModule = add_new_module(lg->get_name());
  import_io_ports(lg, newModule);
  create_all_wires(lg, newModule);

  for (auto node : lg->forward()) {
    auto op = node.get_type_op();
    if (op == Ntype_op::Get_mask || op == Ntype_op::Const)
      continue;

    const Prototype* cell_type = NULL;
    if (Ntype::is_multi_driver(op)) { //submodule, memory ...
      cell_type = find_node_prototype(&node, op);
    } else {
      if (is_node_invalid(node))
        continue;      
      cell_type = find_primitive(op);
    }

    if (cell_type == NULL){
      fmt::print("Cell type not found!\n");
      continue;
    }
    auto new_cell = newModule->add_cell(cell_type);
    new_cell->set_name(node.debug_name());
    conncet_cell(newModule, new_cell, &node);
  }
}

void LGtoYJson::to_json(Eprp_var &var) {
  Reset();
  for(const auto &lg : var.lgs){
    import_module(lg);
  }

  mmap_lib::str odir(var.get("odir"));
  auto file_name = mmap_lib::str::concat(odir, "/", var.lgs[0]->get_name(), ".json");

  ofstream outdata; // outdata is like cin
  outdata.open(file_name.to_s()); 
   if( !outdata ) { // file couldn't be opened
      cerr << "Error: file could not be opened" << endl;
      exit(1);
   }
  outdata << "{";
  JsonComposer jcm(outdata);

  JsonElement model[] = { {"modules", VectorAsObject(&_modules) }, {} };
  jcm.Write(model);
  outdata << "}";
  outdata.close();
}
