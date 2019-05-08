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

//#include "absl/container/node_hash_map.h"
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

typedef std::pair<const RTLIL::Wire *, int> Wire_bit;

// static std::map<const RTLIL::Wire *, GlobalPin> wire2gpin;
static absl::flat_hash_map<const RTLIL::Wire *, Node_pin>  wire2pin;
static absl::flat_hash_map<const RTLIL::Cell *, Node>      cell2node; // Points to the exit_node for the block
static absl::flat_hash_map<const RTLIL::Wire *, Node_pin_iterator>      partially_assigned;

static void look_for_module_outputs(RTLIL::Module *module, const std::string &path) {
#ifndef NDEBUG
  log("yosys2lg look_for_module_outputs pass for module %s:\n", module->name.c_str());
#endif
  auto  library = Graph_library::instance(path);
  auto *g       = library->try_find_lgraph(&module->name.c_str()[1]);
  assert(g);

  int last_input_port_id  = 0;
  int last_output_port_id = 0;

  std::vector<RTLIL::Wire *> io_wires;

  for(auto &wire_iter : module->wires_) {
    RTLIL::Wire *wire = wire_iter.second;
    if(wire->port_input) {
      log("input %s\n",wire->name.c_str());
      io_wires.push_back(wire);
      I(!wire->port_output); // any bidirectional port?
      I(wire->name.c_str()[0] == '\\');
    }else if (wire->port_output) {
      log("output %s\n",wire->name.c_str());
      io_wires.push_back(wire);
      I(wire->name.c_str()[0] == '\\');
    }
  }

  struct Less_than_sort_io {
    inline bool operator() (const RTLIL::Wire *e1, const RTLIL::Wire *e2) {
      return (e1->port_id < e2->port_id);
    }
  };

  std::sort(io_wires.begin(),io_wires.end(), Less_than_sort_io());

  for(RTLIL::Wire *wire:io_wires) {
    if(wire->port_input) {
      log("2input %s\n",wire->name.c_str());
      I(last_input_port_id <= wire->port_id);
      last_input_port_id = wire->port_id;
      g->add_graph_input(&wire->name.c_str()[1], wire->width, wire->start_offset);
    }else{
      I(wire->port_output);
      log("2output %s\n",wire->name.c_str());
      I(last_output_port_id <= wire->port_id);
      last_output_port_id = wire->port_id;
      g->add_graph_output(&wire->name.c_str()[1], wire->width, wire->start_offset);
    }
  }

  g->sync(); // Not needed, but to free-up mmaps...
}

static bool is_yosys_output(const std::string &idstring) {
  return idstring == "\\Y" || idstring == "\\Q" || idstring == "\\RD_DATA";
}

static Node_pin &get_edge_pin(LGraph *g, const RTLIL::Wire *wire) {

  if(wire2pin.find(wire) != wire2pin.end()) {
    if(wire->width != wire2pin[wire].get_bits()) {
      fmt::print("w_name:{} w_w:{} | pid:{} bits:{}\n"
          ,wire->name.c_str()
          ,wire->width
          ,wire2pin[wire].get_pid()
          ,wire2pin[wire].get_bits());
    }
    wire2pin[wire].set_bits(wire->width);
    assert(wire->width == wire2pin[wire].get_bits());
    return wire2pin[wire];
  }

  if(wire->port_input) {
    wire2pin[wire] = g->get_graph_input(&wire->name.c_str()[1]);
    return wire2pin[wire];
  }

  if(wire->port_output) {
    wire2pin[wire] = g->get_graph_output_driver(&wire->name.c_str()[1]);
    return wire2pin[wire];
  }

  auto node = g->create_node(Join_Op, wire->width);

  wire2pin[wire] = node.setup_driver_pin();

  return wire2pin[wire];
}

static Node_pin connect_constant(LGraph *g, uint32_t value, Node &exit_node, Port_ID opid) {

  uint16_t size = 1;
  uint32_t val  = value;
  while (val > 0) {
    size++;
    val >>= 1;
  }

  auto dpin = g->create_node_const(value,size).setup_driver_pin();
  auto spin = exit_node.setup_sink_pin(opid);

  spin.connect_driver(dpin);

  return dpin;
}

class Pick_ID {
  // friend constexpr bool operator==(const Pick_ID &lhs, const Pick_ID &rhs);
public:
  Node_pin driver;
  int      offset;
  int      width;

  Pick_ID(Node_pin driver, int offset, int width)
      : driver(driver)
      , offset(offset)
      , width(width) {}

