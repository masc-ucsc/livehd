#ifndef YOSYS_JSON_H
#define YOSYS_JSON_H

#include <vector>
#include <string>
#include "json_composer.hpp"
#include "mmap_gc.hpp"
#include "core/edge.hpp"
#include "cell.hpp"
#include "../../core/lgraph.hpp"
#include "../../core/node_pin.hpp"
#include "inou_json.hpp"
#include "core/edge.hpp"

using namespace std;
using namespace jsn;

namespace yjson {

using StrTyp = mmap_lib::str;

class Wire : public jsn::Object {
  vector<int> bits;
  
  public:

    Wire(int width, long& starting_wire_id) { 
      for (int i = 0; i < width ; i++) {
        bits.push_back(starting_wire_id++);
      }
    }

    const vector<int>* get_bits() const { return &bits;}
    void set_const_value(int const_val);
    size_t get_width() const { return bits.size(); }
    void ToJson(const JsonComposer* jcm) const;

};

enum enPortDir {
  pdInput,
  pdOutput
};

class Port : public jsn::Object {
  string name;
  enPortDir direction;
  Wire* wire;  // used in modules only (internal connection of the port)

public:
  Port (const char* port_name, enPortDir dir)   { name = port_name; direction = dir; }
  Port (const StrTyp* port_name, enPortDir dir, Wire* port_wire) { name = port_name->to_s(); direction = dir; wire = port_wire; }

  const char* JsonKey() const { return name.c_str(); }  
  const std::string& get_name() const { return name; }
  const Wire* get_wire() const { return wire; }
  enPortDir get_dir() const {return direction; }
  const char* dir_str() const { return ((int)direction== 0)? "input": "output"; }  
  void ToJson(const JsonComposer* jcm) const {
      JsonElement model[] = {
          {"direction",    dir_str() }, 
          {"bits", wire? wire->get_bits() : 0 },
          {}
      };
      jcm->Write(model);
  }
};

class Prototype {
  protected:
    Ntype_op type;
    vector<Port*>* ports;

  public:
 
    using PortFormatter = void (*)(const Port*);
    Prototype(Ntype_op typ, vector<Port*>* ports_list)   { type=typ; ports = ports_list; }

    Ntype_op get_type() const { return type;}
    const Port* get_port(int port_index) const { return ports->at(port_index); } 
    const vector<Port*>* get_all_ports() const {return ports;}
};

class Cell : public jsn::Object{ 
  private:
    std::string name;
    const Prototype* prototype;
    vector<Wire*> connections;
    union CellInfo{
      bool is_subtract;
    } flags;

  public:

    Cell(const Prototype* proto) { prototype = proto; }

    const char* JsonKey() const {return get_name(); }  
    CellInfo& Info() { return flags; }
    const char* get_name() const { return name.c_str(); }
    const Prototype* get_proto() const { return prototype; }
    Wire* get_connection(int conn_index) const { return connections[conn_index]; }
    void add_connection(Wire* wire) { connections.push_back(wire); }
    void set_name(std::string cell_name) { name = cell_name; }
    void ToJson(const JsonComposer* jcm) const;
};

class Module : public jsn::Object, Prototype {
  private:
  string name;
  vector<Cell*>  cells;
  std::map<std::string, Wire*>  wires;
  long last_wire_id;

  public:
  
  Module(string module_name) : Prototype(Ntype_op::Sub, NULL) // type = sub_module
  {
    last_wire_id = 2;
    name = module_name;
    ports = new vector<Port*>;
  }

  const char* JsonKey() const {return get_name(); }  
  const char* get_name() const { return name.c_str(); }
  Wire* get_wire(const std::string& pin_name);
  Cell* add_cell(const Prototype* proto);
  Port* add_port(const StrTyp *port_name, enPortDir port_dir, Wire* port_wire);
  void add_wire_alias(std::string mask_driver, std::string prev_node_diver);
  void create_wires(XEdge_iterator out_edges);
  Wire* create_single_wire(std::string wire_name, int wire_width);
  void ToJson(const JsonComposer* jcm) const;

};
} // end of namespace

#endif
