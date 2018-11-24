//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#include <assert.h>
#include <map>
#include <set>
#include <string>

#include "inou.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct GlobalPin {
  RTLIL::IdString      port;
  const RTLIL::Module *module;
  LGraph *             g;
  bool                 input;
};

struct Local_pin {
  Port_ID out_pid;
  //const RTLIL::Cell   *cell; // always use nid
  Index_ID nid;
};

typedef std::pair<const RTLIL::Wire *, int> Wire_bit;
typedef std::vector<Node_Pin *>             Pins;

//static std::map<const RTLIL::Wire *, GlobalPin> wire2gpin;
static std::map<const RTLIL::Wire *, Local_pin> wire2lpin;
static std::map<const RTLIL::Cell *, Index_ID>  cell2nid;
static std::map<std::string, LGraph *>          module2graph;
static std::map<const RTLIL::Wire *, Pins>      partially_assigned;

static std::map<const Wire_bit, Local_pin> wirebit2lpin;
static std::map<std::string, Index_ID>     const_map;

typedef std::pair<uint32_t, uint32_t> Value_size;
static std::map<Value_size, Index_ID> int_const_map;

#ifndef NDEBUG
static std::map<std::string, uint32_t> used_names;
#endif

static void look_for_module_outputs(RTLIL::Module *module, const std::string &path) {
#ifndef NDEBUG
  log("yosys2lg look_for_module_outputs pass for module %s:\n", module->name.c_str());
#endif
  std::string name = &module->name.c_str()[1];
  auto          *g = module2graph[name];

  for(auto &wire_iter : module->wires_) {
    RTLIL::Wire *wire = wire_iter.second;
    Index_ID     io_idx;
    if(wire->port_input) {
      assert(!wire->port_output); //any bidirectional port?
#ifndef NDEBUG
      log(" adding global input  wire: %s width %d id=%x original_pos=%d\n", wire->name.c_str(), wire->width, wire->hash(), wire->port_id);
#endif
      assert(wire->name.c_str()[0] == '\\');
      io_idx = g->add_graph_input(&wire->name.c_str()[1], 0, wire->width, wire->start_offset, wire->port_id);
      //FIXME: can we get rid of the dependency in the wirename for IOs?
      g->set_node_wirename(io_idx, &wire->name.c_str()[1]);
      g->set_bits(io_idx, wire->width);
      g->node_type_set(io_idx, GraphIO_Op);

#ifndef NDEBUG
      used_names.insert(std::make_pair(&wire->name.c_str()[1], io_idx));
#endif

    } else if(wire->port_output) {
#ifndef NDEBUG
      log(" adding global output wire: %s width %d id=%x\n", wire->name.c_str(), wire->width, wire->hash());
#endif
      assert(wire->name.c_str()[0] == '\\');
      io_idx = g->add_graph_output(&wire->name.c_str()[1], 0, wire->width, wire->start_offset, wire->port_id);
      //FIXME: can we get rid of the dependency in the wirename for IOs?
      g->set_node_wirename(io_idx, &wire->name.c_str()[1]);
      g->set_bits(io_idx, wire->width);
      g->node_type_set(io_idx, GraphIO_Op);

#ifndef NDEBUG
      used_names.insert(std::make_pair(&wire->name.c_str()[1], io_idx));
#endif
    }
  }
}

static bool is_yosys_output(const std::string &idstring) {
  return idstring == "\\Y" || idstring == "\\Q" || idstring == "\\RD_DATA";
}

static Node_Pin get_edge_pin(LGraph *g, const RTLIL::Wire *wire) {

  if(wire->port_input) {
    return Node_Pin(g->get_graph_input(&wire->name.c_str()[1]).get_nid(), 0, false);
  }
  if(wire->port_output) {
    return Node_Pin(g->get_graph_output(&wire->name.c_str()[1]).get_nid(), 0, false);
  }
  if(wire2lpin.find(wire) != wire2lpin.end()) {
    assert(wire->width == g->get_bits_pid(wire2lpin[wire].nid, wire2lpin[wire].out_pid));
    return Node_Pin(wire2lpin[wire].nid, wire2lpin[wire].out_pid, false);
  }

  //no node found for wire
  Index_ID join_nid = g->create_node().get_nid();
  g->node_type_set(join_nid, Join_Op);
  g->set_bits(join_nid, wire->width);

  wire2lpin[wire].out_pid = 0;
  wire2lpin[wire].nid     = join_nid;

  return Node_Pin(join_nid, 0, false);
}

static void connect_constant(LGraph *g, uint32_t value, uint32_t size, Index_ID onid, Port_ID opid) {
  Index_ID const_nid;
  if(int_const_map.find(std::make_pair(value, size)) == int_const_map.end()) {
    const_nid = g->create_node().get_nid();
    g->node_u32type_set(const_nid, value);
    g->set_bits(const_nid, size);
    int_const_map[std::make_pair(value, size)] = const_nid;
  } else {
    const_nid = int_const_map[std::make_pair(value, size)];
  }
  Node_Pin const_pin(const_nid, 0, false);
  g->add_edge(const_pin, Node_Pin(onid, opid, true));
}

class Pick_ID {
public:
  Node_Pin driver;
  int      offset;
  int      width;

  Pick_ID(Node_Pin driver, int offset, int width) : driver(driver), offset(offset), width(width) {
  }

  bool operator<(const Pick_ID other) const {
    return (driver < other.driver) || (driver == other.driver && offset < other.offset) ||
           (driver == other.driver && offset == other.offset && width < other.width);
  }
};
std::map<Pick_ID, Node_Pin> picks;

