//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <cstdint>
#include <array>
#include <string_view>

enum class Cell_op : uint8_t {
  Invalid, // Detect bugs/unset (not used anywhere)
  Sum,
  Mult,
  Div,

  And,
  Or,
  Xor,
  Rand,   // Reduce AND
  Ror,    // Reduce OR

  Not,    // bitwise not
  Tposs,  // To positive signed

  LT,     // Less Than   , also GE = !LT
  GT,     // Greater Than, also LE = !GT
  EQ,     // Equal       , also NE = !EQ

  SHL,    // Shift Left Logical
  SRA,    // Shift Right Arithmetic

  Mux,    // Multiplexor with many options
  LUT,    // LUT

  IO,  // Graph Input or Output

  //------------------BEGIN PIPELINED (break LOOPS)
  Memory,

  Sflop,  // Synchronous reset flop
  Aflop,  // Asynchronous reset flop
  Latch,  // Latch
  Fflop,  // Fluid flop

  Sub,    // Sub module instance
  Const,  // Constant
  //------------------END PIPELINED (break LOOPS)

  // High Level LGraph constructs

  TupAdd,
  TupGet,

  AttrSet,
  AttrGet,

  CompileErr,  // Indicate a compile error during a pass

  Last_invalid
};

class Cell {
protected:
  inline static constexpr std::string_view cell_name[] = {
    "Invalid",
    "Sum",
    "Mult",
    "Div",

    "And",
    "Or",
    "Xor",
    "Rand",
    "Ror",

    "Not",
    "Tposs",

    "LT",
    "GT",
    "EQ",

    "SHL",
    "SRA",

    "Mux",
    "LUT",

    "IO",

    "Memory",

    "Sflop",
    "Aflop",
    "Latch",
    "Fflop",

    "Sub",
    "Const",

    "TupAdd",
    "TupGet",

    "AttrSet",
    "AttrGet",

    "CompileErr",

    "Last_invalid"
  };
  class _init {
  public:
    _init();
  };
  static _init _static_initializer;

  // NOTE: order of operands to maximize code gen when "name" is known (typical case)
  inline static std::array<std::array<char            ,static_cast<std::size_t>(Cell_op::Last_invalid)>,256> sink_name2pid;
  inline static std::array<std::array<std::string_view,static_cast<std::size_t>(Cell_op::Last_invalid)>,11>  sink_pid2name;

  static constexpr std::string_view get_sink_name_slow(Cell_op op, int pid);
public:

  static inline constexpr bool is_loop_breaker(Cell_op op) {
    return static_cast<int>(op)>=static_cast<int>(Cell_op::Memory)
      && static_cast<int>(op)<=static_cast<int>(Cell_op::Const);
  }

  static inline constexpr bool is_multi_driver(Cell_op op) {
    return op==Cell_op::AttrSet || op==Cell_op::Sub || op==Cell_op::CompileErr;
  }

  static inline constexpr bool is_multi_sink(Cell_op op) {
    return op != Cell_op::Mult
        && op != Cell_op::And
        && op != Cell_op::Or
        && op != Cell_op::Xor
        && op != Cell_op::Rand
        && op != Cell_op::Ror
        && op != Cell_op::Not
        && op != Cell_op::Tposs;
  }

  static inline constexpr bool is_unlimited_sink(Cell_op op) {
    return op==Cell_op::IO || op==Cell_op::LUT || op==Cell_op::Sub || op==Cell_op::Mux || op==Cell_op::CompileErr;
  }
  static inline constexpr bool is_unlimited_driver(Cell_op op) {
    return op==Cell_op::Sub || op==Cell_op::IO || op==Cell_op::CompileErr;
  }

  // Carefully crafted call so that it is solved at compile time most of the time
  static inline constexpr int get_sink_pid(Cell_op op, std::string_view str) {
    auto c = str[0];
    // Common case speedup
    if (c>='a' && c<='f') {
      int pid = c-'a';
      assert(sink_name2pid[str[0]][static_cast<std::size_t>(op)]==pid);
      return pid;
    }
    if (c=='A') {
      assert(sink_name2pid[str[0]][static_cast<std::size_t>(op)]==0);
      return 0;
    }
    if (c=='B') {
      assert(sink_name2pid[str[0]][static_cast<std::size_t>(op)]==1);
      return 1;
    }

    if (__builtin_expect(is_unlimited_sink(op) && str.size()>1,0)) { // unlikely case
      int pid = 0;
      for(auto ch:str) {
        assert(ch>='0' && ch<='9');
        auto val = ch - '0';
        pid = pid * 10 + val;
      }

      return pid;
    }

    auto pid = sink_name2pid[str[0]][static_cast<std::size_t>(op)];
    assert(pid!=-1);
    return pid;
  }

  static inline constexpr std::string_view get_sink_name(Cell_op op, int pid) {
    auto name = sink_pid2name[pid][static_cast<std::size_t>(op)];
    assert(name!="invalid");
    return name;
  }
  static inline constexpr bool has_sink(Cell_op op, int pid) {
    if (pid>10)
      return is_unlimited_sink(op);
    return sink_pid2name[pid][static_cast<std::size_t>(op)] != "invalid";
  }

  static inline constexpr int get_driver_pid(Cell_op op, std::string_view str) {
    if (__builtin_expect(str=="Y",1)) { // likely case
      return 0;
    }
    if (op==Cell_op::AttrSet && str=="chain") {
      return 1;
    }
    return -1;
  }

  static inline constexpr std::string_view get_driver_name(Cell_op op, int pid) {
    if (pid==0)
      return "Y";
    if (pid==1 && op==Cell_op::AttrSet) {
      return "chain";
    }
    return "invalid";
  }
  static inline constexpr bool has_driver(Cell_op op, int pid) {
    if (pid==0)
      return true;
    if (pid==1 && op==Cell_op::AttrSet) {
      return true;
    }
    return is_unlimited_driver(op);
  }

  static std::string_view get_name(Cell_op op) {
    return cell_name[static_cast<int>(op)];
  }

};


