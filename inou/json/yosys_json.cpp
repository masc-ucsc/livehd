/*
    Author: Farzaneh Rabiei, GitHub: https://github.com/rabieifk
*/

#include "yosys_json.hpp"

#include <string>
#include <vector>

#include "../../core/lgraph.hpp"
#include "core/edge.hpp"
#include "core/lgraph.hpp"
#include "core/node_pin.hpp"
#include "inou_json.hpp"

namespace yjson {

void Wire::set_const_value(int const_val) {
  for (uint i = 0; i < (uint)bits.size(); i++) {
    bits[i] = (const_val & 1);  // test LSB
    const_val >>= 1;
  }
}

void Wire::ToJson(const JsonComposer* jcm) const {
  JsonElement model[] = {{NULL, &bits}, {}};
  jcm->Write(model);
}

void Cell::ToJson(const JsonComposer* jcm) const {
  auto ports      = prototype->get_all_ports();
  auto jport_dirs = BatchWriter(ports, [](Port* const* port, JsonElement* model, const JsonComposer* json) {
    model[0][*port] = (*port)->dir_str();
    json->Write(model);
  });

  struct PortConn {
    Port* port;
    Wire* wire;
  };
  vector<PortConn> port_conn;
  int              i = 0;
  for (auto p : *ports)  // assocaite port with its connection
    port_conn.push_back({p, connections.at(i++)});

  auto jparams = BatchWriter(&port_conn, [](PortConn const* port_wire, JsonElement*, const JsonComposer* json) {
    auto   port      = port_wire->port;
    auto   port_name = port->get_name().c_str();
    string sign      = "_SIGNED";
    string width     = "_WIDTH";
    sign.insert(0, port_name);
    width.insert(0, port_name);
    auto        wire     = port_wire->wire;
    JsonElement params[] = {{width.c_str(), (long)(wire ? wire->get_width() : 0)}, {sign.c_str(), 1}, {}};
    if (port->get_dir() == pdOutput)
      params[1].type = etEndOfList;  // Do not write "SIGNED" for output
    json->Write(params);
  });

  auto jconn = BatchWriter(&port_conn, [](PortConn const* pw, JsonElement* model, const JsonComposer* json) {
    auto port_name      = pw->port->get_name().c_str();
    model[0][port_name] = pw->wire->get_bits();
    ;
    json->Write(model);
  });

  std::string type_name(Ntype::get_name(prototype->get_type()));

  JsonElement model[] = {{"hide_name", "1"},
                         {"type", type_name.c_str()},
                         {"parameters", &jparams},
                         {"port_directions", &jport_dirs},
                         {"connections", &jconn},
                         {}};
  jcm->Write(model);
}

Port* Module::add_port(const StrTyp& port_name, enPortDir port_dir, Wire* wire_port) {
  ports->push_back(new Port(port_name, port_dir, wire_port));
  return ports->back();
}

Cell* Module::add_cell(const Prototype* proto) {
  cells.push_back(new Cell(proto));
  return cells.back();
}

Wire* Module::get_wire(const std::string& driver_pin_name) { return wires.count(driver_pin_name) ? wires[driver_pin_name] : NULL; }

void Module::create_wires(XEdge_iterator out_edges) {
  for (auto e : out_edges) create_single_wire(e.driver.get_wire_name(), e.driver.get_bits());
}

Wire* Module::create_single_wire(std::string wire_name, int wire_width) {
  if (wires.count(wire_name))
    return wires[wire_name];

  // if this edge does not already have a wire
  auto new_wire    = new Wire(wire_width, last_wire_id);
  wires[wire_name] = new_wire;
  return new_wire;
}

void Module::add_wire_alias(std::string alias, std::string wire_name) { wires[alias] = wires[wire_name]; }

void Module::ToJson(const JsonComposer* jcm) const {
  using NetMapT     = typename std::map<Wire*, std::string>;
  using NetMapItemT = typename std::iterator_traits<NetMapT::const_iterator>::value_type;

  NetMapT unique_nets;       // maps each wire* to a name
  for (auto prt : *ports) {  // map port wires to thier names
    auto port_wire = wires.find(prt->get_name());
    if ((port_wire == wires.end()) || unique_nets.count(port_wire->second))  // if unique_nets contains ...
      continue;
    unique_nets[port_wire->second] = port_wire->first;
  }
  // add non-port wires
  for (auto itr = wires.begin(); itr != wires.end(); ++itr) {
    if (unique_nets.count(itr->second))  // if unique_nets already contains the wire
      continue;
    unique_nets[itr->second] = itr->first;
  }

  auto writeNetNames = BatchWriter{
      &unique_nets,
      [](NetMapItemT const* nam_wire, JsonElement* model, const JsonComposer* json) {
        JsonElement nested_net[] = {{"hide_name", "1"}, {"bits", (nam_wire->first) ? nam_wire->first->get_bits() : 0}, {}};
        model[0][nam_wire->second.c_str()] = nested_net;
        json->Write(model);
      }};

  JsonElement model[] = {{"ports", VectorAsObject{ports}}, {"cells", VectorAsObject{&cells}}, {"netnames", &writeNetNames}, {}};
  jcm->Write(model);
}

}  // namespace yjson
