//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

#include <string>

#include "inou.hpp"
#include "kernel/celltypes.h"
#include "kernel/sigtools.h"
#include "kernel/yosys.h"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"


// When true, the cell bits should have no effect (set to zero or large num for
// bitwidth to adjust should work too)
#define CELL_SIZE_IGNORE 1

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct GlobalPin {
  RTLIL::IdString      port;
  const RTLIL::Module *module;
  LGraph *             g;
  bool                 input;
};

static CellTypes                        ct_all;
static absl::flat_hash_set<size_t>      driven_signals;
static absl::flat_hash_set<std::string> cell_port_inputs;
static absl::flat_hash_set<std::string> cell_port_outputs;

typedef std::pair<const RTLIL::Wire *, int> Wire_bit;

// static std::map<const RTLIL::Wire *, GlobalPin> wire2gpin;
static absl::flat_hash_map<const RTLIL::Wire *, Node_pin>          wire2pin;
static absl::flat_hash_map<const RTLIL::Cell *, Node>              cell2node;  // Points to the exit_node for the block
static absl::flat_hash_map<const RTLIL::Wire *, Node_pin_iterator> partially_assigned;

static void look_for_wire(LGraph *g, const RTLIL::Wire *wire) {
  if (wire2pin.find(wire) != wire2pin.end())
    return;

  if (wire->port_input) {
    // log("input %s\n",wire->name.c_str());
    I(!wire->port_output);  // any bidirectional port?
    I(wire->name.c_str()[0] == '\\');
    Node_pin pin;
    if (g->is_graph_input(&wire->name.c_str()[1])) {
      pin = g->get_graph_input(&wire->name.c_str()[1]);
      I(pin.get_bits() == wire->width);
    } else {
      pin = g->add_graph_input(&wire->name.c_str()[1], wire->port_id, wire->width);
    }
    if (wire->start_offset) {
      pin.set_offset(wire->start_offset);
    }
    wire2pin[wire] = pin;
  } else if (wire->port_output) {
    // log("output %s\n",wire->name.c_str());
    I(wire->name.c_str()[0] == '\\');
    if (!g->is_graph_output(&wire->name.c_str()[1])) {
      g->add_graph_output(&wire->name.c_str()[1], wire->port_id, wire->width);
    }
    auto dpin = g->get_graph_output_driver_pin(&wire->name.c_str()[1]);
    I(dpin.get_bits() == wire->width);
    if (wire->start_offset) {
      dpin.set_offset(wire->start_offset);
    }

    wire2pin[wire] = dpin;
  }
}

static void look_for_module_io(RTLIL::Module *module, std::string_view path) {
#ifndef NDEBUG
  log("yosys2lg look_for_module_io pass for module %s:\n", module->name.c_str());
#endif
  auto  library = Graph_library::instance(path);
  auto *g       = library->try_find_lgraph(&module->name.c_str()[1]);
  I(g);

  for (auto &wire_iter : module->wires_) {
    look_for_wire(g, wire_iter.second);
  }
}

static Node_pin get_edge_pin(LGraph *g, const RTLIL::Wire *wire) {
  if (wire2pin.find(wire) == wire2pin.end()) {
    look_for_wire(g, wire);
  }

  if (wire2pin.find(wire) != wire2pin.end()) {
    if (wire->width != (int)wire2pin[wire].get_bits()) {
      fmt::print("w_name:{} w_w:{} | pid:{} bits:{}\n",
                 wire->name.c_str(),
                 wire->width,
                 wire2pin[wire].get_pid(),
                 wire2pin[wire].get_bits());
      I(false);  // WHY?
      wire2pin[wire].set_bits(wire->width);
      I(wire->width == wire2pin[wire].get_bits());
    }
    return wire2pin[wire];
  }

  I(!wire->port_input);  // Added before at look_for_wire
  I(!wire->port_output);

  auto node = g->create_node(Ntype_op::Or, wire->width); // Just a placeholder/connect gate

  wire2pin[wire] = node.setup_driver_pin();

  return wire2pin[wire];
}

static Node_pin resolve_constant(LGraph *g, const std::vector<RTLIL::State> &data) {
  // this is a vector of RTLIL::State
  // S0 => 0
  // S1 => 1
  // Sx => x
  // Sz => high z
  // Sa => don't care (not sure what is the diff between Sa and Sx
  // Sm => used internally by Yosys

  uint64_t    value = 0;
  std::string val;

  uint32_t current_bit = 0;
  bool     u64_const   = true;
  for (auto &b : data) {
    switch (b) {
      case RTLIL::S0: val = absl::StrCat("0", val); break;
      case RTLIL::S1:
        val = absl::StrCat("1", val);
        value += 1 << current_bit;
        break;
      case RTLIL::Sz:
      case RTLIL::Sa:
        val       = absl::StrCat("z", val);
        u64_const = false;
        break;
      case RTLIL::Sx:
        val       = absl::StrCat("x", val);
        u64_const = false;
        break;
      default: I(false);
    }
    current_bit++;
  }

  if (u64_const && data.size() <= 63) {
    auto node = g->create_node_const(Lconst(value, data.size()));
    I(node.get_driver_pin().get_bits() <= data.size());
    return node.setup_driver_pin();
  }

  val = absl::StrCat("0b", val, "u", (int)data.size(), "bits");
  // fmt::print("val:{} prp:{}\n", val, Lconst(val).to_pyrope());
  auto node = g->create_node_const(Lconst(val));
  return node.setup_driver_pin();
}

class Pick_ID {
  // friend constexpr bool operator==(const Pick_ID &lhs, const Pick_ID &rhs);
public:
  Node_pin driver;
  int      offset;
  int      width;

  Pick_ID(Node_pin driver, int offset, int width) : driver(driver), offset(offset), width(width) {}

  template <typename H>
  friend H AbslHashValue(H h, const Pick_ID &s) {
    return H::combine(std::move(h), s.driver.get_compact(), s.offset, s.width);
  };
};

bool operator==(const Pick_ID &lhs, const Pick_ID &rhs) {
  return lhs.driver == rhs.driver && lhs.width == rhs.width && lhs.offset == rhs.offset;
}

static absl::flat_hash_map<Pick_ID, Node_pin> picks;  // NODE, not flat to preserve pointer stability

static Node_pin create_pick_operator(const Node_pin &wide_dpin, int offset, int width) {
  if (offset == 0 && (int)wide_dpin.get_bits() == width)
    return wide_dpin;

  Pick_ID pick_id(wide_dpin, offset, width);
  if (picks.find(pick_id) != picks.end()) {
    return picks.at(pick_id);
  }

  // Pick(a,width,offset):
  //   x = a>>offset
  //   y = x & ((1<<offset)-1)

  auto *lg = wide_dpin.get_class_lgraph();
  auto and_node = lg->create_node(Ntype_op::And, width);

  if (offset) {
    auto shr_node = lg->create_node(Ntype_op::SRA, width);
    shr_node.setup_sink_pin("a").connect_driver(wide_dpin);
    shr_node.setup_sink_pin("b").connect_driver(lg->create_node_const(offset));

    and_node.connect_sink(shr_node);
  }else{
    and_node.connect_sink(wide_dpin);
  }

  and_node.connect_sink(lg->create_node_const(((Lconst(1)<<Lconst(width))-1)));
  auto tposs_node = lg->create_node(Ntype_op::Tposs, width+1);

  // WARNING: increase the out wire. This is not the most efficient, but it
  // simplifies the code gen in yosys_dump. Otherwise, it needs to track when a
  // wire is "unsigned", and then treat inputs of the cell using it as
  // unsigned.

  tposs_node.connect_sink(and_node);

  auto dpin = tposs_node.setup_driver_pin();

  picks.insert(std::make_pair(pick_id, dpin));

  return dpin;
}

static Node_pin create_pick_operator(LGraph *g, const RTLIL::Wire *wire, int offset, int width) {
  if (wire->width == width && offset == 0)
    return get_edge_pin(g, wire);

  return create_pick_operator(get_edge_pin(g, wire), offset, width);
}

