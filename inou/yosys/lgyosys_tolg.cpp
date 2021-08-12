//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// External package includes
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "kernel/celltypes.h"
#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#pragma GCC diagnostic pop

// LiveHD includes
#include "inou.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

// When true, the cell bits should have no effect (set to zero or large num for
// bitwidth to adjust should work too)
#define CELL_SIZE_IGNORE 1

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

static CellTypes                          ct_all;
static absl::flat_hash_set<size_t>        driven_signals;
static absl::flat_hash_set<mmap_lib::str> cell_port_inputs;
static absl::flat_hash_set<mmap_lib::str> cell_port_outputs;

typedef std::pair<const RTLIL::Wire *, int> Wire_bit;

static absl::flat_hash_map<const RTLIL::Wire *, Node_pin>          wire2pin;
static absl::flat_hash_map<const RTLIL::Cell *, Node>              cell2node;  // Points to the exit_node for the block
static absl::flat_hash_map<const RTLIL::Wire *, Node_pin_iterator> partially_assigned;
static absl::flat_hash_map<const RTLIL::Wire *, std::vector<int>>  partially_assigned_bits;
static absl::flat_hash_map<const RTLIL::Wire *, std::vector<int>>  partially_assigned_fwd;

static std::vector<const RTLIL::Wire *> pending_outputs;

static void look_for_wire(Lgraph *g, const RTLIL::Wire *wire) {
  if (wire2pin.find(wire) != wire2pin.end())
    return;

  if (wire->port_input) {
    // log("input %s\n",wire->name.c_str());
    I(!wire->port_output);  // any bidirectional port?
    I(wire->name.c_str()[0] == '\\');
    Node_pin pin;
    mmap_lib::str wname(&wire->name.c_str()[1]);
    if (g->has_graph_input(wname)) {
      pin = g->get_graph_input(wname);
      I(pin.get_bits() == wire->width);
    } else {
      pin = g->add_graph_input(wname, wire->port_id, wire->width);
    }
    if (wire->start_offset) {
      pin.set_offset(wire->start_offset);
    }
    wire2pin[wire] = pin;
    // NOTE: can not convert to unsidned. In yosys/verilog is use dependent (still undecided)
  } else if (wire->port_output) {
    // log("output %s\n",wire->name.c_str());
    I(wire->name.c_str()[0] == '\\');
#if 0
    if (!g->has_graph_output(&wire->name.c_str()[1])) {
      g->add_graph_output(&wire->name.c_str()[1], wire->port_id, wire->width);
    }
    auto dpin = g->get_graph_output_driver_pin(&wire->name.c_str()[1]);
    I(dpin.get_bits() == wire->width);
    if (wire->start_offset) {
      dpin.set_offset(wire->start_offset);
    }
#else
    auto dpin = g->create_node(Ntype_op::Or, wire->width).setup_driver_pin();
    pending_outputs.emplace_back(wire);
#endif

    wire2pin[wire] = dpin;
  }
}

static Node_pin resolve_constant(Lgraph *g, const std::vector<RTLIL::State> &data, bool is_signed) {
  RTLIL::Const v(data);
  if (v.is_fully_zero()) {
    return g->create_node_const(Lconst(0)).setup_driver_pin();
  }

  if (v.is_fully_def() && data.size() < 30) {
    auto x = v.as_int(is_signed);
    return g->create_node_const(Lconst(x)).setup_driver_pin();
  }

  // this is a vector of RTLIL::State
  // S0 => 0
  // S1 => 1
  // Sx => x
  // Sz => high z
  // Sa => don't care (not sure what is the diff between Sa and Sx
  // Sm => used internally by Yosys
  mmap_lib::str val;
  if (is_signed) {
    val = "0sb";
  }else{
    val = "0b";
  }

  for (size_t i = data.size(); i > 0; i--) {
    switch (data[i - 1]) {
      case RTLIL::S0: val = val.prepend('0'); break;
      case RTLIL::S1: val = val.prepend('1'); break;
      case RTLIL::Sz: val = val.prepend('z'); break;
      default: val = val.prepend('?'); break;
    }
  }

  // fmt::print("val:{} prp:{} bits:{}\n", val, lc.to_pyrope(), lc.get_bits());
  return g->create_node_const(Lconst::from_pyrope(val)).setup_driver_pin();
}

class Pick_ID {
  // friend constexpr bool operator==(const Pick_ID &lhs, const Pick_ID &rhs);
public:
  Node_pin driver;
  int      offset;
  int      width;
  bool     is_signed;

  constexpr Pick_ID(Node_pin _driver, int _offset, int _width, bool _is_signed)
      : driver(_driver), offset(_offset), width(_width), is_signed(_is_signed) {}

  template <typename H>
  friend H AbslHashValue(H h, const Pick_ID &s) {
    return H::combine(std::move(h), s.driver.get_compact(), s.offset, s.width, s.is_signed);
  };
};

bool operator==(const Pick_ID &lhs, const Pick_ID &rhs) {
  return lhs.driver == rhs.driver && lhs.width == rhs.width && lhs.offset == rhs.offset && lhs.is_signed == rhs.is_signed;
}

static absl::flat_hash_map<Pick_ID, Node_pin> picks;

static Node_pin create_pick_operator(const Node_pin &wide_dpin, int offset, int width, bool is_signed) {
  if (offset == 0 && (int)wide_dpin.get_bits() == width && !is_signed)
    return wide_dpin;

  Pick_ID pick_id(wide_dpin, offset, width, is_signed);
  if (picks.find(pick_id) != picks.end()) {
    return picks.at(pick_id);
  }

  Node_pin dpin;

  auto *lg = wide_dpin.get_class_lgraph();

  if (is_signed) {
    // Pick(a,width,offset, false):
    //   x = a>>offset
    //   y = Sext(x, width)

    auto sext_node = lg->create_node(Ntype_op::Sext, width);

    if (offset) {
      auto shr_node = lg->create_node(Ntype_op::SRA, width);
      shr_node.setup_sink_pin("a").connect_driver(wide_dpin);
      shr_node.setup_sink_pin("b").connect_driver(lg->create_node_const(offset));

      sext_node.setup_sink_pin("a").connect_driver(shr_node);
    } else {
      sext_node.setup_sink_pin("a").connect_driver(wide_dpin);
    }

    sext_node.setup_sink_pin("b").connect_driver(lg->create_node_const(Lconst(width)));
    dpin = sext_node.setup_driver_pin();
  } else {
    // Pick(a,width,offset, false):
    //   x = a>>offset
    //   y = x & ((1<<width)-1)

    auto and_node = lg->create_node(Ntype_op::And, width);

    if (offset) {
      auto shr_node = lg->create_node(Ntype_op::SRA, width);
      shr_node.setup_sink_pin("a").connect_driver(wide_dpin);
      shr_node.setup_sink_pin("b").connect_driver(lg->create_node_const(offset));

      and_node.connect_sink(shr_node);
    } else {
      and_node.connect_sink(wide_dpin);
    }

    and_node.connect_sink(lg->create_node_const(((Lconst(1) << Lconst(width)) - 1)));
    dpin = and_node.setup_driver_pin();
  }

  picks.insert(std::make_pair(pick_id, dpin));

  return dpin;
}

static Node_pin get_edge_pin(Lgraph *g, const RTLIL::Wire *wire, bool is_signed) {
  if (wire2pin.find(wire) == wire2pin.end()) {
    look_for_wire(g, wire);
  }

  if (wire2pin.find(wire) != wire2pin.end()) {
    auto &dpin = wire2pin[wire];
    if (wire->width != (int)wire2pin[wire].get_bits()) {
      if (wire->width > static_cast<int>(dpin.get_bits())) {  // OK, just zero extend like in comparators
        return dpin;
      }

      fmt::print("w_name:{} w_w:{} | pid:{} bits:{}\n",  // due to extra tposs
                 wire->name.c_str(),
                 wire->width,
                 dpin.get_pid(),
                 dpin.get_bits());
      // I(false);  // WHY?
      // wire2pin[wire].set_bits(wire->width);
      // I(wire->width == wire2pin[wire].get_bits());
    }

    if (is_signed)  // || !dpin.has_graph_input())
      return dpin;

    auto node = dpin.get_node();

    if (node.is_type(Ntype_op::Get_mask))
      return dpin;

    if (node.is_type_const() && !node.get_type_const().is_negative())
      return dpin;

    auto tposs_node = g->create_node(Ntype_op::Get_mask, dpin.get_bits() + 1);
    tposs_node.setup_sink_pin("a").connect_driver(dpin);
    tposs_node.setup_sink_pin("mask").connect_driver(g->create_node_const(Lconst(-1)));

    return tposs_node.setup_driver_pin();
  }

  I(!wire->port_input);  // Added before at look_for_wire
  I(!wire->port_output);

  auto node = g->create_node(Ntype_op::Or, wire->width);  // Just a placeholder/connect gate

  wire2pin[wire] = node.setup_driver_pin();

  return wire2pin[wire];
}

static Node_pin create_pick_operator(Lgraph *g, const RTLIL::Wire *wire, int offset, int width, bool is_signed) {
  if (wire->width == width && offset == 0)
    return get_edge_pin(g, wire, is_signed);

  return create_pick_operator(get_edge_pin(g, wire, is_signed), offset, width, is_signed);
}

