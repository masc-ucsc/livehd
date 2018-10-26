//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 1/30/18.
//
//#include <boost/filesystem.hpp>

#include <sys/types.h>
#include <sys/stat.h>

#include "abc_cell.hpp"
#include "pass_abc.hpp"

void Pass_abc_options::set(const std::string &key, const std::string &value) {

  try {
    if ( is_opt(key,"verbose") ) {
      if (value == "true")
        verbose = true;
    }else if ( is_opt(key,"liberty_file") ) {
      liberty_file = value;
    }else if ( is_opt(key,"blif_file") ) {
      blif_file = value;
    }else{
      set_val(key,value);
    }
  } catch (const std::invalid_argument& ia) {
    fmt::print("ERROR: key {} has an invalid argument {}\n",key);
  }

}

Pass_abc::Pass_abc() {
  graph_info = new graph_topology;
  mapping_command   = "map;print_stats";
  synthesis_command = "print_stats;cleanup;strash;ifraig;iresyn;dc2;strash;print_stats;";
  readlib_command   = "read_library stdcells.genlib";
}

Pass_abc::~Pass_abc() {
  delete graph_info;
}

void Pass_abc::trans(LGraph *lg) {
  lg->sync(); // sync because Tech Library is loaded
}

LGraph *Pass_abc::regen(const LGraph *lg) {

  if(!is_techmap(lg)) {
    console->error("pass_abc.regen supports techmap graphs only\n");
    return 0;
  }

  find_cell_conn(lg);
  LGraph *mapped = LGraph::create(opack.path, opack.name + "_mapped");
  from_abc(mapped, lg, to_abc(lg));
  mapped->sync();
  if (opack.verbose)
    mapped->print_stats();
  clear();

  if (!opack.blif_file.empty())
    dump_blif(mapped, opack.blif_file);

  return mapped;
}

/************************************************************************
 * Function:  Pass_abc::find_cell_conn()
 * --------------------
 * input arg0 -> const LGraph *g
 *
 * returns: nothing
 *
 * description: compute the netlist topology use recursive_find() ,
 * 							the info is stored in
 * 							node_conn comb_conn;
 * 							node_conn latch_conn;
 * 							node_conn PO_conn;
 * 							block_conn subgraph_conn;
 * 							block_conn memory_conn;
 ***********************************************************************/

void Pass_abc::find_latch_conn(const LGraph *g) {
  for(const auto &idx : graph_info->latch_id) {
	graph_topology::topology_info    pid;
    const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
    if(opack.verbose)
      fmt::print("\nLatch_Op_ID NodeID:{} has direct input from Node: \n", idx);
    std::string trig_pin = tcell->pin_name_exist("C") ? "C" : "E";
    for(const auto &input : g->inp_edges(idx)) {
      if(input.get_inp_pin().get_pid() == tcell->get_pin_id("D")) {
        if(opack.verbose)
          fmt::print("{} pin input port ID {}", tcell->get_name(input.get_inp_pin().get_pid()),
                     input.get_inp_pin().get_pid());
        int bit_index[2] = {0, 0};
        recursive_find(g, &input, pid, bit_index);
      }
      /*********************************************************************
				 * The following code is to keep the clock nets and reset nets that
				 * fanin to the flops
				 * ABC treats all sequential cells as Latches
				 * ABC cannot handle these signals
				 * Although industrial standard flops usually have more pins such as
				 * scan_en, si, etc..,
				 * Due to the limitation of ABC synthesis,
				 * this build only support "clock(FF) or Enable(latch)" pin and "reset" pin
				 *********************************************************************/
      else if(input.get_inp_pin().get_pid() == tcell->get_pin_id(trig_pin)) {
		graph_topology::topology_info clock_pid;
        int           bit_index[2] = {0, 0};
        recursive_find(g, &input, clock_pid, bit_index);
        Index_ID clk_idx = clock_pid[0].idx;

        char clk_name[100];
        if(g->is_graph_input(clk_idx)) {
          sprintf(clk_name, "%s", g->get_node_wirename(clk_idx));
        } else {
          sprintf(clk_name, "generated_clock_id_%ld", clk_idx);
        }
        std::string clock_name(clk_name);
		graph_info->clock_id[clock_name] = clk_idx;
		graph_info->skew_group_map[clock_name].insert(idx);
      } else if(input.get_inp_pin().get_pid() == tcell->get_pin_id("R")) {
		graph_topology::topology_info reset_pid;
        int           bit_index[2] = {0, 0};
        recursive_find(g, &input, reset_pid, bit_index);
        Index_ID reset_idx = reset_pid[0].idx;
        char     rst_name[100];
        if(g->is_graph_input(reset_idx)) {
          sprintf(rst_name, "%s", g->get_node_wirename(reset_idx));
        } else {
          sprintf(rst_name, "generated_reset_id_%ld", reset_idx);
        }
        std::string reset_name(rst_name);
		graph_info->reset_id[reset_name] = reset_idx;
		graph_info->reset_group_map[reset_name].insert(idx);
      }
    }
    assert(pid.size() == 1); // ensure that D pin have fanin and only store D pin info
    graph_info->latch_conn[idx] = std::move(pid);
  }
}

void Pass_abc::find_combinational_conn(const LGraph *g) {

  for(const auto &idx : graph_info->combinational_id) {
    std::vector<const Edge *> inp_edges(16);
    graph_topology::topology_info             pid;
    //const Tech_cell *         tcell      = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
    uint32_t                  port_count = 0;
    for(const auto &input : g->inp_edges(idx)) {
      assert(input.get_inp_pin().get_pid() < 16);
      inp_edges[input.get_inp_pin().get_pid()] = &input;
      port_count++;
    }

    for(uint32_t i = 0; i < port_count; i++) {
      if(inp_edges[i] == nullptr)
        break;

      int bit_index[2] = {0, 0}; //changed withing recursive find
      recursive_find(g, inp_edges[i], pid, bit_index);
    }

    assert(pid.size() == port_count); // ensure that every port have fanin
    graph_info->comb_conn[idx] = std::move(pid);
  }
}

void Pass_abc::find_graphio_output_conn(const LGraph *g) {
  for(const auto &idx : graph_info->graphio_output_id) {
    graph_topology::topology_info pid;

    uint32_t      width = g->get_bits(idx);
    uint32_t      index = 0;

    for(const auto &input : g->inp_edges(idx)) {
      for(index = 0; index < width; index++) {
        int bit_index[2] = {static_cast<int>(index), static_cast<int>(index)};
        recursive_find(g, &input, pid, bit_index);
      }
    }

    assert(index == pid.size());
    graph_info->primary_output_conn[idx] = std::move(pid);
  }
}