static Node_Pin create_pick_operator(LGraph *g, const Node_Pin driver, int offset, int width) {
  if(offset == 0 && g->get_bits_pid(driver.get_nid(), driver.get_pid()) == width)
    return driver;

  Pick_ID pick_id(driver, offset, width);
  if(picks.find(pick_id) != picks.end())
    return picks.at(pick_id);

  Index_ID pick_nid = g->create_node().get_nid();
  g->node_type_set(pick_nid, Pick_Op);
  g->set_bits(pick_nid, width);

  g->add_edge(driver, Node_Pin(pick_nid, 0, true));

  connect_constant(g, offset, 32, pick_nid, 1);

  picks.insert(std::make_pair(pick_id, Node_Pin(pick_nid, 0, false)));

  return picks.at(pick_id);
}

static Node_Pin create_pick_operator(LGraph *g, const RTLIL::Wire *wire, int offset, int width) {
  if(wire->width == width && offset == 0)
    return get_edge_pin(g, wire);

  return create_pick_operator(g, get_edge_pin(g, wire), offset, width);
}

const std::regex dc_name("(\\|/)n(\\d\\+)|^N(\\d\\+)");

static void set_bits_wirename(LGraph *g, const Index_ID idx, const RTLIL::Wire *wire) {
  if(!wire)
    return;

  if(!wire->port_input && !wire->port_output) {

#ifndef NDEBUG
    if(g->get_wid(idx) != 0)
      assert(std::string(g->get_node_wirename(idx)) == wire->name.str().substr(1));
#endif
    //we don't want to keep internal abc/yosys wire names
    //FIXME: is there a more efficient and complete way of doing this?
    if(wire->name.c_str()[0] != '$' &&

        wire->name.str().substr(0, 13) != "\\lgraph_cell_" &&
        wire->name.str().substr(0, 19) != "\\lgraph_spare_wire_" &&

        //dc generated names
        !std::regex_match(wire->name.str(), dc_name) &&

        //skip chisel generated names
        wire->name.str().substr(0, 6) != "\\_GEN_" &&
        wire->name.str().substr(0, 4) != "\\_T_") {
#ifndef NDEBUG
      if(g->get_wid(idx) == 0) {
        if(used_names.find(wire->name.str().substr(1)) != used_names.end())
          fmt::print("wirename {} already in used by idx {} (current idx = {})\n", wire->name.str(), used_names[wire->name.str().substr(1)], idx);
        assert(used_names.find(wire->name.str().substr(1)) == used_names.end());
        used_names.insert(std::make_pair(wire->name.str().substr(1), idx));
      }
#endif
      g->set_node_wirename(idx, &wire->name.c_str()[1]);
    }
  }

  if(!g->is_graph_input(idx) && !g->is_graph_output(idx)) {
#ifndef NDEBUG
    if(g->get_bits(idx) != 0 && g->get_bits(idx) != wire->width)
      console->warn("A previous number of bits was assigned to node %ld (yosys wirename %s) and it differs from the number being assigned now\n", idx, &wire->name.c_str()[1]);
#endif
    g->set_bits(idx, wire->width);
  }
}

static void resolve_memory(LGraph *g, RTLIL::Cell *cell) {
  Index_ID nid     = cell2nid[cell];
  uint32_t rdports = cell->parameters["\\RD_PORTS"].as_int();
  uint32_t bits    = cell->parameters["\\WIDTH"].as_int();

  for(uint32_t rdport = 0; rdport < rdports; rdport++) {
    RTLIL::SigSpec ss       = cell->getPort("\\RD_DATA").extract(rdport * bits, bits);
    Index_ID       port_nid = g->get_idx_from_pid(nid, rdport);

    uint32_t offset = 0;
    for(auto &chunk : ss.chunks()) {
      const RTLIL::Wire *wire = chunk.wire;

      //disconnected output
      if(wire == 0)
        continue;

      if(chunk.width == wire->width) {
        assert(wire2lpin.find(wire) == wire2lpin.end());

        if(chunk.width == ss.size()) {
          //output port drives a single wire
          wire2lpin[wire].nid     = nid;
          wire2lpin[wire].out_pid = rdport;
          set_bits_wirename(g, port_nid, wire);
        } else {
          //output port drives multiple wires
          Node_Pin pick_pin       = create_pick_operator(g, Node_Pin(nid, rdport, false), offset, chunk.width);
          wire2lpin[wire].nid     = pick_pin.get_nid();
          wire2lpin[wire].out_pid = pick_pin.get_pid();
          set_bits_wirename(g, pick_pin.get_nid(), wire);
        }
        offset += chunk.width;
      } else {
        if(partially_assigned.find(wire) == partially_assigned.end()) {
          std::vector<Node_Pin *> nodes(wire->width);
          partially_assigned.insert(std::pair<const RTLIL::Wire *, std::vector<Node_Pin *>>(wire, nodes));

          assert(wire2lpin.find(wire) == wire2lpin.end());
          wire2lpin[wire].nid     = g->create_node().get_nid();
          wire2lpin[wire].out_pid = 0;
          g->set_bits(wire2lpin[wire].nid, wire->width);

          g->node_type_set(wire2lpin[wire].nid, Join_Op);
        }
        g->set_bits(port_nid, ss.size());

        Node_Pin *src_pin = new Node_Pin(create_pick_operator(g, Node_Pin(nid, rdport, false), offset, chunk.width));
        offset += chunk.width;
        for(int i = 0; i < chunk.width; i++) {
          partially_assigned[wire][chunk.offset + i] = src_pin;
        }
      }
    }
  }
}

static bool is_black_box_output(const RTLIL::Module *module, const RTLIL::Cell *cell, const RTLIL::IdString &port_name) {
  const RTLIL::Wire *wire = cell->getPort(port_name).chunks()[0].wire;

  //constant
  if(!wire)
    return false;

  //global output
  if(wire->port_output)
    return true;

  //global input
  if(wire->port_input)
    return false;

  if(wire2lpin.find(wire) != wire2lpin.end())
    return false;

  console->error("Could not find a definition for module {}, treating as a blackbox but could not determine whether {} is an input or an output\n", cell->type.str(), port_name.str());
  log_error("unknown port %s at module %s cell %s\n", port_name.c_str(), module->name.c_str(), cell->type.c_str());
  assert(false); // FIXME: is it possible to resolve this case?
  return false;
}