  template <typename H>
  friend H AbslHashValue(H h, const Pick_ID& s) {
    return H::combine(std::move(h), s.driver.get_compact(), s.offset, s.width);
  };
};

bool operator==(const Pick_ID &lhs, const Pick_ID &rhs) {
  return lhs.driver == rhs.driver && lhs.driver == rhs.driver && lhs.offset == rhs.offset;
}

absl::flat_hash_map<Pick_ID, Node_pin> picks; // NODE, not flat to preserve pointer stability

static Node_pin &create_pick_operator(LGraph *g, Node_pin &driver, int offset, int width) {
  if(offset == 0 && driver.get_bits() == width)
    return driver;

  Pick_ID pick_id(driver, offset, width);
  if(picks.find(pick_id) != picks.end())
    return picks.at(pick_id);

  auto node = g->create_node(Pick_Op, width);
  auto driver_pin0 = node.setup_driver_pin();
  auto sink_pin0   = node.setup_sink_pin(0);

  g->add_edge(driver, sink_pin0);

  connect_constant(g, offset, node, node.setup_sink_pin(1).get_pid());

  picks.insert(std::make_pair(pick_id, driver_pin0));

  return picks.at(pick_id);
}

static Node_pin &create_pick_operator(LGraph *g, const RTLIL::Wire *wire, int offset, int width) {
  if(wire->width == width && offset == 0)
    return get_edge_pin(g, wire);

  return create_pick_operator(g, get_edge_pin(g, wire), offset, width);
}

const std::regex dc_name("(\\|/)n(\\d\\+)|^N(\\d\\+)");

static void set_bits_wirename(Node_pin &pin, const RTLIL::Wire *wire) {
  if(!wire)
    return;

  if(!wire->port_input && !wire->port_output) {
    // we don't want to keep internal abc/yosys wire names
    // TODO: is there a more efficient and complete way of doing this?
    if(wire->name.c_str()[0] != '$'
       && wire->name.str().rfind("\\lg_") != 0
       // dc generated names
       && !std::regex_match(wire->name.str(), dc_name)

       // skip chisel generated names
       && wire->name.str().rfind("\\_GEN_") != 0
       && wire->name.str().rfind("\\_T_") != 0) {

      pin.set_name(&wire->name.c_str()[1]);
    }
  }

  if(!pin.is_graph_io()) {
    pin.set_bits(wire->width);
  }
}

static Node resolve_memory(LGraph *g, RTLIL::Cell *cell) {

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
        assert(wire2pin.find(wire) == wire2pin.end());

        if(chunk.width == ss.size()) {
          // output port drives a single wire
          wire2pin[wire]     = dpin;
          set_bits_wirename(dpin, wire);
        } else {
          // output port drives multiple wires
          Node_pin &pick_pin  = create_pick_operator(g, dpin, offset, chunk.width);
          wire2pin[wire]    = pick_pin;
          set_bits_wirename(pick_pin, wire);
        }
        offset += chunk.width;
      } else {
        if(partially_assigned.find(wire) == partially_assigned.end()) {
          partially_assigned[wire].resize(wire->width);

          assert(wire2pin.find(wire) == wire2pin.end());
          auto node = g->create_node(Join_Op, wire->width);

          wire2pin[wire] = node.setup_driver_pin();
        }
        dpin.set_bits(ss.size());

        auto &src_pin = create_pick_operator(g, dpin, offset, chunk.width);
        offset += chunk.width;
        for(int i = 0; i < chunk.width; i++) {
          I((size_t)(chunk.offset+i)<partially_assigned[wire].size());
          partially_assigned[wire][chunk.offset + i] = src_pin;
        }
      }
    }
  }

  return node;
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
    auto node = g->create_node_const(value,val.size());
    return node.setup_driver_pin();
  }

  auto node = g->create_node_const(val,val.size());
  return node.setup_driver_pin();
}

// does not treat string, keeps as it is (useful for names)
static void connect_string(LGraph *g, std::string_view value, Node &exit_node, Port_ID opid) {

  auto spin = exit_node.setup_sink_pin(opid);
  auto dpin = g->create_node_const(value).setup_driver_pin();

  spin.connect_driver(dpin);
}