void Pass_abc::find_subgraph_conn(const LGraph *g) {

  for(const auto &idx : graph_info->subgraph_id) {
    if(opack.verbose)
      fmt::print("\nSubGraph_Op NodeID:{} has direct input from Node: \n", idx);
    std::map<Port_ID, const Edge *>            inp_edges;
    std::unordered_map<Port_ID, graph_topology::topology_info> subgraph_pid;

    for(const auto &input : g->inp_edges(idx)) {
      Port_ID inp_id    = input.get_inp_pin().get_pid();
      inp_edges[inp_id] = &input;
    }

    for(const auto &input : inp_edges) {
      if(opack.verbose)
        fmt::print("\n------------------------------------------------ \n", idx);
	  graph_topology::topology_info pid;
      auto          node_idx = input.second->get_idx();
      auto          width    = g->get_bits(node_idx);
      int           index    = 0;
      if(width > 1) {
        for(index = 0; index < width; index++) {
          int bit_index[2] = {index, index};
          recursive_find(g, input.second, pid, bit_index);
        }
      } else {
        int bit_index[2] = {0, 0};
        recursive_find(g, input.second, pid, bit_index);
      }
      subgraph_pid[input.first] = std::move(pid);
    }
	graph_info->subgraph_conn[idx] = std::move(subgraph_pid);
  }
}

void Pass_abc::find_memory_conn(const LGraph *g) {
  for(const auto &idx : graph_info->memory_id) {
    if(opack.verbose)
      fmt::print("\nMemory_Op NodeID:{} has direct input from Node: \n", idx);
    std::map<Port_ID, const Edge *>            inp_edges;
    std::unordered_map<Port_ID, graph_topology::topology_info> memory_pid;
    for(const auto &input : g->inp_edges(idx)) {
      Port_ID inp_id = input.get_inp_pin().get_pid();
      if(inp_id >= LGRAPH_MEMOP_CLK)
        inp_edges[inp_id] = &input;
      else
        continue;
    }
    for(const auto &input : inp_edges) {
      Port_ID input_id = input.second->get_inp_pin().get_pid();
      if(opack.verbose)
        fmt::print("\n-------------------{}---------------------- \n", input_id);
	  graph_topology::topology_info pid;
      auto          node_idx = input.second->get_idx();
      auto          width    = g->get_bits(node_idx);
      int           index    = 0;
      if(width > 1) {
        for(index = 0; index < width; index++) {
          int bit_index[2] = {index, index};
          recursive_find(g, input.second, pid, bit_index);
        }
      } else {
        int bit_index[2] = {0, 0};
        recursive_find(g, input.second, pid, bit_index);
      }
      if(input_id == LGRAPH_MEMOP_CLK) {
        assert(pid.size() == 1);
        Index_ID clk_idx = pid[0].idx;
        char     clk_name[100];
        if(g->is_graph_input(clk_idx)) {
          sprintf(clk_name, "%s", g->get_node_wirename(clk_idx));
        } else {
          sprintf(clk_name, "generated_clock_id_%ld", clk_idx);
        }
        std::string clock_name(clk_name);
		graph_info->clock_id[clock_name] = clk_idx;
		graph_info->skew_group_map[clock_name].insert(idx);
      }
      memory_pid[input.first] = std::move(pid);
    }
	graph_info->memory_conn[idx] = std::move(memory_pid);
  }
}

void Pass_abc::find_cell_conn(const LGraph *g) {

#ifndef NDEBUG
  fmt::print("\n******************************************************************\n");
  fmt::print("Begin Computing Netlist Topology Based On Lgraph\n");
  fmt::print("******************************************************************\n");
#endif
  find_latch_conn(g);
  find_combinational_conn(g);
  find_graphio_output_conn(g);
  find_subgraph_conn(g);
  find_memory_conn(g);
#ifndef NDEBUG
  fmt::print("\n******************************************************************\n");
  fmt::print("Finish Computing Netlist Topology Based On Lgraph\n");
  fmt::print("******************************************************************\n");
#endif
}

/************************************************************************
 * Function:  Pass_abc::recursive_find()
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> const Edge *input
 * input arg2 -> topology_info &pid
 * input arg3 -> int *bit_addr
 *
 * returns: nothing
 *
 * description: recursively traverse input nodes untill finds its src
 * 						an src is U32Const_Op StrConst_Op GraphIO_Op TechMap_Op
 * 						FIXME: More Join & Pick Node means (a way to reduce ?)
 * 						"deeper" traverse depth and recursion
 ***********************************************************************/
