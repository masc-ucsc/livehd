//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
/*
Author: Farzaneh Rabiei, GitHub: https://github.com/rabieifk
*/
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "../../core/lgraph.hpp"
#include "../../elab/lnast.hpp"
#include "../../task/lbench.hpp"
#include "core/edge.hpp"
#include "core/lgraph.hpp"
#include "core/node_pin.hpp"
#include "file_output.hpp"
#include "inou_json.hpp"
#include "lgedgeiter.hpp"
#include "yosys_json.hpp"

#define INP_DRV_NAME(node, input_index)  node->inp_edges()[input_index].driver.get_wire_name().to_s()
#define OUT_DRV_NAME(node, output_index) node->out_edges()[output_index].driver.get_wire_name().to_s()
#define PIN_WIRE(pin_name)               module->get_wire(node->get_sink_pin(pin_name).get_driver_pin().get_wire_name().to_s())

using namespace std;
using namespace yjson;

class JsonWriter {
public:
  void AddObj(const char* name);
};

class LGtoYJson {
private:
  static Prototype     primitives[];
  static vector<Port*> primitive_2inp_ABY;
  static vector<Port*> primitive_1inp_AY;
  static vector<Port*> primitive_mux;

  absl::flat_hash_set<std::string>              cell_pins;
  absl::flat_hash_map<std::string, std::string> masked_wire;
  vector<yjson::Module*>                        _modules;

  void              process_multi_driver_node(Node* node, Ntype_op op);
  void              import_module(Lgraph* lg);
  yjson::Module*    add_new_module(yjson::StrTyp name);
  yjson::Module*    find_module(string target_name);
  yjson::Prototype* find_node_prototype(Node* node, Ntype_op op);
  Prototype*        find_primitive(Ntype_op op);

  void import_io_ports(Lgraph* lg, yjson::Module* md);
  void conncet_cell(Module* module, Cell* new_cell, Node* node);
  void Reset();
  void create_all_wires(Lgraph* lg, Module* module);

public:
  LGtoYJson() {}
  void to_json(Eprp_var& var);
};
