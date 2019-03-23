//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <math.h>

#include "lgedgeiter.hpp"
#include "lgyosys_dump.hpp"

// FIXME: In attributes Attr_type_pin<RTLIL::Wire *> pin2
RTLIL::Wire *Lgyosys_dump::get_wire(const Node_pin &pin) {
  // std::pair<Index_ID,Port_ID> nid_pid = std::make_pair(inp_edge.get_idx(), inp_edge.get_out_pin().get_pid());
  if(input_map.find(pin.get_idx()) != input_map.end()) {
    return input_map[pin.get_idx()];
  }
  if(output_map.find(pin.get_idx()) != output_map.end()) {
    return output_map[pin.get_idx()];
  }

  std::pair<Index_ID, Port_ID> nid_pid = std::make_pair(pin.get_idx(), pin.get_pid());
  if(cell_output_map.find(nid_pid) != cell_output_map.end()) {
    return cell_output_map[nid_pid];
  }

  log("trying to get wire for nid %ld, %hu\n", nid_pid.first.value, nid_pid.second);
  assert(false);
}

RTLIL::Wire *Lgyosys_dump::create_tree(const LGraph *g, std::vector<RTLIL::Wire *> &wires, RTLIL::Module *mod,
                                       add_cell_fnc_sign add_fnc, bool sign, RTLIL::Wire *result_wire) {
  if(wires.size() == 0)
    return nullptr;

  if(wires.size() == 1)
    return wires[0];

  std::vector<RTLIL::Wire *> next_level;
  for(uint32_t current = 0; current < wires.size() / 2; current += 2) {
    RTLIL::Wire *aWire = nullptr;
    if(wires.size() > 2)
      aWire = mod->addWire(next_id(g), result_wire->width);
    else
      aWire = result_wire;

    (mod->*add_fnc)(next_id(g), wires[current], wires[current + 1], aWire, sign, "");
    next_level.push_back(aWire);
  }
  if(wires.size() % 2 == 1)
    next_level.push_back(wires[wires.size() - 1]);

  return create_tree(g, next_level, mod, add_fnc, sign, result_wire);
}

RTLIL::Wire *Lgyosys_dump::create_io_wire(const LGraph *g, const Node_pin &pin, RTLIL::Module *module) {

  assert(g->has_wirename(pin)); // IO must have name
  RTLIL::IdString name = absl::StrCat("\\", g->get_node_wirename(pin));

  RTLIL::Wire *new_wire  = module->addWire(name, g->get_bits(pin));
  new_wire->start_offset = g->get_offset(pin);

  module->ports.push_back(name);
  new_wire->port_id = module->ports.size();

  return new_wire;
}

void Lgyosys_dump::create_blackbox(const LGraph &subgraph, RTLIL::Design *design) {
  RTLIL::Module *mod            = new RTLIL::Module;
  mod->name                     = absl::StrCat("\\", subgraph.get_name());
  mod->attributes["\\blackbox"] = RTLIL::Const(1);

  static absl::flat_hash_set<std::string_view> created_blackboxes; // FIXME: Remembers forever???
  if(created_blackboxes.find(subgraph.get_name()) == created_blackboxes.end()) {
    design->add(mod);
    created_blackboxes.insert(subgraph.get_name());
  }

#if 1
  uint32_t port_id = 0;
  subgraph.each_graph_input([&port_id,mod,&subgraph](const Node_pin &pin) {
    std::string name = absl::StrCat("\\", subgraph.get_node_wirename(pin));
    RTLIL::Wire *wire = mod->addWire(name, subgraph.get_bits(pin));
    wire->port_id     = port_id++;
    wire->port_input  = true;
    wire->port_output = false;
  });

  subgraph.each_graph_output([&port_id,mod,&subgraph](const Node_pin &pin) {
    std::string name = absl::StrCat("\\", subgraph.get_node_wirename(pin));
    RTLIL::Wire *wire = mod->addWire(name, subgraph.get_bits(pin));
    wire->port_id     = port_id++;
    wire->port_input  = false;
    wire->port_output = true;
  });
#else
  uint32_t port_idx = 0;
  for(auto &nid : subgraph.fast()) {
    if(subgraph.get_node(nid).get_type().op != GraphIO_Op)
      continue;
    std::string name = "\\";
    if(subgraph.is_graph_input(nid)) {
      name += subgraph.get_graph_input_name(nid);
    } else {
      name += subgraph.get_graph_output_name(nid);
    }
    RTLIL::Wire *wire = mod->addWire(name, subgraph.get_bits(nid));
    wire->port_id     = port_idx++; // FIXME: update to get from graph
    wire->port_input  = subgraph.is_graph_input(nid);
    wire->port_output = subgraph.is_graph_output(nid);
  }
#endif
  mod->fixup_ports();
}