static void append_to_or_node(Lgraph *g, const Node &or_node, const Node_pin &dpin, int or_offset) {
  if (or_node.has_inputs() && dpin.get_node().is_type_const()) {
    auto val = dpin.get_node().get_type_const();
    if (val == 0)
      return;
  }

  Node tposs_node = g->create_node(Ntype_op::Get_mask);

  if (or_offset) {
    auto shl_node = g->create_node(Ntype_op::SHL, or_offset + dpin.get_bits());
    shl_node.setup_sink_pin("a").connect_driver(dpin);
    shl_node.setup_sink_pin("b").connect_driver(g->create_node_const(or_offset));

    tposs_node.setup_driver_pin().set_bits(or_offset + dpin.get_bits() + 1);
    tposs_node.setup_sink_pin("a").connect_driver(shl_node);
  } else {
    tposs_node.setup_driver_pin().set_bits(dpin.get_bits() + 1);
    tposs_node.setup_sink_pin("a").connect_driver(dpin);
  }

  tposs_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));
  or_node.connect_sink(tposs_node);
}

static Node_pin create_pick_concat_dpin(Lgraph *g, const RTLIL::SigSpec &ss, bool is_signed) {
  std::vector<Node_pin> inp_pins;
  I(ss.chunks().size() != 0);

  const auto chunk_list = ss.chunks();
  for (auto i = 0u; i < chunk_list.size(); ++i) {
    const auto &chunk       = chunk_list[i];
    bool        signed_last = ((i + 1) == chunk_list.size()) && is_signed;
    if (chunk.wire == nullptr) {
      inp_pins.emplace_back(resolve_constant(g, chunk.data, signed_last));
    } else {
      inp_pins.emplace_back(create_pick_operator(g, chunk.wire, chunk.offset, chunk.width, signed_last));
    }
  }

  Node_pin dpin;
  if (inp_pins.size() > 1) {
    auto or_node = g->create_node(Ntype_op::Or, ss.size());

    int offset = 0;
    for (auto i = 0u; i < chunk_list.size(); ++i) {
      if (!inp_pins[i].is_invalid()) {
        Node_pin inp_pin;

        if ((i + 1) == chunk_list.size() || is_signed) {  // last pin
          inp_pin = inp_pins[i];
        } else if (inp_pins[i].get_node().is_type(Ntype_op::Get_mask)) {
          inp_pin = inp_pins[i];
        } else {
          auto tposs_node = g->create_node(Ntype_op::Get_mask, inp_pins[i].get_bits() + 1);
          tposs_node.setup_sink_pin("a").connect_driver(inp_pins[i]);
          tposs_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));

          inp_pin = tposs_node.setup_driver_pin();
        }

        append_to_or_node(g, or_node, inp_pin, offset);
      }

      offset += chunk_list[i].width;
    }
    dpin = or_node.setup_driver_pin();
  } else {
    I(!inp_pins.empty());
    dpin = inp_pins[0];  // single wire, do not create pick_concat
  }

  if (dpin.is_invalid())
    return g->create_node_const(0).setup_driver_pin();

  return dpin;
}

static Node_pin get_dpin(Lgraph *g, const RTLIL::Cell *cell, const RTLIL::IdString &name) {
  if (cell->hasParam(name)) {
    const RTLIL::Const &v = cell->getParam(name);
    if (v.is_fully_def() && v.size() < 30) {
      return g->create_node_const(v.as_int()).setup_driver_pin();
    }
    auto v_str = mmap_lib::str::concat("0b", v.as_string());
    return g->create_node_const(Lconst::from_pyrope(v_str)).setup_driver_pin();
  }
  bool is_signed = true;  // signed by default
  if (name == ID::A && cell->hasParam(ID::A_SIGNED)) {
    is_signed = cell->getParam(ID::A_SIGNED).as_bool();
  } else if (name == ID::B && cell->hasParam(ID::B_SIGNED)) {
    is_signed = cell->getParam(ID::B_SIGNED).as_bool();
  }

  if (!cell->hasPort(name)) {
    return g->create_node_const(Lconst::from_pyrope("0b?")).setup_driver_pin();
  }

  return create_pick_concat_dpin(g, cell->getPort(name), is_signed);
}

static Node_pin get_unsigned_dpin(Lgraph *g, const RTLIL::Cell *cell, const RTLIL::IdString &name) {
  bool no_need = false;
  int  bits    = 0;
  (void)bits;

  if (name == ID::A) {
    bits = cell->getParam(ID::A_WIDTH).as_int();
    if (cell->hasParam(ID::A_SIGNED))
      no_need = cell->getParam(ID::A_SIGNED).as_bool();
  } else if (name == ID::B) {
    bits = cell->getParam(ID::B_WIDTH).as_int();
    if (cell->hasParam(ID::B_SIGNED))
      no_need = cell->getParam(ID::B_SIGNED).as_bool();
  } else {
    I(false);  // do not use it for this port name
  }
  auto dpin = get_dpin(g, cell, name);
  if (no_need)
    return dpin;

  auto node = dpin.get_node();
  if (node.is_type(Ntype_op::Get_mask)) {
    return dpin;
  }
  if (node.is_type_const() && !node.get_type_const().is_negative()) {
    return dpin;
  }

  auto a_tposs = g->create_node(Ntype_op::Get_mask, dpin.get_bits() + 1);
  a_tposs.setup_sink_pin("a").connect_driver(dpin);
  a_tposs.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));

  return a_tposs.setup_driver_pin();
}

static bool is_yosys_output(const std::string &idstring) {
  return idstring == "\\Y" || idstring == "\\Q" || idstring == "\\RD_DATA";
}

static void connect_all_inputs(const Node_pin &spin, const RTLIL::Cell *cell) {
  I(spin.is_sink());
  bool is_signed = false;
  for (auto &conn : cell->connections()) {
    RTLIL::SigSpec ss = conn.second;
    if (ss.size() == 0)
      continue;
    if (is_yosys_output(conn.first.c_str()))
      continue;  // Just go over the inputs
    if (conn.first == ID::A && cell->hasParam(ID::A_SIGNED)) {
      is_signed = cell->getParam(ID::A_SIGNED).as_bool();
    } else if (conn.first == ID::B && cell->hasParam(ID::B_SIGNED)) {
      is_signed = cell->getParam(ID::B_SIGNED).as_bool();
    }
    if (!is_signed)
      break;  // no need to test more
  }

  for (auto &conn : cell->connections()) {
    RTLIL::SigSpec ss = conn.second;
    if (ss.size() == 0)
      continue;
    if (is_yosys_output(conn.first.c_str()))
      continue;  // Just go over the inputs

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
      pin.set_name(mmap_lib::str(&wire->name.c_str()[1]));
    }
  }

  //  if (!pin.is_graph_io()) {
  //    pin.set_bits(wire->width);
  //  }
}

static Node_pin get_partial_dpin(Lgraph *g, const RTLIL::Wire *wire) {
  auto or_dpin = get_edge_pin(g, wire, true);

  if (!or_dpin.get_node().is_type(Ntype_op::Or)) {
    auto real_or_node = g->create_node(Ntype_op::Or, or_dpin.get_bits());

    if (unlikely(or_dpin.is_graph_output())) {  // Some outputs are deferred
      or_dpin.change_to_sink_from_graph_out_driver().connect_driver(real_or_node);
    } else if (unlikely(or_dpin.get_type_op() == Ntype_op::Sub)) {
      real_or_node.setup_sink_pin().connect_driver(or_dpin);
    } else if (!or_dpin.get_node().is_type(Ntype_op::Or)) {
      fmt::print("WARNING: wire:{} is mapped to dpin:{} node or_wire\n", wire->name.str(), or_dpin.debug_name());
      or_dpin.get_node().dump();
    }

    or_dpin        = real_or_node.setup_driver_pin();
    wire2pin[wire] = real_or_node.setup_driver_pin();
  }

  set_bits_wirename(or_dpin, wire);

  return or_dpin;
}

