//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "lgraph_base_core.hpp"

// nodetype should be at meta directory but the node type is needed all over in the base class. It may be good to integrate nodetype
// as part of the lgnode itself to avoid extra cache misses

enum Node_Type_Op : uint64_t {
  Invalid_Op,
  // op_class: arith
  Sum_Op,
  Mult_Op,
  Div_Op,
  Mod_Op,
  // op_class: logic
  Not_Op,
  // op_class: wire
  Join_Op,
  Pick_Op,
#if 1
  // WARNING: deprecated once we have LUTs working (mockturtle)
  // op_class: cmp
  LessThan_Op,
  GreaterThan_Op,
  LessEqualThan_Op,
  GreaterEqualThan_Op,
  Equals_Op,
#endif
  // op_class: mux
  Mux_Op,  // WARNING: Trivial MUX (not bus muxes) converted to LUT
#if 1
  // WARNING: deprecated once we have LUTs working (mockturtle)
  // op_class: shift
  LogicShiftRight_Op,
  ArithShiftRight_Op,
  DynamicShiftRight_Op,
  DynamicShiftLeft_Op,
  ShiftRight_Op,  // FIX ME: should be superseded by above Ops
  ShiftLeft_Op,
#endif
  // op_class: LUT
  LUT_Op,
  // op_class: wire
  GraphIO_Op,
  DontCare_Op,
  // op_class: Pyrope Compiler
  TupAdd_Op,
  TupGet_Op,
  TupRef_Op,
  TupKey_Op,
  CompileErr_Op,
// Add here, operators needed
#if 1
  // WARNING: deprecated once we have LUTs working (mockturtle)
  // op_class: logic
  And_Op,
  Or_Op,
  Xor_Op,
#endif
  //------------------BEGIN PIPELINED (break LOOPS)
  Loop_breaker_begin,
  // op_class: register
  SFlop_Op,  // sync reset flop
  AFlop_Op,  // async reset flop
  Latch_Op,
  FFlop_Op,
  // op_class: memory
  Memory_Op,
  SubGraph_Op,
  U32Const_Op,
  StrConst_Op,
  // op_class: sub
  SubGraphMin_Op,  // Each subgraph cell starts here
  SubGraphMax_Op = SubGraphMin_Op + ((1ULL << 32) - 1),
  // op_class: value
  U32ConstMin_Op,
  U32ConstMax_Op = U32ConstMin_Op + ((1ULL << 32) - 1),
  // op_class: str
  StrConstMin_Op,
  StrConstMax_Op = StrConstMin_Op + ((1ULL << 32) - 1),
  Loop_breaker_end,
  //------------------END PIPELINED (break LOOPS)
  // op_class: lut
  LUTMin_Op,
  LUTMax_Op = LUTMin_Op + ((1ULL << (1ULL << LUT_input_bits)) - 1)
};

class Node_Type {
private:
  static Node_Type *                                   table[StrConst_Op + 1];
  static absl::flat_hash_map<std::string, Node_Type *> name2node;

protected:
  const std::string name;
  const bool        pipelined;

  // Both inputs/outputs sorted in alphabetical order
  std::vector<std::string_view> inputs;
  std::vector<std::string_view> outputs;
  std::vector<bool>             inputs_sign;

  bool may_gen_sign;

  Node_Type(std::string_view _name, Node_Type_Op _op, bool _pipelined) : name(_name), pipelined(_pipelined), op(_op) {
    may_gen_sign = false;
  };

  void setup_signs(bool can_sign) {
    may_gen_sign = can_sign;

    inputs_sign.resize(inputs.size());

    for (size_t i = 0; i < inputs.size(); i++) {
      auto len = inputs[i].size();
      I(len > 0);
      inputs_sign[i] = (inputs[i][len - 1] == 'S');
    }
  }

public:
  const Node_Type_Op op;

  bool has_may_gen_sign() const { return may_gen_sign; }

  static Node_Type &  get(Node_Type_Op op);
  static Node_Type_Op get(std::string_view opname);
  static bool         is_type(std::string_view opname);

  std::string_view get_name() const { return name; }

