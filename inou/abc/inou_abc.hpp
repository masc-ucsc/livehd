//
// Created by birdeclipse on 1/30/18.
//
#include "inou.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"
#include "options.hpp"
#include <string>

#ifndef LGRAPH_INOU_ABC_HPP
#define LGRAPH_INOU_ABC_HPP

#ifdef __cplusplus
extern "C" {
#endif
#include <base/abc/abc.h>
#include <base/main/abcapis.h>
#include <base/main/main.h>
#include <map/mio/mio.h>
#ifdef __cplusplus
}
#endif

class Inou_abc_options_pack : public Options_pack {
public:
  Inou_abc_options_pack();

  std::string lef_file;
  std::string liberty_file;
  std::string verbose;
};

class Inou_abc : public Inou {

protected:
  Inou_abc_options_pack opack;

public:
  struct IndexID_Hash {
    inline std::size_t operator()(const Index_ID &k) const {
      return (size_t)k;
    }
  };

  struct Abc_primary_input {
    Abc_Obj_t *PI;    // PI node
    Abc_Obj_t *PIOut; //Fanout of this node type: nets
  };

  struct Abc_primary_output {
    Abc_Obj_t *PO;    // PI node
    Abc_Obj_t *POOut; //Fanout of this node type: nets
  };

  struct Abc_latch {
    Abc_Obj_t *pLatchInput;  // Node itself is the fanin
    Abc_Obj_t *pLatchOutput; //Fanout of this node type: nets
  };

  struct Abc_comb {
    Abc_Obj_t *pNodeInput;  // Node it self is the fanin
    Abc_Obj_t *pNodeOutput; //Fanout of this node type: nets
  };

  struct index_offset {

    Index_ID idx;
    Port_ID  pid;
    int      offset[2];

    inline bool operator==(const index_offset &rhs) const {
      return ((idx == rhs.idx) && pid == rhs.pid && offset[0] && rhs.offset[0]);
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
    Node_Pin driver;
    int      offset;
    int      width;

    Pick_ID(Node_Pin driver, int offset, int width) : driver(driver), offset(offset), width(width) {
    }

    bool operator<(const Pick_ID other) const {
      return (driver < other.driver) || (driver == other.driver && offset < other.offset) ||
             (driver == other.driver && offset == other.offset && width < other.width);
    }
  };

  struct graph_topology {

	using topology_info = std::vector<index_offset>;
	using name2id       = std::unordered_map<std::string, Index_ID>;
	using po_group      = std::map<index_offset, Abc_primary_output>;
	using pi_group      = std::map<index_offset, Abc_primary_input>;
	using cell_group    = std::unordered_map<Index_ID, Abc_comb, IndexID_Hash>;
	using latch_group   = std::unordered_map<Index_ID, Abc_latch, IndexID_Hash>;
	using skew_group    = std::map<std::string, std::set<Index_ID>>;
	using reset_group   = std::map<std::string, std::set<Index_ID>>;
	using node_conn     = std::unordered_map<Index_ID, topology_info, IndexID_Hash>;
	using block_conn    = std::unordered_map<Index_ID, std::unordered_map<Port_ID, topology_info>, IndexID_Hash>;
	using pseduo_name   = std::map<index_offset, std::string>;
	using idremap       = std::unordered_map<Index_ID, Index_ID>;
	using pidremap      = std::unordered_map<Index_ID, std::unordered_map<Port_ID, Index_ID>>;
	using ptr2id        = std::unordered_map<Abc_Obj_t *, Index_ID>;
	using id2pid        = std::unordered_map<Index_ID, Port_ID, IndexID_Hash>;
	using value_size    = std::pair<uint32_t, uint32_t>;
	using value2idx     = std::map<value_size, Index_ID>;
	using picks2pin     = std::map<Pick_ID, Node_Pin>;
	using record        = std::unordered_map<std::string, Abc_Obj_t *>;

	std::vector<Index_ID> combinational_id;
	std::vector<Index_ID> latch_id;
	std::vector<Index_ID> graphio_input_id;
	std::vector<Index_ID> graphio_output_id;
	std::vector<Index_ID> subgraph_id;
	std::vector<Index_ID> memory_id;

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

  Inou_abc();

  virtual ~Inou_abc();

  std::vector<LGraph *> generate() final;

  using Inou::generate;

  void generate(std::vector<const LGraph *> &out) final;

  void dump_blif(const LGraph *g, const std::string filename);

private:
  graph_topology *graph_info;

  bool is_techmap(const LGraph *g);

  bool is_latch(const Tech_cell *tcell) {
    std::string cell_name = tcell->get_name();
    std::string flop      = "FF";
    std::string latch     = "LATCH";
    if(cell_name.find(flop) != std::string::npos) {
      return true;
    } else
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

  void recursive_find(const LGraph *g, const Edge *input, graph_topology::topology_info &pid, int *bit_addr);

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

  Node_Pin create_pick_operator(LGraph *g, const Node_Pin &driver, int offset, int width);

  void connect_constant(LGraph *g, uint32_t value, uint32_t size, const Node_Pin &dst);

  void conn_latch(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void conn_primary_output(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void conn_combinational_cell(LGraph *new_graph, const LGraph *old_graph, Abc_Ntk_t *pNtk);

  void gen_module(const LGraph *g, std::ofstream &fs);

  void gen_io_conn(const LGraph *g, std::ofstream &fs);

  void gen_cell_conn(const LGraph *g, std::ofstream &fs);

  void gen_latch_conn(const LGraph *g, std::ofstream &fs);

  void write_src_info(const LGraph *g, const Inou_abc::index_offset &inp, std::ofstream &fs);
};

#endif //LGRAPH_INOU_ABC_HPP
