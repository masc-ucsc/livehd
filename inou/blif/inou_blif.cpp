//
// Created by birdeclipse on 1/24/18.
//

#include "inou_blif.hpp"
#include "lgraphbase.hpp"
#include "lgedgeiter.hpp"
#include <cstring>

Inou_blif_options_pack::Inou_blif_options_pack() {
	Options::get_desc()->add_options()
					("blif_output,o", boost::program_options::value(&blif_output), "blif output <filename> for graph")
					("verbose,v", boost::program_options::value(&verbose), "verbose output");

	boost::program_options::variables_map vm;
	boost::program_options::store(
					boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv()).options(
									*Options::get_desc()).allow_unregistered().run(), vm);

	if (vm.count("blif_output")) {
		blif_output = vm["blif_output"].as<std::string>();
	}
	else {
		blif_output = "output.blif";
	}

	if (vm.count("verbose")) {
		verbose = vm["verbose"].as<std::string>();
	}
	else {
		verbose = "false";
	}
	console->info("verbose: {}", verbose);

}

Inou_blif::Inou_blif() {

}

Inou_blif::~Inou_blif() {

}

std::vector<LGraph *> Inou_blif::generate() {
	std::vector<LGraph *> lgs;
	if (opack.graph_name != "") {
		lgs.push_back(new LGraph(opack.lgdb_path, opack.graph_name, false)); // Do not clear
		// No need to sync because it is a reload. Already sync
	}
	else {
		//FIXME: BLIF2Lgraph
		fmt::print("Please specify the graph name!\n");
		fmt::print("Blif2Lgraph will be supported later!\n");
	}
	return lgs;
}

void Inou_blif::generate(std::vector<const LGraph *>& out) {
	if (out.size() == 1) {
		if (is_techmap(out[0])) {
			find_cell_conn(out[0]);
			to_blif(out[0], opack.blif_output);
		}
	}
}

void Inou_blif::to_blif(const LGraph *g, const std::string filename) {
	std::ofstream fs;

	fs.open(filename, std::ios::out | std::ios::trunc);
	if (!fs.is_open()) {
		std::cerr << "ERROR: could not open blif file [" << filename << "]";
		exit(-4);
	}
	gen_module(g, fs);
	gen_const_conn(g, fs);
	gen_io_conn(g, fs);
	gen_cell_conn(g, fs);
	gen_latch_conn(g, fs);
	fs << ".end\n";
	fs.close();
}


void Inou_blif::gen_module(const LGraph *g, std::ofstream &fs) {
	fs << ".model " << g->get_name() << "\n";
	fs << ".inputs ";
	for (const auto &idx : GraphIO_input_ID) {
		int width = g->get_bits(idx);
		if (width > 1) {
			for (int j = 0; j < width; j++) {
				fs << g->get_graph_input_name(idx) << "[" << j << "] ";
			}
		}
		else {
			fs << g->get_graph_input_name(idx) << " ";
		}
	}
	fs << "\n";
	fs << ".outputs ";
	for (const auto &idx : GraphIO_output_ID) {
		int width = g->get_bits(idx);
		if (width > 1) {
			for (int j = 0; j < width; j++) {
				fs << g->get_graph_output_name(idx) << "[" << j << "] ";
			}
		}
		else {
			fs << g->get_graph_output_name(idx) << " ";
		}
	}
	fs << "\n";
}