  Port_ID get_input_match(std::string_view str) const {
    if (inputs.empty())  // blackbox, subgraph...
      return Port_invalid;

    std::string data(str);
    std::transform(data.begin(), data.end(), data.begin(), ::toupper);

    for (size_t i = 0; i < inputs.size(); i++) {
      if (inputs[i] == data) {
        return static_cast<Port_ID>(i);
      }
    }
#ifndef NDEBUG
    for (size_t i = 0; i < inputs.size(); i++) {
      std::string d_data(inputs[i]);
      std::transform(d_data.begin(), d_data.end(), d_data.begin(), ::toupper);
      I(inputs[i] == d_data);  // must be set uppercase
    }
#endif

    return Port_invalid;
  }

  std::string_view get_input_match(Port_ID pid) const {
    size_t idx = static_cast<size_t>(pid);
    I(idx < inputs.size());

    return inputs[idx];
  }

  Port_ID get_output_match(std::string_view str) const {
    if (outputs.empty())  // blackbox, subgraph...
      return Port_invalid;

    if (str == "Y" && outputs[0] == "Y") return 0;
    if (str == "Q" && outputs[0] == "Q") return 0;
    if (str == "YREDUCE" && outputs[1] == "YREDUCE") return 1;

    std::string data(str);
    std::transform(data.begin(), data.end(), data.begin(), ::toupper);

    for (size_t i = 0; i < outputs.size(); i++) {
      if (outputs[i] == data) {
        return static_cast<Port_ID>(i);
      }
    }
#ifndef NDEBUG
    for (size_t i = 0; i < outputs.size(); i++) {
      std::string d_data(outputs[i]);
      std::transform(d_data.begin(), d_data.end(), d_data.begin(), ::toupper);
      I(outputs[i] == d_data);  // must be set uppercase
    }
#endif

    return Port_invalid;
  }

  std::string_view get_output_match(Port_ID pid) const {
    size_t idx = static_cast<size_t>(pid);
    I(idx < outputs.size());

    return outputs[idx];
  }

  bool is_input_signed(Port_ID pid) const {
    if (inputs_sign.size() < pid) return inputs_sign[pid];
    return false;
  }

  size_t get_num_inputs() const {
    if (inputs.size()) return inputs.size();
    return (1 << Port_bits) - 1;
  }
  size_t get_num_outputs() const {
    if (outputs.size()) return outputs.size();
    return (1 << Port_bits) - 1;
  }

  bool has_output(Port_ID pid) const {
    return outputs.size() > pid || outputs.empty();  // no default outputs subgraph
  }
  bool has_input(Port_ID pid) const {
    return inputs.size() > pid || inputs.empty();  // no default inputs for subgraph
  }
  bool has_single_output() const { return outputs.size() == 1; }
  bool has_single_input() const { return inputs.size() == 1; }

  bool is_pipelined() const { return pipelined; }  // Can create loops

  class _init {
  public:
    _init();
  };
  static _init _static_initializer;
};

class Node_Type_Invalid : public Node_Type {
public:
  Node_Type_Invalid() : Node_Type("invalid", Invalid_Op, false){};
};

// Y = (As+...+As+Au+...+Au) - (Bs+...+Bs+Bu+...+Bu)
// add is a subset of Sum
// Y = As + Au (is an unsigned add)
class Node_Type_Sum : public Node_Type {
public:
  Node_Type_Sum() : Node_Type("sum", Sum_Op, false) {
    inputs.push_back("AS");  // signed
    inputs.push_back("AU");  // unsigned
    inputs.push_back("BS");
    inputs.push_back("BU");
    outputs.push_back("Y");

    setup_signs(true);
  };
};

// Y = As*...*As * Au*...*Au
class Node_Type_Mult : public Node_Type {
public:
  Node_Type_Mult() : Node_Type("mult", Mult_Op, false) {
    inputs.push_back("AS");
    inputs.push_back("AU");
    outputs.push_back("Y");

    setup_signs(true);
  };
};

// Y = (As|Au)/(Bs/Bu)
class Node_Type_Div : public Node_Type {
public:
  Node_Type_Div() : Node_Type("div", Div_Op, false) {
    inputs.push_back("AS");
    inputs.push_back("AU");
    inputs.push_back("BS");
    inputs.push_back("BU");
    outputs.push_back("Y");

    setup_signs(true);
  };
};

// Y = (As|Au)%(Bs/Bu)
class Node_Type_Mod : public Node_Type {
public:
  Node_Type_Mod() : Node_Type("mod", Mod_Op, false) {
    inputs.push_back("AS");
    inputs.push_back("AU");
    inputs.push_back("BS");
    inputs.push_back("BU");
    outputs.push_back("Y");

    setup_signs(true);
  };
};

