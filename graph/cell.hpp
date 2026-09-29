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

// Ntype_op is a DENSE sequence grouped in BANDS, so the loop-first / loop-last
// / combinational questions are contiguous RANGE tests (see Ntype below), and
// the `Last_invalid`-sized tables in Ntype have no holes.
//
// Storage in hhds: `NodeEntry::type` is 16 bits and hhds reserves its bit 0 as
// the per-node `is_loop_break` flag (a forward/backward iterator cut point).
// LiveHD stores `(op << 1) | loop_last` and reads back `type >> 1` -- see
// node_util.hpp `set_type_op` / `type_op_of`. The op never depends on that bit,
// so a Sub instance keeps reading as `Sub` while hhds's `set_subnode` decides
// per INSTANCE whether the cut bit is set (only when the child body holds
// state). No value below is persisted by name: an lgdb stores the raw type, so
// inserting an op in the middle invalidates every existing lgdb.
enum class Ntype_op : uint8_t {
  Invalid = 0,  // Detect bugs/unset (not used anywhere).

  //------------------BEGIN COMBINATIONAL, COMPUTED: [Sum .. Clock_cell]
  Sum,
  Mult,
  Div,
  // Truncated REMAINDER, `a % b` (sign follows the DIVIDEND, like Verilog `%`
  // and Dlop::rem_op -- NOT a floored modulo). One op: every LNAST/LGraph value
  // is signed, and unsigned is just the non-negative subset, so there is no
  // second unsigned flavour and nothing downstream switches on a sign flag.
  Rem,

  And,
  Or,
  Xor,
  Ror,  // Reduce OR (This is a bit different from the LNAST reduce_or (lnast uses mask)

  Not,  // bitwise not

  // Get_mask(a, mask): the bits of `a` selected by `mask`, packed LSB-first as
  // an UNSIGNED value (`#[..]` zero-extends; only an explicit `#sext` may be
  // negative). Set_mask(a, mask, value) replaces them.
  //
  // CONTRACT on the `mask` pin -- see livehd::graph_util::mask_window:
  //   * it is a CONSTANT. A runtime mask has never been representable (tolg
  //     lowers a dynamic part-select to And/Or/Ror and errors on a mask pin
  //     that is not const), and
  //   * that constant is either the CONTIGUOUS window Dlop::get_mask_value(hi-1,
  //     lo) with 0 <= lo < hi, or the literal -1 meaning "the whole value":
  //     Get_mask(a, -1) is to-unsigned, Set_mask(a, -1, v) is v.
  //
  // A SPARSE mask (0x0f0f) is not part of the IR. Nothing produces one -- a
  // concat lvalue `{a[3],a[0]} = x` becomes one Set_mask per operand, every
  // `#[...]` form is a single window, and yosys hands over one SigChunk at a
  // time -- and consumers that had to tolerate one paid for it with per-bit
  // gather loops and with bail arms that declined real rewrites.
  Get_mask,
  Set_mask,
  Sext,  // Sign extend from a given bit (b) position

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
  // never re-derived. A lane's `w` is the literal field width for both signed
  // and unsigned drivers, matching `Dlop::Concat_lane`.
  //
  // Semantics (unlimited precision): out = sum_i (v_i mod 2^w_i) << offset_i,
  // offset_i = sum of the widths of every lane BELOW i. Each lane is masked
  // into its own window, so a negative lane lands as its two's-complement
  // pattern and an over-wide lane truncates -- exactly the Set_mask lane-write
  // rule. Unknowns are per-lane and positional (no whole-plane smearing). The
  // result is ALWAYS non-negative, so the driver pin stamps the exact literal
  // width bits = sum(w_i) and is `unsign`.
  Concat,

  LT,  // Less Than   , also GE = !LT
  GT,  // Greater Than, also LE = !GT
  EQ,  // Equal       , also NE = !EQ

  SHL,  // Shift Left Logical
  SRA,  // Shift Right Arithmetic

  LUT,     // LUT
  Mux,     // Multiplexor with many options
  Hotmux,  // Interleaved (control, value) pairs, optional trailing default.
           // Controls are one-bit and mutually exclusive (one-hot-or-zero).

  // The ONE recognized clock operator (2f-latch M9): gate / invert / divide.
  // COMBINATIONAL by construction -- the enable-sampling latch of a real ICG
  // is NOT in the graph, because the cell encodes the glitch-free CONTRACT
  // ("en is sampled at clk_ref's active edge") rather than the implementation.
  // That is what keeps every latch-counting consumer from seeing ICG latches.
  Clock_cell,
  //------------------END COMBINATIONAL

  IO,  // Graph Input or Output -- the ONE loop-first op (constants live in the
       // const pool as pins of the builtin const node, never as a typed node).