void Inou_blif::gen_io_conn(const LGraph *g, std::ofstream &fs) {

	for (const auto &idx : GraphIO_output_ID) {
		int width = g->get_bits(idx);
		int counter = 0;
		std::vector<index_bits> src = primary_output_conn[idx];
		std::string output_name = g->get_graph_output_name(idx);
		fs << ".names ";
		for (const auto &inp : src) {
			Index_ID src_idx = inp.idx;
			int src_bits = inp.bits[0];
			Node_Type_Op src_type = g->node_type_get(src_idx).op;

			if (src_type == TechMap_Op) {
				fs << wirename[src_idx] << " ";
			}
			else if (src_type == U32Const_Op) {
				int value = g->node_value_get(src_idx);
				int bit = 0;
				bit = (value >> src_bits) & 1;
				if (bit == 0) {
					fs << "$false ";
				}
				else {
					fs << "$true ";
				}
			}
			else if (src_type == StrConst_Op) {
				std::string value = g->node_const_value_get(src_idx);
				int bit = 1;

				if (value[value.length() - src_bits - 1] == '1')
					bit = 1;
				else if (value[value.length() - src_bits - 1] == '0')
					bit = 0;
				else if (value[value.length() - src_bits - 1] == 'x')
					bit = 1;
				else if (value[value.length() - src_bits - 1] == 'z')
					bit = 0;
				if (bit == 0) {
					fs << "$false ";
				}
				else {
					fs << "$true ";
				}
			}
			else {
				assert(src_type == GraphIO_Op);
				assert(g->is_graph_input(src_idx));
				if (g->get_bits(src_idx) > 1) {
					fs << wirename[src_idx] << "[" << src_bits << "] ";
				}
				else {
					fs << wirename[src_idx];
				}
			}

			if (width > 1) {
				fs << output_name << "[" << counter++ << "] ";
				fs << "\n1 1\n";
			}
			else {
				fs << output_name;
				fs << "\n1 1\n";
			}

		}
	}
}

void Inou_blif::gen_const_conn(const LGraph *g, std::ofstream &fs) {
	fs << ".names $false\n";
	fs << ".names $true\n1\n";
	fs << ".names $undef\n";
}

void Inou_blif::gen_cell_conn(const LGraph *g, std::ofstream &fs) {

	for (const auto &idx : Comb_Op_ID) {
		std::vector<index_bits> src = comb_conn[idx];
		std::string cell_name = wirename[idx];
		const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
		const std::string tcell_name = tcell->get_name();

		fs << ".names ";

		for (const auto &inp : src) {

			Index_ID src_idx = inp.idx;
			int src_bits = inp.bits[0];
			Node_Type_Op src_type = g->node_type_get(src_idx).op;

			if (src_type == TechMap_Op) {
				fs << wirename[src_idx] << " ";
			}
			else if (src_type == U32Const_Op) {
				int value = g->node_value_get(src_idx);
				int bit = 0;
				bit = (value >> src_bits) & 1;
				if (bit == 0) {
					fs << "$false ";
				}
				else {
					fs << "$true ";
				}
			}
			else if (src_type == StrConst_Op) {
				std::string value = g->node_const_value_get(src_idx);
				int bit = 1;

				if (value[value.length() - src_bits - 1] == '1')
					bit = 1;
				else if (value[value.length() - src_bits - 1] == '0')
					bit = 0;
				else if (value[value.length() - src_bits - 1] == 'x')
					bit = 1;
				else if (value[value.length() - src_bits - 1] == 'z')
					bit = 0;
				if (bit == 0) {
					fs << "$false ";
				}
				else {
					fs << "$true ";
				}
			}
			else {
				assert(src_type == GraphIO_Op);
				assert(g->is_graph_input(src_idx));
				if (g->get_bits(src_idx) > 1) {
					fs << wirename[src_idx] << "[" << src_bits << "] ";
				}
				else {
					fs << wirename[src_idx] << " ";
				}
			}

		}

		fs << cell_name;
		if (tcell_name == "$_BUF_") {
			fs << "\n1 1\n";
		}

		if (tcell_name == "$_NOT_") {
			fs << "\n0 1\n";
		}
		else if (tcell_name == "$_AND_") {
			fs << "\n11 1\n";
		}
		else if (tcell_name == "$_OR_") {
			fs << "\n1- 1\n-1 1\n";
		}
		else if (tcell_name == "$_XOR_") {
			fs << "\n10 1\n01 1\n";
		}
		else if (tcell_name == "$_NAND_") {
			fs << "\n0- 1\n-0 1\n";
		}
		else if (tcell_name == "$_NOR_") {
			fs << "\n00 1\n";
		}
		else if (tcell_name == "$_XNOR_") {
			fs << "\n11 1\n00 1\n";
		}
		else if (tcell_name == "$_ANDNOT_") {
			fs << "\n10 1\n";
		}
		else if (tcell_name == "$_ORNOT_") {
			fs << "\n1- 1\n-0 1\n";
		}
		else if (tcell_name == "$_AOI3_") {
			fs << "\n-00 1\n0-0 1\n";
		}
		else if (tcell_name == "$_OAI3_") {
			fs << "\n00- 1\n--0 1\n";
		}
		else if (tcell_name == "$_AOI4_") {
			fs << "\n-0-0 1\n-00- 1\n0--0 1\n0-0- 1\n";
		}
		else if (tcell_name == "$_OAI4_") {
			fs << "\n00-- 1\n--00 1\n";
		}
		else if (tcell_name == "$_MUX_") {
			fs << "\n1-0 1\n-11 1\n";
		}

	}
}

