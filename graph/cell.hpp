//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
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
//  38  Hotmux            -- one-hot select mux (non-loop-last; even slot
//                            between Mux and IO).
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
//  56  AttrSet
//  58  Concat            59 reserved for Last_invalid sentinel
enum class Ntype_op : uint8_t {
  Invalid = 0,  // Detect bugs/unset (not used anywhere). Bit 0 == 0.
  Sum     = 2,
  Mult    = 4,
  Div     = 6,

  And = 8,
  Or  = 10,
  Xor = 12,
  Ror = 14,  // Reduce OR (This is a bit different from the LNAST reduce_or (lnast uses mask)

  Not      = 16,  // bitwise not
  Get_mask = 18,  // To positive signed
  Set_mask = 20,  // To positive signed
  Sext     = 22,  // Sign extend from a given bit (b) position

  LT = 24,  // Less Than   , also GE = !LT
  GT = 26,  // Greater Than, also LE = !GT
  EQ = 28,  // Equal       , also NE = !EQ

  SHL = 30,  // Shift Left Logical
  SRA = 32,  // Shift Right Arithmetic

  LUT    = 34,  // LUT
  Mux    = 36,  // Multiplexor with many options
  Hotmux = 38,  // One-hot select mux (sel is 1-hot encoded; runtime flags
                // non-one-hot select as an error).

  IO = 39,  // Graph Input or Output  -- loop_last (first odd slot)

  //------------------BEGIN PIPELINED (break LOOPS) -- all loop_last (odd)
  Memory = 41,

  Flop  = 43,  // Asynchronous & sync reset flop
  Latch = 45,  // Latch
  Fflop = 47,  // Fluid flop

  Sub    = 49,  // Sub module instance
  //------------------END PIPELINED (break LOOPS)
  Nconst = 50,  // Constant -- non-loop-last; paired with IO via is_loop_first.

  // The ONE recognized clock operator (2f-latch M9): gate / invert / divide.
  // COMBINATIONAL by construction, hence an EVEN slot -- the enable-sampling
  // latch of a real ICG is NOT in the graph, because the cell encodes the
  // glitch-free CONTRACT ("en is sampled at clk_ref's active edge") rather than
  // the implementation. That is what keeps every latch-counting consumer from
  // seeing ICG latches at all.
  Clock_cell = 52,

  // Truncated REMAINDER, `a % b` (sign follows the DIVIDEND, like Verilog `%`
  // and Dlop::rem_op -- NOT a floored modulo). One op: every LNAST/LGraph value
  // is signed, and unsigned is just the non-negative subset, so there is no
  // second unsigned flavour and nothing downstream switches on a sign flag.
  //
  // Slot 54 was the last free EVEN slot below AttrSet, and even is required:
  // bit 0 is is_loop_last, and a remainder is combinational. Growing past
  // AttrSet moves Last_invalid, which resizes the three `Last_invalid`-sized
  // tables below.
  //
  // It does NOT invalidate serialized lgdbs (an earlier version of this comment
  // claimed it did): `Last_invalid` is a compile-time sentinel that is never
  // stored in a node and never serialized, and appending a slot leaves every
  // existing op's raw value unchanged. Only the reverse direction breaks -- an
  // lgdb written WITH the new op and read by an OLDER binary indexes past the
  // end of that build's `cell_name_sv`.
  Rem = 54,

  // High-level construct kept for bitwidth's leftover-AttrSet cleanup pass.
  // Tuple-related ops (TupAdd, TupGet) and AttrGet were dropped along with
  // cprop's tuple_pass; CompileErr was dropped (no producer post-migration).
  AttrSet = 56,

  // n-ary bit CONCATENATION, MSB-first (Verilog `{a, b, c}`), combinational.
  //
  // Sinks are INTERLEAVED (value, declared-width) pairs on the unlimited-sink
  // `p0, p1, ...` names: p0 = the most significant lane's value, p1 = a
  // comptime const holding that lane's DECLARED width in bits, p2/p3 = the next
  // lane down, and so on. Lane i therefore lives at pids 2i / 2i+1.
  //
  // The width has to be an explicit operand because it is NOT recoverable from
  // the lane driver: `bits` on that pin is an upper bound that bitwidth/cprop
  // are free to narrow, and the value's significant bits are narrower still --
  // and dropping a lane's leading zeros shifts every lane ABOVE it. The width
  // is frozen from the LNAST DECLARED type at lowering time (upass.tolg) and is
  // never re-derived. A lane's `w` is the field width: `bits - 1` for an
  // unsigned driver (whose top stored bit is the always-zero sign slot), `bits`
  // for a signed one, matching `Dlop::Concat_lane`.
  //
  // Semantics (unlimited precision): out = sum_i (v_i mod 2^w_i) << offset_i,
  // offset_i = sum of the widths of every lane BELOW i. Each lane is masked
  // into its own window, so a negative lane lands as its two's-complement
  // pattern and an over-wide lane truncates -- exactly the Set_mask lane-write
  // rule. Unknowns are per-lane and positional (no whole-plane smearing). The
  // result is ALWAYS non-negative, so the driver pin stamps
  // bits = sum(w_i) + 1 and is `unsign`.
  //
  // 58 keeps the even/odd invariant (combinational => even). 40..48 are also
  // free evens but are reserved by construction as the opposite-polarity twins
  // of Memory/Flop/Latch/Fflop/Sub -- and cprop's several `op > Ntype_op::Hotmux`
  // ordered tests read that band as state/boundary.
  Concat = 58,

