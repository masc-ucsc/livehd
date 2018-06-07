//
// Created by birdeclipse on 1/30/18.
//

#include "abc_cell.hpp"
#include "inou_abc.hpp"
#include <boost/filesystem.hpp>

const static char ReadLib_Command[]   = "read_library stdcells.genlib";
const static char Synthesis_Command[] = "print_stats;cleanup;strash;ifraig;iresyn;dc2;strash;print_stats;";
const static char Mapping_Command[]   = "map;print_stats";

static void gen_generic_lib() {
  std::string buffer = "stdcells.genlib";
  FILE *      f      = fopen(buffer.c_str(), "wt");
  if(f == NULL)
    console->error("Opening {} for writing failed: {}\n", buffer.c_str(), strerror(errno));
  fprintf(f, "GATE ZERO    1 Y=CONST0;\n");
  fprintf(f, "GATE ONE     1 Y=CONST1;\n");
  fprintf(f, "GATE BUF    1 Y=A;                  PIN * NONINV  1 999 1 0 1 0\n");
  fprintf(f, "GATE NOT    2 Y=!A;                 PIN * INV     1 999 1 0 1 0\n");
  fprintf(f, "GATE AND    4 Y=A*B;                PIN * NONINV  1 999 1 0 1 0\n");
  fprintf(f, "GATE NAND   4 Y=!(A*B);             PIN * INV     1 999 1 0 1 0\n");
  fprintf(f, "GATE OR     4 Y=A+B;                PIN * NONINV  1 999 1 0 1 0\n");
  fprintf(f, "GATE NOR    4 Y=!(A+B);             PIN * INV     1 999 1 0 1 0\n");
  fprintf(f, "GATE XOR    8 Y=(A*!B)+(!A*B);      PIN * UNKNOWN 1 999 1 0 1 0\n");
  fprintf(f, "GATE XNOR   8 Y=(A*B)+(!A*!B);      PIN * UNKNOWN 1 999 1 0 1 0\n");
  fprintf(f, "GATE ANDNOT 4 Y=A*!B;               PIN * UNKNOWN 1 999 1 0 1 0\n");
  fprintf(f, "GATE ORNOT  4 Y=A+!B;               PIN * UNKNOWN 1 999 1 0 1 0\n");
  fprintf(f, "GATE AOI3   6 Y=!((A*B)+C);         PIN * INV     1 999 1 0 1 0\n");
  fprintf(f, "GATE OAI3   6 Y=!((A+B)*C);         PIN * INV     1 999 1 0 1 0\n");
  fprintf(f, "GATE AOI4   8 Y=!((A*B)+(C*D));     PIN * INV     1 999 1 0 1 0\n");
  fprintf(f, "GATE OAI4   8 Y=!((A+B)*(C+D));     PIN * INV     1 999 1 0 1 0\n");
  fprintf(f, "GATE MUX    4 Y=(A*B)+(S*B)+(!S*A); PIN * UNKNOWN 1 999 1 0 1 0\n");
  fclose(f);
}

/************************************************************************
 * Function:  Inou_abc::to_abc()
 * --------------------
 * input arg0 -> const LGraph *g
 *
 * returns: mapped Abc_Ntk_t
 *
 * description: feed to abc for comb synthesis and mapping
 ***********************************************************************/
