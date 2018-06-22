//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <math.h>
#include <algorithm>

#include "lgedgeiter.hpp"
#include "dump_yosys.hpp"

RTLIL::Wire *Dump_yosys::get_wire(Index_ID idx, Port_ID pid, bool can_fail = false) {
  //std::pair<Index_ID,Port_ID> nid_pid = std::make_pair(inp_edge.get_idx(), inp_edge.get_out_pin().get_pid());
  std::pair<Index_ID, Port_ID> nid_pid = std::make_pair(idx, pid);
  if(input_map.find(idx) != input_map.end()) {
    return input_map[idx];

  } else if(output_map.find(idx) != output_map.end()) {
    return output_map[idx];

  } else if(cell_output_map.find(nid_pid) != cell_output_map.end()) {
    return cell_output_map[nid_pid];

  } else {
    if(can_fail)
      return nullptr;

    log("trying to get wire for nid %ld, %hu\n", nid_pid.first, nid_pid.second);
    log_error("Failed to get a wire for nid %ld\n", idx);
  }
}

RTLIL::Wire *Dump_yosys::create_tree(const LGraph *g, std::vector<RTLIL::Wire *> &wires,
                                     RTLIL::Module *mod, add_cell_fnc_sign add_fnc, bool sign, RTLIL::Wire *result_wire) {

  if(wires.size() == 0)
    return nullptr;

  if(wires.size() == 1)
    return wires[0];

  std::vector<RTLIL::Wire *> next_level;
  for(uint32_t current = 0; current < wires.size() / 2; current += 2) {
    RTLIL::Wire *aWire = nullptr;
    if(wires.size() > 2)
      aWire = mod->addWire(next_id(), result_wire->width);
    else
      aWire = result_wire;

    (mod->*add_fnc)(next_id(), wires[current], wires[current + 1], aWire, sign, "");
    next_level.push_back(aWire);
  }
  if(wires.size() % 2 == 1)
    next_level.push_back(wires[wires.size() - 1]);

  return create_tree(g, next_level, mod, add_fnc, sign, result_wire);
}

RTLIL::Wire *Dump_yosys::create_tree(const LGraph *g, std::vector<RTLIL::Wire *> &wires,
                                     RTLIL::Module *mod, add_cell_fnc add_fnc, RTLIL::Wire *result_wire) {

  if(wires.size() == 0)
    return nullptr;

  if(wires.size() == 1)
    return wires[0];

  std::vector<RTLIL::Wire *> next_level;
  for(uint32_t current = 0; current < wires.size() / 2; current += 2) {

    RTLIL::Wire *aWire = nullptr;
    if(wires.size() > 2)
      aWire = mod->addWire(next_id(), result_wire->width);
    else
      aWire = result_wire;
    (mod->*add_fnc)(next_id(), wires[current], wires[current + 1], aWire, "");
    next_level.push_back(aWire);
  }
  if(wires.size() % 2 == 1)
    next_level.push_back(wires[wires.size() - 1]);

  return create_tree(g, next_level, mod, add_fnc, result_wire);
}


RTLIL::Wire* Dump_yosys::create_wire(const LGraph *g, const Index_ID idx, RTLIL::Module* module, bool input, bool output) {

  RTLIL::IdString name;

  if(input)
    name = "\\" + std::string(g->get_graph_input_name(idx));
  else if(output)
    name = "\\" + std::string(g->get_graph_output_name(idx));
  else if(g->get_wid(idx))
    name = "\\" + std::string(g->get_node_wirename(idx));
  else
    name = "\\lgraph_cell_" + std::to_string(idx);

#if DEBUG
      fmt::print("adding wire to yosys module {}, name: {}\n", module->name.str(), name.str());
#endif

  RTLIL::Wire *new_wire = module->addWire(name, g->get_bits(idx));
  new_wire->start_offset = g->get_offset(idx);

  if(input || output) {
    module->ports.push_back(name);
    new_wire->port_id = module->ports.size();

    new_wire->port_input   = input;
    new_wire->port_output  = output;
  }

  return new_wire;
}