void Inou_blif::gen_latch_conn(const LGraph *g, std::ofstream &fs) {
	std::vector<const Edge *> inp_edges;
	inp_edges.reserve(32);
	for (const auto &idx : Latch_Op_ID) {
		std::vector<index_bits> src = latch_conn[idx];
		int port_count = 0;
		const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));

		fs << ".subckt ";
		fs << tcell->get_name() << " ";

		for (const auto &input : g->inp_edges(idx)) {
			inp_edges[input.get_inp_pin().get_pid()] = &input;
			port_count++;
		}

		for (int i = 0; i < port_count; i++) {
			Index_ID src_idx = src[i].idx;
			int src_bits = src[i].bits[0];
			Node_Type_Op src_type = g->node_type_get(src_idx).op;
			fs << tcell->get_name(inp_edges[i]->get_inp_pin().get_pid()) << "=";

			if (src_type == TechMap_Op) {
				fs << wirename[src_idx] << " ";
			}
			else if (src_type == U32Const_Op) {
				int value = g->node_value_get(src_idx);
				int bit = 0;
				bit = (value >> src_bits) & 1;
				if (bit == 0) {
					fs << "$false ";
				}
				else {
					fs << "$true ";
				}
			}
			else if (src_type == StrConst_Op) {
				std::string value = g->node_const_value_get(src_idx);
				int bit = 1;

				if (value[value.length() - src_bits - 1] == '1')
					bit = 1;
				else if (value[value.length() - src_bits - 1] == '0')
					bit = 0;
				else if (value[value.length() - src_bits - 1] == 'x')
					bit = 1;
				else if (value[value.length() - src_bits - 1] == 'z')
					bit = 0;
				if (bit == 0) {
					fs << "$false ";
				}
				else {
					fs << "$true ";
				}
			}
			else {
				assert(src_type == GraphIO_Op);
				assert(g->is_graph_input(src_idx));
				if (g->get_bits(src_idx) > 1) {
					fs << wirename[src_idx] << "[" << src_bits << "] ";
				}
				else {
					fs << wirename[src_idx] << " ";
				}
			}
		}
		fs << "Q=" << wirename[idx] << " ";
		fs << "\n";
		inp_edges.clear();
	}
}

/************************************************************************
 * Function:  Inou_abc::is_techmap
 * --------------------
 * input arg0 -> const LGraph *g
 *
 * returns: true/false
 *
 * description: iterate the lgraph to see it is a valid graph to pass abc
 *
 ***********************************************************************/
