//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

#include "absl/container/flat_hash_map.h"

enum class Ntype_op : uint8_t {
  Invalid,  // Detect bugs/unset (not used anywhere)
  Sum,
  Mult,
  Div,

  And,
  Or,
  Xor,
  Ror,  // Reduce OR

  Not,       // bitwise not
  Get_mask,  // To positive signed
  Set_mask,  // To positive signed
  Sext,      // Sign extend from a given bit (b) position

  LT,  // Less Than   , also GE = !LT
  GT,  // Greater Than, also LE = !GT
  EQ,  // Equal       , also NE = !EQ

  SHL,  // Shift Left Logical
  SRA,  // Shift Right Arithmetic

  Mux,  // Multiplexor with many options
  LUT,  // LUT

  IO,  // Graph Input or Output

  //------------------BEGIN PIPELINED (break LOOPS)
  Memory,

  Flop,   // Asynchronous & sync reset flop
  Latch,  // Latch
  Fflop,  // Fluid flop

  Sub,    // Sub module instance
  Const,  // Constant
  //------------------END PIPELINED (break LOOPS)

  // High Level Lgraph constructs

  TupAdd,
  TupGet,
  TupRef,

  AttrSet,
  AttrGet,

  CompileErr,  // Indicate a compile error during a pass

  Last_invalid
};

class Ntype {
protected:
  inline static constexpr std::string_view cell_name[]
      = {"Invalid",  "Sum",      "Mult",   "Div",     "And",     "Or",         "Xor",         "Ror",   "Not",
         "Get_mask", "Set_mask", "Sext",   "LT",      "GT",      "EQ",         "SHL",         "SRA",   "Mux",
         "LUT",      "IO",       "Memory", "Flop",    "Latch",   "Fflop",      "Sub",         "Const", "TupAdd",
         "TupGet",   "TupRef",   "AttrSet", "AttrGet", "CompileErr", "Last_invalid"};

  inline static absl::flat_hash_map<std::string, Ntype_op> cell_name_map;

  class _init {
  public:
    _init();
  };
  static _init _static_initializer;

  // NOTE: order of operands to maximize code gen when "name" is known (typical case)
  inline static std::array<std::array<char, static_cast<std::size_t>(Ntype_op::Last_invalid)>, 256>            sink_name2pid;
  inline static std::array<std::array<std::string_view, static_cast<std::size_t>(Ntype_op::Last_invalid)>, 11> sink_pid2name;

  static constexpr std::string_view get_sink_name_slow(Ntype_op op, int pid);

public:
  static inline constexpr bool is_loop_first(Ntype_op op) {
    return op == Ntype_op::Const;
  }
  static inline constexpr bool is_loop_last(Ntype_op op) {
    return static_cast<int>(op) >= static_cast<int>(Ntype_op::Memory) && static_cast<int>(op) <= static_cast<int>(Ntype_op::Sub);
  }

  static inline constexpr bool is_multi_sink(Ntype_op op) {
    return op != Ntype_op::Mult && op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor && op != Ntype_op::Ror
           && op != Ntype_op::Not;
  }

  static inline constexpr bool is_synthesizable(Ntype_op op) {
    return op != Ntype_op::Sub && op != Ntype_op::TupAdd && op != Ntype_op::TupGet && op != Ntype_op::TupRef
           && op != Ntype_op::AttrSet && op != Ntype_op::AttrGet && op != Ntype_op::CompileErr
           && op != Ntype_op::Invalid && op != Ntype_op::Last_invalid;
  }

  static inline constexpr bool is_unlimited_sink(Ntype_op op) {
    return op == Ntype_op::IO || op == Ntype_op::LUT || op == Ntype_op::Sub || op == Ntype_op::Mux || op == Ntype_op::CompileErr;
  }
  static inline constexpr bool is_unlimited_driver(Ntype_op op) { return op == Ntype_op::Sub || op == Ntype_op::IO; }
  static inline constexpr bool is_multi_driver(Ntype_op op) { return op == Ntype_op::AttrSet || is_unlimited_driver(op); }
  static inline constexpr bool is_single_driver_per_pin(Ntype_op op) {
    if (is_unlimited_sink(op))
      return true;
    auto c = sink_pid2name[0][static_cast<std::size_t>(op)];  // Is first port Upper or lower case
    return c[0] >= 'a' && c[0] <= 'z';
  }

  // Carefully crafted call so that it is solved at compile time most of the time
  static inline constexpr int get_sink_pid(Ntype_op op, std::string_view str) {
    auto c = str[0];
    // Common case speedup
    if (c >= 'a' && c <= 'f') {
      int pid = c - 'a';
      assert(sink_name2pid[str[0]][static_cast<std::size_t>(op)] == pid);
      assert(get_sink_name(op, pid) == str);
      return pid;
    }
    if (c == 'A') {
      assert(sink_name2pid[str[0]][static_cast<std::size_t>(op)] == 0);
      assert(get_sink_name(op, 0) == str);
      return 0;
    }
    if (c == 'B') {
      assert(sink_name2pid[str[0]][static_cast<std::size_t>(op)] == 1);
      assert(get_sink_name(op, 1) == str);
      return 1;
    }

    if (__builtin_expect(is_unlimited_sink(op) && str.size() > 1, 0)) {  // unlikely case
      int pid = 0;
      for (auto ch : str) {
        assert(ch >= '0' && ch <= '9');
        auto val = ch - '0';
        pid      = pid * 10 + val;
      }

      return pid;
    }

    auto pid = sink_name2pid[str[0]][static_cast<std::size_t>(op)];
    assert(pid != -1);
    assert(get_sink_name(op, pid) == str);
    return pid;
  }

  static inline constexpr std::string_view get_sink_name(Ntype_op op, int pid) {
    if (pid > 10)
      return "x";

    auto name = sink_pid2name[pid][static_cast<std::size_t>(op)];
    assert(name != "invalid");
    return name;
  }
  static inline constexpr bool has_sink(Ntype_op op, int pid) {
    if (pid > 10)
      return is_unlimited_sink(op);
    return sink_pid2name[pid][static_cast<std::size_t>(op)] != "invalid";
  }
  static bool is_valid_sink(Ntype_op op, std::string_view name);

  static inline constexpr int get_driver_pid(Ntype_op op, std::string_view str) {
    if (__builtin_expect(str == "Y", 1)) {  // likely case
      return 0;
    }
    if (op == Ntype_op::AttrSet && str == "chain") {
      return 1;
    }
    return -1;
  }

  static inline constexpr std::string_view get_driver_name(Ntype_op op, int pid) {
    if (pid == 0)
      return "Y";
    if (pid == 1 && op == Ntype_op::AttrSet) {
      return "chain";
    }
    return "invalid";
  }
  static inline constexpr bool has_driver(Ntype_op op, int pid) {
    if (pid == 0)
      return true;
    if (pid == 1 && op == Ntype_op::AttrSet) {
      return true;
    }
    return is_unlimited_driver(op);
  }

  static std::string_view get_name(Ntype_op op) { return cell_name[static_cast<size_t>(op)]; }

  static Ntype_op get_op(std::string_view name) {
    const auto it = cell_name_map.find(name);
    if (it == cell_name_map.end())
      return Ntype_op::Invalid;
    return it->second;
  }
};