// Y = bitwise invert of A
class Node_Type_Not : public Node_Type {
public:
  Node_Type_Not() : Node_Type("not", Not_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
  };
};

// Y = {A,B,C,D,E....} => previous expression might be wrong!!
// Y = {...,E,D,C,B,A} => modified by SH
class Node_Type_Join : public Node_Type {
public:
  Node_Type_Join() : Node_Type("join", Join_Op, false) { outputs.push_back("Y"); };
};

// Y = A[i,j]
// pid 0 : A
// pid 1 : offset
// j = offset
// i = offset + Y-bitwidth
class Node_Type_Pick : public Node_Type {
public:
  Node_Type_Pick() : Node_Type("pick", Pick_Op, false) {
    inputs.push_back("A");
    inputs.push_back("OFFSET");
    outputs.push_back("Y");
  };
};

// Y0 = A0 & A1 & A2...   // multi-inputs on same pin
// Y1 = & {A0, A1, A2...} // bitwise reduction for all inputs bits
class Node_Type_And : public Node_Type {
public:
  Node_Type_And() : Node_Type("and", And_Op, false) {
    inputs.push_back("A");         // pid: 0
    outputs.push_back("Y");        // pid: 0
    outputs.push_back("YREDUCE");  // pid: 1
  };
};

// Y0 = A0 | A1 | A2...   // multi-inputs on same pin
// Y1 = | {A0, A1, A2...} // bitwise reduction for all inputs bits
class Node_Type_Or : public Node_Type {
public:
  Node_Type_Or() : Node_Type("or", Or_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
    outputs.push_back("YREDUCE");
  };
};

// Y0 = A0 ^ A1 ^ A2...   // multi-inputs on same pin
// Y1 = ^ {A0, A1, A2...} // bitwise reduction for all inputs bits
class Node_Type_Xor : public Node_Type {
public:
  Node_Type_Xor() : Node_Type("xor", Xor_Op, false) {
    inputs.push_back("A");
    outputs.push_back("Y");
    outputs.push_back("YREDUCE");
  };
};