static bool is_black_box_input(const RTLIL::Module *module, const RTLIL::Cell *cell, const RTLIL::IdString &port_name) {
  const RTLIL::Wire *wire = cell->getPort(port_name).chunks()[0].wire;

  //constant
  if(!wire)
    return true;

  //global output
  if(wire->port_output)
    return false;

  //global input
  if(wire->port_input)
    return true;

  console->error("Could not find a definition for module {}, treating as a blackbox but could not determine whether {} is an input or an output\n", cell->type.str(), port_name.str());
  log_error("unknown port %s at module %s cell %s\n", port_name.c_str(), module->name.c_str(), cell->type.c_str());
  assert(false); // FIXME: is it possible to resolve this case?
  return false;
}

static Index_ID resolve_constant(LGraph *g, const std::vector<RTLIL::State> &data, bool forceStr = false) {
  // this is a vector of RTLIL::State
  // S0 => 0
  // S1 => 1
  // Sx => x
  // Sz => high z
  // Sa => don't care (not sure what is the diff between Sa and Sx
  // Sm => used internally by Yosys

  uint32_t    value = 0;
  std::string val;

  uint32_t current_bit = 1;
  bool     u32_const   = true;
  for(auto &b : data) {
    switch(b) {
    case RTLIL::S0:
      val = "0" + val;
      break;
    case RTLIL::S1:
      val = "1" + val;
      value += 1 * current_bit;
      break;
    case RTLIL::Sz:
      val       = "z" + val;
      u32_const = false;
      break;
    case RTLIL::Sa:
      assert(false);
      break; //FIXME add support to Sa when a case is found
    default:
      val       = "x" + val;
      u32_const = false;
      break;
    }
    current_bit = current_bit << 1;
  }
  if(const_map.find(val) != const_map.end())
    return const_map[val];

  Index_ID const_nid = g->create_node().get_nid();
  if(u32_const && data.size() <= 32 && !forceStr) {
    g->node_u32type_set(const_nid, value);
    g->set_bits(const_nid, val.size());
    int_const_map[std::make_pair(value, val.size())] = const_nid;
  } else {
    g->node_const_type_set(const_nid, val);
    g->set_bits(const_nid, val.size());
  }
  const_map[val] = const_nid;

  return const_nid;
}

//does not treat string, keeps as it is (useful for names)
static void connect_string(LGraph *g, const char *value, Index_ID onid, Port_ID opid) {
  if(const_map.find(value) != const_map.end()) {
    Node_Pin const_pin(const_map[value], 0, false);
    g->add_edge(const_pin, Node_Pin(onid, opid, true));
  } else {

    Index_ID const_nid = g->create_node().get_nid();
    g->node_const_type_set(const_nid, std::string(value)
#ifndef NDEBUG
                                          ,
                           false
#endif
    );
    Node_Pin const_pin(const_nid, 0, false);
    g->add_edge(const_pin, Node_Pin(onid, opid, true));
    const_map[value] = const_nid;
  }
}

static void look_for_cell_outputs(RTLIL::Module *module) {

#ifndef NDEBUG
  log("yosys2lg look_for_cell_outputs pass for module %s:\n", module->name.c_str());
#endif

  auto *              g    = module2graph[&module->name.c_str()[1]];
  const Tech_library *tlib = g->get_tlibrary();
  for(auto cell : module->cells()) {

    //pre-allocates nodes for each cell
    Index_ID nid = g->create_node().get_nid();
    cell2nid[cell] = nid;

    LGraph *         sub_graph = nullptr;
    const Tech_cell *tcell     = nullptr;
    bool             blackbox  = false;

    std::string mod_name = &(cell->type.c_str()[1]);

    if(cell->type.c_str()[0] == '\\' || cell->type.str().substr(0, 8) == "$paramod")
      sub_graph = LGraph::open(g->get_path(), mod_name);

    if(!sub_graph && tlib->include(cell->type.str())) {
      tcell = tlib->get_const_cell(tlib->get_cell_id(cell->type.str()));
    }

    if(!sub_graph && !tcell && tlib->include(mod_name)) {
      tcell = tlib->get_const_cell(tlib->get_cell_id(mod_name));
    }

    if(!sub_graph && !tcell && tlib->include(mod_name.substr(1))) {
      tcell = tlib->get_const_cell(tlib->get_cell_id(mod_name.substr(1)));
    }

    blackbox = (!sub_graph) && (!tcell) && (cell->type.c_str()[0] == '\\');

    if(cell->type == "$mem") {
      resolve_memory(g, cell);
      continue;
    }

    int pid = 0;
    if(std::strncmp(cell->type.c_str(), "$reduce_", 8) == 0 && cell->type.str() != "$reduce_xnor") {
      pid = 1;
    }

    uint32_t blackbox_out = 0;
    for(const auto &conn : cell->connections()) {
      //first faster filter but doesn't always work
      if(cell->input(conn.first) || (sub_graph && sub_graph->is_graph_input(&(conn.first.c_str()[1]))))
        continue;

      assert(cell->output(conn.first) || tcell || blackbox ||
          (sub_graph && sub_graph->is_graph_output(&(conn.first.c_str()[1]))));
      if(blackbox) {
        assert(is_black_box_output(module, cell, conn.first));
        connect_string(g, &(conn.first.c_str()[1]), nid, LGRAPH_BBOP_ONAME(blackbox_out++));

      } else if(sub_graph && !sub_graph->is_graph_output(&(conn.first.c_str()[1]))) {
        continue;
      } else if(tcell && !tcell->is_output(&(conn.first.c_str()[1]))) {
        continue;
      } else if(!sub_graph && !tcell && !is_yosys_output(conn.first.c_str())) {
        continue;
      }

      const RTLIL::SigSpec ss = conn.second;

      if(sub_graph) {
        pid = sub_graph->get_graph_output(&(conn.first.c_str()[1])).get_pid();
      } else if(tcell) {
        pid = tcell->get_out_id(&(conn.first.c_str()[1]));
      }
      Index_ID port_nid = g->get_idx_from_pid(nid, pid);

      if(ss.chunks().size() > 1)
        g->set_bits(port_nid, ss.size());

      uint32_t offset = 0;
      for(auto &chunk : ss.chunks()) {
        const RTLIL::Wire *wire = chunk.wire;

        //disconnected output
        if(wire == 0)
          continue;

        if(chunk.width == wire->width) {
          assert(wire2lpin.find(wire) == wire2lpin.end());

          if(chunk.width == ss.size()) {
            //output port drives a single wire
            wire2lpin[wire].nid     = nid;
            wire2lpin[wire].out_pid = pid;
            set_bits_wirename(g, port_nid, wire);
          } else {
            //output port drives multiple wires
            Node_Pin pick_pin       = create_pick_operator(g, Node_Pin(nid, pid, false), offset, chunk.width);
            wire2lpin[wire].nid     = pick_pin.get_nid();
            wire2lpin[wire].out_pid = pick_pin.get_pid();
            set_bits_wirename(g, pick_pin.get_nid(), wire);
          }
          offset += chunk.width;

        } else {
          if(partially_assigned.find(wire) == partially_assigned.end()) {
            std::vector<Node_Pin *> nodes(wire->width);
            partially_assigned.insert(std::pair<const RTLIL::Wire *, std::vector<Node_Pin *>>(wire, nodes));

            assert(wire2lpin.find(wire) == wire2lpin.end());
            wire2lpin[wire].nid     = g->create_node().get_nid();
            wire2lpin[wire].out_pid = 0;
            g->set_bits(wire2lpin[wire].nid, wire->width);

            g->node_type_set(wire2lpin[wire].nid, Join_Op);
          }
          g->set_bits(port_nid, ss.size());

          Node_Pin *src_pin = new Node_Pin(create_pick_operator(g, Node_Pin(nid, pid, false), offset, chunk.width));
          offset += chunk.width;
          for(int i = 0; i < chunk.width; i++) {
            partially_assigned[wire][chunk.offset + i] = src_pin;
          }
        }
      }
    }
  }
}

