//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// External package includes
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"

#include <algorithm>
#include <cstring>
#include <format>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "kernel/celltypes.h"
#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#pragma GCC diagnostic pop

// LiveHD includes — HHDS only (no //lgraph dep)
#include "graph_library_singleton.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "perf_tracing.hpp"
#include "str_tools.hpp"

using livehd::Hhds_graph_library;
using livehd::graph_util::bits_of;
using livehd::graph_util::const_value_of;
using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::debug_name;
using livehd::graph_util::has_name;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::node_name_of;
using livehd::graph_util::pin_name_of;
using livehd::graph_util::set_bits;
using livehd::graph_util::set_loc1;
using livehd::graph_util::set_pin_name;
using livehd::graph_util::set_pin_offset;
using livehd::graph_util::set_source;
using livehd::graph_util::set_type_op;
using livehd::graph_util::setup_sink_by_name;
using livehd::graph_util::type_op_of;

// When true, the cell bits should have no effect (set to zero or large num for
// bitwidth to adjust should work too)
#define CELL_SIZE_IGNORE 1

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

static CellTypes                        ct_all;
static absl::flat_hash_set<size_t>      driven_signals;
static absl::flat_hash_set<std::string> cell_port_inputs;
static absl::flat_hash_set<std::string> cell_port_outputs;

typedef std::pair<const RTLIL::Wire*, int> Wire_bit;

static absl::flat_hash_map<const RTLIL::Wire*, hhds::Pin_class>              wire2pin;
static absl::flat_hash_map<const RTLIL::Cell*, hhds::Node_class>             cell2node;
static absl::flat_hash_map<const RTLIL::Wire*, std::vector<hhds::Pin_class>> partially_assigned;
static absl::flat_hash_map<const RTLIL::Wire*, std::vector<int>>             partially_assigned_bits;
static absl::flat_hash_map<const RTLIL::Wire*, std::vector<int>>             partially_assigned_fwd;

static std::vector<const RTLIL::Wire*> pending_outputs;

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------
namespace {

[[nodiscard]] Dlop hydrate_const_pin(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return *Dlop::create_integer(0);
  }
  auto master = pin.get_master_node();
  auto s      = const_value_of(master);
  if (s.empty()) {
    return *Dlop::create_integer(0);
  }
  auto p = Dlop::unserialize(s);
  if (!p) {
    return *Dlop::create_integer(0);
  }
  return *p;
}

[[nodiscard]] hhds::Node_class master_node(const hhds::Pin_class& pin) { return pin.get_master_node(); }

[[nodiscard]] hhds::Port_id next_io_port_id(const hhds::GraphIO& gio) {
  hhds::Port_id max = 0;
  for (const auto& d : gio.get_input_pin_decls()) {
    if (d.port_id > max) {
      max = d.port_id;
    }
  }
  for (const auto& d : gio.get_output_pin_decls()) {
    if (d.port_id > max) {
      max = d.port_id;
    }
  }
  return max + 1;
}

// Declare a graph input on the GraphIO and return its driver pin on the
// materialized body. Mirrors `Lgraph::add_graph_input(name, port_id, bits)`.
[[nodiscard]] hhds::Pin_class add_graph_input(hhds::Graph* g, std::string_view name, hhds::Port_id port_id, uint32_t bits) {
  auto gio = g->get_io();
  if (!gio->has_input(name) && !gio->has_output(name)) {
    gio->add_input(name, port_id);
  }
  gio->set_bits(name, bits);
  return g->get_input_pin(name);
}

[[nodiscard]] hhds::Pin_class add_graph_output(hhds::Graph* g, std::string_view name, hhds::Port_id port_id, uint32_t bits) {
  auto gio = g->get_io();
  if (!gio->has_output(name) && !gio->has_input(name)) {
    gio->add_output(name, port_id);
  }
  gio->set_bits(name, bits);
  return g->get_output_pin(name);  // sink counterpart (for internal -> output connections)
}

[[nodiscard]] hhds::Pin_class get_graph_output_sink(hhds::Graph* g, std::string_view name) { return g->get_output_pin(name); }

[[nodiscard]] bool has_graph_input(hhds::Graph* g, std::string_view name) { return g->get_io()->has_input(name); }
[[nodiscard]] bool has_graph_output(hhds::Graph* g, std::string_view name) { return g->get_io()->has_output(name); }

static void set_loc(hhds::Node_class& node, const std::string& src) {
  if (src.empty()) {
    return;
  }

  std::vector<std::string> line = absl::StrSplit(src, ':');

  if (line.size() >= 2) {
    set_source(node, line[0]);
    uint64_t loc_line = str_tools::to_i(line[1]);
    set_loc1(node, loc_line);
  }
}

// "Find any driver pin in `g` whose pin_name attr matches `name`". HHDS does
// not maintain a reverse index for pin names; this is a linear walk used only
// in the rename-protect path (see set_driver_name_if_free below). Pin renames
// in tolg are rare enough that this is acceptable.
[[nodiscard]] hhds::Pin_class find_driver_by_name(hhds::Graph* g, std::string_view name) {
  if (name.empty()) {
    return {};
  }
  for (auto node : g->fast_class()) {
    for (const auto& dpin : node.out_pins()) {
      auto pn = pin_name_of(dpin);
      if (pn == name) {
        return dpin;
      }
    }
  }
  return {};
}

static bool set_driver_name_if_free(hhds::Pin_class& pin, std::string_view name) {
  if (name.empty()) {
    return false;
  }

  auto existing = find_driver_by_name(pin.get_graph(), name);
  if (!existing.is_invalid() && existing != pin) {
    return false;
  }

  set_pin_name(pin, name);
  return true;
}

}  // namespace

static void look_for_wire(hhds::Graph* g, const RTLIL::Wire* wire) {
  if (wire2pin.find(wire) != wire2pin.end()) {
    return;
  }

  if (wire->port_input) {
    I(!wire->port_output);
    I(wire->name.c_str()[0] == '\\');
    hhds::Pin_class pin;
    std::string     wname(&wire->name.c_str()[1]);
    if (has_graph_input(g, wname)) {
      pin = g->get_input_pin(wname);
      I(static_cast<int>(bits_of(pin, *g->get_io(), wname)) == wire->width);
    } else {
      pin = add_graph_input(g, wname, wire->port_id, wire->width);
    }
    if (wire->start_offset) {
      set_pin_offset(pin, wire->start_offset);
    }
    auto node = pin.get_master_node();
    set_loc(node, wire->get_src_attribute());
    wire2pin[wire] = pin;
  } else if (wire->port_output) {
    I(wire->name.c_str()[0] == '\\');
    auto node = create_typed_node(*g, Ntype_op::Or, wire->width);
    set_loc(node, wire->get_src_attribute());
    auto dpin = node.create_driver_pin(0);
    pending_outputs.emplace_back(wire);
    if (wire->name.c_str()[0] != '$') {
      std::string wname(&wire->name.c_str()[1]);
      set_pin_name(dpin, wname);
    }
    wire2pin[wire] = dpin;
  }
}

static hhds::Pin_class resolve_constant(hhds::Graph* g, const std::vector<RTLIL::State>& data, bool is_signed) {
  RTLIL::Const v(data);
  if (v.is_fully_zero()) {
    return create_const(*g, *Dlop::create_integer(0));
  }

  if (v.is_fully_def() && data.size() < 30) {
    auto x = v.as_int(is_signed);
    return create_const(*g, *Dlop::create_integer(x));
  }

  std::string val;
  if (is_signed) {
    val = "0sb";
  } else {
    val = "0b";
  }

  for (size_t i = data.size(); i > 0; i--) {
    switch (data[i - 1]) {
      case RTLIL::S0: absl::StrAppend(&val, "0"); break;
      case RTLIL::S1: absl::StrAppend(&val, "1"); break;
      case RTLIL::Sz: absl::StrAppend(&val, "z"); break;
      default: absl::StrAppend(&val, "?"); break;
    }
  }

  auto lc = Dlop::from_pyrope(val);
  return create_const(*g, *lc);
}

class Pick_ID {
public:
  hhds::Pin_class driver;
  int             offset;
  int             width;
  bool            is_signed;

  Pick_ID(hhds::Pin_class _driver, int _offset, int _width, bool _is_signed)
      : driver(_driver), offset(_offset), width(_width), is_signed(_is_signed) {}

  template <typename H>
  friend H AbslHashValue(H h, const Pick_ID& s) {
    return H::combine(std::move(h), s.driver.get_class_index(), s.offset, s.width, s.is_signed);
  }
};

bool operator==(const Pick_ID& lhs, const Pick_ID& rhs) {
  return lhs.driver == rhs.driver && lhs.width == rhs.width && lhs.offset == rhs.offset && lhs.is_signed == rhs.is_signed;
}

static absl::flat_hash_map<Pick_ID, hhds::Pin_class> picks;

static hhds::Pin_class create_pick_operator(const hhds::Pin_class& wide_dpin, int offset, int width, bool is_signed) {
  if (offset == 0 && (int)bits_of(wide_dpin) == width && !is_signed) {
    return wide_dpin;
  }
  if (offset == 0 && width == 1 && (int)bits_of(wide_dpin) == 1) {
    return wide_dpin;
  }

  Pick_ID pick_id(wide_dpin, offset, width, is_signed);
  if (picks.find(pick_id) != picks.end()) {
    return picks.at(pick_id);
  }

  hhds::Pin_class dpin;

  auto* g = wide_dpin.get_graph();

  if (is_signed) {
    // Pick(a,width,offset, true): x = a>>offset; y = Sext(x, width)
    auto sext_node = create_typed_node(*g, Ntype_op::Sext, width);

    if (offset) {
      auto shr_node = create_typed_node(*g, Ntype_op::SRA, width);
      setup_sink_by_name(shr_node, "a").connect_driver(wide_dpin);
      setup_sink_by_name(shr_node, "b").connect_driver(create_const(*g, *Dlop::create_integer(offset)));

      setup_sink_by_name(sext_node, "a").connect_driver(shr_node.create_driver_pin(0));
    } else {
      setup_sink_by_name(sext_node, "a").connect_driver(wide_dpin);
    }

    setup_sink_by_name(sext_node, "b")
        .connect_driver(create_const(*g, *Dlop::create_integer(width)));
    dpin = sext_node.create_driver_pin(0);
  } else {
    // Pick(a,width,offset, false): x = a>>offset; y = x & ((1<<width)-1)
    auto and_node = create_typed_node(*g, Ntype_op::And, width);

    if (offset) {
      auto shr_node = create_typed_node(*g, Ntype_op::SRA, width);
      setup_sink_by_name(shr_node, "a").connect_driver(wide_dpin);
      setup_sink_by_name(shr_node, "b").connect_driver(create_const(*g, *Dlop::create_integer(offset)));

      and_node.create_driver_pin(0);  // ensure dpin exists
      shr_node.create_driver_pin(0).connect_sink(and_node.create_sink_pin(0));
    } else {
      wide_dpin.connect_sink(and_node.create_sink_pin(0));
    }

    create_const(*g, *Dlop::get_mask_value(width)).connect_sink(and_node.create_sink_pin(0));
    dpin = and_node.create_driver_pin(0);
  }

  picks.insert(std::make_pair(pick_id, dpin));

  return dpin;
}

static hhds::Pin_class get_edge_pin(hhds::Graph* g, const RTLIL::Wire* wire, bool is_signed) {
  if (wire2pin.find(wire) == wire2pin.end()) {
    look_for_wire(g, wire);
  }

  if (wire2pin.find(wire) != wire2pin.end()) {
    auto& dpin = wire2pin[wire];
    if (wire->width != (int)bits_of(dpin)) {
      if (wire->width > static_cast<int>(bits_of(dpin))) {
        return dpin;
      }
    }

    if (is_signed) {
      return dpin;
    }

    auto node = master_node(dpin);

    if (type_op_of(node) == Ntype_op::Get_mask) {
      return dpin;
    }

    if (is_const_pin(dpin) && !hydrate_const_pin(dpin).is_negative()) {
      return dpin;
    }

    auto tposs_node = create_typed_node(*g, Ntype_op::Get_mask, bits_of(dpin) + 1);
    setup_sink_by_name(tposs_node, "a").connect_driver(dpin);
    setup_sink_by_name(tposs_node, "mask")
        .connect_driver(create_const(*g, *Dlop::create_integer(-1)));

    return tposs_node.create_driver_pin(0);
  }

  I(!wire->port_input);
  I(!wire->port_output);

  auto node = create_typed_node(*g, Ntype_op::Or, wire->width);
  set_loc(node, wire->get_src_attribute());

  wire2pin[wire] = node.create_driver_pin(0);
  return wire2pin[wire];
}

