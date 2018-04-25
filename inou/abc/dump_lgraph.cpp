//
// Created by birdeclipse on 2/27/18.
//
#include "inou_abc.hpp"
#include <regex>


void Inou_abc::connect_constant(LGraph *g, uint32_t value, uint32_t size, Index_ID onid, Port_ID opid) {
	Index_ID const_nid;
	if (int_const_map.find(std::make_pair(value, size)) == int_const_map.end()) {
		const_nid = g->create_node().get_nid();
		g->node_u32type_set(const_nid, value);
		g->set_bits(const_nid, size);
		int_const_map[std::make_pair(value, size)] = const_nid;
	}
	else {
		const_nid = int_const_map[std::make_pair(value, size)];
	}
	Node_Pin const_pin(const_nid, 0, false);
	g->add_edge(const_pin, Node_Pin(onid, opid, true));
}

void Inou_abc::gen_comb_cell_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	assert(old_graph);
	const Tech_library *tlib = new_graph->get_tlibrary();
	const Tech_cell *tcell = nullptr;
	Abc_Obj_t *pObj;
	int i, k = 0;
	Index_ID cell_idx = 0;

	if (Abc_NtkHasMapping(pNtk)) {
		Abc_NtkForEachNode(pNtk, pObj, k) {
				bool constnode = false;
				Mio_Gate_t *pGate = (Mio_Gate_t *) pObj->pData;
				Mio_Pin_t *pGatePin;

				std::string gate_name(Mio_GateReadName(pGate));
				for (pGatePin = Mio_GateReadPins(pGate), i = 0; pGatePin; pGatePin = Mio_PinReadNext(pGatePin), i++) {
					std::string fanin_pin_name((Mio_PinReadName(pGatePin)));
					std::string fanin_name((Abc_ObjName(Abc_ObjFanin(pObj, i))));
				}
				std::string fanout_pin_name((Mio_GateReadOutName(pGate)));
				std::string fanout_name((Abc_ObjName(Abc_ObjFanout0(pObj))));

				cell_idx = new_graph->create_node().get_nid();
				new_graph->set_bits(cell_idx, 1);

				if (opack.liberty_file.empty()) {
					if (gate_name == ("BUF"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_BUF_"));
					else if (gate_name == ("NOT"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_NOT_"));
					else if (gate_name == ("AND"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_AND_"));
					else if (gate_name == ("NAND"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_NAND_"));
					else if (gate_name == ("OR"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_OR_"));
					else if (gate_name == ("NOR"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_NOR_"));
					else if (gate_name == ("XOR"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_XOR_"));
					else if (gate_name == ("XNOR"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_XNOR_"));
					else if (gate_name == ("ANDNOT"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_ANDNOT_"));
					else if (gate_name == ("ORNOT"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_ORNOT_"));
					else if (gate_name == ("AOI3"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_AOI3_"));
					else if (gate_name == ("OAI3"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_OAI3_"));
					else if (gate_name == ("AOI4"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_AOI4_"));
					else if (gate_name == ("OAI4"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_OAI4_"));
					else if (gate_name == ("MUX"))
						tcell = tlib->get_const_cell(tlib->get_cell_id("$_MUX_"));
					else if (gate_name == ("ONE")) {
						constnode = true;
					}
					else if (gate_name == ("ZERO")) {
						constnode = true;
					}
					else
						assert(false);
				}
				else
					tcell = tlib->get_const_cell(tlib->get_cell_id(gate_name));

				if (constnode) {
					std::string op = (gate_name == ("ONE")) ? "1" : "0";
					new_graph->node_const_type_set(cell_idx, op);
				}
				else {
					new_graph->node_tmap_set(cell_idx, tcell->get_id());
				}
				new_graph->set_bits(cell_idx, 1);
				cell2id[Abc_ObjFanout0(pObj)] = cell_idx; // remember the fanout net of std_cell
				cell_out_pid[cell_idx] = 0;               // set the initial output pid to 0
			}
	}
	else {
		assert(false);
	}
}

void Inou_abc::gen_latch_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	assert(old_graph);
	Abc_Obj_t *pNet = nullptr;
	Abc_Obj_t *pLatch = nullptr;
	int i;
	Abc_NtkForEachLatch(pNtk, pLatch, i) {
			pNet = Abc_ObjFanout0(Abc_ObjFanout0(pLatch));
			std::string latch_name(Abc_ObjName(pNet));
			Index_ID cell_idx = new_graph->create_node().get_nid();
			new_graph->set_bits(cell_idx, 1);
			auto tcell = old_graph->get_tlibrary()->get_const_cell(old_graph->tmap_id_get(latchname2id[latch_name]));
			new_graph->node_tmap_set(cell_idx, tcell->get_id());
			cell2id[pNet] = cell_idx;   // remember the fanout net of Q pin of the latch
			cell_out_pid[cell_idx] = 0; // set the initial output pid to 0
			new_graph->set_node_wirename(cell_idx, Abc_ObjName(pNet));
			new_graph->set_bits(cell_idx, 1);
		}
}

void Inou_abc::gen_primary_io_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	assert(old_graph);
	Abc_Obj_t *pTerm, *pNet;
	int i;
	Abc_NtkForEachPi(pNtk, pTerm, i) {
		pNet = Abc_ObjFanout0(pTerm);
		std::string input_name(((Abc_ObjName(pNet))));
		if (input_name[0] == '%' && input_name[input_name.size() - 1] == '%') {
			continue;
		}
		else {
			Index_ID io_idx = new_graph->add_graph_input(Abc_ObjName(pNet), 0, 1, 0);
			new_graph->set_node_wirename(io_idx, Abc_ObjName(pNet));
			new_graph->set_bits(io_idx, 1);
			new_graph->node_type_set(io_idx, GraphIO_Op);
			cell2id[pNet] = io_idx;    // remember the fanout net of input Terminal
			cell_out_pid[io_idx] = 0;  // set the initial output pid to 0
			io_remap[Abc_ObjName(pNet)] = io_idx;
		}
		if (skew_group_map.find(input_name) != skew_group_map.end()) {
			ck_remap[input_name] = cell2id[pNet];
		}
		else if (reset_group_map.find(input_name) != reset_group_map.end()) {
			rst_remap[input_name] = cell2id[pNet];
		}
	}

	Abc_NtkForEachPo(pNtk, pTerm, i) {
		pNet = Abc_ObjFanin0(pTerm);
		std::string output_name(((Abc_ObjName(pNet))));
		if (output_name[0] == '%' && output_name[output_name.size() - 1] == '%') {
			continue;
		}
		else if (skew_group_map.find(output_name) != skew_group_map.end()) {
			ck_remap[output_name] = cell2id[pNet];
			continue;
		}
		else if (reset_group_map.find(output_name) != reset_group_map.end()) {
			rst_remap[output_name] = cell2id[pNet];
			continue;
		}
		else {
			Index_ID io_idx = new_graph->add_graph_output(Abc_ObjName(pNet), 0, 1, 0);
			new_graph->set_node_wirename(io_idx, Abc_ObjName(pNet));
			new_graph->set_bits(io_idx, 1);
			new_graph->node_type_set(io_idx, GraphIO_Op);
			cell2id[pTerm] = io_idx;  // remember the output Terminal
			cell_out_pid[io_idx] = 0; // set the initial output pid to 0
			io_remap[Abc_ObjName(pNet)] = io_idx;
		}
	}
}

void Inou_abc::gen_subgraph_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	assert(old_graph);
	for (const auto &old_idx : subgraph_id) {
		std::string subgraph_name(old_graph->get_library()->get_name(old_graph->subgraph_id_get(old_idx)));
		LGraph *sub_graph = LGraph::find_graph(subgraph_name, old_graph->get_path());
		Index_ID new_idx = new_graph->create_node().get_nid();
		new_graph->set_node_instance_name(new_idx, old_graph->get_node_instancename(old_idx));
		new_graph->node_subgraph_set(new_idx, sub_graph->lg_id());
		subgraph_remap[old_idx] = new_idx; // remap the old subgraph to new subgraph

		for (const auto &output : old_graph->out_edges(old_idx)) {
			auto node_idx = output.get_idx();
			auto width = old_graph->get_bits(node_idx);
			auto join_id = new_graph->create_node().get_nid();
			auto pid = output.get_out_pin().get_pid();
			new_graph->node_type_set(join_id, Join_Op);
			new_graph->set_bits(join_id, width);
			new_graph->add_edge(Node_Pin(new_idx, pid, false),
			                    Node_Pin(join_id, 0, true));
			subgraph_outpid_remap[new_idx][pid] = join_id;
		}

		for (const auto &input : old_graph->inp_edges(old_idx)) {
			auto node_idx = input.get_idx();
			auto width = old_graph->get_bits(node_idx);
			auto join_id = new_graph->create_node().get_nid();
			auto pid = input.get_inp_pin().get_pid();
			new_graph->node_type_set(join_id, Join_Op);
			new_graph->add_edge(Node_Pin(join_id, 0, false),
			                    Node_Pin(new_idx, pid, true));
			new_graph->set_bits(join_id, width);
			subgraph_inpid_remap[new_idx][pid] = join_id;


			topology_info old_sub = subgraph_conn[old_idx][pid];
			for (const auto &i : old_sub) {
				char namebuffer[255];
				if (old_graph->is_graph_input(i.idx)) {
					if (old_sub.size() > 1) {
						sprintf(namebuffer, "%s[%d]", old_graph->get_graph_input_name(i.idx), i.offset[0]);
						std::string input_name(namebuffer);
						if (io_remap.find(input_name) != io_remap.end()) {
							auto new_io_idx = io_remap[input_name];
							new_graph->add_edge(Node_Pin(new_io_idx, cell_out_pid[new_io_idx]++, false),
							                    Node_Pin(join_id, i.offset[0], true));
						}
					}
					else {
						sprintf(namebuffer, "%s", old_graph->get_graph_input_name(i.idx));
						std::string input_name(namebuffer);
						if (io_remap.find(input_name) != io_remap.end()) {
							auto new_io_idx = io_remap[input_name];
							new_graph->add_edge(Node_Pin(new_io_idx, cell_out_pid[new_io_idx]++, false),
							                    Node_Pin(join_id, 0, true));
						}
					}
				}
			}
		}
	}
}

void Inou_abc::from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	// 1. create all standard cell
	// 2. create all latches
	// 3. create all input output
	// 4. create all subgraphs
	if (!Abc_NtkIsAigNetlist(pNtk) && !Abc_NtkIsMappedNetlist(pNtk)) {
		printf("Io_WriteVerilog(): Can produce Verilog for mapped or AIG netlists only.\n");
		return;
	}
	gen_comb_cell_from_abc(new_graph, old_graph, pNtk);
	gen_latch_from_abc(new_graph, old_graph, pNtk);
	gen_primary_io_from_abc(new_graph, old_graph, pNtk);
	gen_subgraph_from_abc(new_graph, old_graph, pNtk);

	conn_latch(new_graph, old_graph, pNtk);
	conn_primary_output(new_graph, old_graph, pNtk);
	conn_combinational_cell(new_graph, old_graph, pNtk);
	conn_subgraph(new_graph, old_graph, pNtk);
}

void Inou_abc::conn_latch(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	Abc_Obj_t *pNode, *pLatch;
	int i;
	Abc_NtkForEachLatch(pNtk, pLatch, i) {
			Index_ID latch_new_idx = cell2id[Abc_ObjFanout0(Abc_ObjFanout0(pLatch))];
			const Tech_cell *tcell = new_graph->get_tlibrary()->get_const_cell(new_graph->tmap_id_get(latch_new_idx));
			pNode = Abc_ObjFanin0(Abc_ObjFanin0(pLatch));
			std::string name(Abc_ObjName(pNode));

			if (name[0] == '%' && name[name.size() - 1] == '%') {
				std::regex trap("%subgraph_output_(\\d++)_(\\d++)_(\\d++)%");
				std::smatch subgraph_info;
				if (std::regex_search(name, subgraph_info, trap)) {
					assert(subgraph_info.size() - 1 == 3); // disregard the 0th capture
					Index_ID subgraph_old_idx = std::stol(subgraph_info[1]);
					Port_ID subgraph_outpid = static_cast<Port_ID>(std::stol(subgraph_info[2]));
					Port_ID offset = static_cast<Port_ID>(std::stol(subgraph_info[3]));
					//TODO
					index_offset info = {subgraph_old_idx, subgraph_outpid, {offset, offset}};
					Index_ID subgraph_new_idx = subgraph_remap[subgraph_old_idx];
					Index_ID port_join_idx = subgraph_outpid_remap[subgraph_new_idx][subgraph_outpid];
					// push some info for furthur processing
					if (pickop_map.find(info) == pickop_map.end()) {
						Index_ID pick_nid = new_graph->create_node().get_nid();
						cell_out_pid[pick_nid] = 0;
						new_graph->node_type_set(pick_nid, Pick_Op);
						new_graph->set_bits(pick_nid, 1);
						new_graph->add_edge(Node_Pin(port_join_idx, offset, false),
						                    Node_Pin(pick_nid, 0, true));
						connect_constant(new_graph, offset, 32, pick_nid, 1);

						new_graph->add_edge(Node_Pin(pick_nid, cell_out_pid[pick_nid]++, false),
						                    Node_Pin(latch_new_idx, tcell->get_pin_id("D"), true));
					}
					else {
						new_graph->add_edge(Node_Pin(pickop_map[info], cell_out_pid[pickop_map[info]]++, false),
						                    Node_Pin(latch_new_idx, tcell->get_pin_id("D"), true));
					}
				}
			}
			else {
				new_graph->add_edge(Node_Pin(cell2id[pNode], cell_out_pid[cell2id[pNode]]++, false),
				                    Node_Pin(latch_new_idx, tcell->get_pin_id("D"), true));
			}

			std::string flop_name(new_graph->get_node_wirename(latch_new_idx));
			Index_ID latch_old_idx = latchname2id[flop_name];
			for (const auto &sg : skew_group_map) {
				if (sg.second.find(latch_old_idx) != sg.second.end()) {
					std::string ck_name = sg.first;
					new_graph->add_edge(Node_Pin(ck_remap[ck_name], cell_out_pid[ck_remap[ck_name]]++, false),
					                    Node_Pin(latch_new_idx, tcell->get_pin_id("C"), true));
				}
			}
			for (const auto &rg : reset_group_map) {
				if (rg.second.find(latch_old_idx) != rg.second.end()) {
					std::string rst_name = rg.first;
					new_graph->add_edge(Node_Pin(rst_remap[rst_name], cell_out_pid[rst_remap[rst_name]]++, false),
					                    Node_Pin(latch_new_idx, tcell->get_pin_id("R"), true));
				}
			}
		}
}

void Inou_abc::conn_primary_output(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	Abc_Obj_t *pTerm, *pNet;
	int i;
	std::map<index_pid, std::map<Port_ID, Abc_Obj_t *>> info_cache;
	Abc_NtkForEachPo(pNtk, pTerm, i) {

		pNet = Abc_ObjFanin0(pTerm);
		std::string output_name(Abc_ObjName(pNet));

		if (output_name[0] == '%' && output_name[output_name.size() - 1] == '%') {
			std::regex trap("%subgraph_input_(\\d++)_(\\d++)_(\\d++)%");
			std::smatch subgraph_info;
			if (std::regex_search(output_name, subgraph_info, trap)) {
				assert(subgraph_info.size() - 1 == 3); // disregard the 0th capture
				Index_ID subgraph_idx = std::stol(subgraph_info[1]);
				Port_ID subgraph_inpid = static_cast<Port_ID>(std::stol(subgraph_info[2]));
				Port_ID offset = static_cast<Port_ID>(std::stol(subgraph_info[3]));
				index_pid idx_pid = {subgraph_idx, subgraph_inpid};
				if (info_cache.find(idx_pid) == info_cache.end()) {
					std::map<Port_ID, Abc_Obj_t *> tmp;
					tmp[offset] = pNet;
					info_cache.insert(std::make_pair(idx_pid, tmp));
				}
				else {
					info_cache.at(idx_pid).insert(std::make_pair(offset, pNet));
				}
			}
		}
		else if (skew_group_map.find(output_name) != skew_group_map.end()) {
			continue;
		}
		else if (reset_group_map.find(output_name) != reset_group_map.end()) {
			continue;
		}
		else {
			new_graph->add_edge(Node_Pin(cell2id[pNet], cell_out_pid[cell2id[pNet]]++, false),
			                    Node_Pin(cell2id[pTerm], 0, true));
		}
	}
	for (const auto &port_info : info_cache) {
		Index_ID old_subgraph_idx = port_info.first.idx;
		Port_ID old_inp_pid = port_info.first.pid;

		for (const auto &j : port_info.second) {
			pNet = j.second;//fanin
			auto offset = j.first;
			auto join_id = subgraph_inpid_remap[subgraph_remap[old_subgraph_idx]][old_inp_pid];
			new_graph->add_edge(Node_Pin(cell2id[pNet], cell_out_pid[cell2id[pNet]]++, false),
			                    Node_Pin(join_id, offset, true));
		}
	}
}

void Inou_abc::conn_combinational_cell(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	Abc_Obj_t *pNet, *pObj;
	int i, k;
	Abc_NtkForEachNode(pNtk, pObj, k) {
			Mio_Gate_t *pGate = (Mio_Gate_t *) pObj->pData;
			Mio_Pin_t *pGatePin;
			Port_ID inpid = 0;
			for (pGatePin = Mio_GateReadPins(pGate), i = 0; pGatePin; pGatePin = Mio_PinReadNext(pGatePin), i++) {
				std::string fanin_pin_name((Mio_PinReadName(pGatePin)));
				pNet = Abc_ObjFanin(pObj, i);
				std::string name(Abc_ObjName(pNet));
				if (name[0] == '%' && name[name.size() - 1] == '%') {
					std::regex trap("%subgraph_output_(\\d++)_(\\d++)_(\\d++)%");
					std::smatch subgraph_info;
					if (std::regex_search(name, subgraph_info, trap)) {
						assert(subgraph_info.size() - 1 == 3); // disregard the 0th capture
						Index_ID subgraph_old_idx = std::stol(subgraph_info[1]);
						Port_ID subgraph_outpid = static_cast<Port_ID>(std::stol(subgraph_info[2]));
						Port_ID offset = static_cast<Port_ID>(std::stol(subgraph_info[3]));
						index_offset info = {subgraph_old_idx, subgraph_outpid, {offset, offset}};
						Index_ID subgraph_new_idx = subgraph_remap[subgraph_old_idx];
						Index_ID port_join_idx = subgraph_outpid_remap[subgraph_new_idx][subgraph_outpid];
						// push some info for furthur processing
						if (pickop_map.find(info) == pickop_map.end()) {
							Index_ID pick_nid = new_graph->create_node().get_nid();
							cell_out_pid[pick_nid] = 0;
							new_graph->node_type_set(pick_nid, Pick_Op);
							new_graph->set_bits(pick_nid, 1);
							new_graph->add_edge(Node_Pin(port_join_idx, offset, false),
							                    Node_Pin(pick_nid, 0, true));
							connect_constant(new_graph, offset, 32, pick_nid, 1);

							new_graph->add_edge(Node_Pin(pick_nid, cell_out_pid[pick_nid]++, false),
							                    Node_Pin(cell2id[Abc_ObjFanout0(pObj)], inpid++, true));
						}
						else {
							new_graph->add_edge(Node_Pin(pickop_map[info], cell_out_pid[pickop_map[info]]++, false),
							                    Node_Pin(cell2id[Abc_ObjFanout0(pObj)], inpid++, true));
						}
					}
				}
				else {
					new_graph->add_edge(Node_Pin(cell2id[pNet], cell_out_pid[cell2id[pNet]]++, false),
					                    Node_Pin(cell2id[Abc_ObjFanout0(pObj)], inpid++, true));
				}
			}
		}
}

void Inou_abc::conn_subgraph(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {

}