Abc_Ntk_t *Inou_abc::to_abc(const LGraph *g) {
  Abc_Frame_t *pAbc = nullptr;
  Abc_Ntk_t *  pAig = nullptr;
  Abc_Start();
  pAbc        = Abc_FrameGetGlobalFrame();
  pAig        = Abc_NtkAlloc(ABC_NTK_NETLIST, ABC_FUNC_AIG, 1);
  pAig->pName = Extra_UtilStrsav(opack.graph_name.c_str());

  fmt::print("rtp toabc\n");
  gen_netList(g, pAig);
  fmt::print("rtp netlist\n");
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
      gen_generic_lib();
      Cmd_CommandExecute(pAbc, ReadLib_Command);
    }

    std::string             path_name("abc_output");
    boost::filesystem::path dir(path_name);

    if(!(boost::filesystem::exists(dir))) {
      boost::filesystem::create_directory(dir);
    }

    if(Cmd_CommandExecute(pAbc, Synthesis_Command)) {
      console->error("Cannot execute command {}", Synthesis_Command);
    }

    sprintf(Command, "write_blif %s/%s_post.blif;write_verilog %s/%s_post.v;",
            path_name.c_str(), opack.graph_name.c_str(),
            path_name.c_str(), opack.graph_name.c_str());
    if(Cmd_CommandExecute(pAbc, Command)) {
      console->error("Cannot execute command {}", Command);
    }

    if(Cmd_CommandExecute(pAbc, Mapping_Command)) {
      console->error("Cannot execute command", Mapping_Command);
    }

    sprintf(Command, "write_blif %s/%s_map.blif;write_verilog %s/%s_map.v",
            path_name.c_str(), opack.graph_name.c_str(),
            path_name.c_str(), opack.graph_name.c_str());
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

  boost::filesystem::wpath file("stdcells.genlib");

  if(boost::filesystem::exists(file))
    boost::filesystem::remove(file);

  return pNtkMapped;
}

/************************************************************************
 * Function:  Inou_abc::gen_NetList()
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: build the netlist
 ***********************************************************************/
void Inou_abc::gen_netList(const LGraph *g, Abc_Ntk_t *pAig) {
  console->info("Inou_abc::gen_primary_io_from_lgraph():Lgraph is calling ABC API to create Primary inputs & outputs");
  fmt::print("rtp primary io {} {}\n", graphio_output_id.size(), graphio_input_id.size());
  gen_primary_io_from_lgraph(g, pAig);
  console->info("Inou_abc::gen_latch_from_lgraph():Lgraph is calling ABC API to create latches");
  gen_latch_from_lgraph(g, pAig);
  console->info("Inou_abc::gen_comb_cell_from_lgraph():Lgraph is calling ABC API to create combinational cells");
  fmt::print("rtp com {}\n", combinational_id.size());
  gen_comb_cell_from_lgraph(g, pAig);
  fmt::print("rtp cells {}\n", pAig->nObjs);

  console->info("Inou_abc::conn_combinational_cell():Lgraph is calling ABC API to connect all combinational cells");
  conn_combinational_cell(g, pAig);
  console->info("Inou_abc::conn_latch():Lgraph is calling ABC API to connect all latches");
  conn_latch(g, pAig);
  console->info("Inou_abc::conn_primary_output():Lgraph is calling ABC API to connect all primary outputs");
  conn_primary_output(g, pAig);
  console->info("Inou_abc::conn_subgraph():Lgraph is calling ABC API to connect all subgraph");
  conn_subgraph(g, pAig);
  console->info("Inou_abc::conn_memory():Lgraph is calling ABC API to connect all memory");
  conn_memory(g, pAig);
  console->info("Inou_abc::conn_clock():Lgraph is calling ABC API to create clock network if generated clock exsit");
  conn_clock(g, pAig);
  console->info("Inou_abc::conn_reset():Lgraph is calling ABC API to create reset network if generated clock exsit");
  conn_reset(g, pAig);
}

/************************************************************************
 * Function:  Inou_abc::gen_latch
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: create Latch nodes from Lgraph
 ***********************************************************************/
void Inou_abc::gen_latch_from_lgraph(const LGraph *g, Abc_Ntk_t *pAig) {
  char namebuffer[64];
  for(const auto &idx : latch_id) {
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
            "Cannot find register name in the lgraph? check inou_yosys? Use random generated name from ABC\n");
      }
    }
    std::string latch_name(Abc_ObjName(pNet));
    latchname2id[latch_name] = idx;
    Abc_latch new_latch      = {pLatchInput, pNet};
    sequential_cell[idx]     = new_latch;
  }
}

/************************************************************************
 * Function:  Inou_abc::gen_primary_io
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: create Primary Input & output node from Lgraph
 ***********************************************************************/
void Inou_abc::gen_primary_io_from_lgraph(const LGraph *g, Abc_Ntk_t *pAig) {
  Abc_Obj_t *pObj;
  char       namebuffer[255];
  for(const auto &idx : graphio_output_id) {
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
        primary_output[key]                   = new_primary_output;
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
      primary_output[key]                   = new_primary_output;
    }
  }

  for(const auto &idx : graphio_input_id) {
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
        primary_input[key]                  = new_primary_input;
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
      primary_input[key]                  = new_primary_input;
    }
  }
}

