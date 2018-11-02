//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef NODETYPE_H
#define NODETYPE_H

#include <assert.h>
#include <map>
#include <string>
#include <vector>

#include "bm.h"
#include "bmsparsevec.h"
#include "char_array.hpp"
#include "dense.hpp"

#include "lgraph_base_core.hpp"

// nodetype should be at meta directory but the node type is needed all over in the base class. It may be good to integrate nodetype
// as part of the lgnode itself to avoid extra cache misses

enum Node_Type_Op : uint64_t {
  Invalid_Op,
  Sum_Op,
  Mult_Op,
  Div_Op,
  Mod_Op,
  Not_Op,
  Join_Op,
  Pick_Op,
  And_Op,
  Or_Op,
  Xor_Op,
  Flop_Op,
  AFlop_Op, // async reset flop
  Latch_Op,
  FFlop_Op,
  Memory_Op,
  LessThan_Op,
  GreaterThan_Op,
  LessEqualThan_Op,
  GreaterEqualThan_Op,
  Equals_Op,
  Mux_Op,
  ShiftRight_Op,
  ShiftLeft_Op,
  GraphIO_Op,
  DontCare_Op,
  CfgAssign_Op,
  CfgIf_Op,
  CfgFunctionCall_Op,
  CfgFor_Op,
  CfgWhile_Op,
  CfgIfMerge_Op,
  CfgBeenRead_Op,
  DfgRef_Op,
  DfgPendingGraph_Op,
  // Add here, operators needed
  SubGraph_Op,
  BlackBox_Op,
  TechMap_Op,
  U32Const_Op,
  StrConst_Op,
  SubGraphMin_Op, // Each subgraph cell starts here
  SubGraphMax_Op = (1ULL << 32),
  U32ConstMin_Op,
  U32ConstMax_Op = 2 * (1ULL << 32),
  StrConstMin_Op,
  StrConstMax_Op = 3 * (1ULL << 32),
  TechMapMin_Op,
  TechMapMax_Op = 4 * (1ULL << 32)
};

class Node_Type {
private:
  static Node_Type *                        table[StrConst_Op + 1];
  static std::map<std::string, Node_Type *> name2node;

protected:
  const std::string name;
  const bool        pipelined;

  // Both inputs/outputs sorted in alphabetical order
  std::vector<const char *> inputs;
  std::vector<const char *> outputs;
  std::vector<bool> inputs_sign;

  bool may_gen_sign;

  Node_Type(const std::string &_name, Node_Type_Op _op, bool _pipelined)
      : name(_name)
      , pipelined(_pipelined)
      , op(_op){

        may_gen_sign = false;
      };


  void setup_signs(bool can_sign) {
    may_gen_sign = can_sign;

    inputs_sign.resize(inputs.size());

    for(size_t i = 0; i < inputs.size(); i++) {
      auto len = strlen(inputs[i]);
      assert(len>0);
      inputs_sign[i] = (inputs[i][len-1] == 's');
    }

  }

public:
  const Node_Type_Op op;

  bool has_may_gen_sign() const { return may_gen_sign; }

  static Node_Type &  get(Node_Type_Op op);
  static Node_Type_Op get(const std::string &opname);
  static bool         is_type(const std::string &opname);

  const std::string &get_name() const {
    return name;
  }

  Port_ID get_input_match(const char *str) const {
    for(size_t i = 0; i < inputs.size(); i++) {
      if(strcasecmp(inputs[i], str) == 0) {
        return static_cast<Port_ID>(i);
      }
    }

    assert(false); // No match found

    return 0;
  }

  Port_ID get_output_match(const char *str) const {
    for(size_t i = 0; i < inputs.size(); i++) {
      if(strcasecmp(outputs[i], str) == 0) {
        return static_cast<Port_ID>(i);
      }
    }

    assert(false); // No match found

    return 0;
  }

  bool is_input_signed(Port_ID pid) const {
    if(inputs_sign.size()<pid)
      return inputs_sign[pid];
    return false;
  }