bool Inou_blif::is_techmap(const LGraph *g) {

	bool is_valid_input = true;
	char buffer[MAX_WIRENAME_LENGTH];

	for (const auto &idx : g->fast()) {
		switch (g->node_type_get(idx).op) {
			case TechMap_Op: {
				const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
				sprintf(buffer, "TechMap_Op:$%s$NodeID:$%ld$Bits:$%d$", tcell->get_name().c_str(), idx, g->get_bits(idx));
				wirename[idx] = std::string(buffer);
				if (is_latch(tcell)) {
					Latch_Op_ID.push_back(idx);
				}
				else {
					Comb_Op_ID.push_back(idx);
				}
				break;
			}
			case Pick_Op: {
				int width = 0;
				int pick_width = g->get_bits(idx);
				int offset = 0;
				for (const auto &c : g->inp_edges(idx)) {
					switch (c.get_inp_pin().get_pid()) {
						case 0: {
							width = c.get_bits();
							break;
						}
						case 1: {
							offset = g->node_value_get(c.get_idx());
							break;
						}
						default: {
							fmt::print("\tPick_Op has more than two inputs!!\n");
							assert(false);
							is_valid_input = false;
						}
					}
				}
				int upper = offset + pick_width - 1;
				int lower = offset;
				assert(upper - lower + 1 <= width);
			}
			case Join_Op: {
				int width = g->get_bits(idx);
				if (width > 1) {
					/**********************************************************************************************
					 * If a Join_Op node has output width > 1:
					 * 	1. MUST NOT be the fanin of a TechMap_Op node (it should traverse through a Pick_OP!!)
					 * 	2. MUST NOT be the fanin of U32Const_Op | StrConst_Op node (undefined behavior!!)
					 * 	3. The width of GraphIO_Op nodes MUST Match (it should traverse through a Pick_OP!!)
					 **********************************************************************************************/
					for (const auto &out: g->out_edges(idx)) {
						switch (g->node_type_get(out.get_idx()).op) {
							case TechMap_Op : {
								const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(out.get_idx()));
								std::string cell_name = tcell->get_name();
								console->error(
												"nodeID:{} type:Join_Op has output to a TechMapNode:{} cell_name: {} ; mismatch in data width!\n",
												idx, out.get_idx(), cell_name);
								is_valid_input = false;
								break;
							}
							case U32Const_Op: {
								console->error("nodeID:{} type:Join_Op has output to a U32ConstNode:{}; undefined behavior!\n", idx,
								               out.get_idx());
								is_valid_input = false;
								break;
							}
							case StrConst_Op: {
								console->error("nodeID:{} type:Join_Op has output to a StrConstNode:{}; undefined behavior!\n", idx,
								               out.get_idx());
								is_valid_input = false;
								break;
							}
							case GraphIO_Op: {
								if (g->get_bits(out.get_idx()) != width) {
									console->error("nodeID:{} type:Join_Op has output to a GraphIONode:{} ; mismatch in data width!\n",
									               idx,
									               out.get_idx());
									is_valid_input = false;
								}
								break;
							}
							default: {
								break;
							}
						}
					}
				}
			}
			case U32Const_Op: {
				break;
			}
			case StrConst_Op: {
				break;
			}
			case GraphIO_Op: {
				if (g->is_graph_input(idx)) {
					GraphIO_input_ID.push_back(idx);
					sprintf(buffer, "%s", g->get_graph_input_name(idx));
					wirename[idx] = std::string(buffer);
				}
				else
					GraphIO_output_ID.push_back(idx);
				break;
			}
			default: {
				console->error("nodeID:{} op is not supported, opType is {}\n", idx, g->node_type_get(idx).get_name().c_str());
				is_valid_input = false;
				break;
			}
		}
	}
	return is_valid_input;
}


/************************************************************************
 * Function:  Inou_blif::find_cell_conn()
 * --------------------
 * input arg0 -> const LGraph *g
 *
 * returns: nothing
 *
 * description: compute the netlist topology use recursive_find() ,
 * 							the info is stored in
 * 							std::unordered_map<Index_ID, std::vector<index_bits>> comb_conn;
 * 							std::unordered_map<Index_ID, std::vector<index_bits>> latch_conn;
 * 							std::unordered_map<Index_ID, std::vector<index_bits>> primary_output_conn;
 ***********************************************************************/