void Dump_yosys::to_yosys(const LGraph *g) {
  std::string name = g->get_name().substr(7);

  RTLIL::Module *module = design->addModule("\\" + name);

  input_map.clear();
  output_map.clear();
  cell_output_map.clear();

  // first create all the output wires
  for(auto idx : g->fast()) {
    assert(g->is_root(idx));
    log("creating wire for node: %ld, width %d, type %s\n", idx, g->get_bits(idx), g->node_type_get(idx).get_name().c_str());

    if(g->is_graph_input(idx)) {
      input_map[idx]    = create_wire(g, idx, module, true, false);
      continue;

    } else if(g->is_graph_output(idx)) {
      output_map[idx]   = create_wire(g, idx, module, false, true);
      continue;
    }

    RTLIL::IdString name;
    RTLIL::IdString yosys_op;
    const char *    wname = nullptr;
    if((wname = g->get_node_wirename(idx))) {
      name = RTLIL::IdString("\\" + std::string(wname));
    } else {
      name = RTLIL::IdString("\\lgraph_cell_" + std::to_string(idx));
    }

    if(g->node_type_get(idx).op == U32Const_Op) {
#if DEBUG
      fmt::print("adding wire to yosys module {}, name: {}\n", module->name.str(), name.str());
#endif
      RTLIL::Wire *new_wire = module->addWire(name, g->get_bits(idx));

      // constants treated as inputs
      module->connect(new_wire, RTLIL::SigSpec(g->node_value_get(idx), g->get_bits(idx)));
      input_map[idx] = new_wire;
      continue;

    } else if(g->node_type_get(idx).op == StrConst_Op) {
      std::string const_val = g->node_const_value_get(idx);
#if DEBUG
      fmt::print("adding wire to yosys module {}, name: {}\n", module->name.str(), name.str());
#endif
      RTLIL::Wire *new_wire = module->addWire(name, const_val.size());

      // constants treated as inputs
      module->connect(new_wire, RTLIL::SigSpec(RTLIL::Const::from_string(const_val)));
      input_map[idx] = new_wire;
      continue;

    } else if(g->node_type_get(idx).op == SubGraph_Op) {
      //FIXME: prevent creating wires when driving the output
      std::string subgraph_name = g->get_library()->get_name(g->subgraph_id_get(idx));
      LGraph *    subgraph      = LGraph::find_lgraph(g->get_path(), subgraph_name);
      if(subgraph == nullptr) {
        assert(false); // can we remove this?
        //need to load graph into memory
        char cadena[4096];
        snprintf(cadena, 4096, "%s/lgraph_%s_nodes", g->get_path().c_str(), subgraph_name.c_str());
        if(access(cadena, R_OK | W_OK) == -1) {
          console->error("ERROR: graph_name {} can not be opened in lgdb {}", subgraph_name, g->get_path());
          exit(-1);
        }
        //FIXME: prevent loading the whole graph just to read the IOs.
        subgraph = new LGraph(g->get_path(), subgraph_name, false);
      }
      std::set<Port_ID> visited_out_pids;

      for(auto &edge : g->out_edges(idx)) {
        //only once per pid
        if(visited_out_pids.find(edge.get_out_pin().get_pid()) != visited_out_pids.end())
          continue;

        visited_out_pids.insert(edge.get_out_pin().get_pid());
        const char *out_name = subgraph->get_graph_output_name_from_pid(edge.get_out_pin().get_pid());
        uint16_t    out_size = subgraph->get_bits(subgraph->get_graph_output(out_name).get_nid());

        std::string full_name = name.str() + std::string(out_name);

#if DEBUG
        fmt::print("adding wire to yosys module {}, name: {}\n", module->name.str(), full_name);
#endif
        RTLIL::Wire *new_wire                                              = module->addWire(full_name, out_size);
        cell_output_map[std::make_pair(idx, edge.get_out_pin().get_pid())] = new_wire;
      }
      continue;
    } else if(g->node_type_get(idx).op == TechMap_Op) {

      const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));

      std::set<Port_ID> visited_out_pids;

      for(auto &edge : g->out_edges(idx)) {
        //only once per pid
        if(visited_out_pids.find(edge.get_out_pin().get_pid()) != visited_out_pids.end())
          continue;

        visited_out_pids.insert(edge.get_out_pin().get_pid());
        uint16_t out_size = g->get_bits(edge.get_out_pin().get_nid());
        if(out_size == 0) {
          console->error("zero sized wire\n");
        }

        std::string full_name;
        if(g->get_wid(edge.get_out_pin().get_nid()) != 0) {
          full_name = g->get_node_wirename(edge.get_out_pin().get_nid());
        } else {
          std::string out_name = "";
          if(edge.get_out_pin().get_pid() < tcell->n_outs())
            out_name = tcell->get_name(edge.get_out_pin().get_pid());
          else
            out_name = next_id().str();
          full_name = name.str().substr(1) + std::string(out_name);
        }

#if DEBUG
        fmt::print("adding wire to yosys module {}, name: {}\n", module->name.str(), full_name);
#endif
        RTLIL::Wire *new_wire                                              = module->addWire("\\" + full_name, out_size);
        cell_output_map[std::make_pair(idx, edge.get_out_pin().get_pid())] = new_wire;
      }
      continue;
    } else if(g->node_type_get(idx).op == Memory_Op) {
      for(auto &edge : g->out_edges(idx)) {
        RTLIL::Wire *new_wire                                              = module->addWire(next_id(), g->get_bits(edge.get_idx()));
        cell_output_map[std::make_pair(idx, edge.get_out_pin().get_pid())] = new_wire;
        mem_output_map[idx].push_back(RTLIL::SigChunk(new_wire));
      }
      continue;
    } else if(g->node_type_get(idx).op == BlackBox_Op) {
      for(auto &edge : g->out_edges(idx)) {
        RTLIL::Wire *new_wire                                              = module->addWire(next_id(), g->get_bits(edge.get_idx()));
        cell_output_map[std::make_pair(idx, edge.get_out_pin().get_pid())] = new_wire;
      }
      continue;
    } else if(g->node_type_get(idx).op == And_Op || g->node_type_get(idx).op == Or_Op ||
              g->node_type_get(idx).op == Xor_Op) {
      //FIXME: prevent creating wires when driving the output
      // This differentiates between binary and unary Ands, Ors, Xors
      int count = 0;
      for(const auto &edge : g->out_edges(idx)) {
        if(cell_output_map.find(std::make_pair(idx, edge.get_out_pin().get_pid())) == cell_output_map.end()) {
          ++count;
          assert(count == 1);
          //bitwise operators have the regular size
          uint16_t out_size = g->get_bits(idx);

          //reduce operator have 1 bit
          if(edge.get_out_pin().get_pid() == 1)
            out_size = 1;
#if DEBUG
          fmt::print("adding wire to yosys module {}, name: {}\n", module->name.str(), name.str());
#endif
          RTLIL::Wire *new_wire                                              = module->addWire(name, out_size);
          cell_output_map[std::make_pair(idx, edge.get_out_pin().get_pid())] = new_wire;
        }
      }
      continue;
    }
    //FIXME: prevent creating wires when driving the output
#if DEBUG
    fmt::print("adding wire to yosys module {}, name: {}\n", module->name.str(), name.str());
