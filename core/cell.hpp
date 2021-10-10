//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "lbench.hpp"
#include "lgraph_base_core.hpp"
#include "mmap_str.hpp"

enum class Ntype_op : uint8_t {
  Invalid,  // Detect bugs/unset (not used anywhere)
  Sum,
  Mult,
  Div,

  And,
  Or,
  Xor,
  Ror,  // Reduce OR (This is a bit different from the LNAST reduce_or (lnast uses mask)

  Not,       // bitwise not
  Get_mask,  // To positive signed
  Set_mask,  // To positive signed
  Sext,      // Sign extend from a given bit (b) position

  LT,  // Less Than   , also GE = !LT
  GT,  // Greater Than, also LE = !GT
  EQ,  // Equal       , also NE = !EQ

  SHL,  // Shift Left Logical
  SRA,  // Shift Right Arithmetic

  LUT,  // LUT
  Mux,  // Multiplexor with many options

  IO,  // Graph Input or Output

  //------------------BEGIN PIPELINED (break LOOPS)
  Memory,

  Flop,   // Asynchronous & sync reset flop
  Latch,  // Latch
  Fflop,  // Fluid flop

  Sub,  // Sub module instance
  //------------------END PIPELINED (break LOOPS)
  Const,  // Constant

  // High Level Lgraph constructs

  TupAdd,
  TupGet,

  AttrSet,
  AttrGet,

  CompileErr,  // Indicate a compile error during a pass

  Last_invalid
};

class Ntype {
protected:
  inline static constexpr std::string_view cell_name_sv[] = {
      "invalid", "sum",   "mult", "div",   "and",     "or",      "xor",      "ror",      "not",         "get_mask",    "set_mask",
      "sext",    "lt",    "gt",   "eq",    "shl",     "sra",     "lut",      "mux",      "io",          "memory",      "flop",
      "latch",   "fflop", "sub",  "const", "tup_add", "tup_get", "attr_set", "attr_get", "compile_err", "last_invalid"};

  inline static std::array<mmap_lib::str,32> cell_name;

  inline static absl::flat_hash_map<mmap_lib::str, Ntype_op> cell_name_map;

  class _init {
  public:
    _init();
  };
  static _init _static_initializer;

  // NOTE: order of operands to maximize code gen when "name" is known (typical case)
  inline static std::array<std::array<Port_ID, static_cast<std::size_t>(Ntype_op::Last_invalid)>, 256>         sink_name2pid;
  inline static std::array<std::array<mmap_lib::str, static_cast<std::size_t>(Ntype_op::Last_invalid)>, 11>    sink_pid2name;
  inline static std::array<bool, static_cast<std::size_t>(Ntype_op::Last_invalid)>                             ntype2single_input;
  inline static absl::flat_hash_map<mmap_lib::str, int>                                                        name2pid;

  static constexpr mmap_lib::str get_sink_name_slow(Ntype_op op, int pid);

public:
  static inline constexpr bool is_loop_first(Ntype_op op) { return op == Ntype_op::Const; }
  static inline constexpr bool is_loop_last(Ntype_op op) {
    return static_cast<int>(op) >= static_cast<int>(Ntype_op::Memory) && static_cast<int>(op) <= static_cast<int>(Ntype_op::Sub);
  }

  static inline constexpr bool is_multi_sink(Ntype_op op) {
    return op != Ntype_op::Mult && op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor && op != Ntype_op::Ror
           && op != Ntype_op::Not && op != Ntype_op::CompileErr;
  }

  static inline constexpr bool is_synthesizable(Ntype_op op) {
    return op != Ntype_op::Sub && op != Ntype_op::TupAdd && op != Ntype_op::TupGet && op != Ntype_op::AttrSet
           && op != Ntype_op::AttrGet && op != Ntype_op::CompileErr && op != Ntype_op::Invalid && op != Ntype_op::Last_invalid;
  }