static hhds::Pin_class create_pick_operator(hhds::Graph* g, const RTLIL::Wire* wire, int offset, int width, bool is_signed) {
  if (wire->width == width && offset == 0) {
    return get_edge_pin(g, wire, is_signed);
  }
  if (auto it = partially_assigned.find(wire); it != partially_assigned.end()) {
    const auto it_bits = partially_assigned_bits.find(wire);
    I(it_bits != partially_assigned_bits.end());
    Bits_t bits = 0;
    auto   pos  = 0u;
    for (const auto& b : it_bits->second) {
      if (bits == offset) {
        I(it->second.size() > pos);
        return it->second[pos];
      }
      bits += b;
      ++pos;
    }
  }

  return create_pick_operator(get_edge_pin(g, wire, is_signed), offset, width, is_signed);
}

static void append_to_or_node(hhds::Graph* g, const hhds::Node_class& or_node, const hhds::Pin_class& dpin, int or_offset) {
  if (or_node.has_inp_edges() && is_const_pin(dpin)) {
    auto val = hydrate_const_pin(dpin);
    if (val.is_known_zero()) {
      return;
    }
  }

  I(!dpin.is_invalid());

  auto tposs_node = create_typed_node(*g, Ntype_op::Get_mask);

  if (or_offset) {
    auto shl_node = create_typed_node(*g, Ntype_op::SHL, or_offset + bits_of(dpin));
    setup_sink_by_name(shl_node, "a").connect_driver(dpin);
    setup_sink_by_name(shl_node, "B")
        .connect_driver(create_const(*g, *Dlop::create_integer(or_offset)));

    set_bits(tposs_node.create_driver_pin(0), or_offset + bits_of(dpin) + 1);
    setup_sink_by_name(tposs_node, "a").connect_driver(shl_node.create_driver_pin(0));
  } else {
    set_bits(tposs_node.create_driver_pin(0), bits_of(dpin) + 1);
    setup_sink_by_name(tposs_node, "a").connect_driver(dpin);
  }

  setup_sink_by_name(tposs_node, "mask").connect_driver(create_const(*g, *Dlop::create_integer(-1)));
  tposs_node.create_driver_pin(0).connect_sink(or_node.create_sink_pin(0));
}

static hhds::Pin_class create_pick_concat_dpin(hhds::Graph* g, const RTLIL::SigSpec& ss, bool is_signed) {
  std::vector<hhds::Pin_class> inp_pins;
  I(ss.chunks().size() != 0);

  const std::vector<RTLIL::SigChunk> chunk_list(ss.chunks().begin(), ss.chunks().end());
  for (auto i = 0u; i < chunk_list.size(); ++i) {
    const auto& chunk       = chunk_list[i];
    bool        signed_last = ((i + 1) == chunk_list.size()) && is_signed;
    if (chunk.wire == nullptr) {
      inp_pins.emplace_back(resolve_constant(g, chunk.data, signed_last));
    } else {
      inp_pins.emplace_back(create_pick_operator(g, chunk.wire, chunk.offset, chunk.width, signed_last));
    }
  }

  hhds::Pin_class dpin;
  if (inp_pins.size() > 1) {
    auto or_node = create_typed_node(*g, Ntype_op::Or, ss.size());

    int offset = 0;
    for (auto i = 0u; i < chunk_list.size(); ++i) {
      if (!inp_pins[i].is_invalid()) {
        hhds::Pin_class inp_pin;

        if ((i + 1) == chunk_list.size() || is_signed) {
          inp_pin = inp_pins[i];
        } else if (type_op_of(master_node(inp_pins[i])) == Ntype_op::Get_mask) {
          inp_pin = inp_pins[i];
        } else {
          if (chunk_list[i].wire) {
            set_loc(or_node, chunk_list[i].wire->get_src_attribute());
          }
          auto tposs_node = create_typed_node(*g, Ntype_op::Get_mask, bits_of(inp_pins[i]) + 1);
          setup_sink_by_name(tposs_node, "a").connect_driver(inp_pins[i]);
          setup_sink_by_name(tposs_node, "mask")
              .connect_driver(create_const(*g, *Dlop::create_integer(-1)));

          inp_pin = tposs_node.create_driver_pin(0);
        }

        append_to_or_node(g, or_node, inp_pin, offset);
      }

      offset += chunk_list[i].width;
    }
    dpin = or_node.create_driver_pin(0);
  } else {
    I(!inp_pins.empty());
    dpin = inp_pins[0];
  }

  if (dpin.is_invalid()) {
    return create_const(*g, *Dlop::create_integer(0));
  }

  return dpin;
}

static hhds::Pin_class get_dpin(hhds::Graph* g, const RTLIL::Cell* cell, const RTLIL::IdString& name) {
  if (cell->hasParam(name)) {
    const RTLIL::Const& v = cell->getParam(name);
    if (v.is_fully_def() && v.size() < 30) {
      return create_const(*g, *Dlop::create_integer(v.as_int()));
    }
    auto v_str = absl::StrCat("0b", v.as_string());
    return create_const(*g, *Dlop::from_pyrope(v_str));
  }
  bool is_signed = true;
  if (name == ID::A && cell->hasParam(ID::A_SIGNED)) {
    is_signed = cell->getParam(ID::A_SIGNED).as_bool();
  } else if (name == ID::B && cell->hasParam(ID::B_SIGNED)) {
    is_signed = cell->getParam(ID::B_SIGNED).as_bool();
  }

  if (!cell->hasPort(name)) {
    return create_const(*g, *Dlop::from_pyrope("0b?"));
  }

  return create_pick_concat_dpin(g, cell->getPort(name), is_signed);
}

static hhds::Pin_class get_unsigned_dpin(hhds::Graph* g, const RTLIL::Cell* cell, const RTLIL::IdString& name) {
  bool no_need = false;
  int  bits    = 0;
  (void)bits;

  if (name == ID::A) {
    bits = cell->getParam(ID::A_WIDTH).as_int();
    if (cell->hasParam(ID::A_SIGNED)) {
      no_need = cell->getParam(ID::A_SIGNED).as_bool();
    }
  } else if (name == ID::B) {
    bits = cell->getParam(ID::B_WIDTH).as_int();
    if (cell->hasParam(ID::B_SIGNED)) {
      no_need = cell->getParam(ID::B_SIGNED).as_bool();
    }
  } else {
    I(false);
  }
  auto dpin = get_dpin(g, cell, name);
  if (no_need) {
    return dpin;
  }

  auto node = master_node(dpin);
  if (type_op_of(node) == Ntype_op::Get_mask) {
    return dpin;
  }
  if (is_const_pin(dpin) && !hydrate_const_pin(dpin).is_negative()) {
    return dpin;
  }

  auto a_tposs = create_typed_node(*g, Ntype_op::Get_mask);
  if (bits_of(dpin)) {
    set_bits(a_tposs.create_driver_pin(0), bits_of(dpin) + 1);
  }
  setup_sink_by_name(a_tposs, "a").connect_driver(dpin);
  setup_sink_by_name(a_tposs, "mask").connect_driver(create_const(*g, *Dlop::create_integer(-1)));

  return a_tposs.create_driver_pin(0);
}

static bool is_yosys_output(const std::string& idstring) {
  return idstring == "\\Y" || idstring == "\\Q" || idstring == "\\RD_DATA";
}

static void connect_all_inputs(const hhds::Pin_class& spin, const RTLIL::Cell* cell) {
  I(spin.is_sink());
  bool is_signed = false;
  for (auto& conn : cell->connections()) {
    RTLIL::SigSpec ss = conn.second;
    if (ss.size() == 0) {
      continue;
    }
    if (is_yosys_output(conn.first.c_str())) {
      continue;
    }
    if (conn.first == ID::A && cell->hasParam(ID::A_SIGNED)) {
      is_signed = cell->getParam(ID::A_SIGNED).as_bool();
    } else if (conn.first == ID::B && cell->hasParam(ID::B_SIGNED)) {
      is_signed = cell->getParam(ID::B_SIGNED).as_bool();
    }
    if (!is_signed) {
      break;
    }
  }

  for (auto& conn : cell->connections()) {
    RTLIL::SigSpec ss = conn.second;
    if (ss.size() == 0) {
      continue;
    }
    if (is_yosys_output(conn.first.c_str())) {
      continue;
    }

    spin.connect_driver(create_pick_concat_dpin(spin.get_graph(), ss, is_signed));
  }
}

static const std::regex dc_name("(\\|/)n(\\d\\+)|^N(\\d\\+)");

static void set_bits_wirename(hhds::Pin_class& pin, const RTLIL::Wire* wire) {
  if (!wire) {
    return;
  }

  if (!wire->port_input && !wire->port_output) {
    if (wire->name.c_str()[0] != '$' && wire->name.str().rfind("\\lg_") != 0 && !std::regex_match(wire->name.str(), dc_name)) {
      auto pn = pin_name_of(pin);
      if (!pn.empty()) {
        auto current_name = std::string(pn);
        if (!current_name.empty() && current_name[0] != '_') {
          return;
        }
      }
      if (!set_driver_name_if_free(pin, std::string(&wire->name.c_str()[1]))) {
        return;
      }
      return;
    }
  }
}

static hhds::Pin_class get_partial_dpin(hhds::Graph* g, const RTLIL::Wire* wire) {
  auto or_dpin = get_edge_pin(g, wire, true);
  auto or_node = master_node(or_dpin);

  if (type_op_of(or_node) != Ntype_op::Or) {
    auto real_or_node = create_typed_node(*g, Ntype_op::Or, bits_of(or_dpin));
    auto real_dpin    = real_or_node.create_driver_pin(0);

    if (unlikely(is_graph_output_pin(or_dpin))) {
      // Convert output-driver-counterpart to sink: use graph's named output sink
      auto out_name = or_dpin.get_pin_name();
      auto out_sink = g->get_output_pin(out_name);
      out_sink.connect_driver(real_dpin);
    } else if (unlikely(type_op_of(master_node(or_dpin)) == Ntype_op::Sub)) {
      real_or_node.create_sink_pin(0).connect_driver(or_dpin);
    } else if (type_op_of(master_node(or_dpin)) != Ntype_op::Or) {
      std::cerr << "WARNING: wire:" << wire->name.str() << " is mapped to dpin:" << debug_name(master_node(or_dpin)) << "\n";
    }

    or_dpin        = real_dpin;
    wire2pin[wire] = real_dpin;
  }

  set_bits_wirename(or_dpin, wire);

  return or_dpin;
}

static hhds::Node_class resolve_memory(hhds::Graph* g, RTLIL::Cell* cell) {
  auto node = create_typed_node(*g, Ntype_op::Memory);
  set_loc(node, cell->get_src_attribute());
  std::string inst_name{cell->name.str()};
  if (!inst_name.empty()) {
    std::string nm = inst_name[0] == '\\' ? inst_name.substr(1) : inst_name;
    node.attr(hhds::attrs::name).set(nm);
  }

  uint32_t rdports = cell->getParam(ID::RD_PORTS).as_int();
  uint32_t wrports = cell->getParam(ID::WR_PORTS).as_int();

  uint32_t bits = cell->getParam(ID::WIDTH).as_int();

  for (uint32_t rdport = 0; rdport < rdports; rdport++) {
    RTLIL::SigSpec ss   = cell->getPort("\\RD_DATA").extract(rdport * bits, bits);
    auto           dpin = node.create_driver_pin(static_cast<hhds::Port_id>(wrports + rdport));
    set_bits(dpin, bits);

    uint32_t offset = 0;
    for (auto& chunk : ss.chunks()) {
      const RTLIL::Wire* wire = chunk.wire;

      if (wire == 0) {
        continue;
      }

      if (chunk.width == wire->width) {
        if (wire2pin.find(wire) != wire2pin.end()) {
          const auto& or_dpin = wire2pin[wire];
          if (is_graph_output_pin(or_dpin)) {
            auto out_name = or_dpin.get_pin_name();
            auto out_sink = g->get_output_pin(out_name);
            dpin.connect_sink(out_sink);
          } else {
            auto or_node = master_node(or_dpin);
            I(type_op_of(or_node) == Ntype_op::Or);
            I(!or_node.has_inp_edges());

            or_node.create_sink_pin(0).connect_driver(dpin);
            I((int)bits_of(or_dpin) == wire->width);
          }
        } else if (chunk.width == ss.size()) {
          wire2pin[wire] = dpin;
          set_bits_wirename(dpin, wire);
        } else {
          hhds::Pin_class pick_pin = create_pick_operator(dpin, offset, chunk.width, false);
          wire2pin[wire]           = pick_pin;
          set_bits_wirename(pick_pin, wire);
        }
        offset += chunk.width;
      } else {
        if (partially_assigned.find(wire) == partially_assigned.end()) {
          partially_assigned[wire].resize(wire->width);
          partially_assigned_bits[wire].resize(wire->width);

          I(wire2pin.find(wire) == wire2pin.end());
          auto pa_node = create_typed_node(*g, Ntype_op::Or, wire->width);

          wire2pin[wire] = pa_node.create_driver_pin(0);
        }
        set_bits(dpin, ss.size());

        auto src_pin = create_pick_operator(dpin, offset, chunk.width, false);
        offset += chunk.width;
        for (int i = 0; i < chunk.width; i++) {
          I((size_t)(chunk.offset + i) < partially_assigned[wire].size());
          partially_assigned[wire][chunk.offset + i] = src_pin;
        }
      }
    }
  }

  return node;
}