/************************************************************************
 * Function:  Inou_abc::gen_combinational_cell
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg1 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: create All combinational cell from Lgraph
 ***********************************************************************/
void Inou_abc::gen_comb_cell_from_lgraph(const LGraph *g, Abc_Ntk_t *pAig) {

  for(const auto &idx : combinational_id) {
    const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));

    if(tcell->get_name() == "$_NOT_") {
      combinational_cell[idx] = LGraph_CreateNot(pAig);
    } else if(tcell->get_name() == "$_AND_") {
      combinational_cell[idx] = LGraph_CreateAnd(pAig);
    } else if(tcell->get_name() == "$_OR_") {
      combinational_cell[idx] = LGraph_CreateOr(pAig);
    } else if(tcell->get_name() == "$_XOR_") {
      combinational_cell[idx] = LGraph_CreateXor(pAig);
    } else if(tcell->get_name() == "$_NAND_") {
      combinational_cell[idx] = LGraph_CreateNand(pAig);
    } else if(tcell->get_name() == "$_NOR_") {
      combinational_cell[idx] = LGraph_CreateNor(pAig);
    } else if(tcell->get_name() == "$_XNOR_") {
      combinational_cell[idx] = LGraph_CreateXnor(pAig);
    } else if(tcell->get_name() == "$_ANDNOT_") {
      //assign Y = A & (~B);
      combinational_cell[idx] = LGraph_CreateAndnot(pAig);
    } else if(tcell->get_name() == "$_ORNOT_") {
      //assign Y = A | (~B);
      combinational_cell[idx] = LGraph_CreateOrnot(pAig);
    } else if(tcell->get_name() == "$_AOI3_") {
      //assign Y = ~((A & B) | C);
      combinational_cell[idx] = LGraph_CreateAoi3(pAig);
    } else if(tcell->get_name() == "$_OAI3_") {
      //assign Y = ~((A | B) & C);
      combinational_cell[idx] = LGraph_CreateOai3(pAig);
    } else if(tcell->get_name() == "$_AOI4_") {
      //assign Y = ~((A & B) | (C & D));
      combinational_cell[idx] = LGraph_CreateAoi4(pAig);
    } else if(tcell->get_name() == "$_OAI4_") {
      //assign Y = ~((A | B) & (C | D));
      combinational_cell[idx] = LGraph_CreateOai4(pAig);
    } else if(tcell->get_name() == "$_MUX_") {
      combinational_cell[idx] = LGraph_CreateMUX(pAig);
    } else {
      console->error("Unknown cell type!!!!");
    }
  }
}

/************************************************************************
 * Function:  Inou_abc::gen_const
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
Abc_Obj_t *Inou_abc::gen_const_from_lgraph(const LGraph *g, index_offset key, Abc_Ntk_t *pAig) {
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
 * Function:  Inou_abc::gen_pseudo_subgraph_input & gen_pseudo_memory_input
 * --------------------
 * input arg0 -> index_offset& inp
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: Abc_Obj_t *
 *
 * description: create a pseduo_input net for subgraph & memory
 ***********************************************************************/
Abc_Obj_t *Inou_abc::gen_pseudo_subgraph_input(const index_offset &inp, Abc_Ntk_t *pAig) {
  if(pseduo_record.end() == pseduo_record.find(subgraph_generated_output_wire[inp])) {
    Abc_Obj_t *pseudo_subgraph_input = Abc_NtkCreatePi(pAig);
    Abc_Obj_t *pNet                  = Abc_NtkCreateNet(pAig);
    Abc_ObjAssignName(pNet, const_cast<char *>(subgraph_generated_output_wire[inp].c_str()), NULL);
    Abc_ObjAddFanin(pNet, pseudo_subgraph_input);
    pseduo_record[subgraph_generated_output_wire[inp]] = pNet;
  }
  Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
  pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
  Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
  Abc_ObjAddFanin(pbuf, pseduo_record[subgraph_generated_output_wire[inp]]);
  Abc_ObjAddFanin(pwire, pbuf);
  return pwire;
}