static Node_pin create_pick_concat_dpin(LGraph *g, const RTLIL::SigSpec &ss, bool is_signed) {
  std::vector<Node_pin> inp_pins;
  I(ss.chunks().size() != 0);

  for (auto &chunk : ss.chunks()) {
    if (chunk.wire == nullptr) {
      inp_pins.emplace_back(resolve_constant(g, chunk.data));
    } else {
      inp_pins.push_back(create_pick_operator(g, chunk.wire, chunk.offset, chunk.width));
    }
  }

  Node_pin dpin;
  if (inp_pins.size() > 1) {
    auto or_node = g->create_node(Ntype_op::Or, ss.size());

    int offset = 0;
    auto chunks=ss.chunks();
    for (int i=0;i<chunks.size();++i) {

      Node_pin inp_pin;

      if ((i+1) == chunks.size()) { // last pin
        inp_pin = inp_pins[i];
      }else{
        auto tposs_node = g->create_node(Ntype_op::Tposs, inp_pins[i].get_bits()+1);
        tposs_node.connect_sink(inp_pins[i]);
        inp_pin = tposs_node.setup_driver_pin();
      }

      if (offset==0) {
        or_node.connect_sink(inp_pin);
      }else{
        auto shl_node = g->create_node(Ntype_op::SHL, ss.size());

        shl_node.setup_sink_pin("a").connect_driver(inp_pin);
        shl_node.setup_sink_pin("b").connect_driver(g->create_node_const(offset));

        or_node.connect_sink(shl_node);
      }

      assert(chunks[i].width <= inp_pin.get_bits()); // there may be Tposs increasing size
      offset += chunks[i].width;
    }
    dpin = or_node.setup_driver_pin();
  } else {
    dpin = inp_pins[0];  // single wire, do not create pick_concat
  }

  return dpin;
}

static Node_pin get_dpin(LGraph *g, const RTLIL::Cell *cell, RTLIL::IdString name) {

  if (cell->hasParam(name)) {
    const RTLIL::Const &v = cell->getParam(name);
    if (v.is_fully_def() && v.size() <60) {
      return g->create_node_const(v.as_int(), v.size()).setup_driver_pin();
    }
    std::string v_str("0b");
    v_str += v.as_string();
    return g->create_node_const(v_str).setup_driver_pin();
  }
  bool is_signed = false;
  if (name == ID::A && cell->hasParam(ID::A_SIGNED)) {
      is_signed = cell->getParam(ID::A_SIGNED).as_bool();
  }else if (name == ID::B && cell->hasParam(ID::B_SIGNED)) {
    is_signed = cell->getParam(ID::B_SIGNED).as_bool();
  }

  return create_pick_concat_dpin(g, cell->getPort(name), is_signed);
}

static bool is_yosys_output(const std::string &idstring) {
  return idstring == "\\Y" || idstring == "\\Q" || idstring == "\\RD_DATA";
}

static void connect_all_inputs(const Node_pin &spin, const RTLIL::Cell *cell) {
  assert(spin.is_sink());
  for (auto &conn : cell->connections()) {
    RTLIL::SigSpec ss = conn.second;
    if (ss.size() == 0)
      continue;
    if (is_yosys_output(conn.first.c_str()))
      continue;  // Just go over the inputs
    bool is_signed = false;
    if (conn.first == ID::A && cell->hasParam(ID::A_SIGNED)) {
      is_signed = cell->getParam(ID::A_SIGNED).as_bool();
    }else if (conn.first == ID::B && cell->hasParam(ID::B_SIGNED)) {
      is_signed = cell->getParam(ID::B_SIGNED).as_bool();
    }

    spin.connect_driver(create_pick_concat_dpin(spin.get_class_lgraph(), ss, is_signed));
  }
}

static const std::regex dc_name("(\\|/)n(\\d\\+)|^N(\\d\\+)");

static void set_bits_wirename(Node_pin &pin, const RTLIL::Wire *wire) {
  if (!wire)
    return;

  if (!wire->port_input && !wire->port_output) {
    // we don't want to keep internal abc/yosys wire names
    // TODO: is there a more efficient and complete way of doing this?
    if (wire->name.c_str()[0] != '$'
        && wire->name.str().rfind("\\lg_") != 0
        // dc generated names
        && !std::regex_match(wire->name.str(), dc_name)

        // skip chisel generated names
        && wire->name.str().rfind("\\_GEN_") != 0 && wire->name.str().rfind("\\_T_") != 0) {
      pin.set_name(&wire->name.c_str()[1]);
    }
  }

  if (!pin.is_graph_io()) {
    pin.set_bits(wire->width);
  }
}

static Node resolve_memory(LGraph *g, RTLIL::Cell *cell) {
  auto node = g->create_node(Ntype_op::Memory);

  uint32_t rdports = cell->getParam(ID::RD_PORTS).as_int();
  uint32_t bits    = cell->getParam(ID::WIDTH).as_int();

#if 1
  fmt::print("Memory pending\n");
#else

  for (uint32_t rdport = 0; rdport < rdports; rdport++) {
    RTLIL::SigSpec ss   = cell->getPort("\\RD_DATA").extract(rdport * bits, bits);
    auto           dpin = node.setup_driver_pin(rdport);
    dpin.set_bits(bits);

    uint32_t offset = 0;
    for (auto &chunk : ss.chunks()) {
      const RTLIL::Wire *wire = chunk.wire;

      // disconnected output
      if (wire == 0)
        continue;

      if (chunk.width == wire->width) {
        if (wire2pin.find(wire) != wire2pin.end()) {
          const auto &or_dpin = wire2pin[wire];
          if (or_dpin.is_graph_output()) {
            g->add_edge(dpin, or_dpin.get_sink_from_output());
          } else {
            auto or_node = or_dpin.get_node();
            I(or_node.get_type_op() == Ntype_op::Or);
            I(!or_node.has_inputs());

            auto or_spin = or_node.setup_sink_pin();
            or_spin.connect_driver(dpin);
            I(or_dpin.get_bits() == wire->width);
          }
        } else if (chunk.width == ss.size()) {
          // output port drives a single wire
          wire2pin[wire] = dpin;
          set_bits_wirename(dpin, wire);
        } else {
          // output port drives multiple wires
          Node_pin pick_pin = create_pick_operator(dpin, offset, chunk.width);
          wire2pin[wire]     = pick_pin;
          set_bits_wirename(pick_pin, wire);
        }
        offset += chunk.width;
      } else {
        if (partially_assigned.find(wire) == partially_assigned.end()) {
          partially_assigned[wire].resize(wire->width);

          I(wire2pin.find(wire) == wire2pin.end());
          auto node = g->create_node(Ntype_op::Or, wire->width);

          wire2pin[wire] = node.setup_driver_pin();
        }
        dpin.set_bits(ss.size());

        auto &src_pin = create_pick_operator(dpin, offset, chunk.width);
        offset += chunk.width;
        for (int i = 0; i < chunk.width; i++) {
          I((size_t)(chunk.offset + i) < partially_assigned[wire].size());
          partially_assigned[wire][chunk.offset + i] = src_pin;
        }
      }
    }
  }
#endif

  return node;
}

static bool is_black_box_output(const RTLIL::Module *module, const RTLIL::Cell *cell, const RTLIL::IdString &port_name) {
  const RTLIL::Wire *wire = cell->getPort(port_name).chunks()[0].wire;

  // constant
  if (!wire)
    return false;

  // WARNING: It can be forward: NOT TRUE global output
  // if(wire->port_output)
  //  return true;

  // global input
  if (wire->port_input)
    return false;

  std::string cell_port = absl::StrCat(cell->type.str(), "_:_", port_name.str());

  if (cell_port_outputs.find(cell_port) != cell_port_outputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an output\n", cell->name.c_str(), port_name.c_str());
    return true;
  }
  if (cell_port_inputs.find(cell_port) != cell_port_inputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an input\n", cell->name.c_str(), port_name.c_str());
    return false;
  }

  ::LGraph::error(
      "Could not find a definition for module {}, treating as a blackbox but could not determine whether {} is an output",
      cell->type.str(),
      port_name.str());

  log_error("output unknown port %s at module %s cell %s\n", port_name.c_str(), module->name.c_str(), cell->type.c_str());
  I(false);  // TODO: is it possible to resolve this case?
  return false;
}

static bool is_black_box_input(const RTLIL::Module *module, const RTLIL::Cell *cell, const RTLIL::IdString &port_name) {
  const RTLIL::Wire *wire = cell->getPort(port_name).chunks()[0].wire;

  // constant
  if (!wire)
    return true;

  // WARNING: It can be used for output in another cell global output
  // if(wire->port_output)
  // return false;

  // global input
  if (wire->port_input)
    return true;

  std::string cell_port = absl::StrCat(cell->type.str(), "_:_", port_name.str());

  // Opposite of is_black_box_output
  if (cell_port_outputs.find(cell_port) != cell_port_outputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an output\n", cell->name.c_str(), port_name.c_str());
    return false;
  }
  if (cell_port_inputs.find(cell_port) != cell_port_inputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an input\n", cell->name.c_str(), port_name.c_str());
    return true;
  }

  ::LGraph::error(
      "Could not find a definition for module {}, treating as a blackbox but could not determine whether {} is an input",
      cell->type.str(),
      port_name.str());

  log_error("input unknown port %s at module %s cell %s\n", port_name.c_str(), module->name.c_str(), cell->type.c_str());
  I(false);  // TODO: is it possible to resolve this case?
  return false;
}