  //------------------BEGIN LOOP-LAST (break LOOPS): [Memory .. Sub]
  Memory,
  Flop,   // Asynchronous & sync reset flop
  Latch,  // Latch
  Fflop,  // Fluid flop
  Sub,    // Sub module instance. Conservatively loop-last at the op level; the
          // per-instance hhds cut bit is cleared when the child body is pure.
  //------------------END LOOP-LAST

  // High-level construct kept for bitwidth's leftover-AttrSet cleanup pass.
  // Tuple-related ops (TupAdd, TupGet) and AttrGet were dropped along with
  // cprop's tuple_pass; CompileErr was dropped (no producer post-migration).
  AttrSet,

  // Appended to preserve existing serialized op numbers. Both inspect the
  // low `b` bits of operand `a`; b is a non-negative constant bit COUNT.
  // Extension of a follows its signed value, independently of output hints.
  // Rxor returns parity (0/1); Popcount returns the number of set bits (0..b).
  Rxor,
  Popcount,

  Last_invalid
};

// Band endpoints the range predicates below rely on. An op added INSIDE a band
// keeps these true; one appended past AttrSet only grows the tables.
static_assert(static_cast<uint8_t>(Ntype_op::Sum) == 1);
static_assert(Ntype_op::Clock_cell < Ntype_op::IO);
static_assert(Ntype_op::IO < Ntype_op::Memory);
static_assert(Ntype_op::Memory < Ntype_op::Sub);
static_assert(Ntype_op::Sub < Ntype_op::AttrSet);
static_assert(Ntype_op::AttrSet < Ntype_op::Last_invalid);

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
  // The language permits such a memory and the readers must not reject it
  // (parse it, keep it, let it regenerate) — but it is a weird
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
  // Dense: indexed by Ntype_op underlying value.
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
    a[static_cast<size_t>(Ntype_op::Rxor)]       = "rxor";
    a[static_cast<size_t>(Ntype_op::Popcount)]   = "popcount";
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
  inline static std::array<std::array<std::string, static_cast<std::size_t>(Ntype_op::Last_invalid)>, Memory_port_stride>
                                                                                   sink_pid2name;
  inline static std::array<bool, static_cast<std::size_t>(Ntype_op::Last_invalid)> ntype2single_input;
  inline static std::array<absl::flat_hash_map<std::string, hhds::Port_id>, static_cast<std::size_t>(Ntype_op::Last_invalid)>
      name2pid;

  static constexpr std::string_view get_sink_name_slow(Ntype_op op, hhds::Port_id pid);