Abc_Obj_t *Inou_abc::gen_pseudo_memory_input(const index_offset &inp, Abc_Ntk_t *pAig) {
  if(pseduo_record.find(memory_generated_output_wire[inp]) == pseduo_record.end()) {
    Abc_Obj_t *pseudo_memory_input = Abc_NtkCreatePi(pAig);
    Abc_Obj_t *pNet                = Abc_NtkCreateNet(pAig);
    Abc_ObjAssignName(pNet, const_cast<char *>(memory_generated_output_wire[inp].c_str()), NULL);
    Abc_ObjAddFanin(pNet, pseudo_memory_input);
    pseduo_record[memory_generated_output_wire[inp]] = pNet;
  }
  Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
  pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
  Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
  Abc_ObjAddFanin(pbuf, pseduo_record[memory_generated_output_wire[inp]]);
  Abc_ObjAddFanin(pwire, pbuf);
  return pwire;
}

/************************************************************************
 * Function:  Inou_abc::conn_combinational_cell
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all the combinational cell based on computed topology
 ***********************************************************************/
void Inou_abc::conn_combinational_cell(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : combinational_id) {
    auto src = comb_conn[idx];
    for(const auto &inp : src) {

      Index_ID     src_idx        = inp.idx;
      int          src_bits       = inp.offset[0];
      index_offset rhs            = {src_idx, 0, {src_bits, src_bits}};
      Node_Type_Op this_node_type = g->node_type_get(src_idx).op;

      if(this_node_type == TechMap_Op) {
        const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(src_idx));
        if(is_latch(tcell)) {
          Abc_ObjAddFanin(combinational_cell[idx].pNodeInput, sequential_cell.at(inp.idx).pLatchOutput);
        } else {
          Abc_ObjAddFanin(combinational_cell[idx].pNodeInput, combinational_cell.at(inp.idx).pNodeOutput);
        }
      } else if(this_node_type == GraphIO_Op) {
        assert(g->is_graph_input(src_idx)); // output is not allowed
        Abc_ObjAddFanin(combinational_cell[idx].pNodeInput, primary_input[rhs].PIOut);
      } else if(this_node_type == U32Const_Op || this_node_type == StrConst_Op) {
        Abc_ObjAddFanin(combinational_cell[idx].pNodeInput, gen_const_from_lgraph(g, rhs, pAig));
      } else if(this_node_type == Memory_Op) {
        if(pseduo_record.find(memory_generated_output_wire[inp]) == pseduo_record.end()) {
          Abc_Obj_t *pseudo_memory_input = Abc_NtkCreatePi(pAig);
          Abc_Obj_t *pNet                = Abc_NtkCreateNet(pAig);
          Abc_ObjAssignName(pNet, const_cast<char *>(memory_generated_output_wire[inp].c_str()), NULL);
          Abc_ObjAddFanin(pNet, pseudo_memory_input);
          pseduo_record[memory_generated_output_wire[inp]] = pNet;
        }
        Abc_ObjAddFanin(combinational_cell[idx].pNodeInput, pseduo_record[memory_generated_output_wire[inp]]);
      } else if(this_node_type == SubGraph_Op) {
        if(pseduo_record.find(subgraph_generated_output_wire[inp]) == pseduo_record.end()) {
          Abc_Obj_t *pseudo_subgraph_input = Abc_NtkCreatePi(pAig);
          Abc_Obj_t *pNet                  = Abc_NtkCreateNet(pAig);
          Abc_ObjAssignName(pNet, const_cast<char *>(subgraph_generated_output_wire[inp].c_str()), NULL);
          Abc_ObjAddFanin(pNet, pseudo_subgraph_input);
          pseduo_record[subgraph_generated_output_wire[inp]] = pNet;
        }
        Abc_ObjAddFanin(combinational_cell[idx].pNodeInput, pseduo_record[subgraph_generated_output_wire[inp]]);
      } else {
        assert(false);
      }
    }
  }
}

/************************************************************************
 * Function:  Inou_abc::conn_latch
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all the sequential cell based on computed topology
 ***********************************************************************/
