//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <math.h>

#include "lgedgeiter.hpp"
#include "lgyosys_dump.hpp"

RTLIL::Wire *Lgyosys_dump::get_wire(const Node_pin &pin) {

  auto inp_it = input_map.find(pin.get_compact());
  if(inp_it != input_map.end()) {
    return inp_it->second;
  }

  auto out_it = output_map.find(pin.get_compact());
  if(out_it != output_map.end()) {
    return out_it->second;
  }

  auto cell_it = cell_output_map.find(pin.get_compact());
  if(cell_it != cell_output_map.end()) {
    return cell_it->second;
  }

  fmt::print("trying to get wire for pin {}\n", pin.get_name());
  assert(false);
}

RTLIL::Wire *Lgyosys_dump::add_wire(RTLIL::Module *module, const Node_pin &pin) {
  assert(pin.is_driver());
  if (pin.has_name()) {
    auto name = absl::StrCat("\\",pin.get_name());
    //printf("add wire [%s]\n", name.c_str());
    return module->addWire(name, pin.get_bits());
  }else{
    return module->addWire(next_id(pin.get_class_lgraph()), pin.get_bits());
  }
}

RTLIL::Wire *Lgyosys_dump::create_tree(LGraph *g, std::vector<RTLIL::Wire *> &wires, RTLIL::Module *mod,
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

    auto name = next_id(g);
    (mod->*add_fnc)(name, wires[current], wires[current + 1], aWire, sign, "");
    next_level.push_back(aWire);
  }
  if(wires.size() % 2 == 1)
    next_level.push_back(wires[wires.size() - 1]);

  return create_tree(g, next_level, mod, add_fnc, sign, result_wire);
}

RTLIL::Wire *Lgyosys_dump::create_io_wire(const Node_pin &pin, RTLIL::Module *module, Port_ID pos) {

  assert(pin.has_name()); // IO must have name
  RTLIL::IdString name = absl::StrCat("\\", pin.get_name());

  RTLIL::Wire *new_wire  = module->addWire(name, pin.get_bits());
  new_wire->start_offset = pin.get_offset();

  module->ports.push_back(name);
  assert(pos == module->ports.size());
  new_wire->port_id = module->ports.size();

  return new_wire;
}

void Lgyosys_dump::create_blackbox(const Sub_node &sub, RTLIL::Design *design) {
  if(created_sub.find(sub.get_name()) != created_sub.end())
    return;
  created_sub.insert(sub.get_name());

  RTLIL::Module *mod            = new RTLIL::Module;
  mod->name                     = absl::StrCat("\\", sub.get_name());
  mod->attributes["\\blackbox"] = RTLIL::Const(1);

  design->add(mod);

  int port_id=0;
  for(const auto &io_pin:sub.get_io_pins()) {
    std::string name = absl::StrCat("\\", io_pin.name);
    RTLIL::Wire *wire = mod->addWire(name); // , pin.get_bits());
    wire->port_id     = port_id++;
    if(io_pin.is_input()) {
      wire->port_input  = false;
      wire->port_output = true;
    }else{
      wire->port_input  = true;
      wire->port_output = false;
    }
  }

  mod->fixup_ports();
}

