//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 1/30/18.
//
#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"

#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"
#include "options.hpp"
#include "pass.hpp"

extern "C" {
#include "base/abc/abc.h"
#include "base/main/abcapis.h"
#include "base/main/main.h"
#include "map/mio/mio.h"
}

class Pass_abc : public Pass {

protected:
  class Pass_abc_options {
  public:
    Pass_abc_options() {
      verbose = false;
      odir    = ".";
    };

    std::string liberty_file;
    std::string blif_file;
    std::string odir;
    bool        verbose;
  };
  Pass_abc_options opack;

  static Abc_Frame_t *pAbc;

  const std::string cmd_mapping;
  const std::string cmd_readlib;
  const std::string cmd_synthesis;

  static void tmap(Eprp_var &var);
  static void optimize(Eprp_var &var);

  LGraph *regen(const LGraph *lg);
  void    trans(LGraph *lg);
  void    dump_blif(const LGraph *g, const std::string &filename);

public:
  struct IndexID_Hash {
    inline std::size_t operator()(const Index_ID k) const {
      return (size_t)k.value;
    }
  };

  struct Abc_primary_input {
    Abc_Obj_t *PI;    // PI node
    Abc_Obj_t *PIOut; // Fanout of this node type: nets
  };

  struct Abc_primary_output {
    Abc_Obj_t *PO;    // PI node
    Abc_Obj_t *POOut; // Fanout of this node type: nets
  };

  struct Abc_latch {
    Abc_Obj_t *pLatchInput;  // Node itself is the fanin
    Abc_Obj_t *pLatchOutput; // Fanout of this node type: nets
  };

  struct Abc_comb {
    Abc_Obj_t *pNodeInput;  // Node it self is the fanin
    Abc_Obj_t *pNodeOutput; // Fanout of this node type: nets
  };

  struct index_offset {

    Index_ID idx;
    Port_ID  pid;
    int      offset[2];

    inline bool operator==(const index_offset &rhs) const {
      return ((idx == rhs.idx) && (pid == rhs.pid) && (offset[0] == rhs.offset[0]));
    }

    inline bool operator<(const index_offset &rhs) const {
      if(idx < rhs.idx)
        return true;
      else if(idx == rhs.idx) {
        if(pid < rhs.pid)
          return true;
        else if(pid == rhs.pid) {
          return offset[0] < rhs.offset[0];
        } else
          return false;
      } else
        return false;
    }

    inline bool operator()(const index_offset &lhs, const index_offset &rhs) const {
      if(lhs.idx < rhs.idx)
        return true;
      else if(lhs.idx == rhs.idx) {
        if(lhs.pid < rhs.pid)
          return true;
        else if(lhs.pid == rhs.pid) {
          return lhs.offset[0] < rhs.offset[0];
        } else
          return false;
      } else
        return false;
    }
  };

  struct Pick_ID {
    Node_pin driver;
    int      offset;
    int      width;

    Pick_ID(Node_pin driver, int offset, int width)
        : driver(driver)
        , offset(offset)
        , width(width) {
    }

    bool operator<(const Pick_ID other) const {
      return (driver < other.driver) || (driver == other.driver && offset < other.offset) ||
             (driver == other.driver && offset == other.offset && width < other.width);
    }
  };

  struct graph_topology {

    using topology_info = std::vector<index_offset>;
    using name2id       = absl::flat_hash_map<std::string, uint64_t>;
    using cell_group    = absl::flat_hash_map<uint64_t, Abc_comb, IndexID_Hash>;
    using latch_group   = absl::flat_hash_map<uint64_t, Abc_latch, IndexID_Hash>;
    using skew_group    = absl::flat_hash_map<std::string, std::set<uint64_t>>;
    using reset_group   = absl::flat_hash_map<std::string, std::set<uint64_t>>;
    using node_conn     = absl::flat_hash_map<uint64_t, topology_info, IndexID_Hash>;
    using block_conn    = absl::flat_hash_map<uint64_t, absl::flat_hash_map<Port_ID, topology_info>, IndexID_Hash>;
    using idremap       = absl::flat_hash_map<uint64_t, uint64_t>;
    using pidremap      = absl::flat_hash_map<uint64_t, absl::flat_hash_map<Port_ID, uint64_t>>;
    using ptr2id        = absl::flat_hash_map<Abc_Obj_t *, uint64_t>;
    using id2pid        = absl::flat_hash_map<uint64_t, Port_ID, IndexID_Hash>;
    using value_size    = std::pair<uint32_t, uint32_t>;
    using value2idx     = absl::flat_hash_map<value_size, uint64_t>;
    using record        = absl::flat_hash_map<std::string, Abc_Obj_t *>;