  static inline constexpr bool is_unlimited_sink(Ntype_op op) {
    return op == Ntype_op::IO || op == Ntype_op::LUT || op == Ntype_op::Sub || op == Ntype_op::Memory || op == Ntype_op::Mux
           || op == Ntype_op::CompileErr;
  }
  static inline constexpr bool is_unlimited_driver(Ntype_op op) {
    return op == Ntype_op::Memory || op == Ntype_op::Sub || op == Ntype_op::IO;
  }
  static inline constexpr bool is_multi_driver(Ntype_op op) { return is_unlimited_driver(op); }
  static inline constexpr bool is_single_driver_per_pin(Ntype_op op) {
    if (is_unlimited_sink(op))
      return true;
    auto c = sink_pid2name[0][static_cast<std::size_t>(op)];  // Is first port Upper or lower case
    return c[0] >= 'a' && c[0] <= 'z';
  }

  static inline constexpr int get_sink_pid(Ntype_op op, const mmap_lib::str &str) {
    auto c = str.front();
    // Common case speedup
    if (c >= 'a' && c <= 'f') {
      int pid = c - 'a';
      assert(sink_name2pid[str.front()][static_cast<std::size_t>(op)] == pid);
      assert(get_sink_name(op, pid) == str);
      return pid;
    }
    if (c == '$') {
      assert(str.size()==1);
      assert(sink_name2pid[str.front()][static_cast<std::size_t>(op)] == 0);
      return 0;
    }
    if (c == 'A') {
      assert(sink_name2pid[str.front()][static_cast<std::size_t>(op)] == 0);
      assert(get_sink_name(op, 0) == str);
      return 0;
    }
    if (c == 'B') {
      assert(sink_name2pid[str.front()][static_cast<std::size_t>(op)] == 1);
      assert(get_sink_name(op, 1) == str);
      return 1;
    }

    if (__builtin_expect(is_unlimited_sink(op) && str.size() > 1 && str.front() >= '0' && str.front() <= '9', 0)) {  // unlikely case
      return str.to_i();
    }

    auto pid = sink_name2pid[str.front()][static_cast<std::size_t>(op)];
    assert(pid != Port_invalid);
    assert(get_sink_name(op, pid) == str);
    return pid;
  }

  static inline constexpr mmap_lib::str get_sink_name(Ntype_op op, int pid) {
    if (pid > 10)
      pid = pid % 11;  // wrap names around for multi inputs like memory cell

    auto name = sink_pid2name[pid][static_cast<std::size_t>(op)];
    assert(name != "invalid");
    return name;
  }

  static bool                  has_sink(Ntype_op op, mmap_lib::str str);
  static inline constexpr bool has_sink(Ntype_op op, int pid) {
    if (pid > 10)
      return is_unlimited_sink(op);
    return sink_pid2name[pid][static_cast<std::size_t>(op)] != "invalid";
  }

  static int get_driver_pid(Ntype_op op, mmap_lib::str pin_name) {
    if (likely(!is_multi_driver(op) || pin_name == "%")) {
      return 0;
    }
    return pin_name.to_i();
  }

  static inline constexpr mmap_lib::str get_driver_name(Ntype_op op) {
    return is_multi_driver(op) ? mmap_lib::str("invalid") : mmap_lib::str("Y");
  }

  static inline constexpr bool has_driver(Ntype_op op, int pid) {
    if (pid == 0)
      return true;
    return is_unlimited_driver(op);
  }

  static inline constexpr bool is_single_sink(Ntype_op op) { return ntype2single_input[static_cast<int>(op)]; }

  static mmap_lib::str get_name(Ntype_op op) { return cell_name[static_cast<size_t>(op)]; }

  static Ntype_op get_op(mmap_lib::str name) {
    const auto it = cell_name_map.find(name);
    if (it == cell_name_map.end())
      return Ntype_op::Invalid;
    return it->second;
  }
};