static bool is_black_box_output(const RTLIL::Cell* cell, const RTLIL::IdString& port_name) {
  const RTLIL::Wire* wire = cell->getPort(port_name).chunks().at(0).wire;

  if (!wire) {
    return false;
  }

  if (wire->port_input) {
    return false;
  }

  auto cell_port = absl::StrCat(cell->type.str(), "_:_", port_name.str());

  if (cell_port_outputs.find(cell_port) != cell_port_outputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an output\n", cell->name.c_str(), port_name.c_str());
    return true;
  }
  if (cell_port_inputs.find(cell_port) != cell_port_inputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an input\n", cell->name.c_str(), port_name.c_str());
    return false;
  }

  return false;
}

static bool is_black_box_input(const RTLIL::Cell* cell, const RTLIL::IdString& port_name) {
  const RTLIL::Wire* wire = cell->getPort(port_name).chunks().at(0).wire;

  if (!wire) {
    return true;
  }

  if (wire->port_input) {
    return true;
  }

  auto cell_port = absl::StrCat(cell->type.str(), "_:_", port_name.str());

  if (cell_port_outputs.find(cell_port) != cell_port_outputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an output\n", cell->name.c_str(), port_name.c_str());
    return false;
  }
  if (cell_port_inputs.find(cell_port) != cell_port_inputs.end()) {
    fprintf(stderr, "WARNING: lgyosys_tolg guessing that cell %s pin %s is an input\n", cell->name.c_str(), port_name.c_str());
    return true;
  }

  return false;
}

static void process_cell_drivers_intialization(RTLIL::Module* mod, hhds::Graph* g) {
  auto& lib = *g->get_io()->get_library();

  for (auto cell : mod->cells()) {
    if (cell->type == "$mem" || cell->type == "$mem_v2") {
      cell2node[cell] = resolve_memory(g, cell);
      continue;
    }

    auto node = g->create_node();
    set_loc(node, cell->get_src_attribute());
    cell2node[cell] = node;

    std::shared_ptr<hhds::GraphIO> sub_gio;
    bool                           is_sub_cell = (cell->type.c_str()[0] == '\\'
                            || strncmp(cell->type.c_str(), "$paramod\\", 8) == 0);

    if (is_sub_cell) {
      std::string mod_name(&(cell->type.c_str()[1]));
      sub_gio = lib.find_io(mod_name);
      if (!sub_gio) {
        sub_gio = lib.create_io(mod_name);
      }

      if (sub_gio && type_op_of(node) != Ntype_op::Sub) {
        set_type_op(node, Ntype_op::Sub);
        node.set_subnode(sub_gio);
        std::string inst_name{cell->name.str()};
        if (!inst_name.empty()) {
          node.attr(hhds::attrs::name).set(inst_name.substr(1));
        }
      }
    }

    for (const auto& conn : cell->connections()) {
      if (conn.second.empty()) {
        continue;
      }

      if (sub_gio) {
        std::string pin_name(&(conn.first.c_str()[1]));

        if (str_tools::is_i(pin_name)) {
          int pos = str_tools::to_i(pin_name);

          if (!sub_gio->has_pin_with_port_id(pos)) {
            if (cell->output(conn.first)) {
              sub_gio->add_output(pin_name, pos);
            } else if (cell->input(conn.first)) {
              sub_gio->add_input(pin_name, pos);
            } else if (conn.second.is_fully_undef()) {
              sub_gio->add_output(pin_name, pos);
            } else if (conn.second.is_fully_const()) {
              sub_gio->add_input(pin_name, pos);
            } else {
              bool is_input  = false;
              bool is_output = false;
              for (auto& chunk : conn.second.chunks()) {
                const RTLIL::Wire* wire = chunk.wire;
                if (wire->port_input) {
                  is_input = true;
                }
                if (driven_signals.count(wire->hashidx_) != 0) {
                  is_input = true;
                }
              }
              if (is_input && !is_output) {
                sub_gio->add_input(pin_name, pos);
              } else if (!is_input && is_output) {
                sub_gio->add_output(pin_name, pos);
              } else {
                fprintf(stderr,
                        "Warning: impossible to figure out direction in module %s cell type %s pin_name to %s\n",
                        mod->name.c_str(),
                        cell->type.c_str(),
                        pin_name.c_str());
                continue;
              }
            }
          }
          if (sub_gio->has_input_with_port_id(pos)) {
            continue;  // input — fed externally, not a driver
          }
        } else {
          if (!sub_gio->has_input(pin_name) && !sub_gio->has_output(pin_name)) {
            hhds::Port_id new_pos = next_io_port_id(*sub_gio);
            if (cell->input(conn.first) || is_black_box_input(cell, conn.first)) {
              sub_gio->add_input(pin_name, new_pos);
            } else if (cell->output(conn.first) || is_black_box_output(cell, conn.first)) {
              sub_gio->add_output(pin_name, new_pos);
            } else if (conn.second.is_fully_undef()) {
              sub_gio->add_output(pin_name, new_pos);
            } else if (conn.second.is_fully_const()) {
              sub_gio->add_input(pin_name, new_pos);
            } else {
              bool is_input  = false;
              bool is_output = false;
              for (auto& chunk : conn.second.chunks()) {
                const RTLIL::Wire* wire = chunk.wire;
                if (wire->port_input) {
                  is_input = true;
                  continue;
                }
                if (driven_signals.count(wire->hashidx_) != 0) {
                  is_input = true;
                }
              }
              if (is_input && !is_output) {
                sub_gio->add_input(pin_name, new_pos);
              } else if (!is_input && is_output) {
                sub_gio->add_output(pin_name, new_pos);
              } else {
                log_error("unknown port %s at module %s cell %s\n", conn.first.c_str(), mod->name.c_str(), cell->type.c_str());
              }
            }
          }

          if ((!sub_gio->has_input(pin_name) && !sub_gio->has_output(pin_name)) || sub_gio->has_input(pin_name)) {
            continue;
          }
        }
      } else {
        set_type_op(node, Ntype_op::Or);  // updated later

        if (cell->input(conn.first)) {
          continue;
        }
      }

      hhds::Pin_class driver_pin;
      if (type_op_of(node) == Ntype_op::Sub) {
        std::string pin_name(&(conn.first.c_str()[1]));
        driver_pin = node.create_driver_pin(pin_name);
      } else {
        driver_pin = node.create_driver_pin(0);
      }

      const RTLIL::SigSpec ss = conn.second;

      if (ss.chunks().size() > 0) {
        set_bits(driver_pin, ss.size());
      }

      uint32_t offset = 0;
      for (auto& chunk : ss.chunks()) {
        const RTLIL::Wire* wire = chunk.wire;

        if (wire == 0) {
          continue;
        }

        if (chunk.width == wire->width) {
          if (chunk.width == ss.size()) {
            I((int)bits_of(driver_pin) == wire->width);
            I((int)bits_of(driver_pin) == ss.size());
            auto prev_it = wire2pin.find(wire);
            if (prev_it != wire2pin.end()) {
              auto prev_pin  = prev_it->second;
              auto prev_node = master_node(prev_pin);
              if (type_op_of(prev_node) == Ntype_op::Or && !prev_node.has_inp_edges()) {
                append_to_or_node(g, prev_node, driver_pin, 0);
              }
            }
            wire2pin[wire] = driver_pin;
            set_bits_wirename(driver_pin, wire);
          } else {
            I((chunk.width + offset) <= bits_of(driver_pin));
            hhds::Pin_class pick_pin = create_pick_operator(driver_pin, offset, chunk.width, wire->is_signed);
            wire2pin[wire]           = pick_pin;
            set_bits_wirename(pick_pin, wire);
          }
          offset += chunk.width;

        } else {
          if (partially_assigned.find(wire) == partially_assigned.end()) {
            partially_assigned[wire].resize(wire->width);
            partially_assigned_bits[wire].resize(wire->width);

            auto n2 = create_typed_node(*g, Ntype_op::Or, wire->width);
            set_loc(n2, wire->get_src_attribute());

            auto n2_dpin = n2.create_driver_pin(0);
            if (wire->name.c_str()[0] != '$') {
              std::string wname(&wire->name.c_str()[1]);
              set_pin_name(n2_dpin, wname);
            }
            wire2pin[wire] = n2_dpin;
          }

          auto src_pin = create_pick_operator(driver_pin, offset, chunk.width, wire->is_signed);
          offset += chunk.width;

          if (!wire->port_input && !wire->port_output) {
            auto s = chunk.offset;
            auto e = chunk.offset + chunk.width - 1;

            if (s == e) {
              set_pin_name(src_pin, absl::StrCat(&wire->name.str()[1], "[", s, "]"));
            } else {
              set_pin_name(src_pin, absl::StrCat(&wire->name.str()[1], "[", s, ":", e, "]"));
            }
          }

          partially_assigned[wire][chunk.offset]      = src_pin;
          partially_assigned_bits[wire][chunk.offset] = chunk.width;
        }
      }
    }
  }
}

