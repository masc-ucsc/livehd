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
#include "hhds/graph_sizing.hpp"
#include "likely.hpp"
#include "str_tools.hpp"

namespace livehd {
// Sentinel for `hhds::Port_id` returned by Ntype::get_sink_pid on an
// unrecognised name. HHDS itself does not declare an invalid-port constant;
// it just sizes the port_id field to Port_bits (22) bits. Anything outside
// that range is fine to use as the "no such pin" marker.
inline constexpr hhds::Port_id Port_invalid = (hhds::Port_id{1} << hhds::Port_bits) - 1;
}  // namespace livehd

// Encoding invariant: bit 0 of the underlying value is `is_loop_last`.
// HHDS reserves the low bit of NodeEntry::type for its own loop-last flag,
// so making the Ntype_op encoding match means LiveHD can store the enum
// value directly into hhds::Node_class without a shift on either side.
//
// Layout: non-loop-last ops take EVEN values, loop-last ops take ODD values.
// Each op-line below has its underlying value next to it. The implied
// neighbour (value ± 1) is the unused slot for the opposite loop-last
// polarity; `cell_name_sv[]` keeps "invalid" there and `cell.cpp`'s init
// loop skips it. Don't renumber to a packed sequence — the bit-0 invariant
// is what lets the round-trip through `hhds::Node_class::set_type` /
// `get_type` work without a shift.
//
// Value ranges (each "..." is a non-loop-last slot whose +1 odd neighbour
// is empty by construction):
//   0  Invalid            -- the empty slot at value 1 is never used
//   2  Sum                3 unused
//   4  Mult               5 unused
//   ...
//  16  Not               17 unused
//   ...
//  36  Mux               37 unused
//  38  Hotmux            -- one-hot select mux (non-loop-last; sits in the
//                            even slot between Mux and IO).
//
//  39  IO  ← FIRST LOOP-LAST OP. Note the jump from 38→39 keeps the
//                        even/odd bit-0 invariant.
//  41  Memory   (loop_last)
//  43  Flop     (loop_last)
//  45  Latch    (loop_last)
//  47  Fflop    (loop_last)
//  49  Sub      (loop_last)
//  50  Nconst             -- non-loop-last; sits next to Sub on purpose so
//                            is_loop_first(Nconst||IO) is the obvious pair.
//   ...
//  56  AttrSet           57 reserved for Last_invalid sentinel
enum class Ntype_op : uint8_t {
  Invalid    = 0,  // Detect bugs/unset (not used anywhere). Bit 0 == 0.
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
  Hotmux     = 38,  // One-hot select mux (sel is 1-hot encoded; runtime
                    // flags non-one-hot select as an error).

  IO         = 39,  // Graph Input or Output  -- loop_last (first odd slot)

  //------------------BEGIN PIPELINED (break LOOPS) -- all loop_last (odd)
  Memory     = 41,

  Flop       = 43,  // Asynchronous & sync reset flop
  Latch      = 45,  // Latch
  Fflop      = 47,  // Fluid flop

  Sub        = 49,  // Sub module instance
  //------------------END PIPELINED (break LOOPS)
  Nconst     = 50,  // Constant -- non-loop-last; paired with IO via is_loop_first.

  // High-level construct kept for bitwidth's leftover-AttrSet cleanup pass.
  // Tuple-related ops (TupAdd, TupGet) and AttrGet were dropped along with
  // cprop's tuple_pass; CompileErr was dropped (no producer post-migration).
  AttrSet    = 56,