void Inou_blif::find_cell_conn(const LGraph *g) {

	std::vector<index_bits> pid;
	fmt::print("\n******************************************************************\n");
	fmt::print("Begin Computing Netlist Topology Based On Lgraph\n");
	fmt::print("******************************************************************\n");
	//This is to ensure iterate edges in order
	std::vector<const Edge *> inp_edges;
	inp_edges.reserve(32);

	for (const auto &idx : Latch_Op_ID) {
		const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
		int port_count = 0;
		if (opack.verbose == "true")
			fmt::print("\nLatch_Op_ID NodeID:{} has direct input from Node: \n", idx);
		for (const auto &input : g->inp_edges(idx)) {
			inp_edges[input.get_inp_pin().get_pid()] = &input;
			port_count++;
		}
		for (int i = 0; i < port_count; i++) {
			if (inp_edges[i] == nullptr)
				break;
			if (opack.verbose == "true")
				fmt::print("{} pin input port ID {}", tcell->get_name(inp_edges[i]->get_inp_pin().get_pid()),
				           inp_edges[i]->get_inp_pin().get_pid());
			int bit_index[2] = {0, 0};
			recursive_find(g, inp_edges[i], pid, bit_index);
		}
		inp_edges.clear();
		assert(pid.size() == port_count);// ensure that D pin have fanin and only store D pin info
		latch_conn[idx] = std::move(pid);
	}

	for (const auto &idx : Comb_Op_ID) {
		const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
		int port_count = 0;
		if (opack.verbose == "true")
			fmt::print("\nComb_Op_ID NodeID:{} has direct input from Node: \n", idx);
		for (const auto &input : g->inp_edges(idx)) {
			inp_edges[input.get_inp_pin().get_pid()] = &input;
			port_count++;
		}

		for (int i = 0; i < port_count; i++) {
			if (inp_edges[i] == nullptr)
				break;
			if (opack.verbose == "true")
				fmt::print("{} pin input port ID {}", tcell->get_name(inp_edges[i]->get_inp_pin().get_pid()),
				           inp_edges[i]->get_inp_pin().get_pid());
			int bit_index[2] = {0, 0};
			recursive_find(g, inp_edges[i], pid, bit_index);
		}

		inp_edges.clear();
		assert(pid.size() == port_count); // ensure that every port have fanin
		comb_conn[idx] = std::move(pid);
	}

	for (const auto &idx : GraphIO_output_ID) {
		int port_count = 0;
		if (opack.verbose == "true")
			fmt::print("\nGraphIO_output_ID NodeID:{} has direct input from Node: \n", idx);
		int width = g->get_bits(idx);
		int index = 0;
		for (const auto &input : g->inp_edges(idx)) {
			for (index = 0; index < width; index++) {
				int bit_index[2] = {index, index};
				recursive_find(g, &input, pid, bit_index);
				port_count++;
			}
		}
		assert(index == width);
		assert(pid.size() == port_count);
		primary_output_conn[idx] = std::move(pid);
	}
	fmt::print("\n******************************************************************\n");
	fmt::print("Finish Computing Netlist Topology Based On Lgraph\n");
	fmt::print("******************************************************************\n");

}

/************************************************************************
 * Function:  Inou_abc::recursive_find()
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> const Edge *input
 * input arg2 -> std::vector<index_bits> &pid
 * input arg3 -> int *bit_addr
 *
 * returns: nothing
 *
 * description: recursively traverse input nodes untill finds its src
 * 						an src is U32Const_Op StrConst_Op GraphIO_Op TechMap_Op
 * 						FIXME: More Join & Pick Node means (a way to reduce ?)
 * 						"deeper" traverse depth and recursion
 ***********************************************************************/