void Lgyosys_dump::create_memory(const LGraph *g, RTLIL::Module *module, Index_ID idx) {
  assert(g->get_node(idx).get_type().op == Memory_Op);

  const std::string cell_name(g->get_node_wirename(idx));

  RTLIL::Cell *memory = module->addCell(absl::StrCat("\\", cell_name), RTLIL::IdString("$mem"));

  RTLIL::SigSpec wr_addr, wr_data, wr_en, rd_addr, rd_data;
  int nrd_ports = 0;
  RTLIL::SigSpec rd_en;
  RTLIL::Wire   *clk        = nullptr;
  bool           rd_clk     = false;
  bool           wr_clk     = false;
  RTLIL::State   rd_posedge = RTLIL::State::Sx;
  RTLIL::State   wr_posedge = RTLIL::State::Sx;
  RTLIL::State   transp     = RTLIL::State::Sx;

  for(const auto &c : g->inp_edges(idx)) {
    Port_ID input_pin = c.get_inp_pin().get_pid();

    if(input_pin == LGRAPH_MEMOP_CLK) {
      if(clk != nullptr)
        log_error("Internal Error: multiple wires assigned to same mem port\n");
      clk = get_wire(c.get_out_pin());
      assert(clk);

    } else if(input_pin == LGRAPH_MEMOP_SIZE) {
      if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
        log_error("Internal Error: Mem size is not a constant.\n");
      memory->setParam("\\SIZE", RTLIL::Const(g->node_value_get(c.get_idx())));

    } else if(input_pin == LGRAPH_MEMOP_OFFSET) {
      if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
        log_error("Internal Error: Mem size is not a constant.\n");
      memory->setParam("\\OFFSET", RTLIL::Const(g->node_value_get(c.get_idx())));

    } else if(input_pin == LGRAPH_MEMOP_ABITS) {
      if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
        log_error("Internal Error: Mem addr bits is not a constant.\n");
      memory->setParam("\\ABITS", RTLIL::Const(g->node_value_get(c.get_idx())));

    } else if(input_pin == LGRAPH_MEMOP_WRPORT) {
      if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
        log_error("Internal Error: Mem num wr ports is not a constant.\n");
      memory->setParam("\\WR_PORTS", RTLIL::Const(g->node_value_get(c.get_idx())));

    } else if(input_pin == LGRAPH_MEMOP_RDPORT) {
      if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
        log_error("Internal Error: Mem num rd ports is not a constant.\n");
      assert(nrd_ports==0); // Do not double set
      nrd_ports = g->node_value_get(c.get_idx());
      memory->setParam("\\RD_PORTS", RTLIL::Const(nrd_ports));

    } else if(input_pin == LGRAPH_MEMOP_RDTRAN) {
      if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
        log_error("Internal Error: Mem rd_transp is not a constant.\n");
      transp = g->node_value_get(c.get_idx()) ? RTLIL::State::S1 : RTLIL::State::S0;

    } else if(input_pin == LGRAPH_MEMOP_RDCLKPOL) {
      if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
        log_error("Internal Error: Mem RD_CLK polarity is not a constant.\n");
      rd_posedge = (g->node_value_get(c.get_idx()) == 0) ? RTLIL::State::S1 : RTLIL::State::S0;
      rd_clk = true;

    } else if(input_pin == LGRAPH_MEMOP_WRCLKPOL) {
      if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
        log_error("Internal Error: Mem WR_CLK polarity is not a constant.\n");
      wr_posedge = (g->node_value_get(c.get_idx()) == 0) ? RTLIL::State::S1 : RTLIL::State::S0;
      wr_clk = true;

    } else if(LGRAPH_MEMOP_ISWRADDR(input_pin)) {
      wr_addr.append(RTLIL::SigSpec(get_wire(c.get_out_pin())));

    } else if(LGRAPH_MEMOP_ISWRDATA(input_pin)) {
      wr_data.append(RTLIL::SigSpec(get_wire(c.get_out_pin())));

    } else if(LGRAPH_MEMOP_ISWREN(input_pin)) {
      RTLIL::Wire *en = get_wire(c.get_out_pin());
      assert(en->width == 1);
      // yosys requires one wr_en per bit
      wr_en.append(RTLIL::SigSpec(RTLIL::SigBit(en), g->get_bits(idx)));

    } else if(LGRAPH_MEMOP_ISRDADDR(input_pin)) {
      rd_addr.append(RTLIL::SigSpec(get_wire(c.get_out_pin())));

    } else if(LGRAPH_MEMOP_ISRDEN(input_pin)) {
      rd_en.append(RTLIL::SigSpec(get_wire(c.get_out_pin())));
      // rd_en.append(RTLIL::SigSpec(RTLIL::State::Sx));

    } else {
      log_error("Unrecognized input pid %hu\n", input_pin);
      assert(false);
    }
  }

  if (rd_clk)
    assert(rd_posedge != RTLIL::State::Sx);
  if (wr_clk)
  assert(wr_posedge != RTLIL::State::Sx);
  assert(transp  != RTLIL::State::Sx);

  memory->setParam("\\MEMID", RTLIL::Const(cell_name));
  memory->setParam("\\WIDTH", g->get_bits(idx));

  int rd_port_bits = memory->getParam("\\RD_PORTS").as_int();
  if (rd_clk) {
    memory->setParam("\\RD_CLK_ENABLE"  , RTLIL::Const(RTLIL::State::S1    , rd_port_bits));
    memory->setParam("\\RD_CLK_POLARITY", RTLIL::Const(rd_posedge          , rd_port_bits));
    memory->setPort("\\RD_CLK"          , RTLIL::SigSpec(RTLIL::SigBit(clk), rd_port_bits));
  }else{
    memory->setParam("\\RD_CLK_ENABLE"  , RTLIL::Const(RTLIL::State::S0  , rd_port_bits));
    memory->setParam("\\RD_CLK_POLARITY", RTLIL::Const(RTLIL::State::S0  , rd_port_bits));
    memory->setPort("\\RD_CLK"          , RTLIL::SigSpec(RTLIL::State::Sx, rd_port_bits));
  }
  int wr_port_bits = memory->getParam("\\WR_PORTS").as_int();
  if (wr_clk) {
    memory->setParam("\\WR_CLK_ENABLE"  , RTLIL::Const(RTLIL::State::S1    , wr_port_bits));
    memory->setParam("\\WR_CLK_POLARITY", RTLIL::Const(wr_posedge          , wr_port_bits));
    memory->setPort("\\WR_CLK"          , RTLIL::SigSpec(RTLIL::SigBit(clk), wr_port_bits));
  }else{
    memory->setParam("\\WR_CLK_ENABLE"  , RTLIL::Const(RTLIL::State::S0  , wr_port_bits));
    memory->setParam("\\WR_CLK_POLARITY", RTLIL::Const(RTLIL::State::S0  , wr_port_bits));
    memory->setPort("\\WR_CLK"          , RTLIL::SigSpec(RTLIL::State::Sx, wr_port_bits));
  }

  memory->setParam("\\INIT", RTLIL::Const::from_string("x"));

  memory->setParam("\\RD_TRANSPARENT", RTLIL::Const(transp, memory->getParam("\\RD_PORTS").as_int()));

  memory->setPort("\\WR_DATA", wr_data);
  memory->setPort("\\WR_ADDR", wr_addr);
  memory->setPort("\\WR_EN", wr_en);

  memory->setPort("\\RD_DATA", RTLIL::SigSpec(mem_output_map[idx]));
  memory->setPort("\\RD_ADDR", rd_addr);

  assert(nrd_ports == rd_en.size());
  memory->setPort("\\RD_EN", rd_en);

}