void Pass_abc::recursive_find(const LGraph *g, const Edge *input, graph_topology::topology_info &pid, int *bit_addr) {

  Index_ID     this_idx       = input->get_idx();
  Node_Type_Op this_node_type = g->node_type_get(this_idx).op;
  if(this_node_type == U32Const_Op) {
    if(opack.verbose)
      fmt::print("\t U32Const_Op_NodeID:{},bit [{}:{}] portid : {} \n",
                 input->get_idx(), bit_addr[0], bit_addr[1], input->get_out_pin().get_pid());

    index_offset info = {this_idx, input->get_out_pin().get_pid(), {bit_addr[0], bit_addr[1]}};
    pid.push_back(info);
  } else if(this_node_type == StrConst_Op) {
    if(opack.verbose)
      fmt::print("\t StrConst_Op_NodeID:{},bit [{}:{}]  portid : {} \n",
                 this_idx, bit_addr[0], bit_addr[1], input->get_out_pin().get_pid());

    index_offset info = {this_idx, input->get_out_pin().get_pid(), {bit_addr[0], bit_addr[1]}};
    pid.push_back(info);
  } else if(this_node_type == GraphIO_Op) {
    if(g->is_graph_output(this_idx)) {
      for(const auto &pre_inp : g->inp_edges(this_idx)) {
        recursive_find(g, &pre_inp, pid, bit_addr);
      }
    } else {
      if(opack.verbose)
        fmt::print("\t GraphIO_Op_NodeID:{},bit [{}:{}] portid : {} \n",
                   this_idx, bit_addr[0], bit_addr[1], input->get_out_pin().get_pid());
      index_offset info = {this_idx, input->get_out_pin().get_pid(), {bit_addr[0], bit_addr[1]}};
      pid.push_back(info);
    }
  } else if(this_node_type == Memory_Op) {
    char namebuffer[255];
    if(opack.verbose)
      fmt::print("\t Memory_Op:{},bit [{}:{}] portid : {} \n",
                 this_idx, bit_addr[0], bit_addr[1], input->get_out_pin().get_pid());
    index_offset info = {this_idx, input->get_out_pin().get_pid(), {bit_addr[0], bit_addr[1]}};
    pid.push_back(info);

    sprintf(namebuffer, "%%memory_output_%ld_%d_%d%%", this_idx, input->get_out_pin().get_pid(), bit_addr[0]);
    const auto it = graph_info->memory_generated_output_wire.find(info);
    if(it == graph_info->memory_generated_output_wire.end()) {
	  graph_info->memory_generated_output_wire[info] = std::string(namebuffer);
    }
  } else if(this_node_type == SubGraph_Op) {
    char namebuffer[255];
    if(opack.verbose)
      fmt::print("\t SubGraph_Op:{},bit [{}:{}] portid : {} \n",
                 this_idx, bit_addr[0], bit_addr[1], input->get_out_pin().get_pid());
    index_offset info = {this_idx, input->get_out_pin().get_pid(), {bit_addr[0], bit_addr[1]}};
    pid.push_back(info);

    sprintf(namebuffer, "%%subgraph_output_%ld_%d_%d%%", this_idx, input->get_out_pin().get_pid(), bit_addr[0]);
    const auto it = graph_info->subgraph_generated_output_wire.find(info);
    if(it == graph_info->subgraph_generated_output_wire.end()) {
	  graph_info->subgraph_generated_output_wire[info] = std::string(namebuffer);
    }
  } else if(this_node_type == TechMap_Op) {
    if(opack.verbose)
      fmt::print("\t NodeID:{},bit [{}:{}] portid : {} \n",
                 this_idx, 0, 0, input->get_out_pin().get_pid());
    const Tech_cell * tcell      = g->get_tlibrary()->get_const_cell(g->tmap_id_get(this_idx));
    const std::string tcell_name = tcell->get_name();
    if(tcell_name == "$_BUF_") {
      for(const auto &pre_inp : g->inp_edges(this_idx)) {
        recursive_find(g, &pre_inp, pid, bit_addr);
      }
    } else {
      index_offset info = {this_idx, input->get_out_pin().get_pid(), {0, 0}};
      pid.push_back(info);
    }
  } else if(this_node_type == Join_Op) {
    int                       width      = 0;
    int                       width_pre  = 0;
    uint32_t                  Join_width = g->get_bits(this_idx);
    std::vector<const Edge *> Join(Join_width);
    for(const auto &pre_inp : g->inp_edges(this_idx)) {
      Join[pre_inp.get_inp_pin().get_pid()] = &pre_inp;
    }
    for(Port_ID i = 0; i < Join_width; i++) {
      if(Join[i] == nullptr)
        break;
      width_pre = width;
      width += g->get_bits(Join[i]->get_idx());
      if(bit_addr[0] + 1 <= width) {
        bit_addr[0] = bit_addr[0] - width_pre;
        bit_addr[1] = bit_addr[1] - width_pre;
        recursive_find(g, Join[i], pid, bit_addr);
        break;
      }
    }
  } else if(this_node_type == Pick_Op) {
    int width      = 0;
    int offset     = 0;
    int pick_width = g->get_bits(this_idx);
    int upper      = 0;
    int lower      = 0;
    for(const auto &pre_inp : g->inp_edges(this_idx)) {
      switch(pre_inp.get_inp_pin().get_pid()) {
      case 0: {
        width = g->get_bits(pre_inp.get_idx());
        break;
      }
      case 1: {
        assert(g->node_type_get(pre_inp.get_idx()).op == U32Const_Op);
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
    for(const auto &pre_inp : g->inp_edges(this_idx)) {
      if(pre_inp.get_inp_pin().get_pid() == 0) {
        bit_addr[0] += lower;
        bit_addr[1] += lower;
        recursive_find(g, &pre_inp, pid, bit_addr);
      }
    }
  }
}

/************************************************************************
 * Function:  Pass_abc::is_techmap
 * --------------------
 * input arg0 -> const LGraph *g
 *
 * returns: true/false
 *
 * description: iterate the lgraph to see it is a valid graph to pass abc
 *
 ***********************************************************************/
bool Pass_abc::is_techmap(const LGraph *g) {

  bool is_valid_input = true;

  for(const auto &idx : g->fast()) {
    switch(g->node_type_get(idx).op) {
    case TechMap_Op: {
      const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
      if(is_latch(tcell)) {
		graph_info->latch_id.push_back(idx);
      } else {
        auto tcell_name = tcell->get_name();
        if(tcell_name != "$_BUF_")
		  graph_info->combinational_id.push_back(idx);
      }
      break;
    }
    case Pick_Op: {
      int width      = 0;
      int pick_width = g->get_bits(idx);
      int offset     = 0;
      for(const auto &c : g->inp_edges(idx)) {
        switch(c.get_inp_pin().get_pid()) {
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
      if(width > 1) {
        /**********************************************************************************************
					 * If a Join_Op node has output width > 1:
					 * 	1. MUST NOT be the fanin of a TechMap_Op node (it should traverse through a Pick_OP!!)
					 * 	2. MUST NOT be the fanin of U32Const_Op | StrConst_Op node (undefined behavior!!)
					 * 	3. The width of GraphIO_Op nodes MUST Match (it should traverse through a Pick_OP!!)
					 **********************************************************************************************/
        for(const auto &out : g->out_edges(idx)) {
          switch(g->node_type_get(out.get_idx()).op) {
          case TechMap_Op: {
            const Tech_cell *tcell     = g->get_tlibrary()->get_const_cell(g->tmap_id_get(out.get_idx()));
            std::string      cell_name = tcell->get_name();
            console->error("nodeID:{} type:Join_Op has output to idx:{} cell_name: {}; mismatch in data width!\n",
                           idx, out.get_idx(), cell_name);
            is_valid_input = false;
            break;
          }
          case U32Const_Op: {
            console->error("nodeID:{} type:Join_Op has output to a U32ConstNode:{}; undefined behavior!\n",
                           idx, out.get_idx());
            is_valid_input = false;
            break;
          }
          case StrConst_Op: {
            console->error("nodeID:{} type:Join_Op has output to a StrConstNode:{}; undefined behavior!\n",
                           idx, out.get_idx());
            is_valid_input = false;
            break;
          }
          case GraphIO_Op: {
            if(g->get_bits(out.get_idx()) != width) {
              console->error("nodeID:{} type:Join_Op has output to a GraphIONode:{}; mismatch in data width!\n",
                             idx, out.get_idx());
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
      if(g->is_graph_input(idx))
        graph_info->graphio_input_id.push_back(idx);
      else
        graph_info->graphio_output_id.push_back(idx);
      break;
    }
    case SubGraph_Op: {
        graph_info->subgraph_id.push_back(idx);
      break;
    }
    case Memory_Op: {
      graph_info->memory_id.push_back(idx);
      break;
    }
    default: {
      console->error("nodeID:{} op is not supported, opType is {}\n",
                     idx, g->node_type_get(idx).get_name().c_str());
      is_valid_input = false;
      break;
    }
    }
  }
  return is_valid_input;
}

/************************************************************************
 * Function:  pass_abc::to_abc()
 * --------------------
 * input arg0 -> const LGraph *g
 *
 * returns: mapped Abc_Ntk_t
 *
 * description: feed to abc for comb synthesis and mapping
 ***********************************************************************/
Abc_Ntk_t *Pass_abc::to_abc(const LGraph *g) {
  Abc_Frame_t *pAbc = nullptr;
  Abc_Ntk_t *  pAig = nullptr;
  Abc_Start();
  pAbc        = Abc_FrameGetGlobalFrame();
  pAig        = Abc_NtkAlloc(ABC_NTK_NETLIST, ABC_FUNC_AIG, 1);
  pAig->pName = Extra_UtilStrsav(opack.name.c_str());

  gen_netList(g, pAig);
  Abc_NtkFinalizeRead(pAig);
  if(!Abc_NtkCheck(pAig)) {
    console->error("The AIG construction has failed.\n");
    Abc_NtkDelete(pAig);
    exit(-4);
  } else {
    char       Command[1000];
    Abc_Ntk_t *pTemp = pAig;
    pAig             = Abc_NtkToLogic(pTemp);
    Abc_FrameClearVerifStatus(pAbc);
    Abc_FrameSetCurrentNetwork(pAbc, pAig);
    if(!opack.liberty_file.empty()) {
      sprintf(Command, "read_lib -w %s", opack.liberty_file.c_str());
      if(Cmd_CommandExecute(pAbc, Command)) {
        console->error("Cannot execute command {}", Command);
      }
    } else {
      gen_generic_lib("stdcells.genlib");
      Cmd_CommandExecute(pAbc, readlib_command.c_str());
    }

    std::string path_name("abc_output");
    struct stat output_status;
    stat(path_name.c_str(), &output_status);

    if(!(output_status.st_mode & S_IFDIR)) {
      mkdir(path_name.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }

    if(Cmd_CommandExecute(pAbc, synthesis_command.c_str())) {
      console->error("Cannot execute command {}", synthesis_command);
    }

    sprintf(Command, "write_blif %s/%s_post.blif;write_verilog %s/%s_post.v;",
            path_name.c_str(), opack.name.c_str(),
            path_name.c_str(), opack.name.c_str());
    if(Cmd_CommandExecute(pAbc, Command)) {
      console->error("Cannot execute command {}", Command);
    }

    if(Cmd_CommandExecute(pAbc, mapping_command.c_str())) {
      console->error("Cannot execute command", mapping_command);
    }

    sprintf(Command, "write_blif %s/%s_map.blif;write_verilog %s/%s_map.v",
            path_name.c_str(), opack.name.c_str(),
            path_name.c_str(), opack.name.c_str());
    if(Cmd_CommandExecute(pAbc, Command)) {
      console->error("Cannot execute command", Command);
    }

    assert(pAbc != nullptr);
  }

  Abc_Ntk_t *pNtkMapped = nullptr;
  pNtkMapped            = Abc_NtkToNetlist(Abc_FrameReadNtk(pAbc));
  if(pNtkMapped == nullptr) {
    console->error("Converting to netlist has failed.\n");
    exit(-4);
  }
  if(!Abc_NtkHasAig(pNtkMapped) && !Abc_NtkHasMapping(pNtkMapped)) {
    Abc_NtkToAig(pNtkMapped);
  }

  remove("stdcells.genlib");

  return pNtkMapped;
}

/************************************************************************
 * Function:  Pass_abc::gen_NetList()
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: build the netlist
 ***********************************************************************/
void Pass_abc::gen_netList(const LGraph *g, Abc_Ntk_t *pAig) {
  console->info("Pass_abc::gen_primary_io_from_lgraph():Lgraph is calling ABC API to create Primary inputs & outputs");
  gen_primary_io_from_lgraph(g, pAig);
  console->info("Pass_abc::gen_latch_from_lgraph():Lgraph is calling ABC API to create latches");
  gen_latch_from_lgraph(g, pAig);
  console->info("Pass_abc::gen_comb_cell_from_lgraph():Lgraph is calling ABC API to create combinational cells");
  gen_comb_cell_from_lgraph(g, pAig);

  console->info("Pass_abc::conn_combinational_cell():Lgraph is calling ABC API to connect all combinational cells");
  conn_combinational_cell(g, pAig);
  console->info("Pass_abc::conn_latch():Lgraph is calling ABC API to connect all latches");
  conn_latch(g, pAig);
  console->info("Pass_abc::conn_primary_output():Lgraph is calling ABC API to connect all primary outputs");
  conn_primary_output(g, pAig);
  console->info("Pass_abc::conn_subgraph():Lgraph is calling ABC API to connect all subgraph");
  conn_subgraph(g, pAig);
  console->info("Pass_abc::conn_memory():Lgraph is calling ABC API to connect all memory");
  conn_memory(g, pAig);
  console->info("Pass_abc::conn_clock():Lgraph is calling ABC API to create clock network if generated clock exsit");
  conn_clock(g, pAig);
  console->info("Pass_abc::conn_reset():Lgraph is calling ABC API to create reset network if generated clock exsit");
  conn_reset(g, pAig);
}

/************************************************************************
 * Function:  Pass_abc::conn_memory
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all generated subgraph signal
 ***********************************************************************/
void Pass_abc::conn_memory(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : graph_info->memory_id) {
    auto inp_info = graph_info->memory_conn[idx];
    for(const auto &src : inp_info) {
      Port_ID offset = 0;
      for(const auto &inp : src.second) {
        char         namebuffer[255];
        Abc_Obj_t *  pseduo_memory_output = nullptr;
        Index_ID     src_idx              = inp.idx;
        int          src_bits             = inp.offset[0];
        index_offset rhs                  = {src_idx, 0, {src_bits, src_bits}};
        Node_Type_Op this_node_type       = g->node_type_get(src_idx).op;

        if(this_node_type == TechMap_Op) {
          const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(src_idx));
          pseduo_memory_output   = Abc_NtkCreatePo(pAig);
          if(is_latch(tcell)) {
            Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
            pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
            Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
            Abc_ObjAddFanin(pbuf, graph_info->sequential_cell[src_idx].pLatchOutput);
            Abc_ObjAddFanin(pwire, pbuf);

            Abc_ObjAddFanin(pseduo_memory_output, pwire);
          } else {
            Abc_ObjAddFanin(pseduo_memory_output, graph_info->combinational_cell[src_idx].pNodeOutput);
          }
        } else if(this_node_type == GraphIO_Op) {
          assert(g->is_graph_input(src_idx));
          pseduo_memory_output = Abc_NtkCreatePo(pAig);

          Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
          pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
          Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
          Abc_ObjAddFanin(pbuf, graph_info->primary_input[rhs].PIOut);
          Abc_ObjAddFanin(pwire, pbuf);
          Abc_ObjAddFanin(pseduo_memory_output, pwire);
        } else if(this_node_type == U32Const_Op || this_node_type == StrConst_Op) {
          pseduo_memory_output = Abc_NtkCreatePo(pAig);
          Abc_ObjAddFanin(pseduo_memory_output, gen_const_from_lgraph(g, rhs, pAig));
        } else if(this_node_type == Memory_Op) {
          pseduo_memory_output = Abc_NtkCreatePo(pAig);
          Abc_ObjAddFanin(pseduo_memory_output, gen_pseudo_memory_input(inp, pAig));
        } else if(this_node_type == SubGraph_Op) {
          pseduo_memory_output = Abc_NtkCreatePo(pAig);
          Abc_ObjAddFanin(pseduo_memory_output, gen_pseudo_subgraph_input(inp, pAig));
        } else {
          assert(false);
        }
        assert(pseduo_memory_output != nullptr);
        sprintf(namebuffer, "%%memory_input_%ld_%d_%d%%", idx, src.first, offset);
        Abc_ObjAssignName(Abc_ObjFanin0Ntk(pseduo_memory_output), namebuffer, NULL);
        index_offset info                 = {idx, src.first, {offset, offset}};
		graph_info->memory_generated_input_wire[info] = std::string(namebuffer);
        offset++;
      }
    }
  }
}

/************************************************************************
 * Function:  Pass_abc::conn_subgraph
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all generated subgraph signal
 ***********************************************************************/
void Pass_abc::conn_subgraph(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : graph_info->subgraph_id) {
    auto inp_info = graph_info->subgraph_conn[idx];
    for(const auto &src : inp_info) {
      Port_ID offset = 0;
      for(const auto &inp : src.second) {
        char         namebuffer[255];
        Abc_Obj_t *  pseduo_subgraph_output = nullptr;
        Index_ID     src_idx                = inp.idx;
        int          src_bits               = inp.offset[0];
        index_offset rhs                    = {src_idx, 0, {src_bits, src_bits}};
        Node_Type_Op this_node_type         = g->node_type_get(src_idx).op;

        if(this_node_type == TechMap_Op) {
          const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(src_idx));
          pseduo_subgraph_output = Abc_NtkCreatePo(pAig);
          if(is_latch(tcell)) {
            Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
            pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
            Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
            Abc_ObjAddFanin(pbuf, graph_info->sequential_cell[src_idx].pLatchOutput);
            Abc_ObjAddFanin(pwire, pbuf);

            Abc_ObjAddFanin(pseduo_subgraph_output, pwire);
          } else {
            Abc_ObjAddFanin(pseduo_subgraph_output, graph_info->combinational_cell[src_idx].pNodeOutput);
          }
        }

        else if(this_node_type == GraphIO_Op) {
          assert(g->is_graph_input(src_idx));
          pseduo_subgraph_output = Abc_NtkCreatePo(pAig);

          Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
          pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
          Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
          Abc_ObjAddFanin(pbuf, graph_info->primary_input[rhs].PIOut);
          Abc_ObjAddFanin(pwire, pbuf);
          Abc_ObjAddFanin(pseduo_subgraph_output, pwire);

        }

        else if(this_node_type == U32Const_Op) {
          pseduo_subgraph_output = Abc_NtkCreatePo(pAig);
          Abc_ObjAddFanin(pseduo_subgraph_output, gen_const_from_lgraph(g, rhs, pAig));
        } else if(this_node_type == StrConst_Op) {
          pseduo_subgraph_output = Abc_NtkCreatePo(pAig);
          Abc_ObjAddFanin(pseduo_subgraph_output, gen_const_from_lgraph(g, rhs, pAig));
        }

        else if(this_node_type == Memory_Op) {
          pseduo_subgraph_output = Abc_NtkCreatePo(pAig);
          Abc_ObjAddFanin(pseduo_subgraph_output, gen_pseudo_memory_input(inp, pAig));
        }

        else if(this_node_type == SubGraph_Op) {
          pseduo_subgraph_output = Abc_NtkCreatePo(pAig);
          Abc_ObjAddFanin(pseduo_subgraph_output, gen_pseudo_subgraph_input(inp, pAig));
        } else {
          assert(false);
        }
        assert(pseduo_subgraph_output != nullptr);
        sprintf(namebuffer, "%%subgraph_input_%ld_%d_%d%%", idx, src.first, offset);
        Abc_ObjAssignName(Abc_ObjFanin0Ntk(pseduo_subgraph_output), namebuffer, NULL);
        index_offset info                   = {idx, src.first, {offset, offset}};
		graph_info->subgraph_generated_input_wire[info] = std::string(namebuffer);
        offset++;
      }
    }
  }
}

/************************************************************************
 * Function:  Pass_abc::gen_latch
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: create Latch nodes from Lgraph
 ***********************************************************************/
void Pass_abc::gen_latch_from_lgraph(const LGraph *g, Abc_Ntk_t *pAig) {
  char namebuffer[64];
  for(const auto &idx : graph_info->latch_id) {
    Abc_Obj_t *pLatch, *pLatchInput, *pLatchOutput;
    pLatch       = Abc_NtkCreateLatch(pAig);
    pLatchInput  = Abc_NtkCreateBi(pAig);
    pLatchOutput = Abc_NtkCreateBo(pAig);
    Abc_LatchSetInitDc(pLatch);
    Abc_ObjAddFanin(pLatch, pLatchInput);
    Abc_ObjAddFanin(pLatchOutput, pLatch);
    Abc_Obj_t *pNet;
    pNet = Abc_NtkCreateNet(pAig);
    Abc_ObjAddFanin(pNet, pLatchOutput);

    for(const auto &output : g->out_edges(idx)) {
      // Latch is CI/CO
      if(g->get_node_wirename(idx) != nullptr) {
        sprintf(namebuffer, "%s_%%r", g->get_node_wirename(idx));
        Abc_ObjAssignName(pNet, namebuffer, NULL);
        break;
      } else if(g->node_type_get(output.get_idx()).op == Join_Op) {
        assert(g->get_bits(output.get_idx()) > 1);
        if(g->get_node_wirename(output.get_idx()) != nullptr) {
          sprintf(namebuffer, "%s_%%r_%d", g->get_node_wirename(output.get_idx()), output.get_inp_pin().get_pid());
          Abc_ObjAssignName(pNet, namebuffer, NULL);
          break;
        }
      } else if(g->node_type_get(output.get_idx()).op == Pick_Op) {
        for(const auto &next_out : g->out_edges(output.get_idx())) {
          if(g->get_node_wirename(next_out.get_idx()) != nullptr) {
            sprintf(namebuffer, "%s_%%r", g->get_node_wirename(next_out.get_idx()));
            Abc_ObjAssignName(pNet, namebuffer, NULL);
            break;
          }
        }
        break;
      } else if(g->get_node_wirename(output.get_idx()) != nullptr) {
        assert(g->get_bits(output.get_idx()) == 1);
        sprintf(namebuffer, "%s_%%r", g->get_node_wirename(output.get_idx()));
        Abc_ObjAssignName(pNet, namebuffer, NULL);
        break;
      } else {
        console->error(
            "Cannot find register name in the lgraph? check Pass_yosys? Use random generated name from ABC\n");
      }
    }
    std::string latch_name(Abc_ObjName(pNet));
	graph_info->latchname2id[latch_name] = idx;
    Abc_latch new_latch      = {pLatchInput, pNet};
	graph_info->sequential_cell[idx]     = new_latch;
  }
}

/************************************************************************
 * Function:  Pass_abc::gen_primary_io
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: create Primary Input & output node from Lgraph
 ***********************************************************************/
void Pass_abc::gen_primary_io_from_lgraph(const LGraph *g, Abc_Ntk_t *pAig) {
  Abc_Obj_t *pObj;
  char       namebuffer[255];
  for(const auto &idx : graph_info->graphio_output_id) {
    int width = g->get_bits(idx);
    if(width > 1) {
      for(int i = 0; i < width; i++) {
        Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
        pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
        Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
        Abc_ObjAddFanin(pwire, pbuf);

        sprintf(namebuffer, "%s[%d]", g->get_graph_output_name(idx), i);
        pObj = Abc_NtkCreatePo(pAig);
        Abc_ObjAddFanin(pObj, pwire);
        Abc_ObjAssignName(pwire, namebuffer, NULL);
        index_offset       key                = {idx, 0, {i, i}};
        Abc_primary_output new_primary_output = {pbuf, NULL};
		graph_info->primary_output[key]                   = new_primary_output;
      }
    } else {
      Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
      pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
      Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
      Abc_ObjAddFanin(pwire, pbuf);

      sprintf(namebuffer, "%s", g->get_graph_output_name(idx));
      pObj = Abc_NtkCreatePo(pAig);
      Abc_ObjAddFanin(pObj, pwire);
      Abc_ObjAssignName(pwire, namebuffer, NULL);
      index_offset       key                = {idx, 0, {0, 0}};
      Abc_primary_output new_primary_output = {pbuf, NULL};
	  graph_info->primary_output[key]                   = new_primary_output;
    }
  }

  for(const auto &idx : graph_info->graphio_input_id) {
    int width = g->get_bits(idx);
    if(width > 1) {
      for(int i = 0; i < width; i++) {
        pObj            = Abc_NtkCreatePi(pAig);
        Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
        sprintf(namebuffer, "%s[%d]", g->get_graph_input_name(idx), i);
        Abc_ObjAssignName(pNet, namebuffer, NULL);
        Abc_ObjAddFanin(pNet, pObj);
        Abc_Obj_t *pbuf = Abc_NtkCreateNode(pAig);
        pbuf->pData     = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
        Abc_ObjAddFanin(pbuf, pNet);
        Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
        Abc_ObjAddFanin(pwire, pbuf);
        index_offset      key               = {idx, 0, {i, i}};
        Abc_primary_input new_primary_input = {pObj, pwire};
		graph_info->primary_input[key]                  = new_primary_input;
      }
    } else {
      pObj            = Abc_NtkCreatePi(pAig);
      Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
      sprintf(namebuffer, "%s", g->get_graph_input_name(idx));
      Abc_ObjAssignName(pNet, namebuffer, NULL);
      Abc_ObjAddFanin(pNet, pObj);
      Abc_Obj_t *pbuf = Abc_NtkCreateNode(pAig);
      pbuf->pData     = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
      Abc_ObjAddFanin(pbuf, pNet);
      Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
      Abc_ObjAddFanin(pwire, pbuf);
      index_offset      key               = {idx, 0, {0, 0}};
      Abc_primary_input new_primary_input = {pObj, pwire};
	  graph_info->primary_input[key]                  = new_primary_input;
    }
  }
}

/************************************************************************
 * Function:  Pass_abc::gen_combinational_cell
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: create All combinational cell from Lgraph
 ***********************************************************************/
void Pass_abc::gen_comb_cell_from_lgraph(const LGraph *g, Abc_Ntk_t *pAig) {

  for(const auto &idx : graph_info->combinational_id) {
    const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));

    if(tcell->get_name() == "$_NOT_") {
	  graph_info->combinational_cell[idx] = LGraph_CreateNot(pAig);
    } else if(tcell->get_name() == "$_AND_") {
	  graph_info->combinational_cell[idx] = LGraph_CreateAnd(pAig);
    } else if(tcell->get_name() == "$_OR_") {
	  graph_info->combinational_cell[idx] = LGraph_CreateOr(pAig);
    } else if(tcell->get_name() == "$_XOR_") {
	  graph_info->combinational_cell[idx] = LGraph_CreateXor(pAig);
    } else if(tcell->get_name() == "$_NAND_") {
	  graph_info->combinational_cell[idx] = LGraph_CreateNand(pAig);
    } else if(tcell->get_name() == "$_NOR_") {
	  graph_info->combinational_cell[idx] = LGraph_CreateNor(pAig);
    } else if(tcell->get_name() == "$_XNOR_") {
	  graph_info->combinational_cell[idx] = LGraph_CreateXnor(pAig);
    } else if(tcell->get_name() == "$_ANDNOT_") {
      //assign Y = A & (~B);
	  graph_info->combinational_cell[idx] = LGraph_CreateAndnot(pAig);
    } else if(tcell->get_name() == "$_ORNOT_") {
      //assign Y = A | (~B);
	  graph_info->combinational_cell[idx] = LGraph_CreateOrnot(pAig);
    } else if(tcell->get_name() == "$_AOI3_") {
      //assign Y = ~((A & B) | C);
	  graph_info->combinational_cell[idx] = LGraph_CreateAoi3(pAig);
    } else if(tcell->get_name() == "$_OAI3_") {
      //assign Y = ~((A | B) & C);
	  graph_info->combinational_cell[idx] = LGraph_CreateOai3(pAig);
    } else if(tcell->get_name() == "$_AOI4_") {
      //assign Y = ~((A & B) | (C & D));
	  graph_info->combinational_cell[idx] = LGraph_CreateAoi4(pAig);
    } else if(tcell->get_name() == "$_OAI4_") {
      //assign Y = ~((A | B) & (C | D));
	  graph_info->combinational_cell[idx] = LGraph_CreateOai4(pAig);
    } else if(tcell->get_name() == "$_MUX_") {
	  graph_info->combinational_cell[idx] = LGraph_CreateMUX(pAig);
    } else {
      console->error("Unknown cell type!!!!");
    }
  }
}

/************************************************************************
 * Function:  Pass_abc::gen_const
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> Abc_PIO key
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: Abc_Obj_t *
 *
 * description: create a const node based on the value of
 * 							"key.bits" of the U32Const_Op/StrConst_Op
 * 							and return the node fanout net
 ***********************************************************************/
Abc_Obj_t *Pass_abc::gen_const_from_lgraph(const LGraph *g, index_offset key, Abc_Ntk_t *pAig) {
  Abc_Obj_t *pObj = nullptr;
  Abc_Obj_t *pNet = nullptr;

  if(g->node_type_get(key.idx).op == U32Const_Op) {
    int value   = g->node_value_get(key.idx);
    int bit     = 0;
    bit         = (value >> key.offset[0]) & 1;
    pObj        = Abc_NtkCreateNode(pAig);
    pObj->pData = Hop_ManConst1((Hop_Man_t *)pAig->pManFunc);
    if(bit == 0) {
      pObj->pData = Hop_Not((Hop_Obj_t *)pObj->pData);
    }
    pNet = Abc_NtkCreateNet(pAig);
    Abc_ObjAddFanin(pNet, pObj);
    return pNet;
  } else if(g->node_type_get(key.idx).op == StrConst_Op) {
    std::string value = g->node_const_value_get(key.idx);
    int         bit   = 1;
    pObj              = Abc_NtkCreateNode(pAig);
    pObj->pData       = Hop_ManConst1((Hop_Man_t *)pAig->pManFunc);

    if(value[value.length() - key.offset[0] - 1] == '1')
      bit = 1;
    else if(value[value.length() - key.offset[0] - 1] == '0')
      bit = 0;
    else if(value[value.length() - key.offset[0] - 1] == 'x')
      bit = 1;
    else if(value[value.length() - key.offset[0] - 1] == 'z')
      bit = 0;

    if(bit == 0) {
      pObj->pData = Hop_Not((Hop_Obj_t *)pObj->pData);
    }
    pNet = Abc_NtkCreateNet(pAig);
    Abc_ObjAddFanin(pNet, pObj);
    return pNet;
  }

  return pNet;
}

/************************************************************************
 * Function:  Pass_abc::gen_pseudo_subgraph_input & gen_pseudo_memory_input
 * --------------------
 * input arg0 -> index_offset& inp
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: Abc_Obj_t *
 *
 * description: create a pseduo_input net for subgraph & memory
 ***********************************************************************/
Abc_Obj_t *Pass_abc::gen_pseudo_subgraph_input(const index_offset &inp, Abc_Ntk_t *pAig) {
  if(graph_info->pseduo_record.end() == graph_info->pseduo_record.find(graph_info->subgraph_generated_output_wire[inp])) {
    Abc_Obj_t *pseudo_subgraph_input = Abc_NtkCreatePi(pAig);
    Abc_Obj_t *pNet                  = Abc_NtkCreateNet(pAig);
    Abc_ObjAssignName(pNet, const_cast<char *>(graph_info->subgraph_generated_output_wire[inp].c_str()), NULL);
    Abc_ObjAddFanin(pNet, pseudo_subgraph_input);
	graph_info->pseduo_record[graph_info->subgraph_generated_output_wire[inp]] = pNet;
  }
  Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
  pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
  Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
  Abc_ObjAddFanin(pbuf, graph_info->pseduo_record[graph_info->subgraph_generated_output_wire[inp]]);
  Abc_ObjAddFanin(pwire, pbuf);
  return pwire;
}

Abc_Obj_t *Pass_abc::gen_pseudo_memory_input(const index_offset &inp, Abc_Ntk_t *pAig) {
  if(graph_info->pseduo_record.find(graph_info->memory_generated_output_wire[inp]) == graph_info->pseduo_record.end()) {
    Abc_Obj_t *pseudo_memory_input = Abc_NtkCreatePi(pAig);
    Abc_Obj_t *pNet                = Abc_NtkCreateNet(pAig);
    Abc_ObjAssignName(pNet, const_cast<char *>(graph_info->memory_generated_output_wire[inp].c_str()), NULL);
    Abc_ObjAddFanin(pNet, pseudo_memory_input);
	graph_info->pseduo_record[graph_info->memory_generated_output_wire[inp]] = pNet;
  }
  Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
  pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
  Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
  Abc_ObjAddFanin(pbuf, graph_info->pseduo_record[graph_info->memory_generated_output_wire[inp]]);
  Abc_ObjAddFanin(pwire, pbuf);
  return pwire;
}

/************************************************************************
 * Function:  Pass_abc::conn_combinational_cell
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all the combinational cell based on computed topology
 ***********************************************************************/
void Pass_abc::conn_combinational_cell(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : graph_info->combinational_id) {
    auto src = graph_info->comb_conn[idx];
    for(const auto &inp : src) {

      Index_ID     src_idx        = inp.idx;
      int          src_bits       = inp.offset[0];
      index_offset rhs            = {src_idx, 0, {src_bits, src_bits}};
      Node_Type_Op this_node_type = g->node_type_get(src_idx).op;

      if(this_node_type == TechMap_Op) {
        const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(src_idx));
        if(is_latch(tcell)) {
          Abc_ObjAddFanin(graph_info->combinational_cell[idx].pNodeInput, graph_info->sequential_cell.at(inp.idx).pLatchOutput);
        } else {
          Abc_ObjAddFanin(graph_info->combinational_cell[idx].pNodeInput, graph_info->combinational_cell.at(inp.idx).pNodeOutput);
        }
      } else if(this_node_type == GraphIO_Op) {
        assert(g->is_graph_input(src_idx)); // output is not allowed
        Abc_ObjAddFanin(graph_info->combinational_cell[idx].pNodeInput, graph_info->primary_input[rhs].PIOut);
      } else if(this_node_type == U32Const_Op || this_node_type == StrConst_Op) {
        Abc_ObjAddFanin(graph_info->combinational_cell[idx].pNodeInput, gen_const_from_lgraph(g, rhs, pAig));
      } else if(this_node_type == Memory_Op) {
        if(graph_info->pseduo_record.find(graph_info->memory_generated_output_wire[inp]) == graph_info->pseduo_record.end()) {
          Abc_Obj_t *pseudo_memory_input = Abc_NtkCreatePi(pAig);
          Abc_Obj_t *pNet                = Abc_NtkCreateNet(pAig);
          Abc_ObjAssignName(pNet, const_cast<char *>(graph_info->memory_generated_output_wire[inp].c_str()), NULL);
          Abc_ObjAddFanin(pNet, pseudo_memory_input);
		  graph_info->pseduo_record[graph_info->memory_generated_output_wire[inp]] = pNet;
        }
        Abc_ObjAddFanin(graph_info->combinational_cell[idx].pNodeInput, graph_info->pseduo_record[graph_info->memory_generated_output_wire[inp]]);
      } else if(this_node_type == SubGraph_Op) {
        if(graph_info->pseduo_record.find(graph_info->subgraph_generated_output_wire[inp]) == graph_info->pseduo_record.end()) {
          Abc_Obj_t *pseudo_subgraph_input = Abc_NtkCreatePi(pAig);
          Abc_Obj_t *pNet                  = Abc_NtkCreateNet(pAig);
          Abc_ObjAssignName(pNet, const_cast<char *>(graph_info->subgraph_generated_output_wire[inp].c_str()), NULL);
          Abc_ObjAddFanin(pNet, pseudo_subgraph_input);
		  graph_info->pseduo_record[graph_info->subgraph_generated_output_wire[inp]] = pNet;
        }
        Abc_ObjAddFanin(graph_info->combinational_cell[idx].pNodeInput, graph_info->pseduo_record[graph_info->subgraph_generated_output_wire[inp]]);
      } else {
        assert(false);
      }
    }
  }
}

/************************************************************************
 * Function:  Pass_abc::conn_latch
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all the sequential cell based on computed topology
 ***********************************************************************/
void Pass_abc::conn_latch(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : graph_info->latch_id) {
    auto src = graph_info->latch_conn[idx];
    for(const auto &inp : src) {
      Index_ID     src_idx        = inp.idx;
      int          src_bits       = inp.offset[0];
      index_offset rhs            = {src_idx, 0, {src_bits, src_bits}};
      Node_Type_Op this_node_type = g->node_type_get(src_idx).op;

      if(this_node_type == TechMap_Op) {
        const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(src_idx));
        if(is_latch(tcell)) {
          Abc_ObjAddFanin(graph_info->sequential_cell[idx].pLatchInput, graph_info->sequential_cell[inp.idx].pLatchOutput);
        } else {
          Abc_ObjAddFanin(graph_info->sequential_cell[idx].pLatchInput, graph_info->combinational_cell[inp.idx].pNodeOutput);
        }
      } else if(this_node_type == GraphIO_Op) {
        assert(g->is_graph_input(src_idx));
        Abc_ObjAddFanin(graph_info->sequential_cell[idx].pLatchInput, graph_info->primary_input[rhs].PIOut);
      } else if(this_node_type == U32Const_Op || this_node_type == StrConst_Op) {
        Abc_ObjAddFanin(graph_info->sequential_cell[idx].pLatchInput, gen_const_from_lgraph(g, rhs, pAig));
      } else if(this_node_type == Memory_Op) {
        if(graph_info->pseduo_record.find(graph_info->memory_generated_output_wire[inp]) == graph_info->pseduo_record.end()) {
          Abc_Obj_t *pseudo_memory_input = Abc_NtkCreatePi(pAig);
          Abc_Obj_t *pNet                = Abc_NtkCreateNet(pAig);
          Abc_ObjAssignName(pNet, const_cast<char *>(graph_info->memory_generated_output_wire[inp].c_str()), NULL);
          Abc_ObjAddFanin(pNet, pseudo_memory_input);
		  graph_info->pseduo_record[graph_info->memory_generated_output_wire[inp]] = pNet;
        }
        Abc_ObjAddFanin(graph_info->sequential_cell[idx].pLatchInput, graph_info->pseduo_record[graph_info->memory_generated_output_wire[inp]]);
      } else if(this_node_type == SubGraph_Op) {
        if(graph_info->pseduo_record.find(graph_info->subgraph_generated_output_wire[inp]) == graph_info->pseduo_record.end()) {
          Abc_Obj_t *pseudo_subgraph_input = Abc_NtkCreatePi(pAig);
          Abc_Obj_t *pNet                  = Abc_NtkCreateNet(pAig);
          Abc_ObjAssignName(pNet, const_cast<char *>(graph_info->subgraph_generated_output_wire[inp].c_str()), NULL);
          Abc_ObjAddFanin(pNet, pseudo_subgraph_input);
		  graph_info->pseduo_record[graph_info->subgraph_generated_output_wire[inp]] = pNet;
        }
        Abc_ObjAddFanin(graph_info->sequential_cell[idx].pLatchInput, graph_info->pseduo_record[graph_info->subgraph_generated_output_wire[inp]]);
      } else {
        assert(false);
      }
    }
  }
}

/************************************************************************
 * Function:  Pass_abc::conn_primary_output
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all the primary output based on computed topology
 ***********************************************************************/
void Pass_abc::conn_primary_output(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : graph_info->graphio_output_id) {
    auto src       = graph_info->primary_output_conn[idx];
    int  bit_index = 0;
    for(const auto &inp : src) {
      Index_ID     src_idx        = inp.idx;
      int          src_bits       = inp.offset[0];
      index_offset lhs            = {idx, 0, {bit_index, bit_index}};
      index_offset rhs            = {src_idx, 0, {src_bits, src_bits}};
      Node_Type_Op this_node_type = g->node_type_get(src_idx).op;

      if(this_node_type == TechMap_Op) {
        const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(src_idx));
        if(is_latch(tcell)) {
          Abc_Obj_t *pbuf = Abc_NtkCreateNode(pAig);
          pbuf->pData     = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
          Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
          Abc_ObjAddFanin(pbuf, graph_info->sequential_cell[src_idx].pLatchOutput);
          Abc_ObjAddFanin(pNet, pbuf);
          Abc_ObjAddFanin(graph_info->primary_output[lhs].PO, pNet);
        } else {
          Abc_ObjAddFanin(graph_info->primary_output[lhs].PO, graph_info->combinational_cell[src_idx].pNodeOutput);
        }
      } else if(this_node_type == GraphIO_Op) {
        assert(g->is_graph_input(src_idx));
        Abc_ObjAddFanin(graph_info->primary_output[lhs].PO, graph_info->primary_input[rhs].PIOut);
      } else if(this_node_type == U32Const_Op || this_node_type == StrConst_Op) {
        Abc_ObjAddFanin(graph_info->primary_output[lhs].PO, gen_const_from_lgraph(g, rhs, pAig));
      } else if(this_node_type == Memory_Op) {
        Abc_ObjAddFanin(graph_info->primary_output[lhs].PO, gen_pseudo_memory_input(inp, pAig));
      } else if(this_node_type == SubGraph_Op) {
        Abc_ObjAddFanin(graph_info->primary_output[lhs].PO, gen_pseudo_subgraph_input(inp, pAig));
      } else {
        assert(false);
      }
      bit_index++;
    }
  }
}