static Node resolve_memory(Lgraph *g, RTLIL::Cell *cell) {
  auto node = g->create_node(Ntype_op::Memory);

  uint32_t rdports = cell->getParam(ID::RD_PORTS).as_int();
  (void)rdports;

  uint32_t bits = cell->getParam(ID::WIDTH).as_int();
  (void)bits;

#if 1
  fmt::print("Memory pending\n");
#else

  for (uint32_t rdport = 0; rdport < rdports; rdport++) {
    RTLIL::SigSpec ss = cell->getPort("\\RD_DATA").extract(rdport * bits, bits);
    auto dpin = node.setup_driver_pin(rdport);
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
          if (or_dpin.has_graph_output()) {
            g->add_edge(dpin, or_dpin.change_to_sink_from_graph_out_driver());
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
          wire2pin[wire] = pick_pin;
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

static bool is_black_box_output(const RTLIL::Module *mod, const RTLIL::Cell *cell, const RTLIL::IdString &port_name) {
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

  auto cell_port = mmap_lib::str::concat(cell->type.str(), "_:_", port_name.str());

  if (cell_port_outputs.find(cell_port) != cell_port_outputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an output\n", cell->name.c_str(), port_name.c_str());
    return true;
  }
  if (cell_port_inputs.find(cell_port) != cell_port_inputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an input\n", cell->name.c_str(), port_name.c_str());
    return false;
  }

  ::Lgraph::error(
      "Could not find a definition for module {}, treating as a blackbox but could not determine whether {} is an output",
      cell->type.str(),
      port_name.str());

  log_error("output unknown port %s at module %s cell %s\n", port_name.c_str(), mod->name.c_str(), cell->type.c_str());
  I(false);  // TODO: is it possible to resolve this case?
  return false;
}

static bool is_black_box_input(const RTLIL::Module *mod, const RTLIL::Cell *cell, const RTLIL::IdString &port_name) {
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

  auto cell_port = mmap_lib::str::concat(cell->type.str(), "_:_", port_name.str());

  // Opposite of is_black_box_output
  if (cell_port_outputs.find(cell_port) != cell_port_outputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an output\n", cell->name.c_str(), port_name.c_str());
    return false;
  }
  if (cell_port_inputs.find(cell_port) != cell_port_inputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an input\n", cell->name.c_str(), port_name.c_str());
    return true;
  }

  ::Lgraph::error(
      "Could not find a definition for module {}, treating as a blackbox but could not determine whether {} is an input",
      cell->type.str(),
      port_name.str());

  log_error("input unknown port %s at module %s cell %s\n", port_name.c_str(), mod->name.c_str(), cell->type.c_str());
  I(false);  // TODO: is it possible to resolve this case?
  return false;
}

static void process_cell_drivers_intialization(RTLIL::Module *mod, Lgraph *g) {
  for (auto cell : mod->cells()) {
    if (cell->type == "$mem") {
      cell2node[cell] = resolve_memory(g, cell);
      continue;
    }

    cell2node[cell] = g->create_node();
    auto &node      = cell2node[cell];

    Sub_node *sub = nullptr;

    if (cell->type.c_str()[0] == '\\' || strncmp(cell->type.c_str(), "$paramod\\", 9) == 0) {  // sub_cell type
      mmap_lib::str mod_name(&(cell->type.c_str()[1]));

      sub = &g->ref_library()->setup_sub(mod_name);
    }

    for (const auto &conn : cell->connections()) {
      if (sub) {
        mmap_lib::str pin_name(&(conn.first.c_str()[1]));

        if (pin_name.is_i()) {
          // hardcoded pin position
          int pos = pin_name.to_i();

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
                        mod->name.c_str(),
                        cell->type.c_str(),
                        pin_name.to_s().c_str());
                continue;
              }
            }
          }
          auto io_pin = sub->get_io_pin_from_graph_pos(pos);
          if (io_pin.dir == Sub_node::Direction::Input)
            continue;

#ifndef NDEBUG
          printf("module %s cell type %s has output pin_name %s\n", mod->name.c_str(), cell->type.c_str(), pin_name.to_s().c_str());
#endif
        } else {
          if (!sub->has_pin(pin_name)) {
            if (cell->input(conn.first) || is_black_box_input(mod, cell, conn.first))
              sub->add_input_pin(pin_name);
            else if (cell->output(conn.first) || is_black_box_output(mod, cell, conn.first))
              sub->add_output_pin(pin_name);
          }

          if (!sub->has_pin(pin_name) || sub->is_input(pin_name))
            continue;

#ifndef NDEBUG
          printf("module %s submodule %s has pin_name %s\n", mod->name.c_str(), cell->type.c_str(), pin_name.to_s().c_str());
#endif
        }

        node.set_type_sub(sub->get_lgid());
      } else {
        node.set_type(Ntype_op::Or);  // it will be updated later. Just a generic 1 output cell

        if (cell->input(conn.first))
          continue;
      }

      Node_pin driver_pin;
      if (node.is_type_sub()) {
        mmap_lib::str pin_name(&(conn.first.c_str()[1]));
        driver_pin = node.setup_driver_pin(pin_name);
      } else {
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

        // fmt::print("dpin:{} bits:{} wire:{}\n", driver_pin.debug_name(), driver_pin.get_bits(), wire->name.str());

        if (chunk.width == wire->width) {
          if (chunk.width == ss.size()) {
            I(driver_pin.get_bits() == wire->width);  // bits should still not be set
            I(driver_pin.get_bits() == ss.size());
            // output port drives a single wire
            wire2pin[wire] = driver_pin;
            set_bits_wirename(driver_pin, wire);
          } else {
            I((chunk.width + offset) <= driver_pin.get_bits());
            // output port drives multiple wires
            Node_pin pick_pin = create_pick_operator(driver_pin, offset, chunk.width, wire->is_signed);
            wire2pin[wire]    = pick_pin;
            set_bits_wirename(pick_pin, wire);
          }
          offset += chunk.width;

        } else {
          if (partially_assigned.find(wire) == partially_assigned.end()) {
            partially_assigned[wire].resize(wire->width);
            partially_assigned_bits[wire].resize(wire->width);

            auto n2 = g->create_node(Ntype_op::Or, wire->width);
            if (wire2pin.find(wire) != wire2pin.end()) {
              auto dpin = wire2pin[wire];
              fmt::print("partial wire {} from module {} cell type {} (switching to partial node:{})\n",
                         wire->name.c_str(),
                         mod->name.c_str(),
                         cell->type.c_str(),
                         dpin.get_node().debug_name());
            }
            wire2pin[wire] = n2.setup_driver_pin();
          }

          auto src_pin = create_pick_operator(driver_pin, offset, chunk.width, wire->is_signed);
          offset += chunk.width;

          fmt::print("partial assign from node:{} to wire:{}[{}:{}]\n",
                     driver_pin.get_node().debug_name(),
                     wire->name.str(),
                     chunk.offset,
                     chunk.offset + chunk.width - 1);

          partially_assigned[wire][chunk.offset]      = src_pin;
          partially_assigned_bits[wire][chunk.offset] = chunk.width;
        }
      }
    }
  }
}

static void dump_partially_assigned() {
  for (auto it : partially_assigned) {
    const auto *wire = it.first;
    fmt::print("wire:{} width:{}\n", wire->name.str(), wire->width);

    int i = 0;
    while (i < (int)it.second.size()) {
      auto width = partially_assigned_bits[wire][i];
      if (width == 0) {
        i++;
        continue;
      }
      const auto &dpin = it.second[i];
      I(!dpin.is_invalid());

      fmt::print("   [{}:{}] name:{}\n", i, i + width - 1, dpin.debug_name());

#ifndef NDEBUG
      for (int j = i + 1; j < i + width; ++j) {
        I(partially_assigned_bits[wire][j] == 0);
      }
#endif

      i += width;
    }
  }
}

