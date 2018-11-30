//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 4/25/18.

#include <regex>

#include "lgraph.hpp"

#include "pass_abc.hpp"

void Pass_abc::from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {

  if(!Abc_NtkIsAigNetlist(pNtk) && !Abc_NtkIsMappedNetlist(pNtk)) {
    console->error("Io_WriteVerilog(): Can produce Verilog for mapped or AIG netlists only.\n");
    return;
  }

  gen_primary_io_from_abc(new_graph, old_graph, pNtk);
  gen_comb_cell_from_abc(new_graph, old_graph, pNtk);
  gen_latch_from_abc(new_graph, old_graph, pNtk);
  gen_memory_from_abc(new_graph, old_graph, pNtk);
  gen_subgraph_from_abc(new_graph, old_graph, pNtk);

  conn_latch(new_graph, old_graph, pNtk);
  conn_primary_output(new_graph, old_graph, pNtk);
  conn_combinational_cell(new_graph, old_graph, pNtk);
}

void Pass_abc::gen_primary_io_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
  assert(old_graph);
  Abc_Obj_t *pTerm = nullptr, *pNet = nullptr;
  int        i = 0;
  Abc_NtkForEachPi(pNtk, pTerm, i) {
    pNet = Abc_ObjFanout0(pTerm);
    std::string input_name(((Abc_ObjName(pNet))));
    if(input_name[0] == '%' && input_name[input_name.size() - 1] == '%') {
      continue;
    } else {
      Index_ID io_idx = new_graph->add_graph_input(Abc_ObjName(pNet), 0, 1, 0);
      new_graph->set_node_wirename(io_idx, Abc_ObjName(pNet));
      new_graph->set_bits(io_idx, 1);
      new_graph->node_type_set(io_idx, GraphIO_Op);
      graph_info->cell2id[pNet]               = io_idx;
      graph_info->cell_out_pid[io_idx]        = 0;
      graph_info->io_remap[Abc_ObjName(pNet)] = io_idx;
    }

    if(graph_info->skew_group_map.find(input_name) != graph_info->skew_group_map.end()) {
      graph_info->ck_remap[input_name] = graph_info->cell2id[pNet];
    }
    if(graph_info->reset_group_map.find(input_name) != graph_info->reset_group_map.end()) {
      graph_info->rst_remap[input_name] = graph_info->cell2id[pNet];
    }
  }

  Abc_NtkForEachPo(pNtk, pTerm, i) {
    pNet = Abc_ObjFanin0(pTerm);
    std::string output_name(((Abc_ObjName(pNet))));
    if(output_name[0] == '%' && output_name[output_name.size() - 1] == '%') {
      continue;
    } else {
      Index_ID io_idx = new_graph->add_graph_output(Abc_ObjName(pNet), 0, 1, 0);
      new_graph->set_node_wirename(io_idx, Abc_ObjName(pNet));
      new_graph->set_bits(io_idx, 1);
      new_graph->node_type_set(io_idx, GraphIO_Op);
      graph_info->cell2id[pTerm]              = io_idx;
      graph_info->cell_out_pid[io_idx]        = 0;
      graph_info->io_remap[Abc_ObjName(pNet)] = io_idx;
    }

    if(graph_info->skew_group_map.find(output_name) != graph_info->skew_group_map.end()) {
      graph_info->ck_remap[output_name] = graph_info->cell2id[pTerm];
    }
    if(graph_info->reset_group_map.find(output_name) != graph_info->reset_group_map.end()) {
      graph_info->rst_remap[output_name] = graph_info->cell2id[pTerm];
    }
  }
}