/************************************************************************
 * Function:  Pass_abc::conn_reset
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all generated reset signal
 ***********************************************************************/
void Pass_abc::conn_reset(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &rg : graph_info->reset_group_map) {
    std::string reset_name = rg.first;
    //check if this reset is the graph IO otherwise create it
    Index_ID idx = graph_info->reset_id[reset_name];
    if(!g->is_graph_input(idx)) {
      Abc_Obj_t *generated_reset = Abc_NtkCreatePo(pAig); // create generated reset here
      assert((g->node_type_get(idx).op == TechMap_Op));
      const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
      if(is_latch(tcell)) {
        Abc_ObjAddFanin(generated_reset, graph_info->sequential_cell[idx].pLatchOutput);
      } else {
        Abc_ObjAddFanin(generated_reset, graph_info->combinational_cell[idx].pNodeOutput);
      }
      Abc_ObjAssignName(Abc_ObjFanin0Ntk(generated_reset), (char *)reset_name.c_str(), NULL);
    }
  }
}

/************************************************************************
 * Function:  Pass_abc::conn_clock
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all generated clock signal
 ***********************************************************************/
void Pass_abc::conn_clock(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &sg : graph_info->skew_group_map) {
    std::string clock_name = sg.first;
    //check if this clock is the graph IO otherwise create it
    Index_ID idx = graph_info->clock_id[clock_name];
    if(!g->is_graph_input(idx)) {
      Abc_Obj_t *generated_clock = Abc_NtkCreatePo(pAig); // create generated clock signal
      assert((g->node_type_get(idx).op == TechMap_Op));
      const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
      if(is_latch(tcell)) {
        Abc_ObjAddFanin(generated_clock, graph_info->sequential_cell[idx].pLatchOutput);
      } else {
        Abc_ObjAddFanin(generated_clock, graph_info->combinational_cell[idx].pNodeOutput);
      }
      Abc_ObjAssignName(Abc_ObjFanin0Ntk(generated_clock), (char *)clock_name.c_str(), NULL);
    }
  }
}