static void look_for_cell_outputs(RTLIL::Module *module, const std::string &path) {

  //log("yosys2lg look_for_cell_outputs pass for module %s:\n", module->name.c_str());

  auto  library = Graph_library::instance(path);
  auto *g       = library->try_find_lgraph(&module->name.c_str()[1]);
  assert(g);

  const Tech_library &tlib = g->get_tlibrary();
  for(auto cell : module->cells()) {

    if(cell->type == "$mem") {
      cell2node[cell] = resolve_memory(g, cell);
      continue;
    }

    LGraph *         sub_graph = nullptr;
    const Tech_cell *tcell     = nullptr;

    std::string_view mod_name(&(cell->type.c_str()[1]));

    if(cell->type.c_str()[0] == '\\' || cell->type.str().rfind("$paramod") == 0)
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

    cell2node[cell] = g->create_node();
    auto &node = cell2node[cell];

    //uint32_t blackbox_out = 0;
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
        continue;
      } else if(sub_graph && !sub_graph->is_graph_output(&(conn.first.c_str()[1]))) {
        continue;
      } else if(tcell && !tcell->is_output(&(conn.first.c_str()[1]))) {
        continue;
      } else if(!sub_graph && !tcell && !is_yosys_output(conn.first.c_str())) {
        continue;
      }

      Port_ID pid;
      if(sub_graph) {
        pid = sub_graph->get_graph_output(&(conn.first.c_str()[1])).get_pid();
      } else if(tcell) {
        pid = tcell->get_out_pid(&(conn.first.c_str()[1]));
      }else if(std::strncmp(cell->type.c_str(), "$reduce_", 8) == 0 && cell->type.str() != "$reduce_xnor") {
        pid = 1;
      }else{
        pid = 0;
      }

      // WARNING: Not very nice that we set pins before we even know the cell type.
      Node_pin driver_pin = node.setup_driver_pin(pid);

      const RTLIL::SigSpec ss = conn.second;
      if(ss.chunks().size() > 0)
        driver_pin.set_bits(ss.size());

      uint32_t offset = 0;
      for(auto &chunk : ss.chunks()) {
        const RTLIL::Wire *wire = chunk.wire;

        // disconnected output
        if(wire == 0)
          continue;

        if(chunk.width == wire->width) {
          assert(wire2pin.find(wire) == wire2pin.end());

          if(chunk.width == ss.size()) {
            // output port drives a single wire
            wire2pin[wire]     = driver_pin;
            set_bits_wirename(driver_pin, wire);
          } else {
            // output port drives multiple wires
            Node_pin &pick_pin = create_pick_operator(g, driver_pin, offset, chunk.width);
            wire2pin[wire]     = pick_pin;
            set_bits_wirename(pick_pin, wire);
          }
          offset += chunk.width;

        } else {
          if(partially_assigned.find(wire) == partially_assigned.end()) {
            partially_assigned[wire].resize(wire->width);

            assert(wire2pin.find(wire) == wire2pin.end());
            auto join = g->create_node(Join_Op, wire->width);
            wire2pin[wire] = join.setup_driver_pin();
          }
          driver_pin.set_bits(ss.size());

          auto &src_pin = create_pick_operator(g, driver_pin, offset, chunk.width);
          offset += chunk.width;
          for(int i = 0; i < chunk.width; i++) {
            I((size_t)(chunk.offset+i)<partially_assigned[wire].size());
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
          if(wire2pin.find(lhs_wire) == wire2pin.end()) {
            wire2pin[lhs_wire] = dpin;
          } else {
            I(dpin.get_bits());
            wire2pin[lhs_wire].set_bits(dpin.get_bits());

            assert(false);// FIXME: find test case to debug/fix this
#if 0
            auto spin = g->get_node(wire2pin[lhs_wire].idx).get_sink_pin(wire2pin[lhs_wire].out_pid);
            g->add_edge(dpin, spin);
#endif
          }
        }

      } else {
        if(partially_assigned.find(lhs_wire) == partially_assigned.end()) {
          partially_assigned[lhs_wire].resize(lhs_wire->width);

          if(wire2pin.find(lhs_wire) == wire2pin.end()) {
            auto join  = g->create_node(Join_Op);
            wire2pin[lhs_wire] = join.setup_driver_pin();
          }

          wire2pin[lhs_wire].set_bits(lhs_wire->width);
        }

        if(chunk.width == 0)
          continue;
        Node_pin dpin = create_join_operator(g, rhs.extract(offset, chunk.width));

        offset += chunk.width;
        for(int i = 0; i < chunk.width; i++) {
          I((size_t)(chunk.offset+i)<partially_assigned[lhs_wire].size());
          partially_assigned[lhs_wire][chunk.offset + i] = dpin;
        }
      }
    }
  }
}

