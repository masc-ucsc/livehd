//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

#include "kernel/sigtools.h"
#include "kernel/yosys.h"
#include "kernel/celltypes.h"

#include <assert.h>
#include <map>
#include <set>
#include <string>

#include "inou.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct GlobalPin {
  RTLIL::IdString      port;
  const RTLIL::Module *module;
  LGraph *             g;
  bool                 input;
};

static CellTypes ct_all;
static absl::flat_hash_set<std::string> cell_port_inputs;
static absl::flat_hash_set<std::string> cell_port_outputs;

// TODO: Replace Local_pin for Node_pin (SAME)
struct Local_pin {
  Port_ID  out_pid;
  Index_ID idx;
};

typedef std::pair<const RTLIL::Wire *, int> Wire_bit;
typedef std::vector<Node_pin *>             Pins; // TODO: replace Node_pin * for Node_pin

// static std::map<const RTLIL::Wire *, GlobalPin> wire2gpin;
static absl::flat_hash_map<const RTLIL::Wire *, Local_pin> wire2lpin;
static absl::flat_hash_map<const RTLIL::Cell *, Index_ID>  cell2nid;
static absl::flat_hash_map<std::string, LGraph *>          module2graph;
static absl::flat_hash_map<const RTLIL::Wire *, Pins>      partially_assigned;

static std::map<const Wire_bit, Local_pin> wirebit2lpin;

typedef std::pair<uint32_t, uint32_t> Value_size;

#ifndef NDEBUG
static absl::flat_hash_map<std::string, uint32_t> used_names;
#endif

static void look_for_module_outputs(RTLIL::Module *module, const std::string &path) {
#ifndef NDEBUG
  log("yosys2lg look_for_module_outputs pass for module %s:\n", module->name.c_str());
#endif
  std::string name = &module->name.c_str()[1];
  auto *      g    = module2graph[name];

  int last_input_port_id  = 0;
  int last_output_port_id = 0;
  for(auto &wire_iter : module->wires_) {
    RTLIL::Wire *wire = wire_iter.second;
    Index_ID     io_idx;
    if(wire->port_input) {
      assert(!wire->port_output); // any bidirectional port?
      //log(" adding global input  wire: %s width %d id=%x original_pos=%d\n", wire->name.c_str(), wire->width, wire->hash(), wire->port_id);
      assert(wire->name.c_str()[0] == '\\');
      assert(last_input_port_id <= wire->port_id);
      last_input_port_id = wire->port_id;
      auto io_pin = g->add_graph_input(&wire->name.c_str()[1], wire->width, wire->start_offset);

#ifndef NDEBUG
      used_names.insert(std::make_pair(&wire->name.c_str()[1], io_pin.get_idx()));
#endif

    } else if(wire->port_output) {
      //log(" adding global output wire: %s width %d id=%x\n", wire->name.c_str(), wire->width, wire->hash());
      assert(wire->name.c_str()[0] == '\\');
      assert(last_output_port_id <= wire->port_id);
      last_output_port_id = wire->port_id;
      auto io_pin = g->add_graph_output(&wire->name.c_str()[1], wire->width, wire->start_offset);
#ifndef NDEBUG
      used_names.insert(std::make_pair(&wire->name.c_str()[1], io_pin.get_idx()));
#endif
    }
  }

  if (module2graph.size()>10) // More than 10 different subgraphs
    g->sync(); // Not needed, but to free-up mmaps...
}

static bool is_yosys_output(const std::string &idstring) {
  return idstring == "\\Y" || idstring == "\\Q" || idstring == "\\RD_DATA";
}

static Node_pin get_edge_pin(LGraph *g, const RTLIL::Wire *wire) {

  if(wire->port_input) {
    return g->get_graph_input(&wire->name.c_str()[1]);
  }
  if(wire->port_output) {
    return g->get_graph_output_driver(&wire->name.c_str()[1]);
  }
  if(wire2lpin.find(wire) != wire2lpin.end()) {
    if(wire->width != g->get_bits_pid(wire2lpin[wire].idx, wire2lpin[wire].out_pid)) {
      fmt::print("w_name:{} w_w:{} | idx:{} pid:{} bits:{}\n"
          ,wire->name.c_str()
          ,wire->width
          ,wire2lpin[wire].idx
          ,wire2lpin[wire].out_pid
          ,g->get_bits_pid(wire2lpin[wire].idx, wire2lpin[wire].out_pid));
    }
    assert(wire->width == g->get_bits_pid(wire2lpin[wire].idx, wire2lpin[wire].out_pid));
    return g->get_node(wire2lpin[wire].idx).get_driver_pin(wire2lpin[wire].out_pid);
  }

  auto node = g->create_node(Join_Op, wire->width);
  auto pin0 = node.setup_driver_pin();

  wire2lpin[wire].out_pid = pin0.get_pid();
  wire2lpin[wire].idx     = pin0.get_idx();

  return pin0;
}

static void connect_constant(LGraph *g, uint32_t value, uint32_t size, Index_ID onid, Port_ID opid) {

  auto dpin = g->create_node_u32(value,size).setup_driver_pin();
  auto spin = g->get_node(onid).setup_sink_pin(opid);

  g->add_edge(dpin, spin);
}

class Pick_ID {
public:
  Node_pin driver;
  int      offset;
  int      width;

  Pick_ID(Node_pin driver, int offset, int width)
      : driver(driver)
      , offset(offset)
      , width(width) {
  }

  bool operator<(const Pick_ID other) const {
    return (driver < other.driver) || (driver == other.driver && offset < other.offset) ||
           (driver == other.driver && offset == other.offset && width < other.width);
  }
};
std::map<Pick_ID, Node_pin> picks;

