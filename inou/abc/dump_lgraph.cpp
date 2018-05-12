//
// Created by birdeclipse on 4/25/18.
//
#include "inou_abc.hpp"
#include <regex>

void Inou_abc::from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {

	if (!Abc_NtkIsAigNetlist(pNtk) && !Abc_NtkIsMappedNetlist(pNtk)) {
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

void Inou_abc::gen_primary_io_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	assert(old_graph);
	Abc_Obj_t *pTerm = nullptr, *pNet = nullptr;
	int i = 0;
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
			cell2id[pNet] = io_idx;
			cell_out_pid[io_idx] = 0;
			io_remap[Abc_ObjName(pNet)] = io_idx;
		}

		if (skew_group_map.find(input_name) != skew_group_map.end()) {
			ck_remap[input_name] = cell2id[pNet];
		}
		if (reset_group_map.find(input_name) != reset_group_map.end()) {
			rst_remap[input_name] = cell2id[pNet];
		}
	}

	Abc_NtkForEachPo(pNtk, pTerm, i) {
		pNet = Abc_ObjFanin0(pTerm);
		std::string output_name(((Abc_ObjName(pNet))));
		if (output_name[0] == '%' && output_name[output_name.size() - 1] == '%') {
			continue;
		}
		else {
			Index_ID io_idx = new_graph->add_graph_output(Abc_ObjName(pNet), 0, 1, 0);
			new_graph->set_node_wirename(io_idx, Abc_ObjName(pNet));
			new_graph->set_bits(io_idx, 1);
			new_graph->node_type_set(io_idx, GraphIO_Op);
			cell2id[pTerm] = io_idx;
			cell_out_pid[io_idx] = 0;
			io_remap[Abc_ObjName(pNet)] = io_idx;
		}

		if (skew_group_map.find(output_name) != skew_group_map.end()) {
			ck_remap[output_name] = cell2id[pTerm];
		}
		if (reset_group_map.find(output_name) != reset_group_map.end()) {
			rst_remap[output_name] = cell2id[pTerm];
		}
	}
}

void Inou_abc::gen_comb_cell_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	assert(old_graph);
	const Tech_library *tlib = new_graph->get_tlibrary();
	const Tech_cell *tcell = nullptr;
	Abc_Obj_t *pObj = nullptr;
	int i, k = 0;
	Index_ID cell_idx = 0;

	if (Abc_NtkHasMapping(pNtk)) {
		Abc_NtkForEachNode(pNtk, pObj, k) {
				bool constnode = false;
				auto *pGate = (Mio_Gate_t *) pObj->pData;
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
			cell2id[pNet] = cell_idx;
			cell_out_pid[cell_idx] = 0;
			new_graph->set_node_wirename(cell_idx, latch_name.c_str());
			new_graph->set_bits(cell_idx, 1);
		}
}