static void process_assigns(RTLIL::Module *mod, Lgraph *g) {
  for (const auto &conn : mod->connections()) {
    const RTLIL::SigSpec lhs = conn.first;
    const RTLIL::SigSpec rhs = conn.second;

    int global_lhs_pos = 0;

    for (auto &lchunk : lhs.chunks()) {
      const RTLIL::Wire *lhs_wire = lchunk.wire;

      // printf("Assignment to %s\n", lhs_wire->name.c_str());
      if (lchunk.width == 0)
        continue;

      if (lhs_wire->port_input) {
        log_error("Assignment to input port %s\n", lhs_wire->name.c_str());
      } else if (lchunk.width == lhs_wire->width) {
#ifndef NDEBUG
        if (lhs_wire->port_output) {
          auto it = std::find(pending_outputs.begin(), pending_outputs.end(), lhs_wire);
          I(it != pending_outputs.end());
        }
        if (wire2pin.find(lhs_wire) != wire2pin.end()) {
          auto dpin = wire2pin[lhs_wire];
          I(!dpin.get_node().has_inputs());
          I(dpin.get_bits() == lhs_wire->width);
        }
#endif
        Node_pin dpin = create_pick_concat_dpin(g, rhs.extract(lchunk.offset, lchunk.width), lhs_wire->is_signed);
        if (wire2pin.find(lhs_wire) != wire2pin.end()) {
          auto prev_dpin = wire2pin[lhs_wire];
          if (prev_dpin.has_outputs()) {  // OOPS, got used out of order (lack of topo here)
            I(prev_dpin.get_node().is_type(Ntype_op::Or));
            I(!prev_dpin.get_node().has_inputs());
            append_to_or_node(g, prev_dpin.get_node(), dpin, 0);
          } else {
            wire2pin[lhs_wire] = dpin;
          }
        } else {
          wire2pin[lhs_wire] = dpin;
        }

        global_lhs_pos += lhs_wire->width;

      } else {
        if (partially_assigned.find(lhs_wire) == partially_assigned.end()) {
          partially_assigned[lhs_wire].resize(lhs_wire->width);
          partially_assigned_bits[lhs_wire].resize(lhs_wire->width);

          if (wire2pin.find(lhs_wire) == wire2pin.end()) {
            auto or_node       = g->create_node(Ntype_op::Or, lhs_wire->width);
            wire2pin[lhs_wire] = or_node.setup_driver_pin();
          } else {
            I(wire2pin[lhs_wire].get_bits() == lhs_wire->width);
          }
        }

        {
          // Detect writes with local reads (loops) to handle specially to avoid combinational loops

          // STEP: look for dpins to write in lhs_wire from lchunk.offset to lchunk.offset+lchunk.width

          const int lhs_off = lchunk.offset;  // position to write in lchunk
          int       lhs_pos = 0;

          int global_rhs_pos = 0;
          for (auto &rchunk : rhs.chunks()) {
            // done for this lhs?
            if (lhs_pos >= lchunk.width) {
              break;
            }

            const int rhs_off = rchunk.offset;
            int       rhs_pos = 0;  // cross rchunk position read

            // advance rhs until end of chunk or reach the global_lhs_pos
            while (global_lhs_pos > global_rhs_pos && rhs_pos < rchunk.width) {
              global_rhs_pos++;
              rhs_pos++;
            }

            // done for this rchunk?
            if (rhs_pos == rchunk.width)
              continue;

            // There should be some overlap between rchunk and lchunk
            if (rchunk.wire == lhs_wire) {
              if (partially_assigned_fwd.find(lhs_wire) == partially_assigned_fwd.end()) {
                partially_assigned_fwd[lhs_wire].resize(lhs_wire->width);
              }
              int delta = (lhs_off + lhs_pos) - (rhs_off + rhs_pos);
              for (int pos = 0; pos < rchunk.width; ++pos) {
                I(lhs_off + lhs_pos < lhs_wire->width);
                I(partially_assigned_fwd[lhs_wire][lhs_off + lhs_pos] == 0);
                partially_assigned_fwd[lhs_wire][lhs_off + lhs_pos] = delta;
                ++lhs_pos;
                ++rhs_pos;  // not needed but to be symmetric
                ++global_lhs_pos;
                ++global_rhs_pos;
              }
              continue;
            }

            I(lhs_off + lhs_pos >= rhs_pos);

            int from_in_rchunk = rhs_pos + rhs_off;
            int max_bits       = std::min(rchunk.width, lchunk.width - lhs_pos);
            int to_in_rchunk   = from_in_rchunk + max_bits;  // not inclusive

            int bits_needed;
            if (to_in_rchunk > rchunk.offset + rchunk.width) {
              to_in_rchunk = rchunk.offset + rchunk.width;
              bits_needed  = to_in_rchunk - from_in_rchunk;
            } else {
              bits_needed = max_bits;
            }

            I(from_in_rchunk < rchunk.offset + rchunk.width);
            I(from_in_rchunk >= rchunk.offset);
            I(from_in_rchunk <= to_in_rchunk);
            I(from_in_rchunk >= rhs_off);
            I(bits_needed > 0);
            I(bits_needed <= rchunk.width);

            Node_pin dpin;
            if (rchunk.wire == nullptr) {
              dpin = resolve_constant(g, rchunk.data, false);
              if (from_in_rchunk > 0) {
                auto sra_node = g->create_node(Ntype_op::SRA, bits_needed);
                sra_node.setup_sink_pin("a").connect_driver(dpin);
                auto c_node = g->create_node_const(Lconst(from_in_rchunk));
                sra_node.setup_sink_pin("b").connect_driver(c_node);
                dpin = sra_node.setup_driver_pin();
              }
              if (to_in_rchunk < rchunk.width) {
                auto and_node = g->create_node(Ntype_op::And, bits_needed);
                and_node.connect_sink(dpin);
                auto c_node = g->create_node_const((Lconst(1) << Lconst(bits_needed)) - Lconst(1));
                and_node.connect_sink(c_node);
                dpin = and_node.setup_driver_pin();
              }
            } else {
              dpin = create_pick_operator(g, rchunk.wire, from_in_rchunk, bits_needed, true);
            }

#ifdef NDEBUG
            for (int pos = 0; pos < bits_needed; ++pos) {
              I(lhs_off + lhs_pos < lhs_wire->width);
              if (partially_assigned_fwd.find(lhs_wire) != partially_assigned_fwd.end()) {
                I(partially_assigned_fwd[lhs_wire][lhs_off + lhs_pos] == 0);
              }
              I(partially_assigned_bits[lhs_wire][lhs_off + lhs_pos] == 0);
            }
#endif
            partially_assigned[lhs_wire][lhs_off + lhs_pos]      = dpin;
            partially_assigned_bits[lhs_wire][lhs_off + lhs_pos] = bits_needed;
            lhs_pos += bits_needed;
            rhs_pos += bits_needed;
            global_lhs_pos += bits_needed;
            global_rhs_pos += bits_needed;
          }
        }
      }
    }
  }
}

static uint32_t get_input_size(const RTLIL::Cell *cell) {
  I(cell->known());  // use only in based yosys cells (known type)

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
  I(cell->known());  // use only in based yosys cells (known type)

  if (cell->hasParam(ID::Y_WIDTH))
    return cell->getParam(ID::Y_WIDTH).as_int();

  for (const auto &conn : cell->connections()) {
    if (cell->input(conn.first))
      continue;

    const RTLIL::SigSpec &ss = conn.second;
    if (ss.chunks().size() > 0)
      return ss.size();
  }

  I(0);

  return 0;  // must be determined later?
}

static void connect_partial_dpin(Lgraph *g, Node &or_node, uint32_t or_offset, uint32_t nbits, const Node_pin &current_dpin) {
  fmt::print("connect_partial_dpin to:{} or_offset:{} from pin:{} bits:{}\n",
             or_node.debug_name(),
             or_offset,
             current_dpin.debug_name(),
             nbits);

  I(or_node.get_type_op() == Ntype_op::Or);

  if (current_dpin.get_node().is_type(Ntype_op::Const)) {  // just use the dpin with zero/sign extend
    append_to_or_node(g, or_node, current_dpin, or_offset);
  } else if (current_dpin.get_bits() > nbits) {
    auto and_node = g->create_node(Ntype_op::And, nbits);
    and_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(nbits)) - 1));
    and_node.connect_sink(current_dpin);

    append_to_or_node(g, or_node, and_node.setup_driver_pin(), or_offset);
  } else if (current_dpin.get_bits() == nbits) {
    append_to_or_node(g, or_node, current_dpin, or_offset);
  } else {
    auto n_x = nbits - current_dpin.get_bits();

    auto n_z = current_dpin.get_bits();

    auto local_or_node = g->create_node(Ntype_op::Or, nbits);
    mmap_lib::str mask("0b");
    mask = mask.append(n_x, '?');
    mask = mask.append(n_z, '0');

    local_or_node.connect_sink(g->create_node_const(Lconst::from_pyrope(mask)));
    local_or_node.connect_sink(current_dpin);

    append_to_or_node(g, or_node, local_or_node.setup_driver_pin(), or_offset);
  }
}

static void process_partially_assigned_other(Lgraph *g) {
  for (const auto &it : partially_assigned) {
    const RTLIL::Wire *wire = it.first;
    I(it.second.size() == it.first->width);  // every bit set (maye be same dpin)

    auto or_dpin = get_partial_dpin(g, wire);
    auto or_node = or_dpin.get_node();

    int i = 0;
    while (i < (int)it.second.size()) {
      auto width = partially_assigned_bits[wire][i];
      if (width == 0) {
        i++;
        continue;
      }
      const auto &dpin = it.second[i];
      I(!dpin.is_invalid());

      connect_partial_dpin(g, or_node, i, width, dpin);

      i += width;
    }
  }
}

static void process_partially_assigned_self_chains(Lgraph *g) {
  for (const auto &kv : partially_assigned_fwd) {
    const RTLIL::Wire *wire = kv.first;

    // Find the order to process partially_assigned_fwd
    bool pending_chain = true;

    std::set<int> processed_pos;
    for (int pos = 0; pos < (int)partially_assigned_fwd[wire].size(); ++pos) {
      auto shift = partially_assigned_fwd[wire][pos];
      if (shift == 0) {  // no pending
        processed_pos.insert(pos);
      }
    }

    if ((int)processed_pos.size() == wire->width)
      return;  // nothing pending to do

    while (pending_chain) {
      pending_chain = false;
      absl::flat_hash_set<int> ready_pos;
      absl::flat_hash_set<int> shifts;

      for (int pos = 0; pos < (int)partially_assigned_fwd[wire].size(); ++pos) {
        auto shift = partially_assigned_fwd[wire][pos];
        if (shift == 0)
          continue;

        I(partially_assigned[wire].size() > pos);
        I(pos - shift >= 0);
        I(pos - shift < partially_assigned[wire].size());

        if (!processed_pos.count(pos - shift)) {
          pending_chain = true;  // more iterations needed
          continue;
        }

        ready_pos.insert(pos);
        shifts.insert(shift);
      }

      if (shifts.empty())
        return;  // done

      auto master_or_dpin = get_partial_dpin(g, wire);
      auto master_or_node = master_or_dpin.get_node();

      auto pre_or_node = g->create_node(Ntype_op::Or, wire->width);

      // Move all the inputs to the new pre
      for (auto e : master_or_node.inp_edges()) {
        pre_or_node.connect_sink(e.driver);
        e.del_edge();
      }
      I(!master_or_node.has_inputs());
      master_or_node.connect_sink(pre_or_node);

      for (auto shift : shifts) {
        Lconst      wr_mask(0);
        bool        started = false;
        const auto &v       = partially_assigned_fwd[wire];
        // for(int pos=0;pos<v.size();++pos)
        for (int pos = v.size() - 1; pos >= 0; --pos) {
          auto i = v[pos];
          if (started)
            wr_mask = wr_mask << 1;
          started = true;

          if (i == shift && processed_pos.count(pos - shift)) {
            wr_mask = wr_mask | 1;
          }
        }
        Lconst rd_mask;
        if (shift < 0)
          rd_mask = wr_mask.lsh_op(-shift).adjust_bits(wire->width);
        else
          rd_mask = wr_mask.rsh_op(shift).adjust_bits(wire->width);

        // fmt::print("wr_mask:{} = rd_mask:{}>>{} from node:{} wire:{}\n" ,wr_mask.to_yosys(), rd_mask.to_yosys(), shift,
        // master_or_node.debug_name(), wire->name.str());

        auto and_node = g->create_node(Ntype_op::And, wire->width);
        and_node.connect_sink(pre_or_node);
        and_node.connect_sink(g->create_node_const(rd_mask));

        auto tposs_node = g->create_node(Ntype_op::Get_mask, wire->width + 1);
        tposs_node.setup_sink_pin("a").connect_driver(and_node);
        tposs_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));

        I(shift);
        if (shift < 0) {
          auto sra_node = g->create_node(Ntype_op::SRA, wire->width);
          sra_node.setup_sink_pin("a").connect_driver(tposs_node);
          sra_node.setup_sink_pin("b").connect_driver(g->create_node_const(Lconst(-shift)));

          master_or_node.connect_sink(sra_node);
        } else {
          auto shl_node = g->create_node(Ntype_op::SHL, wire->width);
          shl_node.setup_sink_pin("a").connect_driver(tposs_node);
          shl_node.setup_sink_pin("b").connect_driver(g->create_node_const(Lconst(shift)));

          master_or_node.connect_sink(shl_node);
        }
      }

      for (auto pos : ready_pos) {
        processed_pos.insert(pos);
      }
    }
  }
}