static Node_Pin create_join_operator(LGraph *g, const RTLIL::SigSpec &ss) {
  std::vector<Node_Pin> inp_pins;
  assert(ss.chunks().size() != 0);

  for(auto &chunk : ss.chunks()) {
    if(chunk.wire == nullptr) {
      Index_ID const_nid = resolve_constant(g, chunk.data);
      inp_pins.push_back(Node_Pin(const_nid, 0, false));

    } else {
      Node_Pin pick_pin = create_pick_operator(g, chunk.wire, chunk.offset, chunk.width);
      inp_pins.push_back(pick_pin);
    }
  }

  Node_Pin src_pin(1, 0, false);
  if(inp_pins.size() > 1) {
    Index_ID join_id = g->create_node().get_nid();
    g->node_type_set(join_id, Join_Op);
    g->set_bits(join_id, ss.size());
    int pid = 0;
    for(auto &inp_pin : inp_pins) {
      g->add_edge(inp_pin, Node_Pin(join_id, pid, true));
      pid++;
    }
    src_pin = Node_Pin(join_id, 0, false);
  } else {
    //single wire, do not create join
    src_pin = inp_pins[0];
  }

  return src_pin;
}

static void process_assigns(RTLIL::Module *module, LGraph* g) {
  for(const auto &conn : module->connections()) {
    const RTLIL::SigSpec lhs = conn.first;
    const RTLIL::SigSpec rhs = conn.second;

    int offset = 0;
    for(auto &chunk : lhs.chunks()) {
      const RTLIL::Wire *lhs_wire = chunk.wire;

      if(lhs_wire->port_input) {
        log_error("Assignment to input port %s\n", lhs_wire->name.c_str());

      } else if(chunk.width == lhs_wire->width) {
        if(chunk.width == 0)
          continue;
        Node_Pin src_pin = create_join_operator(g, rhs.extract(offset, chunk.width));

        offset += chunk.width;
        if(lhs_wire->port_output) {
          Node_Pin output  = g->get_graph_output(&lhs_wire->name.c_str()[1]);
          Node_Pin dst_pin = Node_Pin(output.get_nid(), 0, true);
          g->add_edge(src_pin, dst_pin, lhs_wire->width);

        } else {
          if(wire2lpin.find(lhs_wire) == wire2lpin.end()) {
            wire2lpin[lhs_wire].out_pid = src_pin.get_pid();
            wire2lpin[lhs_wire].nid     = src_pin.get_nid();
          } else {
            g->set_bits(wire2lpin[lhs_wire].nid, g->get_bits_pid(src_pin.get_nid(), src_pin.get_pid()));
            g->add_edge(src_pin, Node_Pin(wire2lpin[lhs_wire].nid, wire2lpin[lhs_wire].out_pid, true));
          }
        }

      } else {
        if(partially_assigned.find(lhs_wire) == partially_assigned.end()) {
          std::vector<Node_Pin *> nodes(lhs_wire->width);
          partially_assigned.insert(std::make_pair(lhs_wire, nodes));

          if(wire2lpin.find(lhs_wire) == wire2lpin.end()) {
            wire2lpin[lhs_wire].nid     = g->create_node().get_nid();
            wire2lpin[lhs_wire].out_pid = 0;
            g->node_type_set(wire2lpin[lhs_wire].nid, Join_Op);
          }

          g->set_bits(wire2lpin[lhs_wire].nid, lhs_wire->width);
        }

        if(chunk.width == 0)
          continue;
        Node_Pin src_pin = create_join_operator(g, rhs.extract(offset, chunk.width));

        offset += chunk.width;
        for(int i = 0; i < chunk.width; i++) {
          partially_assigned[lhs_wire][chunk.offset + i] = new Node_Pin(src_pin);
        }
      }
    }
  }
}