void Pass_abc::gen_comb_cell_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
  assert(old_graph);
  const Tech_library &tlib  = new_graph->get_tlibrary();
  const Tech_cell *   tcell = nullptr;
  Abc_Obj_t *         pObj  = nullptr;
  int                 i, k = 0;
  Index_ID            cell_idx = 0;

  if(Abc_NtkHasMapping(pNtk)) {
    Abc_NtkForEachNode(pNtk, pObj, k) {
      bool       constnode = false;
      auto *     pGate     = (Mio_Gate_t *)pObj->pData;
      Mio_Pin_t *pGatePin;

      std::string gate_name(Mio_GateReadName(pGate));

      for(pGatePin = Mio_GateReadPins(pGate), i = 0; pGatePin; pGatePin = Mio_PinReadNext(pGatePin), i++) {
        // std::string fanin_pin_name((Mio_PinReadName(pGatePin)));
        std::string fanin_name((Abc_ObjName(Abc_ObjFanin(pObj, i))));
      }
      // std::string fanout_pin_name((Mio_GateReadOutName(pGate)));
      // std::string fanout_name((Abc_ObjName(Abc_ObjFanout0(pObj))));
      // std::string gate_instance_name(Abc_ObjName(pObj));

      cell_idx = new_graph->create_node().get_nid();
      new_graph->set_bits(cell_idx, 1);

      if(opack.liberty_file.empty()) {
        if(gate_name == ("BUF"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_BUF_"));
        else if(gate_name == ("NOT"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_NOT_"));
        else if(gate_name == ("AND"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_AND_"));
        else if(gate_name == ("NAND"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_NAND_"));
        else if(gate_name == ("OR"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_OR_"));
        else if(gate_name == ("NOR"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_NOR_"));
        else if(gate_name == ("XOR"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_XOR_"));
        else if(gate_name == ("XNOR"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_XNOR_"));
        else if(gate_name == ("ANDNOT"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_ANDNOT_"));
        else if(gate_name == ("ORNOT"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_ORNOT_"));
        else if(gate_name == ("AOI3"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_AOI3_"));
        else if(gate_name == ("OAI3"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_OAI3_"));
        else if(gate_name == ("AOI4"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_AOI4_"));
        else if(gate_name == ("OAI4"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_OAI4_"));
        else if(gate_name == ("MUX"))
          tcell = tlib.get_const_cell(tlib.get_cell_id("$_MUX_"));
        else if(gate_name == ("ONE")) {
          constnode = true;
        } else if(gate_name == ("ZERO")) {
          constnode = true;
        } else
          assert(false);
      } else
        tcell = tlib.get_const_cell(tlib.get_cell_id(gate_name));

      if(constnode) {
        std::string op = (gate_name == ("ONE")) ? "1" : "0";
        new_graph->node_const_type_set(cell_idx, op);
      } else {
        new_graph->node_tmap_set(cell_idx, tcell->get_id());
      }
      new_graph->set_bits(cell_idx, 1);
      graph_info->cell2id[Abc_ObjFanout0(pObj)] = cell_idx; // remember the fanout net of std_cell
      graph_info->cell_out_pid[cell_idx]        = 0;        // set the initial output pid to 0
    }
  }
}

void Pass_abc::gen_latch_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
  assert(old_graph);
  Abc_Obj_t *pLatch = nullptr;
  int        i      = 0;
  Abc_NtkForEachLatch(pNtk, pLatch, i) {
    Abc_Obj_t * pNet = Abc_ObjFanout0(Abc_ObjFanout0(pLatch));
    std::string latch_name(Abc_ObjName(pNet));
    Index_ID    cell_idx = new_graph->create_node().get_nid();
    new_graph->set_bits(cell_idx, 1);
    auto tcell = old_graph->get_tlibrary().get_const_cell(old_graph->tmap_id_get(graph_info->latchname2id[latch_name]));
    new_graph->node_tmap_set(cell_idx, tcell->get_id());
    graph_info->cell2id[pNet]          = cell_idx;
    graph_info->cell_out_pid[cell_idx] = 0;
    new_graph->set_bits(cell_idx, 1);
  }
}

void Pass_abc::gen_memory_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
  assert(old_graph);
  for(const auto &idx : graph_info->memory_id) {
    Index_ID new_memory_idx       = new_graph->create_node().get_nid();
    graph_info->memory_remap[idx] = new_memory_idx;
    new_graph->node_type_set(new_memory_idx, Memory_Op);
    new_graph->set_node_wirename(new_memory_idx, old_graph->get_node_wirename(idx));
  }

  Abc_Obj_t *pTerm = nullptr, *pNet = nullptr;

  std::map<index_offset, Abc_Obj_t *> memory_input_map;
  int                                 i = 0;
  Abc_NtkForEachPo(pNtk, pTerm, i) {
    pNet = Abc_ObjFanin0(pTerm);
    std::string output_name(((Abc_ObjName(pNet))));
    if(output_name.substr(0, 14) == "%memory_input_") {
      std::regex  trap("%memory_input_(\\d++)_(\\d++)_(\\d++)%");
      std::smatch memory_info;
      if(std::regex_search(output_name, memory_info, trap)) {
        assert(memory_info.size() - 1 == 3); // disregard the 0th capture
        Index_ID old_memory_idx = std::stol(memory_info[1]);
        Port_ID  old_inp_pid    = static_cast<Port_ID>(std::stol(memory_info[2]));
        Port_ID  old_offset     = static_cast<Port_ID>(std::stol(memory_info[3]));

        Index_ID new_memory_idx = graph_info->memory_remap[old_memory_idx];
        if(LGRAPH_MEMOP_ISWREN(old_inp_pid) || LGRAPH_MEMOP_ISRDEN(old_inp_pid)) {
          new_graph->add_edge(Node_Pin(graph_info->cell2id[pNet], graph_info->cell_out_pid[graph_info->cell2id[pNet]]++, false),
                              Node_Pin(new_memory_idx, old_inp_pid, true));
        } else {
          index_offset info      = {new_memory_idx, old_inp_pid, {old_offset, old_offset}};
          memory_input_map[info] = pNet;
        }
      }
    }
  }

  std::map<index_offset, Abc_Obj_t *> memory_output_map;
  Abc_NtkForEachPi(pNtk, pTerm, i) {
    pNet = Abc_ObjFanout0(pTerm);
    std::string input_name(((Abc_ObjName(pNet))));
    if(input_name.substr(0, 15) == "%memory_output_") {
      std::regex  trap("%memory_output_(\\d++)_(\\d++)_(\\d++)%");
      std::smatch memory_info;
      if(std::regex_search(input_name, memory_info, trap)) {
        assert(memory_info.size() - 1 == 3); // disregard the 0th capture
        Index_ID old_memory_idx    = std::stol(memory_info[1]);
        Port_ID  old_memory_outpid = static_cast<Port_ID>(std::stol(memory_info[2]));
        Port_ID  old_offset        = static_cast<Port_ID>(std::stol(memory_info[3]));

        index_offset info       = {graph_info->memory_remap[old_memory_idx], old_memory_outpid, {old_offset, old_offset}};
        memory_output_map[info] = pNet;
      }
    }
  }

  for(const auto &old_idx : graph_info->memory_id) {
    Index_ID new_memory_idx = graph_info->memory_remap[old_idx];

    for(const auto &input : old_graph->inp_edges(old_idx)) {
      Port_ID old_inp_pid = input.get_inp_pin().get_pid();
      if(old_inp_pid < LGRAPH_MEMOP_CLK) {
        auto node_idx = input.get_idx();
        auto width    = old_graph->get_bits(node_idx);
        auto val      = 0;
        if(old_graph->node_type_get(node_idx).op == U32Const_Op) {
          val = old_graph->node_value_get(node_idx);
        }
        connect_constant(new_graph, val, width, Node_Pin(new_memory_idx, old_inp_pid, true));
      } else if(old_inp_pid == LGRAPH_MEMOP_CLK) {
        for(const auto &sg : graph_info->skew_group_map) {
          if(sg.second.find(old_idx) != sg.second.end()) {
            std::string ck_name = sg.first;
            new_graph->add_edge(
                Node_Pin(graph_info->ck_remap[ck_name], graph_info->cell_out_pid[graph_info->ck_remap[ck_name]]++, false),
                Node_Pin(new_memory_idx, LGRAPH_MEMOP_CLK, true));
          }
        }
      } else if(old_inp_pid == LGRAPH_MEMOP_CE) {
        new_graph->add_edge(Node_Pin(graph_info->cell2id[pNet], graph_info->cell_out_pid[graph_info->cell2id[pNet]]++, false),
                            Node_Pin(new_memory_idx, old_inp_pid, true));
      } else if(LGRAPH_MEMOP_ISWRADDR(old_inp_pid) || LGRAPH_MEMOP_ISWRDATA(old_inp_pid) || LGRAPH_MEMOP_ISRDADDR(old_inp_pid)) {
        auto     inp_info = graph_info->memory_conn[old_idx][old_inp_pid];
        auto     size     = inp_info.size();
        Index_ID join_id  = new_graph->create_node().get_nid();
        new_graph->node_type_set(join_id, Join_Op);
        new_graph->set_bits(join_id, size);
        auto src_pin = Node_Pin(join_id, 0, false);
        auto dst_pin = Node_Pin(new_memory_idx, old_inp_pid, true);
        new_graph->add_edge(src_pin, dst_pin);

        for(size_t offset = 0; offset < size; ++offset) {
          index_offset info = {new_memory_idx, old_inp_pid, {static_cast<int>(offset), static_cast<int>(offset)}};
          auto *       pObj = memory_input_map[info];
          new_graph->add_edge(Node_Pin(graph_info->cell2id[pObj], graph_info->cell_out_pid[graph_info->cell2id[pObj]]++, false),
                              Node_Pin(join_id, offset, true));
        }
      }
    }

    for(const auto &out : old_graph->out_edges(old_idx)) {
      auto     out_pid  = out.get_out_pin().get_pid();
      auto     node     = old_graph->get_dest_node(out);
      auto     width    = node.get_bits();
      Node_Pin pick_pin = create_pick_operator(new_graph, Node_Pin(new_memory_idx, out_pid, false), 0, width);

      Index_ID port_nid = new_graph->get_idx_from_pid(new_memory_idx, out_pid);
      new_graph->set_bits(port_nid, width);

      for(int offset = 0; offset < width; ++offset) {
        Node_Pin     pseudo_pin = create_pick_operator(new_graph, Node_Pin(pick_pin.get_nid(), offset, false), offset, 1);
        index_offset key        = {new_memory_idx, out_pid, {offset, offset}};
        graph_info->cell2id[memory_output_map[key]]    = pseudo_pin.get_nid();
        graph_info->cell_out_pid[pseudo_pin.get_nid()] = 0;
      }
    }
  }
}

void Pass_abc::gen_subgraph_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
  assert(old_graph);
  for(const auto &idx : graph_info->subgraph_id) {

    std::string subgraph_name(old_graph->get_library().get_name(old_graph->subgraph_id_get(idx)));
    Index_ID    new_subgraph_idx    = new_graph->create_node().get_nid();
    graph_info->subgraph_remap[idx] = new_subgraph_idx;
    new_graph->node_type_set(new_subgraph_idx, SubGraph_Op);
    new_graph->set_node_instance_name(new_subgraph_idx, old_graph->get_node_instancename(idx));

    LGraph *sub_graph = LGraph::open(old_graph->get_path(), subgraph_name);
    new_graph->node_subgraph_set(new_subgraph_idx, sub_graph->lg_id());
  }
  Abc_Obj_t *pTerm = nullptr, *pNet = nullptr;

  std::map<index_offset, Abc_Obj_t *> subgraph_input_map;

  int i = 0;
  Abc_NtkForEachPo(pNtk, pTerm, i) {
    pNet = Abc_ObjFanin0(pTerm);
    std::string output_name(((Abc_ObjName(pNet))));
    if(output_name.substr(0, 16) == "%subgraph_input_") {
      std::regex  trap("%subgraph_input_(\\d++)_(\\d++)_(\\d++)%");
      std::smatch subgraph_info;
      if(std::regex_search(output_name, subgraph_info, trap)) {
        assert(subgraph_info.size() - 1 == 3); // disregard the 0th capture
        Index_ID     old_subgraph_idx = std::stol(subgraph_info[1]);
        Port_ID      old_inp_pid      = static_cast<Port_ID>(std::stol(subgraph_info[2]));
        Port_ID      old_offset       = static_cast<Port_ID>(std::stol(subgraph_info[3]));
        index_offset info             = {graph_info->subgraph_remap[old_subgraph_idx], old_inp_pid, {old_offset, old_offset}};
        subgraph_input_map[info]      = pNet;
      }
    }
  }

  std::map<index_offset, Abc_Obj_t *> subgraph_output_map;
  Abc_NtkForEachPi(pNtk, pTerm, i) {
    pNet = Abc_ObjFanout0(pTerm);
    std::string input_name(((Abc_ObjName(pNet))));
    if(input_name.substr(0, 17) == "%subgraph_output_") {
      std::regex  trap("%subgraph_output_(\\d++)_(\\d++)_(\\d++)%");
      std::smatch subgraph_info;
      if(std::regex_search(input_name, subgraph_info, trap)) {
        assert(subgraph_info.size() - 1 == 3); // disregard the 0th capture
        Index_ID old_subgraph_idx = std::stol(subgraph_info[1]);
        Port_ID  old_inp_pid      = static_cast<Port_ID>(std::stol(subgraph_info[2]));
        Port_ID  old_offset       = static_cast<Port_ID>(std::stol(subgraph_info[3]));

        index_offset info         = {graph_info->subgraph_remap[old_subgraph_idx], old_inp_pid, {old_offset, old_offset}};
        subgraph_output_map[info] = pNet;
      }
    }
  }

  for(const auto &old_idx : graph_info->subgraph_id) {
    Index_ID new_subgraph_idx = graph_info->subgraph_remap[old_idx];
    for(const auto &input : old_graph->inp_edges(old_idx)) {
      Port_ID  old_inp_pid = input.get_inp_pin().get_pid();
      auto     inp_info    = graph_info->subgraph_conn[old_idx][old_inp_pid];
      auto     size        = inp_info.size();
      Index_ID join_id     = new_graph->create_node().get_nid();
      new_graph->node_type_set(join_id, Join_Op);
      new_graph->set_bits(join_id, size);
      auto src_pin = Node_Pin(join_id, 0, false);
      auto dst_pin = Node_Pin(new_subgraph_idx, old_inp_pid, true);
      new_graph->add_edge(src_pin, dst_pin);
      for(size_t offset = 0; offset < size; ++offset) {
        index_offset info = {new_subgraph_idx, old_inp_pid, {static_cast<int>(offset), static_cast<int>(offset)}};
        auto *       pObj = subgraph_input_map[info];
        new_graph->add_edge(Node_Pin(graph_info->cell2id[pObj], graph_info->cell_out_pid[graph_info->cell2id[pObj]]++, false),
                            Node_Pin(join_id, offset, true));
      }
    }
    for(const auto &out : old_graph->out_edges(old_idx)) {
      auto     out_pid  = out.get_out_pin().get_pid();
      auto     width    = old_graph->get_bits(out.get_out_pin().get_nid());
      Node_Pin pick_pin = create_pick_operator(new_graph, Node_Pin(new_subgraph_idx, out_pid, false), 0, width);
      for(int offset = 0; offset < width; ++offset) {
        Node_Pin     pseudo_pin = create_pick_operator(new_graph, Node_Pin(pick_pin.get_nid(), offset, false), offset, 1);
        index_offset key        = {new_subgraph_idx, out_pid, {static_cast<int>(offset), static_cast<int>(offset)}};
        graph_info->cell2id[subgraph_output_map[key]]  = pseudo_pin.get_nid();
        graph_info->cell_out_pid[pseudo_pin.get_nid()] = 0;
      }
    }
  }
}

void Pass_abc::conn_latch(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
  Abc_Obj_t *pLatch = nullptr;
  int        i      = 0;
  Abc_NtkForEachLatch(pNtk, pLatch, i) {
    Index_ID         latch_new_idx = graph_info->cell2id[Abc_ObjFanout0(Abc_ObjFanout0(pLatch))];
    const Tech_cell *tcell         = new_graph->get_tlibrary().get_const_cell(new_graph->tmap_id_get(latch_new_idx));
    std::string      trig_pin      = tcell->pin_name_exist("C") ? "C" : "E";
    Abc_Obj_t *      pNode         = Abc_ObjFanin0(Abc_ObjFanin0(pLatch));

    new_graph->add_edge(Node_Pin(graph_info->cell2id[pNode], graph_info->cell_out_pid[graph_info->cell2id[pNode]]++, false),
                        Node_Pin(latch_new_idx, tcell->get_pin_id("D"), true));

    Abc_Obj_t * pNet = Abc_ObjFanout0(Abc_ObjFanout0(pLatch));
    std::string latch_name(Abc_ObjName(pNet));
    std::string flop_name(latch_name);
    Index_ID    latch_old_idx = graph_info->latchname2id[flop_name];
    for(const auto &sg : graph_info->skew_group_map) {
      if(sg.second.find(latch_old_idx) != sg.second.end()) {
        std::string ck_name = sg.first;
        if(new_graph->is_graph_input(graph_info->ck_remap[ck_name])) {
          new_graph->add_edge(Node_Pin(graph_info->ck_remap[ck_name], 0, false),
                              Node_Pin(latch_new_idx, tcell->get_pin_id(trig_pin), true));
        } else {
          new_graph->add_edge(
              Node_Pin(graph_info->ck_remap[ck_name], graph_info->cell_out_pid[graph_info->ck_remap[ck_name]]++, false),
              Node_Pin(latch_new_idx, tcell->get_pin_id(trig_pin), true));
        }
      }
    }
    for(const auto &rg : graph_info->reset_group_map) {
      if(rg.second.find(latch_old_idx) != rg.second.end()) {
        std::string rst_name = rg.first;
        new_graph->add_edge(
            Node_Pin(graph_info->rst_remap[rst_name], graph_info->cell_out_pid[graph_info->rst_remap[rst_name]]++, false),
            Node_Pin(latch_new_idx, tcell->get_pin_id("R"), true));
      }
    }

    std::regex  trap("(.*)_(%r)(.*)");
    std::smatch flop_name_info;
    std::regex_search(latch_name, flop_name_info, trap);
    std::string reg_base_name(flop_name_info[1]);
    std::string reg_offset(flop_name_info[3]);
    std::string reg_name =
        reg_offset.empty() ? reg_base_name : reg_base_name + "[" + (reg_offset.substr(1, reg_offset.length())) + "]";

    new_graph->set_node_wirename(latch_new_idx, reg_name.c_str());
  }
}

void Pass_abc::conn_primary_output(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
  Abc_Obj_t *pTerm = nullptr;
  int        i     = 0;
  Abc_NtkForEachPo(pNtk, pTerm, i) {
    Abc_Obj_t * pNet = Abc_ObjFanin0(pTerm);
    std::string output_name(Abc_ObjName(pNet));

    if(output_name[0] == '%' && output_name[output_name.size() - 1] == '%') {
      continue;
    } else if(graph_info->skew_group_map.find(output_name) != graph_info->skew_group_map.end()) {
      continue;
    } else if(graph_info->reset_group_map.find(output_name) != graph_info->reset_group_map.end()) {
      continue;
    } else {
      new_graph->add_edge(Node_Pin(graph_info->cell2id[pNet], graph_info->cell_out_pid[graph_info->cell2id[pNet]]++, false),
                          Node_Pin(graph_info->cell2id[pTerm], 0, true));
    }
  }
}

void Pass_abc::conn_combinational_cell(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
  Abc_Obj_t *pObj = nullptr;
  int        k    = 0;
  Abc_NtkForEachNode(pNtk, pObj, k) {
    int         i;
    Mio_Gate_t *pGate = (Mio_Gate_t *)pObj->pData;
    Mio_Pin_t * pGatePin;
    Port_ID     inpid = 0;
    for(pGatePin = Mio_GateReadPins(pGate), i = 0; pGatePin; pGatePin = Mio_PinReadNext(pGatePin), i++) {
      // std::string fanin_pin_name((Mio_PinReadName(pGatePin)));
      Abc_Obj_t *pNet = Abc_ObjFanin(pObj, i);
      if(new_graph->is_graph_input(graph_info->cell2id[pNet])) {
        new_graph->add_edge(Node_Pin(graph_info->cell2id[pNet], 0, false),
                            Node_Pin(graph_info->cell2id[Abc_ObjFanout0(pObj)], inpid++, true));
      } else {
        new_graph->add_edge(Node_Pin(graph_info->cell2id[pNet], graph_info->cell_out_pid[graph_info->cell2id[pNet]]++, false),
                            Node_Pin(graph_info->cell2id[Abc_ObjFanout0(pObj)], inpid++, true));
      }
    }
  }
}

void Pass_abc::connect_constant(LGraph *g, uint32_t value, uint32_t size, const Node_Pin &dst) {
  Index_ID const_idx;
  if(graph_info->int_const_map.end() == graph_info->int_const_map.find(std::make_pair(value, size))) {
    const_idx = g->create_node().get_nid();
    g->node_u32type_set(const_idx, value);
    g->set_bits(const_idx, size);
    graph_info->int_const_map[std::make_pair(value, size)] = const_idx;
  } else {
    const_idx = graph_info->int_const_map[std::make_pair(value, size)];
  }
  Node_Pin const_pin(const_idx, 0, false);
  g->add_edge(const_pin, dst);
}

Node_Pin Pass_abc::create_pick_operator(LGraph *g, const Node_Pin &driver, int offset, int width) {
  if(offset == 0 && g->get_bits_pid(driver.get_nid(), driver.get_pid()) == width)
    return driver;

  Pick_ID pick_id(driver, offset, width);
  if(graph_info->picks.find(pick_id) != graph_info->picks.end())
    return graph_info->picks.at(pick_id);

  Index_ID pick_nid = g->create_node().get_nid();
  g->node_type_set(pick_nid, Pick_Op);
  g->set_bits(pick_nid, width);
  g->add_edge(driver, Node_Pin(pick_nid, 0, true));
  connect_constant(g, offset, 32, Node_Pin(pick_nid, 1, true));
  graph_info->picks.insert(std::make_pair(pick_id, Node_Pin(pick_nid, 0, false)));
  return graph_info->picks.at(pick_id);
}