static void connect_comparator(Node &exit_node, const RTLIL::Cell *cell) {
  // In yosys, the comparater output can be 1 or 0 or ..01 or ...00. bitwidth in LG allows to ignore this
  // I(cell->getParam(ID::Y_WIDTH).as_int() == 1);

  auto *g = exit_node.get_class_lgraph();

  auto a_dpin = get_unsigned_dpin(g, cell, ID::A);
  auto b_dpin = get_unsigned_dpin(g, cell, ID::B);

  exit_node.setup_sink_pin("A").connect_driver(a_dpin);

  if (exit_node.is_type(Ntype_op::EQ))
    exit_node.setup_sink_pin("A").connect_driver(b_dpin);
  else
    exit_node.setup_sink_pin("B").connect_driver(b_dpin);
}

static void process_partially_assigned(Lgraph *g) {
  dump_partially_assigned();

  process_partially_assigned_other(g);
  process_partially_assigned_self_chains(g);
}

static void process_connect_outputs(RTLIL::Module *mod, Lgraph *g) {
  (void)mod;

  // we need to connect global outputs to the cell that drives it
  for (auto *wire : pending_outputs) {
    mmap_lib::str wname(&wire->name.c_str()[1]);

    if (!g->has_graph_output(wname))
      g->add_graph_output(wname, wire->port_id, wire->width);

    if (wire2pin.find(wire) == wire2pin.end())
      continue;

    Node_pin dpin3 = wire2pin[wire];

    I(wire->port_output);
    if (dpin3.is_graph_output())
      continue;

    Node_pin spin = g->get_graph_output(wname);
    g->add_edge(dpin3, spin);  // , wire->width);

    auto dpin = spin.change_to_driver_from_graph_out_sink();
    if (wire->start_offset) {
      dpin.set_offset(wire->start_offset);
    }

    wire2pin[wire] = dpin;
  }
}