public:
  // Band tests over the Ntype_op declaration order (see the enum comment).
  static inline constexpr bool is_loop_first(Ntype_op op) { return op == Ntype_op::IO; }
  static inline constexpr bool is_loop_last(Ntype_op op) { return op >= Ntype_op::Memory && op <= Ntype_op::Sub; }
  // A computed combinational cell: every op in [Sum .. Clock_cell]. Not the
  // boundary (IO), not state, not the AttrSet marker.
  static inline constexpr bool is_comb(Ntype_op op) {
    return (op >= Ntype_op::Sum && op <= Ntype_op::Clock_cell) || op == Ntype_op::Rxor || op == Ntype_op::Popcount;
  }

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

  // ===========================================================================
  // ONE DRIVER PER SINK PIN -- and the OPERAND BANK convention that follows.
  // ===========================================================================
  //
  // EVERY sink pin of EVERY cell takes exactly one driver. A commutative cell
  // that folds N operands under its identity op therefore spends N CONSECUTIVE
  // sink pids, one per operand, instead of piling N drivers onto one pin. The
  // operand's ROLE ("which bank it belongs to") rides on the PID, not on the
  // fan-in of a shared pin.
  //
  // THE CONVENTION, defined here and nowhere else -- pid PARITY selects the
  // bank, and the bank count is `sink_bank_count(op)`:
  //
  //   Sum / LT / GT              TWO banks, so bank = pid % 2:
  //                                EVEN pid -> the "as" bank (Sum: ADDED,
  //                                            LT/GT: left-hand operand)
  //                                ODD  pid -> the "bs" bank (Sum: SUBTRACTED,
  //                                            LT/GT: right-hand operand)
  //                              Operand k of "as" is pid 2k; operand k of
  //                              "bs" is pid 2k+1. The banks are independent:
  //                              a Sum with three adds and one subtract
  //                              occupies {0, 2, 4} and {1}, and pid 3 simply
  //                              does not exist. NOTHING requires the pids to
  //                              be dense; only sorted and bank-tagged.
  //
  //   Mult/And/Or/Xor/Ror/EQ     ONE bank ("as"), so bank = 0 for every pid:
  //                              operand k is pid k.
  //
  //   every other op             NOT banked. The pid IS the role, one operand
  //                              each (SHL/SRA b is the shift amount, Mux pid 0
  //                              is the selector, ...), and sink_bank() is the
  //                              identity.
  //
  // WHY parity rather than "adds below a split point": the split point would
  // have to be stored somewhere or recomputed, and every graph->graph copy that
  // preserves pids (legalize, flatten, inline_sub, occurrence materialize,
  // color_reduce) would have to preserve it too. Parity is carried by the pid
  // itself, so a copy that preserves pids preserves the bank for free.
  //
  // The NAME of a banked sink is its BANK's name ("as"/"bs") for every pid in
  // the bank -- see get_sink_name -- so `sink_pin_name(e.sink) == "bs"` keeps
  // meaning "this operand is subtracted" no matter which slot it landed in.
  // Conversely get_sink_pid("as"/"bs") answers the BANK pid (0/1), which is the
  // FIRST slot of the bank; use graph_util::setup_sink_by_name to append a new
  // operand and graph_util::inp_drivers_of to read a whole bank.

  // Number of operand banks a commutative cell folds into, or 0 when the op is
  // not banked at all.
  static inline constexpr int sink_bank_count(Ntype_op op) {
    switch (op) {
      case Ntype_op::Sum:
      case Ntype_op::LT:
      case Ntype_op::GT: return 2;  // as (even pid) / bs (odd pid)
      case Ntype_op::Mult:
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor:
      case Ntype_op::Ror:
      case Ntype_op::EQ: return 1;  // as
      default: return 0;
    }
  }
  static inline constexpr bool is_banked_sink_op(Ntype_op op) { return sink_bank_count(op) != 0; }

  // The operand bank a sink pid belongs to. For a banked op this is the pid of
  // the bank's FIRST slot (0 = "as", 1 = "bs"); for every other op it is the
  // pid itself, so a consumer can apply it unconditionally:
  //
  //   const auto pid = Ntype::sink_bank(op, e.sink.get_port_id());
  //   if (pid == 1) { ... }   // "the subtract side" / "the b operand"
  //
  static inline constexpr hhds::Port_id sink_bank(Ntype_op op, hhds::Port_id pid) {
    const int banks = sink_bank_count(op);
    if (banks == 0) {
      return pid;
    }
    if (banks == 1) {
      return 0;
    }
    return static_cast<hhds::Port_id>(pid & 1U);
  }

  // Kept as a TRUE-for-everything predicate: after the one-driver-per-sink-pin
  // change no sink pin anywhere takes more than one driver. It stays as a named
  // concept because call sites read better asking the question than asserting
  // the constant, and because `is_banked_sink_op` is what they usually want
  // instead (the banked ops are the ones whose operands are a MULTISET).
  static inline constexpr bool is_sink_single_driver(Ntype_op, hhds::Port_id) { return true; }

  // Returns the hhds::Port_id for a LiveHD sink name on the given op, or
  // hhds::Port_invalid when the name is not a valid sink for this op.
  // The per-op first-char table is the fast path; same-op sink names that
  // share a leading char (e.g. Flop posclk/pipe_min/pipe_max, all 'p')
  // resolve through that cell type's name2pid map — the
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
    if (pid != hhds::Port_invalid && sink_pid2name[pid][static_cast<std::size_t>(op)] == str) {
      return pid;
    }
    // Slow path: first-char miss or a same-first-char sibling pin.
    const auto& names = name2pid[static_cast<std::size_t>(op)];
    auto        it    = names.find(str);
    if (it != names.end()) {
      return it->second;
    }
    return hhds::Port_invalid;
  }

  static inline std::string get_sink_name(Ntype_op op, hhds::Port_id pid) {
    // A banked op names every slot of a bank after the BANK, so a consumer
    // asking "is this operand subtracted?" gets "bs" from pid 1, 3, 5, ...
    // (see the ONE DRIVER PER SINK PIN block above). The static name table is
    // untouched -- it still only holds the bank pids -- so the `n_sinks`
    // bookkeeping in cell.cpp and the get_sink_pid fast path keep their old,
    // one-name-per-pid shape.
    if (is_banked_sink_op(op)) {
      return sink_bank(op, pid) == 0 ? std::string{"as"} : std::string{"bs"};
    }
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

  // Range-checked: an lgdb written by a NEWER binary can carry an appended
  // op's raw value (see the forward-compat note above), and type_op_of hands
  // it through unclamped — out-of-range folds into "invalid" instead of
  // indexing past cell_name_sv. Entries are whole string literals, so the
  // returned view's .data() is NUL-terminated (op_name in cgen relies on it).
  static std::string_view get_name(Ntype_op op) {
    const auto idx = static_cast<size_t>(op);
    if (idx >= cell_name_sv.size()) {
      return "invalid";
    }
    return cell_name_sv[idx];
  }

  static Ntype_op get_op(std::string_view name) {
    const auto it = cell_name_map.find(name);
    if (it == cell_name_map.end()) {
      return Ntype_op::Invalid;
    }
    return it->second;
  }
};