  bool is_pipelined() const {
    return pipelined;
  } // Can create loops

  class _init {
  public:
    _init();
  };
  static _init _static_initializer;
};

class Node_Type_Invalid : public Node_Type {
public:
  Node_Type_Invalid()
      : Node_Type("invalid", Invalid_Op, false){};
};

// Y = (As+...+As+Au+...+Au) - (Bs+...+Bs+Bu+...+Bu)
// add is a subset of Sum
// y = As + Au (is an unsigned add)
class Node_Type_Sum : public Node_Type {
public:
  Node_Type_Sum()
      : Node_Type("sum", Sum_Op, false) {
    inputs.push_back("As"); // signed
    inputs.push_back("Au"); // unsigned
    inputs.push_back("Bs");
    inputs.push_back("Bu");
    outputs.push_back("Y");

    setup_signs(true);
  };
};

// Y = As*...*As * Au*...*Au
class Node_Type_Mult : public Node_Type {
public:
  Node_Type_Mult()
      : Node_Type("mult", Mult_Op, false) {
    inputs.push_back("As");
    inputs.push_back("Au");
    outputs.push_back("Y");

    setup_signs(true);
  };
};

// Y = (As|Au)/(Bs/Bu)
class Node_Type_Div : public Node_Type {
public:
  Node_Type_Div()
      : Node_Type("div", Div_Op, false) {
    inputs.push_back("As");
    inputs.push_back("Au");
    inputs.push_back("Bs");
    inputs.push_back("Bu");
    outputs.push_back("Y");

    setup_signs(true);
  };
};

// Y = (As|Au)%(Bs/Bu)
class Node_Type_Mod : public Node_Type {
public:
  Node_Type_Mod()
      : Node_Type("mod", Mod_Op, false) {
    inputs.push_back("As");
    inputs.push_back("Au");
    inputs.push_back("Bs");
    inputs.push_back("Bu");
    outputs.push_back("Y");

    setup_signs(true);
  };
};