void Inou_abc::conn_latch(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : latch_id) {
    auto src = latch_conn[idx];
    for(const auto &inp : src) {
      Index_ID     src_idx        = inp.idx;
      int          src_bits       = inp.offset[0];
      index_offset rhs            = {src_idx, 0, {src_bits, src_bits}};
      Node_Type_Op this_node_type = g->node_type_get(src_idx).op;

      if(this_node_type == TechMap_Op) {
        const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(src_idx));
        if(is_latch(tcell)) {
          Abc_ObjAddFanin(sequential_cell[idx].pLatchInput, sequential_cell[inp.idx].pLatchOutput);
        } else {
          Abc_ObjAddFanin(sequential_cell[idx].pLatchInput, combinational_cell[inp.idx].pNodeOutput);
        }
      } else if(this_node_type == GraphIO_Op) {
        assert(g->is_graph_input(src_idx));
        Abc_ObjAddFanin(sequential_cell[idx].pLatchInput, primary_input[rhs].PIOut);
      } else if(this_node_type == U32Const_Op || this_node_type == StrConst_Op) {
        Abc_ObjAddFanin(sequential_cell[idx].pLatchInput, gen_const_from_lgraph(g, rhs, pAig));
      } else if(this_node_type == Memory_Op) {
        if(pseduo_record.find(memory_generated_output_wire[inp]) == pseduo_record.end()) {
          Abc_Obj_t *pseudo_memory_input = Abc_NtkCreatePi(pAig);
          Abc_Obj_t *pNet                = Abc_NtkCreateNet(pAig);
          Abc_ObjAssignName(pNet, const_cast<char *>(memory_generated_output_wire[inp].c_str()), NULL);
          Abc_ObjAddFanin(pNet, pseudo_memory_input);
          pseduo_record[memory_generated_output_wire[inp]] = pNet;
        }
        Abc_ObjAddFanin(sequential_cell[idx].pLatchInput, pseduo_record[memory_generated_output_wire[inp]]);
      } else if(this_node_type == SubGraph_Op) {
        if(pseduo_record.find(subgraph_generated_output_wire[inp]) == pseduo_record.end()) {
          Abc_Obj_t *pseudo_subgraph_input = Abc_NtkCreatePi(pAig);
          Abc_Obj_t *pNet                  = Abc_NtkCreateNet(pAig);
          Abc_ObjAssignName(pNet, const_cast<char *>(subgraph_generated_output_wire[inp].c_str()), NULL);
          Abc_ObjAddFanin(pNet, pseudo_subgraph_input);
          pseduo_record[subgraph_generated_output_wire[inp]] = pNet;
        }
        Abc_ObjAddFanin(sequential_cell[idx].pLatchInput, pseduo_record[subgraph_generated_output_wire[inp]]);
      } else {
        assert(false);
      }
    }
  }
}

/************************************************************************
 * Function:  Inou_abc::conn_primary_output
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all the primary output based on computed topology
 ***********************************************************************/
void Inou_abc::conn_primary_output(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : graphio_output_id) {
    auto src       = primary_output_conn[idx];
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
          Abc_ObjAddFanin(pbuf, sequential_cell[src_idx].pLatchOutput);
          Abc_ObjAddFanin(pNet, pbuf);
          Abc_ObjAddFanin(primary_output[lhs].PO, pNet);
        } else {
          Abc_ObjAddFanin(primary_output[lhs].PO, combinational_cell[src_idx].pNodeOutput);
        }
      } else if(this_node_type == GraphIO_Op) {
        assert(g->is_graph_input(src_idx));
        Abc_ObjAddFanin(primary_output[lhs].PO, primary_input[rhs].PIOut);
      } else if(this_node_type == U32Const_Op || this_node_type == StrConst_Op) {
        Abc_ObjAddFanin(primary_output[lhs].PO, gen_const_from_lgraph(g, rhs, pAig));
      } else if(this_node_type == Memory_Op) {
        Abc_ObjAddFanin(primary_output[lhs].PO, gen_pseudo_memory_input(inp, pAig));
      } else if(this_node_type == SubGraph_Op) {
        Abc_ObjAddFanin(primary_output[lhs].PO, gen_pseudo_subgraph_input(inp, pAig));
      } else {
        assert(false);
      }
      bit_index++;
    }
  }
}

/************************************************************************
 * Function:  Inou_abc::conn_reset
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all generated reset signal
 ***********************************************************************/