static void process_cells(RTLIL::Module *mod, Lgraph *g) {
  for (auto cell : mod->cells()) {
    // log("Looking for cell %s:\n", cell->type.c_str());

    I(cell2node.find(cell) != cell2node.end());
    Node exit_node = cell2node[cell];

    //--------------------------------------------------------------
    if (std::strncmp(cell->type.c_str(), "$and", 4) == 0) {
      exit_node.set_type(Ntype_op::And, get_output_size(cell));

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      if ((a_bits == y_bits && b_bits == y_bits) || (a_sign && b_sign)) {  // Common case
        exit_node.connect_sink(get_dpin(g, cell, ID::A));
        exit_node.connect_sink(get_dpin(g, cell, ID::B));
      } else {
        exit_node.connect_sink(get_unsigned_dpin(g, cell, ID::A));
        exit_node.connect_sink(get_unsigned_dpin(g, cell, ID::B));
      }
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$reduce_and", 11) == 0) {
      // No reduce and to avoid being width (drop zeroes) sensitive
      //
      // multibit inputs
      // And(Not(Ror(Not(inp))), inp.MSB)
      //
      // 1bit inputs
      //   And(inp1,inp2)

      bool all_1bit = true;
      I(cell->hasParam(ID::A_WIDTH));
      if (cell->getParam(ID::A_WIDTH).as_int() > 1) {
        all_1bit = false;
      }

      if (all_1bit) {
        connect_all_inputs(exit_node.setup_sink_pin(), cell);
      } else {
        auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
        Node and_node;
        if (y_bits == 1) {
          exit_node.set_type(Ntype_op::And, 1);
          and_node = exit_node;
        } else {
          and_node = g->create_node(Ntype_op::And, 1);
          exit_node.set_type(Ntype_op::Get_mask, y_bits);
          exit_node.setup_sink_pin("a").connect_driver(and_node);
          exit_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));
        }

        auto ror_node = g->create_node(Ntype_op::Ror, 1);

        auto not_ror_node = g->create_node(Ntype_op::Not, 1);
        not_ror_node.connect_sink(ror_node);

        and_node.connect_sink(not_ror_node);

        auto a_dpin = get_unsigned_dpin(g, cell, ID::A);
        auto a_bits = cell->getParam(ID::A_WIDTH).as_int();

        if (a_bits > 1) {
          auto sra_node = g->create_node(Ntype_op::SRA, 1);

          auto not_a_node = g->create_node(Ntype_op::Not, a_bits);
          not_a_node.connect_sink(a_dpin);

          ror_node.connect_sink(not_a_node);

          sra_node.setup_sink_pin("a").connect_driver(a_dpin);
          sra_node.setup_sink_pin("b").connect_driver(g->create_node_const(a_bits - 1));

          and_node.connect_sink(sra_node);
        } else {
          and_node.connect_sink(a_dpin);
        }
      }
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$logic_and", 10) == 0 || std::strncmp(cell->type.c_str(), "$logic_or", 9) == 0) {
      I(cell->hasParam(ID::A_WIDTH));
      I(cell->hasParam(ID::B_WIDTH));

      auto op = Ntype_op::Or;
      if (std::strncmp(cell->type.c_str(), "$logic_and", 10) == 0) {
        op = Ntype_op::And;
      }

      auto a_dpin = get_dpin(g, cell, ID::A);
      auto b_dpin = get_dpin(g, cell, ID::B);

      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      Node and_node;
      if (y_bits == 1) {
        exit_node.set_type(op, 1);
        and_node = exit_node;
      } else {
        and_node = g->create_node(op, 1);
        exit_node.set_type(Ntype_op::Get_mask, y_bits);
        exit_node.setup_sink_pin("a").connect_driver(and_node);
        exit_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));
      }

      if (a_bits == 1) {
        and_node.connect_sink(a_dpin);
      } else {
        auto ror_node = g->create_node(Ntype_op::Ror, 1);
        ror_node.connect_sink(a_dpin);
        and_node.connect_sink(ror_node);
      }
      if (b_bits == 1) {
        and_node.connect_sink(b_dpin);
      } else {
        auto ror_node = g->create_node(Ntype_op::Ror, 1);
        ror_node.connect_sink(b_dpin);
        and_node.connect_sink(ror_node);
      }

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$not", 4) == 0) {
      I(get_input_size(cell) == get_output_size(cell));
      exit_node.set_type(Ntype_op::Not, get_output_size(cell));

      connect_all_inputs(exit_node.setup_sink_pin(), cell);
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$logic_not", 10) == 0) {
      auto entry_node = g->create_node(Ntype_op::Ror, 1);

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      if (y_bits == 1) {
        // Not needed to have the ror if a_bits==1, let it to the cprop pass?
        exit_node.set_type(Ntype_op::Not, 1);
        exit_node.connect_sink(entry_node);
      } else {
        auto not_node = g->create_node(Ntype_op::Not, 1);
        not_node.connect_sink(entry_node);

        exit_node.set_type(Ntype_op::Get_mask, y_bits);
        exit_node.setup_sink_pin("a").connect_driver(not_node);
        exit_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));
      }

      connect_all_inputs(entry_node.setup_sink_pin(), cell);
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$or", 3) == 0) {
      exit_node.set_type(Ntype_op::Or, get_output_size(cell));

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      if ((a_bits == y_bits && b_bits == y_bits) || (a_sign && b_sign)) {  // Common case
        exit_node.connect_sink(get_dpin(g, cell, ID::A));
        exit_node.connect_sink(get_dpin(g, cell, ID::B));
      } else {
        exit_node.connect_sink(get_unsigned_dpin(g, cell, ID::A));
        exit_node.connect_sink(get_unsigned_dpin(g, cell, ID::B));
      }

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$reduce_or", 10) == 0
               || std::strncmp(cell->type.c_str(), "$reduce_bool", 12) == 0) {
      Node_pin entry_pin;

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      if (y_bits == 1) {
        exit_node.set_type(Ntype_op::Ror, 1);
        entry_pin = exit_node.setup_sink_pin();
      } else {
        auto ror_node = g->create_node(Ntype_op::Ror, 1);
        entry_pin     = ror_node.setup_sink_pin();

        exit_node.set_type(Ntype_op::Get_mask, y_bits);
        exit_node.setup_sink_pin("a").connect_driver(ror_node);
        exit_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));
      }

      connect_all_inputs(entry_pin, cell);
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$xor", 4) == 0) {
      exit_node.set_type(Ntype_op::Xor, get_output_size(cell));

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      if ((a_bits == y_bits && b_bits == y_bits) || (a_sign && b_sign)) {  // Common case
        connect_all_inputs(exit_node.setup_sink_pin(), cell);
      } else {
        exit_node.connect_sink(get_unsigned_dpin(g, cell, ID::A));
        exit_node.connect_sink(get_unsigned_dpin(g, cell, ID::B));
      }

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$reduce_xor", 11) == 0) {
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto a_dpin = get_dpin(g, cell, ID::A);
      auto y_bits = get_output_size(cell);  // in yosys, it can be 00001

      if (a_bits == 1 && y_bits == 1) {  // pass it through
        exit_node.set_type(Ntype_op::And, 1);
        exit_node.connect_sink(a_dpin);
      } else {
        Node and_node;

        if (y_bits == 1) {
          exit_node.set_type(Ntype_op::And, 1);
          and_node = exit_node;
        } else {
          and_node = g->create_node(Ntype_op::And, 1);

          exit_node.set_type(Ntype_op::Get_mask, 1);
          exit_node.setup_sink_pin("a").connect_driver(and_node);
          exit_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));
        }

        auto xor_node = g->create_node(Ntype_op::Xor, 1);
        xor_node.connect_sink(a_dpin);

        for (int i = 1; i < a_bits; ++i) {
          auto sra_node = g->create_node(Ntype_op::SRA, 1);

          sra_node.setup_sink_pin("a").connect_driver(a_dpin);
          sra_node.setup_sink_pin("b").connect_driver(g->create_node_const(i));

          xor_node.connect_sink(sra_node);
        }

        and_node.connect_sink(xor_node);
        and_node.connect_sink(g->create_node_const(1));  // ,y_bits));
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

      auto entry_node = g->create_node(Ntype_op::Xor, size);
      exit_node.set_type(Ntype_op::Not, size);
      entry_node.connect_driver(exit_node);

      connect_all_inputs(entry_node.setup_sink_pin(), cell);
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$reduce_xnor", 11) == 0) {
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto a_dpin = get_dpin(g, cell, ID::A);
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

      if (a_bits == 1 && y_bits == 1) {  // pass it through
        exit_node.set_type(Ntype_op::Not, 1);
        exit_node.connect_sink(a_dpin);
      } else {
        Node not_node;
        if (y_bits == 1) {
          exit_node.set_type(Ntype_op::Not, 1);
          not_node = exit_node;
        } else {
          not_node = g->create_node(Ntype_op::Not, 1);

          exit_node.set_type(Ntype_op::Get_mask, 1);
          exit_node.setup_sink_pin("a").connect_driver(not_node);
          exit_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));
        }

        auto and_node = g->create_node(Ntype_op::And, 1);
        and_node.connect_sink(g->create_node_const(1));  // ,1));

        auto xor_node = g->create_node(Ntype_op::Xor, 1);
        xor_node.connect_sink(a_dpin);

        for (int i = 1; i < a_bits; ++i) {
          auto sra_node = g->create_node(Ntype_op::SRA, 1);

          sra_node.setup_sink_pin("a").connect_driver(a_dpin);
          sra_node.setup_sink_pin("b").connect_driver(g->create_node_const(i));

          xor_node.connect_sink(sra_node);
        }

        and_node.connect_sink(xor_node);
        not_node.connect_sink(and_node);
      }

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$dff", 4) == 0 || std::strncmp(cell->type.c_str(), "$dffe", 5) == 0
               || std::strncmp(cell->type.c_str(), "$dffsr", 6) == 0 || std::strncmp(cell->type.c_str(), "$adff", 5) == 0
               || std::strncmp(cell->type.c_str(), "$adffe", 6) == 0 || std::strncmp(cell->type.c_str(), "$adffsr", 7) == 0
               || std::strncmp(cell->type.c_str(), "$sdff", 5) == 0 || std::strncmp(cell->type.c_str(), "$sdffe", 6) == 0
               || std::strncmp(cell->type.c_str(), "$sdffsr", 7) == 0) {
      exit_node.set_type(Ntype_op::Flop, get_output_size(cell));

      if (cell->hasPort(ID::Q)) {
        const RTLIL::Wire *wire = cell->getPort(ID::Q).chunks()[0].wire;
        if (wire) {
          exit_node.setup_driver_pin().set_name(mmap_lib::str(wire->name.str()));
        }
      }

      // NOTE: yosys has 0s and 1s but multibit flops, but it is not a polarity per bit
      if (cell->hasParam(ID::CLK_POLARITY) && !cell->getParam(ID::CLK_POLARITY).as_bool()) {
        exit_node.setup_sink_pin("posclk").connect_driver(g->create_node_const(0));
      }

      if (cell->hasParam(ID::CLR)) {
        I(false);  // get a test using this and debug
        bool wants_negreset = false;
        if (cell->hasParam(ID::EN)) {
          if (cell->hasParam(ID::EN_POLARITY)) {
            wants_negreset = !cell->getParam(ID::EN_POLARITY).as_bool();
          }
          exit_node.setup_sink_pin("enable").connect_driver(get_dpin(g, cell, ID::EN));
        }

        if (cell->hasParam(ID::CLR_POLARITY)) {
          bool wants_negreset2 = cell->getParam(ID::CLR_POLARITY).as_int() == 0;
          I(wants_negreset2 == wants_negreset);  // both agree
        }
        exit_node.setup_sink_pin("reset").connect_driver(get_dpin(g, cell, ID::CLR));
      } else if (cell->hasPort(ID::SRST)) {
        if (cell->hasParam(ID::SRST_POLARITY)) {
          if (cell->getParam(ID::SRST_POLARITY).as_bool()) {
            exit_node.setup_sink_pin("negreset").connect_driver(g->create_node_const(0));
          } else {
            exit_node.setup_sink_pin("negreset").connect_driver(g->create_node_const(1));
          }
        }

        exit_node.setup_sink_pin("reset").connect_driver(get_dpin(g, cell, ID::SRST));

        if (cell->hasParam(ID::SRST_VALUE)) {
          const auto &v = cell->getParam(ID::SRST_VALUE);
          if (!v.is_fully_zero())
            exit_node.setup_sink_pin("initial").connect_driver(get_dpin(g, cell, ID::SRST_VALUE));
        }
      } else if (cell->hasPort(ID::ARST)) {
        if (cell->hasParam(ID::ARST_POLARITY)) {
          if (cell->getParam(ID::ARST_POLARITY).as_bool()) {
            exit_node.setup_sink_pin("negreset").connect_driver(g->create_node_const(0));
          } else {
            exit_node.setup_sink_pin("negreset").connect_driver(g->create_node_const(1));
          }
        }

        exit_node.setup_sink_pin("reset").connect_driver(get_dpin(g, cell, ID::ARST));

        if (cell->hasParam(ID::ARST_VALUE)) {
          const auto &v = cell->getParam(ID::ARST_VALUE);
          if (!v.is_fully_zero())
            exit_node.setup_sink_pin("initial").connect_driver(get_dpin(g, cell, ID::ARST_VALUE));
        }
      }

      I(!cell->hasParam(ID::SET));  // FIXME: active low not supported in LG (add mux before sflop)

      if (cell->hasPort(ID::EN)) {
        auto enable_dpin = get_dpin(g, cell, ID::EN);
        if (cell->hasParam(ID::EN_POLARITY) && !cell->getParam(ID::EN_POLARITY).as_bool()) {
          auto not_node = g->create_node(Ntype_op::Not, 1);
          not_node.connect_sink(enable_dpin);
          exit_node.setup_sink_pin("enable").connect_driver(not_node.get_driver_pin());
        } else {
          exit_node.setup_sink_pin("enable").connect_driver(enable_dpin);
        }
      }
      if (std::strncmp(cell->type.c_str(), "$adff", 5) == 0 || std::strncmp(cell->type.c_str(), "$adffe", 6) == 0
          || std::strncmp(cell->type.c_str(), "$adffsr", 7) == 0) {
        exit_node.setup_sink_pin("async").connect_driver(g->create_node_const(1));
      }

      exit_node.setup_sink_pin("clock").connect_driver(get_dpin(g, cell, ID::CLK));
      exit_node.setup_sink_pin("din").connect_driver(get_dpin(g, cell, ID::D));
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$dlatch", 7) == 0) {
      exit_node.set_type(Ntype_op::Latch, get_output_size(cell));

      if (cell->hasParam(ID::EN_POLARITY) && !cell->getParam(ID::EN_POLARITY).as_bool()) {
        exit_node.setup_sink_pin("posclk").connect_driver(g->create_node_const(0));
      }

      exit_node.setup_sink_pin("din").connect_driver(get_dpin(g, cell, ID::D));
      exit_node.setup_sink_pin("enable").connect_driver(get_dpin(g, cell, ID::EN));

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$neg", 4) == 0) {  // WARNING: before $ne
      exit_node.set_type(Ntype_op::Sum, get_output_size(cell));

      exit_node.setup_sink_pin("A").connect_driver(g->create_node_const(0));
      exit_node.setup_sink_pin("B").connect_driver(get_dpin(g, cell, ID::A));
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$lt", 3) == 0 || std::strncmp(cell->type.c_str(), "$gt", 3) == 0
               || std::strncmp(cell->type.c_str(), "$eq", 3) == 0) {
      Ntype_op op = Ntype_op::LT;
      if (std::strncmp(cell->type.c_str(), "$gt", 3) == 0) {
        op = Ntype_op::GT;
      } else if (std::strncmp(cell->type.c_str(), "$eq", 3) == 0) {
        op = Ntype_op::EQ;
      }
      int y_bits = get_output_size(cell);
      if (y_bits == 1) {
        exit_node.set_type(op, 1);
        connect_comparator(exit_node, cell);
      } else {
        auto cmp_node = g->create_node(op, 1);

        exit_node.set_type(Ntype_op::Get_mask, 1);
        exit_node.setup_sink_pin("a").connect_driver(cmp_node);
        exit_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));

        connect_comparator(cmp_node, cell);
      }
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$ge", 3) == 0 || std::strncmp(cell->type.c_str(), "$le", 3) == 0
               || std::strncmp(cell->type.c_str(), "$ne", 3) == 0) {  // WARNING: after $neg

      Ntype_op op = Ntype_op::LT;
      if (std::strncmp(cell->type.c_str(), "$le", 3) == 0) {
        op = Ntype_op::GT;
      } else if (std::strncmp(cell->type.c_str(), "$ne", 3) == 0) {
        op = Ntype_op::EQ;
      }

      Node cmp_node = g->create_node(op, 1);
      connect_comparator(cmp_node, cell);

      int y_bits = get_output_size(cell);
      if (y_bits == 1) {
        exit_node.set_type(Ntype_op::Not, 1);
        exit_node.connect_sink(cmp_node);
      } else {
        auto not_node = g->create_node(Ntype_op::Not, 1);
        not_node.connect_sink(cmp_node);

        exit_node.set_type(Ntype_op::Get_mask, y_bits);
        exit_node.setup_sink_pin("a").connect_driver(not_node);
        exit_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));
      }
      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$mux", 4) == 0) {
      exit_node.set_type(Ntype_op::Mux, get_output_size(cell));

      exit_node.setup_sink_pin("0").connect_driver(get_dpin(g, cell, ID::S));
      exit_node.setup_sink_pin("1").connect_driver(get_dpin(g, cell, ID::A));
      exit_node.setup_sink_pin("2").connect_driver(get_dpin(g, cell, ID::B));

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$add", 4) == 0 || std::strncmp(cell->type.c_str(), "$sub", 4) == 0) {
      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      (void)a_bits;

      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();
      (void)b_bits;

      int y_bits = get_output_size(cell);

      auto sum_node = exit_node;
      if (y_bits <= a_bits || y_bits <= b_bits) {
        sum_node = g->create_node(Ntype_op::Sum, y_bits);

        if (false && a_sign && b_sign) {
          // out = Not(And(Not(sum(....)), y_mask)) - Keep sign
          auto not_node = g->create_node(Ntype_op::Not, y_bits);
          not_node.connect_sink(sum_node);

          auto and_node = g->create_node(Ntype_op::And, y_bits);
          and_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(y_bits)) - 1));
          and_node.connect_sink(not_node);

          exit_node.set_type(Ntype_op::Not, y_bits);
          exit_node.connect_sink(and_node);
        } else {
          // out = And(sum(....), y_mask)
          exit_node.set_type(Ntype_op::And, y_bits);

          exit_node.connect_sink(sum_node);
          exit_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(y_bits)) - 1));
        }
      } else {
        exit_node.set_type(Ntype_op::Sum, y_bits);
      }

      mmap_lib::str b{"A"};
      if (std::strncmp(cell->type.c_str(), "$sub", 4) == 0)
        b = "B";

      if (a_sign && b_sign) {
        auto a_dpin = get_dpin(g, cell, ID::A);
        auto b_dpin = get_dpin(g, cell, ID::B);

        sum_node.setup_sink_pin("A").connect_driver(a_dpin);
        sum_node.setup_sink_pin(b).connect_driver(b_dpin);
      } else {
        auto a_dpin = get_unsigned_dpin(g, cell, ID::A);
        auto b_dpin = get_unsigned_dpin(g, cell, ID::B);

        sum_node.setup_sink_pin("A").connect_driver(a_dpin);
        sum_node.setup_sink_pin(b).connect_driver(b_dpin);
      }

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$mul", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