static void setup_cell_driver_pins(RTLIL::Module *module, LGraph *g) {
  // log("yosys2lg setup_cell_driver_pins pass for module %s:\n", module->name.c_str());

  for (auto cell : module->cells()) {
    if (cell->type == "$mem") {
      cell2node[cell] = resolve_memory(g, cell);
      continue;
    }

    cell2node[cell] = g->create_node();
    auto &node      = cell2node[cell];

    Sub_node *sub = nullptr;

    if (cell->type.c_str()[0] == '\\' || strncmp(cell->type.c_str(), "$paramod\\", 9) == 0) {  // sub_cell type
      std::string_view mod_name(&(cell->type.c_str()[1]));

      sub = &g->ref_library()->setup_sub(mod_name);
    }

    for (const auto &conn : cell->connections()) {
      if (sub) {
        std::string pin_name(&(conn.first.c_str()[1]));

        if (isdigit(pin_name[0])) {
          // hardcoded pin position
          int pos = atoi(pin_name.c_str());

          if (!sub->has_instance_pin(pos)) {
            if (cell->output(conn.first)) {
              sub->add_pin(pin_name, Sub_node::Direction::Output, pos);
            } else if (cell->input(conn.first)) {
              sub->add_pin(pin_name, Sub_node::Direction::Input, pos);
            } else if (conn.second.is_fully_undef()) {
              sub->add_pin(pin_name, Sub_node::Direction::Output, pos);
            } else if (conn.second.is_fully_const()) {
              sub->add_pin(pin_name, Sub_node::Direction::Input, pos);
            } else {
              bool is_input  = false;
              bool is_output = false;
              for (auto &chunk : conn.second.chunks()) {
                const RTLIL::Wire *wire = chunk.wire;
                if (wire->port_input)
                  is_input = true;
                // WARNING: Not always if (wire->port_output) is_output = true;
                if (driven_signals.count(wire->hash()) != 0) {
                  is_input = true;
                }
              }
              if (is_input && !is_output) {
                sub->add_pin(pin_name, Sub_node::Direction::Input, pos);
              } else if (!is_input && is_output) {
                sub->add_pin(pin_name, Sub_node::Direction::Output, pos);
              } else {
                fprintf(stderr,
                        "Warning: impossible to figure out direction in module %s cell type %s pin_name to %s\n",
                        module->name.c_str(),
                        cell->type.c_str(),
                        pin_name.c_str());
                continue;
              }
            }
          }
          auto io_pin = sub->get_io_pin_from_graph_pos(pos);
          if (io_pin.dir == Sub_node::Direction::Input)
            continue;

#ifndef NDEBUG
          printf("module %s cell type %s has output pin_name %s\n", module->name.c_str(), cell->type.c_str(), pin_name.c_str());
#endif
        } else {
          if (!sub->has_pin(pin_name)) {
            if (cell->input(conn.first) || is_black_box_input(module, cell, conn.first))
              sub->add_input_pin(pin_name);
            else if (cell->output(conn.first) || is_black_box_output(module, cell, conn.first))
              sub->add_output_pin(pin_name);
          }

          if (!sub->has_pin(pin_name) || sub->is_input(pin_name))
            continue;

#ifndef NDEBUG
          printf("module %s submodule %s has pin_name %s\n", module->name.c_str(), cell->type.c_str(), pin_name.c_str());
#endif
        }

        node.set_type_sub(sub->get_lgid());
      }else{
        node.set_type(Ntype_op::Or); // it will be updated later. Just a generic 1 output cell

        if (cell->input(conn.first))
          continue;
      }

      Node_pin driver_pin;
      if (node.is_type_sub()) {
        std::string pin_name(&(conn.first.c_str()[1]));
        driver_pin = node.setup_driver_pin(pin_name);
      }else{
        driver_pin = node.setup_driver_pin();
      }

      const RTLIL::SigSpec ss = conn.second;

      if (ss.chunks().size() > 0)
        driver_pin.set_bits(ss.size());

      uint32_t offset = 0;
      for (auto &chunk : ss.chunks()) {
        const RTLIL::Wire *wire = chunk.wire;

        // disconnected output
        if (wire == 0)
          continue;

        //fmt::print("dpin:{} bits:{} wire:{}\n", driver_pin.debug_name(), driver_pin.get_bits(), wire->name.str());

        if (chunk.width == wire->width) {
          if (wire2pin.find(wire) != wire2pin.end()) {
            auto pin2 = wire2pin[wire];
            log("io wire %s from module %s cell type %s (%s vs %s)\n",
                wire->name.c_str(),
                module->name.c_str(),
                cell->type.c_str(),
                driver_pin.debug_name().c_str(),
                pin2.debug_name().c_str());
            if (wire->port_output) {
              auto io_spin = g->get_graph_output(&wire->name.c_str()[1]);
              if (driver_pin.get_bits()==wire->width) {
                io_spin.connect_driver(driver_pin);
              }else{
                auto and_node = g->create_node(Ntype_op::And, wire->width);
                and_node.connect_sink(driver_pin);
                and_node.connect_sink(g->create_node_const((Lconst(1)<<Lconst(wire->width))-1));

                io_spin.connect_driver(and_node);
              }
            }
          } else if (chunk.width == ss.size()) {
            I(driver_pin.get_bits()==wire->width); // bits should still not be set
            I(driver_pin.get_bits()==ss.size());
            // output port drives a single wire
            wire2pin[wire] = driver_pin;
            set_bits_wirename(driver_pin, wire);
          } else {
            I((chunk.width+offset)<=driver_pin.get_bits());
            // output port drives multiple wires
            Node_pin pick_pin = create_pick_operator(driver_pin, offset, chunk.width);
            wire2pin[wire]     = pick_pin;
            set_bits_wirename(pick_pin, wire);
          }
          offset += chunk.width;

        } else {
          if (partially_assigned.find(wire) == partially_assigned.end()) {
            partially_assigned[wire].resize(wire->width);

            if (wire2pin.find(wire) != wire2pin.end()) {
              printf("partial wire %s from module %s cell type %s (switching to partial)\n",
                     wire->name.c_str(),
                     module->name.c_str(),
                     cell->type.c_str());
            }
            auto node      = g->create_node(Ntype_op::Or, wire->width);
            wire2pin[wire] = node.setup_driver_pin();
          }
          driver_pin.set_bits(ss.size());

          auto src_pin = create_pick_operator(driver_pin, offset, chunk.width);
          offset += chunk.width;
          for (int i = 0; i < chunk.width; i++) {
            I((size_t)(chunk.offset + i) < partially_assigned[wire].size());
            partially_assigned[wire][chunk.offset + i] = src_pin;
          }
        }
      }
    }
  }
}

static void process_assigns(RTLIL::Module *module, LGraph *g) {
  for (const auto &conn : module->connections()) {
    const RTLIL::SigSpec lhs = conn.first;
    const RTLIL::SigSpec rhs = conn.second;

    int offset = 0;
    for (auto &chunk : lhs.chunks()) {
      const RTLIL::Wire *lhs_wire = chunk.wire;

      // printf("Assignment to %s\n", lhs_wire->name.c_str());

      if (lhs_wire->port_input) {
        log_error("Assignment to input port %s\n", lhs_wire->name.c_str());
      } else if (chunk.width == lhs_wire->width) {
        if (chunk.width == 0)
          continue;

        if (lhs_wire->port_output) {
          Node_pin dpin = create_pick_concat_dpin(g, rhs.extract(offset, chunk.width), false);
          Node_pin spin = g->get_graph_output(&lhs_wire->name.c_str()[1]);
          g->add_edge(dpin, spin, lhs_wire->width);
        } else if (wire2pin.find(lhs_wire) == wire2pin.end()) {
          Node_pin dpin      = create_pick_concat_dpin(g, rhs.extract(offset, chunk.width), false);
          wire2pin[lhs_wire] = dpin;
        } else {
          auto dpin = wire2pin[lhs_wire];
          I(wire2pin[lhs_wire].get_bits() == lhs_wire->width);
        }

        offset += chunk.width;
      } else {
        if (partially_assigned.find(lhs_wire) == partially_assigned.end()) {
          partially_assigned[lhs_wire].resize(lhs_wire->width);

          if (lhs_wire->port_output || wire2pin.find(lhs_wire) == wire2pin.end()) {
            auto or_node       = g->create_node(Ntype_op::Or);
            wire2pin[lhs_wire] = or_node.setup_driver_pin();
          }

          wire2pin[lhs_wire].set_bits(lhs_wire->width);
        }

        if (chunk.width == 0)
          continue;
        Node_pin dpin = create_pick_concat_dpin(g, rhs.extract(offset, chunk.width), false);

        offset += chunk.width;
        for (int i = 0; i < chunk.width; i++) {
          I((size_t)(chunk.offset + i) < partially_assigned[lhs_wire].size());
          partially_assigned[lhs_wire][chunk.offset + i] = dpin;
        }
      }
    }
  }
}

static uint32_t get_input_size(const RTLIL::Cell *cell) {
  assert(cell->known()); // use only in based yosys cells (known type)

  uint32_t max_input = 0;
  for (const auto &conn : cell->connections()) {
    if (cell->output(conn.first))
      continue;

    const RTLIL::SigSpec ss = conn.second;
    if (ss.chunks().size() > max_input)
      max_input = ss.size();
  }

  return max_input;
}