void Inou_abc::gen_memory_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	assert(old_graph);
	for (const auto &idx : memory_id) {
		Index_ID new_memory_idx = new_graph->create_node().get_nid();
		memory_remap[idx] = new_memory_idx;
		new_graph->node_type_set(new_memory_idx, Memory_Op);
		new_graph->set_node_wirename(new_memory_idx,old_graph->get_node_wirename(idx));
	}

	Abc_Obj_t *pTerm = nullptr, *pNet = nullptr;
	int i;

	std::map<index_offset, Abc_Obj_t *> memory_input_map;
	Abc_NtkForEachPo(pNtk, pTerm, i) {
		pNet = Abc_ObjFanin0(pTerm);
		std::string output_name(((Abc_ObjName(pNet))));
		if (output_name.substr(0, 14) == "%memory_input_") {
			std::regex trap("%memory_input_(\\d++)_(\\d++)_(\\d++)%");
			std::smatch memory_info;
			if (std::regex_search(output_name, memory_info, trap)) {
				assert(memory_info.size() - 1 == 3); // disregard the 0th capture
				Index_ID old_memory_idx = std::stol(memory_info[1]);
				Port_ID old_inp_pid = static_cast<Port_ID>(std::stol(memory_info[2]));
				Port_ID old_offset = static_cast<Port_ID>(std::stol(memory_info[3]));

				Index_ID new_memory_idx = memory_remap[old_memory_idx];
				if (LGRAPH_MEMOP_ISWREN(old_inp_pid) || LGRAPH_MEMOP_ISRDEN(old_inp_pid)) {
					new_graph->add_edge(Node_Pin(cell2id[pNet], cell_out_pid[cell2id[pNet]]++, false),
					                    Node_Pin(new_memory_idx, old_inp_pid, true));
				}
				else {
					index_offset info = {new_memory_idx, old_inp_pid, {old_offset, old_offset}};
					memory_input_map[info] = pNet;
				}
			}
		}
	}

	std::map<index_offset, Abc_Obj_t *> memory_output_map;
	Abc_NtkForEachPi(pNtk, pTerm, i) {
		pNet = Abc_ObjFanout0(pTerm);
		std::string input_name(((Abc_ObjName(pNet))));
		if (input_name.substr(0, 15) == "%memory_output_") {
			std::regex trap("%memory_output_(\\d++)_(\\d++)_(\\d++)%");
			std::smatch memory_info;
			if (std::regex_search(input_name, memory_info, trap)) {
				assert(memory_info.size() - 1 == 3); // disregard the 0th capture
				Index_ID old_memory_idx = std::stol(memory_info[1]);
				Port_ID old_memory_outpid = static_cast<Port_ID>(std::stol(memory_info[2]));
				Port_ID old_offset = static_cast<Port_ID>(std::stol(memory_info[3]));

				index_offset info = {memory_remap[old_memory_idx], old_memory_outpid, {old_offset, old_offset}};
				memory_output_map[info] = pNet;
			}
		}
	}

	for (const auto &old_idx : memory_id) {
		Index_ID new_memory_idx = memory_remap[old_idx];

		for (const auto &input : old_graph->inp_edges(old_idx)) {
			Port_ID old_inp_pid = input.get_inp_pin().get_pid();
			if (old_inp_pid < LGRAPH_MEMOP_CLK) {
				auto node_idx = input.get_idx();
				auto width = old_graph->get_bits(node_idx);
				auto val = 0;
				if (old_graph->node_type_get(node_idx).op == U32Const_Op) {
					val = old_graph->node_value_get(node_idx);
				}
				connect_constant(new_graph, val, width, new_memory_idx, old_inp_pid);
			}
			else if (old_inp_pid == LGRAPH_MEMOP_CLK) {
				for (const auto &sg : skew_group_map) {
					if (sg.second.find(old_idx) != sg.second.end()) {
						std::string ck_name = sg.first;
						new_graph->add_edge(Node_Pin(ck_remap[ck_name], cell_out_pid[ck_remap[ck_name]]++, false),
						                    Node_Pin(new_memory_idx, LGRAPH_MEMOP_CLK, true));
					}
				}
			}
			else if (old_inp_pid == LGRAPH_MEMOP_CE) {
				new_graph->add_edge(Node_Pin(cell2id[pNet], cell_out_pid[cell2id[pNet]]++, false),
				                    Node_Pin(new_memory_idx, old_inp_pid, true));
			}
			else if (LGRAPH_MEMOP_ISWRADDR(old_inp_pid) || LGRAPH_MEMOP_ISWRDATA(old_inp_pid) ||
			         LGRAPH_MEMOP_ISRDADDR(old_inp_pid)) {
				auto inp_info = memory_conn[old_idx][old_inp_pid];
				auto size = inp_info.size();
				Index_ID join_id = new_graph->create_node().get_nid();
				new_graph->node_type_set(join_id, Join_Op);
				new_graph->set_bits(join_id, size);
				auto src_pin = Node_Pin(join_id, 0, false);
				auto dst_pin = Node_Pin(new_memory_idx, old_inp_pid, true);
				new_graph->add_edge(src_pin, dst_pin);

				for (int offset = 0; offset < size; ++offset) {
					index_offset info = {new_memory_idx, old_inp_pid, {offset, offset}};
					auto *pObj = memory_input_map[info];
					new_graph->add_edge(Node_Pin(cell2id[pObj], cell_out_pid[cell2id[pObj]]++, false),
					                    Node_Pin(join_id, offset, true));
				}
			}
		}

		for (const auto &out : old_graph->out_edges(old_idx)) {
			auto out_pid = out.get_out_pin().get_pid();
			auto node = old_graph->get_dest_node(out);
			auto width = node.get_bits();
			Node_Pin pick_pin = create_pick_operator(new_graph, Node_Pin(new_memory_idx, out_pid, false), 0, width);

			Index_ID port_nid = new_graph->get_idx_from_pid(new_memory_idx, out_pid);
			new_graph->set_bits(port_nid, width);

			for (int offset = 0; offset < width; ++offset) {
				Node_Pin pseudo_pin = create_pick_operator(new_graph, Node_Pin(pick_pin.get_nid(), offset, false), offset, 1);
				index_offset key = {new_memory_idx, out_pid, {offset, offset}};
				cell2id[memory_output_map[key]] = pseudo_pin.get_nid();
				cell_out_pid[pseudo_pin.get_nid()] = 0;
				//fmt::print("generated memory output idx : {} name is {}\n",
				//           pseudo_pin.get_nid(),Abc_ObjName(memory_output_map[key]));
			}
		}
	}
}