void Lgyosys_dump::create_subgraph(const LGraph *g, RTLIL::Module *module, Index_ID idx) {
  assert(g->get_node(idx).get_type().op == SubGraph_Op);

  auto sub_id = g->subgraph_id_get(idx);
  LGraph *subgraph = LGraph::open(g->get_path(), sub_id);
  if(subgraph == nullptr) {
    // FIXME: prevent loading the whole graph just to read the IOs if
    // hierarchy is set to false
    auto subgraph_name = g->get_library().get_name(sub_id);
    auto source        = g->get_library().get_source(sub_id);
    subgraph           = LGraph::create(g->get_path(), subgraph_name, source);
  }
  if(hierarchy) {
    _subgraphs.insert(subgraph);
  } else {
    create_blackbox(*subgraph, module->design);
  }

  RTLIL::IdString instance_name("\\tmp");
  if(g->get_instance_name_id(idx) == 0) {
    instance_name = next_id(g);
#ifndef NDEBUG
    fmt::print("inou_yosys got empty inst_name for cell type {}\n", subgraph->get_name());
#endif
  } else {
    assert(g->get_node_instancename(idx) != "");
    instance_name = RTLIL::IdString(absl::StrCat("\\", g->get_node_instancename(idx)));
  }

  fmt::print("inou_yosys instance_name:{}, subgraph->get_name():{}\n", g->get_node_instancename(idx), subgraph->get_name());
  RTLIL::Cell *new_cell = module->addCell(instance_name, absl::StrCat("\\", subgraph->get_name()));
  for(const auto &c : g->inp_edges(idx)) {
    auto  port = subgraph->get_graph_input_name_from_pid(c.get_inp_pin().get_pid());
    RTLIL::Wire *input = get_wire(c.get_out_pin());
    new_cell->setPort(absl::StrCat("\\", port).c_str(), input);
  }
  for(const auto &c : g->out_edges(idx)) {
    auto  port = subgraph->get_graph_output_name_from_pid(c.get_out_pin().get_pid());
    RTLIL::Wire *output = get_wire(c.get_out_pin());
    new_cell->setPort(absl::StrCat("\\", port).c_str(), output);
  }
}

void Lgyosys_dump::create_subgraph_outputs(const LGraph *g, RTLIL::Module *module, Index_ID idx) {
  assert(g->get_node(idx).get_type().op == SubGraph_Op);

  auto subgraph_name = g->get_library().get_name(g->subgraph_id_get(idx));
  LGraph *subgraph   = LGraph::open(g->get_path(), subgraph_name);
  assert(subgraph);
  std::set<Port_ID> visited_out_pids;

  for(auto &edge : g->out_edges(idx)) {
    // only once per pid
    if(visited_out_pids.find(edge.get_out_pin().get_pid()) != visited_out_pids.end())
      continue;

    visited_out_pids.insert(edge.get_out_pin().get_pid());

    std::string name;
    if(g->has_wirename(edge.get_out_pin())) {
      name = absl::StrCat("\\", g->get_node_wirename(edge.get_out_pin()));
    } else {
      name = absl::StrCat("\\lgraph_cell_", subgraph->get_graph_output_name_from_pid(edge.get_out_pin().get_pid()), std::to_string(idx));
      if(g->has_wirename(name)) {
        name = next_wire(g);
      }
    }
    uint16_t out_size = g->get_bits(edge.get_out_pin());

    RTLIL::Wire *new_wire                                              = module->addWire(name, out_size);
    cell_output_map[std::make_pair(edge.get_out_pin().get_idx(), edge.get_out_pin().get_pid())] = new_wire;
  }
}

void Lgyosys_dump::create_wires(const LGraph *g, RTLIL::Module *module) {
  // first create all the output wires

  g->each_graph_output([g,module,this](const Node_pin &pin) {
    output_map[pin.get_idx()] = create_io_wire(g, pin, module);
    output_map[pin.get_idx()]->port_input  = false;
    output_map[pin.get_idx()]->port_output = true;
  });

  g->each_graph_input([g,module,this](const Node_pin &pin) {
    input_map[pin.get_idx()] = create_io_wire(g, pin, module);
    input_map[pin.get_idx()]->port_input  = true;
    input_map[pin.get_idx()]->port_output = false;
  });

  for(auto nid : g->fast()) {

    if(g->get_node(nid).get_type().op == GraphIO_Op)
      continue; // handled before with each_output/each_input

    RTLIL::IdString name;
    RTLIL::IdString yosys_op;
    auto wname = g->get_node_wirename(nid);
    if(!wname.empty()) {
      name = RTLIL::IdString(absl::StrCat("\\", wname));
    } else {
      if(!g->has_wirename(absl::StrCat("lgraph_cell_", std::to_string(nid))))
        name = RTLIL::IdString(absl::StrCat("\\lgraph_cell_", std::to_string(nid)));
      else {
        name = RTLIL::IdString(absl::StrCat("\\", next_wire(g)));
      }
    }

    if(g->get_node(nid).get_type().op == U32Const_Op) {
      RTLIL::Wire *new_wire = module->addWire(name, g->get_bits(nid));

      // constants treated as inputs
      module->connect(new_wire, RTLIL::SigSpec(g->node_value_get(nid), g->get_bits(nid)));
      input_map[nid] = new_wire;
      continue;

    } else if(g->get_node(nid).get_type().op == StrConst_Op) {
      bool blackbox_idx = 0;
      for(const auto &c : g->out_edges(nid)) {
        if (g->get_node(c.get_inp_pin()).get_type().op == BlackBox_Op) {
          blackbox_idx = g->get_node(c.get_inp_pin()).get_nid();
        }else{
          I(!blackbox_idx); // if blackbox, always blackbox argument
        }
      }
      if (blackbox_idx==0) {
        auto const_val = g->node_const_value_get(nid);
        RTLIL::Wire *new_wire  = module->addWire(name, const_val.size()); // FIXME: This assumes that const are in base 2. OK always?

        // constants treated as inputs
        module->connect(new_wire, RTLIL::SigSpec(RTLIL::Const::from_string(std::string(const_val))));
        input_map[nid] = new_wire;
      }
      continue;

    } else if(g->get_node(nid).get_type().op == SubGraph_Op) {
      create_subgraph_outputs(g, module, nid);
      continue;

    } else if(g->get_node(nid).get_type().op == TechMap_Op) {

      const Tech_cell *tcell = g->get_tlibrary().get_const_cell(g->tmap_id_get(nid));

      std::set<Port_ID> visited_out_pids;

      for(auto &edge : g->out_edges(nid)) {
        // only once per pid
        if(visited_out_pids.find(edge.get_out_pin().get_pid()) != visited_out_pids.end())
          continue;

        visited_out_pids.insert(edge.get_out_pin().get_pid());
        uint16_t out_size = g->get_bits(edge.get_out_pin());

        std::string full_name;
        if(g->has_wirename(edge.get_out_pin())) {
          full_name = absl::StrCat("\\", g->get_node_wirename(edge.get_out_pin()));
        }else if(edge.get_out_pin().get_pid() < tcell->n_outs()) {
          full_name = absl::StrCat("\\", name.str().substr(1), tcell->get_name(edge.get_out_pin().get_pid()));
        }else{
          full_name = absl::StrCat("\\", name.str().substr(1), next_id(g).c_str());
        }

        cell_output_map[std::make_pair(edge.get_out_pin().get_idx(), edge.get_out_pin().get_pid())] = module->addWire(full_name, out_size);
      }
      continue;
    } else if(g->get_node(nid).get_type().op == Memory_Op) {

      std::set<Port_ID> visited_out_pids;

      for(auto &edge : g->out_edges(nid)) {
        if(visited_out_pids.find(edge.get_out_pin().get_pid()) != visited_out_pids.end())
          continue;

        visited_out_pids.insert(edge.get_out_pin().get_pid());

        RTLIL::Wire *new_wire = module->addWire(next_id(g), g->get_bits(edge.get_out_pin()));
        cell_output_map[std::make_pair(edge.get_out_pin().get_idx(), edge.get_out_pin().get_pid())] = new_wire;
        mem_output_map[nid].push_back(RTLIL::SigChunk(new_wire));
      }
      continue;
    } else if(g->get_node(nid).get_type().op == BlackBox_Op) {
      for(auto &edge : g->out_edges(nid)) {
        RTLIL::Wire *new_wire = module->addWire(next_id(g), g->get_bits(edge.get_out_pin()));
        cell_output_map[std::make_pair(edge.get_out_pin().get_idx(), edge.get_out_pin().get_pid())] = new_wire;
      }
      continue;
    } else if(g->get_node(nid).get_type().op == And_Op || g->get_node(nid).get_type().op == Or_Op || g->get_node(nid).get_type().op == Xor_Op) {
      // This differentiates between binary and unary Ands, Ors, Xors
      int count = 0;
      for(const auto &edge : g->out_edges(nid)) {
        if(cell_output_map.find(std::make_pair(edge.get_out_pin().get_idx(), edge.get_out_pin().get_pid())) == cell_output_map.end()) {
          ++count;
          assert(count == 1);
          // bitwise operators have the regular size
          uint16_t out_size = g->get_bits(edge.get_out_pin());
          assert(out_size);

#if 0
          // reduce operator have 1 bit
          if(edge.get_out_pin().get_pid() == 1)
            out_size = 1;
#endif
          RTLIL::Wire *new_wire                                              = module->addWire(name, out_size);
          cell_output_map[std::make_pair(edge.get_out_pin().get_idx(), edge.get_out_pin().get_pid())] = new_wire;
        }
      }
      continue;
    }

    const auto node = g->get_node(nid);
    RTLIL::Wire *result                     = module->addWire(name, g->get_bits(node.get_driver_pin()));
    cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)] = result;
  }
}