  Last_invalid = 57
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
static_assert((static_cast<uint8_t>(Ntype_op::Hotmux) & 1) == 0);

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
    a[static_cast<size_t>(Ntype_op::Hotmux)]     = "hotmux";
    a[static_cast<size_t>(Ntype_op::IO)]         = "io";
    a[static_cast<size_t>(Ntype_op::Memory)]     = "memory";
    a[static_cast<size_t>(Ntype_op::Flop)]       = "flop";
    a[static_cast<size_t>(Ntype_op::Latch)]      = "latch";
    a[static_cast<size_t>(Ntype_op::Fflop)]      = "fflop";
    a[static_cast<size_t>(Ntype_op::Sub)]        = "sub";
    a[static_cast<size_t>(Ntype_op::Nconst)]     = "const";
    a[static_cast<size_t>(Ntype_op::AttrSet)]    = "attr_set";
    return a;
  }();

  inline static absl::flat_hash_map<std::string, Ntype_op> cell_name_map;

  class _init {
  public:
    _init();
  };
  static _init _static_initializer;

  // NOTE: order of operands to maximize code gen when "name" is known (typical case)
  inline static std::array<std::array<hhds::Port_id, static_cast<std::size_t>(Ntype_op::Last_invalid)>, 256> sink_name2pid;
  inline static std::array<std::array<std::string, static_cast<std::size_t>(Ntype_op::Last_invalid)>, 11>    sink_pid2name;
  inline static std::array<bool, static_cast<std::size_t>(Ntype_op::Last_invalid)>                           ntype2single_input;
  inline static absl::flat_hash_map<std::string, hhds::Port_id>                                              name2pid;

  static constexpr std::string_view get_sink_name_slow(Ntype_op op, hhds::Port_id pid);

public:
  static inline constexpr bool is_loop_first(Ntype_op op) { return op == Ntype_op::Nconst || op == Ntype_op::IO; }
  // Bit 0 of the underlying value encodes loop_last (see the Ntype_op
  // declaration). This matches the bit HHDS already reserves for its own
  // is_loop_last flag, so a LiveHD-stored type round-trips both meanings.
  static inline constexpr bool is_loop_last(Ntype_op op) { return (static_cast<uint8_t>(op) & 1) != 0; }

  static inline constexpr bool is_multi_sink(Ntype_op op) {
    return op != Ntype_op::Mult && op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor && op != Ntype_op::Ror
           && op != Ntype_op::Not;
  }

  static inline constexpr bool is_pin_trackable(Ntype_op op) {
    return op == Ntype_op::Set_mask || op == Ntype_op::Get_mask || op == Ntype_op::SHL || op == Ntype_op::SRA || op == Ntype_op::And
           || op == Ntype_op::Or || op == Ntype_op::Sext;
  }

  static inline constexpr bool is_synthesizable(Ntype_op op) {
    return op != Ntype_op::Sub && op != Ntype_op::AttrSet && op != Ntype_op::Invalid && op != Ntype_op::Last_invalid;
  }

  static inline constexpr bool is_unlimited_sink(Ntype_op op) {
    return op == Ntype_op::IO || op == Ntype_op::LUT || op == Ntype_op::Sub || op == Ntype_op::Memory || op == Ntype_op::Mux
           || op == Ntype_op::Hotmux;
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

  // Returns the hhds::Port_id for a LiveHD sink name on the given op, or
  // livehd::Port_invalid when the name is not a valid sink for this op.
  // The round-trip asserts (name → pid → name) stay enabled in debug builds
  // for the recognised-name paths.
  static inline constexpr hhds::Port_id get_sink_pid(Ntype_op op, std::string_view str) {
    auto c = str.front();
    // Common case speedup
    if (c >= 'a' && c <= 'f') {
      hhds::Port_id pid = static_cast<hhds::Port_id>(c - 'a');
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
      return static_cast<hhds::Port_id>(str_tools::to_i(str));
    }

    auto pid = sink_name2pid[str.front()][static_cast<std::size_t>(op)];
    if (pid == livehd::Port_invalid) {
      return livehd::Port_invalid;
    }
    assert(get_sink_name(op, pid) == str);
    return pid;
  }

  static inline std::string get_sink_name(Ntype_op op, hhds::Port_id pid) {
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
  static inline constexpr bool has_sink(Ntype_op op, hhds::Port_id pid) {
    if (pid > 10) {
      return is_unlimited_sink(op);
    }
    return sink_pid2name[pid][static_cast<std::size_t>(op)] != "invalid";
  }

  static inline constexpr std::string_view get_driver_name(Ntype_op op) {
    (void)op;
    assert(!is_multi_driver(op));  // use <PID> for multidriveer pins
    return {"Y"};
  }

  static inline constexpr bool has_driver(Ntype_op op, hhds::Port_id pid) {
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
