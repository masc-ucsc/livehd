//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "graph_sizing.hpp"
#include "likely.hpp"
#include "str_tools.hpp"

// Encoding invariant: bit 0 of the underlying value is `is_loop_last`.
// HHDS reserves the low bit of NodeEntry::type for its own loop-last flag,
// so making the Ntype_op encoding match means LiveHD can store the enum
// value directly into hhds::Node_class without a shift on either side.
//
// Layout: non-loop-last ops take even values, loop-last ops take odd values.
// Relative ordering is preserved so the range-based predicates below
// (is_synthesizable, etc.) keep working. Gaps (odd slots for non-loop-last
// ops, and one even slot interleaved with the loop-last block) are unused
// and array slots at those indices stay "invalid" — see cell.cpp init.
enum class Ntype_op : uint8_t {
  Invalid    = 0,  // Detect bugs/unset (not used anywhere)
  Sum        = 2,
  Mult       = 4,
  Div        = 6,

  And        = 8,
  Or         = 10,
  Xor        = 12,
  Ror        = 14,  // Reduce OR (This is a bit different from the LNAST reduce_or (lnast uses mask)

  Not        = 16,  // bitwise not
  Get_mask   = 18,  // To positive signed
  Set_mask   = 20,  // To positive signed
  Sext       = 22,  // Sign extend from a given bit (b) position

  LT         = 24,  // Less Than   , also GE = !LT
  GT         = 26,  // Greater Than, also LE = !GT
  EQ         = 28,  // Equal       , also NE = !EQ

  SHL        = 30,  // Shift Left Logical
  SRA        = 32,  // Shift Right Arithmetic

  LUT        = 34,  // LUT
  Mux        = 36,  // Multiplexor with many options

  IO         = 39,  // Graph Input or Output  -- loop_last

  //------------------BEGIN PIPELINED (break LOOPS) -- all loop_last (odd)
  Memory     = 41,

  Flop       = 43,  // Asynchronous & sync reset flop
  Latch      = 45,  // Latch
  Fflop      = 47,  // Fluid flop

  Sub        = 49,  // Sub module instance
  //------------------END PIPELINED (break LOOPS)
  Nconst     = 50,  // Constant

  // High Level Lgraph constructs

  TupAdd     = 52,
  TupGet     = 54,

  AttrSet    = 56,
  AttrGet    = 58,

  CompileErr = 60,  // Indicate a compile error during a pass

  Last_invalid = 61
};

// Encoding invariant: bit 0 == is_loop_last.
static_assert((static_cast<uint8_t>(Ntype_op::IO)     & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Memory) & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Flop)   & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Latch)  & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Fflop)  & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Sub)    & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Sum)    & 1) == 0);
static_assert((static_cast<uint8_t>(Ntype_op::Nconst) & 1) == 0);
static_assert((static_cast<uint8_t>(Ntype_op::Invalid)& 1) == 0);

class Ntype {
protected:
  // Sparse: indexed by Ntype_op underlying value. Unused slots ("invalid")
  // never round-trip through cell_name_map (see the init in cell.cpp).
  inline static constexpr auto cell_name_sv = []() {
    std::array<std::string_view, static_cast<size_t>(Ntype_op::Last_invalid) + 1> a{};
    for (auto& s : a) {
      s = "invalid";
    }
    a[static_cast<size_t>(Ntype_op::Sum)]        = "sum";
    a[static_cast<size_t>(Ntype_op::Mult)]       = "mult";
    a[static_cast<size_t>(Ntype_op::Div)]        = "div";
    a[static_cast<size_t>(Ntype_op::And)]        = "and";
    a[static_cast<size_t>(Ntype_op::Or)]         = "or";
    a[static_cast<size_t>(Ntype_op::Xor)]        = "xor";
    a[static_cast<size_t>(Ntype_op::Ror)]        = "ror";
    a[static_cast<size_t>(Ntype_op::Not)]        = "not";
    a[static_cast<size_t>(Ntype_op::Get_mask)]   = "get_mask";
    a[static_cast<size_t>(Ntype_op::Set_mask)]   = "set_mask";
    a[static_cast<size_t>(Ntype_op::Sext)]       = "sext";
    a[static_cast<size_t>(Ntype_op::LT)]         = "lt";
    a[static_cast<size_t>(Ntype_op::GT)]         = "gt";
    a[static_cast<size_t>(Ntype_op::EQ)]         = "eq";
    a[static_cast<size_t>(Ntype_op::SHL)]        = "shl";
    a[static_cast<size_t>(Ntype_op::SRA)]        = "sra";
    a[static_cast<size_t>(Ntype_op::LUT)]        = "lut";
    a[static_cast<size_t>(Ntype_op::Mux)]        = "mux";
    a[static_cast<size_t>(Ntype_op::IO)]         = "io";
    a[static_cast<size_t>(Ntype_op::Memory)]     = "memory";
    a[static_cast<size_t>(Ntype_op::Flop)]       = "flop";
    a[static_cast<size_t>(Ntype_op::Latch)]      = "latch";
    a[static_cast<size_t>(Ntype_op::Fflop)]      = "fflop";
    a[static_cast<size_t>(Ntype_op::Sub)]        = "sub";
    a[static_cast<size_t>(Ntype_op::Nconst)]     = "const";
    a[static_cast<size_t>(Ntype_op::TupAdd)]     = "tup_add";
    a[static_cast<size_t>(Ntype_op::TupGet)]     = "tup_get";
    a[static_cast<size_t>(Ntype_op::AttrSet)]    = "attr_set";
    a[static_cast<size_t>(Ntype_op::AttrGet)]    = "attr_get";
    a[static_cast<size_t>(Ntype_op::CompileErr)] = "compile_err";
    return a;
  }();

