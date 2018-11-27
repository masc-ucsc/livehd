//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 5/23/18.

#include "lgraph.hpp"

#include "pass_abc.hpp"

void Pass_abc::write_src_info(const LGraph *g, const index_offset &inp, std::ofstream &fs) {
  auto src_idx  = inp.idx;
  auto src_type = g->node_type_get(src_idx).op;
  switch(src_type) {
  case TechMap_Op: {
    const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(src_idx));
    if(is_latch(tcell)) {
      fs << g->get_node_wirename(src_idx) << " ";
    } else {
      std::string gate_name = "g" + std::to_string(src_idx);
      fs << gate_name << " ";
    }
    break;
  }
  case StrConst_Op: {
    std::string value = g->node_const_value_get(src_idx);
    auto        bit   = (value == "1") ? 1 : 0;
    if(bit == 0) {
      fs << "$false ";
    } else {
      fs << "$true ";
    }
    break;
  }
  case GraphIO_Op: {
    assert(g->is_graph_input(src_idx));
    fs << g->get_node_wirename(src_idx) << " ";
    break;
  }
  default: {
    fmt::print("not supported operator after mapped lgraph! {}\n", g->node_type_get(src_idx).get_name());
    break;
  }
  }
}

void Pass_abc::dump_blif(const LGraph *g, const std::string &filename) {
  auto mapped = is_techmap(g);
  assert(mapped);
  find_cell_conn(g);

  std::ofstream fs;
  fs.open(filename, std::ios::out | std::ios::trunc);
  if(!fs.is_open()) {
    std::cerr << "ERROR: could not open blif file [" << filename << "]";
    exit(-4);
  }
  gen_module(g, fs);
  gen_io_conn(g, fs);
  gen_cell_conn(g, fs);
  gen_latch_conn(g, fs);
  fs << ".end\n";
  fs.close();
}

void Pass_abc::gen_module(const LGraph *g, std::ofstream &fs) {
  fs << ".model " << g->get_name() << "\n";
  fs << ".inputs";
  for(const auto &idx : graph_info->graphio_input_id) {
    int width = g->get_bits(idx);
    if(width > 1) {
      for(int j = 0; j < width; j++) {
        fs << " " << g->get_graph_input_name(idx) << "[" << j << "]";
      }
    } else {
      fs << " " << g->get_graph_input_name(idx);
    }
  }
  fs << "\n";
  fs << ".outputs";
  for(const auto &idx : graph_info->graphio_output_id) {
    int width = g->get_bits(idx);
    if(width > 1) {
      for(int j = 0; j < width; j++) {
        fs << " " << g->get_graph_output_name(idx) << "[" << j << "]";
      }
    } else {
      fs << " " << g->get_graph_output_name(idx);
    }
  }
  fs << "\n";
  fs << ".names $false\n";
  fs << ".names $true\n1\n";
  fs << ".names $undef\n";
  fs << "\n";
}

void Pass_abc::gen_io_conn(const LGraph *g, std::ofstream &fs) {
  for(const auto &idx : graph_info->graphio_output_id) {
    auto src = graph_info->primary_output_conn[idx];
    assert(src.size() == 1);
    if(g->get_node_wirename(src[0].idx) != nullptr && strcmp(g->get_node_wirename(src[0].idx), g->get_graph_output_name(idx)) == 0)
      continue;
    fs << ".names ";
    for(const auto &inp : src) {
      write_src_info(g, inp, fs);
    }
    fs << g->get_graph_output_name(idx);
    fs << "\n1 1\n";
  }
}

void Pass_abc::gen_cell_conn(const LGraph *g, std::ofstream &fs) {
  for(const auto &idx : graph_info->combinational_id) {
    auto              src        = graph_info->comb_conn[idx];
    const Tech_cell * tcell      = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
    const std::string tcell_name = tcell->get_name();
    fs << ".names ";
    for(const auto &inp : src) {
      write_src_info(g, inp, fs);
    }
    std::string gate_name = "g" + std::to_string(idx);
    fs << gate_name;
    if(tcell_name == "$_BUF_") {
      fs << "\n1 1\n";
    }
    if(tcell_name == "$_NOT_") {
      fs << "\n0 1\n";
    } else if(tcell_name == "$_AND_") {
      fs << "\n11 1\n";
    } else if(tcell_name == "$_OR_") {
      fs << "\n1- 1\n-1 1\n";
    } else if(tcell_name == "$_XOR_") {
      fs << "\n10 1\n01 1\n";
    } else if(tcell_name == "$_NAND_") {
      fs << "\n0- 1\n-0 1\n";
    } else if(tcell_name == "$_NOR_") {
      fs << "\n00 1\n";
    } else if(tcell_name == "$_XNOR_") {
      fs << "\n11 1\n00 1\n";
    } else if(tcell_name == "$_ANDNOT_") {
      fs << "\n10 1\n";
    } else if(tcell_name == "$_ORNOT_") {
      fs << "\n1- 1\n-0 1\n";
    } else if(tcell_name == "$_AOI3_") {
      fs << "\n-00 1\n0-0 1\n";
    } else if(tcell_name == "$_OAI3_") {
      fs << "\n00- 1\n--0 1\n";
    } else if(tcell_name == "$_AOI4_") {
      fs << "\n-0-0 1\n-00- 1\n0--0 1\n0-0- 1\n";
    } else if(tcell_name == "$_OAI4_") {
      fs << "\n00-- 1\n--00 1\n";
    } else if(tcell_name == "$_MUX_") {
      fs << "\n1-0 1\n-11 1\n";
    }
  }
}

void Pass_abc::gen_latch_conn(const LGraph *g, std::ofstream &fs) {
  for(const auto &idx : graph_info->latch_id) {
    auto src = graph_info->latch_conn[idx];
    fs << ".latch ";
    for(const auto &inp : src) {
      write_src_info(g, inp, fs);
    }
    fs << g->get_node_wirename(idx) << " "
       << "re ";

    std::string ck_name;
    for(const auto &sg : graph_info->skew_group_map) {
      if(sg.second.find(idx) != sg.second.end()) {
        ck_name = sg.first;
        break;
      }
    }
    assert(!ck_name.empty());
    fs << ck_name << " 2\n";
  }
}