  Last_invalid = 59
};

// Encoding invariant: bit 0 == is_loop_last.
static_assert((static_cast<uint8_t>(Ntype_op::IO) & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Memory) & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Flop) & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Latch) & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Fflop) & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Sub) & 1) == 1);
static_assert((static_cast<uint8_t>(Ntype_op::Clock_cell) & 1) == 0);  // combinational
static_assert((static_cast<uint8_t>(Ntype_op::Rem) & 1) == 0);         // combinational
static_assert((static_cast<uint8_t>(Ntype_op::Concat) & 1) == 0);      // combinational
static_assert((static_cast<uint8_t>(Ntype_op::Sum) & 1) == 0);
static_assert((static_cast<uint8_t>(Ntype_op::Nconst) & 1) == 0);
static_assert((static_cast<uint8_t>(Ntype_op::Invalid) & 1) == 0);
static_assert((static_cast<uint8_t>(Ntype_op::Hotmux) & 1) == 0);

class Ntype {
public:
  // Memory cell sink pins are laid out in PORT BLOCKS of this stride: port `i`'s
  // per-port pin at base offset `off` lives at raw_pid = i*Memory_port_stride + off.
  // The 0..Memory_port_stride-1 block also holds the cell-global/singleton pins.
  // Keep in lockstep with get_sink_name's `% Memory_port_stride` wrap and every
  // consumer that decodes a Memory raw_pid (tolg, cgen_verilog, cgen_sim, bitwidth,
  // pass/lec). Was 12; widened to 16 to fit the whole-array pins update(12)/
  // update_enable(13)/reset(14) and, since the `ordering` work, undef(15).
  // The cell-global block is now FULL: a new singleton pin needs a wider stride.
  static constexpr hhds::Port_id Memory_port_stride = 16;
  // Reserved DRIVER pid for the async whole-array `read_all` output (width
  // size*bits). Driver pids are sparse, so a high reserved value cannot collide
  // with the sequential read-dout pids (n_wr_total + r). Well below Port_invalid.
  static constexpr hhds::Port_id Memory_readall_pid = (hhds::Port_id{1} << 20);