static uint32_t get_output_size(const RTLIL::Cell *cell) {
  assert(cell->known()); // use only in based yosys cells (known type)

  if (cell->hasParam(ID::Y_WIDTH))
    return cell->getParam(ID::Y_WIDTH).as_int();

  for (const auto &conn : cell->connections()) {
    if (cell->input(conn.first))
      continue;

    const RTLIL::SigSpec ss = conn.second;
    if (ss.chunks().size() > 0)
      return ss.size();
  }

  assert(0);

  return 0; // must be determined later?
}

static void connect_comparator(Ntype_op op, Node &exit_node, const RTLIL::Cell *cell) {
  exit_node.set_type(op, 1);
  // In yosys, the comparater output can be 1 or 0 or ..01 or ...00. bitwidth in LG allows to ignore this
  // assert(cell->getParam(ID::Y_WIDTH).as_int() == 1);

  auto *g = exit_node.get_class_lgraph();

  bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
  bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();
  auto a_dpin = get_dpin(g, cell,ID::A);
  auto b_dpin = get_dpin(g, cell,ID::B);

  if (a_sign) {
    exit_node.setup_sink_pin("A").connect_driver(a_dpin);
  }else{
    auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
    auto a_tposs = g->create_node(Ntype_op::Tposs, a_bits+1);
    a_tposs.connect_sink(a_dpin);
    exit_node.setup_sink_pin("A").connect_driver(a_tposs);
  }
  std::string_view b("B");
  if (op == Ntype_op::EQ)
    b="A";

  if (b_sign) {
    exit_node.setup_sink_pin(b).connect_driver(b_dpin);
  }else{
    auto b_bits = cell->getParam(ID::B_WIDTH).as_int();
    auto b_tposs = g->create_node(Ntype_op::Tposs, b_bits+1);
    b_tposs.connect_sink(b_dpin);
    exit_node.setup_sink_pin(b).connect_driver(b_tposs);
  }
}