class Node_Type_Not : public Node_Type {
public:
  Node_Type_Not()
      : Node_Type("not", Not_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

// Y = {A,B,C,D,E....}
class Node_Type_Join : public Node_Type {
public:
  Node_Type_Join()
      : Node_Type("join", Join_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    inputs.push_back("C");
    inputs.push_back("D");
    inputs.push_back("E");
    inputs.push_back("F");
    outputs.push_back("Y");
  };
};

// Y = A[i,j]
// pid 0 : A
// pid 1 : offset
// j = offset + bits
class Node_Type_Pick : public Node_Type {
public:
  Node_Type_Pick()
      : Node_Type("pick", Pick_Op, false) {
    inputs.push_back("A");
    inputs.push_back("offset");
    outputs.push_back("Y");
  };
};

// Y0 = A & A & A...
// Y1 = & {A, A, A...} //reduction
class Node_Type_And : public Node_Type {
public:
  Node_Type_And()
      : Node_Type("and", And_Op, false) {
    inputs.push_back("A");        // pid: 0
    outputs.push_back("Y");       // pid: 0
    outputs.push_back("Yreduce"); // pid: 1
  };
};

// Y0 = A | A | A...
// Y1 = | {A, A, A...} //reduction
class Node_Type_Or : public Node_Type {
public:
  Node_Type_Or()
      : Node_Type("or", Or_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
    outputs.push_back("Yreduce");
  };
};

// Y0 = A ^ A ^ A...
// Y1 = ^ {A, A, A...} //reduction
class Node_Type_Xor : public Node_Type {
public:
  Node_Type_Xor()
      : Node_Type("xor", Xor_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
    outputs.push_back("Yreduce");
  };
};

// Y = (As|AU) < (Bs|Bu)
// it can not have both As and Au at the same time
// it can not have both Bs and Bu at the same time
// If many edges are connected, it is and AND.
// E.g:
// LessThan A , (B C) means (LessThan A , B) and (LessThan A, C)
class Node_Type_LessThan : public Node_Type {
public:
  Node_Type_LessThan()
      : Node_Type("lessthan", LessThan_Op, false) {
    inputs.push_back("As");
    inputs.push_back("Au");
    inputs.push_back("Bs");
    inputs.push_back("Bu");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = (As|Au) > (Bs|Bu)
class Node_Type_GreaterThan : public Node_Type {
public:
  Node_Type_GreaterThan()
      : Node_Type("greaterthan", GreaterThan_Op, false) {
    inputs.push_back("As");
    inputs.push_back("Au");
    inputs.push_back("Bs");
    inputs.push_back("Bu");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = (As|Au) >= (Bs|Bu)
class Node_Type_GreaterEqualThan : public Node_Type {
public:
  Node_Type_GreaterEqualThan()
      : Node_Type("greaterequalthan", GreaterEqualThan_Op, false) {
    inputs.push_back("As");
    inputs.push_back("Au");
    inputs.push_back("Bs");
    inputs.push_back("Bu");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = (As|Au) <= (Bs|Bu)
class Node_Type_LessEqualThan : public Node_Type {
public:
  Node_Type_LessEqualThan()
      : Node_Type("lessequalthan", LessEqualThan_Op, false) {
    inputs.push_back("As");
    inputs.push_back("Au");
    inputs.push_back("Bs");
    inputs.push_back("Bu");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = (As|Au) == (As|Au) == ...
class Node_Type_Equals : public Node_Type {
public:
  Node_Type_Equals()
      : Node_Type("equals", Equals_Op, false) {
    inputs.push_back("As");
    inputs.push_back("Au");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = ~SA + SB
class Node_Type_Mux : public Node_Type {
public:
  Node_Type_Mux()
      : Node_Type("mux", Mux_Op, false) {
    inputs.push_back("S");
    inputs.push_back("A");
    inputs.push_back("B");
    outputs.push_back("Y");
  };
};

// Y = (A >> B)
// S == 1: sign extension
// S == 2: B is signed
class Node_Type_ShiftRight : public Node_Type {
public:
  Node_Type_ShiftRight()
      : Node_Type("shr", ShiftRight_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    inputs.push_back("S");
    outputs.push_back("Y");
  };
};

// Y = A << B
class Node_Type_ShiftLeft : public Node_Type {
public:
  Node_Type_ShiftLeft()
      : Node_Type("shl", ShiftLeft_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    outputs.push_back("Y");
  };
};

class Node_Type_GraphIO : public Node_Type {
public:
  Node_Type_GraphIO()
      : Node_Type("graphio", GraphIO_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

class Node_Type_Flop : public Node_Type {
public:
  Node_Type_Flop()
      : Node_Type("flop", Flop_Op, true) {
    inputs.push_back("clk"); // clk
    inputs.push_back("D");
    inputs.push_back("E");
    inputs.push_back("R");    // reset signal
    inputs.push_back("Rval"); // reset value
    inputs.push_back("pol");  // clock polarity (positive if not specified)
    outputs.push_back("Q");
  };
};

class Node_Type_AFlop : public Node_Type {
public:
  Node_Type_AFlop()
      : Node_Type("flop", AFlop_Op, true) {
    inputs.push_back("clk"); // clk
    inputs.push_back("D");
    inputs.push_back("E");
    inputs.push_back("R");    // reset signal
    inputs.push_back("Rval"); // reset value
    outputs.push_back("Q");
  };
};

class Node_Type_Latch : public Node_Type {
public:
  Node_Type_Latch()
      : Node_Type("latch", Latch_Op, true) {
    inputs.push_back("D");
    inputs.push_back("EN");
    outputs.push_back("Q");
  };
};

class Node_Type_FFlop : public Node_Type {
public:
  Node_Type_FFlop()
      : Node_Type("fflop", FFlop_Op, true) {
    inputs.push_back("clk"); // clk
    inputs.push_back("D");
    inputs.push_back("Vi");   // valid in
    inputs.push_back("So");   // stop out
    inputs.push_back("R");    // rst
    inputs.push_back("Rval"); // reset value
    outputs.push_back("Q");
    outputs.push_back("Vo"); // valid out
    outputs.push_back("Si"); // stop in
  };
};

// Parameters
#define LGRAPH_MEMOP_SIZE   0
#define LGRAPH_MEMOP_OFFSET 1
#define LGRAPH_MEMOP_ABITS  2
#define LGRAPH_MEMOP_WRPORT 3
#define LGRAPH_MEMOP_RDPORT 4
#define LGRAPH_MEMOP_CLKPOL 5
#define LGRAPH_MEMOP_RDTRAN 6

// Shared signals
#define LGRAPH_MEMOP_CLK 7
#define LGRAPH_MEMOP_CE  8

// Port specific signals
#define LGRAPH_MEMOP_POFFSET (LGRAPH_MEMOP_CE + 1)
#define LGRAPH_MEMOP_PIDS 5

#define LGRAPH_MEMOP_WRADDR(_n) (LGRAPH_MEMOP_POFFSET + 0 + _n * (LGRAPH_MEMOP_PIDS))
#define LGRAPH_MEMOP_WRDATA(_n) (LGRAPH_MEMOP_POFFSET + 1 + _n * (LGRAPH_MEMOP_PIDS))
#define LGRAPH_MEMOP_WREN(_n) (LGRAPH_MEMOP_POFFSET + 2 + _n * (LGRAPH_MEMOP_PIDS))

#define LGRAPH_MEMOP_RDADDR(_n) (LGRAPH_MEMOP_POFFSET + 3 + _n * (LGRAPH_MEMOP_PIDS))
#define LGRAPH_MEMOP_RDEN(_n) (LGRAPH_MEMOP_POFFSET + 4 + _n * (LGRAPH_MEMOP_PIDS))

#define LGRAPH_MEMOP_ISWRADDR(_pid) (((_pid - LGRAPH_MEMOP_POFFSET) % (LGRAPH_MEMOP_PIDS)) == 0)
#define LGRAPH_MEMOP_ISWRDATA(_pid) (((_pid - LGRAPH_MEMOP_POFFSET) % (LGRAPH_MEMOP_PIDS)) == 1)
#define LGRAPH_MEMOP_ISWREN(_pid) (((_pid - LGRAPH_MEMOP_POFFSET) % (LGRAPH_MEMOP_PIDS)) == 2)
#define LGRAPH_MEMOP_ISRDADDR(_pid) (((_pid - LGRAPH_MEMOP_POFFSET) % (LGRAPH_MEMOP_PIDS)) == 3)
#define LGRAPH_MEMOP_ISRDEN(_pid) (((_pid - LGRAPH_MEMOP_POFFSET) % (LGRAPH_MEMOP_PIDS)) == 4)

// generic multiported memory object
class Node_Type_Memory : public Node_Type {
public:
  Node_Type_Memory()
      : Node_Type("memory", Memory_Op, true) {

    // parameters
    inputs.push_back("size");           // number of words (parameter)
    inputs.push_back("abits");          // address bits //FIXME: if input ports have sizes, it is possible to remove this
    inputs.push_back("wr_ports");       // number of wr_ports (parameter)
    inputs.push_back("rd_ports");       // number of rd_ports (parameter)
    inputs.push_back("clk_polarity");   // clock polarity: 0 == posedge, 1 == negedge
    inputs.push_back("rd_transparent"); // fwd writes to reads in the same address

    // shared ports
    inputs.push_back("clk"); // single clock support only
    inputs.push_back("ce");  // shared chip enable (no connection means always enable)

    // wr / rd port 0
    inputs.push_back("wr0_addr");
    inputs.push_back("wr0_data");
    inputs.push_back("wr0_en");

    inputs.push_back("rd0_addr");
    inputs.push_back("rd0_en");

    // wr / rd port 1
    inputs.push_back("wr1_addr");
    inputs.push_back("wr1_data");
    inputs.push_back("wr1_en");

    inputs.push_back("rd1_addr");
    inputs.push_back("rd1_en");

    // wr / rd port 2
    inputs.push_back("wr2_addr");
    inputs.push_back("wr2_data");
    inputs.push_back("wr2_en");

    inputs.push_back("rd2_addr");
    inputs.push_back("rd2_en");

    // wr / rd port 3
    inputs.push_back("wr3_addr");
    inputs.push_back("wr3_data");
    inputs.push_back("wr3_en");

    inputs.push_back("rd3_addr");
    inputs.push_back("rd3_en");
    // continues...

    outputs.push_back("rd_data0");
    outputs.push_back("rd_data1");
    outputs.push_back("rd_data2");
    outputs.push_back("rd_data3");
    // continues...
  };
};

class Node_Type_SubGraph : public Node_Type {
public:
  // TODO: Create 2 subgraphs? one pipelined and another not
  Node_Type_SubGraph()
      : Node_Type("subgraph", SubGraph_Op, true){};
};

class Node_Type_TechMap : public Node_Type {
public:
  // FIXME: Create 2 TechMaps to know if it is pipelined or not for loops
  Node_Type_TechMap()
      : Node_Type("techmap", TechMap_Op, true){};
};

#define LGRAPH_BBOP_TYPE 0
#define LGRAPH_BBOP_NAME 1

#define LGRAPH_BBOP_OFFSET 2
#define LGRAPH_BBOP_PORT_SIZE 4

#define LGRAPH_BBOP_PARAM(_n) (LGRAPH_BBOP_OFFSET + 0 + _n * (LGRAPH_BBOP_PORT_SIZE))
#define LGRAPH_BBOP_PNAME(_n) (LGRAPH_BBOP_OFFSET + 1 + _n * (LGRAPH_BBOP_PORT_SIZE))
#define LGRAPH_BBOP_CONNECT(_n) (LGRAPH_BBOP_OFFSET + 2 + _n * (LGRAPH_BBOP_PORT_SIZE))
#define LGRAPH_BBOP_ONAME(_n) (LGRAPH_BBOP_OFFSET + 3 + _n * (LGRAPH_BBOP_PORT_SIZE))

#define LGRAPH_BBOP_ISPARAM(_pid) (((_pid - LGRAPH_BBOP_OFFSET) % (LGRAPH_BBOP_PORT_SIZE)) == 0)
#define LGRAPH_BBOP_ISPNAME(_pid) (((_pid - LGRAPH_BBOP_OFFSET) % (LGRAPH_BBOP_PORT_SIZE)) == 1)
#define LGRAPH_BBOP_ISCONNECT(_pid) (((_pid - LGRAPH_BBOP_OFFSET) % (LGRAPH_BBOP_PORT_SIZE)) == 2)
#define LGRAPH_BBOP_ISONAME(_pid) (((_pid - LGRAPH_BBOP_OFFSET) % (LGRAPH_BBOP_PORT_SIZE)) == 3)

#define LGRAPH_BBOP_PORT_N(_pid) ((_pid - LGRAPH_BBOP_OFFSET) / (LGRAPH_BBOP_PORT_SIZE))

class Node_Type_BlackBox : public Node_Type {
public:
  Node_Type_BlackBox()
      : Node_Type("blackbox", BlackBox_Op, true) {
    inputs.push_back("type");
    inputs.push_back("instance_name");

    inputs.push_back("port1_isparam"); // 0 = input, 1 = parameter
    inputs.push_back("port1_name");
    inputs.push_back("port1_connection");

    inputs.push_back("port2_isparam"); // 0 = input, 1 = parameter
    inputs.push_back("port2_name");
    inputs.push_back("port2_connection");

    inputs.push_back("port3_isparam"); // 0 = input, 1 = parameter
    inputs.push_back("port3_name");
    inputs.push_back("port3_connection");

    // continues ...
    outputs.push_back("port1_name");
    outputs.push_back("port1_connection");

    outputs.push_back("port2_name");
    outputs.push_back("port2_connection");

    outputs.push_back("port3_name");
    outputs.push_back("port3_connection");
  };
};

class Node_Type_U32Const : public Node_Type {
public:
  Node_Type_U32Const()
      : Node_Type("u32const", U32Const_Op, false) {
    outputs.push_back("Y");
  };
};

class Node_Type_StrConst : public Node_Type {
public:
  Node_Type_StrConst()
      : Node_Type("strconst", StrConst_Op, false) {
    outputs.push_back("Y");
  };
};

// start adding CFG node_types descriptions

// Y1=(A==True), Y2=(A==False)
class Node_Type_CfgIf : public Node_Type {
public:
  Node_Type_CfgIf()
      : Node_Type("cfg_if", CfgIf_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y1");
    outputs.push_back("Y2");
  };
};

// Y = A
class Node_Type_CfgAssign : public Node_Type {
public:
  Node_Type_CfgAssign()
      : Node_Type("cfg_assign", CfgAssign_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

class Node_Type_CfgFunctionCall : public Node_Type {
public:
  Node_Type_CfgFunctionCall()
      : Node_Type("cfg_func", CfgFunctionCall_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

class Node_Type_CfgFor : public Node_Type {
public:
  Node_Type_CfgFor()
      : Node_Type("cfg_for", CfgFor_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

class Node_Type_CfgWhile : public Node_Type {
public:
  Node_Type_CfgWhile()
      : Node_Type("cfg_while", CfgWhile_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

class Node_Type_CfgIfMerge : public Node_Type {
public:
  Node_Type_CfgIfMerge()
      : Node_Type("cfg_if_merge", CfgIfMerge_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

class Node_Type_CfgBeenRead : public Node_Type {
public:
  Node_Type_CfgBeenRead()
      : Node_Type("cfg_been_read", CfgBeenRead_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

class Node_Type_DfgRef : public Node_Type {
public:
  Node_Type_DfgRef()
    : Node_Type("dfg_ref", DfgRef_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

class Node_Type_DfgPendingGraph : public Node_Type {
public:
  Node_Type_DfgPendingGraph()
    : Node_Type("dfg_pending_graph", DfgPendingGraph_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

class Node_Type_DontCare : public Node_Type {
public:
  Node_Type_DontCare()
      : Node_Type("don't_care", DontCare_Op, false) {
    outputs.push_back("Y");
  };
};

typedef Char_Array_ID Const_ID;

class LGraph_Node_Type : virtual public Lgraph_base_core {
private:
  Char_Array<Const_ID> consts;
  Dense<Node_Type_Op>  node_type_table;
  bm::bvector<>        const_nodes;

public:
  LGraph_Node_Type() = delete;
  explicit LGraph_Node_Type(const std::string &path, const std::string &name) noexcept;
  virtual ~LGraph_Node_Type(){};

  Const_ID    get_constant_id(const char *constant);
  const char *get_constant(Const_ID const_id) const;

  void clear();
  void reload(uint64_t sz);
  void sync();
  void emplace_back();

  void node_type_set(Index_ID nid, Node_Type_Op op);

  void     node_u32type_set(Index_ID nid, uint32_t value);
  uint32_t node_value_get(Index_ID nid) const;

  void     node_subgraph_set(Index_ID nid, uint32_t subgraphid);
  uint32_t subgraph_id_get(Index_ID nid) const;

  void node_const_type_set(Index_ID nid, const std::string &value
#ifndef NDEBUG
                     , bool enforce_bits = true
#endif
  );
  const char *node_const_value_get(Index_ID nid) const;

  void     node_tmap_set(Index_ID nid, uint32_t tmapid);
  uint32_t tmap_id_get(Index_ID nid) const;

  const Node_Type &node_type_get(Index_ID nid) const;

  const bm::bvector<> &get_const_node_ids() const {
    return const_nodes;
  };
};

#endif