  // Memory `posclk` SENTINEL: the source memory's ports do NOT all commit on the
  // same clock edge, so the cell's ONE global polarity cannot represent it.
  //
  // The language permits such a memory and the readers must not reject it (user
  // ruling 2026-08-02: parse it, keep it, let it regenerate) — but it is a weird
  // shape LiveHD does not model, so FORMAL refuses it BY NAME and the user opts
  // back in per memory with `--set formal.ignore_memory=<name>`, which blackboxes
  // it. See pass/lec/encode.cpp and pass/lec/README.md §2.
  //
  // 2 and not a new pin: the cell-global pin block above is FULL. Every existing
  // consumer decides the edge with `!is_known_false()`, so a 2 reads as posedge —
  // the same lossy-but-harmless answer they already gave for this shape — while
  // the encoder tests for the sentinel explicitly. Only pass/lec may treat it as
  // anything other than "not negedge".
  static constexpr int Memory_posclk_mixed = 2;

protected:
  // Sparse: indexed by Ntype_op underlying value. Unused slots ("invalid")
  // never round-trip through cell_name_map (see the init in cell.cpp).
  inline static constexpr auto cell_name_sv = []() {
    std::array<std::string_view, static_cast<size_t>(Ntype_op::Last_invalid) + 1> a{};
    for (auto& s : a) {
      s = "invalid";
    }
    a[static_cast<size_t>(Ntype_op::Sum)]      = "sum";
    a[static_cast<size_t>(Ntype_op::Mult)]     = "mult";
    a[static_cast<size_t>(Ntype_op::Div)]      = "div";
    a[static_cast<size_t>(Ntype_op::And)]      = "and";
    a[static_cast<size_t>(Ntype_op::Or)]       = "or";
    a[static_cast<size_t>(Ntype_op::Xor)]      = "xor";
    a[static_cast<size_t>(Ntype_op::Ror)]      = "ror";
    a[static_cast<size_t>(Ntype_op::Not)]      = "not";
    a[static_cast<size_t>(Ntype_op::Get_mask)] = "get_mask";
    a[static_cast<size_t>(Ntype_op::Set_mask)] = "set_mask";
    a[static_cast<size_t>(Ntype_op::Sext)]     = "sext";
    a[static_cast<size_t>(Ntype_op::LT)]       = "lt";
    a[static_cast<size_t>(Ntype_op::GT)]       = "gt";
    a[static_cast<size_t>(Ntype_op::EQ)]       = "eq";
    a[static_cast<size_t>(Ntype_op::SHL)]      = "shl";
    a[static_cast<size_t>(Ntype_op::SRA)]      = "sra";
    a[static_cast<size_t>(Ntype_op::LUT)]      = "lut";
    a[static_cast<size_t>(Ntype_op::Mux)]      = "mux";
    a[static_cast<size_t>(Ntype_op::Hotmux)]   = "hotmux";
    a[static_cast<size_t>(Ntype_op::IO)]       = "io";
    a[static_cast<size_t>(Ntype_op::Memory)]   = "memory";
    a[static_cast<size_t>(Ntype_op::Flop)]     = "flop";
    a[static_cast<size_t>(Ntype_op::Latch)]    = "latch";
    a[static_cast<size_t>(Ntype_op::Fflop)]    = "fflop";
    a[static_cast<size_t>(Ntype_op::Sub)]      = "sub";
    a[static_cast<size_t>(Ntype_op::Nconst)]   = "const";
    a[static_cast<size_t>(Ntype_op::Clock_cell)] = "clock_cell";
    a[static_cast<size_t>(Ntype_op::Rem)]      = "rem";
    a[static_cast<size_t>(Ntype_op::Concat)]   = "concat";
    a[static_cast<size_t>(Ntype_op::AttrSet)]  = "attr_set";
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
  inline static std::array<std::array<std::string, static_cast<std::size_t>(Ntype_op::Last_invalid)>, Memory_port_stride> sink_pid2name;
  inline static std::array<bool, static_cast<std::size_t>(Ntype_op::Last_invalid)>                           ntype2single_input;
  inline static absl::flat_hash_map<std::string, hhds::Port_id>                                              name2pid;

  static constexpr std::string_view get_sink_name_slow(Ntype_op op, hhds::Port_id pid);

public:
  static inline constexpr bool is_loop_first(Ntype_op op) { return op == Ntype_op::Nconst || op == Ntype_op::IO; }
  // Bit 0 of the underlying value encodes loop_last (see the Ntype_op
  // declaration). This matches the bit HHDS already reserves for its own
  // is_loop_last flag, so a LiveHD-stored type round-trips both meanings.
  static inline constexpr bool is_loop_last(Ntype_op op) { return (static_cast<uint8_t>(op) & 1) != 0; }

  // Ops that only MOVE bits: a pin-tracker maps each result bit back to the
  // (source pin, source bit) it came from, so these mint no gate and add no
  // delay. Concat belongs here for the same reason -- it is wiring/packing that
  // renames bit positions (see Pin_tracker::add_concat and graph_util::ge_weight,
  // which charges a Concat zero gates).
  static inline constexpr bool is_pin_trackable(Ntype_op op) {
    return op == Ntype_op::Set_mask || op == Ntype_op::Get_mask || op == Ntype_op::SHL || op == Ntype_op::SRA || op == Ntype_op::And
           || op == Ntype_op::Or || op == Ntype_op::Sext || op == Ntype_op::Concat;
  }

  static inline constexpr bool is_unlimited_sink(Ntype_op op) {
    return op == Ntype_op::IO || op == Ntype_op::LUT || op == Ntype_op::Sub || op == Ntype_op::Memory || op == Ntype_op::Mux
           || op == Ntype_op::Hotmux || op == Ntype_op::Concat;
  }
  static inline constexpr bool is_unlimited_driver(Ntype_op op) {
    return op == Ntype_op::Memory || op == Ntype_op::Sub || op == Ntype_op::IO;
  }
  // True when the CELL exposes more than one driver (output) pin, so callers
  // must address an output by <PID> instead of the single "Y" driver name.
  // (Memory/Sub/IO.) This is about the cell's output arity -- NOT about a sink
  // pin being fed by several drivers; for that, see is_sink_single_driver().
  static inline constexpr bool has_multiple_driver_pins(Ntype_op op) { return is_unlimited_driver(op); }

  // True when a given SINK pin accepts at most one driver pin. The handful of
  // sinks that legally take several drivers fold them with the cell's identity
  // op (Sum sums, And/Or/Xor reduce):
  //   Sum/LT/GT               a (pid 0) and b (pid 1)
  //   Mult/And/Or/Xor/Ror/EQ  a (pid 0)
  // These multi-driver sinks carry an 's'-suffixed name ("as"/"bs") so the
  // distinction is visible at every use. Every other sink is single-driver --
  // including SHL/SRA b (the shift amount takes exactly one driver; the old
  // one-hot `a<<(b0,b1)` multi-driver SHL form was removed, comptime-folded
  // only). Keep this op/pid list in sync with get_sink_name_slow's 's' suffixes.
  static inline constexpr bool is_sink_single_driver(Ntype_op op, hhds::Port_id pid) {
    switch (op) {
      case Ntype_op::Sum:
      case Ntype_op::LT:
      case Ntype_op::GT: return pid != 0 && pid != 1;  // as, bs
      case Ntype_op::Mult:
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor:
      case Ntype_op::Ror:
      case Ntype_op::EQ: return pid != 0;  // as
      default: return true;
    }
  }

  // Returns the hhds::Port_id for a LiveHD sink name on the given op, or
  // livehd::Port_invalid when the name is not a valid sink for this op.
  // The per-op first-char table is the fast path; same-op sink names that
  // share a leading char (e.g. Flop posclk/pipe_min/pipe_max, all 'p')
  // resolve through the global name2pid map with a per-op verify — the
  // first-char slot keeps the first-declared (lowest-pid) name.
  static inline hhds::Port_id get_sink_pid(Ntype_op op, std::string_view str) {
    auto c = str.front();
    // Common case speedup
    if (c >= 'a' && c <= 'f') {
      hhds::Port_id pid = static_cast<hhds::Port_id>(c - 'a');
      assert(sink_name2pid[str.front()][static_cast<std::size_t>(op)] == pid);
      assert(get_sink_name(op, pid) == str);
      return pid;
    }
    if (__builtin_expect(is_unlimited_sink(op) && str.size() > 1 && str.front() >= '0' && str.front() <= '9',
                         0)) {  // pid>11 names: "<num><base>" (e.g. "12addr", "14clock_pin")
      return static_cast<hhds::Port_id>(str_tools::to_i(str));
    }
    if (__builtin_expect(is_unlimited_sink(op) && str.size() >= 2 && str.front() == 'p' && str[1] >= '0' && str[1] <= '9',
                         0)) {  // unlimited-sink names "p0".."p10" collide on sink_name2pid['p']; parse digits.
      return static_cast<hhds::Port_id>(str_tools::to_i(str.substr(1)));
    }

    auto pid = sink_name2pid[str.front()][static_cast<std::size_t>(op)];
    if (pid != livehd::Port_invalid && sink_pid2name[pid][static_cast<std::size_t>(op)] == str) {
      return pid;
    }
    // Slow path: first-char miss or a same-first-char sibling pin.
    auto it = name2pid.find(str);
    if (it != name2pid.end() && sink_pid2name[it->second][static_cast<std::size_t>(op)] == str) {
      return it->second;
    }
    return livehd::Port_invalid;
  }

  static inline std::string get_sink_name(Ntype_op op, hhds::Port_id pid) {
    if (pid >= Memory_port_stride) {
      auto pid_index = pid % Memory_port_stride;  // wrap names for multi inputs like the memory cell (port stride)
      auto name      = sink_pid2name[pid_index][static_cast<std::size_t>(op)];
      assert(name != "invalid");

      return absl::StrCat(pid, name);
    }

    auto name = sink_pid2name[pid][static_cast<std::size_t>(op)];
    assert(name != "invalid");
    return name;
  }

  static inline constexpr std::string_view get_driver_name(Ntype_op op) {
    (void)op;
    assert(!has_multiple_driver_pins(op));  // use <PID> for multi-driver-pin cells
    return {"Y"};
  }

  static std::string_view get_name(Ntype_op op) { return cell_name_sv[static_cast<size_t>(op)]; }

  static Ntype_op get_op(std::string_view name) {
    const auto it = cell_name_map.find(name);
    if (it == cell_name_map.end()) {
      return Ntype_op::Invalid;
    }
    return it->second;
  }
};