static void process_assigns(RTLIL::Module* mod, hhds::Graph* g) {
  for (const auto& conn : mod->connections()) {
    const RTLIL::SigSpec lhs = conn.first;
    const RTLIL::SigSpec rhs = conn.second;

    int global_lhs_pos = 0;

    for (auto& lchunk : lhs.chunks()) {
      const RTLIL::Wire* lhs_wire = lchunk.wire;

      if (lchunk.width == 0) {
        continue;
      }

      if (lhs_wire->port_input) {
        log_error("Assignment to input port %s\n", lhs_wire->name.c_str());
      } else if (lchunk.width == lhs_wire->width) {
        auto ss = rhs.extract(lchunk.offset, lchunk.width);

        std::string lhs_name;
        bool        lhs_is_temp     = true;
        bool        lhs_is_internal = (lhs_wire->name.c_str()[0] == '$');
        if (!lhs_is_internal) {
          lhs_name    = std::string(&lhs_wire->name.c_str()[1]);
          lhs_is_temp = (!lhs_name.empty() && lhs_name[0] == '_');
        }

        std::string        rhs_name;
        bool               rhs_is_temp     = true;
        bool               rhs_is_internal = true;
        const RTLIL::Wire* rhs_wire        = (ss.size() == 1) ? ss[0].wire : nullptr;
        if (rhs_wire && rhs_wire->name.c_str()[0] != '$') {
          rhs_name        = std::string(&rhs_wire->name.c_str()[1]);
          rhs_is_internal = false;
          rhs_is_temp     = (!rhs_name.empty() && rhs_name[0] == '_');
        }

        std::string best_name;
        if (!lhs_is_internal && !lhs_is_temp) {
          best_name = lhs_name;
        } else if (!rhs_is_internal && !rhs_is_temp) {
          best_name = rhs_name;
        } else if (!lhs_is_internal) {
          best_name = lhs_name;
        }
        bool needs_alias_node = rhs_wire && !lhs_wire->port_output && !lhs_is_internal && !rhs_is_internal && !lhs_is_temp
                                && !rhs_is_temp && lhs_name != rhs_name;

        if (!best_name.empty() && !lhs_wire->port_output) {
          auto lhs_it = wire2pin.find(lhs_wire);
          if (lhs_it != wire2pin.end()) {
            auto& prev_pin = lhs_it->second;
            auto  prev_n   = master_node(prev_pin);
            bool  is_placeholder = type_op_of(prev_n) == Ntype_op::Or && !prev_n.has_inp_edges();
            if (!is_placeholder) {
              auto pn = pin_name_of(prev_pin);
              if (pn.empty() || pn[0] == '_') {
                set_driver_name_if_free(prev_pin, best_name);
              } else if (pn != best_name && best_name[0] != '_') {
                auto or_node = create_typed_node(*g, Ntype_op::Or, lhs_wire->width);
                auto or_dpin = or_node.create_driver_pin(0);
                set_driver_name_if_free(or_dpin, best_name);
                or_node.create_sink_pin(0).connect_driver(prev_pin);
                wire2pin[lhs_wire] = or_dpin;
              }
              global_lhs_pos += lhs_wire->width;
              continue;
            }
          }
        }

        hhds::Pin_class dpin = create_pick_concat_dpin(g, ss, lhs_wire->is_signed);

        if (!best_name.empty() && !rhs_wire && ss.size() > 1) {
          const std::vector<RTLIL::SigChunk> chunks(ss.chunks().begin(), ss.chunks().end());
          int                                bit_offset = 0;
          for (const auto& chunk : chunks) {
            if (chunk.wire) {
              auto it     = wire2pin.find(chunk.wire);
              bool in_w2p = (it != wire2pin.end());
              if (in_w2p) {
                auto&       chunk_pin = it->second;
                std::string bit_name;
                if (chunk.width == 1) {
                  bit_name = absl::StrCat(best_name, "[", bit_offset, "]");
                } else {
                  bit_name = absl::StrCat(best_name, "[", bit_offset + chunk.width - 1, ":", bit_offset, "]");
                }
                auto pn = pin_name_of(chunk_pin);
                if (pn.empty() || pn[0] == '_') {
                  set_driver_name_if_free(chunk_pin, bit_name);
                } else if (pn != bit_name) {
                  auto or_node = create_typed_node(*g, Ntype_op::Or, chunk.width);
                  auto or_dpin = or_node.create_driver_pin(0);
                  set_driver_name_if_free(or_dpin, bit_name);
                  or_node.create_sink_pin(0).connect_driver(chunk_pin);
                  wire2pin[chunk.wire] = or_dpin;
                }
              }
            }
            bit_offset += chunk.width;
          }
        }

        if (needs_alias_node) {
          auto alias_node = create_typed_node(*g, Ntype_op::Or, lhs_wire->width);
          set_loc(alias_node, lhs_wire->get_src_attribute());

          auto alias_dpin = alias_node.create_driver_pin(0);
          set_driver_name_if_free(alias_dpin, lhs_name);
          alias_node.create_sink_pin(0).connect_driver(dpin);
          dpin = alias_dpin;
        } else if (!lhs_is_internal) {
          bool rhs_adjusted_name = false;
          if (!lhs_wire->port_output && rhs_wire) {
            auto it = wire2pin.find(rhs_wire);
            if (it != wire2pin.end()) {
              auto& rhs_dpin = it->second;
              auto  rn       = pin_name_of(rhs_dpin);
              if (rn.empty() || rn[0] == '_') {
                auto set_name = best_name.empty() ? lhs_name : best_name;
                set_driver_name_if_free(rhs_dpin, set_name);
                rhs_adjusted_name = true;
              }
            }
          }
          if (!rhs_adjusted_name && !best_name.empty()) {
            auto dn = pin_name_of(dpin);
            if (dn.empty() || dn[0] == '_') {
              set_driver_name_if_free(dpin, best_name);
            } else if (dn != best_name && best_name[0] != '_') {
              auto or_node = create_typed_node(*g, Ntype_op::Or, lhs_wire->width);
              auto or_dpin = or_node.create_driver_pin(0);
              set_driver_name_if_free(or_dpin, best_name);
              or_node.create_sink_pin(0).connect_driver(dpin);
              dpin = or_dpin;
            }
          }
        }
        if (wire2pin.find(lhs_wire) != wire2pin.end()) {
          auto prev_dpin      = wire2pin[lhs_wire];
          auto prev_n         = master_node(prev_dpin);
          bool is_placeholder = type_op_of(prev_n) == Ntype_op::Or && !prev_n.has_inp_edges();
          if (is_placeholder) {
            append_to_or_node(g, prev_n, dpin, 0);
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
            auto or_node = create_typed_node(*g, Ntype_op::Or, lhs_wire->width);
            set_loc(or_node, lhs_wire->get_src_attribute());
            auto or_dpin = or_node.create_driver_pin(0);
            if (lhs_wire->name.c_str()[0] != '$') {
              std::string wname(&lhs_wire->name.c_str()[1]);
              set_pin_name(or_dpin, wname);
            }
            wire2pin[lhs_wire] = or_dpin;
          } else {
            auto& dpin = wire2pin[lhs_wire];
            I((int)bits_of(dpin) == lhs_wire->width);
            auto pn = pin_name_of(dpin);
            if (pn.empty()) {
              if (lhs_wire->name.c_str()[0] != '$') {
                std::string wname(&lhs_wire->name.c_str()[1]);
                set_pin_name(dpin, wname);
              }
            }
          }
        }

        {
          const int lhs_off = lchunk.offset;
          int       lhs_pos = 0;

          int global_rhs_pos = 0;
          for (auto& rchunk : rhs.chunks()) {
            if (lhs_pos >= lchunk.width) {
              break;
            }

            const int rhs_off = rchunk.offset;
            int       rhs_pos = 0;

            while (global_lhs_pos > global_rhs_pos && rhs_pos < rchunk.width) {
              global_rhs_pos++;
              rhs_pos++;
            }

            if (rhs_pos == rchunk.width) {
              continue;
            }

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
                ++rhs_pos;
                ++global_lhs_pos;
                ++global_rhs_pos;
              }
              continue;
            }

            I(lhs_off + lhs_pos >= rhs_pos);

            int from_in_rchunk = rhs_pos + rhs_off;
            int max_bits       = std::min(rchunk.width, lchunk.width - lhs_pos);
            int to_in_rchunk   = from_in_rchunk + max_bits;

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

            hhds::Pin_class dpin;
            if (rchunk.wire == nullptr) {
              dpin = resolve_constant(g, rchunk.data, false);
              if (from_in_rchunk > 0) {
                auto sra_node = create_typed_node(*g, Ntype_op::SRA, bits_needed);
                setup_sink_by_name(sra_node, "a").connect_driver(dpin);
                auto c_pin = create_const(*g, *Dlop::create_integer(from_in_rchunk));
                setup_sink_by_name(sra_node, "b").connect_driver(c_pin);
                dpin = sra_node.create_driver_pin(0);
              }
              if (to_in_rchunk < rchunk.width) {
                auto and_node = create_typed_node(*g, Ntype_op::And, bits_needed);
                and_node.create_sink_pin(0).connect_driver(dpin);
                auto c_pin = create_const(*g, *Dlop::get_mask_value(bits_needed));
                and_node.create_sink_pin(0).connect_driver(c_pin);
                dpin = and_node.create_driver_pin(0);
              }
            } else {
              dpin = create_pick_operator(g, rchunk.wire, from_in_rchunk, bits_needed, true);
            }

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

static uint32_t get_input_size(const RTLIL::Cell* cell) {
  I(cell->known());

  uint32_t max_input = 0;
  for (const auto& conn : cell->connections()) {
    if (cell->output(conn.first)) {
      continue;
    }

    const RTLIL::SigSpec ss = conn.second;
    if (static_cast<uint32_t>(ss.chunks().size()) > max_input) {
      max_input = ss.size();
    }
  }

  return max_input;
}

static uint32_t get_output_size(const RTLIL::Cell* cell) {
  I(cell->known());

  if (cell->hasParam(ID::Y_WIDTH)) {
    return cell->getParam(ID::Y_WIDTH).as_int();
  }

  for (const auto& conn : cell->connections()) {
    if (cell->input(conn.first)) {
      continue;
    }

    const RTLIL::SigSpec& ss = conn.second;
    if (ss.chunks().size() > 0) {
      return ss.size();
    }
  }

  I(0);

  return 0;
}

static void connect_partial_dpin(hhds::Graph* g, hhds::Node_class& or_node, uint32_t or_offset, Bits_t nbits,
                                 const hhds::Pin_class& current_dpin) {
  I(type_op_of(or_node) == Ntype_op::Or);

  if (type_op_of(master_node(current_dpin)) == Ntype_op::Nconst) {
    append_to_or_node(g, or_node, current_dpin, or_offset);
  } else if (bits_of(current_dpin) > nbits) {
    auto and_node = create_typed_node(*g, Ntype_op::And, nbits);
    create_const(*g, *Dlop::get_mask_value(nbits)).connect_sink(and_node.create_sink_pin(0));
    and_node.create_sink_pin(0).connect_driver(current_dpin);

    append_to_or_node(g, or_node, and_node.create_driver_pin(0), or_offset);
  } else if (bits_of(current_dpin) == nbits) {
    append_to_or_node(g, or_node, current_dpin, or_offset);
  } else {
    auto n_x = nbits - bits_of(current_dpin);
    auto n_z = bits_of(current_dpin);

    auto local_or_node = create_typed_node(*g, Ntype_op::Or, nbits);
    std::string mask("0b");
    mask = mask.append(n_x, '?');
    mask = mask.append(n_z, '0');

    create_const(*g, *Dlop::from_pyrope(mask)).connect_sink(local_or_node.create_sink_pin(0));
    local_or_node.create_sink_pin(0).connect_driver(current_dpin);

    append_to_or_node(g, or_node, local_or_node.create_driver_pin(0), or_offset);
  }
}

static void process_partially_assigned_other(hhds::Graph* g) {
  for (const auto& it : partially_assigned) {
    const RTLIL::Wire* wire = it.first;
    I(it.second.size() == it.first->width);

    auto or_dpin = get_partial_dpin(g, wire);
    auto or_node = master_node(or_dpin);

    int i = 0;
    while (i < (int)it.second.size()) {
      auto width = partially_assigned_bits[wire][i];
      if (width == 0) {
        i++;
        continue;
      }
      const auto& dpin = it.second[i];
      if (!dpin.is_invalid()) {
        connect_partial_dpin(g, or_node, i, width, dpin);
      }

      i += width;
    }
  }
}

static void process_partially_assigned_self_chains(hhds::Graph* g) {
  for (const auto& kv : partially_assigned_fwd) {
    const RTLIL::Wire* wire = kv.first;

    bool pending_chain = true;

    std::set<int> processed_pos;
    for (int pos = 0; pos < (int)partially_assigned_fwd[wire].size(); ++pos) {
      auto shift = partially_assigned_fwd[wire][pos];
      if (shift == 0) {
        processed_pos.insert(pos);
      }
    }

    if ((int)processed_pos.size() == wire->width) {
      return;
    }

    while (pending_chain) {
      pending_chain = false;
      absl::flat_hash_set<int> ready_pos;
      absl::flat_hash_set<int> shifts;

      for (int pos = 0; pos < (int)partially_assigned_fwd[wire].size(); ++pos) {
        auto shift = partially_assigned_fwd[wire][pos];
        if (shift == 0) {
          continue;
        }

        I(partially_assigned[wire].size() > pos);
        I(pos - shift >= 0);
        I(pos - shift < partially_assigned[wire].size());

        if (!processed_pos.count(pos - shift)) {
          pending_chain = true;
          continue;
        }

        ready_pos.insert(pos);
        shifts.insert(shift);
      }

      if (shifts.empty()) {
        return;
      }

      auto master_or_dpin = get_partial_dpin(g, wire);
      auto master_or_node = master_node(master_or_dpin);

      auto pre_or_node = create_typed_node(*g, Ntype_op::Or, wire->width);
      set_loc(pre_or_node, wire->get_src_attribute());

      for (auto e : master_or_node.inp_edges()) {
        pre_or_node.create_sink_pin(0).connect_driver(e.driver);
        e.del_edge();
      }
      I(!master_or_node.has_inp_edges());
      master_or_node.create_sink_pin(0).connect_driver(pre_or_node.create_driver_pin(0));

      for (auto shift : shifts) {
        Dlop wr_mask;
        wr_mask             = Dlop::create_integer(0);
        bool        started = false;
        const auto& v       = partially_assigned_fwd[wire];
        for (int pos = static_cast<int>(v.size()) - 1; pos >= 0; --pos) {
          auto i = v[pos];
          if (started) {
            wr_mask = wr_mask.lsh_op(1);
          }
          started = true;

          if (i == shift && processed_pos.count(pos - shift)) {
            wr_mask = wr_mask.or_op(*Dlop::create_integer(1));
          }
        }
        Dlop rd_mask;
        if (shift < 0) {
          rd_mask = wr_mask.lsh_op(-shift)->adjust_bits(wire->width);
        } else {
          rd_mask = wr_mask.rsh_op(shift)->adjust_bits(wire->width);
        }

        auto and_node = create_typed_node(*g, Ntype_op::And, wire->width);

        and_node.create_sink_pin(0).connect_driver(pre_or_node.create_driver_pin(0));
        create_const(*g, rd_mask).connect_sink(and_node.create_sink_pin(0));

        auto tposs_node = create_typed_node(*g, Ntype_op::Get_mask, wire->width + 1);
        setup_sink_by_name(tposs_node, "a").connect_driver(and_node.create_driver_pin(0));
        setup_sink_by_name(tposs_node, "mask")
            .connect_driver(create_const(*g, *Dlop::create_integer(-1)));

        I(shift);
        if (shift < 0) {
          auto sra_node = create_typed_node(*g, Ntype_op::SRA, wire->width);
          setup_sink_by_name(sra_node, "a").connect_driver(tposs_node.create_driver_pin(0));
          setup_sink_by_name(sra_node, "b")
              .connect_driver(create_const(*g, *Dlop::create_integer(-shift)));

          master_or_node.create_sink_pin(0).connect_driver(sra_node.create_driver_pin(0));
        } else {
          auto shl_node = create_typed_node(*g, Ntype_op::SHL, wire->width);
          setup_sink_by_name(shl_node, "a").connect_driver(tposs_node.create_driver_pin(0));
          setup_sink_by_name(shl_node, "B")
              .connect_driver(create_const(*g, *Dlop::create_integer(shift)));

          master_or_node.create_sink_pin(0).connect_driver(shl_node.create_driver_pin(0));
        }
      }

      for (auto pos : ready_pos) {
        processed_pos.insert(pos);
      }
    }
  }
}

static void connect_comparator(hhds::Node_class& exit_node, const RTLIL::Cell* cell) {
  auto* g = exit_node.get_graph();

  auto a_dpin = get_unsigned_dpin(g, cell, ID::A);
  auto b_dpin = get_unsigned_dpin(g, cell, ID::B);

  setup_sink_by_name(exit_node, "A").connect_driver(a_dpin);

  if (type_op_of(exit_node) == Ntype_op::EQ) {
    setup_sink_by_name(exit_node, "A").connect_driver(b_dpin);
  } else {
    setup_sink_by_name(exit_node, "B").connect_driver(b_dpin);
  }
}

static void process_partially_assigned(hhds::Graph* g) {
  process_partially_assigned_other(g);
  process_partially_assigned_self_chains(g);
}

static void process_connect_outputs(RTLIL::Module* mod, hhds::Graph* g) {
  (void)mod;

  for (auto* wire : pending_outputs) {
    std::string wname(&wire->name.c_str()[1]);

    if (!has_graph_output(g, wname)) {
      (void)add_graph_output(g, wname, wire->port_id, wire->width);
    }

    if (wire2pin.find(wire) == wire2pin.end()) {
      continue;
    }

    hhds::Pin_class dpin3 = wire2pin[wire];

    I(wire->port_output);
    if (is_graph_output_pin(dpin3)) {
      continue;
    }

    hhds::Pin_class spin = get_graph_output_sink(g, wname);
    dpin3.connect_sink(spin);

    auto pn = pin_name_of(dpin3);
    if (pn.empty()) {
      set_pin_name(dpin3, wname);
    }

    // Note: LiveHD's change_to_driver_from_graph_out_sink swapped to driver
    // counterpart and stored that. In HHDS we can store the sink itself —
    // future readers use is_graph_output_pin to detect.
    if (wire->start_offset) {
      set_pin_offset(spin, wire->start_offset);
    }

    wire2pin[wire] = spin;
  }
}

static void process_cells(RTLIL::Module* mod, hhds::Graph* g) {
  for (auto cell : mod->cells()) {
    I(cell2node.find(cell) != cell2node.end());
    hhds::Node_class exit_node = cell2node[cell];

    if (std::strncmp(cell->type.c_str(), "$and", 4) == 0) {
      set_type_op(exit_node, Ntype_op::And);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      if ((a_bits == y_bits && b_bits == y_bits) || (a_sign && b_sign)) {
        exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::A));
        exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::B));
      } else {
        exit_node.create_sink_pin(0).connect_driver(get_unsigned_dpin(g, cell, ID::A));
        exit_node.create_sink_pin(0).connect_driver(get_unsigned_dpin(g, cell, ID::B));
      }
    } else if (std::strncmp(cell->type.c_str(), "$reduce_and", 11) == 0) {
      bool all_1bit = true;
      I(cell->hasParam(ID::A_WIDTH));
      if (cell->getParam(ID::A_WIDTH).as_int() > 1) {
        all_1bit = false;
      }

      if (all_1bit) {
        connect_all_inputs(exit_node.create_sink_pin(0), cell);
      } else {
        auto             y_bits = cell->getParam(ID::Y_WIDTH).as_int();
        hhds::Node_class and_node;
        if (y_bits == 1) {
          set_type_op(exit_node, Ntype_op::And);
          set_bits(exit_node.create_driver_pin(0), 1);
          and_node = exit_node;
        } else {
          and_node = create_typed_node(*g, Ntype_op::And, 1);
          set_type_op(exit_node, Ntype_op::Get_mask);
          set_bits(exit_node.create_driver_pin(0), y_bits);
          setup_sink_by_name(exit_node, "a").connect_driver(and_node.create_driver_pin(0));
          setup_sink_by_name(exit_node, "mask")
              .connect_driver(create_const(*g, *Dlop::create_integer(-1)));
        }

        auto ror_node = create_typed_node(*g, Ntype_op::Ror, 1);

        auto not_ror_node = create_typed_node(*g, Ntype_op::Not, 1);
        not_ror_node.create_sink_pin(0).connect_driver(ror_node.create_driver_pin(0));

        and_node.create_sink_pin(0).connect_driver(not_ror_node.create_driver_pin(0));

        auto a_dpin = get_unsigned_dpin(g, cell, ID::A);
        auto a_bits = cell->getParam(ID::A_WIDTH).as_int();

        if (a_bits > 1) {
          auto sra_node = create_typed_node(*g, Ntype_op::SRA, 1);

          auto not_a_node = create_typed_node(*g, Ntype_op::Not, a_bits);
          not_a_node.create_sink_pin(0).connect_driver(a_dpin);

          ror_node.create_sink_pin(0).connect_driver(not_a_node.create_driver_pin(0));

          setup_sink_by_name(sra_node, "a").connect_driver(a_dpin);
          setup_sink_by_name(sra_node, "b")
              .connect_driver(create_const(*g, *Dlop::create_integer(a_bits - 1)));

          and_node.create_sink_pin(0).connect_driver(sra_node.create_driver_pin(0));
        } else {
          and_node.create_sink_pin(0).connect_driver(a_dpin);
        }
      }
    } else if (std::strncmp(cell->type.c_str(), "$logic_and", 10) == 0 || std::strncmp(cell->type.c_str(), "$logic_or", 9) == 0) {
      I(cell->hasParam(ID::A_WIDTH));
      I(cell->hasParam(ID::B_WIDTH));

      auto op = Ntype_op::Or;
      if (std::strncmp(cell->type.c_str(), "$logic_and", 10) == 0) {
        op = Ntype_op::And;
      }

      auto a_dpin = get_dpin(g, cell, ID::A);
      auto b_dpin = get_dpin(g, cell, ID::B);

      auto             a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto             b_bits = cell->getParam(ID::B_WIDTH).as_int();
      auto             y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      hhds::Node_class and_node;
      if (y_bits == 1) {
        set_type_op(exit_node, op);
        set_bits(exit_node.create_driver_pin(0), 1);
        and_node = exit_node;
      } else {
        and_node = create_typed_node(*g, op, 1);
        set_type_op(exit_node, Ntype_op::Get_mask);
        setup_sink_by_name(exit_node, "a").connect_driver(and_node.create_driver_pin(0));
        setup_sink_by_name(exit_node, "mask")
            .connect_driver(create_const(*g, *Dlop::create_integer(-1)));
      }

      if (a_bits == 1) {
        and_node.create_sink_pin(0).connect_driver(a_dpin);
      } else {
        auto ror_node = create_typed_node(*g, Ntype_op::Ror, 1);
        ror_node.create_sink_pin(0).connect_driver(a_dpin);
        and_node.create_sink_pin(0).connect_driver(ror_node.create_driver_pin(0));
      }
      if (b_bits == 1) {
        and_node.create_sink_pin(0).connect_driver(b_dpin);
      } else {
        auto ror_node = create_typed_node(*g, Ntype_op::Ror, 1);
        ror_node.create_sink_pin(0).connect_driver(b_dpin);
        and_node.create_sink_pin(0).connect_driver(ror_node.create_driver_pin(0));
      }
    } else if (std::strncmp(cell->type.c_str(), "$not", 4) == 0) {
      I(get_input_size(cell) == get_output_size(cell));
      set_type_op(exit_node, Ntype_op::Not);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));
      connect_all_inputs(exit_node.create_sink_pin(0), cell);
    } else if (std::strncmp(cell->type.c_str(), "$logic_not", 10) == 0) {
      auto entry_node = create_typed_node(*g, Ntype_op::Ror, 1);

      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      if (y_bits == 1) {
        set_type_op(exit_node, Ntype_op::Not);
        set_bits(exit_node.create_driver_pin(0), 1);
        exit_node.create_sink_pin(0).connect_driver(entry_node.create_driver_pin(0));
      } else {
        auto not_node = create_typed_node(*g, Ntype_op::Not, 1);
        not_node.create_sink_pin(0).connect_driver(entry_node.create_driver_pin(0));

        set_type_op(exit_node, Ntype_op::Get_mask);
        setup_sink_by_name(exit_node, "a").connect_driver(not_node.create_driver_pin(0));
        setup_sink_by_name(exit_node, "mask")
            .connect_driver(create_const(*g, *Dlop::create_integer(-1)));
      }

      connect_all_inputs(entry_node.create_sink_pin(0), cell);
    } else if (std::strncmp(cell->type.c_str(), "$or", 3) == 0) {
      set_type_op(exit_node, Ntype_op::Or);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      if ((a_bits == y_bits && b_bits == y_bits) || (a_sign && b_sign)) {
        exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::A));
        exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::B));
      } else {
        exit_node.create_sink_pin(0).connect_driver(get_unsigned_dpin(g, cell, ID::A));
        exit_node.create_sink_pin(0).connect_driver(get_unsigned_dpin(g, cell, ID::B));
      }
    } else if (std::strncmp(cell->type.c_str(), "$reduce_or", 10) == 0
               || std::strncmp(cell->type.c_str(), "$reduce_bool", 12) == 0) {
      hhds::Pin_class entry_pin;
      auto            y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      if (y_bits == 1) {
        set_type_op(exit_node, Ntype_op::Ror);
        set_bits(exit_node.create_driver_pin(0), 1);
        entry_pin = exit_node.create_sink_pin(0);
      } else {
        auto ror_node = create_typed_node(*g, Ntype_op::Ror, 1);
        entry_pin     = ror_node.create_sink_pin(0);

        set_type_op(exit_node, Ntype_op::Get_mask);
        set_bits(exit_node.create_driver_pin(0), y_bits);
        setup_sink_by_name(exit_node, "a").connect_driver(ror_node.create_driver_pin(0));
        setup_sink_by_name(exit_node, "mask")
            .connect_driver(create_const(*g, *Dlop::create_integer(-1)));
      }
      connect_all_inputs(entry_pin, cell);
    } else if (std::strncmp(cell->type.c_str(), "$xor", 4) == 0) {
      set_type_op(exit_node, Ntype_op::Xor);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));

      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();

      if ((a_bits == y_bits && b_bits == y_bits) || (a_sign && b_sign)) {
        connect_all_inputs(exit_node.create_sink_pin(0), cell);
      } else {
        exit_node.create_sink_pin(0).connect_driver(get_unsigned_dpin(g, cell, ID::A));
        exit_node.create_sink_pin(0).connect_driver(get_unsigned_dpin(g, cell, ID::B));
      }
    } else if (std::strncmp(cell->type.c_str(), "$reduce_xor", 11) == 0) {
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto a_dpin = get_dpin(g, cell, ID::A);
      auto y_bits = get_output_size(cell);

      if (a_bits == 1 && y_bits == 1) {
        set_type_op(exit_node, Ntype_op::And);
        set_bits(exit_node.create_driver_pin(0), 1);
        exit_node.create_sink_pin(0).connect_driver(a_dpin);
      } else {
        hhds::Node_class and_node;
        if (y_bits == 1) {
          set_type_op(exit_node, Ntype_op::And);
          set_bits(exit_node.create_driver_pin(0), 1);
          and_node = exit_node;
        } else {
          and_node = create_typed_node(*g, Ntype_op::And, 1);

          set_type_op(exit_node, Ntype_op::Get_mask);
          set_bits(exit_node.create_driver_pin(0), 1);
          setup_sink_by_name(exit_node, "a").connect_driver(and_node.create_driver_pin(0));
          setup_sink_by_name(exit_node, "mask")
              .connect_driver(create_const(*g, *Dlop::create_integer(-1)));
        }

        auto xor_node = create_typed_node(*g, Ntype_op::Xor, 1);
        xor_node.create_sink_pin(0).connect_driver(a_dpin);

        for (int i = 1; i < a_bits; ++i) {
          auto sra_node = create_typed_node(*g, Ntype_op::SRA, 1);
          setup_sink_by_name(sra_node, "a").connect_driver(a_dpin);
          setup_sink_by_name(sra_node, "b")
              .connect_driver(create_const(*g, *Dlop::create_integer(i)));

          xor_node.create_sink_pin(0).connect_driver(sra_node.create_driver_pin(0));
        }

        and_node.create_sink_pin(0).connect_driver(xor_node.create_driver_pin(0));
        and_node.create_sink_pin(0).connect_driver(create_const(*g, *Dlop::create_integer(1)));
      }
    } else if (std::strncmp(cell->type.c_str(), "$xnor", 5) == 0) {
      auto s = get_output_size(cell);
      I(s < Bits_max);
      auto size = static_cast<Bits_t>(s);

      auto entry_node = create_typed_node(*g, Ntype_op::Xor, size);

      set_type_op(exit_node, Ntype_op::Not);
      set_bits(exit_node.create_driver_pin(0), size);
      exit_node.create_sink_pin(0).connect_driver(entry_node.create_driver_pin(0));

      connect_all_inputs(entry_node.create_sink_pin(0), cell);
    } else if (std::strncmp(cell->type.c_str(), "$reduce_xnor", 11) == 0) {
      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto a_dpin = get_dpin(g, cell, ID::A);
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

      if (a_bits == 1 && y_bits == 1) {
        set_type_op(exit_node, Ntype_op::Not);
        set_bits(exit_node.create_driver_pin(0), 1);
        exit_node.create_sink_pin(0).connect_driver(a_dpin);
      } else {
        hhds::Node_class not_node;
        if (y_bits == 1) {
          set_type_op(exit_node, Ntype_op::Not);
          set_bits(exit_node.create_driver_pin(0), 1);
          not_node = exit_node;
        } else {
          not_node = create_typed_node(*g, Ntype_op::Not, 1);

          set_type_op(exit_node, Ntype_op::Get_mask);
          set_bits(exit_node.create_driver_pin(0), 1);
          setup_sink_by_name(exit_node, "a").connect_driver(not_node.create_driver_pin(0));
          setup_sink_by_name(exit_node, "mask")
              .connect_driver(create_const(*g, *Dlop::create_integer(-1)));
        }

        auto and_node = create_typed_node(*g, Ntype_op::And, 1);
        and_node.create_sink_pin(0).connect_driver(create_const(*g, *Dlop::create_integer(1)));

        auto xor_node = create_typed_node(*g, Ntype_op::Xor, 1);
        xor_node.create_sink_pin(0).connect_driver(a_dpin);

        for (int i = 1; i < a_bits; ++i) {
          auto sra_node = create_typed_node(*g, Ntype_op::SRA, 1);
          setup_sink_by_name(sra_node, "a").connect_driver(a_dpin);
          setup_sink_by_name(sra_node, "b")
              .connect_driver(create_const(*g, *Dlop::create_integer(i)));

          xor_node.create_sink_pin(0).connect_driver(sra_node.create_driver_pin(0));
        }

        and_node.create_sink_pin(0).connect_driver(xor_node.create_driver_pin(0));
        not_node.create_sink_pin(0).connect_driver(and_node.create_driver_pin(0));
      }
    } else if (std::strncmp(cell->type.c_str(), "$dff", 4) == 0 || std::strncmp(cell->type.c_str(), "$dffe", 5) == 0
               || std::strncmp(cell->type.c_str(), "$dffsr", 6) == 0 || std::strncmp(cell->type.c_str(), "$adff", 5) == 0
               || std::strncmp(cell->type.c_str(), "$adffe", 6) == 0 || std::strncmp(cell->type.c_str(), "$adffsr", 7) == 0
               || std::strncmp(cell->type.c_str(), "$aldff", 6) == 0 || std::strncmp(cell->type.c_str(), "$sdff", 5) == 0
               || std::strncmp(cell->type.c_str(), "$sdffe", 6) == 0 || std::strncmp(cell->type.c_str(), "$sdffsr", 7) == 0) {
      set_type_op(exit_node, Ntype_op::Flop);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));
      std::string inst_name{cell->name.str()};
      if (!inst_name.empty()) {
        std::string nm = inst_name[0] == '\\' ? inst_name.substr(1) : inst_name;
        exit_node.attr(hhds::attrs::name).set(nm);
      }

      if (cell->hasPort(ID::Q)) {
        const RTLIL::Wire* wire = cell->getPort(ID::Q).chunks().at(0).wire;
        if (wire) {
          std::string wname(&wire->name.c_str()[1]);
          auto        dpin = exit_node.create_driver_pin(0);
          set_pin_name(dpin, wname);
        }
      }

      if (cell->hasParam(ID::CLK_POLARITY) && !cell->getParam(ID::CLK_POLARITY).as_bool()) {
        setup_sink_by_name(exit_node, "posclk")
            .connect_driver(create_const(*g, *Dlop::create_integer(0)));
      }

      if (cell->hasParam(ID::CLR)) {
        I(false);
        bool wants_negreset = false;
        if (cell->hasParam(ID::EN)) {
          if (cell->hasParam(ID::EN_POLARITY)) {
            wants_negreset = !cell->getParam(ID::EN_POLARITY).as_bool();
          }
          setup_sink_by_name(exit_node, "enable").connect_driver(get_dpin(g, cell, ID::EN));
        }

        if (cell->hasParam(ID::CLR_POLARITY)) {
          bool wants_negreset2 = cell->getParam(ID::CLR_POLARITY).as_int() == 0;
          I(wants_negreset2 == wants_negreset);
        }
        setup_sink_by_name(exit_node, "reset_pin").connect_driver(get_dpin(g, cell, ID::CLR));
      } else if (cell->hasPort(ID::SRST)) {
        if (cell->hasParam(ID::SRST_POLARITY)) {
          if (cell->getParam(ID::SRST_POLARITY).as_bool()) {
            setup_sink_by_name(exit_node, "negreset")
                .connect_driver(create_const(*g, *Dlop::create_integer(0)));
          } else {
            setup_sink_by_name(exit_node, "negreset")
                .connect_driver(create_const(*g, *Dlop::create_integer(1)));
          }
        }

        setup_sink_by_name(exit_node, "reset_pin").connect_driver(get_dpin(g, cell, ID::SRST));

        if (cell->hasParam(ID::SRST_VALUE)) {
          const auto& v = cell->getParam(ID::SRST_VALUE);
          if (!v.is_fully_zero()) {
            setup_sink_by_name(exit_node, "initial").connect_driver(get_dpin(g, cell, ID::SRST_VALUE));
          }
        }
      } else if (cell->hasPort(ID::ARST)) {
        if (cell->hasParam(ID::ARST_POLARITY)) {
          if (cell->getParam(ID::ARST_POLARITY).as_bool()) {
            setup_sink_by_name(exit_node, "negreset")
                .connect_driver(create_const(*g, *Dlop::create_integer(0)));
          } else {
            setup_sink_by_name(exit_node, "negreset")
                .connect_driver(create_const(*g, *Dlop::create_integer(1)));
          }
        }

        setup_sink_by_name(exit_node, "reset_pin").connect_driver(get_dpin(g, cell, ID::ARST));

        if (cell->hasParam(ID::ARST_VALUE)) {
          const auto& v = cell->getParam(ID::ARST_VALUE);
          if (!v.is_fully_zero()) {
            setup_sink_by_name(exit_node, "initial").connect_driver(get_dpin(g, cell, ID::ARST_VALUE));
          }
        }
      } else if (cell->hasPort(ID::ALOAD)) {
        if (cell->hasParam(ID::ALOAD_POLARITY)) {
          if (cell->getParam(ID::ALOAD_POLARITY).as_bool()) {
            setup_sink_by_name(exit_node, "negreset")
                .connect_driver(create_const(*g, *Dlop::create_integer(0)));
          } else {
            setup_sink_by_name(exit_node, "negreset")
                .connect_driver(create_const(*g, *Dlop::create_integer(1)));
          }
        }

        setup_sink_by_name(exit_node, "reset_pin").connect_driver(get_dpin(g, cell, ID::ALOAD));

        if (cell->hasPort(ID::AD)) {
          setup_sink_by_name(exit_node, "initial").connect_driver(get_dpin(g, cell, ID::AD));
        }
      }

      I(!cell->hasParam(ID::SET));

      if (cell->hasPort(ID::EN)) {
        auto enable_dpin = get_dpin(g, cell, ID::EN);
        if (cell->hasParam(ID::EN_POLARITY) && !cell->getParam(ID::EN_POLARITY).as_bool()) {
          auto not_node = create_typed_node(*g, Ntype_op::Not, 1);
          not_node.create_sink_pin(0).connect_driver(enable_dpin);
          setup_sink_by_name(exit_node, "enable").connect_driver(not_node.create_driver_pin(0));
        } else {
          setup_sink_by_name(exit_node, "enable").connect_driver(enable_dpin);
        }
      }
      if (std::strncmp(cell->type.c_str(), "$adff", 5) == 0 || std::strncmp(cell->type.c_str(), "$adffe", 6) == 0
          || std::strncmp(cell->type.c_str(), "$adffsr", 7) == 0 || std::strncmp(cell->type.c_str(), "$aldff", 6) == 0) {
        setup_sink_by_name(exit_node, "async")
            .connect_driver(create_const(*g, *Dlop::create_integer(1)));
      }

      setup_sink_by_name(exit_node, "clock_pin").connect_driver(get_dpin(g, cell, ID::CLK));
      auto flop_dpin = get_dpin(g, cell, ID::D);
      setup_sink_by_name(exit_node, "din").connect_driver(flop_dpin);
    } else if (std::strncmp(cell->type.c_str(), "$dlatch", 7) == 0) {
      set_type_op(exit_node, Ntype_op::Latch);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));

      if (cell->hasParam(ID::EN_POLARITY) && !cell->getParam(ID::EN_POLARITY).as_bool()) {
        setup_sink_by_name(exit_node, "posclk")
            .connect_driver(create_const(*g, *Dlop::create_integer(0)));
      }

      setup_sink_by_name(exit_node, "din").connect_driver(get_dpin(g, cell, ID::D));
      setup_sink_by_name(exit_node, "enable").connect_driver(get_dpin(g, cell, ID::EN));
    } else if (std::strncmp(cell->type.c_str(), "$neg", 4) == 0) {
      set_type_op(exit_node, Ntype_op::Sum);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));

      setup_sink_by_name(exit_node, "A").connect_driver(create_const(*g, *Dlop::create_integer(0)));
      setup_sink_by_name(exit_node, "B").connect_driver(get_dpin(g, cell, ID::A));
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
        set_type_op(exit_node, op);
        set_bits(exit_node.create_driver_pin(0), 1);
        connect_comparator(exit_node, cell);
      } else {
        auto cmp_node = create_typed_node(*g, op, 1);
        set_type_op(exit_node, Ntype_op::Get_mask);
        set_bits(exit_node.create_driver_pin(0), 1);
        setup_sink_by_name(exit_node, "a").connect_driver(cmp_node.create_driver_pin(0));
        setup_sink_by_name(exit_node, "mask")
            .connect_driver(create_const(*g, *Dlop::create_integer(-1)));
        connect_comparator(cmp_node, cell);
      }
    } else if (std::strncmp(cell->type.c_str(), "$ge", 3) == 0 || std::strncmp(cell->type.c_str(), "$le", 3) == 0
               || std::strncmp(cell->type.c_str(), "$ne", 3) == 0) {
      Ntype_op op = Ntype_op::LT;
      if (std::strncmp(cell->type.c_str(), "$le", 3) == 0) {
        op = Ntype_op::GT;
      } else if (std::strncmp(cell->type.c_str(), "$ne", 3) == 0) {
        op = Ntype_op::EQ;
      }

      auto cmp_node = create_typed_node(*g, op, 1);
      connect_comparator(cmp_node, cell);

      int y_bits = get_output_size(cell);
      if (y_bits == 1) {
        set_type_op(exit_node, Ntype_op::Not);
        set_bits(exit_node.create_driver_pin(0), 1);
        exit_node.create_sink_pin(0).connect_driver(cmp_node.create_driver_pin(0));
      } else {
        auto not_node = create_typed_node(*g, Ntype_op::Not, 1);
        not_node.create_sink_pin(0).connect_driver(cmp_node.create_driver_pin(0));

        set_type_op(exit_node, Ntype_op::Get_mask);
        setup_sink_by_name(exit_node, "a").connect_driver(not_node.create_driver_pin(0));
        setup_sink_by_name(exit_node, "mask")
            .connect_driver(create_const(*g, *Dlop::create_integer(-1)));
      }
    } else if (std::strncmp(cell->type.c_str(), "$mux", 4) == 0) {
      set_type_op(exit_node, Ntype_op::Mux);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));

      setup_sink_by_name(exit_node, "0").connect_driver(get_dpin(g, cell, ID::S));
      setup_sink_by_name(exit_node, "1").connect_driver(get_dpin(g, cell, ID::A));
      setup_sink_by_name(exit_node, "2").connect_driver(get_dpin(g, cell, ID::B));
    } else if (std::strncmp(cell->type.c_str(), "$add", 4) == 0 || std::strncmp(cell->type.c_str(), "$sub", 4) == 0) {
      bool a_sign = cell->getParam(ID::A_SIGNED).as_bool();
      bool b_sign = cell->getParam(ID::B_SIGNED).as_bool();

      auto a_bits = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits = cell->getParam(ID::B_WIDTH).as_int();
      int  y_bits = get_output_size(cell);

      auto sum_node = exit_node;
      if (y_bits <= a_bits || y_bits <= b_bits) {
        sum_node = create_typed_node(*g, Ntype_op::Sum, y_bits);
        set_type_op(exit_node, Ntype_op::And);
        set_bits(exit_node.create_driver_pin(0), y_bits);
        exit_node.create_sink_pin(0).connect_driver(sum_node.create_driver_pin(0));
        exit_node.create_sink_pin(0)
            .connect_driver(create_const(*g, *Dlop::get_mask_value(y_bits)));
      } else {
        set_type_op(exit_node, Ntype_op::Sum);
        set_bits(exit_node.create_driver_pin(0), y_bits);
      }

      std::string b{"A"};
      if (std::strncmp(cell->type.c_str(), "$sub", 4) == 0) {
        b = "B";
      }

      if (a_sign && b_sign) {
        auto a_dpin = get_dpin(g, cell, ID::A);
        auto b_dpin = get_dpin(g, cell, ID::B);
        setup_sink_by_name(sum_node, "A").connect_driver(a_dpin);
        setup_sink_by_name(sum_node, b).connect_driver(b_dpin);
      } else {
        auto a_dpin = get_unsigned_dpin(g, cell, ID::A);
        auto b_dpin = get_unsigned_dpin(g, cell, ID::B);
        setup_sink_by_name(sum_node, "A").connect_driver(a_dpin);
        setup_sink_by_name(sum_node, b).connect_driver(b_dpin);
      }
    } else if (std::strncmp(cell->type.c_str(), "$mul", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

#ifdef CELL_SIZE_IGNORE
      set_type_op(exit_node, Ntype_op::And);
      set_bits(exit_node.create_driver_pin(0), y_bits);
      auto mul_node = create_typed_node(*g, Ntype_op::Mult, y_bits);
      exit_node.create_sink_pin(0).connect_driver(mul_node.create_driver_pin(0));
      exit_node.create_sink_pin(0).connect_driver(create_const(*g, *Dlop::get_mask_value(y_bits)));
#else
      set_type_op(exit_node, Ntype_op::Mult);
      set_bits(exit_node.create_driver_pin(0), y_bits);
      auto mul_node = exit_node;
#endif
      setup_sink_by_name(mul_node, "A").connect_driver(get_dpin(g, cell, ID::A));
      setup_sink_by_name(mul_node, "A").connect_driver(get_dpin(g, cell, ID::B));
    } else if (std::strncmp(cell->type.c_str(), "$div", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

#ifdef CELL_SIZE_IGNORE
      set_type_op(exit_node, Ntype_op::And);
      set_bits(exit_node.create_driver_pin(0), y_bits);
      auto div_node = create_typed_node(*g, Ntype_op::Div, y_bits);
      exit_node.create_sink_pin(0).connect_driver(div_node.create_driver_pin(0));
      exit_node.create_sink_pin(0).connect_driver(create_const(*g, *Dlop::get_mask_value(y_bits)));
#else
      set_type_op(exit_node, Ntype_op::Div);
      set_bits(exit_node.create_driver_pin(0), y_bits);
      auto div_node = exit_node;
#endif
      auto a_dpin = get_unsigned_dpin(g, cell, ID::A);
      auto b_dpin = get_unsigned_dpin(g, cell, ID::B);

      setup_sink_by_name(div_node, "a").connect_driver(a_dpin);
      setup_sink_by_name(div_node, "b").connect_driver(b_dpin);
    } else if (std::strncmp(cell->type.c_str(), "$mod", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

      auto div_node = create_typed_node(*g, Ntype_op::Div, y_bits);
      auto mul_node = create_typed_node(*g, Ntype_op::Mult, y_bits);
      auto sub_node = create_typed_node(*g, Ntype_op::Sum, y_bits);

      hhds::Pin_class a_dpin = get_unsigned_dpin(g, cell, ID::A);
      hhds::Pin_class b_dpin = get_unsigned_dpin(g, cell, ID::B);

      set_type_op(exit_node, Ntype_op::And);
      set_bits(exit_node.create_driver_pin(0), y_bits);
      exit_node.create_sink_pin(0).connect_driver(create_const(*g, *Dlop::get_mask_value(y_bits)));

      setup_sink_by_name(div_node, "a").connect_driver(a_dpin);
      setup_sink_by_name(div_node, "b").connect_driver(b_dpin);

      setup_sink_by_name(mul_node, "A").connect_driver(div_node.create_driver_pin(0));
      setup_sink_by_name(mul_node, "A").connect_driver(b_dpin);

      setup_sink_by_name(sub_node, "A").connect_driver(a_dpin);
      setup_sink_by_name(sub_node, "B").connect_driver(mul_node.create_driver_pin(0));

      exit_node.create_sink_pin(0).connect_driver(sub_node.create_driver_pin(0));
    } else if (std::strncmp(cell->type.c_str(), "$pos", 4) == 0) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();

      if (cell->getParam(ID::A_SIGNED).as_bool()) {
        set_type_op(exit_node, Ntype_op::And);
        set_bits(exit_node.create_driver_pin(0), y_bits);
        exit_node.create_sink_pin(0).connect_driver(create_const(*g, *Dlop::get_mask_value(y_bits)));
        exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::A));
      } else {
        auto and_node = create_typed_node(*g, Ntype_op::And, y_bits);
        and_node.create_sink_pin(0).connect_driver(create_const(*g, *Dlop::get_mask_value(y_bits)));
        and_node.create_sink_pin(0).connect_driver(get_unsigned_dpin(g, cell, ID::A));

        set_type_op(exit_node, Ntype_op::Get_mask);
        set_bits(exit_node.create_driver_pin(0), y_bits + 1);
        setup_sink_by_name(exit_node, "a").connect_driver(and_node.create_driver_pin(0));
        setup_sink_by_name(exit_node, "mask")
            .connect_driver(create_const(*g, *Dlop::create_integer(-1)));
      }
    } else if (std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && cell->getParam(ID::B_SIGNED).as_bool()) {
      auto a_bits       = cell->getParam(ID::A_WIDTH).as_int();
      auto b_bits       = cell->getParam(ID::B_WIDTH).as_int();
      auto y_bits       = cell->getParam(ID::Y_WIDTH).as_int();
      auto max_out_bits = a_bits + (1 << b_bits);

      set_type_op(exit_node, Ntype_op::And);
      set_bits(exit_node.create_driver_pin(0), y_bits);

      auto sra_node = create_typed_node(*g, Ntype_op::SRA, max_out_bits);
      auto shl_node = create_typed_node(*g, Ntype_op::SHL, max_out_bits);
      auto mux_node = create_typed_node(*g, Ntype_op::Mux, max_out_bits);
      auto neg_node = create_typed_node(*g, Ntype_op::Sum, b_bits);
      auto lt_node  = create_typed_node(*g, Ntype_op::LT, 1);

      auto a_dpin = get_dpin(g, cell, ID::A);
      auto b_dpin = get_dpin(g, cell, ID::B);

      setup_sink_by_name(sra_node, "a").connect_driver(get_unsigned_dpin(g, cell, ID::A));
      setup_sink_by_name(sra_node, "b").connect_driver(get_dpin(g, cell, ID::B));
      auto y0_dpin = sra_node.create_driver_pin(0);

      setup_sink_by_name(neg_node, "A").connect_driver(create_const(*g, *Dlop::create_integer(0)));
      setup_sink_by_name(neg_node, "B").connect_driver(b_dpin);
      setup_sink_by_name(shl_node, "a").connect_driver(a_dpin);
      setup_sink_by_name(shl_node, "B").connect_driver(neg_node.create_driver_pin(0));
      auto y1_dpin = shl_node.create_driver_pin(0);

      setup_sink_by_name(lt_node, "A").connect_driver(b_dpin);
      setup_sink_by_name(lt_node, "B").connect_driver(create_const(*g, *Dlop::create_integer(0)));

      setup_sink_by_name(mux_node, "0").connect_driver(lt_node.create_driver_pin(0));
      setup_sink_by_name(mux_node, "1").connect_driver(y0_dpin);
      setup_sink_by_name(mux_node, "2").connect_driver(y1_dpin);
      auto y2_dpin = mux_node.create_driver_pin(0);

      exit_node.create_sink_pin(0).connect_driver(y2_dpin);
      exit_node.create_sink_pin(0).connect_driver(create_const(*g, *Dlop::get_mask_value(y_bits)));
    } else if (std::strncmp(cell->type.c_str(), "$shr", 4) == 0
               || (std::strncmp(cell->type.c_str(), "$shiftx", 6) == 0 && !cell->getParam(ID::B_SIGNED).as_bool())
               || (std::strncmp(cell->type.c_str(), "$sshr", 5) == 0 && !cell->getParam(ID::A_SIGNED).as_bool())) {
      auto y_bits = cell->getParam(ID::Y_WIDTH).as_int();
      set_type_op(exit_node, Ntype_op::SRA);
      set_bits(exit_node.create_driver_pin(0), y_bits);

      hhds::Pin_class dpin_a;
      hhds::Pin_class dpin_a_signed = get_dpin(g, cell, ID::A);
      if (cell->getParam(ID::A_SIGNED).as_bool()) {
        if ((int)bits_of(dpin_a_signed) < y_bits) {
          auto and_node = create_typed_node(*g, Ntype_op::And, y_bits);
          and_node.create_sink_pin(0).connect_driver(dpin_a_signed);
          and_node.create_sink_pin(0).connect_driver(create_const(*g, *Dlop::get_mask_value(y_bits)));

          auto tposs_node = create_typed_node(*g, Ntype_op::Get_mask, y_bits + 1);
          setup_sink_by_name(tposs_node, "a").connect_driver(and_node.create_driver_pin(0));
          setup_sink_by_name(tposs_node, "mask")
              .connect_driver(create_const(*g, *Dlop::create_integer(-1)));

          dpin_a = tposs_node.create_driver_pin(0);
        }
      }
      if (dpin_a.is_invalid()) {
        auto node = master_node(dpin_a_signed);
        if (type_op_of(node) == Ntype_op::Get_mask) {
          dpin_a = dpin_a_signed;
        } else if (is_const_pin(dpin_a_signed) && !hydrate_const_pin(dpin_a_signed).is_negative()) {
          dpin_a = dpin_a_signed;
        } else {
          auto tposs_node = create_typed_node(*g, Ntype_op::Get_mask);
          if (bits_of(dpin_a_signed)) {
            set_bits(tposs_node.create_driver_pin(0), bits_of(dpin_a_signed) + 1);
          }
          setup_sink_by_name(tposs_node, "a").connect_driver(dpin_a_signed);
          setup_sink_by_name(tposs_node, "mask")
              .connect_driver(create_const(*g, *Dlop::create_integer(-1)));

          dpin_a = tposs_node.create_driver_pin(0);
        }
      }

      setup_sink_by_name(exit_node, "a").connect_driver(dpin_a);
      setup_sink_by_name(exit_node, "b").connect_driver(get_dpin(g, cell, ID::B));
    } else if (std::strncmp(cell->type.c_str(), "$sshr", 5) == 0 && cell->getParam(ID::A_SIGNED).as_bool()) {
      set_type_op(exit_node, Ntype_op::SRA);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));

      setup_sink_by_name(exit_node, "a").connect_driver(get_dpin(g, cell, ID::A));
      setup_sink_by_name(exit_node, "b").connect_driver(get_dpin(g, cell, ID::B));
    } else if (std::strncmp(cell->type.c_str(), "$shl", 4) == 0 || std::strncmp(cell->type.c_str(), "$sshl", 5) == 0) {
      set_type_op(exit_node, Ntype_op::SHL);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));

      setup_sink_by_name(exit_node, "a").connect_driver(get_dpin(g, cell, ID::A));
      setup_sink_by_name(exit_node, "B").connect_driver(get_dpin(g, cell, ID::B));
    } else if (cell->type == "$mem" || cell->type == "$mem_v2") {
      set_type_op(exit_node, Ntype_op::Memory);

      uint32_t width = cell->getParam(ID::WIDTH).as_int();
      uint32_t depth = cell->getParam(ID::SIZE).as_int();
      auto     abits = cell->getParam(ID::ABITS).as_int();
      auto     rdports = cell->getParam(ID::RD_PORTS).as_int();
      auto     wrports = cell->getParam(ID::WR_PORTS).as_int();
      auto     transp  = cell->hasParam(ID::RD_TRANSPARENCY_MASK) ? cell->getParam(ID::RD_TRANSPARENCY_MASK).as_int() : 0;
      auto     rd_clke = cell->getParam(ID::RD_CLK_ENABLE).as_int();
      auto     wr_clkp = cell->getParam(ID::WR_CLK_POLARITY).as_int();

      setup_sink_by_name(exit_node, "bits")
          .connect_driver(create_const(*g, *Dlop::create_integer(width)));
      setup_sink_by_name(exit_node, "fwd")
          .connect_driver(create_const(*g, *Dlop::create_integer(transp)));
      setup_sink_by_name(exit_node, "posclk")
          .connect_driver(create_const(*g, *Dlop::create_integer(wr_clkp)));
      setup_sink_by_name(exit_node, "type")
          .connect_driver(create_const(*g, *Dlop::create_integer(rd_clke)));
      setup_sink_by_name(exit_node, "wensize")
          .connect_driver(create_const(*g, *Dlop::create_integer(width)));
      setup_sink_by_name(exit_node, "size")
          .connect_driver(create_const(*g, *Dlop::create_integer(depth)));

      for (int i = 0; i < wrports; i++) {
        auto port_n = i * 11;
        exit_node.create_sink_pin(static_cast<hhds::Port_id>(10 + port_n))
            .connect_driver(create_const(*g, *Dlop::create_integer(0)));
        exit_node.create_sink_pin(static_cast<hhds::Port_id>(3 + port_n))
            .connect_driver(create_pick_concat_dpin(g, cell->getPort(ID::WR_DATA).extract(i * width, width), false));
        exit_node.create_sink_pin(static_cast<hhds::Port_id>(4 + port_n))
            .connect_driver(create_pick_concat_dpin(g, cell->getPort(ID::WR_EN).extract(i * width, width), false));
        exit_node.create_sink_pin(static_cast<hhds::Port_id>(0 + port_n))
            .connect_driver(create_pick_concat_dpin(g, cell->getPort(ID::WR_ADDR).extract(i * abits, abits), false));
        exit_node.create_sink_pin(static_cast<hhds::Port_id>(2 + port_n))
            .connect_driver(create_pick_concat_dpin(g, cell->getPort(ID::WR_CLK).extract(i, 1), false));
      }
      for (int i = 0; i < rdports; i++) {
        auto port_n = (wrports + i) * 11;
        exit_node.create_sink_pin(static_cast<hhds::Port_id>(10 + port_n))
            .connect_driver(create_const(*g, *Dlop::create_integer(1)));
        exit_node.create_sink_pin(static_cast<hhds::Port_id>(4 + port_n))
            .connect_driver(create_pick_concat_dpin(g, cell->getPort(ID::RD_EN).extract(i, 1), false));
        exit_node.create_sink_pin(static_cast<hhds::Port_id>(0 + port_n))
            .connect_driver(create_pick_concat_dpin(g, cell->getPort(ID::RD_ADDR).extract(i * abits, abits), false));
        exit_node.create_sink_pin(static_cast<hhds::Port_id>(2 + port_n))
            .connect_driver(create_pick_concat_dpin(g, cell->getPort(ID::RD_CLK).extract(i, 1), false));
      }
    } else if (cell->type.c_str()[0] == '$' && cell->type.c_str()[1] != '_' && strncmp(cell->type.c_str(), "$paramod", 8) != 0) {
      log("likely error: add this cell type %s to lgraph\n", cell->type.c_str());
    } else if (std::strncmp(cell->type.c_str(), "$_AND_", 6) == 0) {
      set_type_op(exit_node, Ntype_op::And);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));
      exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::A));
      exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::B));
    } else if (std::strncmp(cell->type.c_str(), "$_OR_", 6) == 0) {
      set_type_op(exit_node, Ntype_op::Or);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));
      exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::A));
      exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::B));
    } else if (std::strncmp(cell->type.c_str(), "$_XOR_", 6) == 0) {
      set_type_op(exit_node, Ntype_op::Xor);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));
      exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::A));
      exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::B));
    } else if (std::strncmp(cell->type.c_str(), "$_NOT_", 6) == 0) {
      set_type_op(exit_node, Ntype_op::Not);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));
      exit_node.create_sink_pin(0).connect_driver(get_dpin(g, cell, ID::A));
    } else if (std::strncmp(cell->type.c_str(), "$_DFF_P_", 8) == 0) {
      set_type_op(exit_node, Ntype_op::Flop);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));
      setup_sink_by_name(exit_node, "clock_pin").connect_driver(get_dpin(g, cell, ID::C));
      setup_sink_by_name(exit_node, "din").connect_driver(get_dpin(g, cell, ID::D));
    } else if (std::strncmp(cell->type.c_str(), "$_DFF_N_", 8) == 0) {
      set_type_op(exit_node, Ntype_op::Flop);
      set_bits(exit_node.create_driver_pin(0), get_output_size(cell));
      setup_sink_by_name(exit_node, "posclk")
          .connect_driver(create_const(*g, *Dlop::create_integer(0)));
      setup_sink_by_name(exit_node, "clock_pin").connect_driver(get_dpin(g, cell, ID::C));
      setup_sink_by_name(exit_node, "din").connect_driver(get_dpin(g, cell, ID::D));
    } else if (std::strncmp(cell->type.c_str(), "$_DFF_NN", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_NP", 8) == 0
               || std::strncmp(cell->type.c_str(), "$_DFF_PP", 8) == 0 || std::strncmp(cell->type.c_str(), "$_DFF_PN", 8) == 0) {
      log_error("Found complex yosys DFFs, run `techmap -map +/adff2dff.v` before calling the yosys2lg pass\n");
      I(false);
    } else if (cell->type.c_str()[0] == '\\' || strncmp(cell->type.c_str(), "$paramod\\", 8) == 0) {
      if (cell->connections().empty()) {
        set_type_op(exit_node, Ntype_op::Or);
      } else {
        I(type_op_of(exit_node) == Ntype_op::Sub);

        absl::flat_hash_set<std::pair<hhds::Class_index, hhds::Class_index>> added_edges;

        // The sub's GraphIO (declared pin shape) comes straight from the
        // set_subnode bookkeeping HHDS already maintains on this Sub node.
        auto gio_ptr = exit_node.get_subnode_io();
        I(gio_ptr);

        for (auto& conn : cell->connections()) {
          RTLIL::SigSpec ss = conn.second;
          if (ss.size() == 0) {
            continue;
          }

          std::string name(&conn.first.c_str()[1]);
          if (str_tools::is_i(name)) {
            int pos = str_tools::to_i(name);
            // find name from pos
            std::string found_name;
            for (const auto& d : gio_ptr->get_input_pin_decls()) {
              if ((int)d.port_id == pos) {
                found_name = d.name;
                break;
              }
            }
            if (found_name.empty()) {
              for (const auto& d : gio_ptr->get_output_pin_decls()) {
                if ((int)d.port_id == pos) {
                  found_name = d.name;
                  break;
                }
              }
            }
            name = found_name;
          }

          if (gio_ptr->has_output(name)) {
            continue;
          }
          if (!gio_ptr->has_input(name)) {
            log_error("sub:%s does not have pin:%s as input\n", std::string{gio_ptr->get_name()}.c_str(), name.c_str());
          }

          hhds::Pin_class spin = exit_node.create_sink_pin(name);
          if (spin.is_invalid()) {
            continue;
          }

          hhds::Pin_class dpin = create_pick_concat_dpin(g, ss, true);

          auto edge_key = std::make_pair(dpin.get_class_index(), spin.get_class_index());
          if (added_edges.find(edge_key) != added_edges.end()) {
            auto or_node = create_typed_node(*g, Ntype_op::Or, ss.size());
            dpin.connect_sink(or_node.create_sink_pin(0));
            dpin = or_node.create_driver_pin(0);
          }
          dpin.connect_sink(spin);
          added_edges.insert(edge_key);
        }
      }
    } else {
      log_error("Black box addition from yosys frontend, cell type %s not found instance %s\n",
                cell->type.c_str(),
                cell->name.c_str());
      I(false);
    }
  }
}