// this function is called for each module in the design
static LGraph *process_module(RTLIL::Module *module) {
#ifndef NDEBUG
  log("yosys2lg pass for module %s:\n", module->name.c_str());
  printf("process_module %s\n", module->name.c_str());
#endif

  std::string name = &module->name.c_str()[1];
  assert(module2graph.find(name) != module2graph.end());
  auto *              g     = module2graph[name];
  const Tech_library *tlib  = g->get_tlibrary();
  const Tech_cell *   tcell = nullptr;

  process_assigns(module, g);

#ifndef NDEBUG
  log("INOU/YOSYS processing module %s, ncells %lu, nwires %lu\n", module->name.c_str(), module->cells().size(), module->wires().size());
#endif

  for(auto cell : module->cells()) {
#ifndef NDEBUG
    log("Looking for cell %s:\n", cell->type.c_str());
#endif

    Index_ID onid, inid;
    assert(cell2nid.find(cell) != cell2nid.end());
    onid                   = cell2nid[cell];
    LGraph *     sub_graph = 0;
    Node_Type_Op op;

    inid = onid;

    bool subtraction           = false,
         negonly               = false,
         yosys_tech            = false;
    uint32_t              size = 0;
    uint32_t              rdports, wrports, abits = 0;
    RTLIL::Wire *         clock  = nullptr;

    // Note that $_AND_ and $_NOT_ format are exclusive for aigmap
    // yosys usually uses cells like $or $not $and
    if(std::strncmp(cell->type.c_str(), "$and", 4) == 0 ||
       std::strncmp(cell->type.c_str(), "$logic_and", 10) == 0 || std::strncmp(cell->type.c_str(), "$reduce_and", 11) == 0) {
      op = And_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$not", 4) == 0) {
      op = Not_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$logic_not", 10) == 0) {
      op = Or_Op;

      inid = g->create_node().get_nid();
      size = cell->parameters["\\Y_WIDTH"].as_int();
      g->set_bits(inid, size);
      g->add_edge(Node_Pin(inid, 1, false), Node_Pin(onid, 0, true));

      g->node_type_set(onid, Not_Op);
      g->set_bits(onid, 1);

    } else if(std::strncmp(cell->type.c_str(), "$or", 4) == 0 || std::strncmp(cell->type.c_str(), "$logic_or", 9) == 0 ||
              std::strncmp(cell->type.c_str(), "$reduce_or", 10) == 0 || std::strncmp(cell->type.c_str(), "$reduce_bool", 12) == 0) {
      op = Or_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$xor", 5) == 0 || std::strncmp(cell->type.c_str(), "$reduce_xor", 11) == 0) {
      op = Xor_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$xnor", 5) == 0 || std::strncmp(cell->type.c_str(), "$reduce_xnor", 11) == 0) {
      op = Xor_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

      inid = g->create_node().get_nid();
      g->set_bits(inid, size);

      g->node_type_set(onid, Not_Op);
      if(std::strncmp(cell->type.c_str(), "$xnor", 5) == 0)
        g->add_edge(Node_Pin(inid, 0, false), Node_Pin(onid, 0, true));
      else
        g->add_edge(Node_Pin(inid, 1, false), Node_Pin(onid, 0, true));

    } else if(std::strncmp(cell->type.c_str(), "$dff", 4) == 0) {
      op = SFlop_Op;
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$adff", 4) == 0) {
      op = AFlop_Op;
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$dlatch", 7) == 0) {
      op = Latch_Op;
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$gt", 3) == 0) {
      op = GreaterThan_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$lt", 3) == 0) {
      op = LessThan_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$ge", 3) == 0) {
      op = GreaterEqualThan_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$le", 3) == 0) {
      op = LessEqualThan_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$mux", 4) == 0) {
      op = Mux_Op;
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$add", 4) == 0) {
      op = Sum_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$mul", 4) == 0) {
      op = Mult_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$div", 4) == 0) {
      op = Div_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$mod", 4) == 0) {
      op = Mod_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$sub", 4) == 0) {
      op = Sum_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      subtraction = true;
    } else if(std::strncmp(cell->type.c_str(), "$neg", 4) == 0) {
      op = Sum_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      negonly = true;
    } else if(std::strncmp(cell->type.c_str(), "$pos", 4) == 0) {
      //FIXME: prevent the genereration of the join and simply connect wires
      op = Join_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$eq", 3) == 0) {
      op = Equals_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$ne", 3) == 0) {
      op = Equals_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      inid   = g->create_node().get_nid();
      g->set_bits(inid, size);

      g->node_type_set(onid, Not_Op);
      g->add_edge(Node_Pin(inid, 0, false), Node_Pin(onid, 0, true), size);

    } else if(std::strncmp(cell->type.c_str(), "$shr", 4) == 0 ||
              (std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && !cell->parameters["\\B_SIGNED"].as_bool())) {
      op = ShiftRight_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && cell->parameters["\\B_SIGNED"].as_bool()) {
      op = ShiftRight_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      connect_constant(g, 2, 1, onid, 2);

    } else if(std::strncmp(cell->type.c_str(), "$sshr", 5) == 0) {
      op = ShiftRight_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

      if(cell->parameters["\\A_SIGNED"].as_bool())
        connect_constant(g, 3, 1, onid, 2);
      else
        connect_constant(g, 1, 1, onid, 2);

    } else if(std::strncmp(cell->type.c_str(), "$shl", 4) == 0 || std::strncmp(cell->type.c_str(), "$sshl", 5) == 0) {
      op = ShiftLeft_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

    } else if(std::strncmp(cell->type.c_str(), "$mem", 4) == 0) {
      op = Memory_Op;

      //int parameters
      uint32_t width  = cell->parameters["\\WIDTH"].as_int();
      uint32_t depth  = cell->parameters["\\SIZE"].as_int();
      uint32_t offset = cell->parameters["\\OFFSET"].as_int();
      abits           = cell->parameters["\\ABITS"].as_int();
      rdports         = cell->parameters["\\RD_PORTS"].as_int();
      wrports         = cell->parameters["\\WR_PORTS"].as_int();

      //string parameters
      RTLIL::Const transp  = cell->parameters["\\RD_TRANSPARENT"];
      RTLIL::Const rd_clkp = cell->parameters["\\RD_CLK_POLARITY"];
      RTLIL::Const rd_clke = cell->parameters["\\RD_CLK_ENABLE"];
      RTLIL::Const wr_clkp = cell->parameters["\\WR_CLK_POLARITY"];
      RTLIL::Const wr_clke = cell->parameters["\\WR_CLK_ENABLE"];

      std::string name = cell->parameters["\\MEMID"].decode_string();

      //we only support a single clock, so polarity needs to be the same
      //FIXME: return this statement when done with google demo
      //assert(rd_clkp == RTLIL::Const(rd_clkp[0], rd_clkp.size()));
      //assert(wr_clkp == RTLIL::Const(wr_clkp[0], wr_clkp.size()));

      //we only support a single clock, so polarity and enable needs to be the same
      //FIXME: return this statement when done with google demo
      //assert(rd_clkp[0] == wr_clkp[0]);

      //lgraph has reversed convention compared to yosys.
      rd_clkp = RTLIL::Const(rd_clkp[0]).as_int() ? RTLIL::Const(0, 1) : RTLIL::Const(1, 1);

      size = width;

      fmt::print("name:{} depth:{} wrports:{} rdports:{}\n", name, depth, wrports, rdports);

      connect_constant(g, depth, 32, onid, LGRAPH_MEMOP_SIZE);
      connect_constant(g, offset, 32, onid, LGRAPH_MEMOP_OFFSET);
      connect_constant(g, abits, 32, onid, LGRAPH_MEMOP_ABITS);
      connect_constant(g, wrports, 32, onid, LGRAPH_MEMOP_WRPORT);
      connect_constant(g, rdports, 32, onid, LGRAPH_MEMOP_RDPORT);

      connect_constant(g, rd_clkp.as_int(), 1, onid, LGRAPH_MEMOP_CLKPOL);
      connect_constant(g, transp.as_int(), 1, onid, LGRAPH_MEMOP_RDTRAN);

      //FIXME: get a test case to patch
      if(cell->parameters.find("\\INIT") != cell->parameters.end())
        assert(cell->parameters["\\INIT"].as_string() == "x");

      clock = cell->getPort("\\RD_CLK")[0].wire;
      if(clock == nullptr) {
        clock = cell->getPort("\\WR_CLK")[0].wire;
      }
      if(clock == nullptr) {
        log_error("No clock found for memory.\n");
      }

#ifndef NDEBUG
      if(g->get_wid(onid) == 0) {
        if(used_names.find(name) != used_names.end())
          fmt::print("wirename {} already in used by idx {} (current idx = {})\n", name, used_names[name], onid);
        assert(used_names.find(name) == used_names.end());
        used_names.insert(std::make_pair(name, onid));
      }
#endif
      g->set_node_wirename(onid, name.c_str());

    } else if(cell->type.c_str()[0] == '$' && cell->type.c_str()[1] != '_' && strncmp(cell->type.c_str(), "$paramod", 8) != 0) {
      log_error("FIXME: add this cell type %s to lgraph\n", cell->type.c_str());
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      op = Invalid_Op;

    } else if((sub_graph = LGraph::open(g->get_path(), &cell->type.c_str()[1]))) {
      // external graph reference
      const char *mod_name = &cell->type.c_str()[1];
      log("module name %s original was  %s\n", mod_name, cell->type.c_str());
      op = SubGraph_Op;

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "")
        g->set_node_instance_name(inid, inst_name.c_str());
#ifndef NDEBUG
      else
        fmt::print("yosys2lg got empty inst_name for cell type {}\n", mod_name);
#endif

    } else if(tlib->include(cell->type.str())) {
      std::string ttype = cell->type.str();
      op                = TechMap_Op;
      tcell             = tlib->get_const_cell(tlib->get_cell_id(ttype));

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "" && inst_name.substr(0, 5) != "auto$" &&
         inst_name.substr(0, 4) != "abc$")
        g->set_node_instance_name(inid, inst_name.c_str());

    } else if(tlib->include(cell->type.str().substr(1))) {
      std::string ttype = cell->type.str().substr(1);
      op                = TechMap_Op;
      tcell             = tlib->get_const_cell(tlib->get_cell_id(ttype));

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "" && inst_name.substr(0, 5) != "auto$" &&
         inst_name.substr(0, 4) != "abc$")
        g->set_node_instance_name(inid, inst_name.c_str());

    } else if(tlib->include(cell->type.str().substr(2))) {
      std::string ttype = cell->type.str().substr(2);
      op                = TechMap_Op;
      tcell             = tlib->get_const_cell(tlib->get_cell_id(ttype));

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "" && inst_name.substr(0, 5) != "auto$" &&
         inst_name.substr(0, 4) != "abc$")
        g->set_node_instance_name(inid, inst_name.c_str());

      // DO NOT MERGE THE BELLOW WITH THE OTHER ANDs, NOTs, DFFs
    } else if(std::strncmp(cell->type.c_str(), "$_AND_", 6) == 0) {
      op = And_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

    } else if(std::strncmp(cell->type.c_str(), "$_NOT_", 6) == 0) {
      op = Not_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

    } else if(std::strncmp(cell->type.c_str(), "$_DFF_P_", 8) == 0) {
      op = SFlop_Op;
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$_DFF_N_", 8) == 0) {
      op = SFlop_Op;
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
      connect_constant(g, 0, 1, onid, 5);

    } else if(std::strncmp(cell->type.c_str(), "$_DFF_NN", 8) == 0 ||
              std::strncmp(cell->type.c_str(), "$_DFF_NP", 8) == 0 ||
              std::strncmp(cell->type.c_str(), "$_DFF_PP", 8) == 0 ||
              std::strncmp(cell->type.c_str(), "$_DFF_PN", 8) == 0) {

      //FIXME: add support for those DFF types
      log_error("Found complex yosys DFFs, run `techmap -map +/adff2dff.v` before calling the yosys2lg pass\n");
      assert(false);

    } else {
      //blackbox addition
#ifndef NDEBUG
      console->info("Black box addition from yosys frontend, cell type {} not found\n", cell->type.c_str());
#endif

      op = BlackBox_Op;
      connect_string(g, &(cell->type.c_str()[1]), onid, 0);
      connect_string(g, &(cell->name.c_str()[1]), onid, 1);
    }

    if(op == SubGraph_Op) {
      g->node_subgraph_set(inid, sub_graph->lg_id());
    } else if(op == TechMap_Op) {
      g->node_tmap_set(inid, tcell->get_id());
    } else {
      g->node_type_set(inid, op);
    }

    uint32_t blackbox_port = 0;

    std::set<std::pair<Node_Pin, Node_Pin>> added_edges;
    for(auto &conn : cell->connections()) {
      RTLIL::SigSpec ss = conn.second;
      if(ss.size() == 0)
        continue;

      Port_ID dst_pid = 0;
      // Go over cells with multiple inputs that map to something different than A
      if(op == SubGraph_Op) {
        const char *name = &conn.first.c_str()[1];
        if(sub_graph->is_graph_output(name))
          continue;
        dst_pid = sub_graph->get_graph_input(name).get_pid();
      } else if(op == TechMap_Op) {
        const char *name = &conn.first.c_str()[1];
        if(tcell->is_output(name))
          continue;
        dst_pid = tcell->get_inp_id(name);

      } else if(op == BlackBox_Op && !yosys_tech) {
        if(is_black_box_output(module, cell, conn.first)) {
          continue;
        } else if(is_black_box_input(module, cell, conn.first)) {
          connect_constant(g, 0, 1, onid, LGRAPH_BBOP_PARAM(blackbox_port));
          connect_string(g, &(conn.first.c_str()[1]), onid, LGRAPH_BBOP_PNAME(blackbox_port));
          dst_pid = LGRAPH_BBOP_CONNECT(blackbox_port);
          blackbox_port++;
        } else {
          assert(false); // not able to distinguish if blackbox input or output
        }
      } else {
        if(is_yosys_output(conn.first.c_str()))
          continue; // Just go over the inputs

        if(op == SFlop_Op || op == Mux_Op || op == ShiftRight_Op || op == ShiftLeft_Op) {
          dst_pid = Node_Type::get(op).get_input_match(&conn.first.c_str()[1]);
        } else if(op == AFlop_Op) {
          if(conn.first.str() == "\\ARST")
            dst_pid = 3;
          else
            dst_pid = Node_Type::get(op).get_input_match(&conn.first.c_str()[1]);
        } else if(op == Sum_Op) {
          dst_pid = 0;
          if(cell->parameters[conn.first.str() + "_SIGNED"].as_int() == 0)
            dst_pid += 1;
          if(negonly || (subtraction && conn.first.c_str()[1] == 'B'))
            dst_pid += 2;
        } else if(op == Mult_Op) {
          dst_pid = 0;
          if(cell->parameters[conn.first.str() + "_SIGNED"].as_int() == 0)
            dst_pid += 1;
        } else if(op == GreaterThan_Op || op == LessThan_Op || op == GreaterEqualThan_Op || op == LessEqualThan_Op || op == Div_Op ||
                  op == Mod_Op) {
          dst_pid = 0;
          if(cell->parameters[conn.first.str() + "_SIGNED"].as_int() == 0)
            dst_pid += 1;
          if(conn.first.c_str()[1] == 'B')
            dst_pid += 2;
        } else if(op == Memory_Op) {
          if(conn.first.str() == "\\WR_CLK") {
            for(auto &clk_chunk : conn.second.chunks()) {
              assert(clk_chunk.wire == clock);
            }
            dst_pid = LGRAPH_MEMOP_CLK;
            ss      = RTLIL::SigSpec(clock);
          } else if(conn.first.str() == "\\WR_ADDR") {
            for(uint32_t wrport = 0; wrport < wrports; wrport++) {
              Node_Pin dst_pin(inid, LGRAPH_MEMOP_WRADDR(wrport), true);
              Node_Pin src_pin = create_join_operator(g, ss.extract(wrport * abits, abits));
              g->add_edge(src_pin, dst_pin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\WR_DATA", 8) == 0) {
            for(uint32_t wrport = 0; wrport < wrports; wrport++) {
              Node_Pin dst_pin(inid, LGRAPH_MEMOP_WRDATA(wrport), true);
              Node_Pin src_pin = create_join_operator(g, ss.extract(wrport * size, size));
              g->add_edge(src_pin, dst_pin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\WR_EN", 6) == 0) {
            for(uint32_t wrport = 0; wrport < wrports; wrport++) {
              Node_Pin dst_pin(inid, LGRAPH_MEMOP_WREN(wrport), true);
              Node_Pin src_pin = create_join_operator(g, ss.extract(wrport, 1));
              g->add_edge(src_pin, dst_pin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\RD_CLK", 7) == 0) {
            for(auto &clk_chunk : conn.second.chunks()) {
              assert(clk_chunk.wire == clock || !clk_chunk.wire);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\RD_ADDR", 8) == 0) {
            for(uint32_t rdport = 0; rdport < rdports; rdport++) {
              Node_Pin dst_pin(inid, LGRAPH_MEMOP_RDADDR(rdport), true);
              Node_Pin src_pin = create_join_operator(g, ss.extract(rdport * abits, abits));
              g->add_edge(src_pin, dst_pin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\RD_EN", 6) == 0) {
            for(uint32_t rdport = 0; rdport < rdports; rdport++) {
              Node_Pin dst_pin(inid, LGRAPH_MEMOP_RDEN(rdport), true);
              Node_Pin src_pin = create_join_operator(g, ss.extract(rdport, 1));
              g->add_edge(src_pin, dst_pin);
            }
            continue;
          } else {
            assert(false); //port not found
          }
        }
      }

      Node_Pin dst_pin(inid, dst_pid, true);
      if(ss.size() > 0) {
        Node_Pin src_pin = create_join_operator(g, ss);
        if(added_edges.find(std::make_pair(src_pin, dst_pin)) != added_edges.end()) {
          //there are two edges from src_pin to dst_pin
          //this is not allowed in lgraph, add a join in between
          Index_ID new_join = g->create_node().get_nid();
          g->node_type_set(new_join, Join_Op);
          g->set_bits(new_join, ss.size());
          g->add_edge(src_pin, Node_Pin(new_join, 0, true));
          src_pin = Node_Pin(new_join, 0, false);
        }
        g->add_edge(src_pin, dst_pin);
        added_edges.insert(std::make_pair(src_pin, dst_pin));
      }
    }
  }

  for(auto &kv : partially_assigned) {
    const RTLIL::Wire *wire = kv.first;

    int join_nid = wire2lpin[wire].nid;
    int join_pid = 0;

    set_bits_wirename(g, join_nid, wire);

    Node_Pin *current = nullptr;
    int       size    = 0;
    bool      first   = true;
    Node_Pin *src_pin;
    for(auto *pin : kv.second) {
      if(first)
        first = false;
      else if((current == nullptr && pin != nullptr) || (current != nullptr && pin == nullptr) || (current != pin && *current != *pin)) {
        src_pin = current;
        if(current == nullptr) {
          Index_ID nid = g->create_node().get_nid();
          g->node_const_type_set(nid, std::string(size, 'x'));
          g->set_bits(nid, size);
          src_pin = new Node_Pin(nid, 0, false);
        }

        Index_ID port_nid = g->get_idx_from_pid(src_pin->get_nid(), src_pin->get_pid());
        assert(size < g->get_bits(port_nid) || size % g->get_bits(port_nid) == 0);
        int times = size < g->get_bits(port_nid) ? 1 : size / g->get_bits(port_nid);
        for(int i = 0; i < times; i++) {
          g->add_edge(*src_pin, Node_Pin(join_nid, join_pid++, true));
        }
        size = 0;
      }
      size++;
      current = pin;
    }
    src_pin = current;
    if(current == nullptr) {
      Index_ID nid = g->create_node().get_nid();
      g->node_const_type_set(nid, std::string(size, 'x'));
      g->set_bits(nid, size);
      src_pin = new Node_Pin(nid, 0, false);
    }

    Index_ID port_nid = g->get_idx_from_pid(src_pin->get_nid(), src_pin->get_pid());
    assert(size < g->get_bits(port_nid) || size % g->get_bits(port_nid) == 0);
    int times = size < g->get_bits(port_nid) ? 1 : size / g->get_bits(port_nid);
    for(int i = 0; i < times; i++) {
      g->add_edge(*src_pin, Node_Pin(join_nid, join_pid++, true));
    }
  }

  //we need to connect global outputs to the cell that drives it
  for(auto wire : module->wires()) {
    if(wire->port_output && wire2lpin.find(wire) != wire2lpin.end()) {

      Node_Pin output  = g->get_graph_output(&wire->name.c_str()[1]);
      Node_Pin dst_pin = Node_Pin(output.get_nid(), 0, true);
      Node_Pin src_pin = Node_Pin(wire2lpin[wire].nid, wire2lpin[wire].out_pid, false);
#ifndef NDEBUG
      log("  connecting module output %s %d %ld\n", wire->name.c_str(), src_pin.get_pid(), src_pin.get_nid());
#endif
      g->add_edge(src_pin, dst_pin, wire->width);
    }
  }

  return g;
}

// each pass contains a singleton object that is derived from Pass
struct Yosys2lg_Pass : public Pass {
  Yosys2lg_Pass() : Pass("yosys2lg") {}
  virtual void help() {
    //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
    log("\n");
    log("    yosys2lg [options]\n");
    log("\n");
    log("Write multiple lgraph from the selected modules.\n");
    log("\n");
    log("    -path [default=lgdb]\n");
    log("        Specify from which path to read\n");
    log("\n");
    log("\n");
  }

  virtual void execute(std::vector<std::string> args, RTLIL::Design *design) {
    // variables to mirror information from passed options
    log_header(design, "Executing yosys2lg pass (convert from yosys to lgraph).\n");

    // parse options
    size_t      argidx;
    std::string path = "lgdb";

    for(argidx = 1; argidx < args.size(); argidx++) {
      if(args[argidx] == "-path") {
        path = args[++argidx];
        continue;
      }
      break;
    }

    // handle extra options (e.g. selection)
    extra_args(args, argidx, design);

    module2graph.clear();

    for(auto &it : design->modules_) {
      RTLIL::Module *module = it.second;
      std::string    name   = &module->name.c_str()[1];
      assert(module2graph.find(name) == module2graph.end());

      auto *g            = LGraph::create(path, name);
      module2graph[name] = g;
      log("yosys2lg look_for_module_outputs pass for module %s:\n", module->name.c_str());
      look_for_module_outputs(module, path);
    }

    for(auto &it : design->modules_) {
#ifndef NDEBUG
      used_names.clear();
#endif
      RTLIL::Module *module = it.second;
      log("yosys2lg NOT look_for_cell_outputs pass for module %s:\n", module->name.c_str());
      if(design->selected_module(it.first)) {
#ifndef NDEBUG
        log("yosys2lg look_for_cell_outputs pass for module %s:\n", module->name.c_str());
        console->info("now processing module {}\n", module->name.str());
#endif
        look_for_cell_outputs(module);
        LGraph *g = process_module(module);

        g->sync();
        g->close();
      }

      wire2lpin.clear();
      cell2nid.clear();
      wirebit2lpin.clear();
      partially_assigned.clear();
      const_map.clear();
      int_const_map.clear();
      picks.clear();
    }
  }
} Yosys2lg_Pass;

PRIVATE_NAMESPACE_END