static Node_pin create_pick_operator(LGraph *g, const Node_pin driver, int offset, int width) {
  if(offset == 0 && g->get_bits(driver) == width)
    return driver;

  Pick_ID pick_id(driver, offset, width);
  if(picks.find(pick_id) != picks.end())
    return picks.at(pick_id);

  auto node = g->create_node(Pick_Op, width);
  auto driver_pin0 = node.setup_driver_pin();
  auto sink_pin0   = node.setup_sink_pin(0);

  g->add_edge(driver, sink_pin0);

  connect_constant(g, offset, 32, node.get_nid(), node.setup_sink_pin(1).get_pid());

  picks.insert(std::make_pair(pick_id, driver_pin0));

  return picks.at(pick_id);
}

static Node_pin create_pick_operator(LGraph *g, const RTLIL::Wire *wire, int offset, int width) {
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
    if(g->get_wid(idx) != 0) {
      fmt::print("wirename:{} wire:{}\n",g->get_node_wirename(idx), wire->name.str());
      assert(std::string(g->get_node_wirename(idx)) == wire->name.str().substr(1));
    }
#endif
    // we don't want to keep internal abc/yosys wire names
    // TODO: is there a more efficient and complete way of doing this?
    if(wire->name.c_str()[0] != '$' &&

       wire->name.str().substr(0, 13) != "\\lgraph_cell_" && wire->name.str().substr(0, 19) != "\\lgraph_spare_wire_" &&

       // dc generated names
       !std::regex_match(wire->name.str(), dc_name) &&

       // skip chisel generated names
       wire->name.str().substr(0, 6) != "\\_GEN_" && wire->name.str().substr(0, 4) != "\\_T_") {
#ifndef NDEBUG
      if(g->get_wid(idx) == 0) {
        if(used_names.find(wire->name.str().substr(1)) != used_names.end())
          fmt::print("wirename {} already in used by idx {} (current idx = {})\n", wire->name.str(),
                     used_names[wire->name.str().substr(1)], idx);
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
      ::Pass::warn("A previous number of bits was assigned to node %ld (yosys wirename %s) and it differs from the number being "
                    "assigned now",
                    idx, &wire->name.c_str()[1]);
#endif
    g->set_bits(idx, wire->width);
  }
}

static Index_ID resolve_memory(LGraph *g, RTLIL::Cell *cell) {

  auto node = g->create_node(Memory_Op);

  uint32_t rdports = cell->parameters["\\RD_PORTS"].as_int();
  uint32_t bits    = cell->parameters["\\WIDTH"].as_int();

  for(uint32_t rdport = 0; rdport < rdports; rdport++) {
    RTLIL::SigSpec ss       = cell->getPort("\\RD_DATA").extract(rdport * bits, bits);
    auto           dpin     = node.setup_driver_pin(rdport);

    uint32_t offset = 0;
    for(auto &chunk : ss.chunks()) {
      const RTLIL::Wire *wire = chunk.wire;

      // disconnected output
      if(wire == 0)
        continue;

      if(chunk.width == wire->width) {
        assert(wire2lpin.find(wire) == wire2lpin.end());

        if(chunk.width == ss.size()) {
          // output port drives a single wire
          wire2lpin[wire].idx     = dpin.get_idx();
          wire2lpin[wire].out_pid = dpin.get_pid();
          set_bits_wirename(g, dpin.get_idx(), wire);
        } else {
          // output port drives multiple wires
          Node_pin pick_pin       = create_pick_operator(g, dpin, offset, chunk.width);
          wire2lpin[wire].idx     = pick_pin.get_idx();
          wire2lpin[wire].out_pid = pick_pin.get_pid();
          set_bits_wirename(g, pick_pin.get_idx(), wire);
        }
        offset += chunk.width;
      } else {
        if(partially_assigned.find(wire) == partially_assigned.end()) {
          std::vector<Node_pin *> nodes(wire->width);
          partially_assigned.insert(std::pair<const RTLIL::Wire *, std::vector<Node_pin *>>(wire, nodes));

          assert(wire2lpin.find(wire) == wire2lpin.end());
          auto node = g->create_node(Join_Op, wire->width);

          wire2lpin[wire].idx     = node.get_nid();
          wire2lpin[wire].out_pid = 0;
        }
        g->set_bits(dpin, ss.size());

        Node_pin *src_pin = new Node_pin(create_pick_operator(g, dpin, offset, chunk.width));
        offset += chunk.width;
        for(int i = 0; i < chunk.width; i++) {
          partially_assigned[wire][chunk.offset + i] = src_pin;
        }
      }
    }
  }

  return node.get_nid();
}

static bool is_black_box_output(const RTLIL::Module *module, const RTLIL::Cell *cell, const RTLIL::IdString &port_name) {
  const RTLIL::Wire *wire  = cell->getPort(port_name).chunks()[0].wire;

  // constant
  if(!wire)
    return false;

  // global output
  if(wire->port_output)
    return true;

  // global input
  if(wire->port_input)
    return false;

  //if(wire2lpin.find(wire) != wire2lpin.end())
  //  return false;

  std::string cell_port = absl::StrCat(cell->type.str(), "_:_", port_name.str());

  if (cell_port_outputs.find(cell_port) != cell_port_outputs.end()) {
    //std::cout << "1.output " << cell_port << std::endl;
    return true;
  }
  if (cell_port_inputs.find(cell_port) != cell_port_inputs.end()) {
    //std::cout << "1.input " << cell_port << std::endl;
    return false;
  }

  ::Pass::error("Could not find a definition for module {}, treating as a blackbox but could not determine whether {} is an output",
                 cell->type.str(), port_name.str());

  log_error("unknown port %s at module %s cell %s\n", port_name.c_str(), module->name.c_str(), cell->type.c_str());
  assert(false); // TODO: is it possible to resolve this case?
  return false;
}

static bool is_black_box_input(const RTLIL::Module *module, const RTLIL::Cell *cell, const RTLIL::IdString &port_name) {
  const RTLIL::Wire *wire = cell->getPort(port_name).chunks()[0].wire;

  // constant
  if(!wire)
    return true;

  // global output
  if(wire->port_output)
    return false;

  // global input
  if(wire->port_input)
    return true;

  std::string cell_port = absl::StrCat(cell->type.str(), "_:_", port_name.str());

  // Opposite of is_black_box_output
  if (cell_port_outputs.find(cell_port) != cell_port_outputs.end()) {
    //std::cout << "2.output " << cell_port << std::endl;
    return false;
  }
  if (cell_port_inputs.find(cell_port) != cell_port_inputs.end()) {
    //std::cout << "2.input " << cell_port << std::endl;
    return true;
  }

  ::Pass::error("Could not find a definition for module {}, treating as a blackbox but could not determine whether {} is an input",
                 cell->type.str(), port_name.str());

  log_error("unknown port %s at module %s cell %s\n", port_name.c_str(), module->name.c_str(), cell->type.c_str());
  assert(false); // TODO: is it possible to resolve this case?
  return false;
}

static Node_pin resolve_constant(LGraph *g, const std::vector<RTLIL::State> &data) {
  // this is a vector of RTLIL::State
  // S0 => 0
  // S1 => 1
  // Sx => x
  // Sz => high z
  // Sa => don't care (not sure what is the diff between Sa and Sx
  // Sm => used internally by Yosys

  uint32_t    value = 0;
  std::string val;

  uint32_t current_bit = 0;
  bool     u32_const   = true;
  for(auto &b : data) {
    switch(b) {
    case RTLIL::S0:
      val = absl::StrCat("0",val);
      break;
    case RTLIL::S1:
      val = absl::StrCat("1",val);
      value += 1<<current_bit;
      break;
    case RTLIL::Sz:
    case RTLIL::Sa:
      val       = absl::StrCat("z",val);
      u32_const = false;
      break;
    case RTLIL::Sx:
      val       = absl::StrCat("x",val);
      u32_const = false;
      break;
    default:
      assert(false);
    }
    current_bit++;
  }

  if(u32_const && data.size() <= 32) {
    auto node = g->create_node_u32(value,val.size());
    return node.setup_driver_pin();
  }

  auto node = g->create_node_const(val,val.size());
  return node.setup_driver_pin();
}

// does not treat string, keeps as it is (useful for names)
static void connect_string(LGraph *g, std::string_view value, Index_ID onid, Port_ID opid) {

  auto spin = g->get_node(onid).setup_sink_pin(opid);
  auto dpin = g->create_node_const(value).setup_driver_pin();

  g->add_edge(dpin, spin);
}

static void look_for_cell_outputs(RTLIL::Module *module) {

  //log("yosys2lg look_for_cell_outputs pass for module %s:\n", module->name.c_str());

  auto *              g    = module2graph[&module->name.c_str()[1]];
  const Tech_library &tlib = g->get_tlibrary();
  for(auto cell : module->cells()) {

    LGraph *         sub_graph = nullptr;
    const Tech_cell *tcell     = nullptr;

    std::string_view mod_name(&(cell->type.c_str()[1]));

    if(cell->type.c_str()[0] == '\\' || cell->type.str().substr(0, 8) == "$paramod")
      sub_graph = LGraph::open(g->get_path(), mod_name);

    if(!sub_graph && tlib.include(cell->type.str())) {
      tcell = tlib.get_const_cell(tlib.get_cell_id(cell->type.str()));
    }

    if(!sub_graph && !tcell && tlib.include(mod_name)) {
      tcell = tlib.get_const_cell(tlib.get_cell_id(mod_name));
    }

    if(!sub_graph && !tcell && tlib.include(mod_name.substr(1))) {
      tcell = tlib.get_const_cell(tlib.get_cell_id(mod_name.substr(1)));
    }

    bool blackbox = false;
    if ((!sub_graph) && (!tcell)) {
      if (cell->type.c_str()[0] == '\\')
        blackbox = true;
      else if (!ct_all.cell_known(cell->type))
        blackbox = true;
    }

    if(cell->type == "$mem") {
      cell2nid[cell] = resolve_memory(g, cell);
      continue;
    }

    Index_ID nid   = g->create_node().get_nid();
    cell2nid[cell] = nid;

    int pid = 0;
    if(std::strncmp(cell->type.c_str(), "$reduce_", 8) == 0 && cell->type.str() != "$reduce_xnor") {
      pid = 1;
    }

    uint32_t blackbox_out = 0;
    for(const auto &conn : cell->connections()) {
      // first faster filter but doesn't always work
      if(cell->input(conn.first) || (sub_graph && sub_graph->is_graph_input(&(conn.first.c_str()[1]))))
        continue;

#ifndef NDEBUG
      if (sub_graph && !(cell->output(conn.first) || tcell || blackbox || (sub_graph && sub_graph->is_graph_output(&(conn.first.c_str()[1]))))) {
        fmt::print("cf.s:{} i:{} o:{}\n",conn.first.c_str()
            ,sub_graph->is_graph_input(&(conn.first.c_str()[1]))
            ,sub_graph->is_graph_output(&(conn.first.c_str()[1])));
        sub_graph->dump();
      }
#endif
      assert(cell->output(conn.first) || tcell || blackbox || (sub_graph && sub_graph->is_graph_output(&(conn.first.c_str()[1]))));
      if(blackbox && !is_black_box_output(module, cell, conn.first)) {
        connect_string(g, &(conn.first.c_str()[1]), nid, LGRAPH_BBOP_ONAME(blackbox_out++));
        continue;
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

      Node_pin driver_pin = g->get_node(nid).setup_driver_pin(pid);

      if(ss.chunks().size() > 0)
        g->set_bits(driver_pin, ss.size());

      uint32_t offset = 0;
      for(auto &chunk : ss.chunks()) {
        const RTLIL::Wire *wire = chunk.wire;

        // disconnected output
        if(wire == 0)
          continue;

        if(chunk.width == wire->width) {
          assert(wire2lpin.find(wire) == wire2lpin.end());

          if(chunk.width == ss.size()) {
            // output port drives a single wire
            wire2lpin[wire].idx     = driver_pin.get_idx();
            wire2lpin[wire].out_pid = driver_pin.get_pid();
            set_bits_wirename(g, driver_pin.get_idx(), wire);
          } else {
            // output port drives multiple wires
            Node_pin pick_pin       = create_pick_operator(g, g->get_node(nid).get_driver_pin(pid), offset, chunk.width);
            wire2lpin[wire].idx     = pick_pin.get_idx();
            wire2lpin[wire].out_pid = pick_pin.get_pid();
            set_bits_wirename(g, pick_pin.get_idx(), wire);
          }
          offset += chunk.width;

        } else {
          if(partially_assigned.find(wire) == partially_assigned.end()) {
            std::vector<Node_pin *> nodes(wire->width);
            partially_assigned.insert(std::pair<const RTLIL::Wire *, std::vector<Node_pin *>>(wire, nodes));

            assert(wire2lpin.find(wire) == wire2lpin.end());
            wire2lpin[wire].idx     = g->create_node().get_nid();
            wire2lpin[wire].out_pid = 0;
            g->set_bits(wire2lpin[wire].idx, wire->width);

            g->node_type_set(wire2lpin[wire].idx, Join_Op);
          }
          g->set_bits(driver_pin, ss.size());

          Node_pin *src_pin = new Node_pin(create_pick_operator(g, g->get_node(nid).get_driver_pin(pid), offset, chunk.width));
          offset += chunk.width;
          for(int i = 0; i < chunk.width; i++) {
            partially_assigned[wire][chunk.offset + i] = src_pin;
          }
        }
      }
    }

    if (sub_graph) { // To do not leave too many mmaps open
      sub_graph->close();
      sub_graph = nullptr;
    }
  }
}

static Node_pin create_join_operator(LGraph *g, const RTLIL::SigSpec &ss) {
  std::vector<Node_pin> inp_pins;
  assert(ss.chunks().size() != 0);

  for(auto &chunk : ss.chunks()) {
    if(chunk.wire == nullptr) {
      inp_pins.push_back(resolve_constant(g, chunk.data));
    } else {
      inp_pins.push_back(create_pick_operator(g, chunk.wire, chunk.offset, chunk.width));
    }
  }

  Node_pin dpin;
  if(inp_pins.size() > 1) {
    auto join_node = g->create_node(Join_Op, ss.size());

    int pid = 0;
    for(auto &inp_pin : inp_pins) {
      g->add_edge(inp_pin, join_node.setup_sink_pin(pid));
      pid++;
    }
    dpin = join_node.setup_driver_pin(0);
  } else {
    dpin = inp_pins[0]; // single wire, do not create join
  }

  return dpin;
}

static void process_assigns(RTLIL::Module *module, LGraph *g) {
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
        Node_pin dpin = create_join_operator(g, rhs.extract(offset, chunk.width));

        offset += chunk.width;
        if(lhs_wire->port_output) {
          Node_pin spin = g->get_graph_output(&lhs_wire->name.c_str()[1]);
          g->add_edge(dpin, spin, lhs_wire->width);
        } else {
          if(wire2lpin.find(lhs_wire) == wire2lpin.end()) {
            wire2lpin[lhs_wire].out_pid = dpin.get_pid();
            wire2lpin[lhs_wire].idx     = dpin.get_idx();
          } else {
            g->set_bits(wire2lpin[lhs_wire].idx, g->get_bits(dpin));
            auto spin = g->get_node(wire2lpin[lhs_wire].idx).get_sink_pin(wire2lpin[lhs_wire].out_pid);
            g->add_edge(dpin, spin);
          }
        }

      } else {
        if(partially_assigned.find(lhs_wire) == partially_assigned.end()) {
          std::vector<Node_pin *> nodes(lhs_wire->width);
          partially_assigned.insert(std::make_pair(lhs_wire, nodes));

          if(wire2lpin.find(lhs_wire) == wire2lpin.end()) {
            wire2lpin[lhs_wire].idx     = g->create_node().get_nid();
            wire2lpin[lhs_wire].out_pid = 0;
            g->node_type_set(wire2lpin[lhs_wire].idx, Join_Op);
          }

          g->set_bits(wire2lpin[lhs_wire].idx, lhs_wire->width);
        }

        if(chunk.width == 0)
          continue;
        Node_pin dpin = create_join_operator(g, rhs.extract(offset, chunk.width));

        offset += chunk.width;
        for(int i = 0; i < chunk.width; i++) {
          partially_assigned[lhs_wire][chunk.offset + i] = new Node_pin(dpin);
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
  const Tech_library &tlib  = g->get_tlibrary();
  const Tech_cell *   tcell = nullptr;

  process_assigns(module, g);

#ifndef NDEBUG
  log("INOU/YOSYS processing module %s, ncells %lu, nwires %lu\n", module->name.c_str(), module->cells().size(),
      module->wires().size());
#endif

  for(auto cell : module->cells()) {
    //log("Looking for cell %s:\n", cell->type.c_str());

    Index_ID onid, inid;
    assert(cell2nid.find(cell) != cell2nid.end());
    onid                   = cell2nid[cell];
    Node_Type_Op op;

    inid = onid;

    LGraph *sub_graph = nullptr;

    bool         subtraction = false, negonly = false, yosys_tech = false;
    uint32_t     size = 0;
    uint32_t     rdports = 0;
    uint32_t     wrports = 0;
    uint32_t     abits = 0;
    RTLIL::Wire *clock = nullptr;

    // Note that $_AND_ and $_NOT_ format are exclusive for aigmap
    // yosys usually uses cells like $or $not $and
    if(std::strncmp(cell->type.c_str(), "$and", 4) == 0 || std::strncmp(cell->type.c_str(), "$logic_and", 10) == 0 ||
       std::strncmp(cell->type.c_str(), "$reduce_and", 11) == 0) {
      op = And_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$not", 4) == 0) {
      op = Not_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$logic_not", 10) == 0) {
      op = Or_Op;

      auto i_node = g->create_node(Or_Op);
      inid = i_node.get_nid();
      auto o_node = g->get_node(onid);
      g->node_type_set(onid, Not_Op);

      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

      g->add_edge(i_node.setup_driver_pin(1), o_node.setup_sink_pin(), size); // OR
      g->set_bits(o_node.setup_driver_pin(), size); // NOT

    } else if(std::strncmp(cell->type.c_str(), "$or", 3) == 0 || std::strncmp(cell->type.c_str(), "$logic_or", 9) == 0 ||
              std::strncmp(cell->type.c_str(), "$reduce_or", 10) == 0 ||
              std::strncmp(cell->type.c_str(), "$reduce_bool", 12) == 0) {
      op = Or_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$xor", 4) == 0 || std::strncmp(cell->type.c_str(), "$reduce_xor", 11) == 0) {
      op = Xor_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
    } else if(std::strncmp(cell->type.c_str(), "$xnor", 5) == 0 || std::strncmp(cell->type.c_str(), "$reduce_xnor", 11) == 0) {
      op = Xor_Op;
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

      auto i_node = g->create_node();
      inid = i_node.get_nid();
      g->set_bits(inid, size);

      g->node_type_set(onid, Not_Op);
      auto not_node = g->get_node(onid);

      if(std::strncmp(cell->type.c_str(), "$xnor", 5) == 0)
        g->add_edge(i_node.setup_driver_pin(0), not_node.setup_sink_pin());
      else
        g->add_edge(i_node.setup_driver_pin(1), not_node.setup_sink_pin(), size);

    } else if(std::strncmp(cell->type.c_str(), "$dff", 4) == 0) {
      op = SFlop_Op;
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
      RTLIL::Const clk_polarity = cell->parameters["\\CLK_POLARITY"];
      for(int i=1;i<clk_polarity.size();i++) {
        assert(clk_polarity[0] == clk_polarity[i]);
      }
      if (clk_polarity.size() && clk_polarity[0] != RTLIL::S1)
        connect_constant(g, 0, 1, onid, 5); // POL is 5 in SFlop_Op


    } else if(std::strncmp(cell->type.c_str(), "$adff", 4) == 0) {
      op = AFlop_Op;
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
      RTLIL::Const clk_polarity = cell->parameters["\\EN_POLARITY"];
      for(int i=1;i<clk_polarity.size();i++) {
        assert(clk_polarity[0] == clk_polarity[i]);
      }
      if (clk_polarity.size() && clk_polarity[0] != RTLIL::S1)
        connect_constant(g, 0, 1, onid, 2); // POL is 3 in Latch_Op

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
      // TODO: prevent the genereration of the join and simply connect wires
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
      auto i_node = g->create_node();
      inid = i_node.get_nid();
      g->set_bits(inid, size);

      g->node_type_set(onid, Not_Op);
      g->add_edge(i_node.setup_driver_pin(0), g->get_node(onid).setup_sink_pin(), size);

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

      // int parameters
      uint32_t width  = cell->parameters["\\WIDTH"].as_int();
      uint32_t depth  = cell->parameters["\\SIZE"].as_int();
      uint32_t offset = cell->parameters["\\OFFSET"].as_int();
      abits           = cell->parameters["\\ABITS"].as_int();
      rdports         = cell->parameters["\\RD_PORTS"].as_int();
      wrports         = cell->parameters["\\WR_PORTS"].as_int();

      // string parameters
      RTLIL::Const transp  = cell->parameters["\\RD_TRANSPARENT"];
      RTLIL::Const rd_clkp = cell->parameters["\\RD_CLK_POLARITY"];
      RTLIL::Const rd_clke = cell->parameters["\\RD_CLK_ENABLE"];
      RTLIL::Const wr_clkp = cell->parameters["\\WR_CLK_POLARITY"];
      RTLIL::Const wr_clke = cell->parameters["\\WR_CLK_ENABLE"];

      std::string name = cell->parameters["\\MEMID"].decode_string();

      size = width;

      fmt::print("name:{} depth:{} wrports:{} rdports:{}\n", name, depth, wrports, rdports);

      connect_constant(g, depth  , 32, onid, LGRAPH_MEMOP_SIZE  );
      connect_constant(g, offset , 32, onid, LGRAPH_MEMOP_OFFSET);
      connect_constant(g, abits  , 32, onid, LGRAPH_MEMOP_ABITS );
      connect_constant(g, wrports, 32, onid, LGRAPH_MEMOP_WRPORT);
      connect_constant(g, rdports, 32, onid, LGRAPH_MEMOP_RDPORT);

      // lgraph has reversed convention compared to yosys.
      int rd_clk_enabled  = 0;
      int rd_clk_polarity = 0;
      for(int i=0;i<rd_clkp.size();i++) {
        if (rd_clke[i] != RTLIL::S1)
          continue;

        if (rd_clkp[i] == RTLIL::S1) {
          if (rd_clk_enabled)
            assert(rd_clk_polarity); // All the read ports are equal
          rd_clk_enabled = 1;
          rd_clk_polarity = 1;
        }else if (rd_clkp[i] == RTLIL::S0) {
          if (rd_clk_enabled)
            assert(!rd_clk_polarity); // All the read ports are equal
          rd_clk_enabled = 1;
          rd_clk_polarity = 0;
        }
      }
      if (rd_clk_enabled) {
        // If there is a rd_clk, all should have rd_clk
        for(int i=0;i<rd_clkp.size();i++) {
          if (rd_clke[i] == RTLIL::S1)
            continue;

          log("oops rd_port:%d does not need clk cell %s\n", i, cell->type.c_str());
        }
      }
      int wr_clk_enabled  = 0;
      int wr_clk_polarity = 0;
      for(int i=0;i<wr_clkp.size();i++) {
        if (wr_clke[i] != RTLIL::S1)
          continue;
        if (wr_clkp[i] == RTLIL::S1) {
          if (wr_clk_enabled)
            assert(wr_clk_polarity); // All the read ports are equal
          wr_clk_enabled = 1;
          wr_clk_polarity = 1;
        }else if (wr_clkp[i] == RTLIL::S0) {
          if (wr_clk_enabled)
            assert(!wr_clk_polarity); // All the read ports are equal
          wr_clk_enabled = 1;
          wr_clk_polarity = 0;
        }
      }
      rd_clk_polarity = rd_clk_polarity?0:1; // polarity flipped in lgraph vs yosys
      wr_clk_polarity = wr_clk_polarity?0:1; // polarity flipped in lgraph vs yosys

      if (rd_clk_enabled)
        connect_constant(g, rd_clk_polarity, 1, onid, LGRAPH_MEMOP_RDCLKPOL);
      if (wr_clk_enabled)
        connect_constant(g, wr_clk_polarity, 1, onid, LGRAPH_MEMOP_WRCLKPOL);

      connect_constant(g, transp.as_int() , 1, onid, LGRAPH_MEMOP_RDTRAN);

      // TODO: get a test case to patch
      if(cell->parameters.find("\\INIT") != cell->parameters.end()) {
        assert(cell->parameters["\\INIT"].as_string()[0] == 'x');
      }

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
      log_error("ERROR: add this cell type %s to lgraph\n", cell->type.c_str());
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      op = Invalid_Op;

    } else if((LGraph::exists(g->get_path(), &cell->type.c_str()[1]))) {
      // external graph reference
      sub_graph = LGraph::open(g->get_path(), &cell->type.c_str()[1]);
      assert(sub_graph);
      log("module name original was %s\n", cell->type.c_str());
      op = SubGraph_Op;

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "")
        g->set_node_instance_name(inid, inst_name.c_str());
#ifndef NDEBUG
      else
        fmt::print("yosys2lg got empty inst_name for cell type {}\n", cell->type.c_str());
#endif

    } else if(tlib.include(cell->type.str())) {
      std::string ttype = cell->type.str();
      op                = TechMap_Op;
      tcell             = tlib.get_const_cell(tlib.get_cell_id(ttype));

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "" && inst_name.substr(0, 5) != "auto$" && inst_name.substr(0, 4) != "abc$")
        g->set_node_instance_name(inid, inst_name.c_str());

    } else if(tlib.include(cell->type.str().substr(1))) {
      std::string ttype = cell->type.str().substr(1);
      op                = TechMap_Op;
      tcell             = tlib.get_const_cell(tlib.get_cell_id(ttype));

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "" && inst_name.substr(0, 5) != "auto$" && inst_name.substr(0, 4) != "abc$")
        g->set_node_instance_name(inid, inst_name.c_str());

    } else if(tlib.include(cell->type.str().substr(2))) {
      std::string ttype = cell->type.str().substr(2);
      op                = TechMap_Op;
      tcell             = tlib.get_const_cell(tlib.get_cell_id(ttype));

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "" && inst_name.substr(0, 5) != "auto$" && inst_name.substr(0, 4) != "abc$")
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

    } else if(std::strncmp(cell->type.c_str(), "$_DFF_NN", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_NP", 8) == 0 ||
              std::strncmp(cell->type.c_str(), "$_DFF_PP", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_PN", 8) == 0) {

      // TODO: add support for those DFF types
      log_error("Found complex yosys DFFs, run `techmap -map +/adff2dff.v` before calling the yosys2lg pass\n");
      assert(false);

    } else {
      // blackbox addition
      ::Pass::info("Black box addition from yosys frontend, cell type {} not found instance {}", cell->type.c_str(), cell->name.c_str());

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

      if(std::strncmp(cell->type.c_str(), "$reduce_", 8) == 0 && cell->type.str() != "$reduce_xnor") {
        assert(size);
#if 0
        if (size>1) {
          auto x_node     = g->create_node_const(std::string(size, 'x'), size-1);
          auto x_dpin     = x_node.setup_driver_pin();

          auto join_node  = g->create_node(Join_Op, size);
          auto j_spin_x   = join_node.setup_sink_pin(0);
          auto j_spin_red = join_node.setup_sink_pin(1);

          g->add_edge(x_dpin, j_spin_x);
          g->add_edge(Node_pin(inid, 1, false), j_spin_red);

          //inid = join_node.get_nid();
          g->set_bits(join_node.setup_driver_pin(), size);
        }
#endif
      }
    }

    uint32_t blackbox_port = 0;

    std::set<std::pair<Node_pin, Node_pin>> added_edges;
    for(auto &conn : cell->connections()) {
      RTLIL::SigSpec ss = conn.second;
      if(ss.size() == 0)
        continue;

      Port_ID dst_pid = 0;
      // Go over cells with multiple inputs that map to something different than A
      if(op == SubGraph_Op) {
        std::string_view name(&conn.first.c_str()[1]);
        assert(sub_graph);
        if(sub_graph->is_graph_output(name))
          continue;
        dst_pid = sub_graph->get_graph_input(name).get_pid();
      } else if(op == TechMap_Op) {
        std::string_view name(&conn.first.c_str()[1]);
        if(tcell->is_output(name))
          continue;
        dst_pid = tcell->get_inp_id(name);

      } else if(op == BlackBox_Op && !yosys_tech) {
        if(is_black_box_output(module, cell, conn.first)
        || is_black_box_input(module, cell, conn.first)) {
          connect_constant(g, 0, 1, onid, LGRAPH_BBOP_PARAM(blackbox_port));
          connect_string(g, &(conn.first.c_str()[1]), onid, LGRAPH_BBOP_PNAME(blackbox_port));
          dst_pid = LGRAPH_BBOP_CONNECT(blackbox_port);
          blackbox_port++;
        } else {
          bool o = is_black_box_output(module, cell, conn.first);
          bool i = is_black_box_input (module, cell, conn.first);
          ::Pass::error("Could not find a definition for module {}, treating as a blackbox but could not determine whether {} is an input"
              "or an output {} {}",
              cell->type.str(), conn.first.c_str(), i, o);
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
        } else if(op == Latch_Op) {
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
        } else if(op == GreaterThan_Op || op == LessThan_Op || op == GreaterEqualThan_Op || op == LessEqualThan_Op ||
                  op == Div_Op || op == Mod_Op) {
          dst_pid = 0;
          if(cell->parameters[conn.first.str() + "_SIGNED"].as_int() == 0)
            dst_pid += 1;
          if(conn.first.c_str()[1] == 'B')
            dst_pid += 2;
        } else if(op == Memory_Op) {
          if(conn.first.str() == "\\WR_CLK") {
#ifndef NDEBUG
            for(auto &clk_chunk : conn.second.chunks()) {
              assert(clk_chunk.wire == clock);
            }
#endif
            dst_pid = LGRAPH_MEMOP_CLK;
            ss      = RTLIL::SigSpec(clock);
          } else if(conn.first.str() == "\\WR_ADDR") {
            for(uint32_t wrport = 0; wrport < wrports; wrport++) {
              Node_pin spin = g->get_node(inid).setup_sink_pin(LGRAPH_MEMOP_WRADDR(wrport));
              Node_pin dpin = create_join_operator(g, ss.extract(wrport * abits, abits));
              g->add_edge(dpin, spin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\WR_DATA", 8) == 0) {
            for(uint32_t wrport = 0; wrport < wrports; wrport++) {
              Node_pin spin = g->get_node(inid).setup_sink_pin(LGRAPH_MEMOP_WRDATA(wrport));
              Node_pin dpin = create_join_operator(g, ss.extract(wrport * size, size));
              g->add_edge(dpin, spin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\WR_EN", 6) == 0) {
            for(uint32_t wrport = 0; wrport < wrports; wrport++) {
              Node_pin spin = g->get_node(inid).setup_sink_pin(LGRAPH_MEMOP_WREN(wrport));
              Node_pin dpin = create_join_operator(g, ss.extract(wrport, 1));
              g->add_edge(dpin, spin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\RD_CLK", 7) == 0) {
#ifndef NDEBUG
            for(auto &clk_chunk : conn.second.chunks()) {
              assert(clk_chunk.wire == clock || !clk_chunk.wire);
            }
#endif
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\RD_ADDR", 8) == 0) {
            for(uint32_t rdport = 0; rdport < rdports; rdport++) {
              Node_pin spin = g->get_node(inid).setup_sink_pin(LGRAPH_MEMOP_RDADDR(rdport));
              Node_pin dpin = create_join_operator(g, ss.extract(rdport * abits, abits));
              g->add_edge(dpin, spin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\RD_EN", 6) == 0) {
            for(uint32_t rdport = 0; rdport < rdports; rdport++) {
              Node_pin spin = g->get_node(inid).setup_sink_pin(LGRAPH_MEMOP_RDEN(rdport));
              if (ss.extract(rdport, 1)[0].data == RTLIL::State::Sx) { // Yosys has Sx as enable sometimes, WEIRD. Fix it to 0
                auto node = g->create_node_u32(0,1);
                Node_pin dpin = node.setup_driver_pin();
                g->add_edge(dpin, spin);
              }else{
                Node_pin dpin = create_join_operator(g, ss.extract(rdport, 1));
                g->add_edge(dpin, spin);
              }
            }
            continue;
          } else {
            assert(false); // port not found
          }
        }
      }

      Node_pin spin = g->get_node(inid).setup_sink_pin(dst_pid);
      if(ss.size() > 0) {
        Node_pin dpin = create_join_operator(g, ss);
        if(added_edges.find(std::make_pair(dpin, spin)) != added_edges.end()) {
          // there are two edges from dpin to spin
          // this is not allowed in lgraph, add a join in between
          auto join_node = g->create_node(Join_Op, ss.size());
          g->add_edge(dpin, join_node.setup_sink_pin(0), ss.size());
          dpin = join_node.setup_driver_pin(0);
        }
        g->add_edge(dpin, spin);
        added_edges.insert(std::make_pair(dpin, spin));
      }
    }

    if (sub_graph) {
      sub_graph->close(); // to avoid output leak
      sub_graph = nullptr;
    }

  }

  for(auto &kv : partially_assigned) {
    const RTLIL::Wire *wire = kv.first;

    Index_ID join_idx  = wire2lpin[wire].idx;
    auto     join_node = g->get_node(join_idx);
    Port_ID  join_pid  = 0;

    set_bits_wirename(g, join_idx, wire);

    Node_pin *current = nullptr;
    int       size    = 0;
    bool      first   = true;
    for(auto *pin : kv.second) {
      if(first)
        first = false;
      else if((current == nullptr && pin != nullptr) || (current != nullptr && pin == nullptr) ||
              (current != pin && *current != *pin)) {
        Node_pin dpin;
        if (current) {
          dpin = *current;
        }else{
          auto node = g->create_node_const(std::string(size, 'x'), size);
          dpin = node.setup_driver_pin();
        }

        assert(size < g->get_bits(dpin) || size % g->get_bits(dpin) == 0);
        int times = size < g->get_bits(dpin) ? 1 : size / g->get_bits(dpin);
        for(int i = 0; i < times; i++) {
          g->add_edge(dpin, join_node.setup_sink_pin(join_pid++));
        }
        size = 0;
      }
      size++;
      current = pin;
    }

    Node_pin dpin;
    if (current) {
      dpin = *current;
    }else{
      auto node = g->create_node_const(std::string(size, 'x'), size);
      dpin = node.setup_driver_pin();
    }
    assert(size < g->get_bits(dpin) || size % g->get_bits(dpin) == 0);
    int times = size < g->get_bits(dpin) ? 1 : size / g->get_bits(dpin);
    for(int i = 0; i < times; i++) {
      g->add_edge(dpin, join_node.setup_sink_pin(join_pid++));
    }
  }

  // we need to connect global outputs to the cell that drives it
  for(auto wire : module->wires()) {
    if(wire->port_output && wire2lpin.find(wire) != wire2lpin.end()) {

      Node_pin spin = g->get_graph_output(&wire->name.c_str()[1]);
      Node_pin dpin = g->get_node(wire2lpin[wire].idx).get_driver_pin(wire2lpin[wire].out_pid);
      g->add_edge(dpin, spin, wire->width);

      log("  connecting module output %s driver[%d:%d] sink[%d:%d]\n"
          ,wire->name.c_str()
          ,dpin.get_idx().value, dpin.get_pid()
          ,spin.get_idx().value, spin.get_pid());
    }
  }

  return g;
}

// each pass contains a singleton object that is derived from Pass
struct Yosys2lg_Pass : public Yosys::Pass {
  Yosys2lg_Pass()
      : Pass("yosys2lg") {
  }
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
		ct_all.setup(design);

    for(auto &it : design->modules_) {
      RTLIL::Module *module = it.second;
      std::string    name   = &module->name.c_str()[1];
      assert(module2graph.find(name) == module2graph.end());

      std::set<size_t> driven_signals;
      SigMap sigmap(module);

      for (auto wire : module->wires()) {
        if (wire->port_input)
          driven_signals.insert(wire->hash());
      }

      for (auto it : module->connections()) {
        for(auto &chunk : it.first.chunks()) {
          const RTLIL::Wire *wire = chunk.wire;
          if (wire)
            driven_signals.insert(wire->hash());
        }
      }

      for (auto cell : module->cells()) {
        if (ct_all.cell_known(cell->type)) {
          for (auto &conn : cell->connections()) {
            if (ct_all.cell_output(cell->type, conn.first)) {
              for(auto &chunk : conn.second.chunks()) {
                const RTLIL::Wire *wire = chunk.wire;
                if (wire)
                  driven_signals.insert(wire->hash());
              }
            }
          }
        }
      }

      for (auto cell : module->cells()) {
        if (!ct_all.cell_known(cell->type)) {
          for (auto &conn : cell->connections()) {
            for(auto &chunk : conn.second.chunks()) {
              const RTLIL::Wire *wire = chunk.wire;
              if (wire == nullptr)
                continue;

              bool is_input  = wire->port_input  || driven_signals.count(wire->hash())>0;
              bool is_output = wire->port_output || driven_signals.count(wire->hash())==0;

#if 0
              std::cout << "unknown cell_type:" << cell->type.str() << " cell_instance:" << cell->name.str() << " port_name:" << conn.first.str() << " wire_name:" << wire->name.str()
                << " sig:" << wire->hash()
                << " io:" << (is_input?"I":"") << (is_output?"O":"")
                << "\n";
#endif

              std::string cell_port = absl::StrCat(cell->type.str(),"_:_",conn.first.str());
              if (is_input)
                cell_port_inputs.insert(cell_port);
              if (is_output)
                cell_port_outputs.insert(cell_port);
            }
          }
        }
      }



      auto *g            = LGraph::create(path, name, "-"); // No source, unable to track
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
        look_for_cell_outputs(module);
        LGraph *g = process_module(module);

        g->sync();
        g->close();
      }

      wire2lpin.clear();
      cell2nid.clear();
      wirebit2lpin.clear();
      partially_assigned.clear();
      picks.clear();
    }
  }
} Yosys2lg_Pass;

PRIVATE_NAMESPACE_END