struct Yosys2lg_Pass : public Yosys::Pass {
  Yosys2lg_Pass() : Pass("yosys2lg") {}
  virtual void help() {
    log("\n");
    log("    yosys2lg [options]\n");
    log("\n");
    log("Write multiple hhds graphs from the selected modules.\n");
    log("\n");
    log("    -path [default=lgdb]\n");
    log("        Specify from which path to read\n");
    log("\n");
    log("\n");
  }

  virtual void execute(std::vector<std::string> args, RTLIL::Design* design) {
    log_header(design, "Executing yosys2lg pass (convert from yosys to hhds::Graph).\n");

    size_t      argidx;
    std::string path("lgdb");

    for (argidx = 1; argidx < args.size(); argidx++) {
      if (args[argidx] == "-path") {
        path = std::string(args[++argidx]);
        continue;
      }
      break;
    }

    extra_args(args, argidx, design);

    ct_all.setup(design);
    cell_port_outputs.clear();
    cell_port_inputs.clear();
    driven_signals.clear();

    auto& lib = Hhds_graph_library::instance(path);

    // First pass: declare IOs for every module so cross-module Sub references
    // can resolve pin shapes.
    for (const auto& it : design->modules_) {
      RTLIL::Module* mod = it.second;
      std::string    mod_name(&mod->name.c_str()[1]);

      auto gio = lib.find_io(mod_name);
      if (!gio) {
        gio = lib.create_io(mod_name);
      }

      for (const auto& port : mod->ports) {
        RTLIL::Wire* wire = mod->wire(port);
        std::string  wire_name(&wire->name.c_str()[1]);

        auto cell_port = absl::StrCat(mod_name, "_:_", wire_name);
        if (wire->port_input && !wire->port_output) {
          cell_port_inputs.insert(cell_port);
          if (gio->has_input(wire_name) || gio->has_output(wire_name)) {
            // Update positional port_id if needed: delete + re-add
            if (gio->has_input(wire_name)) {
              gio->delete_input(wire_name);
            } else {
              gio->delete_output(wire_name);
            }
            gio->add_input(wire_name, wire->port_id);
          } else {
            gio->add_input(wire_name, wire->port_id);
          }
          gio->set_bits(wire_name, wire->width);
        } else if (!wire->port_input && wire->port_output) {
          cell_port_outputs.insert(cell_port);
          if (gio->has_input(wire_name) || gio->has_output(wire_name)) {
            if (gio->has_input(wire_name)) {
              gio->delete_input(wire_name);
            } else {
              gio->delete_output(wire_name);
            }
            gio->add_output(wire_name, wire->port_id);
          } else {
            gio->add_output(wire_name, wire->port_id);
          }
          gio->set_bits(wire_name, wire->width);
        } else {
          log_error("inou.yosys.tolg: mod:%s bidirectional:%s NOT supported by livehd\n",
                    mod->name.c_str(),
                    wire->name.c_str());
        }
      }
    }

    // Second pass: build module bodies.
    for (const auto& it : design->modules_) {
      RTLIL::Module* mod = it.second;
      if (design->selected_module(it.first)) {
        std::string mod_name(&(mod->name.c_str()[1]));

        driven_signals.clear();
        cell_port_inputs.clear();
        cell_port_outputs.clear();
        wire2pin.clear();
        cell2node.clear();
        partially_assigned.clear();
        partially_assigned_bits.clear();
        partially_assigned_fwd.clear();

        TRACE_EVENT("inou", nullptr, [&mod_name](perfetto::EventContext ctx) { ctx.event()->set_name("YOSYS_tolg_" + mod_name); });

        for (const auto& conn : mod->connections()) {
          const RTLIL::SigSpec lhs = conn.first;
          for (auto& lchunk : lhs.chunks()) {
            const RTLIL::Wire* lhs_wire = lchunk.wire;
            if (lchunk.width == 0) {
              continue;
            }
            driven_signals.insert(lhs_wire->hashidx_);
          }
        }

        for (const auto& port : mod->ports) {
          RTLIL::Wire* wire = mod->wire(port);
          if (wire->port_output) {
            pending_outputs.emplace_back(wire);
          }
        }

        auto gio = lib.find_io(mod_name);
        I(gio);
        std::shared_ptr<hhds::Graph> g_shared;
        if (gio->has_graph()) {
          g_shared = gio->get_graph();
        } else {
          g_shared = gio->create_graph();
        }
        auto* g = g_shared.get();

        process_assigns(mod, g);
        process_cell_drivers_intialization(mod, g);
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