void Inou_abc::gen_subgraph_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	assert(old_graph);
	for (const auto &idx : subgraph_id) {

		std::string subgraph_name(old_graph->get_library()->get_name(old_graph->subgraph_id_get(idx)));
		LGraph *sub_graph = LGraph::find_graph(subgraph_name, old_graph->get_path());
		Index_ID new_subgraph_idx = new_graph->create_node().get_nid();
		subgraph_remap[idx] = new_subgraph_idx;
		new_graph->node_type_set(new_subgraph_idx, SubGraph_Op);
		new_graph->set_node_instance_name(new_subgraph_idx, old_graph->get_node_instancename(idx));
		new_graph->node_subgraph_set(new_subgraph_idx, sub_graph->lg_id());
	}
	Abc_Obj_t *pTerm = nullptr, *pNet = nullptr;
	int i = 0;

	std::map<index_offset, Abc_Obj_t *> subgraph_input_map;
	Abc_NtkForEachPo(pNtk, pTerm, i) {
		pNet = Abc_ObjFanin0(pTerm);
		std::string output_name(((Abc_ObjName(pNet))));
		if (output_name.substr(0, 16) == "%subgraph_input_") {
			std::regex trap("%subgraph_input_(\\d++)_(\\d++)_(\\d++)%");
			std::smatch subgraph_info;
			if (std::regex_search(output_name, subgraph_info, trap)) {
				assert(subgraph_info.size() - 1 == 3); // disregard the 0th capture
				Index_ID old_subgraph_idx = std::stol(subgraph_info[1]);
				Port_ID old_inp_pid = static_cast<Port_ID>(std::stol(subgraph_info[2]));
				Port_ID old_offset = static_cast<Port_ID>(std::stol(subgraph_info[3]));
				index_offset info = {subgraph_remap[old_subgraph_idx], old_inp_pid, {old_offset, old_offset}};
				subgraph_input_map[info] = pNet;
			}
		}
	}

	std::map<index_offset, Abc_Obj_t *> subgraph_output_map;
	Abc_NtkForEachPi(pNtk, pTerm, i) {
		pNet = Abc_ObjFanout0(pTerm);
		std::string input_name(((Abc_ObjName(pNet))));
		if (input_name.substr(0, 17) == "%subgraph_output_") {
			std::regex trap("%subgraph_output_(\\d++)_(\\d++)_(\\d++)%");
			std::smatch subgraph_info;
			if (std::regex_search(input_name, subgraph_info, trap)) {
				assert(subgraph_info.size() - 1 == 3); // disregard the 0th capture
				Index_ID old_subgraph_idx = std::stol(subgraph_info[1]);
				Port_ID old_inp_pid = static_cast<Port_ID>(std::stol(subgraph_info[2]));
				Port_ID old_offset = static_cast<Port_ID>(std::stol(subgraph_info[3]));

				index_offset info = {subgraph_remap[old_subgraph_idx], old_inp_pid, {old_offset, old_offset}};
				subgraph_output_map[info] = pNet;
			}
		}
	}

	for (const auto &old_idx : subgraph_id) {
		Index_ID new_subgraph_idx = subgraph_remap[old_idx];
		for (const auto &input : old_graph->inp_edges(old_idx)) {
			Port_ID old_inp_pid = input.get_inp_pin().get_pid();
			auto inp_info = subgraph_conn[old_idx][old_inp_pid];
			auto size = inp_info.size();
			Index_ID join_id = new_graph->create_node().get_nid();
			new_graph->node_type_set(join_id, Join_Op);
			new_graph->set_bits(join_id, size);
			auto src_pin = Node_Pin(join_id, 0, false);
			auto dst_pin = Node_Pin(new_subgraph_idx, old_inp_pid, true);
			new_graph->add_edge(src_pin, dst_pin);
			for (int offset = 0; offset < size; ++offset) {
				index_offset info = {new_subgraph_idx, old_inp_pid, {offset, offset}};
				auto *pObj = subgraph_input_map[info];
				new_graph->add_edge(Node_Pin(cell2id[pObj], cell_out_pid[cell2id[pObj]]++, false),
				                    Node_Pin(join_id, offset, true));
			}
		}
		for (const auto &out : old_graph->out_edges(old_idx)) {
			auto out_pid = out.get_out_pin().get_pid();
			auto width = old_graph->get_bits(out.get_out_pin().get_nid());
			Node_Pin pick_pin = create_pick_operator(new_graph, Node_Pin(new_subgraph_idx, out_pid, false), 0, width);
			for (int offset = 0; offset < width; ++offset) {
				Node_Pin pseudo_pin = create_pick_operator(new_graph, Node_Pin(pick_pin.get_nid(), offset, false), offset, 1);
				index_offset key = {new_subgraph_idx, out_pid, {offset, offset}};
				cell2id[subgraph_output_map[key]] = pseudo_pin.get_nid();
				cell_out_pid[pseudo_pin.get_nid()] = 0;
			}
		}
	}
}