void Inou_blif::recursive_find(const LGraph *g, const Edge *input, std::vector<index_bits> &pid, int *bit_addr) {

	Index_ID this_idx = input->get_idx();
	Node_Type_Op this_node_type = g->node_type_get(this_idx).op;
	if (this_node_type == U32Const_Op) {
		if (opack.verbose == "true")
			fmt::print("\t U32Const_Op_NodeID:{},bit [{}:{}] \n", input->get_idx(), bit_addr[0], bit_addr[1]);

		index_bits info = {this_idx, {bit_addr[0], bit_addr[1]}};
		pid.push_back(info);
	}
	else if (this_node_type == StrConst_Op) {
		if (opack.verbose == "true")
			fmt::print("\t StrConst_Op_NodeID:{},bit [{}:{}] \n", this_idx, bit_addr[0], bit_addr[1]);

		index_bits info = {this_idx, {bit_addr[0], bit_addr[1]}};
		pid.push_back(info);
	}
	else if (this_node_type == GraphIO_Op) {
		if (g->is_graph_output(this_idx)) {
			// Lgraph allow GroupIO_out to have outputs to the internal node
			// However This would bring difficulties to construct the netlist with ABC
			// Therefore we should recursively find the node;
			for (const auto &pre_inp : g->inp_edges(this_idx)) {
				recursive_find(g, &pre_inp, pid, bit_addr);
			}
		}
		else {
			if (opack.verbose == "true")
				fmt::print("\t GraphIO_Op_NodeID:{},bit [{}:{}] \n", this_idx, bit_addr[0], bit_addr[1]);
			index_bits info = {this_idx, {bit_addr[0], bit_addr[1]}};
			pid.push_back(info);
		}
	}
	else if (this_node_type == TechMap_Op) {
		if (opack.verbose == "true")
			fmt::print("\t NodeID:{},bit [{}:{}] \n", this_idx, 0, 0);
		index_bits info = {this_idx, {0, 0}};
		pid.push_back(info);
	}
	else if (this_node_type == Join_Op) {
		int width = 0;
		int width_pre = 0;
		int Join_width = g->get_bits(this_idx);
		std::vector<const Edge *> Join;
		Join.reserve(Join_width);
		for (const auto &pre_inp : g->inp_edges(this_idx)) {
			Join[pre_inp.get_inp_pin().get_pid()] = &pre_inp;
		}
		for (Port_ID i = 0; i < Join_width; i++) {
			if (Join[i] == nullptr)
				break;
			width_pre = width;
			width += g->get_bits(Join[i]->get_idx());
			if (bit_addr[0] + 1 <= width) {
				bit_addr[0] = bit_addr[0] - width_pre;
				bit_addr[1] = bit_addr[1] - width_pre;
				recursive_find(g, Join[i], pid, bit_addr);
				break;
			}
		}
	}
	else if (this_node_type == Pick_Op) {
		int width = 0;
		int offset = 0;
		int pick_width = g->get_bits(this_idx);
		int upper = 0;
		int lower = 0;
		for (const auto &pre_inp : g->inp_edges(this_idx)) {
			switch (pre_inp.get_inp_pin().get_pid()) {
				case 0: {
					width = g->get_bits(pre_inp.get_idx());
					break;
				}
				case 1: {
					assert (g->node_type_get(pre_inp.get_idx()).op == U32Const_Op);
					offset = g->node_value_get(pre_inp.get_idx());
					break;
				}
				default:
					break;
			}
		}
		upper = offset + pick_width - 1;
		lower = offset;
		assert(bit_addr[1] - bit_addr[0] + 1 <= width);
		assert(upper - lower + 1 <= width);
		for (const auto &pre_inp : g->inp_edges(this_idx)) {
			if (pre_inp.get_inp_pin().get_pid() == 0) {
				bit_addr[0] += lower;
				bit_addr[1] += lower;
				recursive_find(g, &pre_inp, pid, bit_addr);
			}
		}
	}
}