    using picks2pin     = std::map<Pick_ID, Node_pin>;
    using po_group      = std::map<index_offset, Abc_primary_output>;
    using pi_group      = std::map<index_offset, Abc_primary_input>;
    using pseduo_name   = std::map<index_offset, std::string>;

    std::vector<uint64_t> combinational_id;
    std::vector<uint64_t> latch_id;
    std::vector<uint64_t> graphio_input_id;
    std::vector<uint64_t> graphio_output_id;
    std::vector<uint64_t> subgraph_id;
    std::vector<uint64_t> memory_id;

    po_group    primary_output;
    pi_group    primary_input;
    cell_group  combinational_cell;
    name2id     latchname2id;
    latch_group sequential_cell;

    skew_group  skew_group_map;
    name2id     clock_id;
    reset_group reset_group_map;
    name2id     reset_id;

    node_conn   comb_conn;
    node_conn   latch_conn;
    node_conn   primary_output_conn;
    block_conn  subgraph_conn;
    block_conn  memory_conn;
    pseduo_name subgraph_generated_output_wire;
    pseduo_name subgraph_generated_input_wire;
    pseduo_name memory_generated_output_wire;
    pseduo_name memory_generated_input_wire;
    record      pseduo_record;

    name2id   ck_remap;
    name2id   rst_remap;
    name2id   io_remap;
    idremap   subgraph_remap;
    idremap   memory_remap;
    ptr2id    cell2id;
    id2pid    cell_out_pid;
    value2idx int_const_map;
    picks2pin picks;
  };

  Pass_abc();

  void setup() final;

  virtual ~Pass_abc();

private:
  graph_topology *graph_info;

  bool setup_techmap(const LGraph *g);

  bool is_latch(const Tech_cell *tcell) const {
    std::string_view cell_name = tcell->get_name();
    std::string_view flop      = "FF";
    std::string_view latch     = "LATCH";
    if(cell_name.find(flop) != std::string::npos) {
      return true;
    }

    return cell_name.find(latch) != std::string::npos;
  }

  void clear() {
    delete graph_info;
    graph_info = new graph_topology;
  }

  void find_cell_conn(const LGraph *g);
  void find_latch_conn(const LGraph *g);
  void find_combinational_conn(const LGraph *g);
  void find_graphio_output_conn(const LGraph *g);
  void find_subgraph_conn(const LGraph *g);
  void find_memory_conn(const LGraph *g);

  void recursive_find(const LGraph *g, const Edge *input, graph_topology::topology_info &pid, int bit_addr[2]);

  Abc_Obj_t *gen_const_from_lgraph(const LGraph *g, index_offset key, Abc_Ntk_t *pAig);

  void gen_latch_from_lgraph(const LGraph *g, Abc_Ntk_t *pAig);

  void gen_primary_io_from_lgraph(const LGraph *g, Abc_Ntk_t *pAig);

  void gen_comb_cell_from_lgraph(const LGraph *g, Abc_Ntk_t *pAig);

  Abc_Obj_t *gen_pseudo_subgraph_input(const index_offset &inp, Abc_Ntk_t *pAig);

  Abc_Obj_t *gen_pseudo_memory_input(const index_offset &inp, Abc_Ntk_t *pAig);

  void conn_latch(const LGraph *g, Abc_Ntk_t *pAig);

  void conn_combinational_cell(const LGraph *g, Abc_Ntk_t *pAig);

  void conn_primary_output(const LGraph *g, Abc_Ntk_t *pAig);

  void conn_reset(const LGraph *g, Abc_Ntk_t *pAig);

  void conn_clock(const LGraph *g, Abc_Ntk_t *pAig);

  void conn_subgraph(const LGraph *g, Abc_Ntk_t *pAig);

  void conn_memory(const LGraph *g, Abc_Ntk_t *pAig);

  void gen_netList(const LGraph *g, Abc_Ntk_t *pAig);

  Abc_Ntk_t *to_abc(const LGraph *g);

  void from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void gen_latch_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void gen_primary_io_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void gen_comb_cell_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void gen_subgraph_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void gen_memory_from_abc(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  Node_pin create_pick_operator(LGraph *g, const Node_pin &driver, int offset, int width);

  void connect_constant(LGraph *g, uint32_t value, uint32_t size, const Node_pin &dst);

  void conn_latch(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void conn_primary_output(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void conn_combinational_cell(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void gen_module(const LGraph *g, std::ofstream &fs);

  void gen_io_conn(const LGraph *g, std::ofstream &fs);

  void gen_cell_conn(const LGraph *g, std::ofstream &fs);

  void gen_latch_conn(const LGraph *g, std::ofstream &fs);

  void write_src_info(const LGraph *g, const Pass_abc::index_offset &inp, std::ofstream &fs);

  void gen_generic_lib(const std::string &buffer) const;
};