void Inou_abc::conn_reset(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &rg : reset_group_map) {
    std::string reset_name = rg.first;
    //check if this reset is the graph IO otherwise create it
    Index_ID idx = reset_id[reset_name];
    if(!g->is_graph_input(idx)) {
      Abc_Obj_t *generated_reset = Abc_NtkCreatePo(pAig); // create generated reset here
      assert((g->node_type_get(idx).op == TechMap_Op));
      const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
      if(is_latch(tcell)) {
        Abc_ObjAddFanin(generated_reset, sequential_cell[idx].pLatchOutput);
      } else {
        Abc_ObjAddFanin(generated_reset, combinational_cell[idx].pNodeOutput);
      }
      Abc_ObjAssignName(Abc_ObjFanin0Ntk(generated_reset), (char *)reset_name.c_str(), NULL);
    }
  }
}

/************************************************************************
 * Function:  Inou_abc::conn_clock
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all generated clock signal
 ***********************************************************************/
void Inou_abc::conn_clock(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &sg : skew_group_map) {
    std::string clock_name = sg.first;
    //check if this clock is the graph IO otherwise create it
    Index_ID idx = clock_id[clock_name];
    if(!g->is_graph_input(idx)) {
      Abc_Obj_t *generated_clock = Abc_NtkCreatePo(pAig); // create generated clock signal
      assert((g->node_type_get(idx).op == TechMap_Op));
      const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
      if(is_latch(tcell)) {
        Abc_ObjAddFanin(generated_clock, sequential_cell[idx].pLatchOutput);
      } else {
        Abc_ObjAddFanin(generated_clock, combinational_cell[idx].pNodeOutput);
      }
      Abc_ObjAssignName(Abc_ObjFanin0Ntk(generated_clock), (char *)clock_name.c_str(), NULL);
    }
  }
}

/************************************************************************
 * Function:  Inou_abc::conn_subgraph
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all generated subgraph signal
 ***********************************************************************/
void Inou_abc::conn_subgraph(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : subgraph_id) {
    auto inp_info = subgraph_conn[idx];
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
            Abc_ObjAddFanin(pbuf, sequential_cell[src_idx].pLatchOutput);
            Abc_ObjAddFanin(pwire, pbuf);

            Abc_ObjAddFanin(pseduo_subgraph_output, pwire);
          } else {
            Abc_ObjAddFanin(pseduo_subgraph_output, combinational_cell[src_idx].pNodeOutput);
          }
        }

        else if(this_node_type == GraphIO_Op) {
          assert(g->is_graph_input(src_idx));
          pseduo_subgraph_output = Abc_NtkCreatePo(pAig);

          Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
          pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
          Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
          Abc_ObjAddFanin(pbuf, primary_input[rhs].PIOut);
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
        subgraph_generated_input_wire[info] = std::string(namebuffer);
        offset++;
      }
    }
  }
}

/************************************************************************
 * Function:  Inou_abc::conn_memory
 * --------------------
 * input arg0 -> const LGraph *g
 * input arg2 -> Abc_Ntk_t *pAig
 *
 * returns: nothing
 *
 * description: connect all generated subgraph signal
 ***********************************************************************/
void Inou_abc::conn_memory(const LGraph *g, Abc_Ntk_t *pAig) {
  for(const auto &idx : memory_id) {
    auto inp_info = memory_conn[idx];
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
            Abc_ObjAddFanin(pbuf, sequential_cell[src_idx].pLatchOutput);
            Abc_ObjAddFanin(pwire, pbuf);

            Abc_ObjAddFanin(pseduo_memory_output, pwire);
          } else {
            Abc_ObjAddFanin(pseduo_memory_output, combinational_cell[src_idx].pNodeOutput);
          }
        } else if(this_node_type == GraphIO_Op) {
          assert(g->is_graph_input(src_idx));
          pseduo_memory_output = Abc_NtkCreatePo(pAig);

          Abc_Obj_t *pbuf  = Abc_NtkCreateNode(pAig);
          pbuf->pData      = Hop_IthVar((Hop_Man_t *)pAig->pManFunc, 0);
          Abc_Obj_t *pwire = Abc_NtkCreateNet(pAig);
          Abc_ObjAddFanin(pbuf, primary_input[rhs].PIOut);
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
        memory_generated_input_wire[info] = std::string(namebuffer);
        offset++;
      }
    }
  }
}