// this function is called for each module in the design
static void process_module(RTLIL::Module *module, LGraph *g) {
#ifndef NDEBUG
  log("yosys2lg pass for module %s:\n", module->name.c_str());
  printf("process_module %s\n", module->name.c_str());
#endif

  process_assigns(module, g);

#ifndef NDEBUG
  log("INOU/YOSYS processing module %s, ncells %lu, nwires %lu\n",
      module->name.c_str(),
      module->cells().size(),
      module->wires().size());
#endif

  for (auto cell : module->cells()) {
    // log("Looking for cell %s:\n", cell->type.c_str());

    I(cell2node.find(cell) != cell2node.end());
    Node exit_node  = cell2node[cell];

    //--------------------------------------------------------------
    if (std::strncmp(cell->type.c_str(), "$and", 4) == 0) {
      exit_node.set_type(Ntype_op::And, get_output_size(cell));

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      assert(!a_sign || !b_sign || (a_bits==y_bits && b_bits==y_bits)); // FIXME: like in xor when case shows

      connect_all_inputs(exit_node.setup_sink_pin(), cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$reduce_and", 11) == 0 || std::strncmp(cell->type.c_str(), "$logic_and", 10)==0) {
      exit_node.set_type(Ntype_op::Rand, 1);

      connect_all_inputs(exit_node.setup_sink_pin(), cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$not", 4) == 0) {
      assert(get_input_size(cell) == get_output_size(cell));
      exit_node.set_type(Ntype_op::Not, get_output_size(cell));

      connect_all_inputs(exit_node.setup_sink_pin(), cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$logic_not", 10) == 0) {
      auto entry_node = g->create_node(Ntype_op::Ror, 1);
      exit_node.set_type(Ntype_op::Not, 1);
      g->add_edge(entry_node.setup_driver_pin(), exit_node.setup_sink_pin());

      connect_all_inputs(entry_node.setup_sink_pin(), cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$or", 3) == 0) {
      exit_node.set_type(Ntype_op::Or, get_output_size(cell));

      auto a_dpin = get_dpin(g, cell, ID::A);
      auto b_dpin = get_dpin(g, cell, ID::B);

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      if (!a_sign || !b_sign || (a_bits==y_bits && b_bits==y_bits)) { // Common case
        connect_all_inputs(exit_node.setup_sink_pin(), cell);
      }else{
        if (a_sign) {
          exit_node.connect_sink(a_dpin);
        }else{
          auto tposs_node = g->create_node(Ntype_op::Tposs, y_bits);
          tposs_node.connect_sink(a_dpin);
          exit_node.connect_sink(tposs_node);
        }
        if (b_sign) {
          exit_node.connect_sink(b_dpin);
        }else{
          auto tposs_node = g->create_node(Ntype_op::Tposs, y_bits);
          tposs_node.connect_sink(b_dpin);
          exit_node.connect_sink(tposs_node);
        }
      }

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$reduce_or", 10) == 0 || std::strncmp(cell->type.c_str(), "$reduce_bool", 12) == 0 || std::strncmp(cell->type.c_str(), "$logic_or", 9)==0) {
      exit_node.set_type(Ntype_op::Ror, 1);

      connect_all_inputs(exit_node.setup_sink_pin(), cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$xor", 4) == 0) {
      exit_node.set_type(Ntype_op::Xor, get_output_size(cell));

      auto a_dpin = get_dpin(g, cell, ID::A);
      auto b_dpin = get_dpin(g, cell, ID::B);

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      if (!a_sign || !b_sign || (a_bits==y_bits && b_bits==y_bits)) { // Common case
        connect_all_inputs(exit_node.setup_sink_pin(), cell);
      }else{
        if (a_sign) {
          exit_node.connect_sink(a_dpin);
        }else{
          auto tposs_node = g->create_node(Ntype_op::Tposs, y_bits);
          tposs_node.connect_sink(a_dpin);
          exit_node.connect_sink(tposs_node);
        }
        if (b_sign) {
          exit_node.connect_sink(b_dpin);
        }else{
          auto tposs_node = g->create_node(Ntype_op::Tposs, y_bits);
          tposs_node.connect_sink(b_dpin);
          exit_node.connect_sink(tposs_node);
        }
      }

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$reduce_xor", 11) == 0) {
      assert(get_output_size(cell)==1); // reduce xor

      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto a_dpin = get_dpin(g, cell, ID::A);

      if (a_bits==1) { // pass it through
        exit_node.set_type(Ntype_op::And,1);
        exit_node.connect_driver(a_dpin);
      }else{
        exit_node.set_type(Ntype_op::And,1);

        auto xor_node = g->create_node(Ntype_op::Xor,1);
        xor_node.connect_sink(a_dpin);

        for(int i=1;i<a_bits;++i) {
          auto sra_node = g->create_node(Ntype_op::SRA, 1);

          sra_node.setup_sink_pin("a").connect_driver(a_dpin);
          sra_node.setup_sink_pin("b").connect_driver(g->create_node_const(i));

          xor_node.connect_sink(sra_node);
        }

        exit_node.connect_sink(xor_node);
        exit_node.connect_sink(g->create_node_const(1,1));
      }
#if 0
      // C++ code for log2 parity compute
      int parity(uint64_t num) {  // log n complexity to compute parity
        int max = 16; // start with 32bit number

        while (max) {
          num = num ^ (num >> max);
          max = max>>1;
        }

        return num&1;
      }
#endif

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$xnor", 5) == 0) {
      auto size = get_output_size(cell);
      assert(get_input_size(cell)==size);

      auto entry_node = g->create_node(Ntype_op::Xor, size);
      exit_node.set_type(Ntype_op::Not, size);
      entry_node.connect_driver(exit_node);

      connect_all_inputs(entry_node.setup_sink_pin(), cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$reduce_xnor", 11) == 0) {
      assert(get_output_size(cell)==1); // reduce xor

      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto a_dpin = get_dpin(g, cell, ID::A);

      if (a_bits==1) { // pass it through
        exit_node.set_type(Ntype_op::Not,1);
        exit_node.connect_driver(a_dpin);
      }else{
        exit_node.set_type(Ntype_op::Not,1);
        auto and_node = g->create_node(Ntype_op::And,1);
        and_node.connect_sink(g->create_node_const(1,1));

        auto xor_node = g->create_node(Ntype_op::Xor,1);
        xor_node.connect_sink(a_dpin);

        for(int i=1;i<a_bits;++i) {
          auto sra_node = g->create_node(Ntype_op::SRA, 1);

          sra_node.setup_sink_pin("a").connect_driver(a_dpin);
          sra_node.setup_sink_pin("b").connect_driver(g->create_node_const(i));

          xor_node.connect_sink(sra_node);
        }

        and_node.connect_sink(xor_node);
        exit_node.connect_sink(and_node);
      }

      connect_all_inputs(exit_node.setup_sink_pin(), cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$dff", 4) == 0
            || std::strncmp(cell->type.c_str(), "$dffe", 5) == 0
            || std::strncmp(cell->type.c_str(), "$dffsr", 6) == 0) {
      exit_node.set_type(Ntype_op::Sflop, get_output_size(cell));

      RTLIL::Const clk_polarity = cell->getParam(ID::CLK_POLARITY);
      for (int i = 1; i < clk_polarity.size(); i++) {
        I(clk_polarity[0] == clk_polarity[i]);
      }
      if (clk_polarity.size() && clk_polarity[0] != RTLIL::S1) {
        exit_node.setup_sink_pin("posclk").connect_driver(g->create_node_const(0));
      }

      bool wants_negreset=false;
      if (cell->hasParam(ID::EN)) {
        if (cell->hasParam(ID::EN_POLARITY)) {
          wants_negreset = cell->getParam(ID::EN_POLARITY).as_int() == 0;
        }
        exit_node.setup_sink_pin("enable").connect_driver(get_dpin(g, cell, ID::EN));
      }

      if (cell->hasParam(ID::CLR)) {
        if (cell->hasParam(ID::CLR_POLARITY)) {
          bool wants_negreset2 = cell->getParam(ID::CLR_POLARITY).as_int() == 0;
          assert(wants_negreset2 == wants_negreset); // both agree
        }
        exit_node.setup_sink_pin("reset").connect_driver(get_dpin(g, cell, ID::CLR));
      }
      if (wants_negreset)
        exit_node.setup_sink_pin("negreset").connect_driver(g->create_node_const(1));

      assert(!cell->hasParam(ID::SET)); // FIXME: active low not supported in LG (add mux before sflop)

      exit_node.setup_sink_pin("clock").connect_driver(get_dpin(g, cell,ID::CLK));
      exit_node.setup_sink_pin("din").connect_driver(get_dpin(g, cell,ID::D));
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$adff", 4) == 0) {
      exit_node.set_type(Ntype_op::Aflop, get_output_size(cell));

      if (cell->getParam(ID::ARST_POLARITY)[0] != RTLIL::S1)
        exit_node.setup_sink_pin("negreset").connect_driver(g->create_node_const(1));

      if (!cell->getParam(ID::CLK_POLARITY).as_bool())
        exit_node.setup_sink_pin("posclk").connect_driver(g->create_node_const(0));

      exit_node.setup_sink_pin("clock").connect_driver(get_dpin(g, cell,ID::CLK));
      exit_node.setup_sink_pin("reset").connect_driver(get_dpin(g, cell,ID::ARST));
      if (cell->hasParam(ID::ARST_VALUE)) {
        const auto &v = cell->getParam(ID::ARST_VALUE);
        if (!v.is_fully_zero())
          exit_node.setup_sink_pin("initial").connect_driver(get_dpin(g, cell, ID::ARST_VALUE));
      }
      exit_node.setup_sink_pin("din").connect_driver(get_dpin(g, cell,ID::D));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$dlatch", 7) == 0) {
      exit_node.set_type(Ntype_op::Latch, get_output_size(cell));

      if (cell->hasParam(ID::EN_POLARITY) && cell->getParam(ID::EN_POLARITY)[0] != RTLIL::S1)
        exit_node.setup_sink_pin("posclk").connect_driver(g->create_node_const(0));

      exit_node.setup_sink_pin("din").connect_driver(get_dpin(g, cell,ID::D));
      exit_node.setup_sink_pin("enable").connect_driver(get_dpin(g, cell, ID::EN));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$lt", 3) == 0) {
      connect_comparator(Ntype_op::LT, exit_node, cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$ge", 3) == 0) {
      Node node = g->create_node();
      connect_comparator(Ntype_op::LT, node, cell);

      exit_node.set_type(Ntype_op::Not, 1);
      exit_node.connect_sink(node);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$gt", 3) == 0) {
      connect_comparator(Ntype_op::GT, exit_node, cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$le", 3) == 0) {
      Node node = g->create_node();
      connect_comparator(Ntype_op::GT, node, cell);

      exit_node.set_type(Ntype_op::Not, 1);
      exit_node.connect_sink(node);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$eq", 3) == 0) {
      connect_comparator(Ntype_op::EQ, exit_node, cell);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$ne", 3) == 0) {
      Node node = g->create_node();
      connect_comparator(Ntype_op::EQ, node, cell);

      exit_node.set_type(Ntype_op::Not, 1);
      exit_node.connect_sink(node);
    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$mux", 4) == 0) {
      exit_node.set_type(Ntype_op::Mux, get_output_size(cell));

      exit_node.setup_sink_pin("0").connect_driver(get_dpin(g, cell, "\\S"));
      exit_node.setup_sink_pin("1").connect_driver(get_dpin(g, cell, ID::A));
      exit_node.setup_sink_pin("2").connect_driver(get_dpin(g, cell, ID::B));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$add", 4) == 0
            || std::strncmp(cell->type.c_str(), "$sub", 4) == 0) {

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      auto y_bits = get_output_size(cell);

#ifdef CELL_SIZE_IGNORE
      exit_node.set_type(Ntype_op::And, y_bits);
      auto sum_node = g->create_node(Ntype_op::Sum, y_bits);

      exit_node.connect_sink(sum_node);
      exit_node.connect_sink(g->create_node_const((Lconst(1)<<Lconst(y_bits))-1));
#else
      exit_node.set_type(Ntype_op::Sum, y_bits);
      auto sum_node = exit_node;
#endif

      std::string_view b{"A"};
      if (std::strncmp(cell->type.c_str(), "$sub", 4) == 0)
        b = "B";

      auto a_dpin = get_dpin(g, cell, ID::A);
      auto b_dpin = get_dpin(g, cell, ID::B);

      if ((a_sign && b_sign) || (a_bits==b_bits)) {
        sum_node.setup_sink_pin("A").connect_driver(a_dpin);
        sum_node.setup_sink_pin(b).connect_driver(b_dpin);
      }else{
        // NOTE: not connect_comparator because everything goes to A
        if (a_sign) {
          sum_node.setup_sink_pin("A").connect_driver(a_dpin);
        }else{
          auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
          auto a_tposs = g->create_node(Ntype_op::Tposs, a_bits+1);
          a_tposs.connect_sink(a_dpin);
          sum_node.setup_sink_pin("A").connect_driver(a_tposs);
        }
        if (b_sign) {
          sum_node.setup_sink_pin(b).connect_driver(b_dpin);
        }else{
          auto b_bits = cell->getParam(ID::B_WIDTH).as_int();
          auto b_tposs = g->create_node(Ntype_op::Tposs, b_bits+1);
          b_tposs.connect_sink(b_dpin);
          sum_node.setup_sink_pin(b).connect_driver(b_tposs);
        }
      }

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$mul", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

#ifdef CELL_SIZE_IGNORE
      exit_node.set_type(Ntype_op::And, y_bits);
      auto mul_node = g->create_node(Ntype_op::Mult, y_bits);

      exit_node.connect_sink(mul_node);
      exit_node.connect_sink(g->create_node_const((Lconst(1)<<Lconst(y_bits))-1));
#else
      exit_node.set_type(Ntype_op::Mult, y_bits);
      auto mul_node = exit_node;
#endif
      mul_node.setup_sink_pin("A").connect_driver(get_dpin(g, cell, ID::A));
      mul_node.setup_sink_pin("A").connect_driver(get_dpin(g, cell, ID::B));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$div", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

      auto a_dpin = get_dpin(g, cell,ID::A);
      auto b_dpin = get_dpin(g, cell,ID::B);

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

#ifdef CELL_SIZE_IGNORE
      exit_node.set_type(Ntype_op::And, y_bits);
      auto div_node = g->create_node(Ntype_op::Div, y_bits);

      exit_node.connect_sink(div_node);
      exit_node.connect_sink(g->create_node_const((Lconst(1)<<Lconst(y_bits))-1));
#else
      exit_node.set_type(Ntype_op::Div, y_bits);
      auto div_node = exit_node;
#endif
      if (a_sign) {
        div_node.setup_sink_pin("a").connect_driver(a_dpin);
      }else{
        auto a_bits  = cell->getParam(ID::A_WIDTH).as_int();
        auto a_tposs = g->create_node(Ntype_op::Tposs, a_bits+1);
        a_tposs.connect_sink(a_dpin);
        div_node.setup_sink_pin("a").connect_driver(a_tposs);
      }

      if (b_sign) {
        div_node.setup_sink_pin("b").connect_driver(b_dpin);
      }else{
        auto b_bits  = cell->getParam(ID::B_WIDTH).as_int();
        auto b_tposs = g->create_node(Ntype_op::Tposs, b_bits+1);
        b_tposs.connect_sink(b_dpin);
        div_node.setup_sink_pin("b").connect_driver(b_tposs);
      }

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$mod", 4) == 0) {

      // original:
      //    y = a mod b
      // same as:
      //    y = a-b*(a/b)

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

      auto div_node = g->create_node(Ntype_op::Div, y_bits);
      auto mul_node = g->create_node(Ntype_op::Mult, y_bits);
      auto sub_node = g->create_node(Ntype_op::Sum, y_bits);

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      Node_pin a_dpin = get_dpin(g, cell,ID::A);
      Node_pin b_dpin = get_dpin(g, cell,ID::B);
      if (!a_sign) {
        auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
        auto tposs_a_node = g->create_node(Ntype_op::Tposs, a_bits+1);
        tposs_a_node.connect_sink(a_dpin);
        a_dpin = tposs_a_node.get_driver_pin();
      }
      if (!b_sign) {
        auto b_bits = cell->getParam(ID::B_WIDTH).as_int();
        auto tposs_b_node = g->create_node(Ntype_op::Tposs, b_bits+1);
        tposs_b_node.connect_sink(b_dpin);
        b_dpin = tposs_b_node.get_driver_pin();
      }

      exit_node.set_type(Ntype_op::And, y_bits);
      exit_node.connect_sink(g->create_node_const((Lconst(1)<<Lconst(y_bits))-1));

      div_node.setup_sink_pin("a").connect_driver(a_dpin);
      div_node.setup_sink_pin("b").connect_driver(b_dpin);

      mul_node.setup_sink_pin("A").connect_driver(div_node);
      mul_node.setup_sink_pin("A").connect_driver(b_dpin);

      sub_node.setup_sink_pin("A").connect_driver(a_dpin);
      sub_node.setup_sink_pin("B").connect_driver(mul_node);

      exit_node.connect_sink(sub_node);

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$neg", 4) == 0) {
      exit_node.set_type(Ntype_op::Sum, get_output_size(cell));

      exit_node.setup_driver_pin("A").connect_driver(g->create_node_const(0));
      exit_node.setup_driver_pin("B").connect_driver(get_dpin(g, cell, ID::A));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$pos", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      exit_node.set_type(Ntype_op::And, y_bits);

      exit_node.connect_sink(g->create_node_const((Lconst(1)<<Lconst(y_bits))-1));
      exit_node.connect_sink(get_dpin(g, cell, ID::A));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && cell->getParam(ID::B_SIGNED).as_bool()) {
      // y0 = SRA(Tposs(A),Tposs(B))
      // y1 = SHL(A, -B)
      // y2 = mux(B<0, Y0, Y1)
      // y  = Y2 & Y.__mask

      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

      auto max_out_bits = a_bits + (1<<b_bits);

      exit_node.set_type(Ntype_op::And, y_bits);

      auto tposs_a_node = g->create_node(Ntype_op::Tposs, a_bits+1);
      auto tposs_b_node = g->create_node(Ntype_op::Tposs, b_bits+1);
      auto sra_node     = g->create_node(Ntype_op::SRA, max_out_bits);
      auto shl_node     = g->create_node(Ntype_op::SHL, max_out_bits);
      auto mux_node     = g->create_node(Ntype_op::Mux, max_out_bits);
      auto neg_node     = g->create_node(Ntype_op::Sum, b_bits);
      auto lt_node      = g->create_node(Ntype_op::LT, 1);

      auto a_dpin = get_dpin(g, cell, ID::A);
      auto b_dpin = get_dpin(g, cell, ID::B);

      //--
      tposs_a_node.connect_sink(a_dpin);
      tposs_b_node.connect_sink(b_dpin);

      sra_node.setup_sink_pin("a").connect_driver(tposs_a_node);
      sra_node.setup_sink_pin("b").connect_driver(tposs_b_node);

      auto y0_dpin = sra_node.setup_driver_pin();

      //--
      neg_node.setup_sink_pin("A").connect_driver(g->create_node_const(0));
      neg_node.setup_sink_pin("B").connect_driver(b_dpin);

      shl_node.setup_sink_pin("a").connect_driver(a_dpin);
      shl_node.setup_sink_pin("b").connect_driver(neg_node);

      auto y1_dpin = shl_node.setup_driver_pin();

      //--
      lt_node.setup_sink_pin("A").connect_driver(b_dpin);
      lt_node.setup_sink_pin("B").connect_driver(g->create_node_const(0));

      mux_node.setup_sink_pin("0").connect_driver(lt_node);
      mux_node.setup_sink_pin("1").connect_driver(y0_dpin);
      mux_node.setup_sink_pin("2").connect_driver(y1_dpin);

      auto y2_dpin = mux_node.setup_driver_pin();

      //--
      exit_node.connect_sink(y2_dpin);
      exit_node.connect_sink(g->create_node_const((Lconst(1)<<Lconst(y_bits))-1));

    } else if (std::strncmp(cell->type.c_str(), "$shr", 4) == 0
               || (std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && !cell->getParam(ID::B_SIGNED).as_bool())
               || (std::strncmp(cell->type.c_str(), "$sshr"  , 5) == 0 && !cell->getParam(ID::A_SIGNED).as_bool())) {
      // y = SRA(Tposs(A),B) - unsigned shift right

      exit_node.set_type(Ntype_op::SRA, get_output_size(cell));
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();

      auto tposs_node = g->create_node(Ntype_op::Tposs, a_bits+1);
      tposs_node.connect_sink(get_dpin(g, cell, ID::A));
      exit_node.setup_sink_pin("a").connect_driver(tposs_node);
      exit_node.setup_sink_pin("b").connect_driver(get_dpin(g, cell, ID::B));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$sshr", 5) == 0 && cell->getParam(ID::A_SIGNED).as_bool()) {
      exit_node.set_type(Ntype_op::SRA, get_output_size(cell));

      exit_node.setup_sink_pin("a").connect_driver(get_dpin(g, cell, ID::A));
      exit_node.setup_sink_pin("b").connect_driver(get_dpin(g, cell, ID::B));

    } else if (std::strncmp(cell->type.c_str(), "$shl", 4) == 0 || std::strncmp(cell->type.c_str(), "$sshl", 5) == 0) {
      exit_node.set_type(Ntype_op::SHL, get_output_size(cell));

      exit_node.setup_sink_pin("a").connect_driver(get_dpin(g, cell, ID::A));
      exit_node.setup_sink_pin("b").connect_driver(get_dpin(g, cell, ID::B));

    } else if (std::strncmp(cell->type.c_str(), "$mem", 4) == 0) {
      exit_node.set_type(Ntype_op::Memory);

      // int parameters
      uint32_t width  = cell->getParam(ID::WIDTH).as_int();
      uint32_t depth  = cell->getParam(ID::SIZE).as_int();
      uint32_t offset = cell->getParam(ID::OFFSET).as_int();
      auto abits      = cell->getParam(ID::ABITS).as_int();
      auto rdports    = cell->getParam(ID::RD_PORTS).as_int();
      auto wrports    = cell->getParam(ID::WR_PORTS).as_int();

      // string parameters
      RTLIL::Const transp  = cell->getParam(ID::RD_TRANSPARENT);
      RTLIL::Const rd_clkp = cell->getParam(ID::RD_CLK_POLARITY);
      RTLIL::Const rd_clke = cell->getParam(ID::RD_CLK_ENABLE);
      RTLIL::Const wr_clkp = cell->getParam(ID::WR_CLK_POLARITY);
      RTLIL::Const wr_clke = cell->getParam(ID::WR_CLK_ENABLE);

      std::string name = cell->getParam(ID::MEMID).decode_string();

      fmt::print("name:{} depth:{} wrports:{} rdports:{}\n", name, depth, wrports, rdports);

#if 1
      fmt::print("FIXME: memory still pending in yosys\n");
#else

      connect_constant(g, depth, exit_node, LGRAPH_MEMOP_SIZE);
      connect_constant(g, offset, exit_node, LGRAPH_MEMOP_OFFSET);
      connect_constant(g, abits, exit_node, LGRAPH_MEMOP_ABITS);
      connect_constant(g, wrports, exit_node, LGRAPH_MEMOP_WRPORT);
      connect_constant(g, rdports, exit_node, LGRAPH_MEMOP_RDPORT);

      // lgraph has reversed convention compared to yosys.
      int rd_clk_enabled  = 0;
      int rd_clk_polarity = 0;
      for (int i = 0; i < rd_clkp.size(); i++) {
        if (rd_clke[i] != RTLIL::S1)
          continue;

        if (rd_clkp[i] == RTLIL::S1) {
          if (rd_clk_enabled)
            I(rd_clk_polarity);  // All the read ports are equal
          rd_clk_enabled  = 1;
          rd_clk_polarity = 1;
        } else if (rd_clkp[i] == RTLIL::S0) {
          if (rd_clk_enabled)
            I(!rd_clk_polarity);  // All the read ports are equal
          rd_clk_enabled  = 1;
          rd_clk_polarity = 0;
        }
      }
      if (rd_clk_enabled) {
        // If there is a rd_clk, all should have rd_clk
        for (int i = 0; i < rd_clkp.size(); i++) {
          if (rd_clke[i] == RTLIL::S1)
            continue;

          log("oops rd_port:%d does not need clk cell %s\n", i, cell->type.c_str());
        }
      }
      int wr_clk_enabled  = 0;
      int wr_clk_polarity = 0;
      for (int i = 0; i < wr_clkp.size(); i++) {
        if (wr_clke[i] != RTLIL::S1)
          continue;
        if (wr_clkp[i] == RTLIL::S1) {
          if (wr_clk_enabled)
            I(wr_clk_polarity);  // All the read ports are equal
          wr_clk_enabled  = 1;
          wr_clk_polarity = 1;
        } else if (wr_clkp[i] == RTLIL::S0) {
          if (wr_clk_enabled)
            I(!wr_clk_polarity);  // All the read ports are equal
          wr_clk_enabled  = 1;
          wr_clk_polarity = 0;
        }
      }
      rd_clk_polarity = rd_clk_polarity ? 0 : 1;  // polarity flipped in lgraph vs yosys
      wr_clk_polarity = wr_clk_polarity ? 0 : 1;  // polarity flipped in lgraph vs yosys

      if (rd_clk_enabled)
        connect_constant(g, rd_clk_polarity, exit_node, LGRAPH_MEMOP_RDCLKPOL);
      if (wr_clk_enabled)
        connect_constant(g, wr_clk_polarity, exit_node, LGRAPH_MEMOP_WRCLKPOL);

      connect_constant(g, transp.as_int(), exit_node, LGRAPH_MEMOP_RDTRAN);

      // TODO: get a test case to patch
      if (cell->hasParam(ID::INIT)) {
        I(cell->getParam(ID::INIT).as_string()[0] == 'x');
      }

      clock = cell->getPort("\\RD_CLK")[0].wire;
      if (clock == nullptr) {
        clock = cell->getPort("\\WR_CLK")[0].wire;
      }
      if (clock == nullptr) {
        log_error("No clock found for memory.\n");
      }

      exit_node.set_name(name);

    } else if (g->get_library().get_lgid(&cell->type.c_str()[1])) {
      // external graph reference
      auto sub_lgid = g->get_library().get_lgid(&cell->type.c_str()[1]);
      I(sub_lgid);
      log("module name original was %s\n", cell->type.c_str());

      entry_node.set_type_sub(sub_lgid);

      std::string inst_name = cell->name.str().substr(1);
      if (!inst_name.empty())
        entry_node.set_name(inst_name);
#ifndef NDEBUG
      else
        fmt::print("yosys2lg got empty inst_name for cell type {}\n", cell->type.c_str());
#endif

#endif

      // DO NOT MERGE THE BELLOW WITH THE OTHER ANDs, NOTs, DFFs
    //--------------------------------------------------------------
    } else if (cell->type.c_str()[0] == '$' && cell->type.c_str()[1] != '_' && strncmp(cell->type.c_str(), "$paramod", 8) != 0) {
      log("likely error: add this cell type %s to lgraph\n", cell->type.c_str());

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$_AND_", 6) == 0) {
      exit_node.set_type(Ntype_op::And, get_output_size(cell));

      exit_node.connect_sink(get_dpin(g, cell, ID::A));
      exit_node.connect_sink(get_dpin(g, cell, ID::B));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$_OR_", 6) == 0) {
      exit_node.set_type(Ntype_op::Or, get_output_size(cell));

      exit_node.connect_sink(get_dpin(g, cell, ID::A));
      exit_node.connect_sink(get_dpin(g, cell, ID::B));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$_XOR_", 6) == 0) {
      exit_node.set_type(Ntype_op::Xor, get_output_size(cell));

      exit_node.connect_sink(get_dpin(g, cell, ID::A));
      exit_node.connect_sink(get_dpin(g, cell, ID::B));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$_NOT_", 6) == 0) {
      exit_node.set_type(Ntype_op::Not, get_output_size(cell));

      exit_node.connect_sink(get_dpin(g, cell, ID::A));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$_DFF_P_", 8) == 0) {
      exit_node.set_type(Ntype_op::Sflop, get_output_size(cell));

      exit_node.setup_sink_pin("clock").connect_driver(get_dpin(g, cell,ID::C));
      exit_node.setup_sink_pin("din").connect_driver(get_dpin(g, cell,ID::D));

    //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$_DFF_N_", 8) == 0) {
      exit_node.set_type(Ntype_op::Sflop, get_output_size(cell));

      exit_node.setup_sink_pin("posclk").connect_driver(g->create_node_const(0));
      exit_node.setup_sink_pin("clock").connect_driver(get_dpin(g, cell,ID::C));
      exit_node.setup_sink_pin("din").connect_driver(get_dpin(g, cell,ID::D));

    } else if (std::strncmp(cell->type.c_str(), "$_DFF_NN", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_NP", 8) == 0
               || std::strncmp(cell->type.c_str(), "$_DFF_PP", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_PN", 8) == 0) {
      // TODO: add support for those DFF types
      log_error("Found complex yosys DFFs, run `techmap -map +/adff2dff.v` before calling the yosys2lg pass\n");
      I(false);

    } else if (cell->type.c_str()[0] == '\\' || strncmp(cell->type.c_str(), "$paramod\\", 9) == 0) {  // sub_cell type
      assert(exit_node.is_type_sub());

      absl::flat_hash_set<XEdge::Compact> added_edges;

      const auto &sub = exit_node.get_type_sub_node();

      for (auto &conn : cell->connections()) {
        RTLIL::SigSpec ss = conn.second;
        if (ss.size() == 0)
          continue;

        std::string name(&conn.first.c_str()[1]);

        if (sub.is_output(name))
          continue;
        if (!sub.is_input(name)) {
          log_error("sub:%s does not have pin:%s as input\n", std::string(sub.get_name()).c_str(), name.c_str());
        }

        Node_pin spin = exit_node.setup_sink_pin(name);
        if (spin.is_invalid())
          continue;

        Node_pin dpin = create_pick_concat_dpin(g, ss, true);

        if (added_edges.find(XEdge(dpin, spin).get_compact()) != added_edges.end()) {
          // there are two edges from dpin to spin
          // this is not allowed in lgraph, add a Ntype_op::Or in between
          auto or_node = g->create_node(Ntype_op::Or, ss.size());
          g->add_edge(dpin, or_node.setup_sink_pin(), ss.size());
          dpin = or_node.setup_driver_pin();
        }
        g->add_edge(dpin, spin);
        added_edges.insert(XEdge(dpin, spin).get_compact());
      }

    } else {
      // blackbox addition
      ::LGraph::warn("Black box addition from yosys frontend, cell type {} not found instance {}",
                     cell->type.c_str(),
                     cell->name.c_str());
      I(false);
    }
  }

  for (auto &kv : partially_assigned) {
    const RTLIL::Wire *wire = kv.first;

    auto or_dpin = wire2pin[wire];
    auto or_node = or_dpin.get_node();
    I(or_node.get_type_op() == Ntype_op::Or);

    uint32_t offset= 0;

    set_bits_wirename(or_dpin, wire);

    Node_pin current;
    auto     size  = 0UL;
    bool     first = true;
    for (auto pin : kv.second) {
      if (first)
        first = false;
      else if ((current.is_invalid() && !pin.is_invalid()) || (!current.is_invalid() && pin.is_invalid())
               || (!pin.is_invalid() && current != pin)) {
        Node_pin dpin;
        if (!current.is_invalid()) {
          dpin = current;
        } else {
          auto all_x = absl::StrCat("0b", std::string(size, 'x'), "u", (int)size, "bits");
          auto node  = g->create_node_const(Lconst(all_x));
          dpin       = node.setup_driver_pin();
        }

        I(size < dpin.get_bits() || size % dpin.get_bits() == 0);
        int times = size < dpin.get_bits() ? 1 : size / dpin.get_bits();
        for (int i = 0; i < times; i++) {
          auto shl_node = g->create_node(Ntype_op::SHL, dpin.get_bits());
          shl_node.setup_sink_pin("a").connect_driver(dpin);
          shl_node.setup_sink_pin("b").connect_driver(g->create_node_const(offset));

          or_node.connect_sink(shl_node);

          offset += dpin.get_bits();
        }
        size = 0;
      }
      size++;
      current = pin;
    }

    Node_pin dpin;
    if (!current.is_invalid()) {
      dpin = current;
    } else {
      auto all_x = absl::StrCat("0b", std::string(size, 'x'), "u", (int)size, "bits");
      auto node  = g->create_node_const(Lconst(all_x));
      dpin       = node.setup_driver_pin();
    }
    I(size < dpin.get_bits() || size % dpin.get_bits() == 0);
    auto times = size < dpin.get_bits() ? 1 : size / dpin.get_bits();
    for (auto i = 0UL; i < times; i++) {
      auto shl_node = g->create_node(Ntype_op::SHL, dpin.get_bits());
      shl_node.setup_sink_pin("a").connect_driver(dpin);
      shl_node.setup_sink_pin("b").connect_driver(g->create_node_const(offset));

      or_node.connect_sink(shl_node);
      offset += dpin.get_bits();
    }
  }

  // we need to connect global outputs to the cell that drives it
  for (auto wire : module->wires()) {
    if (wire->port_output && wire2pin.find(wire) != wire2pin.end()) {
      Node_pin &dpin = wire2pin[wire];
      if (dpin.is_graph_output())
        continue;

      Node_pin spin = g->get_graph_output(&wire->name.c_str()[1]);
      g->add_edge(dpin, spin, wire->width);
    }
  }
}

static void guess_modules_io(RTLIL::Design *design, std::string_view path) {
  for (auto &it : design->modules_) {
    RTLIL::Module *module = it.second;
    std::string    name   = &module->name.c_str()[1];

    SigMap sigmap(module);

    for (auto wire : module->wires()) {
      if (wire->port_input)
        driven_signals.insert(wire->hash());
    }

    for (auto it : module->connections()) {
      for (auto &chunk : it.first.chunks()) {
        const RTLIL::Wire *wire = chunk.wire;
        if (wire)
          driven_signals.insert(wire->hash());
      }
    }

    for (auto cell : module->cells()) {
      if (ct_all.cell_known(cell->type)) {
        for (auto &conn : cell->connections()) {
          if (ct_all.cell_output(cell->type, conn.first)) {
            for (auto &chunk : conn.second.chunks()) {
              const RTLIL::Wire *wire = chunk.wire;
              if (wire)
                driven_signals.insert(wire->hash());
            }
          }
        }
      }
    }

    auto library = Graph_library::instance(path);

    for (auto cell : module->cells()) {
      if (!ct_all.cell_known(cell->type)) {
        std::string_view mod_name(&(cell->type.c_str()[1]));

        for (auto &conn : cell->connections()) {
          for (auto &chunk : conn.second.chunks()) {
            const RTLIL::Wire *wire = chunk.wire;
            if (wire == nullptr)
              continue;
            std::string cell_port = absl::StrCat(cell->type.str(), "_:_", conn.first.str());
            if (cell_port_inputs.count(cell_port))
              continue;

            bool is_input  = wire->port_input;
            bool is_output = false;  // WARNING: wire->port_output is NOT ALWAYS. The output can go to other internal
            if (!is_input && !is_output)
              is_input = driven_signals.count(wire->hash()) > 0;

            if (library->has_name(mod_name)) {
              const auto &sub = library->get_sub(mod_name);
              if (sub.has_pin(name)) {
                is_input  = sub.is_input(mod_name);
                is_output = sub.is_output(mod_name);
                continue;
              }
            }

            const char *str = conn.first.c_str();
            if (!is_input && !is_output && str[0] == '\\') {
              str++;
              if (str[0] == 'A' || str[0] == 'B' || str[0] == 'C' || str[0] == 'D' || str[0] == 'S') {
                if (isdigit(str[1]) || str[1] == 0)
                  is_input = true;
              }
              if (str[0] == 'a' || str[0] == 'b' || str[0] == 'c' || str[0] == 'd' || str[0] == 's') {
                if ((isdigit(str[1]) && str[2] == 0) || str[1] == 0)
                  is_input = true;
              }
              if (strncmp(str, "CK", 2) == 0 || strstr(str, "CLK") || strstr(str, "clk") || strcmp(str, "clock") == 0
                  || strcmp(str, "EN") == 0 || strcmp(str, "RD") == 0 || strcmp(str, "en") == 0 || strcmp(str, "enable") == 0
                  || strstr(str, "reset") || strstr(str, "RST"))
                is_input = true;

              if ((strncmp(cell->type.c_str(), "\\sa", 3) == 0)
                  && (strncmp(str, "ME", 2) == 0 || strncmp(str, "QPB", 3) == 0 || strncmp(str, "RME", 3) == 0))
                is_input = true;

              if ((strncmp(cell->type.c_str(), "\\sa", 3) == 0)
                  && (strcmp(str, "BC2") == 0 || strncmp(str, "DFT", 3) == 0 || strncmp(str, "SO_CNT", 6) == 0
                    || strcmp(str, "SO_D") == 0))
                is_input = true;

              if (cell->type.str()[1] == 'H' && cell->type.str()[2] == 'D') {
                if (strcmp(str, "SI") == 0 || strcmp(str, "SE") == 0 || strcmp(str, "CI") == 0 || strcmp(str, "TE") == 0
                    || strcmp(str, "TEN") == 0 || strcmp(str, "S") == 0 || strcmp(str, "SE") == 0)
                  is_input = true;
              }

              if (strncmp(str, "io_is", 5) == 0 || strncmp(str, "io_in", 5) == 0)
                is_input = true;

              if (strstr(cell->type.c_str(), "mem")) {
                if (strcmp(str, "R0_data") == 0 || strcmp(str, "R0_en") == 0 || strcmp(str, "R0_addr") == 0
                    || strcmp(str, "W0_addr") == 0 || strcmp(str, "W0_mask") == 0 || strcmp(str, "W0_en") == 0)
                  is_input = true;

                if (strcmp(str, "R0_wdata") == 0)
                  is_output = true;
              }
              if (strstr(cell->type.c_str(), "array")) {
                if (strcmp(str, "RW0_rdata") == 0)
                  is_input = true;

                if (strcmp(str, "RW0_wdata") == 0)
                  is_output = true;
              }

              if (strncmp(str, "IN", 2) == 0 || strncmp(str, "in", 2) == 0)
                is_input = true;

              if (str[1] == 0 && (str[0] == 'X' || str[0] == 'Y' || str[0] == 'Z' || str[0] == 'Q'))
                is_output = true;
              if (str[1] == 0 && (str[0] == 'x' || str[0] == 'y' || str[0] == 'z' || str[0] == 'q'))
                is_output = true;

              if (cell->type.str()[1] == 'H' && cell->type.str()[2] == 'D') {
                if (strcmp(str, "CON") == 0 || strcmp(str, "SN") == 0)
                  is_output = true;
              }

              if (strcmp(str, "SO") == 0 || strncmp(str, "io_out", 6) == 0 || strcmp(str, "QN") == 0 || strcmp(str, "CO") == 0
                  || strcmp(str, "QP") == 0)
                is_output = true;

              if (strncmp(str, "OU", 2) == 0 || strncmp(str, "ou", 2) == 0)
                is_output = true;

              if (cell->type.str() == "\\$memrd") {
                is_input  = false;
                is_output = false;
                if (strcmp(str, "DATA") == 0)
                  is_output = true;
                else
                  is_input = true;
              } else if (cell->type.str() == "\\$memwr") {
                is_input  = true;
                is_output = false;
              }

              // Last fix for SRAMs
              if (strncmp(cell->type.c_str(), "\\sa", 3) == 0 && strstr(cell->type.c_str(), "rw")) {
                if (strcmp(str, "RSCOUT") == 0 || strcmp(str, "SO_CN") == 0 || strcmp(str, "SO_Q") == 0)
                  is_output = true;
                if (!is_output)
                  is_input = true;
              }
            }

#if 1
            std::cout << "unknown cell_type:" << cell->type.str() << " cell_instance:" << cell->name.str()
              << " port_name:" << conn.first.str() << " wire_name:" << wire->name.str() << " sig:" << wire->hash()
              << " io:" << (is_input ? "I" : "") << (is_output ? "O" : "") << "\n";
#endif
            I(!(is_input && is_output));

            if (is_input)
              cell_port_inputs.insert(cell_port);
            if (is_output)
              cell_port_outputs.insert(cell_port);
          }
        }
      }
    }

    auto *g = LGraph::create(path, name, "-");  // No source, unable to track
    I(g);
    look_for_module_io(module, path);
  }
}

// each pass contains a singleton object that is derived from Pass
struct Yosys2lg_Pass : public Yosys::Pass {
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

    for (argidx = 1; argidx < args.size(); argidx++) {
      if (args[argidx] == "-path") {
        path = args[++argidx];
        continue;
      }
      break;
    }

    // handle extra options (e.g. selection)
    extra_args(args, argidx, design);

    ct_all.setup(design);

    driven_signals.clear();

    guess_modules_io(design, path);

    for (auto &it : design->modules_) {
      RTLIL::Module *module = it.second;
      if (design->selected_module(it.first)) {
        Lbench b("inou.yosys.tolg." + module->name.str());

        auto  library = Graph_library::instance(path);
        auto *g       = library->try_find_lgraph(&module->name.c_str()[1]);
        I(g);

        setup_cell_driver_pins(module, g);
        process_module(module, g);

        g->sync();
      }

      wire2pin.clear();
      cell2node.clear();
      partially_assigned.clear();
      picks.clear();
    }
  }
} Yosys2lg_Pass;

PRIVATE_NAMESPACE_END