// Y = (As|Au) < (Bs|Bu)
// it can not have both As and Au at the same time
// it can not have both Bs and Bu at the same time
// If many edges are connected, it is and AND.
// E.g:
// LessThan A , (B C) means (LessThan A , B) and (LessThan A, C)
class Node_Type_LessThan : public Node_Type {
public:
  Node_Type_LessThan() : Node_Type("lessthan", LessThan_Op, false) {
    inputs.push_back("AS");
    inputs.push_back("AU");
    inputs.push_back("BS");
    inputs.push_back("BU");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = (As|Au) > (Bs|Bu)
class Node_Type_GreaterThan : public Node_Type {
public:
  Node_Type_GreaterThan() : Node_Type("greaterthan", GreaterThan_Op, false) {
    inputs.push_back("AS");
    inputs.push_back("AU");
    inputs.push_back("BS");
    inputs.push_back("BU");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = (As|Au) >= (Bs|Bu)
class Node_Type_GreaterEqualThan : public Node_Type {
public:
  Node_Type_GreaterEqualThan() : Node_Type("greaterequalthan", GreaterEqualThan_Op, false) {
    inputs.push_back("AS");
    inputs.push_back("AU");
    inputs.push_back("BS");
    inputs.push_back("BU");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = (As|Au) <= (Bs|Bu)
class Node_Type_LessEqualThan : public Node_Type {
public:
  Node_Type_LessEqualThan() : Node_Type("lessequalthan", LessEqualThan_Op, false) {
    inputs.push_back("AS");
    inputs.push_back("AU");
    inputs.push_back("BS");
    inputs.push_back("BU");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = (As|Au) == (As|Au) == ...
class Node_Type_Equals : public Node_Type {
public:
  Node_Type_Equals() : Node_Type("equals", Equals_Op, false) {
    inputs.push_back("AS");
    inputs.push_back("AU");
    outputs.push_back("Y");

    setup_signs(false);
  };
};

// Y = ~SA + SB
class Node_Type_Mux : public Node_Type {
public:
  Node_Type_Mux() : Node_Type("mux", Mux_Op, false) {
    inputs.push_back("S");
    inputs.push_back("A");
    inputs.push_back("B");
    inputs.push_back("C");
    inputs.push_back("D");
    inputs.push_back("E");  // Keeps going
    outputs.push_back("Y");
  };
};

// Y = A >> B
class Node_Type_LogicShiftRight : public Node_Type {
public:
  Node_Type_LogicShiftRight() : Node_Type("lshr", LogicShiftRight_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    outputs.push_back("Y");
  };
};

// Y = $signed(A) >>> B
class Node_Type_ArithShiftRight : public Node_Type {
public:
  Node_Type_ArithShiftRight() : Node_Type("ashr", ArithShiftRight_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    outputs.push_back("Y");
  };
};

// Y = A[$signed(B) +: bit_width(A)]
class Node_Type_DynamicShiftRight : public Node_Type {
public:
  Node_Type_DynamicShiftRight() : Node_Type("dshr", DynamicShiftRight_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    outputs.push_back("Y");
  };
};

// Y = A[$signed(B) -: bit_width(A)]
class Node_Type_DynamicShiftLeft : public Node_Type {
public:
  Node_Type_DynamicShiftLeft() : Node_Type("dshl", DynamicShiftLeft_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    outputs.push_back("Y");
  };
};

// Y = A >> B
// S == 1: sign extension
// S == 2: B is signed
// FIX ME: should be superseded
class Node_Type_ShiftRight : public Node_Type {
public:
  Node_Type_ShiftRight() : Node_Type("shr", ShiftRight_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    inputs.push_back("S");
    outputs.push_back("Y");
  };
};

// Y = A << B
class Node_Type_ShiftLeft : public Node_Type {
public:
  Node_Type_ShiftLeft() : Node_Type("shl", ShiftLeft_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    outputs.push_back("Y");
  };
};

class Node_Type_GraphIO : public Node_Type {
public:
  Node_Type_GraphIO()
      : Node_Type("graphio", GraphIO_Op, false){
            // No pins because there are many pids (one per IO)
        };
};

class Node_Type_Flop : public Node_Type {
public:
  Node_Type_Flop() : Node_Type("sflop", SFlop_Op, true) {
    inputs.push_back("C");
    inputs.push_back("D");
    inputs.push_back("EN");
    inputs.push_back("CLR");  // reset signal
    inputs.push_back("SET");  // set value
    inputs.push_back("POL");  // clock polarity (positive if not specified)
    outputs.push_back("Q");
  };
};

class Node_Type_AFlop : public Node_Type {
public:
  Node_Type_AFlop() : Node_Type("flop", AFlop_Op, true) {
    inputs.push_back("CLK");
    inputs.push_back("D");
    inputs.push_back("EN");
    inputs.push_back("CLR");  // reset signal
    inputs.push_back("SET");  // set value
    outputs.push_back("Q");
  };
};

class Node_Type_Latch : public Node_Type {
public:
  Node_Type_Latch() : Node_Type("latch", Latch_Op, true) {
    inputs.push_back("D");
    inputs.push_back("EN");
    inputs.push_back("POL");  // reset value
    outputs.push_back("Q");
  };
};

class Node_Type_FFlop : public Node_Type {
public:
  Node_Type_FFlop() : Node_Type("fflop", FFlop_Op, true) {
    inputs.push_back("CLK");  // clk
    inputs.push_back("D");
    inputs.push_back("VI");    // valid in
    inputs.push_back("SO");    // stop out
    inputs.push_back("R");     // rst
    inputs.push_back("RVAL");  // reset value
    outputs.push_back("Q");
    outputs.push_back("VO");  // valid out
    outputs.push_back("SI");  // stop in
  };
};

// Parameters
#define LGRAPH_MEMOP_SIZE 0
#define LGRAPH_MEMOP_OFFSET 1
#define LGRAPH_MEMOP_ABITS 2
#define LGRAPH_MEMOP_WRPORT 3
#define LGRAPH_MEMOP_RDPORT 4
#define LGRAPH_MEMOP_RDCLKPOL 5
#define LGRAPH_MEMOP_WRCLKPOL 6
#define LGRAPH_MEMOP_RDTRAN 7

// Shared signals
#define LGRAPH_MEMOP_CLK 8
#define LGRAPH_MEMOP_CE 9

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
  Node_Type_Memory() : Node_Type("memory", Memory_Op, true) {
    // parameters
    inputs.push_back("SIZE");            // number of words (parameter)
    inputs.push_back("ABITS");           // address bits //FIXME: if input ports have sizes, it is possible to remove this
    inputs.push_back("WR_PORTS");        // number of wr_ports (parameter)
    inputs.push_back("RD_PORTS");        // number of rd_ports (parameter)
    inputs.push_back("RDCLK_POLARITY");  // clock polarity: 0 == posedge, 1 == negedge
    inputs.push_back("WRCLK_POLARITY");  // clock polarity: 0 == posedge, 1 == negedge
    inputs.push_back("RD_TRANSPARENT");  // fwd writes to reads in the same address

    // shared ports
    inputs.push_back("CLK");  // single clock support only
    inputs.push_back("CE");   // shared chip enable (no connection means always enable)

    for (int i = 0; i < 1024; i++) {  // At most 1K ports?? Increase if needed
      std::string wr = "WR" + std::to_string(i);
      // wr / rd port 0
      inputs.push_back(wr + "_ADDR");
      inputs.push_back(wr + "_DATA");
      inputs.push_back(wr + "_EN");
      //inputs.push_back(wr + "_SEQID");

      std::string rd = "RD" + std::to_string(i);
      inputs.push_back(rd + "_ADDR");
      inputs.push_back(rd + "_EN");
      //inputs.push_back(wr + "_SEQID");

      outputs.push_back(rd + "_DATA");
    }

    /*
     a.rd2_seqid = 0
     a.rd2_addr = 3
     I(a.rd2_data == 0)

     a.wr0_seqid =1
     a.wr0_addr = 3
     a.wr0_data = 3

     a.rd0_seqid = 4
     a.rd0_addr = 3
     I(a.rd0_data == 3)

     a.wr1_seqid =7
     a.wr1_addr = 3
     a.wr1_data = 4

     a.rd1_seqid = 8
     a.rd1_addr = 3
     I(a.rd1_data == 4)
    */
  };
};

class Node_Type_SubGraph : public Node_Type {
public:
  Node_Type_SubGraph() : Node_Type("subgraph", SubGraph_Op, true){};
};

class Node_Type_U32Const : public Node_Type {
public:
  Node_Type_U32Const() : Node_Type("u32const", U32Const_Op, true) { outputs.push_back("Y"); };
};

class Node_Type_StrConst : public Node_Type {
public:
  Node_Type_StrConst() : Node_Type("strconst", StrConst_Op, true) { outputs.push_back("Y"); };
};

class Node_Type_LUT : public Node_Type {
public:
  Node_Type_LUT() : Node_Type("lut", LUT_Op, false) {
    inputs.push_back("A");
    inputs.push_back("B");
    inputs.push_back("C");
    inputs.push_back("D");
    inputs.push_back("E");
    inputs.push_back("F");
    outputs.push_back("Y");
  };
};

// TN = tuple name, KP = key position , KN = key name, V = value
class Node_Type_TupAdd : public Node_Type {
public:
  Node_Type_TupAdd() : Node_Type("tup_add", TupAdd_Op, false) {
    inputs.push_back("TN");
    inputs.push_back("KN");
    inputs.push_back("KP");
    inputs.push_back("KV");
    outputs.push_back("Y");
  };
};


// TN = tuple precedence name, K = position or name, Y = value
class Node_Type_TupGet : public Node_Type {
public:
  Node_Type_TupGet() : Node_Type("tup_get", TupGet_Op, false) {
    inputs.push_back("TN");
    inputs.push_back("KN");
    inputs.push_back("KP");
    outputs.push_back("Y");
  };
};


// Y = tuple root name
class Node_Type_TupRef : public Node_Type {
public:
  Node_Type_TupRef() : Node_Type("tup_ref", TupRef_Op, true) {
    outputs.push_back("Y");
  };
};

// Y = tuple key name
class Node_Type_TupKey : public Node_Type {
public:
  Node_Type_TupKey() : Node_Type("tup_key", TupKey_Op, true) {
    outputs.push_back("Y");
  };
};


// Y = tuple root name
class Node_Type_CompileErr : public Node_Type {
public:
  Node_Type_CompileErr() : Node_Type("compile_err", CompileErr_Op, true) {
    outputs.push_back("Y");
  };
};


class Node_Type_DontCare : public Node_Type {
public:
  Node_Type_DontCare() : Node_Type("don't_care", DontCare_Op, false) { outputs.push_back("Y"); };
};