void Lgyosys_dump::to_yosys(const LGraph *g) {
  auto name = g->get_name();

  RTLIL::Module *module = design->addModule(absl::StrCat("\\",name));

  input_map.clear();
  output_map.clear();
  cell_output_map.clear();

  create_wires(g, module);

  g->each_graph_output([g,this,module](const Node_pin &pin) {
    assert(g->is_graph_output(pin));
    for(const auto &c : g->inp_edges(g->get_node(pin).get_nid())) {
      RTLIL::SigSpec lhs = RTLIL::SigSpec(output_map[pin.get_idx()]);
      RTLIL::SigSpec rhs = RTLIL::SigSpec(get_wire(c.get_out_pin()));
      assert(rhs != lhs);
      module->connect(lhs, rhs);
    }
  });

  // now create nodes and make connections
  for(auto nid : g->fast()) {
    auto op = g->get_node(nid).get_type().op;
    if (op == GraphIO_Op)
      continue; // outputs already handled, inputs are not used

    auto node = g->get_node(nid);

    if(op != Memory_Op && op != SubGraph_Op && op != BlackBox_Op && !node.has_outputs())
      continue;

    RTLIL::IdString name = RTLIL::IdString(absl::StrCat("\\lgraph_cell_", std::to_string(nid)));
    RTLIL::IdString yosys_op;

    uint16_t size = 0;
    // bool     u_type = true;

    switch(g->get_node(nid).get_type().op) {
    case GraphIO_Op:
    case U32Const_Op:
    case StrConst_Op:
      continue;

    case Sum_Op: {
      std::vector<RTLIL::Wire *> add_unsigned;
      std::vector<RTLIL::Wire *> add_signed;
      std::vector<RTLIL::Wire *> sub_unsigned;
      std::vector<RTLIL::Wire *> sub_signed;

      size = 0;
      for(const auto &c : g->inp_edges(nid)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          add_signed.push_back(get_wire(c.get_out_pin()));
          break;
        case 1:
          add_unsigned.push_back(get_wire(c.get_out_pin()));
          break;
        case 2:
          sub_signed.push_back(get_wire(c.get_out_pin()));
          break;
        case 3:
          sub_unsigned.push_back(get_wire(c.get_out_pin()));
          break;
        }
      }

      // u_type = (add_unsigned.size() > 0 || sub_unsigned.size() > 0);

      RTLIL::Wire *addu_result = nullptr;
      if(add_unsigned.size() > 1) {
        if(add_signed.size() + sub_unsigned.size() + sub_signed.size() > 0) {
          addu_result = module->addWire(next_id(g), size);
        } else {
          addu_result = cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)];
        }
        create_tree(g, add_unsigned, module, &RTLIL::Module::addAdd, false, addu_result);
      } else if(add_unsigned.size() == 1) {
        addu_result = add_unsigned[0];
      }

      RTLIL::Wire *adds_result = nullptr;
      if(add_signed.size() > 1) {
        if(add_unsigned.size() + sub_unsigned.size() + sub_signed.size() > 0) {
          adds_result = module->addWire(next_id(g), size);
        } else {
          adds_result = cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)];
        }
        create_tree(g, add_signed, module, &RTLIL::Module::addAdd, true, adds_result);
      } else if(add_signed.size() == 1) {
        adds_result = add_signed[0];
      }

      RTLIL::Wire *subu_result = nullptr;
      if(sub_unsigned.size() > 1) {
        if(add_signed.size() + add_unsigned.size() + sub_signed.size() > 0) {
          subu_result = module->addWire(next_id(g), size);
        } else {
          subu_result = cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)];
        }
        create_tree(g, sub_unsigned, module, &RTLIL::Module::addAdd, false, subu_result);
      } else if(sub_unsigned.size() == 1) {
        subu_result = sub_unsigned[0];
      }

      RTLIL::Wire *subs_result = nullptr;
      if(sub_signed.size() > 1) {
        if(add_unsigned.size() + sub_unsigned.size() + add_signed.size() > 0) {
          subs_result = module->addWire(next_id(g), size);
        } else {
          subs_result = cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)];
        }
        create_tree(g, sub_signed, module, &RTLIL::Module::addAdd, true, subs_result);
      } else if(sub_signed.size() == 1) {
        subs_result = sub_signed[0];
      }

      RTLIL::Wire *a_result = nullptr;
      if(addu_result != nullptr && adds_result != nullptr) {
        if(subu_result != nullptr || subs_result != nullptr) {
          a_result = module->addWire(next_id(g), size);
        } else {
          a_result = cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)];
        }
        module->addAdd(next_id(g), addu_result, adds_result, false, a_result);
      } else if(addu_result != nullptr) {
        a_result = addu_result;
      } else if(adds_result != nullptr) {
        a_result = adds_result;
      }

      RTLIL::Wire *s_result = nullptr;
      if(subu_result != nullptr && subs_result != nullptr) {
        s_result = module->addWire(next_id(g), size);
        module->addAdd(next_id(g), subu_result, subs_result, false, s_result);
      } else if(subu_result != nullptr) {
        s_result = subu_result;
      } else if(subs_result != nullptr) {
        s_result = subs_result;
      }

      if(a_result != nullptr && s_result != nullptr) {
        module->addSub(next_id(g), a_result, s_result, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], false);
      } else if(s_result != nullptr) {
        module->addSub(next_id(g), RTLIL::Const(0), s_result, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], false);
      } else {
        if(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->name != a_result->name)
          module->connect(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], RTLIL::SigSpec(a_result));
      }

      break;
    }
    case Mult_Op: {
      std::vector<RTLIL::Wire *> m_unsigned;
      std::vector<RTLIL::Wire *> m_signed;

      size = 0;
      for(const auto &c : g->inp_edges(nid)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          m_signed.push_back(get_wire(c.get_out_pin()));
          break;
        case 1:
          m_unsigned.push_back(get_wire(c.get_out_pin()));
          break;
        }
      }

      // u_type                 = (m_unsigned.size() > 0);
      RTLIL::Wire *mu_result = nullptr;
      RTLIL::Wire *ms_result = nullptr;

      if(m_unsigned.size() > 1) {
        if(m_signed.size() == 0) {
          mu_result = cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)];
        } else {
          mu_result = module->addWire(next_id(g), size);
        }
        create_tree(g, m_unsigned, module, &RTLIL::Module::addMul, false, mu_result);
      } else if(m_unsigned.size() == 1) {
        mu_result = m_unsigned[0];
      }

      if(m_signed.size() > 1) {
        if(m_unsigned.size() == 0) {
          ms_result = cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)];
        } else {
          ms_result = module->addWire(next_id(g), size);
        }
        create_tree(g, m_signed, module, &RTLIL::Module::addMul, false, ms_result);
      } else if(m_signed.size() == 1) {
        ms_result = m_signed[0];
      }

      if(mu_result != nullptr && ms_result != nullptr) {
        module->addMul(next_id(g), mu_result, ms_result, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], false);
      } else if(mu_result != nullptr) {
        if(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->name != mu_result->name)
          module->connect(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], RTLIL::SigSpec(mu_result));
      } else if(ms_result != nullptr) {
        if(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->name != ms_result->name)
          module->connect(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], RTLIL::SigSpec(ms_result));
      }

      break;
    }

    case Not_Op: {
      int n_inps = 0;

      for(const auto &c : g->inp_edges(nid)) {
        n_inps++;
        RTLIL::Wire *inWire = get_wire(c.get_out_pin());
        module->addNot(next_id(g), inWire, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]);
      }
      // if the assertion fails, check what does a not with multiple inputs mean
      assert(n_inps == 1);

      break;
    }
    case Join_Op: {
      std::vector<RTLIL::SigChunk> joined_wires;
      joined_wires.resize(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->width);
      uint32_t width      = 0;
      bool     has_inputs = false;
      for(const auto &c : g->inp_edges(nid)) {
        RTLIL::Wire *join                       = get_wire(c.get_out_pin());
        joined_wires[c.get_inp_pin().get_pid()] = RTLIL::SigChunk(join);
        width += join->width;
        has_inputs = true;
      }

      if(!has_inputs) {
        continue;
      }
      assert(cell_output_map.find(std::make_pair(nid, 0))!=cell_output_map.end());
      assert(width == (uint32_t)(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->width));
      module->connect(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], RTLIL::SigSpec(joined_wires));

      break;
    }
    case Pick_Op: {
      int          upper       = 0;
      int          lower       = 0;
      RTLIL::Wire *picked_wire = nullptr;

      for(const auto &c : g->inp_edges(nid)) {
        switch(c.get_inp_pin().get_pid()) {
        case 0:
          picked_wire = get_wire(c.get_out_pin());
          break;
        case 1:
          if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
            log_error("Internal Error: Pick range is not a constant.\n");
          lower = g->node_value_get(c.get_idx());
          break;
        default:
          assert(0); // pids > 1 not supported
        }
      }
      upper = lower + g->get_bits(node.get_driver_pin()) - 1;
      if(upper - lower + 1 > picked_wire->width) {
        upper = lower + picked_wire->width - 1;
        module->connect(RTLIL::SigSpec(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], lower, upper - lower + 1),
                        RTLIL::SigSpec(picked_wire, lower, upper - lower + 1));
      } else {
        assert(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->name != picked_wire->name);
        module->connect(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], RTLIL::SigSpec(picked_wire, lower, upper - lower + 1));
      }
      break;
    }
    case And_Op: {
      std::vector<RTLIL::Wire *> and_inps;
      uint32_t                   width = 0;
      for(const auto &c : g->inp_edges(nid)) {
        RTLIL::Wire *current = get_wire(c.get_out_pin());
        and_inps.push_back(current);
        width = (width < (uint32_t)current->width) ? current->width : width;
      }

      if(and_inps.size() == 1) {
        // reduce and: assign a = &b;
        assert(cell_output_map.find(std::make_pair(node.get_driver_pin(1).get_idx(), 1)) !=
               cell_output_map.end()); // single input and gate that is not used as a reduce and
        module->addReduceAnd(next_id(g), and_inps[0], cell_output_map[std::make_pair(node.get_driver_pin(1).get_idx(), 1)]);
      } else {
        assert(cell_output_map.find(std::make_pair(node.get_driver_pin(0).get_idx(), 0)) != cell_output_map.end());
        create_tree(g, and_inps, module, &RTLIL::Module::addAnd, false, cell_output_map[std::make_pair(node.get_driver_pin(0).get_idx(), 0)]);
      }
      break;
    }
    case Or_Op: {
      std::vector<RTLIL::Wire *> or_inps;
      for(const auto &c : g->inp_edges(nid)) {
        or_inps.push_back(get_wire(c.get_out_pin()));
      }

      if(or_inps.size() == 1) {
        // reduce or: assign a = |b;
        assert(cell_output_map.find(std::make_pair(node.get_driver_pin(1).get_idx(), 1)) !=
               cell_output_map.end()); // single input or gate that is not used as a reduce or
        module->addReduceOr(next_id(g), or_inps[0], cell_output_map[std::make_pair(node.get_driver_pin(1).get_idx(), 1)]);
      } else {
        assert(cell_output_map.find(std::make_pair(node.get_driver_pin(0).get_idx(), 0)) != cell_output_map.end());
        create_tree(g, or_inps, module, &RTLIL::Module::addOr, false, cell_output_map[std::make_pair(node.get_driver_pin(0).get_idx(), 0)]);
      }
      break;
    }

    case Xor_Op: {
      //log("adding xOr_Op\n");
      std::vector<RTLIL::Wire *> xor_inps;
      for(const auto &c : g->inp_edges(nid)) {
        xor_inps.push_back(get_wire(c.get_out_pin()));
      }

      if(xor_inps.size() == 1) {
        // reduce xor: assign a = ^b;
        assert(cell_output_map.find(std::make_pair(node.get_driver_pin(1).get_idx(), 1)) !=
               cell_output_map.end()); // single input xor gate that is not used as a reduce xor
        module->addReduceXor(next_id(g), xor_inps[0], cell_output_map[std::make_pair(node.get_driver_pin(1).get_idx(), 1)]);
      } else {
        create_tree(g, xor_inps, module, &RTLIL::Module::addXor, false, cell_output_map[std::make_pair(node.get_driver_pin(0).get_idx(), 0)]);
      }
      break;
    }
    case Latch_Op: {
      RTLIL::Wire *dWire    = nullptr;
      RTLIL::Wire *enWire   = nullptr;
      bool         polarity = true;

      for(const auto &c : g->inp_edges(nid)) {
        switch(c.get_inp_pin().get_pid()) {
        case 0: dWire  = get_wire(c.get_out_pin()); break;
        case 1: enWire = get_wire(c.get_out_pin()); break;
        case 2: {
          auto const_node = g->get_node(c.get_out_pin());
          if(const_node.get_type().op != U32Const_Op)
            log_error("Internal Error: polarity is not a constant.\n");
          polarity = g->node_value_get(const_node.get_nid()) != 0? true:false;
        } break;
        default:
          log_error("DumpYosys: unrecognized wire connection pid=%d\n", c.get_out_pin().get_pid());
        }
      }
      if(dWire)
        log("adding Latch_Op width = %d\n", dWire->width);
      if(enWire)
        log("adding Latch_Op enable width = %d\n", dWire->width);
      // last argument is polarity
      module->addDlatch(next_id(g), enWire, dWire, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], polarity);
    } break;
    case SFlop_Op:
    case AFlop_Op: {
      RTLIL::Wire *enWire  = nullptr;
      RTLIL::Wire *dWire   = nullptr;
      RTLIL::Wire *clkWire = nullptr;
      RTLIL::Wire *rstWire = nullptr;
      RTLIL::Wire *rstVal  = nullptr;
      bool polarity = true;

      for(const auto &c : g->inp_edges(nid)) {
        switch(c.get_inp_pin().get_pid()) {
        case 0: clkWire = get_wire(c.get_out_pin()); break;
        case 1: dWire   = get_wire(c.get_out_pin()); break;
        case 2: enWire  = get_wire(c.get_out_pin()); break;
        case 3: rstWire = get_wire(c.get_out_pin()); break; // clr signal in yosys
        case 4: rstVal  = get_wire(c.get_out_pin()); break; // set signal in yosys
        case 5: {
          auto const_node = g->get_node(c.get_out_pin());
          if(const_node.get_type().op != U32Const_Op)
            log_error("Internal Error: polarity is not a constant.\n");
          polarity = g->node_value_get(const_node.get_nid()) != 0? true:false;
        }
        default:
          log("[WARNING] DumpYosys: unrecognized wire connection pid=%d\n", c.get_out_pin().get_pid());
        }
      }
      if(dWire)
        log("adding sflop_Op width = %d\n", dWire->width);
      // last argument is polarity
      switch(g->get_node(nid).get_type().op) {
      case SFlop_Op:
        if(enWire == nullptr && clkWire != nullptr && dWire != nullptr && rstWire == nullptr && rstVal == nullptr) {
          module->addDff(next_id(g), clkWire, dWire, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], polarity);
        }else if(enWire == nullptr && clkWire != nullptr && dWire != nullptr && rstWire != nullptr && rstVal != nullptr) {
          module->addDffsr(next_id(g), clkWire, rstVal, rstWire, dWire, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], polarity, polarity, polarity);
        } else {
          log_error("Enable and Reset not supported on Flops yet!\n");
        }
        break;
      case AFlop_Op:
        if(enWire == nullptr && clkWire != nullptr && dWire != nullptr && rstWire != nullptr && rstVal == nullptr) {
          //      addAdff(name,      sig_clk,sig_arst, sig_d, sig_q,                                   arst_value, clk_polarity,
          //      arst_polarity);
          module->addAdff(next_id(g), clkWire, rstWire, dWire, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)],
                          RTLIL::Const(0, dWire->width), true, true);
        } else {
          log_error("Enable not supported on AFlops yet, RST required on AFlops!\n");
        }
        break;
      default:
        assert(false); // internal error
      }
      break;
    }
    case Div_Op:
    case Mod_Op:
    case LessThan_Op:
    case GreaterThan_Op:
    case LessEqualThan_Op:
    case GreaterEqualThan_Op: {
      RTLIL::Wire *lhs  = nullptr;
      RTLIL::Wire *rhs  = nullptr;
      bool         sign = false;

      for(const auto &c : g->inp_edges(nid)) {
        switch(c.get_inp_pin().get_pid()) {
        case 0:
          if(lhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          lhs  = get_wire(c.get_out_pin());
          sign = true;
          break;
        case 1:
          if(lhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          lhs = get_wire(c.get_out_pin());
          break;
        case 2:
          if(rhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          rhs  = get_wire(c.get_out_pin());
          sign = true;
          break;
        case 3:
          if(rhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          rhs = get_wire(c.get_out_pin());
          break;
        }
      }

      if(lhs == nullptr || rhs == nullptr)
        log_error("Internal error: found no connections to LT pid.\n");

      switch(g->get_node(nid).get_type().op) {
      case LessThan_Op:
        module->addLt(next_id(g), lhs, rhs, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], sign);
        break;
      case LessEqualThan_Op:
        module->addLe(next_id(g), lhs, rhs, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], sign);
        break;
      case GreaterThan_Op:
        module->addGt(next_id(g), lhs, rhs, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], sign);
        break;
      case GreaterEqualThan_Op:
        module->addGe(next_id(g), lhs, rhs, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], sign);
        break;
      case Div_Op:
        module->addDiv(next_id(g), lhs, rhs, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], sign);
        break;
      case Mod_Op:
        module->addMod(next_id(g), lhs, rhs, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], sign);
        break;
      default:
        ::Pass::error("lgyosys_dump: internal error!");
      }
      break;
    }
    case ShiftLeft_Op: {
      //log("adding SHL_Op\n");
      RTLIL::Wire *  shifted_wire = nullptr;
      RTLIL::SigSpec shift_amount;

      for(const auto &c : g->inp_edges(nid)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          if(shifted_wire != nullptr)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          shifted_wire = get_wire(c.get_out_pin());
          break;
        case 1:
          if(shift_amount.size() != 0)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          if(g->get_node(c.get_idx()).get_type().op == U32Const_Op)
            shift_amount = RTLIL::Const(g->node_value_get(c.get_idx()));
          else
            shift_amount = get_wire(c.get_out_pin());
          break;
        }
      }
      if(shifted_wire == nullptr)
        log_error("Internal Error: did not find a wire to be shifted.\n");
      if(shift_amount.size() == 0)
        log_error("Internal Error: did not find a wire to be shifted.\n");
      module->addShl(next_id(g), shifted_wire, shift_amount, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], false);
      break;
    }
    case ShiftRight_Op: {
      //log("adding SHR_Op\n");
      RTLIL::Wire *  shifted_wire = nullptr;
      RTLIL::SigSpec shift_amount;

      bool sign   = false;
      bool b_sign = false;
      bool a_sign = false;

      for(const auto &c : g->inp_edges(nid)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          if(shifted_wire != nullptr)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          shifted_wire = get_wire(c.get_out_pin());
          break;
        case 1:
          if(shift_amount.size() != 0)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          if(g->get_node(c.get_idx()).get_type().op == U32Const_Op)
            shift_amount = RTLIL::Const(g->node_value_get(c.get_idx()));
          else
            shift_amount = get_wire(c.get_out_pin());
          break;
        case 2:
          if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
            log_error("Internal Error: Shift sign is not a constant.\n");
          sign   = (g->node_value_get(c.get_idx()) % 2 == 1);
          b_sign = (g->node_value_get(c.get_idx()) == 2);
          a_sign = (g->node_value_get(c.get_idx()) > 2);
          break;
        }
      }
      if(shifted_wire == nullptr)
        log_error("Internal Error: did not find a wire to be shifted.\n");
      if(shift_amount.size() == 0)
        log_error("Internal Error: did not find a wire to be shifted.\n");

      if(sign)
        module->addSshr(next_id(g), shifted_wire, shift_amount, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], a_sign);
      else if(b_sign)
        module->addShiftx(next_id(g), shifted_wire, shift_amount, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], a_sign);
      else
        module->addShr(next_id(g), shifted_wire, shift_amount, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], a_sign);
      break;
    }
    case Equals_Op: {
      std::vector<RTLIL::Wire *> e_unsigned;
      std::vector<RTLIL::Wire *> e_signed;

      size = 0;
      for(const auto &c : g->inp_edges(nid)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          e_signed.push_back(get_wire(c.get_out_pin()));
          break;
        case 1:
          e_unsigned.push_back(get_wire(c.get_out_pin()));
          break;
        }
      }

      // u_type                 = (e_unsigned.size() > 0);
      RTLIL::Wire *eu_result = nullptr;
      RTLIL::Wire *es_result = nullptr;

      if(e_unsigned.size() > 1) {
        if(e_signed.size() == 0) {
          eu_result = cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)];
        } else {
          eu_result = module->addWire(next_id(g), size);
        }
        create_tree(g, e_unsigned, module, &RTLIL::Module::addEq, false, eu_result);
      } else if(e_unsigned.size() == 1) {
        eu_result = e_unsigned[0];
      }

      if(e_signed.size() > 1) {
        if(e_unsigned.size() == 0) {
          es_result = cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)];
        } else {
          es_result = module->addWire(next_id(g), size);
        }
        create_tree(g, e_signed, module, &RTLIL::Module::addEq, false, es_result);
      } else if(e_signed.size() == 1) {
        es_result = e_signed[0];
      }

      if(eu_result != nullptr && es_result != nullptr) {
        module->addEq(next_id(g), eu_result, es_result, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], false);
      } else if(eu_result != nullptr) {
        if(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->name != eu_result->name)
          module->connect(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], RTLIL::SigSpec(eu_result));
      } else if(es_result != nullptr) {
        if(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->name != es_result->name)
          module->connect(cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)], RTLIL::SigSpec(es_result));
      }
      break;
    }
    case Mux_Op: {
      //log("adding MUX_Op\n");

      RTLIL::Wire *aport = nullptr;
      RTLIL::Wire *bport = nullptr;
      RTLIL::Wire *sel   = nullptr;

      for(const auto &c : g->inp_edges(nid)) {
        switch(c.get_inp_pin().get_pid()) {
        case 0:
          if(sel != nullptr)
            log_error("Internal Error: multiple wires assigned to same mux port\n");
          sel = get_wire(c.get_out_pin());
          break;
        case 1:
          if(aport != nullptr)
            log_error("Internal Error: multiple wires assigned to same mux port\n");
          aport = get_wire(c.get_out_pin());
          break;
        case 2:
          if(bport != nullptr)
            log_error("Internal Error: multiple wires assigned to same mux port\n");
          bport = get_wire(c.get_out_pin());
          break;
        }
      }
      if(aport == nullptr || bport == nullptr || sel == nullptr)
        log_error("Internal Error: did not find a wire to be shifted.\n");

      if(aport->width != cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->width ||
         bport->width != cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->width) {
        log("ports size don't match a=%d, b=%d, y=%d\n", aport->width, bport->width,
            cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->width);
      }
      assert(aport->width <= (cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->width));
      assert(bport->width <= (cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]->width));

      module->addMux(next_id(g), aport, bport, sel, cell_output_map[std::make_pair(node.get_driver_pin().get_idx(), 0)]);
      break;
    }
    case Memory_Op: {
      //log("adding Mem_Op\n");
      create_memory(g, module, nid);
      break;
    }
    case SubGraph_Op: {
      create_subgraph(g, module, nid);
      break;
    }
    case TechMap_Op: {

      const Tech_cell *tcell = g->get_tlibrary().get_const_cell(g->tmap_id_get(nid));

      RTLIL::IdString instance_name("\\tmp");
      if(g->get_instance_name_id(nid) == 0 || g->get_node_instancename(nid) == "") {
        instance_name = next_id(g);
#ifndef NDEBUG
        fmt::print("inou_yosys got empty inst_name for cell type {}\n", tcell->get_name());
#endif
      } else {
        instance_name = RTLIL::IdString(absl::StrCat("\\",g->get_node_instancename(nid)));
      }

      std::string name;
      if (tcell->get_name()[0] == '$' || tcell->get_name()[0] == '\\')
        name = absl::StrCat(tcell->get_name());
      else
        name = absl::StrCat("\\",tcell->get_name());

      RTLIL::Cell *new_cell = module->addCell(instance_name, name);
      for(const auto &c : g->inp_edges(nid)) {
        if(c.get_inp_pin().get_pid() >= tcell->n_inps())
          log_error("I could not find a port associated with inp pin %hu\n", c.get_inp_pin().get_pid());

        auto port = tcell->get_input_name(c.get_inp_pin().get_pid());

        RTLIL::Wire *input = get_wire(c.get_out_pin());
        new_cell->setPort(absl::StrCat("\\", port).c_str(), input);
      }
      for(const auto &c : g->out_edges(nid)) {
        if(c.get_out_pin().get_pid() >= tcell->n_outs())
          log_error("I could not find a port associated with out pin %hu\n", c.get_out_pin().get_pid());

        auto port = tcell->get_output_name(c.get_out_pin().get_pid());
        RTLIL::Wire *output = get_wire(c.get_out_pin());
        assert(output);
        new_cell->setPort(absl::StrCat("\\", port).c_str(), output);
      }
      break;
    }
    case BlackBox_Op: {
      std::string_view celltype;
      std::string_view instance_name;

      for(const auto &c : g->inp_edges(nid)) {
        if(c.get_inp_pin().get_pid() == LGRAPH_BBOP_TYPE) {
          // celltype
          if(g->get_node(c.get_idx()).get_type().op != StrConst_Op)
            log_error("Internal Error: BB type is not a string.\n");
          celltype = g->node_const_value_get(c.get_idx());
        } else if(c.get_inp_pin().get_pid() == LGRAPH_BBOP_NAME) {
          // instance name
          if(g->get_node(c.get_idx()).get_type().op != StrConst_Op)
            log_error("Internal Error: BB name is not a string.\n");
          instance_name = g->node_const_value_get(c.get_idx());
        } else if(c.get_inp_pin().get_pid() < LGRAPH_BBOP_OFFSET) {
          log_error("Unrecognized blackbox option, pid %hu\n", c.get_out_pin().get_pid());
        }
      }

      if (celltype.empty() || instance_name.empty()) {
        log_error("could not find instance name or celltype for blackbox\n");
      }

      RTLIL::Cell *            cell = module->addCell(absl::StrCat("\\",instance_name), RTLIL::IdString(absl::StrCat("\\", celltype)));
#ifndef NDEBUG
      int current_port = 0, def = 0;
#endif
      bool is_param = false;
      for(const auto &c : g->inp_edges(nid)) {
        if(c.get_inp_pin().get_pid() < LGRAPH_BBOP_OFFSET)
          continue;

        if(LGRAPH_BBOP_ISIPARAM(c.get_inp_pin().get_pid())) {
          if(g->get_node(c.get_idx()).get_type().op != U32Const_Op)
            log_error("Internal Error: Could not define if input is parameter.\n");
          is_param = g->node_value_get(c.get_idx()) == 1;
#ifndef NDEBUG
          assert(def == 0);
          def++;
          assert(current_port == 0);
          current_port = LGRAPH_BBOP_PORT_N(c.get_inp_pin().get_pid());
#endif
        } else if(LGRAPH_BBOP_ISICONNECT(c.get_inp_pin().get_pid())) {
          auto current_name = g->get_node_wirename(c.get_inp_pin());
          if(is_param) {
            if(g->get_node(c.get_idx()).get_type().op == U32Const_Op) {
              cell->setParam(absl::StrCat("\\",current_name), RTLIL::Const(g->node_value_get(c.get_idx())));
            } else if(g->get_node(c.get_idx()).get_type().op == StrConst_Op) {
              cell->setParam(absl::StrCat("\\",current_name), RTLIL::Const(std::string(g->node_const_value_get(c.get_idx()))));
            } else {
              assert(false); // parameter is not a constant
            }
          } else {
            if(g->get_node(c.get_idx()).get_type().op == U32Const_Op) {
              cell->setPort(absl::StrCat("\\",current_name), RTLIL::Const(g->node_value_get(c.get_idx())));
            } else if(g->get_node(c.get_idx()).get_type().op == StrConst_Op) {
              cell->setPort(absl::StrCat("\\",current_name), RTLIL::Const(std::string(g->node_const_value_get(c.get_idx()))));
            } else {
              cell->setPort(absl::StrCat("\\",current_name), get_wire(c.get_out_pin()));
            }
          }
#ifndef NDEBUG
          assert(def == 2);
          assert(current_port == LGRAPH_BBOP_PORT_N(c.get_inp_pin().get_pid()));
          current_port = 0;
          def          = 0;
#endif
        }
      }

#ifndef NDEBUG
      current_port = 0, def = 0;
#endif
      for(const auto &c : g->inp_edges(nid)) {
        if(LGRAPH_BBOP_ISICONNECT(c.get_inp_pin().get_pid())) {
          // FIXME: nicer API last_name = c.get_inp_pin().name();
          auto wname = absl::StrCat("\\",g->get_node_wirename(c.get_inp_pin()));
          cell->setPort(wname, cell_output_map[std::make_pair(c.get_out_pin().get_idx(), c.get_out_pin().get_pid())]);
        }
      }
      break;
    }

    default:
      log_error("Operation %s (node = %ld) not supported, please add to lgyosys_dump.\n", g->get_node(nid).get_type().get_name().c_str(), nid.value);
      break;
    }
  }
}