void Inou_abc::conn_latch(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	Abc_Obj_t *pNode = nullptr, *pLatch = nullptr;
	int i;
	Abc_NtkForEachLatch(pNtk, pLatch, i) {
			Index_ID latch_new_idx = cell2id[Abc_ObjFanout0(Abc_ObjFanout0(pLatch))];
			const Tech_cell *tcell = new_graph->get_tlibrary()->get_const_cell(new_graph->tmap_id_get(latch_new_idx));
			std::string trig_pin = tcell->pin_name_exist("C") ? "C" : "E";
			pNode = Abc_ObjFanin0(Abc_ObjFanin0(pLatch));

			new_graph->add_edge(Node_Pin(cell2id[pNode], cell_out_pid[cell2id[pNode]]++, false),
			                    Node_Pin(latch_new_idx, tcell->get_pin_id("D"), true));

			std::string flop_name(new_graph->get_node_wirename(latch_new_idx));
			Index_ID latch_old_idx = latchname2id[flop_name];
			for (const auto &sg : skew_group_map) {
				if (sg.second.find(latch_old_idx) != sg.second.end()) {
					std::string ck_name = sg.first;
					new_graph->add_edge(Node_Pin(ck_remap[ck_name], cell_out_pid[ck_remap[ck_name]]++, false),
					                    Node_Pin(latch_new_idx, tcell->get_pin_id(trig_pin), true));
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
	Abc_Obj_t *pTerm = nullptr, *pNet = nullptr;
	int i;
	Abc_NtkForEachPo(pNtk, pTerm, i) {
		pNet = Abc_ObjFanin0(pTerm);
		std::string output_name(Abc_ObjName(pNet));

		if (output_name[0] == '%' && output_name[output_name.size() - 1] == '%') {
			continue;
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
}

void Inou_abc::conn_combinational_cell(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk) {
	Abc_Obj_t *pNet = nullptr, *pObj = nullptr;
	int i, k;
	Abc_NtkForEachNode(pNtk, pObj, k) {
			Mio_Gate_t *pGate = (Mio_Gate_t *) pObj->pData;
			Mio_Pin_t *pGatePin;
			Port_ID inpid = 0;
			for (pGatePin = Mio_GateReadPins(pGate), i = 0; pGatePin; pGatePin = Mio_PinReadNext(pGatePin), i++) {
				std::string fanin_pin_name((Mio_PinReadName(pGatePin)));
				pNet = Abc_ObjFanin(pObj, i);
				new_graph->add_edge(Node_Pin(cell2id[pNet], cell_out_pid[cell2id[pNet]]++, false),
				                    Node_Pin(cell2id[Abc_ObjFanout0(pObj)], inpid++, true));
			}
		}
}

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

Node_Pin Inou_abc::create_pick_operator(LGraph *g, const Node_Pin &driver, int offset, int width) {
	if (offset == 0 && g->get_bits_pid(driver.get_nid(), driver.get_pid()) == width)
		return driver;

	Pick_ID pick_id(driver, offset, width);
	if (picks.find(pick_id) != picks.end())
		return picks.at(pick_id);

	Index_ID pick_nid = g->create_node().get_nid();
	g->node_type_set(pick_nid, Pick_Op);
	g->set_bits(pick_nid, width);

	g->add_edge(driver, Node_Pin(pick_nid, 0, true));

	connect_constant(g, offset, 32, pick_nid, 1);

	picks.insert(std::make_pair(pick_id, Node_Pin(pick_nid, 0, false)));

	return picks.at(pick_id);
}