#ifdef CELL_SIZE_IGNORE
      exit_node.set_type(Ntype_op::And, y_bits);
      auto mul_node = g->create_node(Ntype_op::Mult, y_bits);

      exit_node.connect_sink(mul_node);
      exit_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(y_bits)) - 1));
#else
      exit_node.set_type(Ntype_op::Mult, y_bits);
      auto mul_node = exit_node;
#endif
      mul_node.setup_sink_pin("A").connect_driver(get_dpin(g, cell, ID::A));
      mul_node.setup_sink_pin("A").connect_driver(get_dpin(g, cell, ID::B));

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$div", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

#ifdef CELL_SIZE_IGNORE
      exit_node.set_type(Ntype_op::And, y_bits);
      auto div_node = g->create_node(Ntype_op::Div, y_bits);

      exit_node.connect_sink(div_node);
      exit_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(y_bits)) - 1));
#else
      exit_node.set_type(Ntype_op::Div, y_bits);
      auto div_node = exit_node;
#endif
      auto a_dpin = get_unsigned_dpin(g, cell, ID::A);
      auto b_dpin = get_unsigned_dpin(g, cell, ID::B);

      div_node.setup_sink_pin("a").connect_driver(a_dpin);
      div_node.setup_sink_pin("b").connect_driver(b_dpin);

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

      Node_pin a_dpin = get_unsigned_dpin(g, cell, ID::A);
      Node_pin b_dpin = get_unsigned_dpin(g, cell, ID::B);

      exit_node.set_type(Ntype_op::And, y_bits);
      exit_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(y_bits)) - 1));

      div_node.setup_sink_pin("a").connect_driver(a_dpin);
      div_node.setup_sink_pin("b").connect_driver(b_dpin);

      mul_node.setup_sink_pin("A").connect_driver(div_node);
      mul_node.setup_sink_pin("A").connect_driver(b_dpin);

      sub_node.setup_sink_pin("A").connect_driver(a_dpin);
      sub_node.setup_sink_pin("B").connect_driver(mul_node);

      exit_node.connect_sink(sub_node);

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$pos", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

      if (cell->getParam(ID::A_SIGNED).as_bool()) {
        exit_node.set_type(Ntype_op::And, y_bits);
        exit_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(y_bits)) - 1));
        exit_node.connect_sink(get_dpin(g, cell, ID::A));
      } else {
        auto and_node = g->create_node(Ntype_op::And, y_bits);
        and_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(y_bits)) - 1));
        and_node.connect_sink(get_unsigned_dpin(g, cell, ID::A));

        exit_node.set_type(Ntype_op::Get_mask, y_bits + 1);
        exit_node.setup_sink_pin("a").connect_driver(and_node);
        exit_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));
      }

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && cell->getParam(ID::B_SIGNED).as_bool()) {
      // y0 = SRA(Tposs(A),Tposs(B))
      // y1 = SHL(A, -B)
      // y2 = mux(B<0, Y0, Y1)
      // y  = Y2 & Y.__mask

      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

      auto max_out_bits = a_bits + (1 << b_bits);

      exit_node.set_type(Ntype_op::And, y_bits);

      auto sra_node = g->create_node(Ntype_op::SRA, max_out_bits);
      auto shl_node = g->create_node(Ntype_op::SHL, max_out_bits);
      auto mux_node = g->create_node(Ntype_op::Mux, max_out_bits);
      auto neg_node = g->create_node(Ntype_op::Sum, b_bits);
      auto lt_node  = g->create_node(Ntype_op::LT, 1);

      auto a_dpin = get_dpin(g, cell, ID::A);
      auto b_dpin = get_dpin(g, cell, ID::B);

      //--

      sra_node.setup_sink_pin("a").connect_driver(get_unsigned_dpin(g, cell, ID::A));
      sra_node.setup_sink_pin("b").connect_driver(get_dpin(g, cell, ID::B));

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
      exit_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(y_bits)) - 1));

    } else if (std::strncmp(cell->type.c_str(), "$shr", 4) == 0
               || (std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && !cell->getParam(ID::B_SIGNED).as_bool())
               || (std::strncmp(cell->type.c_str(), "$sshr", 5) == 0 && !cell->getParam(ID::A_SIGNED).as_bool())) {
      // y = SRA(Tposs(A),B) - unsigned shift right

      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      (void)a_bits;

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      exit_node.set_type(Ntype_op::SRA, y_bits);

      Node_pin dpin_a;
      Node_pin dpin_a_signed = get_dpin(g, cell, ID::A);
      if (cell->getParam(ID::A_SIGNED).as_bool()) {
        if ((int)dpin_a_signed.get_bits() < y_bits) {
          auto and_node = g->create_node(Ntype_op::And, y_bits);
          and_node.connect_sink(dpin_a_signed);
          and_node.connect_sink(g->create_node_const((Lconst(1) << Lconst(y_bits)) - 1));

          auto tposs_node = g->create_node(Ntype_op::Get_mask, y_bits + 1);
          tposs_node.setup_sink_pin("a").connect_driver(and_node);
          tposs_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));

          dpin_a = tposs_node.setup_driver_pin();
        }
      }
      if (dpin_a.is_invalid()) {
        auto node = dpin_a_signed.get_node();
        if (node.is_type(Ntype_op::Get_mask)) {
          dpin_a = dpin_a_signed;
        } else if (node.is_type_const() && !node.get_type_const().is_negative()) {
          dpin_a = dpin_a_signed;
        } else {
          auto tposs_node = g->create_node(Ntype_op::Get_mask, dpin_a_signed.get_bits() + 1);
          tposs_node.setup_sink_pin("a").connect_driver(dpin_a_signed);
          tposs_node.setup_sink_pin("mask").connect_driver(g->create_node_const(-1));

          dpin_a = tposs_node.setup_driver_pin();
        }
      }

      exit_node.setup_sink_pin("a").connect_driver(dpin_a);
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
      uint32_t width = cell->getParam(ID::WIDTH).as_int();
      (void)width;

      uint32_t depth  = cell->getParam(ID::SIZE).as_int();
      uint32_t offset = cell->getParam(ID::OFFSET).as_int();
      (void)offset;

      auto abits = cell->getParam(ID::ABITS).as_int();
      (void)abits;

      auto rdports = cell->getParam(ID::RD_PORTS).as_int();
      auto wrports = cell->getParam(ID::WR_PORTS).as_int();

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

      connect_constant(g, depth, exit_node, LgRAPH_MEMOP_SIZE);
      connect_constant(g, offset, exit_node, LgRAPH_MEMOP_OFFSET);
      connect_constant(g, abits, exit_node, LgRAPH_MEMOP_ABITS);
      connect_constant(g, wrports, exit_node, LgRAPH_MEMOP_WRPORT);
      connect_constant(g, rdports, exit_node, LgRAPH_MEMOP_RDPORT);

      // lgraph has reversed convention compared to yosys.
      int rd_clk_enabled = 0;
      int rd_clk_polarity = 0;
      for (int i = 0; i < rd_clkp.size(); i++) {
        if (rd_clke[i] != RTLIL::S1)
          continue;

        if (rd_clkp[i] == RTLIL::S1) {
          if (rd_clk_enabled)
            I(rd_clk_polarity);  // All the read ports are equal
          rd_clk_enabled = 1;
          rd_clk_polarity = 1;
        } else if (rd_clkp[i] == RTLIL::S0) {
          if (rd_clk_enabled)
            I(!rd_clk_polarity);  // All the read ports are equal
          rd_clk_enabled = 1;
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
      int wr_clk_enabled = 0;
      int wr_clk_polarity = 0;
      for (int i = 0; i < wr_clkp.size(); i++) {
        if (wr_clke[i] != RTLIL::S1)
          continue;
        if (wr_clkp[i] == RTLIL::S1) {
          if (wr_clk_enabled)
            I(wr_clk_polarity);  // All the read ports are equal
          wr_clk_enabled = 1;
          wr_clk_polarity = 1;
        } else if (wr_clkp[i] == RTLIL::S0) {
          if (wr_clk_enabled)
            I(!wr_clk_polarity);  // All the read ports are equal
          wr_clk_enabled = 1;
          wr_clk_polarity = 0;
        }
      }
      rd_clk_polarity = rd_clk_polarity ? 0 : 1;  // polarity flipped in lgraph vs yosys
      wr_clk_polarity = wr_clk_polarity ? 0 : 1;  // polarity flipped in lgraph vs yosys

      if (rd_clk_enabled)
        connect_constant(g, rd_clk_polarity, exit_node, LgRAPH_MEMOP_RDCLKPOL);
      if (wr_clk_enabled)
        connect_constant(g, wr_clk_polarity, exit_node, LgRAPH_MEMOP_WRCLKPOL);

      connect_constant(g, transp.as_int(), exit_node, LgRAPH_MEMOP_RDTRAN);

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
      exit_node.set_type(Ntype_op::Flop, get_output_size(cell));

      exit_node.setup_sink_pin("clock").connect_driver(get_dpin(g, cell, ID::C));
      exit_node.setup_sink_pin("din").connect_driver(get_dpin(g, cell, ID::D));

      //--------------------------------------------------------------
    } else if (std::strncmp(cell->type.c_str(), "$_DFF_N_", 8) == 0) {
      exit_node.set_type(Ntype_op::Flop, get_output_size(cell));

      exit_node.setup_sink_pin("posclk").connect_driver(g->create_node_const(0));
      exit_node.setup_sink_pin("clock").connect_driver(get_dpin(g, cell, ID::C));
      exit_node.setup_sink_pin("din").connect_driver(get_dpin(g, cell, ID::D));

    } else if (std::strncmp(cell->type.c_str(), "$_DFF_NN", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_NP", 8) == 0
               || std::strncmp(cell->type.c_str(), "$_DFF_PP", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_PN", 8) == 0) {
      // TODO: add support for those DFF types
      log_error("Found complex yosys DFFs, run `techmap -map +/adff2dff.v` before calling the yosys2lg pass\n");
      I(false);

    } else if (cell->type.c_str()[0] == '\\' || strncmp(cell->type.c_str(), "$paramod\\", 9) == 0) {  // sub_cell type
      I(exit_node.is_type_sub());

      absl::flat_hash_set<XEdge::Compact> added_edges;

      const auto &sub = exit_node.get_type_sub_node();

      for (auto &conn : cell->connections()) {
        RTLIL::SigSpec ss = conn.second;
        if (ss.size() == 0)
          continue;

        mmap_lib::str name(&conn.first.c_str()[1]);

        if (sub.is_output(name))
          continue;
        if (!sub.is_input(name)) {
          log_error("sub:%s does not have pin:%s as input\n", sub.get_name().to_s().c_str(), name.to_s().c_str());
        }

        Node_pin spin = exit_node.setup_sink_pin(name);
        if (spin.is_invalid())
          continue;

        Node_pin dpin = create_pick_concat_dpin(g, ss, true);

        if (added_edges.find(XEdge(dpin, spin).get_compact()) != added_edges.end()) {
          // there are two edges from dpin to spin
          // this is not allowed in lgraph, add a Ntype_op::Or in between
          auto or_node = g->create_node(Ntype_op::Or, ss.size());
          g->add_edge(dpin, or_node.setup_sink_pin());
          dpin = or_node.setup_driver_pin();
        }
        g->add_edge(dpin, spin);
        added_edges.insert(XEdge(dpin, spin).get_compact());
      }

    } else {
      // blackbox addition
      ::Lgraph::warn("Black box addition from yosys frontend, cell type {} not found instance {}",
                     cell->type.c_str(),
                     cell->name.c_str());
      I(false);
    }
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
    mmap_lib::str path("lgdb");

    for (argidx = 1; argidx < args.size(); argidx++) {
      if (args[argidx] == "-path") {
        path = mmap_lib::str(args[++argidx]);
        continue;
      }
      break;
    }

    // handle extra options (e.g. selection)
    extra_args(args, argidx, design);

    ct_all.setup(design);
    cell_port_outputs.clear();
    cell_port_inputs.clear();
    driven_signals.clear();

    auto library = Graph_library::instance(path);

    for (auto &it : design->modules_) {
      RTLIL::Module *mod = it.second;
      mmap_lib::str mod_name(&mod->name.c_str()[1]);

      auto *g= library->try_find_lgraph(mod_name);
      if (g == nullptr) {
        g = ::Lgraph::create(path, mod_name, "-");
      }
      Sub_node *sub = g->ref_self_sub_node();

      for (const auto &port : mod->ports) {
        RTLIL::Wire *wire = mod->wire(port);
        mmap_lib::str  wire_name(&wire->name.c_str()[1]);

        auto cell_port = mmap_lib::str::concat(mod->name.str(), "_:_", wire->name.str());
        if (wire->port_input && !wire->port_output) {
          // fmt::print("mod:{} inp:{}\n", mod->name.str(), wire->name.str());
          cell_port_inputs.insert(cell_port);
          if (sub->has_pin(wire_name))
            sub->map_graph_pos(wire_name, Sub_node::Direction::Input, wire->port_id);
          else
            g->add_graph_input(wire_name, wire->port_id, wire->width);
        } else if (!wire->port_input && wire->port_output) {
          // fmt::print("mod:{} out:{}\n", mod->name.str(), wire->name.str());
          cell_port_outputs.insert(cell_port);
          if (sub->has_pin(wire_name))
            sub->map_graph_pos(wire_name, Sub_node::Direction::Output, wire->port_id);
          else
            g->add_graph_output(wire_name, wire->port_id, wire->width);
        } else {
          ::Lgraph::error("inou.yosys.tolg: mod:{} bidirectional:{} NOT supported by livehd\n",
                          mod->name.str(),
                          wire->name.str());
        }
      }
    }

    for (auto &it : design->modules_) {
      RTLIL::Module *mod = it.second;
      if (design->selected_module(it.first)) {
        mmap_lib::str mod_name(&(mod->name.c_str()[1]));
#ifndef NDEBUG
        fmt::print("inou.yosys.tolg module:{}\n", mod_name);
#endif

        Lbench b("inou.YOSYS_tolg_" + mod_name.to_s());

        for (const auto &port : mod->ports) {
          RTLIL::Wire *wire = mod->wire(port);
          mmap_lib::str  wire_name(&wire->name.c_str()[1]);
          if (wire->port_output) {
            pending_outputs.emplace_back(wire);
          }
        }

        auto *g = library->try_find_lgraph(mod_name);
        I(g);

        process_cell_drivers_intialization(mod, g);
        process_assigns(mod, g);
        process_cells(mod, g);
        process_partially_assigned(g);
        process_connect_outputs(mod, g);

        wire2pin.clear();
        cell2node.clear();
        partially_assigned.clear();
        picks.clear();
        pending_outputs.clear();
      }
    }
  }
} Yosys2lg_Pass;

PRIVATE_NAMESPACE_END