#endif
    RTLIL::Wire *result                     = module->addWire(name, g->get_bits(idx));
    cell_output_map[std::make_pair(idx, 0)] = result;
  }

  // now create nodes and make connections
  for(auto idx : g->fast()) {

    if(g->is_graph_output(idx)) {

      RTLIL::SigSpec lhs  = RTLIL::SigSpec(output_map[idx]);
      uint8_t        inps = 0;
      for(const auto &c : g->inp_edges(idx)) {
        ++inps;
        if(inps != 1) {
          break;
          //FIXME: revert back to assertion
          assert(inps == 1); //is there a multidriver output?
        }

        RTLIL::SigSpec rhs = RTLIL::SigSpec(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
        module->connect(lhs, rhs);
      }
      continue;
    }

    RTLIL::IdString name = RTLIL::IdString("\\lgraph_cell_" + std::to_string(idx));
    RTLIL::IdString yosys_op;

    uint16_t size   = 0;
    bool     u_type = true;

    switch(g->node_type_get(idx).op) {
    case GraphIO_Op:
    case U32Const_Op:
    case StrConst_Op:
      continue;

    case Sum_Op: {
      log("adding Sum_Op\n");
      std::vector<RTLIL::Wire *> add_unsigned;
      std::vector<RTLIL::Wire *> add_signed;
      std::vector<RTLIL::Wire *> sub_unsigned;
      std::vector<RTLIL::Wire *> sub_signed;

      size = 0;
      for(const auto &c : g->inp_edges(idx)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          add_signed.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
          break;
        case 1:
          add_unsigned.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
          break;
        case 2:
          sub_signed.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
          break;
        case 3:
          sub_unsigned.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
          break;
        }
      }

      u_type = (add_unsigned.size() > 0 || sub_unsigned.size() > 0);

      RTLIL::Wire *addu_result = nullptr;
      if(add_unsigned.size() > 1) {
        if(add_signed.size() + sub_unsigned.size() + sub_signed.size() > 0) {
          addu_result = module->addWire(next_id(), size);
        } else {
          addu_result = cell_output_map[std::make_pair(idx, 0)];
        }
        create_tree(g, add_unsigned, module, &RTLIL::Module::addAdd, false, addu_result);
      } else if(add_unsigned.size() == 1) {
        addu_result = add_unsigned[0];
      }

      RTLIL::Wire *adds_result = nullptr;
      if(add_signed.size() > 1) {
        if(add_unsigned.size() + sub_unsigned.size() + sub_signed.size() > 0) {
          adds_result = module->addWire(next_id(), size);
        } else {
          adds_result = cell_output_map[std::make_pair(idx, 0)];
        }
        create_tree(g, add_signed, module, &RTLIL::Module::addAdd, true, adds_result);
      } else if(add_signed.size() == 1) {
        adds_result = add_signed[0];
      }

      RTLIL::Wire *subu_result = nullptr;
      if(sub_unsigned.size() > 1) {
        if(add_signed.size() + add_unsigned.size() + sub_signed.size() > 0) {
          subu_result = module->addWire(next_id(), size);
        } else {
          subu_result = cell_output_map[std::make_pair(idx, 0)];
        }
        create_tree(g, sub_unsigned, module, &RTLIL::Module::addAdd, false, subu_result);
      } else if(sub_unsigned.size() == 1) {
        subu_result = sub_unsigned[0];
      }

      RTLIL::Wire *subs_result = nullptr;
      if(sub_signed.size() > 1) {
        if(add_unsigned.size() + sub_unsigned.size() + add_signed.size() > 0) {
          subs_result = module->addWire(next_id(), size);
        } else {
          subs_result = cell_output_map[std::make_pair(idx, 0)];
        }
        create_tree(g, sub_signed, module, &RTLIL::Module::addAdd, true, subs_result);
      } else if(sub_signed.size() == 1) {
        subs_result = sub_signed[0];
      }

      RTLIL::Wire *a_result = nullptr;
      if(addu_result != nullptr && adds_result != nullptr) {
        if(subu_result != nullptr || subs_result != nullptr) {
          a_result = module->addWire(next_id(), size);
        } else {
          a_result = cell_output_map[std::make_pair(idx, 0)];
        }
        module->addAdd(next_id(), addu_result, adds_result, false, a_result);
      } else if(addu_result != nullptr) {
        a_result = addu_result;
      } else if(adds_result != nullptr) {
        a_result = adds_result;
      }

      RTLIL::Wire *s_result = nullptr;
      if(subu_result != nullptr && subs_result != nullptr) {
        s_result = module->addWire(next_id(), size);
        module->addAdd(next_id(), subu_result, subs_result, false, s_result);
      } else if(subu_result != nullptr) {
        s_result = subu_result;
      } else if(subs_result != nullptr) {
        s_result = subs_result;
      }

      if(a_result != nullptr && s_result != nullptr) {
        module->addSub(next_id(), a_result, s_result, cell_output_map[std::make_pair(idx, 0)], false);
      } else if(s_result != nullptr) {
        module->addSub(next_id(), RTLIL::Const(0), s_result, cell_output_map[std::make_pair(idx, 0)], false);
      } else {
        if(a_result != cell_output_map[std::make_pair(idx, 0)])
          module->connect(cell_output_map[std::make_pair(idx, 0)], RTLIL::SigSpec(a_result));
      }

      break;
    }
    case Mult_Op: {
      std::vector<RTLIL::Wire *> m_unsigned;
      std::vector<RTLIL::Wire *> m_signed;

      size = 0;
      for(const auto &c : g->inp_edges(idx)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          m_signed.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
          break;
        case 1:
          m_unsigned.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
          break;
        }
      }

      u_type                 = (m_unsigned.size() > 0);
      RTLIL::Wire *mu_result = nullptr;
      RTLIL::Wire *ms_result = nullptr;

      if(m_unsigned.size() > 1) {
        if(m_signed.size() == 0) {
          mu_result = cell_output_map[std::make_pair(idx, 0)];
        } else {
          mu_result = module->addWire(next_id(), size);
        }
        create_tree(g, m_unsigned, module, &RTLIL::Module::addMul, false, mu_result);
      } else if(m_unsigned.size() == 1) {
        mu_result = m_unsigned[0];
      }

      if(m_signed.size() > 1) {
        if(m_unsigned.size() == 0) {
          ms_result = cell_output_map[std::make_pair(idx, 0)];
        } else {
          ms_result = module->addWire(next_id(), size);
        }
        create_tree(g, m_signed, module, &RTLIL::Module::addMul, false, ms_result);
      } else if(m_signed.size() == 1) {
        ms_result = m_signed[0];
      }

      if(mu_result != nullptr && ms_result != nullptr) {
        module->addMul(next_id(), mu_result, ms_result, cell_output_map[std::make_pair(idx, 0)], false);
      } else if(mu_result != nullptr) {
        module->connect(cell_output_map[std::make_pair(idx, 0)], RTLIL::SigSpec(mu_result));
      } else if(ms_result != nullptr) {
        module->connect(cell_output_map[std::make_pair(idx, 0)], RTLIL::SigSpec(ms_result));
      }

      break;
    }

    case Not_Op: {
      int n_inps = 0;

      for(const auto &c : g->inp_edges(idx)) {
        n_inps++;
        RTLIL::Wire *inWire = get_wire(c.get_idx(), c.get_out_pin().get_pid());
        module->addNot(next_id(), inWire, cell_output_map[std::make_pair(idx, 0)]);
      }
      //if the assertion fails, check what does a not with multiple inputs mean
      assert(n_inps == 1);

      break;
    }
    case Join_Op: {
      std::vector<RTLIL::SigChunk> joined_wires;
      joined_wires.resize(cell_output_map[std::make_pair(idx, 0)]->width);
      uint32_t width      = 0;
      bool     has_inputs = false;
      for(const auto &c : g->inp_edges(idx)) {
        RTLIL::Wire *join                       = get_wire(c.get_idx(), c.get_out_pin().get_pid());
        joined_wires[c.get_inp_pin().get_pid()] = RTLIL::SigChunk(join);
        width += join->width;
        has_inputs = true;
      }

      if(!has_inputs) {
        continue;
      }
      assert(width == (cell_output_map[std::make_pair(idx, 0)]->width));
      module->connect(cell_output_map[std::make_pair(idx, 0)], RTLIL::SigSpec(joined_wires));

      break;
    }
    case Pick_Op: {
      int          upper       = 0;
      int          lower       = 0;
      RTLIL::Wire *picked_wire = nullptr;

      for(const auto &c : g->inp_edges(idx)) {
        switch(c.get_inp_pin().get_pid()) {
        case 0:
          picked_wire = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 1:
          if(g->node_type_get(c.get_idx()).op != U32Const_Op)
            log_error("Internal Error: Pick range is not a constant.\n");
          lower = g->node_value_get(c.get_idx());
          break;
        default:
          assert(0); //pids > 1 not supported
        }
      }
      upper = lower + g->get_bits(idx) - 1;
      //FIXME: get back to assertion after google demo
      //assert(upper-lower+1 <= picked_wire->width);
      if(upper - lower + 1 > picked_wire->width) {
        upper = lower + picked_wire->width - 1;
        module->connect(RTLIL::SigSpec(cell_output_map[std::make_pair(idx, 0)], lower, upper - lower + 1), RTLIL::SigSpec(picked_wire, lower, upper - lower + 1));
      } else {
        module->connect(cell_output_map[std::make_pair(idx, 0)], RTLIL::SigSpec(picked_wire, lower, upper - lower + 1));
      }
      break;
    }
    case And_Op: {
      log("adding And_Op\n");
      std::vector<RTLIL::Wire *> and_inps;
      uint32_t                   width = 0;
      for(const auto &c : g->inp_edges(idx)) {
        RTLIL::Wire *current = get_wire(c.get_idx(), c.get_out_pin().get_pid());
        and_inps.push_back(current);
        width = (width < current->width) ? current->width : width;
      }

      if(and_inps.size() == 1) {
        //reduce and: assign a = &b;
        module->addReduceAnd(next_id(), and_inps[0], cell_output_map[std::make_pair(idx, 1)]);
      } else {
        if(idx == 8)
          log("idx 8\n");
        create_tree(g, and_inps, module, &RTLIL::Module::addAnd, false, cell_output_map[std::make_pair(idx, 0)]);
      }
      break;
    }
    case Or_Op: {
      log("adding Or_Op\n");
      std::vector<RTLIL::Wire *> or_inps;
      for(const auto &c : g->inp_edges(idx)) {
        or_inps.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
      }

      if(or_inps.size() == 1) {
        //reduce or: assign a = |b;
        module->addReduceOr(next_id(), or_inps[0], cell_output_map[std::make_pair(idx, 1)]);
      } else {
        create_tree(g, or_inps, module, &RTLIL::Module::addOr, false, cell_output_map[std::make_pair(idx, 0)]);
      }
      break;
    }

    case Xor_Op: {
      log("adding xOr_Op\n");
      std::vector<RTLIL::Wire *> xor_inps;
      for(const auto &c : g->inp_edges(idx)) {
        xor_inps.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
      }

      if(xor_inps.size() == 1) {
        //reduce xor: assign a = ^b;
        module->addReduceXor(next_id(), xor_inps[0], cell_output_map[std::make_pair(idx, 1)]);
      } else {
        create_tree(g, xor_inps, module, &RTLIL::Module::addXor, false, cell_output_map[std::make_pair(idx, 0)]);
      }
      break;
    }
    case Flop_Op:
    case AFlop_Op: {
      RTLIL::Wire *enWire  = nullptr;
      RTLIL::Wire *dWire   = nullptr;
      RTLIL::Wire *clkWire = nullptr;
      RTLIL::Wire *rstWire = nullptr;
      RTLIL::Wire *rstVal  = nullptr;

      for(const auto &c : g->inp_edges(idx)) {
        switch(c.get_inp_pin().get_pid()) {
        case 0:
          clkWire = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 1:
          dWire = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 2:
          enWire = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 3:
          rstWire = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 4:
          rstVal = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        default:
          log("[WARNING] DumpYosys: unrecognized wire connection pid=%d\n", c.get_out_pin().get_pid());
        }
      }
      if(dWire)
        log("adding flop_Op width = %d\n", dWire->width);
      //last argument is polarity
      switch(g->node_type_get(idx).op) {
      case Flop_Op:
        if(enWire == nullptr && clkWire != nullptr && dWire != nullptr && rstWire == nullptr && rstVal == nullptr) {
          module->addDff(next_id(), clkWire, dWire, cell_output_map[std::make_pair(idx, 0)], true);
        } else {
          log_error("Enable and Reset not supported on Flops yet!\n");
        }
        break;
      case AFlop_Op:
        if(enWire == nullptr && clkWire != nullptr && dWire != nullptr && rstWire != nullptr && rstVal == nullptr) {
          //      addAdff(name,      sig_clk,sig_arst, sig_d, sig_q,                                   arst_value, clk_polarity, arst_polarity);
          module->addAdff(next_id(), clkWire, rstWire, dWire, cell_output_map[std::make_pair(idx, 0)], RTLIL::Const(0, dWire->width), true, true);
        } else {
          log_error("Enable not supported on AFlops yet, RST required on AFlops!\n");
        }
        break;
      default:
        assert(false); //internal error
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

      for(const auto &c : g->inp_edges(idx)) {
        switch(c.get_inp_pin().get_pid()) {
        case 0:
          if(lhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          lhs  = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          sign = true;
          break;
        case 1:
          if(lhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          lhs = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 2:
          if(rhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          rhs  = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          sign = true;
          break;
        case 3:
          if(rhs != nullptr)
            log_error("Internal error: found two connections to GT pid.\n");
          rhs = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        }
      }

      if(lhs == nullptr || rhs == nullptr)
        log_error("Internal error: found no connections to LT pid.\n");

      switch(g->node_type_get(idx).op) {
      case LessThan_Op:
        module->addLt(next_id(), lhs, rhs, cell_output_map[std::make_pair(idx, 0)], sign);
        break;
      case LessEqualThan_Op:
        module->addLe(next_id(), lhs, rhs, cell_output_map[std::make_pair(idx, 0)], sign);
        break;
      case GreaterThan_Op:
        module->addGt(next_id(), lhs, rhs, cell_output_map[std::make_pair(idx, 0)], sign);
        break;
      case GreaterEqualThan_Op:
        module->addGe(next_id(), lhs, rhs, cell_output_map[std::make_pair(idx, 0)], sign);
        break;
      case Div_Op:
        module->addDiv(next_id(), lhs, rhs, cell_output_map[std::make_pair(idx, 0)], sign);
        break;
      case Mod_Op:
        module->addMod(next_id(), lhs, rhs, cell_output_map[std::make_pair(idx, 0)], sign);
        break;
      default:
        console->error("Internal Error!");
      }
      break;
    }
    case ShiftLeft_Op: {
      log("adding SHL_Op\n");
      RTLIL::Wire *  shifted_wire = nullptr;
      RTLIL::SigSpec shift_amount;

      for(const auto &c : g->inp_edges(idx)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          if(shifted_wire != nullptr)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          shifted_wire = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 1:
          if(shift_amount.size() != 0)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          if(g->node_type_get(c.get_idx()).op == U32Const_Op)
            shift_amount = RTLIL::Const(g->node_value_get(c.get_idx()));
          else
            shift_amount = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        }
      }
      if(shifted_wire == nullptr)
        log_error("Internal Error: did not find a wire to be shifted.\n");
      if(shift_amount.size() == 0)
        log_error("Internal Error: did not find a wire to be shifted.\n");
      module->addShl(next_id(), shifted_wire, shift_amount, cell_output_map[std::make_pair(idx, 0)], false);
      break;
    }
    case ShiftRight_Op: {
      log("adding SHR_Op\n");
      RTLIL::Wire *  shifted_wire = nullptr;
      RTLIL::SigSpec shift_amount;

      bool sign   = false;
      bool b_sign = false;

      for(const auto &c : g->inp_edges(idx)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          if(shifted_wire != nullptr)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          shifted_wire = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 1:
          if(shift_amount.size() != 0)
            log_error("Internal Error: multiple wires assigned to same shift\n");
          if(g->node_type_get(c.get_idx()).op == U32Const_Op)
            shift_amount = RTLIL::Const(g->node_value_get(c.get_idx()));
          else
            shift_amount = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 2:
          if(g->node_type_get(c.get_idx()).op != U32Const_Op)
            log_error("Internal Error: Shift sign is not a constant.\n");
          sign   = (g->node_value_get(c.get_idx()) == 1);
          b_sign = (g->node_value_get(c.get_idx()) == 2);
          break;
        }
      }
      if(shifted_wire == nullptr)
        log_error("Internal Error: did not find a wire to be shifted.\n");
      if(shift_amount.size() == 0)
        log_error("Internal Error: did not find a wire to be shifted.\n");

      if(sign)
        module->addSshr(next_id(), shifted_wire, shift_amount, cell_output_map[std::make_pair(idx, 0)], false);
      else if(b_sign)
        module->addShiftx(next_id(), shifted_wire, shift_amount, cell_output_map[std::make_pair(idx, 0)], false);
      else
        module->addShr(next_id(), shifted_wire, shift_amount, cell_output_map[std::make_pair(idx, 0)], false);
      break;
    }
    case Equals_Op: {
      std::vector<RTLIL::Wire *> e_unsigned;
      std::vector<RTLIL::Wire *> e_signed;

      size = 0;
      for(const auto &c : g->inp_edges(idx)) {
        size = (c.get_bits() > size) ? c.get_bits() : size;

        switch(c.get_inp_pin().get_pid()) {
        case 0:
          e_signed.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
          break;
        case 1:
          e_unsigned.push_back(get_wire(c.get_idx(), c.get_out_pin().get_pid()));
          break;
        }
      }

      u_type                 = (e_unsigned.size() > 0);
      RTLIL::Wire *eu_result = nullptr;
      RTLIL::Wire *es_result = nullptr;

      if(e_unsigned.size() > 1) {
        if(e_signed.size() == 0) {
          eu_result = cell_output_map[std::make_pair(idx, 0)];
        } else {
          eu_result = module->addWire(next_id(), size);
        }
        create_tree(g, e_unsigned, module, &RTLIL::Module::addEq, false, eu_result);
      } else if(e_unsigned.size() == 1) {
        eu_result = e_unsigned[0];
      }

      if(e_signed.size() > 1) {
        if(e_unsigned.size() == 0) {
          es_result = cell_output_map[std::make_pair(idx, 0)];
        } else {
          es_result = module->addWire(next_id(), size);
        }
        create_tree(g, e_signed, module, &RTLIL::Module::addEq, false, es_result);
      } else if(e_signed.size() == 1) {
        es_result = e_signed[0];
      }

      if(eu_result != nullptr && es_result != nullptr) {
        module->addEq(next_id(), eu_result, es_result, cell_output_map[std::make_pair(idx, 0)], false);
      } else if(eu_result != nullptr) {
        module->connect(cell_output_map[std::make_pair(idx, 0)], RTLIL::SigSpec(eu_result));
      } else if(es_result != nullptr) {
        module->connect(cell_output_map[std::make_pair(idx, 0)], RTLIL::SigSpec(es_result));
      }
      break;
    }
    case Mux_Op: {
      log("adding MUX_Op\n");

      RTLIL::Wire *aport = nullptr;
      RTLIL::Wire *bport = nullptr;
      RTLIL::Wire *sel   = nullptr;

      for(const auto &c : g->inp_edges(idx)) {
        switch(c.get_inp_pin().get_pid()) {
        case 0:
          if(sel != nullptr)
            log_error("Internal Error: multiple wires assigned to same mux port\n");
          sel = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 1:
          if(aport != nullptr)
            log_error("Internal Error: multiple wires assigned to same mux port\n");
          aport = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        case 2:
          if(bport != nullptr)
            log_error("Internal Error: multiple wires assigned to same mux port\n");
          bport = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          break;
        }
      }
      if(aport == nullptr || bport == nullptr || sel == nullptr)
        log_error("Internal Error: did not find a wire to be shifted.\n");

      if(aport->width != cell_output_map[std::make_pair(idx, 0)]->width ||
         bport->width != cell_output_map[std::make_pair(idx, 0)]->width) {
        log("ports size don't match a=%d, b=%d, y=%d\n", aport->width, bport->width, cell_output_map[std::make_pair(idx, 0)]->width);
      }
      assert(aport->width == (cell_output_map[std::make_pair(idx, 0)]->width));
      assert(bport->width == (cell_output_map[std::make_pair(idx, 0)]->width));

      module->addMux(next_id(), aport, bport, sel, cell_output_map[std::make_pair(idx, 0)]);
      break;
    }
    case Memory_Op: {
      log("adding Mem_Op\n");

      std::string cell_name = g->get_node_wirename(idx);

      RTLIL::Cell *memory = module->addCell("\\" + cell_name, RTLIL::IdString("$mem"));

      RTLIL::Wire *  clk = nullptr;
      RTLIL::SigSpec wr_addr, wr_data, wr_en, rd_addr, rd_en, rd_data;
      RTLIL::State   posedge, transp;

      for(const auto &c : g->inp_edges(idx)) {

        Port_ID input_pin = c.get_inp_pin().get_pid();

        if(input_pin == LGRAPH_MEMOP_CLK) {
          if(clk != nullptr)
            log_error("Internal Error: multiple wires assigned to same mem port\n");
          clk = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          assert(clk);
        } else if(input_pin == LGRAPH_MEMOP_SIZE) {
          if(g->node_type_get(c.get_idx()).op != U32Const_Op)
            log_error("Internal Error: Mem size is not a constant.\n");
          memory->setParam("\\SIZE", RTLIL::Const(g->node_value_get(c.get_idx())));
        } else if(input_pin == LGRAPH_MEMOP_ABITS) {
          if(g->node_type_get(c.get_idx()).op != U32Const_Op)
            log_error("Internal Error: Mem addr bits is not a constant.\n");
          memory->setParam("\\ABITS", RTLIL::Const(g->node_value_get(c.get_idx())));
        } else if(input_pin == LGRAPH_MEMOP_WRPORT) {
          if(g->node_type_get(c.get_idx()).op != U32Const_Op)
            log_error("Internal Error: Mem num wr ports is not a constant.\n");
          memory->setParam("\\WR_PORTS", RTLIL::Const(g->node_value_get(c.get_idx())));
        } else if(input_pin == LGRAPH_MEMOP_RDPORT) {
          if(g->node_type_get(c.get_idx()).op != U32Const_Op)
            log_error("Internal Error: Mem num rd ports is not a constant.\n");
          memory->setParam("\\RD_PORTS", RTLIL::Const(g->node_value_get(c.get_idx())));
        } else if(input_pin == LGRAPH_MEMOP_RDTRAN) {
          if(g->node_type_get(c.get_idx()).op != U32Const_Op)
            log_error("Internal Error: Mem rd_transp is not a constant.\n");
          transp = g->node_value_get(c.get_idx()) ? RTLIL::State::S1 : RTLIL::State::S0;
        } else if(input_pin == LGRAPH_MEMOP_CLKPOL) {
          if(g->node_type_get(c.get_idx()).op != U32Const_Op)
            log_error("Internal Error: Mem RD_CLK polarity is not a constant.\n");
          posedge = (g->node_value_get(c.get_idx()) == 0) ? RTLIL::State::S1 : RTLIL::State::S0;
        } else if(LGRAPH_MEMOP_ISWRADDR(input_pin)) {
          wr_addr.append(RTLIL::SigSpec(get_wire(c.get_idx(), c.get_out_pin().get_pid())));
        } else if(LGRAPH_MEMOP_ISWRDATA(input_pin)) {
          wr_data.append(RTLIL::SigSpec(get_wire(c.get_idx(), c.get_out_pin().get_pid())));
        } else if(LGRAPH_MEMOP_ISWREN(input_pin)) {
          // we assume one write per port, yosys has one per bit
          RTLIL::Wire *en = get_wire(c.get_idx(), c.get_out_pin().get_pid());
          assert(en->width == 1);
          wr_en.append(RTLIL::SigSpec(RTLIL::SigBit(en), g->get_bits(idx)));
        } else if(LGRAPH_MEMOP_ISRDADDR(input_pin)) {
          rd_addr.append(RTLIL::SigSpec(get_wire(c.get_idx(), c.get_out_pin().get_pid())));
        } else if(LGRAPH_MEMOP_ISRDEN(input_pin)) {
          rd_en.append(RTLIL::SigSpec(get_wire(c.get_idx(), c.get_out_pin().get_pid())));
        } else {
          log_error("Unrecognized input pid %hu\n", input_pin);
          assert(false);
        }
      }

      memory->setParam("\\MEMID", RTLIL::Const(cell_name));
      memory->setParam("\\WIDTH", g->get_bits(idx));

      memory->setParam("\\RD_CLK_ENABLE", RTLIL::Const(RTLIL::State::S1, memory->getParam("\\RD_PORTS").as_int()));
      memory->setParam("\\WR_CLK_ENABLE", RTLIL::Const(RTLIL::State::S1, memory->getParam("\\WR_PORTS").as_int()));

      memory->setParam("\\INIT", RTLIL::Const::from_string("x"));
      memory->setParam("\\OFFSET", RTLIL::Const(0));

      memory->setParam("\\RD_CLK_POLARITY", RTLIL::Const(posedge, memory->getParam("\\RD_PORTS").as_int()));
      memory->setParam("\\WR_CLK_POLARITY", RTLIL::Const(posedge, memory->getParam("\\WR_PORTS").as_int()));
      memory->setParam("\\RD_TRANSPARENT", RTLIL::Const(transp, memory->getParam("\\RD_PORTS").as_int()));

      memory->setPort("\\WR_DATA", wr_data);
      memory->setPort("\\WR_ADDR", wr_addr);
      memory->setPort("\\WR_EN", wr_en);

      memory->setPort("\\RD_DATA", RTLIL::SigSpec(mem_output_map[idx]));
      memory->setPort("\\RD_ADDR", rd_addr);
      memory->setPort("\\RD_EN", rd_en);

      memory->setPort("\\WR_CLK", RTLIL::SigSpec(RTLIL::SigBit(clk), memory->getParam("\\WR_PORTS").as_int()));
      memory->setPort("\\RD_CLK", RTLIL::SigSpec(RTLIL::SigBit(clk), memory->getParam("\\RD_PORTS").as_int()));

      break;
    }
    case SubGraph_Op: {
      LGraph *subgraph = g->get_library()->get_graph(g->subgraph_id_get(idx));
      if(subgraph == nullptr) {
        //FIXME: prevent loading the whole graph just to read the IOs if
        //hierarchy is set to false
        std::string subgraph_name = g->get_subgraph_name(idx);
        subgraph                  = LGraph::open_lgraph(g->get_path(), subgraph_name);
      }
      if(hierarchy) {
        _subgraphs.insert(subgraph);
      }

      RTLIL::IdString instance_name("\\tmp");
      if(g->get_instance_name_id(idx) == 0 || std::string(g->get_node_instancename(idx)) == "") {
        instance_name = next_id();
#ifndef NDEBUG
        fmt::print("inou_yosys got empty inst_name for cell type {}\n", subgraph->get_name());
#endif
      } else {
        instance_name = RTLIL::IdString("\\" + std::string(g->get_node_instancename(idx)));
      }

      RTLIL::Cell *new_cell = module->addCell(instance_name, "\\" + subgraph->get_name().substr(7));
      for(const auto &c : g->inp_edges(idx)) {
        std::string  port  = subgraph->get_graph_input_name_from_pid(c.get_inp_pin().get_pid());
        RTLIL::Wire *input = get_wire(c.get_idx(), c.get_out_pin().get_pid());
        new_cell->setPort(("\\" + port).c_str(), input);
      }
      for(const auto &c : g->out_edges(idx)) {
        std::string  port   = subgraph->get_graph_output_name_from_pid(c.get_out_pin().get_pid());
        RTLIL::Wire *output = get_wire(c.get_out_pin().get_nid(), c.get_out_pin().get_pid());
        new_cell->setPort(("\\" + port).c_str(), output);
      }
      break;
    }
    case TechMap_Op: {

      const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));

      RTLIL::IdString instance_name("\\tmp");
      if(g->get_instance_name_id(idx) == 0 || std::string(g->get_node_instancename(idx)) == "") {
        instance_name = next_id();
#ifndef NDEBUG
        fmt::print("inou_yosys got empty inst_name for cell type {}\n", tcell->get_name());
#endif
      } else {
        instance_name = RTLIL::IdString("\\" + std::string(g->get_node_instancename(idx)));
      }

      std::string  name     = (tcell->get_name()[0] == '$' || tcell->get_name()[0] == '\\') ? tcell->get_name() : "\\" + tcell->get_name();
      RTLIL::Cell *new_cell = module->addCell(instance_name, name);
      for(const auto &c : g->inp_edges(idx)) {
        std::string port = "";
        if(c.get_inp_pin().get_pid() < tcell->n_inps())
          port = tcell->get_input_name(c.get_inp_pin().get_pid());
        else
          log_error("I could not find a port associated with inp pin %hu\n", c.get_inp_pin().get_pid());

        RTLIL::Wire *input = get_wire(c.get_idx(), c.get_out_pin().get_pid());
        new_cell->setPort(("\\" + port).c_str(), input);
      }
      for(const auto &c : g->out_edges(idx)) {
        std::string port = "";
        if(c.get_out_pin().get_pid() < tcell->n_outs())
          port = tcell->get_output_name(c.get_out_pin().get_pid());
        else
          log_error("I could not find a port associated with out pin %hu\n", c.get_out_pin().get_pid());
        RTLIL::Wire *output = get_wire(c.get_out_pin().get_nid(), c.get_out_pin().get_pid());
        assert(output);
        new_cell->setPort(("\\" + port).c_str(), output);
      }
      break;
    }
    case BlackBox_Op: {
      std::string celltype;
      std::string instance_name;

      for(const auto &c : g->inp_edges(idx)) {
        if(c.get_out_pin().get_pid() == LGRAPH_BBOP_TYPE) {
          //celltype
          if(g->node_type_get(c.get_idx()).op != StrConst_Op)
            log_error("Internal Error: BB type is not a string.\n");
          celltype = g->node_const_value_get(c.get_idx());
        } else if(c.get_out_pin().get_pid() == LGRAPH_BBOP_NAME) {
          //instance name
          if(g->node_type_get(c.get_idx()).op != StrConst_Op)
            log_error("Internal Error: BB name is not a string.\n");
          instance_name = g->node_const_value_get(c.get_idx());
        } else if(c.get_out_pin().get_pid() < LGRAPH_BBOP_OFFSET) {
          log_error("Unrecognized blackbox option, pid %hu\n", c.get_out_pin().get_pid());
        }
      }

      std::vector<std::string> output_names;
      RTLIL::Cell *            cell = module->addCell("\\" + instance_name, RTLIL::IdString("\\" + celltype));
      std::string              current_name;
#ifndef NDEBUG
      int current_port = 0, def = 0;
#endif
      bool is_param = false;
      for(const auto &c : g->inp_edges(idx)) {
        if(c.get_out_pin().get_pid() >= LGRAPH_BBOP_OFFSET) {
          if(LGRAPH_BBOP_ISPARAM(c.get_out_pin().get_pid())) {
            if(g->node_type_get(c.get_idx()).op != U32Const_Op)
              log_error("Internal Error: Could not define if input is parameter.\n");
            is_param = g->node_value_get(c.get_idx()) == 1;
#ifndef NDEBUG
            assert(def == 0);
            def++;
            assert(current_port == 0);
            current_port = LGRAPH_BBOP_PORT_N(c.get_out_pin().get_pid());
#endif
          } else if(LGRAPH_BBOP_ISPNAME(c.get_out_pin().get_pid())) {
            if(g->node_type_get(c.get_idx()).op != StrConst_Op)
              log_error("Internal Error: BB input name not a string.\n");
            current_name = g->node_const_value_get(c.get_idx());
#ifndef NDEBUG
            assert(def == 1);
            def += 1;
            assert(current_port == 0 || current_port == LGRAPH_BBOP_PORT_N(c.get_out_pin().get_pid()));
#endif
          } else if(LGRAPH_BBOP_ISCONNECT(c.get_out_pin().get_pid())) {
            if(is_param) {
              if(g->node_type_get(c.get_idx()).op == U32Const_Op) {
                cell->setParam("\\" + current_name, RTLIL::Const(g->node_value_get(c.get_idx())));
              } else if(g->node_type_get(c.get_idx()).op == StrConst_Op) {
                cell->setParam("\\" + current_name, RTLIL::Const(g->node_const_value_get(c.get_idx())));
              } else {
                assert(false); // parameter is not a constant
              }
            } else {
              if(g->node_type_get(c.get_idx()).op == U32Const_Op) {
                cell->setPort("\\" + current_name, RTLIL::Const(g->node_value_get(c.get_idx())));
              } else if(g->node_type_get(c.get_idx()).op == StrConst_Op) {
                cell->setPort("\\" + current_name, RTLIL::Const(g->node_const_value_get(c.get_idx())));
              } else {
                cell->setPort("\\" + current_name, get_wire(c.get_idx(), c.get_out_pin().get_pid()));
              }
            }
#ifndef NDEBUG
            assert(def == 2);
            assert(current_port == LGRAPH_BBOP_PORT_N(c.get_out_pin().get_pid()));
            current_port = 0;
            def          = 0;
#endif
          } else if(LGRAPH_BBOP_ISONAME(c.get_out_pin().get_pid())) {
            if(g->node_type_get(c.get_idx()).op != StrConst_Op)
              log_error("Internal Error: BB output name is not a string.\n");
            assert(output_names.size() == LGRAPH_BBOP_PORT_N(c.get_out_pin().get_pid()));
            output_names.push_back(g->node_const_value_get(c.get_idx()));
          }
        }
      }
#ifndef NDEBUG
      current_port = 0, def = 0;
#endif
      int i = 0;
      for(const auto &c : g->out_edges(idx)) {
        cell->setPort("\\" + output_names[i++], cell_output_map[std::make_pair(idx, c.get_out_pin().get_pid())]);
      }
      break;
    }

    default:
      log_error("Operation %s (node = %ld) not supported, please add to dump_yosys.\n", g->node_type_get(idx).get_name().c_str(), idx);
      break;
    }
  }
}