  inline static absl::flat_hash_map<std::string, Ntype_op> cell_name_map;

  class _init {
  public:
    _init();
  };
  static _init _static_initializer;

  // NOTE: order of operands to maximize code gen when "name" is known (typical case)
  inline static std::array<std::array<Port_ID, static_cast<std::size_t>(Ntype_op::Last_invalid)>, 256>    sink_name2pid;
  inline static std::array<std::array<std::string, static_cast<std::size_t>(Ntype_op::Last_invalid)>, 11> sink_pid2name;
  inline static std::array<bool, static_cast<std::size_t>(Ntype_op::Last_invalid)>                        ntype2single_input;
  inline static absl::flat_hash_map<std::string, int>                                                     name2pid;

  static constexpr std::string_view get_sink_name_slow(Ntype_op op, int pid);

public:
  static inline constexpr bool is_loop_first(Ntype_op op) { return op == Ntype_op::Nconst || op == Ntype_op::IO; }
  // Bit 0 of the underlying value encodes loop_last (see the Ntype_op
  // declaration). This matches the bit HHDS already reserves for its own
  // is_loop_last flag, so a LiveHD-stored type round-trips both meanings.
  static inline constexpr bool is_loop_last(Ntype_op op) { return (static_cast<uint8_t>(op) & 1) != 0; }

  static inline constexpr bool is_multi_sink(Ntype_op op) {
    return op != Ntype_op::Mult && op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor && op != Ntype_op::Ror
           && op != Ntype_op::Not && op != Ntype_op::CompileErr;
  }

  static inline constexpr bool is_pin_trackable(Ntype_op op) {
    return op == Ntype_op::Set_mask || op == Ntype_op::Get_mask || op == Ntype_op::SHL || op == Ntype_op::SRA || op == Ntype_op::And
           || op == Ntype_op::Or || op == Ntype_op::Sext;
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
  static inline bool           is_single_driver_per_pin(Ntype_op op) {
    if (is_unlimited_sink(op)) {
      return true;
    }
    auto c = sink_pid2name[0][static_cast<std::size_t>(op)];  // Is first port Upper or lower case
    return c[0] >= 'a' && c[0] <= 'z';
  }

  static inline constexpr int get_sink_pid(Ntype_op op, std::string_view str) {
    auto c = str.front();
    // Common case speedup
    if (c >= 'a' && c <= 'f') {
      int pid = c - 'a';
      assert(sink_name2pid[str.front()][static_cast<std::size_t>(op)] == pid);
      assert(get_sink_name(op, pid) == str);
      return pid;
    }
    if (c == '$') {
      assert(str.size() == 1);
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

    if (__builtin_expect(is_unlimited_sink(op) && str.size() > 1 && str.front() >= '0' && str.front() <= '9',
                         0)) {  // unlikely case
      return str_tools::to_i(str);
    }

    auto pid = sink_name2pid[str.front()][static_cast<std::size_t>(op)];
    assert(pid != Port_invalid);
    assert(get_sink_name(op, pid) == str);
    return pid;
  }

  static inline std::string get_sink_name(Ntype_op op, int pid) {
    if (pid > 10) {
      auto pid_index = pid % 11;  // wrap names around for multi inputs like memory cell
      auto name      = sink_pid2name[pid_index][static_cast<std::size_t>(op)];
      assert(name != "invalid");

      return absl::StrCat(pid, name);
    }

    auto name = sink_pid2name[pid][static_cast<std::size_t>(op)];
    assert(name != "invalid");
    return name;
  }

  static bool                  has_sink(Ntype_op op, std::string_view str);
  static inline constexpr bool has_sink(Ntype_op op, int pid) {
    if (pid > 10) {
      return is_unlimited_sink(op);
    }
    return sink_pid2name[pid][static_cast<std::size_t>(op)] != "invalid";
  }

  static int get_driver_pid(Ntype_op op, std::string_view pin_name) {
    if (likely(!is_multi_driver(op) || pin_name == "%")) {
      return 0;
    }
    return str_tools::to_i(pin_name);
  }

  static inline constexpr std::string_view get_driver_name(Ntype_op op) {
    (void)op;
    assert(!is_multi_driver(op));  // use <PID> for multidriveer pins
    return {"Y"};
  }

  static inline constexpr bool has_driver(Ntype_op op, int pid) {
    if (pid == 0) {
      return true;
    }
    return is_unlimited_driver(op);
  }

  static inline constexpr bool is_single_sink(Ntype_op op) { return ntype2single_input[static_cast<int>(op)]; }

  static std::string_view get_name(Ntype_op op) { return cell_name_sv[static_cast<size_t>(op)]; }

  static Ntype_op get_op(std::string_view name) {
    const auto it = cell_name_map.find(name);
    if (it == cell_name_map.end()) {
      return Ntype_op::Invalid;
    }
    return it->second;
  }
};