// this function is called for each module in the design
static LGraph *process_module(RTLIL::Module *module, const std::string &path) {
#ifndef NDEBUG
  log("yosys2lg pass for module %s:\n", module->name.c_str());
  printf("process_module %s\n", module->name.c_str());
#endif

  auto  library = Graph_library::instance(path);
  auto *g       = library->try_find_lgraph(&module->name.c_str()[1]);
  assert(g);

  const Tech_library &tlib  = g->get_tlibrary();
  const Tech_cell    *tcell = nullptr;

  process_assigns(module, g);

#ifndef NDEBUG
  log("INOU/YOSYS processing module %s, ncells %lu, nwires %lu\n", module->name.c_str(), module->cells().size(),
      module->wires().size());
#endif

  for(auto cell : module->cells()) {
    //log("Looking for cell %s:\n", cell->type.c_str());

    assert(cell2node.find(cell) != cell2node.end());
    Node exit_node  = cell2node[cell];
    Node entry_node = exit_node; // Same entry/exit unless multiple cells are needed to model

    LGraph      *sub_graph   = nullptr;
    bool         subtraction = false;
    bool         negonly     = false;
    bool         yosys_tech  = false;
    uint32_t     size        = 0;
    uint32_t     rdports     = 0;
    uint32_t     wrports     = 0;
    uint32_t     abits       = 0;
    RTLIL::Wire *clock       = nullptr;

    // Note that $_AND_ and $_NOT_ format are exclusive for aigmap
    // yosys usually uses cells like $or $not $and
    if(std::strncmp(cell->type.c_str(), "$and", 4) == 0 || std::strncmp(cell->type.c_str(), "$logic_and", 10) == 0 ||
       std::strncmp(cell->type.c_str(), "$reduce_and", 11) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      exit_node.set_type(And_Op);
      exit_node.setup_driver_pin(0).set_bits(size);
    } else if(std::strncmp(cell->type.c_str(), "$not", 4) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

      exit_node.set_type(Not_Op,size);
    } else if(std::strncmp(cell->type.c_str(), "$logic_not", 10) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

      entry_node = g->create_node(Or_Op);

      exit_node.set_type(Not_Op);
      auto &not_node = exit_node;

      g->add_edge(entry_node.setup_driver_pin(1), not_node.setup_sink_pin(), size); // OR
      not_node.setup_driver_pin().set_bits(size); // NOT

    } else if(std::strncmp(cell->type.c_str(), "$or", 3) == 0 || std::strncmp(cell->type.c_str(), "$logic_or", 9) == 0 ||
              std::strncmp(cell->type.c_str(), "$reduce_or", 10) == 0 ||
              std::strncmp(cell->type.c_str(), "$reduce_bool", 12) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      exit_node.set_type(Or_Op);
      exit_node.setup_driver_pin(0).set_bits(size);
    } else if(std::strncmp(cell->type.c_str(), "$xor", 4) == 0 || std::strncmp(cell->type.c_str(), "$reduce_xor", 11) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      exit_node.set_type(Xor_Op);
      exit_node.setup_driver_pin(0).set_bits(size);
    } else if(std::strncmp(cell->type.c_str(), "$xnor", 5) == 0 || std::strncmp(cell->type.c_str(), "$reduce_xnor", 11) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

      entry_node = g->create_node(Xor_Op, size);
      exit_node.set_type(Not_Op);

      if(std::strncmp(cell->type.c_str(), "$xnor", 5) == 0)
        g->add_edge(entry_node.setup_driver_pin(0), exit_node.setup_sink_pin());
      else
        g->add_edge(entry_node.setup_driver_pin(1), exit_node.setup_sink_pin(), size);

    } else if(std::strncmp(cell->type.c_str(), "$dff", 4) == 0) {
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
      entry_node.set_type(SFlop_Op, size);

      RTLIL::Const clk_polarity = cell->parameters["\\CLK_POLARITY"];
      for(int i=1;i<clk_polarity.size();i++) {
        assert(clk_polarity[0] == clk_polarity[i]);
      }
      if (clk_polarity.size() && clk_polarity[0] != RTLIL::S1)
        connect_constant(g, 0, exit_node, 5); // POL is 5 in SFlop_Op


    } else if(std::strncmp(cell->type.c_str(), "$adff", 4) == 0) {
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
      entry_node.set_type(AFlop_Op, size);

      RTLIL::Const clk_polarity = cell->parameters["\\EN_POLARITY"];
      for(int i=1;i<clk_polarity.size();i++) {
        assert(clk_polarity[0] == clk_polarity[i]);
      }
      if (clk_polarity.size() && clk_polarity[0] != RTLIL::S1)
        connect_constant(g, 0, exit_node, 2); // POL is 3 in Latch_Op

    } else if(std::strncmp(cell->type.c_str(), "$dlatch", 7) == 0) {
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
      entry_node.set_type(Latch_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$gt", 3) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(GreaterThan_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$lt", 3) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(LessThan_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$ge", 3) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(GreaterEqualThan_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$le", 3) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(LessEqualThan_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$mux", 4) == 0) {
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
      entry_node.set_type(Mux_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$add", 4) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Sum_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$mul", 4) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Mult_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$div", 4) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Div_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$mod", 4) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Mod_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$sub", 4) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Sum_Op);

      subtraction = true;
    } else if(std::strncmp(cell->type.c_str(), "$neg", 4) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Sum_Op);

      negonly = true;
    } else if(std::strncmp(cell->type.c_str(), "$pos", 4) == 0) {
      // TODO: prevent the genereration of the join and simply connect wires
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Join_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$eq", 3) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Equals_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$ne", 3) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();

      entry_node = g->create_node(Equals_Op, size);

      if (size>1) {
        auto zero_pin = g->create_node_const(0,size-1).setup_driver_pin();
        auto not_node = g->create_node(Not_Op, size);
        g->add_edge(entry_node.setup_driver_pin(0), not_node.setup_sink_pin(), 1);

        exit_node.set_type(Join_Op);
        g->add_edge(not_node.setup_driver_pin(), exit_node.setup_sink_pin(0), 1);
        g->add_edge(zero_pin, exit_node.setup_sink_pin(1), size-1);
      }else{
        exit_node.set_type(Not_Op);
        g->add_edge(entry_node.setup_driver_pin(0), exit_node.setup_sink_pin(), 1);
      }

      // WORKS: assign tmp2 = {~{lgraph_cell_13[0]}}; Zero upper bits
      // FAILS: assign tmp2 = ~lgraph_cell_13;

    } else if(std::strncmp(cell->type.c_str(), "$shr", 4) == 0 ||
              (std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && !cell->parameters["\\B_SIGNED"].as_bool())) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(ShiftRight_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && cell->parameters["\\B_SIGNED"].as_bool()) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(ShiftRight_Op, size);
      connect_constant(g, 2, entry_node, 2);

    } else if(std::strncmp(cell->type.c_str(), "$sshr", 5) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(ShiftRight_Op, size);

      if(cell->parameters["\\A_SIGNED"].as_bool())
        connect_constant(g, 3, entry_node, 2);
      else
        connect_constant(g, 1, entry_node, 2);

    } else if(std::strncmp(cell->type.c_str(), "$shl", 4) == 0 || std::strncmp(cell->type.c_str(), "$sshl", 5) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(ShiftLeft_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$mem", 4) == 0) {
      entry_node.set_type(Memory_Op);

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

      connect_constant(g, depth  , exit_node, LGRAPH_MEMOP_SIZE  );
      connect_constant(g, offset , exit_node, LGRAPH_MEMOP_OFFSET);
      connect_constant(g, abits  , exit_node, LGRAPH_MEMOP_ABITS );
      connect_constant(g, wrports, exit_node, LGRAPH_MEMOP_WRPORT);
      connect_constant(g, rdports, exit_node, LGRAPH_MEMOP_RDPORT);

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
        connect_constant(g, rd_clk_polarity, exit_node, LGRAPH_MEMOP_RDCLKPOL);
      if (wr_clk_enabled)
        connect_constant(g, wr_clk_polarity, exit_node, LGRAPH_MEMOP_WRCLKPOL);

      connect_constant(g, transp.as_int(), exit_node, LGRAPH_MEMOP_RDTRAN);

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

      exit_node.set_name(name);

    } else if(cell->type.c_str()[0] == '$' && cell->type.c_str()[1] != '_' && strncmp(cell->type.c_str(), "$paramod", 8) != 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      log_error("ERROR: add this cell type %s to lgraph size:%d\n", cell->type.c_str(), size);

      assert(false);

    } else if((LGraph::exists(g->get_path(), &cell->type.c_str()[1]))) {
      // external graph reference
      sub_graph = LGraph::open(g->get_path(), &cell->type.c_str()[1]);
      assert(sub_graph);
      log("module name original was %s\n", cell->type.c_str());

      entry_node.set_type_subgraph(sub_graph->lg_id());

      std::string inst_name = cell->name.str().substr(1);
      if(!inst_name.empty())
        entry_node.set_name(inst_name);
#ifndef NDEBUG
      else
        fmt::print("yosys2lg got empty inst_name for cell type {}\n", cell->type.c_str());
#endif

    } else if(tlib.include(cell->type.str())) {
      std::string ttype = cell->type.str();
      tcell             = tlib.get_const_cell(tlib.get_cell_id(ttype));

      entry_node.set_type_tmap_id(tcell->get_id());

      std::string inst_name = cell->name.str().substr(1);
      if(!inst_name.empty() && inst_name.rfind("auto$")!=0 && inst_name.rfind("abc$")!=0)
        entry_node.set_name(inst_name);

    } else if(tlib.include(cell->type.str().substr(1))) {
      std::string ttype = cell->type.str().substr(1);
      tcell             = tlib.get_const_cell(tlib.get_cell_id(ttype));

      entry_node.set_type_tmap_id(tcell->get_id());

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "" && inst_name.rfind("auto$")!=0 && inst_name.rfind("abc$")!=0)
        entry_node.set_name(inst_name);

    } else if(tlib.include(cell->type.str().substr(2))) {
      std::string ttype = cell->type.str().substr(2);
      tcell             = tlib.get_const_cell(tlib.get_cell_id(ttype));

      entry_node.set_type_tmap_id(tcell->get_id());

      std::string inst_name = cell->name.str().substr(1);
      if(inst_name != "" && inst_name.rfind("auto$")!=0 && inst_name.rfind("abc$")!=0)
        entry_node.set_name(inst_name);

      // DO NOT MERGE THE BELLOW WITH THE OTHER ANDs, NOTs, DFFs
    } else if(std::strncmp(cell->type.c_str(), "$_AND_", 6) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(And_Op);
      entry_node.setup_driver_pin(0).set_bits(size); // zero

    } else if(std::strncmp(cell->type.c_str(), "$_OR_", 6) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Or_Op);
      entry_node.setup_driver_pin(0).set_bits(size); // zero

    } else if(std::strncmp(cell->type.c_str(), "$_XOR_", 6) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Xor_Op);
      entry_node.setup_driver_pin(0).set_bits(size); // zero

    } else if(std::strncmp(cell->type.c_str(), "$_NOT_", 6) == 0) {
      if(cell->parameters.find("\\Y_WIDTH") != cell->parameters.end())
        size = cell->parameters["\\Y_WIDTH"].as_int();
      entry_node.set_type(Not_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$_DFF_P_", 8) == 0) {
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
      entry_node.set_type(SFlop_Op, size);

    } else if(std::strncmp(cell->type.c_str(), "$_DFF_N_", 8) == 0) {
      if(cell->parameters.find("\\WIDTH") != cell->parameters.end())
        size = cell->parameters["\\WIDTH"].as_int();
      entry_node.set_type(SFlop_Op, size);

      connect_constant(g, 0, entry_node, 5);

    } else if(std::strncmp(cell->type.c_str(), "$_DFF_NN", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_NP", 8) == 0 ||
              std::strncmp(cell->type.c_str(), "$_DFF_PP", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_PN", 8) == 0) {

      // TODO: add support for those DFF types
      log_error("Found complex yosys DFFs, run `techmap -map +/adff2dff.v` before calling the yosys2lg pass\n");
      assert(false);

    } else {
      // blackbox addition
      ::Pass::info("Black box addition from yosys frontend, cell type {} not found instance {}", cell->type.c_str(), cell->name.c_str());

      entry_node.set_type(BlackBox_Op);
      connect_string(g, &(cell->type.c_str()[1]), exit_node, LGRAPH_BBOP_TYPE);
      connect_string(g, &(cell->name.c_str()[1]), exit_node, LGRAPH_BBOP_NAME);
    }

    uint32_t blackbox_port = 0;

    absl::flat_hash_set<XEdge::Compact> added_edges;
    for(auto &conn : cell->connections()) {
      RTLIL::SigSpec ss = conn.second;
      if(ss.size() == 0)
        continue;

      Port_ID sink_pid = 0; // default for most cells
      // Go over cells with multiple inputs that map to something different than A
      if(entry_node.is_type(SubGraph_Op)) {
        std::string_view name(&conn.first.c_str()[1]);
        assert(sub_graph);
        if(sub_graph->is_graph_output(name))
          continue;
        sink_pid = sub_graph->get_graph_input(name).get_pid();
      } else if(entry_node.is_type(TechMap_Op)) {
        assert(tcell);
        std::string_view name(&conn.first.c_str()[1]);
        if(tcell->is_output(name))
          continue;
        sink_pid = tcell->get_inp_pid(name);

      } else if(entry_node.is_type(BlackBox_Op) && !yosys_tech) {
        if(is_black_box_input(module, cell, conn.first) || is_black_box_output(module, cell, conn.first)) {
          connect_string(g, &(conn.first.c_str()[1]), exit_node, LGRAPH_BBOP_IPARAM(blackbox_port));
          sink_pid = LGRAPH_BBOP_ICONNECT(blackbox_port);
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

        if(entry_node.is_type(SFlop_Op)
        || entry_node.is_type(Mux_Op)
        || entry_node.is_type(ShiftRight_Op)
        || entry_node.is_type(ShiftLeft_Op)) {
          if (conn.first.str() == "\\CLK")
            sink_pid = entry_node.get_type().get_input_match("C");
          else
            sink_pid = entry_node.get_type().get_input_match(&conn.first.c_str()[1]);
          assert(sink_pid<Port_invalid);
					//printf("input_match[%s] -> pid:%d\n", &conn.first.c_str()[1], sink_pid);
        } else if(entry_node.is_type(AFlop_Op)) {
          if(conn.first.str() == "\\ARST")
            sink_pid = 3;
          else
            sink_pid = entry_node.get_type().get_input_match(&conn.first.c_str()[1]);
        } else if(entry_node.is_type(Latch_Op)) {
          sink_pid = entry_node.get_type().get_input_match(&conn.first.c_str()[1]);
        } else if(entry_node.is_type(Sum_Op)) {
          sink_pid = 0;
          if(cell->parameters[conn.first.str() + "_SIGNED"].as_int() == 0)
            sink_pid += 1;
          if(negonly || (subtraction && conn.first.c_str()[1] == 'B'))
            sink_pid += 2;
        } else if(entry_node.is_type(Mult_Op)) {
          sink_pid = 0;
          if(cell->parameters[conn.first.str() + "_SIGNED"].as_int() == 0)
            sink_pid += 1;
        } else if(entry_node.is_type(GreaterThan_Op)
               || entry_node.is_type(LessThan_Op)
               || entry_node.is_type(GreaterEqualThan_Op)
               || entry_node.is_type(LessEqualThan_Op)
               || entry_node.is_type(Div_Op)
               || entry_node.is_type(Mod_Op)) {
          sink_pid = 0;
          if(cell->parameters[conn.first.str() + "_SIGNED"].as_int() == 0)
            sink_pid += 1;
          if(conn.first.c_str()[1] == 'B')
            sink_pid += 2;
        } else if(entry_node.is_type(Memory_Op)) {
          if(conn.first.str() == "\\WR_CLK") {
#ifndef NDEBUG
            for(auto &clk_chunk : conn.second.chunks()) {
              assert(clk_chunk.wire == clock);
            }
#endif
            sink_pid = LGRAPH_MEMOP_CLK;
            ss      = RTLIL::SigSpec(clock);
          } else if(conn.first.str() == "\\WR_ADDR") {
            for(uint32_t wrport = 0; wrport < wrports; wrport++) {
              Node_pin spin = entry_node.setup_sink_pin(LGRAPH_MEMOP_WRADDR(wrport));
              Node_pin dpin = create_join_operator(g, ss.extract(wrport * abits, abits));
              g->add_edge(dpin, spin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\WR_DATA", 8) == 0) {
            for(uint32_t wrport = 0; wrport < wrports; wrport++) {
              Node_pin spin = entry_node.setup_sink_pin(LGRAPH_MEMOP_WRDATA(wrport));
              Node_pin dpin = create_join_operator(g, ss.extract(wrport * size, size));
              g->add_edge(dpin, spin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\WR_EN", 6) == 0) {
            for(uint32_t wrport = 0; wrport < wrports; wrport++) {
              Node_pin spin = entry_node.setup_sink_pin(LGRAPH_MEMOP_WREN(wrport));
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
              Node_pin spin = entry_node.setup_sink_pin(LGRAPH_MEMOP_RDADDR(rdport));
              Node_pin dpin = create_join_operator(g, ss.extract(rdport * abits, abits));
              g->add_edge(dpin, spin);
            }
            continue;
          } else if(std::strncmp(conn.first.c_str(), "\\RD_EN", 6) == 0) {
            for(uint32_t rdport = 0; rdport < rdports; rdport++) {
              Node_pin spin = entry_node.setup_sink_pin(LGRAPH_MEMOP_RDEN(rdport));
              if (ss.extract(rdport, 1)[0].data == RTLIL::State::Sx) { // Yosys has Sx as enable sometimes, WEIRD. Fix it to 0
                auto node = g->create_node_const(0,1);
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

      Node_pin spin = entry_node.setup_sink_pin(sink_pid);
      if(ss.size() > 0) {
        Node_pin dpin = create_join_operator(g, ss);
        if(added_edges.find(XEdge(dpin,spin).get_compact()) != added_edges.end()) {
          // there are two edges from dpin to spin
          // this is not allowed in lgraph, add a join in between
          auto join_node = g->create_node(Join_Op, ss.size());
          g->add_edge(dpin, join_node.setup_sink_pin(0), ss.size());
          dpin = join_node.setup_driver_pin(0);
        }
        g->add_edge(dpin, spin);
        added_edges.insert(XEdge(dpin, spin).get_compact());
      }
    }

    if (sub_graph) {
      sub_graph->close(); // to avoid output leak
      sub_graph = nullptr;
    }

  }

  for(auto &kv : partially_assigned) {
    const RTLIL::Wire *wire = kv.first;

    auto     join_node = wire2pin[wire].get_node();
    Port_ID  join_pid  = 0;

    auto join_dpin = join_node.setup_driver_pin();
    set_bits_wirename(join_dpin, wire);

    Node_pin current;
    int       size    = 0;
    bool      first   = true;
    for(auto pin : kv.second) {
      if(first)
        first = false;
      else if((current.is_invalid() && !pin.is_invalid()) || (!current.is_invalid() && pin.is_invalid()) ||
              (!pin.is_invalid() && current != pin)) {
        Node_pin dpin;
        if (!current.is_invalid()) {
          dpin = current;
        }else{
          auto node = g->create_node_const(std::string(size, 'x'), size);
          dpin = node.setup_driver_pin();
        }

        assert(size < dpin.get_bits() || size % dpin.get_bits() == 0);
        int times = size < dpin.get_bits() ? 1 : size / dpin.get_bits();
        for(int i = 0; i < times; i++) {
          g->add_edge(dpin, join_node.setup_sink_pin(join_pid++));
        }
        size = 0;
      }
      size++;
      current = pin;
    }

    Node_pin dpin;
    if (!current.is_invalid()) {
      dpin = current;
    }else{
      auto node = g->create_node_const(std::string(size, 'x'), size);
      dpin = node.setup_driver_pin();
    }
    assert(size < dpin.get_bits() || size % dpin.get_bits() == 0);
    int times = size < dpin.get_bits() ? 1 : size / dpin.get_bits();
    for(int i = 0; i < times; i++) {
      g->add_edge(dpin, join_node.setup_sink_pin(join_pid++));
    }
  }

  // we need to connect global outputs to the cell that drives it
  for(auto wire : module->wires()) {
    if(wire->port_output && wire2pin.find(wire) != wire2pin.end()) {

      Node_pin spin = g->get_graph_output(&wire->name.c_str()[1]);
      Node_pin dpin = wire2pin[wire];
      g->add_edge(dpin, spin, wire->width);
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

		ct_all.setup(design);

    for(auto &it : design->modules_) {
      RTLIL::Module *module = it.second;
      std::string    name   = &module->name.c_str()[1];

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
      I(g);
      log("yosys2lg look_for_module_outputs pass for module %s:\n", module->name.c_str());
      look_for_module_outputs(module, path);
    }

    for(auto &it : design->modules_) {
      RTLIL::Module *module = it.second;
      log("yosys2lg NOT look_for_cell_outputs pass for module %s:\n", module->name.c_str());
        if(design->selected_module(it.first)) {
        look_for_cell_outputs(module, path);
        LGraph *g = process_module(module, path);

        g->sync();
        g->close();
      }

      wire2pin.clear();
      cell2node.clear();
      partially_assigned.clear();
      picks.clear();
    }
  }
} Yosys2lg_Pass;

PRIVATE_NAMESPACE_END