void Lgyosys_dump::create_memory(LGraph *g, RTLIL::Module *module, Node &node) {
  assert(node.get_type().op == Memory_Op);

  RTLIL::Cell *memory = module->addCell(absl::StrCat("\\", node.create_name()), RTLIL::IdString("$mem"));

  RTLIL::SigSpec wr_addr, wr_data, wr_en, rd_addr, rd_data;
  int nrd_ports = 0;
  RTLIL::SigSpec rd_en;
  RTLIL::Wire   *clk        = nullptr;
  bool           rd_clk     = false;
  bool           wr_clk     = false;
  RTLIL::State   rd_posedge = RTLIL::State::Sx;
  RTLIL::State   wr_posedge = RTLIL::State::Sx;
  RTLIL::State   transp     = RTLIL::State::Sx;

  for(const auto &e:node.inp_edges()) {
    Port_ID input_pin = e.sink.get_pid();
    auto driver_node  = e.driver.get_node();

    if(input_pin == LGRAPH_MEMOP_CLK) {
      if(clk != nullptr)
        log_error("Internal Error: multiple wires assigned to same mem port\n");
      clk = get_wire(e.driver);
      assert(clk);

    } else if(input_pin == LGRAPH_MEMOP_SIZE) {
      memory->setParam("\\SIZE", RTLIL::Const(driver_node.get_type_const_value()));

    } else if(input_pin == LGRAPH_MEMOP_OFFSET) {
      memory->setParam("\\OFFSET", RTLIL::Const(driver_node.get_type_const_value()));

    } else if(input_pin == LGRAPH_MEMOP_ABITS) {
      memory->setParam("\\ABITS", RTLIL::Const(driver_node.get_type_const_value()));

    } else if(input_pin == LGRAPH_MEMOP_WRPORT) {
      memory->setParam("\\WR_PORTS", RTLIL::Const(driver_node.get_type_const_value()));

    } else if(input_pin == LGRAPH_MEMOP_RDPORT) {
      assert(nrd_ports==0); // Do not double set

      nrd_ports = driver_node.get_type_const_value();
      memory->setParam("\\RD_PORTS", RTLIL::Const(nrd_ports));

    } else if(input_pin == LGRAPH_MEMOP_RDTRAN) {
      transp = driver_node.get_type_const_value() ? RTLIL::State::S1 : RTLIL::State::S0;

    } else if(input_pin == LGRAPH_MEMOP_RDCLKPOL) {
      rd_posedge = (driver_node.get_type_const_value() == 0) ? RTLIL::State::S1 : RTLIL::State::S0;
      rd_clk = true;

    } else if(input_pin == LGRAPH_MEMOP_WRCLKPOL) {
      wr_posedge = (driver_node.get_type_const_value() == 0) ? RTLIL::State::S1 : RTLIL::State::S0;
      wr_clk = true;

    } else if(LGRAPH_MEMOP_ISWRADDR(input_pin)) {
      wr_addr.append(RTLIL::SigSpec(get_wire(e.driver)));

    } else if(LGRAPH_MEMOP_ISWRDATA(input_pin)) {
      wr_data.append(RTLIL::SigSpec(get_wire(e.driver)));

    } else if(LGRAPH_MEMOP_ISWREN(input_pin)) {
      wr_en.append(RTLIL::SigSpec(get_wire(e.driver)));
#if 0
      RTLIL::Wire *en = get_wire(e.driver);
      assert(en->width == 1); // yosys requires one wr_en per bit
      wr_en.append(RTLIL::SigSpec(RTLIL::SigBit(en), e.get_bits()));
#endif

    } else if(LGRAPH_MEMOP_ISRDADDR(input_pin)) {
      rd_addr.append(RTLIL::SigSpec(get_wire(e.driver)));

    } else if(LGRAPH_MEMOP_ISRDEN(input_pin)) {
      rd_en.append(RTLIL::SigSpec(get_wire(e.driver)));
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

  memory->setParam("\\MEMID", RTLIL::Const(std::string(node.get_name())));
  memory->setParam("\\WIDTH", node.get_driver_pin(0).get_bits());

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

  memory->setPort("\\RD_DATA", RTLIL::SigSpec(mem_output_map[node.get_compact()]));
  memory->setPort("\\RD_ADDR", rd_addr);

  assert(nrd_ports == rd_en.size());
  memory->setPort("\\RD_EN", rd_en);
}

void Lgyosys_dump::create_subgraph(LGraph *g, RTLIL::Module *module, Node &node) {
  assert(node.get_type().op == SubGraph_Op);

  const auto &sub = node.get_type_sub_node();

  create_blackbox(sub, module->design);

  RTLIL::Cell *new_cell = module->addCell(absl::StrCat("\\",node.create_name()), absl::StrCat("\\", sub.get_name()));

  fmt::print("inou_yosys instance_name:{}, subgraph->get_name():{}\n", node.get_name(), sub.get_name());
  for(const auto &e:node.inp_edges()) {
    auto  port_name = e.sink.get_type_sub_io_name();
    fmt::print("input:{}\n", port_name);
    RTLIL::Wire *input = get_wire(e.driver);
    new_cell->setPort(absl::StrCat("\\", port_name).c_str(), input);
  }
  for(const auto &dpin:node.out_connected_pins()) {
    auto  port_name = dpin.get_type_sub_io_name();
    fmt::print("output:{}\n", port_name);
    RTLIL::Wire *output = get_wire(dpin);
    new_cell->setPort(absl::StrCat("\\", port_name).c_str(), output);
  }
}

void Lgyosys_dump::create_subgraph_outputs(LGraph *g, RTLIL::Module *module, Node &node) {
  assert(node.get_type().op == SubGraph_Op);

  for(auto &dpin:node.out_connected_pins()) {
    cell_output_map[dpin.get_compact()] = add_wire(module, dpin);
  }
}

void Lgyosys_dump::create_wires(LGraph *g, RTLIL::Module *module) {
  // first create all the output wires

  uint32_t port_id = 0;
  g->each_sorted_graph_io([&port_id,module,this](const Node_pin &pin, Port_ID pos) {
    port_id++;
    assert(port_id == pos);
    if(pin.is_graph_output()) {
      output_map[pin.get_compact()] = create_io_wire(pin, module, pos);
      output_map[pin.get_compact()]->port_input  = false;
      output_map[pin.get_compact()]->port_output = true;
    }else{
      assert(pin.is_graph_input());
      input_map[pin.get_compact()] = create_io_wire(pin, module, pos);
      input_map[pin.get_compact()]->port_input  = true;
      input_map[pin.get_compact()]->port_output = false;
    }
  });

  for(auto node : g->fast()) {

    if(node.get_type().op == GraphIO_Op)
      continue; // handled before with each_output/each_input

    if(node.get_type().op == U32Const_Op) {
      auto dpin = node.get_driver_pin();
      RTLIL::Wire *new_wire = add_wire(module, dpin);

      // constants treated as inputs
      module->connect(new_wire, RTLIL::SigSpec(node.get_type_const_value(), dpin.get_bits()));
      input_map[dpin.get_compact()] = new_wire;
      continue;

    } else if(node.get_type().op == StrConst_Op) {
      auto const_val = node.get_type_const_sview();
      RTLIL::Wire *new_wire  = module->addWire(absl::StrCat("\\",node.get_driver_pin().create_name()), const_val.size()); // FIXME: This assumes that const are in base 2. OK always?

      // constants treated as inputs
      module->connect(new_wire, RTLIL::SigSpec(RTLIL::Const::from_string(std::string(const_val))));
      input_map[node.get_driver_pin().get_compact()] = new_wire;
      continue;

    } else if(node.get_type().op == SubGraph_Op) {
      create_subgraph_outputs(g, module, node);
      continue;

    } else if(node.get_type().op == Memory_Op) {

      for(auto &dpin:node.out_connected_pins()) {
        RTLIL::Wire *new_wire = add_wire(module, dpin);

        cell_output_map[dpin.get_compact()] = new_wire;
        mem_output_map[node.get_compact()].push_back(RTLIL::SigChunk(new_wire));
      }
      continue;
    } else if(node.get_type().op == And_Op || node.get_type().op == Or_Op || node.get_type().op == Xor_Op) {
      // This differentiates between binary and unary Ands, Ors, Xors
      int count = 0;
      for(const auto &e:node.out_edges()) {
        if(cell_output_map.find(e.driver.get_compact()) == cell_output_map.end()) {
          ++count;
          assert(count == 1);
          // bitwise operators have the regular size

          RTLIL::Wire *new_wire = add_wire(module, e.driver);
          cell_output_map[e.driver.get_compact()] = new_wire;
        }
      }
      continue;
    }

    auto dpin = node.get_driver_pin();
    RTLIL::Wire *result = add_wire(module, dpin);
    cell_output_map[dpin.get_compact()] = result;
  }
}

void Lgyosys_dump::to_yosys(LGraph *g) {
  auto name = g->get_name();

  RTLIL::Module *module = design->addModule(absl::StrCat("\\",name));

  created_sub.clear();
  input_map.clear();
  output_map.clear();
  cell_output_map.clear();

  create_wires(g, module);

  g->each_graph_output([this,module](const Node_pin &pin) {
    assert(pin.is_graph_output());

    RTLIL::SigSpec lhs = RTLIL::SigSpec(output_map[pin.get_compact()]);

    for(const auto &e:pin.get_node().inp_edges()) {
      if (e.sink.get_pid() != pin.get_pid())
        continue;

      RTLIL::SigSpec rhs = RTLIL::SigSpec(get_wire(e.driver));
      assert(rhs != lhs);
      module->connect(lhs, rhs);
    }
  });

  // now create nodes and make connections
  for(auto node : g->fast()) {

    auto op = node.get_type().op;
    if (op == GraphIO_Op)
      continue; // outputs already handled, inputs are not used

    if(op != Memory_Op && op != SubGraph_Op && !node.has_outputs())
      continue;

    uint16_t size = 0;

    switch(op) {
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
      for(const auto &e:node.inp_edges()) {
        size = (e.get_bits() > size) ? e.get_bits() : size;

        switch(e.sink.get_pid()) {
        case 0:
          add_signed.push_back(get_wire(e.driver));
          break;
        case 1:
          add_unsigned.push_back(get_wire(e.driver));
          break;
        case 2:
          sub_signed.push_back(get_wire(e.driver));
          break;
        case 3:
          sub_unsigned.push_back(get_wire(e.driver));
          break;
        }
      }

      // u_type = (add_unsigned.size() > 0 || sub_unsigned.size() > 0);

      RTLIL::Wire *addu_result = nullptr;
      if(add_unsigned.size() > 1) {
        if(add_signed.size() + sub_unsigned.size() + sub_signed.size() > 0) {
          addu_result = module->addWire(next_id(g), size);
        } else {
          addu_result = cell_output_map[node.get_driver_pin().get_compact()];
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
          adds_result = cell_output_map[node.get_driver_pin().get_compact()];
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
          subu_result = cell_output_map[node.get_driver_pin().get_compact()];
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
          subs_result = cell_output_map[node.get_driver_pin().get_compact()];
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
          a_result = cell_output_map[node.get_driver_pin().get_compact()];
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
        module->addSub(next_id(g), a_result, s_result, cell_output_map[node.get_driver_pin().get_compact()], false);
      } else if(s_result != nullptr) {
        module->addSub(next_id(g), RTLIL::Const(0), s_result, cell_output_map[node.get_driver_pin().get_compact()], false);
      } else {
        if(cell_output_map[node.get_driver_pin().get_compact()]->name != a_result->name)
          module->connect(cell_output_map[node.get_driver_pin().get_compact()], RTLIL::SigSpec(a_result));
      }

      break;
    }
    case Mult_Op: {
      std::vector<RTLIL::Wire *> m_unsigned;
      std::vector<RTLIL::Wire *> m_signed;

      size = 0;
      for(const auto &e:node.inp_edges()) {
        size = (e.get_bits() > size) ? e.get_bits() : size;

        switch(e.sink.get_pid()) {
        case 0:
          m_signed.push_back(get_wire(e.driver));
          break;
        case 1:
          m_unsigned.push_back(get_wire(e.driver));
          break;
        }
      }

      // u_type                 = (m_unsigned.size() > 0);
      RTLIL::Wire *mu_result = nullptr;
      RTLIL::Wire *ms_result = nullptr;

      if(m_unsigned.size() > 1) {
        if(m_signed.size() == 0) {
          mu_result = cell_output_map[node.get_driver_pin().get_compact()];
        } else {
          mu_result = module->addWire(next_id(g), size);
        }
        create_tree(g, m_unsigned, module, &RTLIL::Module::addMul, false, mu_result);
      } else if(m_unsigned.size() == 1) {
        mu_result = m_unsigned[0];
      }

      if(m_signed.size() > 1) {
        if(m_unsigned.size() == 0) {
          ms_result = cell_output_map[node.get_driver_pin().get_compact()];
        } else {
          ms_result = module->addWire(next_id(g), size);
        }
        create_tree(g, m_signed, module, &RTLIL::Module::addMul, false, ms_result);
      } else if(m_signed.size() == 1) {
        ms_result = m_signed[0];
      }

      if(mu_result != nullptr && ms_result != nullptr) {
        module->addMul(next_id(g), mu_result, ms_result, cell_output_map[node.get_driver_pin().get_compact()], false);
      } else if(mu_result != nullptr) {
        if(cell_output_map[node.get_driver_pin().get_compact()]->name != mu_result->name)
          module->connect(cell_output_map[node.get_driver_pin().get_compact()], RTLIL::SigSpec(mu_result));
      } else if(ms_result != nullptr) {
        if(cell_output_map[node.get_driver_pin().get_compact()]->name != ms_result->name)
          module->connect(cell_output_map[node.get_driver_pin().get_compact()], RTLIL::SigSpec(ms_result));
      }

      break;
    }

    case Not_Op: {
      int n_inps = 0;

      for(const auto &e:node.inp_edges()) {
        n_inps++;
        RTLIL::Wire *inWire = get_wire(e.driver);
        module->addNot(next_id(g), inWire, cell_output_map[node.get_driver_pin().get_compact()]);
      }
      // if the assertion fails, check what does a not with multiple inputs mean
      assert(n_inps == 1);

      break;
    }
    case Join_Op: {
      std::vector<RTLIL::SigChunk> joined_wires;
      joined_wires.resize(cell_output_map[node.get_driver_pin().get_compact()]->width);
      uint32_t width      = 0;
      bool     has_inputs = false;
      for(const auto &e:node.inp_edges()) {
        RTLIL::Wire *join              = get_wire(e.driver);
        joined_wires[e.sink.get_pid()] = RTLIL::SigChunk(join);
        width += join->width;
        has_inputs = true;
      }

      if(!has_inputs) {
        continue;
      }
      assert(cell_output_map.find(node.get_driver_pin().get_compact())!=cell_output_map.end());
      assert(width == (uint32_t)(cell_output_map[node.get_driver_pin().get_compact()]->width));
      module->connect(cell_output_map[node.get_driver_pin().get_compact()], RTLIL::SigSpec(joined_wires));

      break;
    }
    case Pick_Op: {
      int          upper       = 0;
      int          lower       = 0;
      RTLIL::Wire *picked_wire = nullptr;

      for(const auto &e:node.inp_edges()) {
        switch(e.sink.get_pid()) {
        case 0:
          picked_wire = get_wire(e.driver);
          break;
        case 1:
          if(e.driver.get_node().get_type().op != U32Const_Op)
            log_error("Internal Error: Pick range is not a constant.\n");
          lower = e.driver.get_node().get_type_const_value();
          break;
        default:
          assert(0); // pids > 1 not supported
        }
      }
      assert(node.get_driver_pin().get_bits());
      upper = lower + node.get_driver_pin().get_bits() - 1;
      if(upper - lower + 1 > picked_wire->width) {
        upper = lower + picked_wire->width - 1;
        module->connect(RTLIL::SigSpec(cell_output_map[node.get_driver_pin().get_compact()], lower, upper - lower + 1),
                        RTLIL::SigSpec(picked_wire, lower, upper - lower + 1));
      } else {
        assert(cell_output_map[node.get_driver_pin().get_compact()]->name != picked_wire->name);
        module->connect(cell_output_map[node.get_driver_pin().get_compact()], RTLIL::SigSpec(picked_wire, lower, upper - lower + 1));
      }
      break;
    }
    case And_Op:
    case Or_Op:
    case Xor_Op: {
      std::vector<RTLIL::Wire *> inps;

      for(const auto &e:node.inp_edges()) {
        inps.push_back(get_wire(e.driver));
      }

      Port_ID pid;
      if (inps.size() == 1) {
        pid = 1;
      } else {
        if(cell_output_map.find(node.get_driver_pin(0).get_compact()) != cell_output_map.end())
          pid = 0;
        else if(cell_output_map.find(node.get_driver_pin(1).get_compact()) != cell_output_map.end())
          pid = 1;
        else
          assert(false);
      }

      assert(cell_output_map.find(node.get_driver_pin(pid).get_compact()) != cell_output_map.end()); // single input and gate that is not used as a reduce and
      if (inps.size() == 1) {
        if (pid == 1) {  // REDUCE OP
          if (node.get_type().op == And_Op)
            module->addReduceAnd(next_id(g), inps[0], cell_output_map[node.get_driver_pin(pid).get_compact()]);
          else if (node.get_type().op == Or_Op)
            module->addReduceOr(next_id(g), inps[0], cell_output_map[node.get_driver_pin(pid).get_compact()]);
          else if (node.get_type().op == Xor_Op)
            module->addReduceXor(next_id(g), inps[0], cell_output_map[node.get_driver_pin(pid).get_compact()]);
          else
            assert(false);
        } else { // Just connect wire (one input)
          module->connect(cell_output_map[node.get_driver_pin(pid).get_compact()], inps[0]);
        }
      } else {
        if (pid == 1) {  // REDUCE OP
          RTLIL::Wire *or_input_wires = module->addWire(next_id(g), inps[0]->width);
          if (node.get_type().op == And_Op)
            create_tree(g, inps, module, &RTLIL::Module::addAnd, false, or_input_wires);
          else if (node.get_type().op == Or_Op)
            create_tree(g, inps, module, &RTLIL::Module::addOr, false, or_input_wires);
          else if (node.get_type().op == Xor_Op)
            create_tree(g, inps, module, &RTLIL::Module::addXor, false, or_input_wires);
          else
            assert(false);

          if (node.get_type().op == And_Op)
            module->addReduceAnd(next_id(g), or_input_wires, cell_output_map[node.get_driver_pin(pid).get_compact()]);
          else if (node.get_type().op == Or_Op)
            module->addReduceOr(next_id(g), or_input_wires, cell_output_map[node.get_driver_pin(pid).get_compact()]);
          else if (node.get_type().op == Xor_Op)
            module->addReduceXor(next_id(g), or_input_wires, cell_output_map[node.get_driver_pin(pid).get_compact()]);
          else
            assert(false);

        } else {
          if (node.get_type().op == And_Op)
            create_tree(g, inps, module, &RTLIL::Module::addAnd, false, cell_output_map[node.get_driver_pin(pid).get_compact()]);
          else if (node.get_type().op == Or_Op)
            create_tree(g, inps, module, &RTLIL::Module::addOr, false, cell_output_map[node.get_driver_pin(pid).get_compact()]);
          else if (node.get_type().op == Xor_Op)
            create_tree(g, inps, module, &RTLIL::Module::addXor, false, cell_output_map[node.get_driver_pin(pid).get_compact()]);
          else
            assert(false);
        }
      }
      break;
    }
    case Latch_Op: {
      RTLIL::Wire *dWire    = nullptr;
      RTLIL::Wire *enWire   = nullptr;
      bool         polarity = true;

      for(const auto &e:node.inp_edges()) {
        switch(e.sink.get_pid()) {
        case 0: dWire  = get_wire(e.driver); break;
        case 1: enWire = get_wire(e.driver); break;
        case 2: {
          if(e.driver.get_node().get_type().op != U32Const_Op)
            log_error("Internal Error: polarity is not a constant.\n");
          polarity = e.driver.get_node().get_type_const_value() != 0? true:false;
        } break;
        default:
          log_error("DumpYosys: unrecognized wire connection pid=%d\n", e.sink.get_pid());
        }
      }
      if(dWire)
        log("adding Latch_Op width = %d\n", dWire->width);
      if(enWire)
        log("adding Latch_Op enable width = %d\n", dWire->width);
      // last argument is polarity
      module->addDlatch(next_id(g), enWire, dWire, cell_output_map[node.get_driver_pin().get_compact()], polarity);
    } break;
    case SFlop_Op:
    case AFlop_Op: {
      RTLIL::Wire *enWire  = nullptr;
      RTLIL::Wire *dWire   = nullptr;
      RTLIL::Wire *clkWire = nullptr;
      RTLIL::Wire *rstWire = nullptr;
      RTLIL::Wire *rstVal  = nullptr;
      bool polarity = true;

      for(const auto &e:node.inp_edges()) {
        switch(e.sink.get_pid()) {
        case 0: clkWire = get_wire(e.driver); break;
        case 1: dWire   = get_wire(e.driver); break;
        case 2: enWire  = get_wire(e.driver); break;
        case 3: rstWire = get_wire(e.driver); break; // clr signal in yosys
        case 4: rstVal  = get_wire(e.driver); break; // set signal in yosys
        case 5: {
          if(e.driver.get_node().get_type().op != U32Const_Op)
            log_error("Internal Error: polarity is not a constant.\n");
          polarity = e.driver.get_node().get_type_const_value()!= 0? true:false;
        }
        default:
          log("[WARNING] DumpYosys: unrecognized wire connection pid=%d\n", e.sink.get_pid());
        }
      }
      if(dWire)
        log("adding sflop_Op width = %d\n", dWire->width);
      // last argument is polarity
      switch(node.get_type().op) {
      case SFlop_Op:
        if(enWire == nullptr && clkWire != nullptr && dWire != nullptr && rstWire == nullptr && rstVal == nullptr) {
          module->addDff(next_id(g), clkWire, dWire, cell_output_map[node.get_driver_pin().get_compact()], polarity);
        }else if(enWire == nullptr && clkWire != nullptr && dWire != nullptr && rstWire != nullptr && rstVal != nullptr) {
          module->addDffsr(next_id(g), clkWire, rstVal, rstWire, dWire, cell_output_map[node.get_driver_pin().get_compact()], polarity, polarity, polarity);
        } else {
          log_error("Enable and Reset not supported on Flops yet!\n");
        }
        break;
      case AFlop_Op:
        if(enWire == nullptr && clkWire != nullptr && dWire != nullptr && rstWire != nullptr && rstVal == nullptr) {
          module->addAdff(next_id(g), clkWire, rstWire, dWire, cell_output_map[node.get_driver_pin().get_compact()],RTLIL::Const(0, dWire->width), true, true);
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

      for(const auto &e:node.inp_edges()) {
        switch(e.sink.get_pid()) {
        case 0:
          if(lhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          lhs  = get_wire(e.driver);
          sign = true;
          break;
        case 1:
          if(lhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          lhs = get_wire(e.driver);
          break;
        case 2:
          if(rhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          rhs  = get_wire(e.driver);
          sign = true;
          break;
        case 3:
          if(rhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          rhs = get_wire(e.driver);
          break;
        }
      }

      if(lhs == nullptr || rhs == nullptr)
        log_error("Internal error: found no connections to LT pid.\n");

      switch(node.get_type().op) {
      case LessThan_Op:
        module->addLt(next_id(g), lhs, rhs, cell_output_map[node.get_driver_pin().get_compact()], sign);
        break;
      case LessEqualThan_Op:
        module->addLe(next_id(g), lhs, rhs, cell_output_map[node.get_driver_pin().get_compact()], sign);
        break;
      case GreaterThan_Op:
        module->addGt(next_id(g), lhs, rhs, cell_output_map[node.get_driver_pin().get_compact()], sign);
        break;
      case GreaterEqualThan_Op:
        module->addGe(next_id(g), lhs, rhs, cell_output_map[node.get_driver_pin().get_compact()], sign);
        break;
      case Div_Op:
        module->addDiv(next_id(g), lhs, rhs, cell_output_map[node.get_driver_pin().get_compact()], sign);
        break;
      case Mod_Op:
        module->addMod(next_id(g), lhs, rhs, cell_output_map[node.get_driver_pin().get_compact()], sign);
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

      for(const auto &e:node.inp_edges()) {
        size = (e.get_bits() > size) ? e.get_bits() : size;

        switch(e.sink.get_pid()) {
        case 0:
          if(shifted_wire != nullptr)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          shifted_wire = get_wire(e.driver);
          break;
        case 1:
          if(shift_amount.size() != 0)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          if(e.driver.get_node().get_type().op == U32Const_Op)
            shift_amount = RTLIL::Const(e.driver.get_node().get_type_const_value());
          else
            shift_amount = get_wire(e.driver);
          break;
        }
      }
      if(shifted_wire == nullptr)
        log_error("Internal Error: did not find a wire to be shifted.\n");
      if(shift_amount.size() == 0)
        log_error("Internal Error: did not find a wire to be shifted.\n");
      module->addShl(next_id(g), shifted_wire, shift_amount, cell_output_map[node.get_driver_pin().get_compact()], false);
      break;
    }
    case ShiftRight_Op: {
      //log("adding SHR_Op\n");
      RTLIL::Wire *  shifted_wire = nullptr;
      RTLIL::SigSpec shift_amount;

      bool sign   = false;
      bool b_sign = false;
      bool a_sign = false;

      for(const auto &e:node.inp_edges()) {
        size = (e.get_bits() > size) ? e.get_bits() : size;

        switch(e.sink.get_pid()) {
        case 0:
          if(shifted_wire != nullptr)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          shifted_wire = get_wire(e.driver);
          break;
        case 1:
          if(shift_amount.size() != 0)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          if(e.driver.get_node().get_type().op == U32Const_Op)
            shift_amount = RTLIL::Const(e.driver.get_node().get_type_const_value());
          else
            shift_amount = get_wire(e.driver);
          break;
        case 2:
          if(e.driver.get_node().get_type().op != U32Const_Op)
            log_error("Internal Error: Shift sign is not a constant.\n");
          auto val = e.driver.get_node().get_type_const_value();
          sign   = (val) % 2 == 1; // FIXME: Weird encoding
          b_sign = (val) == 2;
          a_sign = (val) > 2;
          break;
        }
      }
      if(shifted_wire == nullptr)
        log_error("Internal Error: did not find a wire to be shifted.\n");
      if(shift_amount.size() == 0)
        log_error("Internal Error: did not find a wire to be shifted.\n");

      if(sign)
        module->addSshr(next_id(g), shifted_wire, shift_amount, cell_output_map[node.get_driver_pin().get_compact()], a_sign);
      else if(b_sign)
        module->addShiftx(next_id(g), shifted_wire, shift_amount, cell_output_map[node.get_driver_pin().get_compact()], a_sign);
      else
        module->addShr(next_id(g), shifted_wire, shift_amount, cell_output_map[node.get_driver_pin().get_compact()], a_sign);
      break;
    }
    case Equals_Op: {
      std::vector<RTLIL::Wire *> e_unsigned;
      std::vector<RTLIL::Wire *> e_signed;

      size = 0;
      for(const auto &e:node.inp_edges()) {
        size = (e.get_bits() > size) ? e.get_bits() : size;

        switch(e.sink.get_pid()) {
        case 0:
          e_signed.push_back(get_wire(e.driver));
          break;
        case 1:
          e_unsigned.push_back(get_wire(e.driver));
          break;
        }
      }

      // u_type                 = (e_unsigned.size() > 0);
      RTLIL::Wire *eu_result = nullptr;
      RTLIL::Wire *es_result = nullptr;

      if(e_unsigned.size() > 1) {
        if(e_signed.size() == 0) {
          eu_result = cell_output_map[node.get_driver_pin().get_compact()];
        } else {
          eu_result = module->addWire(next_id(g), size);
        }
        create_tree(g, e_unsigned, module, &RTLIL::Module::addEq, false, eu_result);
      } else if(e_unsigned.size() == 1) {
        eu_result = e_unsigned[0];
      }

      if(e_signed.size() > 1) {
        if(e_unsigned.size() == 0) {
          es_result = cell_output_map[node.get_driver_pin().get_compact()];
        } else {
          es_result = module->addWire(next_id(g), size);
        }
        create_tree(g, e_signed, module, &RTLIL::Module::addEq, false, es_result);
      } else if(e_signed.size() == 1) {
        es_result = e_signed[0];
      }

      if(eu_result != nullptr && es_result != nullptr) {
        module->addEq(next_id(g), eu_result, es_result, cell_output_map[node.get_driver_pin().get_compact()], false);
      } else if(eu_result != nullptr) {
        if(cell_output_map[node.get_driver_pin().get_compact()]->name != eu_result->name)
          module->connect(cell_output_map[node.get_driver_pin().get_compact()], RTLIL::SigSpec(eu_result));
      } else if(es_result != nullptr) {
        if(cell_output_map[node.get_driver_pin().get_compact()]->name != es_result->name)
          module->connect(cell_output_map[node.get_driver_pin().get_compact()], RTLIL::SigSpec(es_result));
      }
      break;
    }
    case Mux_Op: {
      //log("adding MUX_Op\n");

      RTLIL::Wire *aport = nullptr;
      RTLIL::Wire *bport = nullptr;
      RTLIL::Wire *sel   = nullptr;

      for(const auto &e:node.inp_edges()) {
        switch(e.sink.get_pid()) {
        case 0:
          if(sel != nullptr)
            log_error("Internal Error: multiple wires assigned to same mux port\n");
          sel = get_wire(e.driver);
          break;
        case 1:
          if(aport != nullptr)
            log_error("Internal Error: multiple wires assigned to same mux port\n");
          aport = get_wire(e.driver);
          break;
        case 2:
          if(bport != nullptr)
            log_error("Internal Error: multiple wires assigned to same mux port\n");
          bport = get_wire(e.driver);
          break;
        }
      }

      if(aport == nullptr || bport == nullptr || sel == nullptr)
        log_error("Internal Error: did not find a wire to be shifted.\n");

      if(aport->width != cell_output_map[node.get_driver_pin().get_compact()]->width ||
         bport->width != cell_output_map[node.get_driver_pin().get_compact()]->width) {
        log("ports size don't match a=%d, b=%d, y=%d\n", aport->width, bport->width,
            cell_output_map[node.get_driver_pin().get_compact()]->width);
      }
      assert(aport->width <= (cell_output_map[node.get_driver_pin().get_compact()]->width));
      assert(bport->width <= (cell_output_map[node.get_driver_pin().get_compact()]->width));

      module->addMux(next_id(g), aport, bport, sel, cell_output_map[node.get_driver_pin().get_compact()]);
      break;
    }
    case Memory_Op: {
      //log("adding Mem_Op\n");
      create_memory(g, module, node);
      break;
    }
    case SubGraph_Op: {
      create_subgraph(g, module, node);
      break;
    }
#if 0
    case TechMap_Op: {

      const Tech_cell *tcell = node.get_type_tmap_cell();

      std::string name;
      if (tcell->get_name()[0] == '$' || tcell->get_name()[0] == '\\')
        name = absl::StrCat(tcell->get_name());
      else
        name = absl::StrCat("\\",tcell->get_name());

      RTLIL::Cell *new_cell = module->addCell(absl::StrCat("\\",node.create_name()), name);

      for(const auto &e:node.inp_edges()) {
        if(e.sink.get_pid() >= tcell->n_inps())
          log_error("I could not find a port associated with inp pin %hu\n", e.sink.get_pid());

        auto port_name = e.sink.get_type_tmap_io_name();

        RTLIL::Wire *wire = get_wire(e.driver);
        assert(wire);
        new_cell->setPort(absl::StrCat("\\", port_name).c_str(), wire);
      }

      for(const auto &e:node.out_edges()) {
        if(e.driver.get_pid() >= tcell->n_outs())
          log_error("I could not find a port associated with out pin %hu\n", e.driver.get_pid());

        auto port_name = e.driver.get_type_tmap_io_name();
        RTLIL::Wire *wire = get_wire(e.driver);
        assert(wire);
        new_cell->setPort(absl::StrCat("\\", port_name).c_str(), wire);
      }
      break;
    }
#endif
#if 0
    case BlackBox_Op: {
      std::string_view celltype;
      std::string_view instance_name;

      for(const auto &e:node.inp_edges()) {
        if(e.sink.get_pid() == LGRAPH_BBOP_TYPE) {
          if(e.driver.get_node().get_type().op != StrConst_Op)
            log_error("Internal Error: BB type is not a string.\n");
          celltype = e.driver.get_node().get_type_const_sview();
        } else if(e.sink.get_pid() == LGRAPH_BBOP_NAME) {
          // instance name
          if(e.driver.get_node().get_type().op != StrConst_Op)
            log_error("Internal Error: BB name is not a string.\n");
          instance_name = e.driver.get_node().get_type_const_sview();
        } else if(e.sink.get_pid() < LGRAPH_BBOP_OFFSET) {
          log_error("Unrecognized blackbox option, pid %hu\n", e.sink.get_pid());
        }
      }

      if (celltype.empty() || instance_name.empty()) {
        log_error("could not find instance name or celltype for blackbox\n");
      }

      RTLIL::Cell *cell = module->addCell(absl::StrCat("\\",instance_name), RTLIL::IdString(absl::StrCat("\\", celltype)));
      bool is_param = false;
      std::string current_name= "bbox";
      for(const auto &e:node.inp_edges()) {
        if(e.sink.get_pid() < LGRAPH_BBOP_OFFSET)
          continue;

        auto dpin_node = e.driver.get_node();

        if(LGRAPH_BBOP_ISIPARAM(e.sink.get_pid())) {
          //is_param = dpin_node.get_type_const_value() == 1;
          current_name = dpin_node.get_type_const_sview();
        } else if(LGRAPH_BBOP_ISICONNECT(e.sink.get_pid())) {
          if(is_param) {
            if(dpin_node.get_type().op == U32Const_Op) {
              cell->setParam(absl::StrCat("\\",current_name), RTLIL::Const(dpin_node.get_type_const_value()));
            } else if(dpin_node.get_type().op == StrConst_Op) {
              cell->setParam(absl::StrCat("\\",current_name), RTLIL::Const(std::string(dpin_node.get_type_const_sview())));
            } else {
              assert(false); // parameter is not a constant
            }
          } else {
            if(dpin_node.get_type().op == U32Const_Op) {
              cell->setPort(absl::StrCat("\\",current_name), RTLIL::Const(dpin_node.get_type_const_value()));
            } else if(dpin_node.get_type().op == StrConst_Op) {
              cell->setPort(absl::StrCat("\\",current_name), RTLIL::Const(std::string(dpin_node.get_type_const_sview())));
            } else {
              cell->setPort(absl::StrCat("\\",current_name), get_wire(e.driver));
            }
          }
        }
      }

      for(const auto &e:node.inp_edges()) {
        if (e.driver.is_graph_input())
          continue;
        if(LGRAPH_BBOP_ISICONNECT(e.sink.get_pid())) {
          std::string wname;
          if (e.driver.has_name()) {
            wname = absl::StrCat("\\",e.driver.get_name());
          } else {
            wname = "\\wire_";
            wname = unique_name(g, wname);
          }
          cell->setPort(wname, cell_output_map[e.driver.get_compact()]);
        }
      }
      break;
    }
#endif

    default:
      log_error("Operation %s not supported, please add to lgyosys_dump.\n", std::string(node.get_type().get_name()).c_str());
      break;
    }
  }
}
