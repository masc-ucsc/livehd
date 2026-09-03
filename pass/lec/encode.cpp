// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "encode.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/source_locator.hpp"
#include "hlop/dlop.hpp"
#include "latch_contract.hpp"  // Design_clocks::name_looks_like_clock (the ICG clock-operand disambiguator)
#include "node_util.hpp"
#include "query.hpp"  // is_assume_kind -- one spelling table shared with the asserter

namespace livehd::lec {

using cvc5::Kind;
using cvc5::Sort;
using cvc5::Term;
namespace gu = livehd::graph_util;

// Keep structural-refusal diagnostics actionable on large generated designs.
// The numeric debug id is useful for one graph instance, but changes after a
// contextual inline.  A surviving signal name and source location let the
// caller map the same cycle back to the emitted RTL in either context.
static std::string diagnostic_node_name(hhds::Graph* g, const hhds::Occurrence_node& node) {
  std::string out  = gu::debug_name(node);
  auto        name = gu::node_name_of(node);
  if (!name.empty()) {
    out += "[name=" + std::string{name} + "]";
  }
  auto src = node.base_node().attr(hhds::attrs::srcid);
  if (src.has()) {
    auto span = g->source_locator().resolve_span(src.get());
    if (!span.file.empty() || span.start_line) {
      out += "@" + (span.file.empty() ? std::string{"?"} : span.file);
      if (span.start_line) {
        out += ":" + std::to_string(*span.start_line);
      }
    }
  }
  return out;
}

// Walk a clock_pin driver back to the graph INPUT it comes from, hopping only
// the width-mask Get_mask a typed port read picks up. Returns invalid for a
// DERIVED clock (a divider Q, an inverter, a mux, an ICG cone) -- which is the
// signal that this encoder cannot model the element's commit condition, since
// its only gating branches are the ICG fold and the multi-clock detected edge.
// File scope so the flop loop and the memory cut share ONE definition.
// Peel WIDTH-ONLY wrappers off a clock driver so the shape dispatch below sees
// the operator that actually derives the clock.
//
// Explicit casts and independently inferred hierarchy boundaries can leave a
// width-only Get_mask wrapper between a derived clock expression and its
// consumer. The wrapper changes representation, not the clock's SHAPE, so peel
// it before recognizing an ICG cone. Measured on tests/equiv/mclk_derived,
// which is the identical `<clock> & <enable>` cone the fold exists to handle.
//
// Same peel set as resolve_clk_input, and the same lowest-port-id rule for
// picking the value operand out of Get_mask/Set_mask/Sext -- these all take the
// value on the low pid and the mask/amount above it. Depth-capped.
template <typename Pin>
static Pin peel_clock_width(Pin p, int depth = 0) {
  while (!p.is_invalid() && depth < 16) {
    ++depth;
    const auto op = gu::type_op_of(p.get_master_node());
    // MASK-SHAPED And and ZERO-PAD Or are width adjustments, not gates. cgen
    // spells a narrowing as `v & MASK` and a signed/width-preserving pad as
    // `$signed(0) | $signed(v)`. Exactly ONE non-constant operand distinguishes
    // these identities from a real clock gate (`clk & enable`) or derived clock
    // logic. Constants must preserve bit 0: 1 for And, 0 for Or.
    if (op == Ntype_op::And || op == Ntype_op::Or) {
      Pin  data;
      int  data_ins = 0;
      bool identity = true;
      for (const auto& e : p.get_master_node().inp_edges()) {
        if (e.driver.is_const()) {
          if (op == Ntype_op::And) {
            identity &= !gu::const_of(e.driver).and_op(*Dlop::create_integer(1))->is_known_false();
          } else {
            identity &= gu::const_of(e.driver).is_known_zero();
          }
          continue;
        }
        ++data_ins;
        data = e.driver;
      }
      if (data_ins != 1 || !identity || data.is_invalid()) {
        break;
      }
      p = data;
      continue;
    }
    if (op != Ntype_op::Get_mask && op != Ntype_op::Sext) {
      break;
    }
    // Take the `a` operand (sink pid 0); the mask / bit count is pid 2 resp. 1
    // (graph/cell.cpp). Track the SINK pid explicitly: comparing against
    // `a.get_port_id()` would read the held pin's DRIVER pid, which is 0 for any
    // single-output node, so the test would be `sink_pid < 0` -- never true --
    // and whichever edge iterated first would win. When that is the constant the
    // walk lands on it and gives up, order-dependently.
    Pin           a;
    hhds::Port_id a_pid = Port_invalid;
    for (const auto& e : p.get_master_node().inp_edges()) {
      const auto spid = e.sink.get_port_id();
      if (a.is_invalid() || spid < a_pid) {
        a     = e.driver;
        a_pid = spid;
      }
    }
    if (a.is_invalid()) {
      break;
    }
    p = a;
  }
  return p;
}

template <typename Pin>
static Pin resolve_clk_input(Pin p) {
  // ONE definition of "width adjustment", shared with the shape dispatch: strip
  // every wrapper that cannot change bit 0 (the only bit an edge is detected
  // on), then ask whether what is left is a clock INPUT.
  //
  // This used to peel Get_mask only, and to compare `e.sink.get_port_id() <
  // a.get_port_id()` -- the candidate's SINK pid against the held pin's DRIVER
  // pid, two different numbering spaces. A driver pid is 0 for any
  // single-output node, so once any edge was taken the test read `sink_pid < 0`
  // and whichever edge iterated FIRST won; when that was `mask` the walk landed
  // on the constant and gave up. Both bugs hit the same design: an ICG whose
  // operands arrive through width wrappers resolved to NOTHING and the flop was
  // refused as an unmodellable derived clock. Measured on
  // tests/equiv/mclk_derived, whose golden spells the same cone at native width
  // and always passed -- the asymmetry only shows on a Verilog round trip,
  // where cgen's extra magnitude bit forces the extension in.
  const auto base = peel_clock_width(p);
  return gu::is_graph_input_pin(base) ? base : Pin{};
}

// 2f-latch M9 -- decode a clock_pin driver that is (or reaches, through the
// width-mask wrappers a typed port read picks up) a `Clock_cell`.
//
// Deliberately SEPARATE from resolve_clk_input rather than a hop inside it:
// that function's INVALID answer is the "this clock is derived, refuse" signal
// in four places, so teaching it to see through a gate would silently turn
// every gated element into "on the plain reference clock, ungated" -- the exact
// false PROVEN the guards exist to stop.
template <typename Pin>
struct Clock_cell_use {
  Pin  cell;     // the cell's output pin; invalid when there is none
  Pin  clk_ref;  // reference clock driver
  Pin  en;       // enable driver; invalid => identity (always commits)
  int  div    = 1;
  bool invert = false;
};

template <typename Pin>
static Clock_cell_use<Pin> clock_cell_on(Pin p) {
  Clock_cell_use<Pin> r;
  for (int hops = 0; hops < 8 && !p.is_invalid(); ++hops) {
    p = peel_clock_width(p);
    if (gu::is_graph_input_pin(p) || p.is_const()) {
      return r;
    }
    auto       n  = p.get_master_node();
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Clock_cell) {
      r.cell = p;
      for (const auto& e : n.inp_edges()) {
        switch (static_cast<int>(e.sink.get_port_id())) {
          case 2: r.clk_ref = e.driver; break;
          case 3:
            if (e.driver.is_const()) {
              const auto& dc = gu::const_of(e.driver);
              r.div          = dc.is_just_i64() ? static_cast<int>(dc.to_just_i64()) : 0;
            }
            break;
          case 4 : r.en = e.driver; break;
          case 6 : r.invert = e.driver.is_const() && !e.driver.is_known_false(); break;
          default: break;
        }
      }
      return r;
    }
    if (op != Ntype_op::Get_mask && op != Ntype_op::Set_mask && op != Ntype_op::Sext) {
      return r;
    }
    p = gu::first_value_driver(n);
  }
  return r;
}

// Guard-side classifier for a Memory's clock_pin driver: TRUE iff nothing in
// the clock's shape contradicts the "every write lands once per step" model.
// Acceptable lanes: unconnected, a constant (a tied-off port), a graph clock
// input (via resolve_clk_input), or an opaque instance (a mapped clock-buffer
// cell -- hhds resolves a Sub body only through the graph's OWN library and a
// netlist holds IO-only cell decls, so a cell Sub never flattens here). The
// Set_mask recursion is the load-bearing part: the abc read-back rebuilds a
// signed 1-bit clock_pin as `set_mask(const, mapped-driver)` and chains one
// per port (abc_map.cpp pass 3b), so the buffer Sub the exemption above was
// written for sits BEHIND a Set_mask and the bare master-node test never sees
// it -- that regression refused every ABC-mapped memory under `lhd lec`.
// Ordinary combinational logic in ANY lane (an And, a mux, a divider's Q)
// still returns false, which is the guard's whole point. Depth-capped
// fail-closed: a pathological chain refuses rather than recursing away.
template <typename Pin>
static bool memory_clock_shape_ok(Pin p, int depth = 0) {
  if (depth > 64) {
    return false;
  }
  if (p.is_invalid() || p.is_const()) {
    return true;
  }
  if (!resolve_clk_input(p).is_invalid()) {
    return true;
  }
  auto       n  = p.get_master_node();
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Sub) {
    // A clock BUFFER/inverter is one cell input (the clock) and is transparent
    // to "every write lands once per step". A clock GATE is not: it is the same
    // opaque Sub with an EXTRA enable input, and accepting it models a gated
    // memory as if it were written unconditionally -- a false PROVEN against a
    // reference that writes every cycle. That distinction is the only thing
    // separating them here (the cell body never flattens: hhds resolves a Sub
    // only through the graph's OWN library and a netlist holds IO-only cell
    // decls), so count the non-constant inputs and refuse anything with a
    // second one. Constants are tie-offs, not enables.
    int data_ins = 0;
    for (const auto& e : n.inp_edges()) {
      if (!e.driver.is_const()) {
        ++data_ins;
      }
    }
    return data_ins <= 1;
  }
  // A WIDTH MASK on the clock, not a clock gate. cgen emits every net read as
  // `net & MASK`, so routing a design out to Verilog and back turns a plain
  // `.clk(clock)` port binding into `And(clock, 1'h1)` -- and the memory inside
  // ware/rtl/cgen_memory_*.v (the graph Memory cell's own Verilog body, which
  // every emitted design instantiates) is reached through exactly that edge. A
  // non-zero constant ANDed with a 1-bit clock is the identity, so the shape
  // stays legal; recurse into the single data operand.
  //
  // TWO data operands is a real clock GATE (`clk & enable`) and is still
  // refused here, for the same reason the Sub arm refuses a two-input cell:
  // that shape belongs to the clock-cell path, which turns it into a write
  // ENABLE, and silently accepting it here would model a gated memory as if it
  // were written every cycle.
  if (op == Ntype_op::And) {
    Pin data;
    int data_ins = 0;
    for (const auto& e : n.inp_edges()) {
      if (e.driver.is_const()) {
        if (gu::const_of(e.driver).is_known_false()) {
          return false;  // `clk & 0` is a constant, not a clock
        }
        continue;
      }
      ++data_ins;
      data = e.driver;
    }
    if (data_ins != 1) {
      return false;
    }
    return memory_clock_shape_ok(data, depth + 1);
  }
  if (op != Ntype_op::Set_mask) {
    return false;
  }
  for (const auto& e : n.inp_edges()) {
    if (!memory_clock_shape_ok(e.driver, depth + 1)) {
      return false;
    }
  }
  return true;
}

// Stable 1:1 cut-point key for a state cell (Flop), used to put corresponding
// registers of the two designs in correspondence. The REGISTER NAME is the
// primary key: both front-ends preserve the RTL name (yosys-slang on the pin,
// native slang now stamps it in tolg), whereas they anchor the srcid to
// different source constructs (native -> the declaration, yosys -> the
// always_ff assignment) so the source span does NOT match across readers. The
// span is the fallback for an unnamed flop (a pass-inserted pipeline stage),
// then the node id (an unmatchable per-design fallback -> a sound Unknown).
//
// The name is normalized: a leading "$...$" yosys decoration (e.g.
// "$driver$cnt_q") and a trailing "___ssa_N" SSA suffix are stripped so the two
// readers' spellings of the same register collapse to one key.
static std::string normalize_reg_name(std::string_view raw) {
  std::string_view s = raw;
  if (!s.empty() && s.front() == '$') {
    if (auto e = s.find('$', 1); e != std::string_view::npos) {
      s.remove_prefix(e + 1);
    }
  }
  if (auto p = s.find("___ssa_"); p != std::string_view::npos) {
    s = s.substr(0, p);
  }
  std::string out{s};
  out.erase(std::remove(out.begin(), out.end(), '`'), out.end());
  return out;
}

std::string canon_flop_name(std::string_view hier_name) {
  std::string_view sv = hier_name;
  // A leading yosys "$decoration$" (e.g. "$driver$ex_mem_ex_result", left by the
  // yosys-slang reader when it flattens an instance hierarchy) and a trailing
  // "___ssa_N" SSA suffix are reader noise — strip them so the flattened name
  // matches the native flat spelling. (normalize_reg_name does the same for the
  // flop_state_key path; this is the get_hier_name path.)
  if (!sv.empty() && sv.front() == '$') {
    if (auto e = sv.find('$', 1); e != std::string_view::npos) {
      sv.remove_prefix(e + 1);
    }
  }
  if (auto p = sv.find("___ssa_"); p != std::string_view::npos) {
    sv = sv.substr(0, p);
  }
  std::string s(sv);
  // Pyrope backticks and Verilog's leading backslash quote identifiers but are
  // not part of their RTL names. After an instance prefix is attached, the
  // latter can appear in the middle of a hierarchical name
  // (`csrMod\_Mhpmevent10_0` versus `csrMod_Mhpmevent10_0`).
  s.erase(std::remove(s.begin(), s.end(), '`'), s.end());
  s.erase(std::remove(s.begin(), s.end(), '\\'), s.end());
  // ".reg_" (the CIRCT single-field stage-register flop name) -> "_" first, so the
  // collapse survives the generic "." -> "_" flatten that follows. (A register file
  // bank "registers.regs_7" has ".regs_", not ".reg_", so it is left for the dot
  // flatten -> "registers_regs_7", which the bank detector then splits on "_<idx>".)
  for (std::string::size_type p = s.find(".reg_"); p != std::string::npos; p = s.find(".reg_", p)) {
    s.replace(p, 5, "_");
  }
  std::replace(s.begin(), s.end(), '.', '_');
  return s;
}

std::string flop_state_key(const hhds::Graph& g, const hhds::Node_class& node) {
  auto pn = gu::pin_name_of(node.get_driver_pin(0));
  if (pn.empty()) {
    pn = gu::node_name_of(node);
  }
  if (auto nm = normalize_reg_name(pn); !nm.empty()) {
    return std::string("\x01n:") + nm;
  }
  auto ref = node.attr(hhds::attrs::srcid);
  if (ref.has()) {
    auto span = g.source_locator().resolve_span(ref.get());
    if (!span.file.empty()) {
      return std::format("\x01s:{}-{}@{}", span.start_byte.value_or(0), span.end_byte.value_or(0), span.file);
    }
  }
  return std::string("\x01f:") + std::to_string(static_cast<uint64_t>(node.get_debug_nid()));
}

namespace {

// Bit-vector constant that always fits the requested width: cvc5 requires the
// value to be < 2^width, so mask the low `width` bits (this also yields the
// correct two's-complement encoding for negative i64 inputs).
Term bv_const(cvc5::TermManager& tm, int width, uint64_t val) {
  if (width < 64) {
    val &= (uint64_t{1} << width) - 1;
  }
  return tm.mkBitVector(static_cast<uint32_t>(width), val);
}

// Indexed bit-vector slice a[hi:lo] (inclusive), via mkOp(BITVECTOR_EXTRACT).
Term bv_extract(cvc5::TermManager& tm, const Term& t, int hi, int lo) {
  auto op = tm.mkOp(Kind::BITVECTOR_EXTRACT, {static_cast<uint32_t>(hi), static_cast<uint32_t>(lo)});
  return tm.mkTerm(op, {t});
}

// Selector test for ONE arm of a Mux/Hotmux. Mux compares the selector against
// the arm index; Hotmux tests that arm's one-hot bit.
//
// The hot bit is tested with EXTRACT rather than by comparing against a `1 << k`
// constant. That constant is undefined behaviour for k >= 64 (the shift count is
// masked mod 64 on arm64/x86, so arm 64 aliases arm 0) and is unrepresentable
// through a uint64_t-valued bv_const in the first place. The result was that
// EVERY arm above 63 silently fell through to the default arm — which both
// false-REFUTES a correct design and, far worse, false-PROVES two designs that
// differ only above arm 63. A 128-entry `match` (a ROM/decode table) is enough
// to hit it. Returns a null Term if the arm index is past the selector width,
// so a malformed graph degrades to UNKNOWN instead of dropping the arm.
Term mux_arm_cond(cvc5::TermManager& tm, bool hotmux, const Term& sel, int sel_width, int k) {
  if (!hotmux) {
    return tm.mkTerm(Kind::EQUAL, {sel, bv_const(tm, sel_width, static_cast<uint64_t>(k))});
  }
  if (k >= sel_width) {
    return Term();
  }
  return tm.mkTerm(Kind::EQUAL, {bv_extract(tm, sel, k, k), bv_const(tm, 1, 1)});
}

}  // namespace

// Extend / truncate a value to exactly `width` bits.
Term fit_to(cvc5::TermManager& tm, const Val& v, int width) {
  if (v.width == width) {
    return v.term;
  }
  if (width < v.width) {
    return bv_extract(tm, v.term, width - 1, 0);  // truncate low bits
  }
  uint32_t d  = static_cast<uint32_t>(width - v.width);
  auto     op = tm.mkOp(v.is_signed ? Kind::BITVECTOR_SIGN_EXTEND : Kind::BITVECTOR_ZERO_EXTEND, {d});
  return tm.mkTerm(op, {v.term});
}

Sort Encoder::bv(int width) { return tm_.mkBitVectorSort(width < 1 ? 1 : width); }

Term Encoder::fit(const Val& v, int width) { return fit_to(tm_, v, width); }

Term fit_x_mask_to(cvc5::TermManager& tm, const Val& v, int width) {
  if (v.x_mask.isNull() || v.width == width) {
    return v.x_mask;
  }
  if (width < v.width) {
    return bv_extract(tm, v.x_mask, width - 1, 0);
  }
  uint32_t d  = static_cast<uint32_t>(width - v.width);
  // sign-extension replicates the top VALUE bit, so the copies are unknown iff
  // that bit is unknown (replicate the undef msb); zero-extension appends
  // known-0 bits (zero-extend the plane).
  auto     op = tm.mkOp(v.is_signed ? Kind::BITVECTOR_SIGN_EXTEND : Kind::BITVECTOR_ZERO_EXTEND, {d});
  return tm.mkTerm(op, {v.x_mask});
}

namespace {

// Exact X plane for a constant-mask Get_mask (bit EXTRACT), or a null Term when
// the shape is anything else (caller then falls back to the conservative
// whole-value smear).
//
// Unknowns are POSITIONAL, so the plane must be sliced exactly the way the value
// is: the window's own bits, extended above `a`'s stored width the way `fit`
// extends the value. Smearing instead marks every extracted bit unknown, which
// makes an output assembled out of such slices compare NOTHING -- a vacuous
// PROVEN. This is `//pass/lec:lec_combarray_test`'s corrupted chunk: the golden's
// `'0` fill leaves the packed bus partially X, so slicing it used to hand the
// miter a fully don't-care 128-bit `dout` even though the differing bits are in
// a fully known window.
Term exact_get_mask_x_plane(cvc5::TermManager& tm, const hhds::Occurrence_node& node, Ntype_op op, const std::vector<Val>& a_in,
                            int width) {
  if (op != Ntype_op::Get_mask || a_in.empty()) {
    return Term();
  }
  const Val& a = a_in.front();
  if (a.x_mask.isNull()) {
    return Term();  // `a` is fully known; the plane can only come from the mask pin
  }
  auto mask_pin = gu::get_driver_of_sink_name(node, "mask");
  if (mask_pin.is_invalid() || !mask_pin.is_const()) {
    return Term();
  }
  Dlop mask = gu::const_of(mask_pin);
  if (mask.has_unknowns()) {
    return Term();  // an unknown mask selects unknown POSITIONS: smear
  }
  if (mask.is_just_i64() && mask.to_just_i64() == -1) {
    // Unsigned cast: the value is re-read as unsigned, so the plane extends with
    // known-zero bits exactly like the value extends with zeros.
    return fit_x_mask_to(tm, Val{a.term, a.width, /*is_signed=*/false, a.x_mask}, width);
  }
  auto      range = mask.get_mask_range();  // [begin, end)
  const int rb = range.first, re = range.second;
  if (rb < 0 || re <= rb) {
    return Term();  // non-contiguous: the value path refuses too
  }
  Term axw = re > a.width ? fit_x_mask_to(tm, a, re) : a.x_mask;
  if (axw.isNull()) {
    return Term();
  }
  Term slice = bv_extract(tm, axw, re - 1, rb);
  return fit_x_mask_to(tm, Val{slice, re - rb, /*is_signed=*/false, slice}, width);
}

// Exact X plane for a constant-position Sext. The kept window carries `a`'s own
// plane; the replicated copies above it are unknown exactly when the kept sign
// bit is, which is what fit_x_mask_to's signed branch already computes.
Term exact_sext_x_plane(cvc5::TermManager& tm, const hhds::Occurrence_node& node, Ntype_op op, const std::vector<Val>& a_in,
                        int width) {
  if (op != Ntype_op::Sext || a_in.empty()) {
    return Term();
  }
  const Val& a = a_in.front();
  if (a.x_mask.isNull()) {
    return Term();
  }
  auto pos_pin = gu::get_driver_of_sink_name(node, "b");
  if (pos_pin.is_invalid() || !pos_pin.is_const()) {
    return Term();
  }
  Dlop posc = gu::const_of(pos_pin);
  if (posc.has_unknowns() || !posc.is_just_i64()) {
    return Term();
  }
  const int pos = static_cast<int>(posc.to_just_i64());
  if (pos < 1) {
    return Term();
  }
  Term axw = (pos > a.width) ? fit_x_mask_to(tm, a, pos) : a.x_mask;
  if (axw.isNull()) {
    return Term();
  }
  Term low = (pos == a.width) ? axw : bv_extract(tm, axw, pos - 1, 0);
  return fit_x_mask_to(tm, Val{low, pos, /*is_signed=*/true, low}, width);
}

}  // namespace

std::string Encoder::frame_tag(std::string_view prefix) {
  // "r3_" / "i3_" -> "3_" (same frame, opposite sides); "" -> "" (inductive).
  if (!prefix.empty() && (prefix.front() == 'r' || prefix.front() == 'i')) {
    prefix.remove_prefix(1);
  }
  return std::string(prefix);
}

std::string Encoder::flop_key(std::string_view hier) const {
  std::string c = canon_flop_name(hier);
  if (name_alias_ != nullptr) {
    if (auto it = name_alias_->find(c); it != name_alias_->end()) {
      return it->second;
    }
  }
  return c;
}

// Concrete reset/initial value of a flop's `initial` pin (the reset value), as a
// `width`-bit BV. Returns nullopt for a reset-less flop (no constant initial) —
// its power-on value is arbitrary, so the BMC caller seeds a fresh shared symbol
// instead. Unknown bits in the initial are masked to 0 in the value plane.
// Under `x_as_undefined` (formal.lec.gold_x=ignore), ONLY those literal unknown
// bits are marked in the undef plane. The known bits remain reset constraints:
// treating one partially-unknown word as wholly unconstrained can mask a real
// mismatch on its known lanes (notably a packed source flop against synthesized
// bit flops).
template <typename Node>
std::optional<Val> flop_initial_impl(cvc5::TermManager& tm, const Node& node, int width, bool x_as_undefined) {
  auto init_d = gu::get_driver_of_sink_name(node, "initial");
  if (init_d.is_invalid() || !init_d.is_const()) {
    return std::nullopt;
  }
  Dlop c = gu::const_of(init_d);
  if (c.is_just_i64()) {
    return Val{bv_const(tm, width, static_cast<uint64_t>(c.to_just_i64())), width, c.is_negative()};
  }
  auto        bin = c.to_binary();
  std::string ubin;
  ubin.reserve(bin.size());
  bool any_unknown = false;
  for (auto& ch : bin) {
    if (ch != '0' && ch != '1') {
      ch           = '0';
      any_unknown  = true;
      ubin        += '1';
    } else {
      ubin += '0';
    }
  }
  if (bin.empty()) {
    bin  = "0";
    ubin = "0";
  }
  Val v{tm.mkBitVector(static_cast<uint32_t>(bin.size()), bin, 2), static_cast<int>(bin.size()), c.is_negative()};
  if (x_as_undefined && any_unknown) {
    v.x_mask = tm.mkBitVector(static_cast<uint32_t>(ubin.size()), ubin, 2);
  }
  Val out{fit_to(tm, v, width), width, c.is_negative()};
  out.x_mask = fit_x_mask_to(tm, v, width);
  return out;
}

std::optional<Val> flop_initial(cvc5::TermManager& tm, const hhds::Node_class& node, int width, bool x_as_undefined) {
  return flop_initial_impl(tm, node, width, x_as_undefined);
}

std::optional<Val> flop_initial(cvc5::TermManager& tm, const hhds::Occurrence_node& node, int width, bool x_as_undefined) {
  return flop_initial_impl(tm, node, width, x_as_undefined);
}

// Pipeline depth of a flop: the comptime `pipe_min` pin makes one Flop cell
// model a depth-d shift register (cgen expands it to d chained flops, see
// inou/cgen/cgen_verilog.cpp). Unset/<=1 => the plain single flop. The encoder
// must honor it or it UNDER-counts the latency — reading a `stage[3]`/`@[3]`
// pipeline flop as a single 1-cycle delay, which false-REFUTEs the v->prp miter
// (the emitted side expands the stages into d real flops).
static int flop_depth(const hhds::Occurrence_node& node) {
  auto pm = gu::get_driver_of_sink_name(node, "pipe_min");
  if (pm.is_const()) {
    auto d = gu::const_of(pm).to_just_i64();
    if (d > 1) {
      return static_cast<int>(d);
    }
  }
  return 1;
}

// Index width to address `size` entries: clog2(size), at least 1.
static int mem_addr_width(int size) {
  int w = 1;
  while ((1 << w) < size) {
    ++w;
  }
  return w;
}

// Decode the reader-invariant signature (size/bits/port-roles) of a Memory cell
// from its config pins. Mirrors inou/cgen's port decode (pid -> port*12+field).
Mem_sig read_mem_sig(const hhds::Node_class& node) {
  Mem_sig sig;
  for (auto e : node.inp_edges()) {
    auto raw_pid  = static_cast<int>(e.sink.get_port_id());
    auto pin_name = Ntype::get_sink_name(Ntype_op::Memory, raw_pid);
    if (pin_name == "bits") {
      if (e.driver.is_const()) {
        sig.bits = static_cast<int>(gu::const_of(e.driver).to_just_i64());
      }
    } else if (pin_name == "size") {
      if (e.driver.is_const()) {
        sig.size = static_cast<int>(gu::const_of(e.driver).to_just_i64());
      }
    } else if (std::string_view(pin_name).find("rdport") != std::string_view::npos) {
      if (e.driver.is_const()) {
        if (gu::const_of(e.driver).is_known_false()) {
          ++sig.n_wr;
        } else {
          ++sig.n_rd;
        }
      }
    }
  }
  sig.addr_w = mem_addr_width(sig.size > 0 ? sig.size : 2);
  return sig;
}

// Stable cross-front-end correspondence key for a Memory cut. The signature is
// reader-invariant (same RTL array -> same size/bits/ports); `occ` (running
// count of prior same-signature memories in body().nodes(hhds::Node_order::forward) order) disambiguates
// multiple identical memories. Both designs enumerate in the same RTL order, so
// corresponding memories collapse to one shared array symbol. See M4 in lec.md.
// NOTE: the key is the size×bits SHAPE + occurrence ONLY — deliberately NOT the
// read/write PORT COUNTS. The shared symbol is the memory's INITIAL CONTENTS, which
// depend only on the array shape, not on how many ports access it. firtool unrolls a
// dynamic write `regs[wr]<=d` into ~N const-address write ports, so the same RTL array
// can present a very different port count across front-ends (e.g. r4w65 vs r4w2); keying
// the init cut on port counts wrongly prevented those corresponding memories from sharing
// one initial array (a false-refute on any uninitialized-read). Matching by shape+order
// (both designs enumerate memories in the same RTL order) is the same premise already used
// for flop-state correspondence; the per-design read/write topology is still honored when
// building each side's next-state relation.
std::string mem_state_key(const Mem_sig& sig, int occ) { return std::format("\x01m:{}x{}#{}", sig.size, sig.bits, occ); }

// Structural node identity within one design (see encode.hpp). Must stay in
// lock-step with the encoder's pinkey convention (INVALID -> ROOT), so the
// query-side box-correspondence builder and the encoder name the same instance
// identically no matter which walk found it.
std::string box_node_key(const hhds::Node_class& n) {
  auto hp = n.get_hier_pos();
  if (hp == hhds::INVALID) {
    hp = hhds::ROOT;
  }
  return std::to_string(static_cast<uint64_t>(n.get_current_gid())) + ":" + std::to_string(static_cast<int64_t>(hp)) + ":"
         + std::to_string(static_cast<uint64_t>(n.get_debug_nid()));
}

std::string box_node_key(const hhds::Occurrence_node& n) {
  const auto  index = n.get_occurrence_index();
  std::string key   = std::to_string(static_cast<uint64_t>(index.path.root_gid()));
  for (const auto& step : index.path.steps()) {
    key += "/" + std::to_string(static_cast<uint64_t>(step.subnode.gid)) + ":"
           + std::to_string(static_cast<uint64_t>(step.subnode.value)) + ":";
    key += step.ordinal ? std::to_string(*step.ordinal) : "-";
  }
  key += "#" + std::to_string(static_cast<uint64_t>(index.object.gid)) + ":"
         + std::to_string(static_cast<uint64_t>(index.object.value));
  return key;
}

Encoded Encoder::encode(hhds::Graph* g, const Io_name_map<Val>* shared_inputs, std::string_view prefix,
                        const Io_name_map<cvc5::Term>* shared_mems, const Io_name_map<Val>* shared_reads) {
  Encoded out;
  auto    gio = g->get_io();

  // driver-pin -> Val (SSA value table). Keyed by a HIER-stable string
  // "gid:hier_pos:pid" so that, under occurrences().nodes(hhds::Node_order::forward), a producer deep in an
  // instance and a consumer reading it across the boundary (inp_edges resolves
  // the real leaf driver, carrying its instance hier_pos) agree on one key.
  absl::flat_hash_map<std::string, Val> pin2val;
  auto                                  pinkey = [](const auto& p) -> std::string {
    using Pin = std::remove_cvref_t<decltype(p)>;
    if constexpr (std::same_as<Pin, hhds::Occurrence_pin>) {
      const auto  index = p.get_occurrence_index();
      std::string key   = std::to_string(static_cast<uint64_t>(index.path.root_gid()));
      for (const auto& step : index.path.steps()) {
        key += "/" + std::to_string(static_cast<uint64_t>(step.subnode.gid)) + ":"
               + std::to_string(static_cast<uint64_t>(step.subnode.value)) + ":";
        key += step.ordinal ? std::to_string(*step.ordinal) : "-";
      }
      key += "#" + std::to_string(static_cast<uint64_t>(index.object.gid)) + ":"
             + std::to_string(static_cast<uint64_t>(index.object.value));
      return key;
    } else {
      // A top-level pin reached via the class API (g->get_output_pin, a node's
      // driver pin outside a hier walk) carries hier_pos == INVALID, while the
      // same node reached via forward_hier carries ROOT. Both denote the top
      // frame, so canonicalize INVALID -> ROOT (a real sub-instance always has a
      // distinct child tree_pos, so this never aliases two different pins).
      auto hp = p.get_hier_pos();
      if (hp == hhds::INVALID) {
        hp = hhds::ROOT;
      }
      return std::to_string(static_cast<uint64_t>(p.get_current_gid())) + ":" + std::to_string(static_cast<int64_t>(hp)) + ":"
             + std::to_string(static_cast<uint64_t>(p.get_debug_pid()));
    }
  };
  Io_name_map<int> bbox_occ;  // LEGACY blackbox occurrence per def-name — only when no box_keys_ map is set
  // Two-phase blackbox bookkeeping (keyed by structural nodekey): the assigned
  // correspondence key once its OUTPUTS are emitted, and the seeded state symbol
  // for a state-aware box. Outputs emit on the first visit (they need only the
  // state / nothing); the input-dependent bbin / UF-tie + next-state are emitted
  // in a later pass.
  absl::flat_hash_map<std::string, std::string> bb_outkey;
  absl::flat_hash_map<std::string, Val>         bb_state_sym;

  auto fail = [&](const std::string& msg) -> Encoded& {
    if (out.ok) {
      out.ok    = false;
      out.error = msg;
    }
    return out;
  };
  // A structural REFUSAL: a cell or shape this encoder does not model. Distinct
  // from the budget-out `fail`s (see Encoded::unsupported) — a refusal decides
  // nothing and a bigger `formal.timeout` cannot change that, so the CLI turns
  // it into a hard error instead of an exit-0 "inconclusive" warning (2f-latch
  // M0: a latch design used to report status:pass, making every gate vacuous).
  auto fail_unsupported = [&](const std::string& msg) -> Encoded& {
    const bool first = out.ok;
    fail(msg);
    if (first) {
      out.unsupported = true;
    }
    return out;
  };

  // Budget-aware encode (2f-lec): a fresh top-level encode() call (sub_depth_==0)
  // arms a `budget_seconds_`-long deadline; a recursive Sub-flatten re-entry
  // (sub_depth_>0) inherits the parent's deadline so a slow subtree counts against
  // its parent, not a new clock. `over_budget()` is true once that deadline passes;
  // it's checked at the head of the combinational fixpoint and (throttled) inside
  // its node walk, plus once here so every recursive re-entry is covered. A
  // budget-out degrades to ok=false, which the query layer maps to a sound
  // Verdict::Unknown (never a wrong verdict).
  if (budget_seconds_ > 0 && sub_depth_ == 0) {
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(budget_seconds_);
  }
  auto over_budget = [&]() -> bool { return deadline_ && std::chrono::steady_clock::now() > *deadline_; };
  if (over_budget()) {
    return fail("encode budget exceeded (formal.timeout); raise --set formal.timeout to allow more encode time");
  }

  // bit-vector literal of `width` bits from a constant pin's Dlop.
  auto const_val = [&](const auto& dpin) -> Val {
    Dlop c     = gu::const_of(dpin);
    int  width = std::max(1, c.get_bits());
    bool sgn   = c.is_negative();
    Term t;
    if (c.is_just_i64()) {
      t = bv_const(tm_, width, static_cast<uint64_t>(c.to_just_i64()));
    } else {
      // Wide / partially-unknown constant: build from the MSB-first binary string
      // (get_bits() chars, no prefix). Unknown (X / don't-care) bits are masked
      // to 0 — consistent across two designs reading the same source constant.
      // Under x_dontcare (formal.lec.gold_x=ignore, REF side) the '?' positions ALSO
      // source the undef bit-plane, so the miter can exclude ref-unknown bits
      // from the compare instead of comparing an arbitrary concretization.
      auto        bin = c.to_binary();
      std::string ubin;
      ubin.reserve(bin.size());
      bool any_unknown = false;
      for (auto& ch : bin) {
        if (ch != '0' && ch != '1') {
          ch           = '0';
          any_unknown  = true;
          ubin        += '1';
        } else {
          ubin += '0';
        }
      }
      if (bin.empty()) {
        bin  = "0";
        ubin = "0";
      }
      width = static_cast<int>(bin.size());
      t     = tm_.mkBitVector(static_cast<uint32_t>(width), bin, 2);
      if (x_dontcare_ && any_unknown) {
        Val v{t, width, sgn};
        v.x_mask = tm_.mkBitVector(static_cast<uint32_t>(width), ubin, 2);
        return v;
      }
    }
    return Val{t, width, sgn};
  };

  // Resolve a driver pin to its Val (constant literal or a computed SSA value).
  // Under occurrences().nodes(hhds::Node_order::forward), inp_edges() resolves a sink across instance boundaries
  // to the real LEAF driver: a constant, a top primary input (a pin on the root
  // INPUT_NODE — resolved by port name from the seeded, cross-design-shared
  // inputs), or an ordinary producer node (looked up by its hier pinkey).
  std::string missing_driver_key;
  auto        driver_val = [&](const auto& dpin, bool& ok) -> Val {
    ok = true;
    if (dpin.is_const()) {
      return const_val(dpin);
    }
    if (gu::is_graph_input_pin(dpin)) {
      using Pin = std::remove_cvref_t<decltype(dpin)>;
      if constexpr (std::same_as<Pin, hhds::Occurrence_pin>) {
        // A native loop's domain index is a call binding, not a stored graph
        // edge. HHDS leaves the callee input boundary pin as the value-bearing
        // occurrence handle; recover the exact ordinal value from its final
        // path step. All ordinary/invariant inputs resolve through the parent
        // edge and therefore do not reach this branch with a non-empty path.
        const auto steps = dpin.path().steps();
        if (!steps.empty() && steps.back().ordinal) {
          auto  io     = dpin.get_graph()->get_io();
          auto* lib    = io ? io->get_library() : nullptr;
          auto  parent = lib ? lib->get_graph(steps.back().subnode.gid) : std::shared_ptr<hhds::Graph>{};
          if (parent) {
            auto site = parent->get_node(hhds::Class_index{steps.back().subnode.value});
            if (auto loop = site.subnode_loop(); loop && loop->index_input && dpin.get_port_id() == *loop->index_input) {
              const int64_t value = loop->index_at(*steps.back().ordinal);
              const int     width = std::max(1, gu::real_width(dpin));
              return Val{bv_const(tm_, width, static_cast<uint64_t>(value)), width, value < 0};
            }
          }
        }
      }
      auto it = out.inputs.find(std::string(dpin.get_pin_name()));
      if (it != out.inputs.end()) {
        return it->second;
      }
      ok = false;
      return {};
    }
    auto it = pin2val.find(pinkey(dpin));
    if (it == pin2val.end()) {
      missing_driver_key = pinkey(dpin);
      ok                 = false;
      return {};
    }
    return it->second;
  };

  // 1-bit BV from a Bool predicate.
  auto pred_to_bv = [&](const Term& b) -> Term { return tm_.mkTerm(Kind::ITE, {b, bv_const(tm_, 1, 1), bv_const(tm_, 1, 0)}); };

  // A Clock_cell deliberately has no ordinary DATA value at P==1: one encoder
  // step is an edge, not a sampled clock level, and making its output globally
  // readable would silently accept clock-monitor logic we cannot model.  There
  // is one narrower place where its Boolean waveform is required, however: a
  // PROVEN child collapsed as a sequence transducer still has a structural
  // CLOCK input, and the parent's proof must establish that the two child
  // instances receive the same clock sequence.  Treating that port like an
  // ordinary box input currently stalls forever because driver_val() correctly
  // finds no value for the cell (XiangShan ExuBlock -> CSR was the measured
  // refusal).
  //
  // Build the waveform only for that boundary obligation.  It is the actual
  // gate function -- ordinary `clk & held_en`, active-low `clk | !held_en` --
  // so a gated-vs-ungated child clock still REFUTES.  The value is returned
  // locally and is never installed in pin2val: any non-clock Sub port, or any
  // ordinary data consumer of the same net, keeps the fail-closed behavior.
  std::function<Val(const hhds::Occurrence_pin&, bool&, int)> clock_box_input;
  livehd::latch_contract::Clock_port_cache                    clock_port_cache;
  clock_box_input = [&](const hhds::Occurrence_pin& pin, bool& ok, int depth) -> Val {
    ok = false;
    if (pin.is_invalid() || depth > 16) {
      return {};
    }
    const auto cc = clock_cell_on(pin);
    if (cc.cell.is_invalid()) {
      return driver_val(pin, ok);
    }
    if (cc.div != 1 || cc.clk_ref.is_invalid()) {
      return {};
    }

    bool rok = true;
    Val  rv  = clock_box_input(cc.clk_ref, rok, depth + 1);
    if (!rok || rv.term.isNull()) {
      return {};
    }
    Term ref_hot = tm_.mkTerm(Kind::DISTINCT, {rv.term, bv_const(tm_, rv.width, 0)});

    Term en_hot = tm_.mkBoolean(true);
    if (!cc.en.is_invalid()) {
      bool eok = true;
      Val  ev  = driver_val(cc.en, eok);
      if (!eok || ev.term.isNull()) {
        return {};  // enable cone is not encoded yet; the fixpoint retries
      }
      en_hot = tm_.mkTerm(Kind::DISTINCT, {ev.term, bv_const(tm_, ev.width, 0)});
    }
    Term wave
        = cc.invert ? tm_.mkTerm(Kind::OR, {ref_hot, tm_.mkTerm(Kind::NOT, {en_hot})}) : tm_.mkTerm(Kind::AND, {ref_hot, en_hot});
    ok = true;
    return Val{pred_to_bv(wave), 1, false};
  };

  // ---- Inputs: shared symbol or a fresh one, mapped onto the input driver pin.
  for (const auto& d : gio->get_input_pin_decls()) {
    auto dpin = g->get_input_pin(d.name);
    int  w    = gu::real_width(dpin, *gio, d.name);
    if (w == 0) {
      // A width-less input is a scalar control signal — a clock (abstracted out
      // of the relational encoding via the flop-state cut) or a 1-bit reset /
      // enable. Both designs read the same RTL, so a missing bits attr is
      // consistent across sides; default to 1 rather than refusing to encode.
      // (A genuinely multi-bit input always carries a bits attr from tolg.)
      w = 1;
    }
    bool sgn = dpin.is_invalid() ? !gio->is_unsign(d.name) : !gu::is_unsign(dpin);
    Val  v;
    if (shared_inputs != nullptr) {
      auto it = shared_inputs->find(d.name);
      if (it != shared_inputs->end()) {
        v           = it->second;
        // The shared symbol's VALUE (bits) is shared across designs, but its SIGN
        // is THIS design's interpretation — the two front-ends can disagree on a
        // port's signedness (e.g. yosys-slang stamps a 32-bit reg `signed`, native
        // slang `unsigned`), and each must extend the bus by ITS OWN sign downstream
        // (a u64() cast of an unsigned source zero-extends; of a signed source
        // sign-extends). Adopt the local sign so the same value isn't sign-extended
        // on one side and zero-extended on the other.
        v.is_signed = sgn;
        // Reconcile a width disagreement by EXTENDING up to the wider view, never
        // truncating: the readers can undercount a bus width by a sign-bit slot
        // (the "bit-width trap"); truncating to the narrower side would drop the top
        // bit (e.g. d=0x80 -> 0). The shared symbol is built at the max width in
        // query.cpp, so this only ever extends (defensively).
        if (v.width < w) {
          v.term  = fit(v, w);
          v.width = w;
        }
      }
    }
    if (v.term.isNull()) {
      v = Val{tm_.mkConst(bv(w), std::string(prefix) + d.name), w, sgn};
    }
    out.inputs[d.name] = v;  // resolved by name in driver_val (graph-input pins)
  }

  // ---- M2 flop cut-points (register-correspondence SEC). Each Flop's Q (driver
  // pin 0) is a CURRENT-STATE symbol, shared across the two designs by its
  // preserved name (so a 1:1 latch map falls out of name equality). Seeded here,
  // before the combinational loop, so downstream comb reads resolve it like an
  // input; the matching NEXT-STATE value is emitted as a synthetic output after
  // the loop, and the miter then compares next-states alongside primary outputs.
  // occurrences().nodes(hhds::Node_order::forward) descends the whole instance tree, so flops at EVERY level
  // (e.g. a register inside a StageReg pipeline-register instance) are cut here.
  // The cross-design correspondence key is the Verilog-style hierarchical name
  // (get_hier_name = instance path + register name), matching query.cpp's shared
  // current-state symbols. (`flops` keeps the hier Node_class for the post-loop
  // next-state emission.)
  // Subnode Gids the hierarchical walks treat as OPAQUE: any Sub whose def name
  // is in the proven-module collapse set. forward_hier(&opaque) then yields them
  // as leaf Subs (not descended), so the blackbox path collapses them instead of
  // flattening — and their internal state is NOT cut here (the box models it).
  // Found via a HIERARCHICAL scan (a collapse def instantiated only NESTED
  // below a flattened parent would be missed by a top-level scan, and the walk
  // would then descend it while the Sub handler still boxes it — cutting its
  // internal flops on one side only); gids are name-hash stable, so one
  // instance pins the gid for every instance of the same def.
  // fast_hier (not forward_hier): a set build needs no topological order, and
  // this runs once per encode() -- i.e. once per BMC cycle -- so materializing
  // the whole flattened design here costs 56 B/node EVERY cycle. fast_hier is
  // O(depth). It also cannot honor an opaque scope, which is exactly right here:
  // this walk DISCOVERS the opaque set and must see every Sub (the scope is
  // installed below, once `opaque` is known).
  ankerl::unordered_dense::set<hhds::Gid> opaque_subs;
  if (collapse_defs_ != nullptr) {
    for (auto sn : g->grouped_hierarchy().nodes()) {
      if (gu::type_op_of(sn) != Ntype_op::Sub) {
        continue;
      }
      auto sio = sn.get_subnode_io();
      if (sio != nullptr && collapse_defs_->count(std::string(sio->get_name())) > 0) {
        opaque_subs.insert(sn.get_subnode_gid());
      }
    }
  }
  const ankerl::unordered_dense::set<hhds::Gid>* opaque = opaque_subs.empty() ? nullptr : &opaque_subs;
  std::vector<hhds::Occurrence_node>             flops;
  std::vector<int>                               flop_depths;     // pipe_min depth per flop (>=1)
  std::vector<std::vector<Val>>                  flop_internals;  // depth>1: the d-1 INTERNAL stage
                                                                  // current-states, din-side -> Q-side
  // Internal pipeline-stage current-state for a depth-d flop with output key K:
  // K (the Q stage, seeded below like any flop) plus d-1 hidden stages keyed
  // "K\x02p<i>". Shared/threaded by key exactly like Q (the BMC unroll re-seeds
  // them from each cycle's emitted next-state; the power-on cycle gets a fresh
  // arbitrary value, sound for a reset-less pipeline register).
  auto                                           seed_state = [&](const std::string& key, int w, bool sgn) -> Val {
    Val v;
    if (shared_inputs != nullptr) {
      if (auto it = shared_inputs->find(key); it != shared_inputs->end()) {
        v = it->second;
        // query.cpp pre-builds this shared symbol at the MIN width seen across
        // ref+impl (the value's width): a wider local flop (cgen's spare-sign-
        // bit reg) EXTENDS it here, so the headroom bits are pinned to the
        // value's extension instead of ranging free — a free headroom bit has
        // no counterpart on the narrow side and spuriously refutes any design
        // whose control reads the flop unmasked (`vld != 0`) before the first
        // real write (see tests/equiv/flop_init_headroom). Fit BEFORE adopting
        // the local sign: the extension must follow the SHARED symbol's own
        // (narrow, value-semantics) signedness, not this side's container
        // signedness. Always fit to the LOCAL w so every term feeding this
        // design's next-state ITE (din/self/source) agrees on width — a stale
        // "only ever extends" check left v wider than w on that side,
        // producing a width-mismatched ITE that crashes the cvc5 encode (the
        // worker then dies and the auto-portfolio swallows it as INCONCLUSIVE).
        if (v.width != w) {
          v.x_mask = fit_x_mask_to(tm_, v, w);  // BEFORE the value: needs v's old width
          v.term   = fit(v, w);
          v.width  = w;
        }
        v.is_signed = sgn;
      }
    }
    if (v.term.isNull()) {
      v = Val{tm_.mkConst(bv(w), std::string(prefix) + key), w, sgn};
    }
    return v;
  };
  // A LATCH is a state endpoint exactly like a flop once a phase schedule
  // exists: the schedule says WHICH microstep it closes in, and tolg already
  // baked `din = gate ? d : q`, so the transition is an ordinary
  // flop-with-enable. Without a schedule it stays REFUSED below (there is no
  // sub-period resolution to close it in, and encoding it as committing every
  // step is the false-PROVEN class this whole task exists to remove).
  const bool phased               = phase_plan_ != nullptr;
  // An ALWAYS-TRANSPARENT latch is not state: its window never closes, so it is
  // a wire. Encoding it as a flop-with-enable would add a full period of delay
  // the hardware does not have (cgen already emits it as `always_comb` with a
  // blocking assign, so the emitted Verilog and the encoding would disagree).
  auto       is_transparent_latch = [&](const hhds::Occurrence_node& n) {
    if (!phased || gu::type_op_of(n) != Ntype_op::Latch) {
      return false;
    }
    auto it = phase_plan_->ep.find(box_node_key(n));
    return it != phase_plan_->ep.end() && it->second.transparent;
  };
  // Combinational AND of a guard's canonicalized enable cones, for the P == 1
  // path (no sub-period resolution: one step IS the reference edge).
  auto guard_term = [&](const std::string& gkey, bool& gok) -> Term {
    Term acc;
    auto it = phase_plan_->guard_cones.find(gkey);
    if (it == phase_plan_->guard_cones.end()) {
      gok = false;
      return acc;
    }
    for (const auto& cone : it->second) {
      bool ok2 = true;
      Val  gv  = driver_val(cone, ok2);
      if (!ok2 || gv.term.isNull()) {
        gok = false;
        return Term{};
      }
      Term hot = tm_.mkTerm(Kind::DISTINCT, {gv.term, bv_const(tm_, gv.width, 0)});
      acc      = acc.isNull() ? hot : tm_.mkTerm(Kind::AND, {acc, hot});
    }
    if (acc.isNull()) {
      gok = false;
    }
    return acc;
  };
  for (auto node : g->occurrences(opaque).nodes(hhds::Node_order::forward)) {
    auto op = gu::type_op_of(node);
    if (op != Ntype_op::Flop && !(phased && op == Ntype_op::Latch)) {
      continue;
    }
    if (is_transparent_latch(node)) {
      continue;  // a wire, not a state cut -- see is_transparent_latch
    }
    auto qpin = node.get_driver_pin(0);
    if (qpin.is_invalid()) {
      continue;
    }
    int w = gu::real_width(qpin);
    if (w == 0) {
      w = 1;
    }
    bool        sgn        = !gu::is_unsign(qpin);
    std::string nm         = flop_key(node.get_hier_name());
    Val         v          = seed_state(nm, w, sgn);
    bool        was_shared = shared_inputs != nullptr && shared_inputs->find(nm) != shared_inputs->end();
    if (const char* dump_enc = std::getenv("LEC_DUMP_ENC");
        dump_enc != nullptr && (dump_enc[0] == '\0' || nm.find(dump_enc) != std::string::npos)) {
      std::fprintf(stderr,
                   "[LEC_ENC pfx=%s] flop hier='%s' key='%s' w=%d sgn=%d shared=%d xmask=%d term=%s\n",
                   std::string(prefix).c_str(),
                   node.get_hier_name().c_str(),
                   nm.c_str(),
                   w,
                   sgn ? 1 : 0,
                   was_shared ? 1 : 0,
                   v.x_mask.isNull() ? 0 : 1,
                   v.term.toString().c_str());
    }
    pin2val[pinkey(qpin)] = v;
    out.inputs[nm]        = v;
    flops.push_back(node);

    int depth = flop_depth(node);
    flop_depths.push_back(depth);
    std::vector<Val> internals;  // [s0 .. s(d-2)], din-side -> Q-side
    internals.reserve(depth > 1 ? static_cast<size_t>(depth - 1) : 0);
    for (int s = 0; s + 1 < depth; ++s) {
      internals.push_back(seed_state(nm + "\x02p" + std::to_string(s), w, sgn));
    }
    flop_internals.push_back(std::move(internals));
  }

  // ---- M4 memory cut, phase 1: decode each Memory and SEED its read douts with
  // fresh symbols (like a flop Q) so the combinational loop can consume them.
  // The actual dout = select(array, addr) and the write next-state are emitted in
  // phase 2 (after the loop), once addr/din/enable have been computed; an
  // equality ties the fresh dout to its real value. This mirrors the BMC
  // fresh-var deferral and is sound for both async and registered reads.
  auto ends_with = [](std::string_view s, std::string_view suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
  };
  // formal.ignore_memory: does the user's list name THIS memory? Matched three
  // ways because the two front-ends spell a memory differently and the user
  // types whatever the diagnostic printed: the full hier name, its canonical
  // form (canon_flop_name strips the per-design instance prefix, so one entry
  // covers both sides), or the bare leaf name after the last '.'.
  auto mem_ignored = [&](const hhds::Occurrence_node& n) -> bool {
    if (ignore_memory_ == nullptr || ignore_memory_->empty()) {
      return false;
    }
    const std::string hier{n.get_hier_name()};
    const std::string canon = canon_flop_name(hier);
    const auto        leaf  = [](std::string_view v) {
      auto d = v.rfind('.');
      return std::string(d == std::string_view::npos ? v : v.substr(d + 1));
    };
    const std::string hleaf = leaf(hier);
    const std::string dbg   = gu::debug_name(n);  // the spelling the diagnostics print
    for (const auto& want : *ignore_memory_) {
      if (want == hier || want == canon || want == hleaf || want == dbg || canon_flop_name(want) == canon || leaf(want) == hleaf
          || leaf(want) == leaf(dbg)) {
        return true;
      }
    }
    return false;
  };
  struct MPort {
    bool                 rd = false;
    hhds::Occurrence_pin addr, din, en;
  };
  struct MemCut {
    hhds::Occurrence_node             node;
    std::string                       key;
    Mem_sig                           sig;
    std::vector<MPort>                ports;
    int                               wensize = 0;
    // Per-(read,write) forwarding matrix (graph/cell.cpp): bit r*n_wr + w.
    // Held as the const itself — Dlop::bit_test is arbitrary precision, and
    // n_rd*n_wr overflows an int on wide (many-port) shapes.
    spool_ptr<Dlop>                   fwd;
    // Per-(read,write) UNDEFINED matrix (graph/cell.cpp pid 15), same layout as
    // `fwd`: ordering="none" says a colliding read has no defined value. On the
    // REFERENCE side that becomes an X bit-plane on the read dout, so the miter
    // excludes the collision window from the compare (the memory analogue of a
    // '?' constant under formal.lec.gold_x=ignore). The VALUE is untouched.
    spool_ptr<Dlop>                   undef;
    int                               mtype  = -1;
    bool                              is_rom = false;
    cvc5::Term                        a_cur;
    // a_cur came from `shared_mems` (the SAME symbol on both designs) rather
    // than being minted per-design. The whole-array bulk-update record is only
    // meaningful then: `a_next = cond ? from_bus(bus) : a_cur` proves nothing
    // about the hold arm when each side holds a DIFFERENT free array.
    bool                              a_cur_shared = false;
    std::vector<Term>                 rd_fresh;  // dout symbol per read port (port order): a
                                                 // fresh within-cycle var (async/comb, tied via
                                                 // equalities) OR a seeded current-state symbol
                                                 // (type==1 sync, threaded via next_read).
    std::vector<hhds::Occurrence_pin> rd_addr;
    std::vector<std::string>          rd_key;    // "<key>:rd<N>" per read port (sync threading)
    std::vector<Term>                 rd_xmask;  // per read port: deferred X bit-plane symbol (null = fully known)
    std::vector<Term>                 rd_xcur;   // sync (type==1) only: the plane threaded IN from last cycle (null = none)
    // Whole-array support (the `update` bus is driven): one update/read_all bus
    // instead of N per-entry ports. `is_comb` (type==2, no clock) => no persistent
    // state (no next_mem); reads/read_all reflect the post-update array.
    hhds::Occurrence_pin              update, update_enable, reset, init;
    bool                              is_whole = false;
    bool                              is_comb  = false;
    hhds::Occurrence_pin              ra_pin;    // async read_all driver pin (size*bits)
    cvc5::Term                        ra_fresh;  // deferred read_all symbol (tied in phase 2)
    // 2f-latch M9: enable of the Clock_cell gating this memory's clock, or
    // invalid when it is on a plain (or identity-celled) clock. Recorded as a
    // PIN in phase 1 and resolved to a term in phase 2 -- the combinational
    // fixpoint has not run yet when the clock scan happens.
    hhds::Occurrence_pin              commit_en;
    // formal.ignore_memory: the user EXCLUDED this memory from the comparison.
    // It is blackboxed -- every read dout becomes ONE SHARED free symbol per
    // (memory, port, cycle) across ref and impl, and no next-state array is
    // built -- so the proof says nothing about what it stores. A disclosed
    // ASSUMPTION, exactly like formal.lec.trust on a def.
    bool                              ignored = false;
  };
  std::vector<MemCut> mem_cuts;
  Io_name_map<int>    mem_occ;  // per-signature occurrence -> stable key
  for (auto node : g->occurrences(opaque).nodes(hhds::Node_order::forward)) {
    if (gu::type_op_of(node) != Ntype_op::Memory || !node.has_out_edges()) {
      continue;
    }
    MemCut mc;
    mc.node = node;
    mc.sig  = read_mem_sig(node);
    if (mc.sig.bits <= 0 || mc.sig.size <= 0) {
      return fail("memory '" + gu::debug_name(node) + "' missing bits/size");
    }
    std::string sg = std::to_string(mc.sig.size) + "x" + std::to_string(mc.sig.bits);  // shape only; occ matches by RTL order
    mc.key         = mem_state_key(mc.sig, mem_occ[sg]++);
    mc.ignored     = mem_ignored(node);
    // ---- FAIL CLOSED on a memory clocked by anything but the reference clock.
    // This encoder DISCARDS a Memory's clock_pin and posclk entirely: every
    // write is modelled as landing once per step. That is right for a memory on
    // the single reference clock and silently WRONG otherwise -- measured, a
    // memory written on `negedge clk` came back PROVEN equal to the same memory
    // written on `posedge clk`, and a gated-clock write PROVEN equal to an
    // ungated one. (pass.single_edge does not cover this either: its trigger
    // scans Flop/Fflop/Latch, so a negedge MEMORY does not even fire it.)
    for (auto e : node.inp_edges()) {
      const auto  raw = static_cast<int>(e.sink.get_port_id());
      std::string pn  = Ntype::get_sink_name(Ntype_op::Memory, raw);
      if (pn == "posclk") {
        if (e.driver.is_known_false()) {
          return fail_unsupported("memory '" + gu::debug_name(node)
                                  + "' is written on the FALLING clock edge, which this encoder does not model "
                                    "(it treats every memory write as landing once per step)");
        }
        // PER-PORT clock edges (Ntype::Memory_posclk_mixed): the source memory's
        // ports do not all commit on the same edge, so the reader could not
        // represent it and said so. A latch may mix phases -- that is what the
        // formal phase schedule is for -- a memory may not; the encoder has ONE
        // commit point per memory. This is a FORMAL refusal, not a read error:
        // the language allows the shape, so it parses
        // and regenerates, and the user opts back in per memory.
        if (!mc.ignored && e.driver.is_const() && gu::const_of(e.driver).to_just_i64() == Ntype::Memory_posclk_mixed) {
          // Offer a name mem_ignored actually ACCEPTS. `debug_name` carries a
          // node-id prefix ("memory_36:m") that differs between the two designs,
          // so the bare leaf is the spelling that works on both sides.
          const std::string hn = std::string{node.get_hier_name()};
          const auto        d  = hn.rfind('.');
          const std::string suggest
              = hn.empty() ? std::string{gu::debug_name(node)} : (d == std::string::npos ? hn : hn.substr(d + 1));
          return fail_unsupported(
              "memory '" + hn + "' (" + gu::debug_name(node)
              + ") has PER-PORT clock edge polarity: its ports do not all commit on the same clock edge, which this "
                "encoder does not model -- it has ONE commit point per memory. The language allows the shape, so it "
                "parses and regenerates; formal refuses it. Exclude it with '--set formal.ignore_memory="
              + suggest
              + "', which BLACKBOXES it: its reads become one shared free symbol per (port, cycle) on both sides and "
                "its contents are never compared, so the proof no longer says anything about what it stores");
        }
      } else if (pn == "clock_pin") {
        // DERIVED-BY-LOGIC only. A tech-mapped netlist routes the clock through
        // a BUFFER CELL (`.clk(g118_BUFx1_2)`), which is a `Sub` and resolves to
        // no input either -- but it is the same clock, and refusing it would
        // reject every mapped design. So: refuse when any lane of the clock is
        // ordinary combinational LOGIC in this graph (an And gate, an inverter,
        // a mux, a divider's Q), and allow an opaque instance. A tech-mapped
        // ICG cell is therefore still not caught here; that needs cell-model
        // awareness and is the pre-existing behaviour for mapped netlists.
        // memory_clock_shape_ok, not the bare Sub test: the abc read-back does
        // not wire the buffer cell to clock_pin directly -- it wraps it in a
        // Set_mask concat -- so the immediate driver is a Set_mask and the Sub
        // exemption alone refuses every mapped memory.
        // 2f-latch M9 -- a recognized Clock_cell IS modellable, and this is the
        // half the M8 fold structurally cannot reach: that fold rewrites flop
        // clocks into enables and never touches a Memory's clock_pin. Gate the
        // writes on the cell's enable instead of refusing.
        //
        // The enable's VALUE cannot be built here: this scan is phase 1, before
        // the combinational fixpoint, so no Val exists for the cone yet. Record
        // the driver PIN and resolve it in phase 2, where every write folds.
        if (auto cc = clock_cell_on(e.driver); !cc.cell.is_invalid()) {
          if (cc.div != 1) {
            return fail_unsupported("memory '" + gu::debug_name(node) + "' is clocked by a Clock_cell with div="
                                    + std::to_string(cc.div) + ", which is not implemented (v1 is div=1 only)");
          }
          if (cc.invert) {
            return fail_unsupported("memory '" + gu::debug_name(node)
                                    + "' is clocked by an INVERTED Clock_cell; this encoder models every memory "
                                      "write as landing once per step and cannot express the opposite edge");
          }
          if (resolve_clk_input(cc.clk_ref).is_invalid()) {
            return fail_unsupported("memory '" + gu::debug_name(node)
                                    + "' is clocked by a Clock_cell whose clk_ref is itself derived");
          }
          mc.commit_en = cc.en;  // invalid => an identity cell: commits every step
          continue;
        }
        if (!memory_clock_shape_ok(e.driver)) {
          return fail_unsupported("memory '" + gu::debug_name(node)
                                  + "' has a derived clock the encoder cannot model (it treats every memory write as "
                                    "landing once per step, with the clock derivation as dead code)");
        }
      }
    }
    int mtype = -1;
    for (auto e : node.inp_edges()) {
      auto        raw_pid = static_cast<int>(e.sink.get_port_id());
      std::string pn      = Ntype::get_sink_name(Ntype_op::Memory, raw_pid);
      size_t      pid     = static_cast<size_t>(raw_pid) / Ntype::Memory_port_stride;
      if (pn == "wensize") {
        mc.wensize = static_cast<int>(gu::const_of(e.driver).to_just_i64());
      } else if (pn == "fwd") {
        mc.fwd = Dlop::clone(gu::const_of(e.driver));
      } else if (pn == "undef") {
        mc.undef = Dlop::clone(gu::const_of(e.driver));
      } else if (pn == "update") {
        mc.update   = e.driver;
        mc.is_whole = true;
      } else if (pn == "update_enable") {  // MUST precede ends_with("enable") below
        mc.update_enable = e.driver;
      } else if (pn == "reset") {
        mc.reset = e.driver;
      } else if (pn == "init") {
        mc.init = e.driver;  // whole-array runtime reset-value bus
      } else if (pn == "type") {
        if (e.driver.is_const()) {
          mtype = static_cast<int>(gu::const_of(e.driver).to_just_i64());
        }
      } else if (pn == "bits" || pn == "size" || pn == "posclk" || ends_with(pn, "clock_pin")) {
        // config / clock: abstracted out of the relational encoding
      } else {
        if (mc.ports.size() <= pid) {
          mc.ports.resize(pid + 1);
        }
        if (ends_with(pn, "addr")) {
          mc.ports[pid].addr = e.driver;
        } else if (ends_with(pn, "din")) {
          mc.ports[pid].din = e.driver;
        } else if (ends_with(pn, "enable")) {
          mc.ports[pid].en = e.driver;
        } else if (ends_with(pn, "rdport")) {
          // A comptime pin: a non-constant driver is not "read port" -- probe
          // it (const_of on a wire is a hard abort).
          mc.ports[pid].rd = e.driver.is_const() && !gu::const_of(e.driver).is_known_false();
        }
      }
    }
    // A type==2 array has NO cross-cycle persistence (a runtime-indexed comb
    // array / ROM): its contents are rebuilt each cycle from either the
    // whole-array `update` bus (is_whole) or the comptime `init` constant
    // (per-port-write arrays / ROMs, applied in phase 2), so it gets no shared
    // persistent `a_cur` and no next_mem; reads/read_all see the post-write
    // array (phase 2). This was gated on is_whole, which silently DROPPED the
    // init contents of non-whole type==2 arrays (ROM / mut-array) -> the array
    // stayed a free symbol -> reads diverged across designs -> false refute.
    mc.mtype   = mtype;
    mc.is_comb = (mtype == 2);
    for (const auto& e2 : node.out_edges()) {  // read_all is a DRIVER pin (not in inp_edges)
      if (static_cast<hhds::Port_id>(e2.driver.get_port_id()) == Ntype::Memory_readall_pid) {
        mc.ra_pin = node.get_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
        break;
      }
    }
    // Current contents: shared array symbol (collapse) or fresh. A no-write
    // PERSISTENT memory with comptime init is a ROM: its contents are the const
    // init forever (the type==2 path already rebuilds from init each cycle; this
    // is the persistent type==0/1 analogue — sync/async ROM). It must NOT share a
    // free symbol: phase 2 pins its (fresh, PER-DESIGN) a_cur to the init
    // constant. Sharing one symbol and then pinning each side to its own init
    // would be a vacuous proof when the two inits differ; per-design + compared
    // outputs makes equal inits PROVE and differing inits REFUTE.
    Sort asort = tm_.mkArraySort(bv(mc.sig.addr_w), bv(mc.sig.bits));
    mc.is_rom  = (!mc.is_comb && !mc.is_whole && mc.sig.n_wr == 0 && !mc.init.is_invalid());
    if (shared_mems != nullptr && !mc.is_rom) {
      if (auto it = shared_mems->find(mc.key); it != shared_mems->end()) {
        mc.a_cur        = it->second;
        mc.a_cur_shared = true;
      }
    }
    if (mc.a_cur.isNull()) {
      mc.a_cur = tm_.mkConst(asort, std::string(prefix) + mc.key);
    }
    // Cell write-port count: the `undef`/`fwd` matrices are laid out with THIS
    // stride (bit r*n_wr_cells + w). Phase 2's `n_wr` is the same count
    // (applied_upto gets one entry per non-read port plus a sentinel), so the
    // two agree by construction.
    size_t n_wr_cells = 0;
    for (auto& p : mc.ports) {
      if (!p.rd) {
        ++n_wr_cells;
      }
    }
    // Seed each read port's dout with a fresh symbol at driver pid (n_wr + k).
    int n_rd_pos = 0;
    for (auto& p : mc.ports) {
      if (!p.rd) {
        continue;
      }
      auto        dout_dpin = node.get_driver_pin(static_cast<hhds::Port_id>(mc.sig.n_wr + n_rd_pos));
      bool        sgn       = dout_dpin.is_invalid() ? false : !gu::is_unsign(dout_dpin);
      std::string rk        = mc.key + ":rd" + std::to_string(n_rd_pos);
      // type==1 (sync, latency-1): the dout THIS cycle is a REGISTERED value — a
      // current-state symbol seeded from shared_reads (threaded from last cycle),
      // NOT a within-cycle read. Its next-state (select(read-source, addr)) is
      // emitted in phase 2 into out.next_read[rk]. type==0/2 keep a within-cycle
      // fresh var tied to select(...) in phase 2 (latency-0).
      Term        fresh;
      Term        carried_xm;  // sync read: LAST cycle's X plane, threaded in with the value
      const bool  sync_threaded = (mtype == 1 && shared_reads != nullptr);
      if (sync_threaded) {
        if (auto it = shared_reads->find(rk); it != shared_reads->end()) {
          fresh      = it->second.term;
          carried_xm = it->second.x_mask;
        }
      }
      if (fresh.isNull()) {
        // BLACKBOXED memory (formal.ignore_memory): mint the dout WITHOUT the
        // per-design `prefix`, so REF and IMPL get the SAME symbol -- the
        // sequence-transducer treatment (pass/lec/README.md §2) applied to a
        // memory, with the premise ASSERTED by the user instead of proven. The
        // symbol must still be fresh per CYCLE: the unroll re-encodes, and a
        // symbol reused across cycles would pin the read to a constant and
        // false-prove a design whose only difference is WHEN the memory changes.
        // Phase 2 skips the tie and the next-state, so nothing constrains it.
        if (mc.ignored) {
          // Take the CALLER-BUILT symbol: cvc5's mkConst does not intern by
          // name, so minting "the same name" on each side yields two DISTINCT
          // free variables and the read diverges per side (a false REFUTE).
          // query.cpp seeds these into shared_bbox / step_bbox, which is the
          // same channel a true blackbox's outputs ride.
          const std::string sk = "ignmem:" + rk;
          if (shared_bbox_ != nullptr) {
            if (auto sit = shared_bbox_->find(sk); sit != shared_bbox_->end()) {
              fresh = sit->second.term;
            }
          }
          if (fresh.isNull()) {
            return fail("memory '" + gu::debug_name(node) + "' is in formal.ignore_memory but its shared read symbol '" + sk
                        + "' was not built; the blackbox would be free PER SIDE and could false-refute");
          }
        } else {
          fresh = tm_.mkConst(bv(mc.sig.bits), std::string(prefix) + rk);
        }
      }
      if (!dout_dpin.is_invalid()) {
        pin2val[pinkey(dout_dpin)] = Val{fresh, mc.sig.bits, sgn};
      }
      // ordering="none" (the `undef` matrix): this read has no defined value in
      // a collision window. Mint a deferred X bit-plane symbol NOW and tie it to
      // the collision predicate in phase 2, exactly like the dout value itself —
      // driver_val() hands out COPIES of the Val, and the combinational fixpoint
      // runs between the phases, so a plane attached in phase 2 would arrive
      // after every consumer already snapshotted a plane-less Val.
      //   * only on the REFERENCE side (x_dontcare_): an impl-side plane would
      //     let the implementation mask away its own bugs.
      //   * mtype==1 (registered dout) mints the plane too, but for the NEXT
      //     state: the dout visible THIS cycle is last cycle's registered read,
      //     so the plane visible now is last cycle's plane (`carried_xm`, come
      //     in through shared_reads with the value). `xm` is this cycle's read
      //     plane and leaves through next_read. Without shared_reads the caller
      //     is not threading state at all, so the latency-0 tie below applies
      //     and `xm` IS the visible plane.
      Term xm;
      if (x_dontcare_ && mc.undef && !mc.is_whole && !mc.is_comb && n_wr_cells > 0 && !dout_dpin.is_invalid()) {
        for (size_t w = 0; w < n_wr_cells; ++w) {
          if (mc.undef->bit_test(static_cast<int>(static_cast<size_t>(n_rd_pos) * n_wr_cells + w))) {
            xm                                = tm_.mkConst(bv(mc.sig.bits), std::string(prefix) + rk + ":xm");
            pin2val[pinkey(dout_dpin)].x_mask = sync_threaded ? carried_xm : xm;
            break;
          }
        }
      }
      mc.rd_xmask.push_back(xm);
      mc.rd_xcur.push_back(sync_threaded ? carried_xm : Term{});
      mc.rd_fresh.push_back(fresh);
      mc.rd_addr.push_back(p.addr);
      mc.rd_key.push_back(rk);
      ++n_rd_pos;
    }
    // Async read_all: registered reflects the CURRENT committed array (computable
    // now from a_cur); combinational reflects the post-update array (deferred to
    // phase 2 via a fresh symbol, like the read douts).
    if (!mc.ra_pin.is_invalid()) {
      bool sgn = !gu::is_unsign(mc.ra_pin);
      int  rw  = mc.sig.size * mc.sig.bits;
      if (mc.is_comb) {
        mc.ra_fresh                = tm_.mkConst(bv(rw), std::string(prefix) + mc.key + ":ra");
        pin2val[pinkey(mc.ra_pin)] = Val{mc.ra_fresh, rw, sgn};
      } else {
        Term bus = tm_.mkTerm(Kind::SELECT, {mc.a_cur, bv_const(tm_, mc.sig.addr_w, 0)});  // entry 0 (low bits)
        for (int i = 1; i < mc.sig.size; ++i) {
          Term ei = tm_.mkTerm(Kind::SELECT, {mc.a_cur, bv_const(tm_, mc.sig.addr_w, static_cast<uint64_t>(i))});
          bus     = tm_.mkTerm(Kind::BITVECTOR_CONCAT, {ei, bus});  // CONCAT arg0 = high
        }
        pin2val[pinkey(mc.ra_pin)] = Val{bus, rw, sgn};
      }
    }
    mem_cuts.push_back(std::move(mc));
  }

  // ---- Combinational nodes. occurrences().nodes(hhds::Node_order::forward) virtual-flattens the design: it
  // descends into every sub-instance body (so a StageReg/ALU instance's internal
  // nodes are visited here) and inp_edges() resolves drivers across instance
  // boundaries. Flop/Memory state is cut (seeded above), so the combinational
  // graph is ACYCLIC — but occurrences().nodes(hhds::Node_order::forward) can still emit a node before a driver
  // that lives in a loop_break sub-instance emitted earlier (e.g. a register
  // file's combinational read, whose read-address is computed in the parent).
  // So process to a FIXPOINT: a node whose operands are not yet resolved is
  // deferred to a later pass; an acyclic graph converges, and a node still stuck
  // after no-progress is a genuine error (a real comb cycle / missing driver).
  absl::flat_hash_set<std::string> done;
  auto                             nodekey = [](const auto& n) -> std::string { return box_node_key(n); };
  // Resolve a named state-cell sink through the occurrence, so drivers across
  // an instance boundary are threaded into this hierarchy position. Latch pin
  // ids intentionally match Flop pin ids (graph/cell.cpp).
  auto hier_sink_driver = [](const hhds::Occurrence_node& n, std::string_view sink_name) -> hhds::Occurrence_pin {
    auto pid = Ntype::get_sink_pid(Ntype_op::Flop, sink_name);
    for (const auto& e : n.inp_edges()) {
      if (e.sink.get_port_id() == pid) {
        return e.driver;
      }
    }
    return {};
  };

  // Timing-only portion of every scheduled state control cone. A nested ICG has
  // `always_latch if (!gclk) held_en <= en`; after gclk becomes a Clock_cell,
  // tolg's Boolean Eq/Xor/Mux chain for `!gclk` must not be encoded as sampled
  // DATA. Cgen can also put that polarity-normalization chain on a flop's
  // `clock_pin`, so use the same control selection as phase_sched: a latch's
  // `enable`, every other state element's `clock_pin`. The phase schedule
  // already absorbs the clock waveform and, when a latch also has a live data
  // guard, records that guard separately.
  //
  // Discover from scheduled clock controls only, and mark only nodes whose own
  // sub-cone reaches a Clock_cell. A data-only branch of `!gclk && en` therefore
  // remains ordinary data and is still available to guard_term(). If a marked
  // node also drives an ordinary data consumer, it deliberately carries no Val
  // and that consumer fails closed below.
  absl::flat_hash_set<std::string> clock_control_timing;
  if (phased) {
    absl::flat_hash_map<std::string, bool> clock_dep_memo;
    absl::flat_hash_set<std::string>       clock_dep_visiting;
    auto                                   clock_dependent = [&](auto&& self, const hhds::Occurrence_pin& p) -> bool {
      if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p)) {
        return false;
      }
      auto              n = p.get_master_node();
      const std::string k = nodekey(n);
      if (auto it = clock_dep_memo.find(k); it != clock_dep_memo.end()) {
        return it->second;
      }
      if (gu::type_op_of(n) == Ntype_op::Clock_cell) {
        clock_dep_memo[k] = true;
        return true;
      }
      // A state output is a DATA boundary.  Walking through all of its sink
      // pins reaches the cell's clock input and would therefore label every
      // ordinary read of that state as clock-derived.  On Minion this marked
      // `reg_sepc_pre[48]` timing-only merely because its Flop is clocked,
      // then refused the unrelated sepc output cone.  Sub/Memory boundaries
      // are likewise not combinational pieces of this latch-enable cone.
      const auto op = gu::type_op_of(n);
      if (gu::is_type_register(n) || op == Ntype_op::Memory || op == Ntype_op::Sub || op == Ntype_op::IO) {
        clock_dep_memo[k] = false;
        return false;
      }
      if (!clock_dep_visiting.insert(k).second) {
        return false;
      }
      bool dep = false;
      for (const auto& e : n.inp_edges()) {
        dep |= self(self, e.driver);
      }
      clock_dep_visiting.erase(k);
      clock_dep_memo[k] = dep;
      return dep;
    };
    auto collect_clock_timing = [&](auto&& self, const hhds::Occurrence_pin& p) -> void {
      if (!clock_dependent(clock_dependent, p)) {
        return;
      }
      auto              n = p.get_master_node();
      const std::string k = nodekey(n);
      if (!clock_control_timing.insert(k).second) {
        return;
      }
      for (const auto& e : n.inp_edges()) {
        self(self, e.driver);
      }
    };
    for (auto n : g->occurrences(opaque).nodes(hhds::Node_order::forward)) {
      const auto op = gu::type_op_of(n);
      if (op != Ntype_op::Flop && op != Ntype_op::Fflop && op != Ntype_op::Latch && op != Ntype_op::Memory) {
        continue;
      }
      auto pit = phase_plan_->ep.find(box_node_key(n));
      if (pit == phase_plan_->ep.end()) {
        continue;
      }
      if (op == Ntype_op::Latch) {
        if (!pit->second.clock_role_latch) {
          continue;
        }
        collect_clock_timing(collect_clock_timing, livehd::latch_contract::sink_driver_hier(n, "enable"));
      } else {
        collect_clock_timing(collect_clock_timing, livehd::latch_contract::sink_driver_hier(n, "clock_pin"));
      }
    }
  }

  bool         progress    = true;
  unsigned int budget_tick = 0;  // throttles the per-node deadline check (avoids a now() per node)
  while (progress) {
    // Budget-aware encode: bail before another full fixpoint pass over the
    // virtually-flattened design — this loop, not cvc5, is the deep-parent time sink.
    if (over_budget()) {
      return fail("encode budget exceeded during combinational fixpoint (formal.timeout); raise --set formal.timeout");
    }
    progress = false;
    for (auto node : g->occurrences(opaque).nodes(hhds::Node_order::forward)) {
      // A single forward_hier pass over a huge flattened design can itself blow
      // the budget before the while-head re-checks; sample the clock every 1024 nodes.
      if ((++budget_tick & 0x3ff) == 0 && over_budget()) {
        return fail("encode budget exceeded during combinational fixpoint (formal.timeout); raise --set formal.timeout");
      }
      auto op = gu::type_op_of(node);

      // Skip nodes with no consumers (cgen does the same). A memory with no read
      // ports is unobservable (its contents are never sampled) -> sound to skip.
      // Use stored connectivity for this early dead-node elimination. The
      // occurrence view resolves edges to hierarchy leaves; a parent producer
      // feeding a callee input can therefore have no occurrence-level out edge
      // even though the callee consumer resolves that producer through
      // inp_edges(). Encoding an extra unreachable node is harmless; skipping a
      // live producer is not.
      if (!node.base_node().has_out_edges()) {
        continue;
      }
      // Flops were cut above (Q seeded; next-state emitted below) — skip the cell.
      if (op == Ntype_op::Flop) {
        continue;
      }
      if (phased && op == Ntype_op::Latch) {
        if (!is_transparent_latch(node)) {
          continue;  // scheduled state endpoint, cut with the flops above
        }
        if (done.contains(nodekey(node))) {
          continue;
        }
        hhds::Occurrence_pin dd;
        for (const auto& e : node.inp_edges()) {
          if (e.sink.get_port_id() == Ntype::get_sink_pid(Ntype_op::Latch, "din")) {
            dd = e.driver;
            break;
          }
        }
        if (dd.is_invalid()) {
          return fail("always-transparent latch '" + gu::debug_name(node) + "' has no din");
        }
        bool bok = true;
        Val  dv  = driver_val(dd, bok);
        if (!bok) {
          continue;  // operand not ready yet; the fixpoint comes back to it
        }
        auto qp = node.get_driver_pin(0);
        int  qw = gu::real_width(qp);
        if (qw == 0) {
          qw = 1;
        }
        Val qv{fit(dv, qw), qw, !gu::is_unsign(qp)};
        qv.x_mask           = fit_x_mask_to(tm_, dv, qw);
        pin2val[pinkey(qp)] = qv;
        done.insert(nodekey(node));
        progress = true;
        continue;
      }
      // Memory is cut in two phases (read douts seeded before this loop, writes +
      // dout-tie emitted after) — like a flop. Skip the cell here.
      if (op == Ntype_op::Memory) {
        continue;
      }

      // Still out of scope: Fflop (no reset model yet), latches.
      if (op == Ntype_op::Fflop || op == Ntype_op::Latch) {
        return fail_unsupported("sequential op '" + std::string(Ntype::get_name(op)) + "' not supported yet (M2 = Flop only)");
      }
      if (done.contains(nodekey(node))) {
        continue;  // already encoded in an earlier fixpoint pass
      }
      // Sub (instance) flattening (M5): encode the def inline with its inputs
      // bound to this instance's input Vals, and wire its outputs onto this
      // instance's output pins. Combinational defs only (e.g. ABC standard cells);
      // anything unresolved or stateful keeps the sound `Sub -> fail`.
      if (op == Ntype_op::Sub) {
        auto       sub_io = node.get_subnode_io();
        // Proven-module collapse (formal.lec.collapse): a def the driver already proved
        // equivalent is FORCED to the blackbox path even if it could be flattened,
        // so the parent stops re-solving its internals. query.cpp's shared-bbox
        // builder applies the identical predicate so the box's outputs are shared,
        // and forward_hier(opaque) does NOT descend into it (so the body below is
        // not double-encoded). Matched case-sensitively (name policy).
        const bool force_collapse
            = collapse_defs_ != nullptr && sub_io != nullptr && collapse_defs_->count(std::string(sub_io->get_name())) > 0;

        // A design sub-instance whose body lives in the graph library is DESCENDED
        // into by forward_hier (its internal nodes are visited and inp_edges()
        // threads its boundary), so encode nothing here. Only a sub NOT in the
        // library (an ABC cell-model from sub_lib_, or a true blackbox) — or one
        // FORCE-COLLAPSED, which forward_hier left undescended — is handled inline.
        if (node.get_subnode_graph() != nullptr && !force_collapse) {
          continue;
        }
        absl::flat_hash_map<hhds::Port_id, std::string> in_name;
        absl::flat_hash_map<hhds::Port_id, int>         in_pw;  // declared input-port real width
        absl::flat_hash_map<hhds::Port_id, std::string> out_name;
        for (const auto& d : sub_io->get_input_pin_decls()) {
          in_name[sub_io->get_input_port_id(d.name)] = d.name;
          // Literal declared width of the port the BLACKBOX receives on. A driver
          // wider than this is truncated by the port connection, so the miter must
          // compare only these bits. (DeclaredIoPin.bits is the literal bus width,
          // already in the same units as the encoder's real_width here.)
          in_pw[sub_io->get_input_port_id(d.name)]   = static_cast<int>(d.bits);
        }
        for (const auto& d : sub_io->get_output_pin_decls()) {
          out_name[sub_io->get_output_port_id(d.name)] = d.name;
        }
        const auto sub_def = node.get_subnode_graph();

        // Resolve to a *combinational* def for inline flattening (M5): only when a
        // resolution library is supplied AND the def has no state. Otherwise the
        // instance is a BLACKBOX and is collapsed below (shared outputs / mitered
        // inputs) — sound when both designs carry the corresponding instance.
        hhds::Graph* def = nullptr;
        if (!force_collapse && sub_lib_ != nullptr && sub_depth_ <= 32) {
          if (auto git = sub_lib_->find(node.get_subnode_gid()); git != sub_lib_->end() && git->second != nullptr) {
            def = git->second;
            for (auto dn : def->body().nodes(hhds::Node_order::forward)) {  // combinational defs only (sound)
              auto dop = gu::type_op_of(dn);
              if (dop == Ntype_op::Flop || dop == Ntype_op::Fflop || dop == Ntype_op::Latch || dop == Ntype_op::Memory) {
                def = nullptr;  // stateful -> not flattenable -> fall through to blackbox
                break;
              }
            }
          }
        }

        if (def != nullptr) {
          // Bind the instance inputs by NAME and encode the def inline.
          Io_name_map<Val> bound;
          bool             sub_deferred = false;
          for (const auto& e : node.inp_edges()) {
            auto nit = in_name.find(e.sink.get_port_id());
            if (nit == in_name.end()) {
              return fail("Sub instance '" + gu::debug_name(node) + "' input pin has no IO name");
            }
            bool sok = true;
            Val  v   = driver_val(e.driver, sok);
            if (!sok) {
              sub_deferred = true;  // input not yet encoded — retry next fixpoint pass
              break;
            }
            bound[nit->second] = v;
          }
          if (sub_deferred) {
            continue;
          }
          ++sub_depth_;
          Encoded sub_out = encode(def, &bound, std::format("{}{}.", prefix, static_cast<uint64_t>(node.get_debug_nid())));
          --sub_depth_;
          if (!sub_out.ok) {
            // A refusal inside the child IS a refusal of the parent (the parent's
            // encoding is incomplete for the same non-budget reason), so carry the
            // classification across the boundary instead of demoting it to a
            // budget-out.
            const std::string msg = "Sub def '" + std::string(def->get_name()) + "' encode failed: " + sub_out.error;
            return sub_out.unsupported ? fail_unsupported(msg) : fail(msg);
          }
          for (const auto& e : node.out_edges()) {
            auto dp  = e.driver;
            auto nit = out_name.find(dp.get_port_id());
            if (nit == out_name.end()) {
              return fail("Sub instance '" + gu::debug_name(node) + "' output pin has no IO name");
            }
            auto oit = sub_out.outputs.find(nit->second);
            if (oit == sub_out.outputs.end()) {
              return fail("Sub def '" + std::string(def->get_name()) + "' has no output '" + nit->second + "'");
            }
            pin2val[pinkey(dp)] = oit->second;
          }
          done.insert(nodekey(node));
          progress = true;
          continue;
        }

        // ---- BLACKBOX COLLAPSE (two-phase). The cross-design correspondence key
        // comes from query.cpp's box-correspondence builder (set_box_keys):
        // name-first instance pairing with an occurrence fallback, computed ONCE
        // over both designs so neither traversal order nor declaration order can
        // mispair interchangeable instances. (Without a map — a bare Encoder
        // user — fall back to the legacy per-def occurrence counter.) A stateless
        // collapsed PROVEN leaf takes the pairing-free Comb_box path instead: no
        // correspondence at all, congruence over shared per-def UFs (see
        // encode.hpp). The OUTPUTS need only the state cut (a state-aware Moore
        // box) or nothing (a stateless box) — NEVER the current inputs — so emit
        // them on the FIRST visit, before the inputs resolve. This breaks a false
        // combinational cycle where a collapsed stage register's output must be
        // available to resolve its own (stall-fed) input. The input-dependent
        // bbin compare points / UF-tie equalities + next-state are deferred to a
        // later pass (synthetic, read by no combinational node, so they block none).
        std::string      nk = nodekey(node);
        std::string      defname(sub_io->get_name());
        std::string      bk;
        const State_box* sbox = nullptr;
        const Comb_box*  cbox = nullptr;
        if (comb_boxes_ != nullptr) {
          if (auto it = comb_boxes_->find(defname); it != comb_boxes_->end()) {
            cbox = &it->second;
          }
        }
        if (auto okit = bb_outkey.find(nk); okit == bb_outkey.end()) {
          if (box_keys_ != nullptr) {
            auto kit = box_keys_->find(nk);
            if (kit == box_keys_->end()) {
              // The builder walks the same hierarchy with the same blackbox
              // predicate, so a miss means the walks drifted apart. Degrading
              // silently (an unshared / mis-keyed box) risks a WRONG verdict in
              // either direction — fail loudly instead; the engine reports the
              // encode error as INCONCLUSIVE.
              return fail("blackbox Sub '" + gu::debug_name(node) + "' (def '" + defname
                          + "') missing from the box-correspondence map (builder/encoder walk drift)");
            }
            bk = kit->second;
          } else {
            bk = defname + "#" + std::to_string(bbox_occ[defname]++);  // legacy: encoder-local occurrence order
          }
          bb_outkey[nk] = bk;
          if (state_boxes_ != nullptr) {
            if (auto it = state_boxes_->find(bk); it != state_boxes_->end()) {
              sbox = &it->second;
            }
          }
          if (cbox != nullptr) {  // pairing-free stateless box: fresh per-instance outputs, tied to UF(inputs) in phase 2
            for (const auto& e : node.out_edges()) {
              auto        dp   = e.driver;
              auto        nit  = out_name.find(dp.get_port_id());
              std::string port = nit != out_name.end() ? nit->second : std::to_string(dp.get_port_id());
              // LOCAL width + LOCAL signedness: the Val models the pin this side's
              // parent actually reads, and downstream fit_to extends by is_signed —
              // adopting the other side's (union) signedness would model BOTH sides
              // extending identically and false-PROVE a real sign-vs-zero-extension
              // divergence downstream of the leaf. The UF codomain stays the union
              // width; phase 2 fits the application down to this local width.
              int         ow   = gu::real_width(dp);
              if (ow == 0) {
                ow = 1;
              }
              bool osgn = !gu::is_unsign(dp);
              Val  ov;
              if (cbox->in_w == 0) {
                // A no-input leaf is a constant: same def => same value, so the
                // outputs share ONE per-(def,port) symbol (see Comb_box) — refit
                // to the local pin width/sign for the same reason as above.
                if (auto cit = cbox->out_const.find(port); cit != cbox->out_const.end()) {
                  ov = Val{fit(cit->second, ow), ow, osgn};
                } else {
                  ov = Val{tm_.mkConst(bv(ow), std::string(prefix) + "cb:" + bk + ":" + port), ow, osgn};  // unshared (sound)
                }
              } else {
                // NOT shared across the designs: sharing without the bbin input
                // obligations would be an unjustified equality. The phase-2 UF tie
                // provides the (pairing-free) congruence instead.
                // NO x_mask is minted here even under formal.lec.gold_x=ignore: a box is
                // X-opaque, and smearing the outputs whenever ANY input might be X
                // makes every downstream compare vacuous — a false PROVEN that no
                // fallback ever re-examines (the flat confirmation fires only on
                // REFUTED). Leaving the mask empty is the PASS-sound choice: an X
                // reaching the box at worst makes the UF applications differ, and
                // that spurious refute IS flat-confirmed (the flat encode carries
                // the precise per-cone X planes).
                ov = Val{tm_.mkConst(bv(ow), std::string(prefix) + "cb:" + bk + ":" + port), ow, osgn};
              }
              pin2val[pinkey(dp)] = ov;
            }
          } else if (sbox != nullptr) {  // state-aware: outputs are MOORE — UF_out(state).
            Val S            = seed_state(std::string("\x01") + "leafstate:" + bk, sbox->state_w, false);
            bb_state_sym[nk] = S;
            for (const auto& e : node.out_edges()) {
              auto        dp   = e.driver;
              auto        nit  = out_name.find(dp.get_port_id());
              std::string port = nit != out_name.end() ? nit->second : std::to_string(dp.get_port_id());
              auto        ofn  = sbox->out_fn.find(port);
              if (ofn == sbox->out_fn.end()) {
                return fail("state box for '" + defname + "' has no output UF for port '" + port + "'");
              }
              int ow = sbox->out_w.count(port) ? sbox->out_w.at(port) : std::max(1, gu::real_width(dp));
              Val bov{tm_.mkTerm(Kind::APPLY_UF, {ofn->second, S.term}), ow, !gu::is_unsign(dp)};
              // Deliberately NO x_mask (see the Comb_box note above): smearing a
              // Moore output whenever the abstract state might hold an X made every
              // downstream compare vacuous — a false PROVEN with no fallback. An X
              // divergence at worst refutes spuriously, which the driver's flat
              // confirmation re-solves with the precise per-cone X planes.
              pin2val[pinkey(dp)] = bov;
            }
          } else {  // stateless: outputs are shared free symbols.
            for (const auto& e : node.out_edges()) {
              auto        dp   = e.driver;
              auto        nit  = out_name.find(dp.get_port_id());
              std::string port = nit != out_name.end() ? nit->second : std::to_string(dp.get_port_id());
              std::string key  = bk + ":" + port;
              Val         ov;
              if (shared_bbox_ != nullptr) {
                if (auto it = shared_bbox_->find(key); it != shared_bbox_->end()) {
                  ov = it->second;
                }
              }
              if (ov.term.isNull()) {  // fallback (query should have pre-built it): fresh
                int w = gu::real_width(dp);
                if (w == 0) {
                  w = 1;
                }
                ov = Val{tm_.mkConst(bv(w), "bb:" + key), w, !gu::is_unsign(dp)};
              }
              pin2val[pinkey(dp)] = ov;
            }
          }
          progress = true;
        } else {
          bk = okit->second;
          if (state_boxes_ != nullptr) {
            if (auto it = state_boxes_->find(bk); it != state_boxes_->end()) {
              sbox = &it->second;
            }
          }
        }

        // Phase 2: resolve the inputs; defer (blocking no one — outputs are out)
        // until they ALL resolve, then emit the bbin compare points + next-state.
        Io_name_map<Val> bb_in_by_port;
        bool             all_in = true;
        for (const auto& e : node.inp_edges()) {
          bool sok = true;
          Val  v   = driver_val(e.driver, sok);
          if (!sok && sub_def != nullptr) {
            // Resolve the callee's clock interface only on the exceptional
            // timing-only path. Most boxes have ordinary encoded inputs; doing
            // a recursive interface scan for every one of them made large
            // hierarchy encodes needlessly quadratic. The shared cache keeps
            // the remaining probes one-per-definition.
            const auto& clock_input_pids = livehd::latch_contract::clock_input_ports(sub_def, clock_port_cache);
            if (clock_input_pids.contains(static_cast<uint32_t>(e.sink.get_port_id()))) {
              // A collapsed child's CLOCK is a sequence-boundary obligation,
              // not an ordinary read of the clock as data. See
              // clock_box_input above.
              v = clock_box_input(e.driver, sok, 0);
            }
          }
          if (!sok) {
            all_in = false;
            break;
          }
          auto        pid  = e.sink.get_port_id();
          auto        nit  = in_name.find(pid);
          std::string port = nit != in_name.end() ? nit->second : std::to_string(pid);
          // Compare only the bits the blackbox port actually receives (a wider driver
          // is truncated by the connection — see the SRAM addr note).
          if (auto pit = in_pw.find(pid); pit != in_pw.end() && pit->second > 0 && pit->second < v.width) {
            Val tv{fit(v, pit->second), pit->second, v.is_signed};
            tv.x_mask = fit_x_mask_to(tm_, v, pit->second);  // keep the X plane through the truncation
            v         = tv;
          }
          bb_in_by_port[port] = v;
        }
        if (!all_in) {
          continue;  // inputs not ready; outputs already emitted, so nothing is blocked
        }
        if (cbox != nullptr) {
          // Pairing-free stateless box, phase 2: tie the phase-1 fresh output
          // symbols to UF_def_port(NAME-SORTED input concat). Congruence over the
          // shared per-def UF replaces the bbin obligations entirely — equal
          // inputs anywhere yield equal outputs, no instance pairing to get
          // wrong. (in_w == 0: the outputs are already the shared per-def
          // constants; nothing to tie.)
          if (cbox->in_w > 0) {
            // A connected input the layout does not cover would silently narrow
            // the UF domain — the leaf would be modeled as INDEPENDENT of that
            // input (an obligation dropped with no bbin backstop). The builder
            // unions declared ports with every connected pid-fallback port, so a
            // miss here is walk drift: fail loudly (sound Unknown).
            for (const auto& [port, v] : bb_in_by_port) {
              bool covered = false;
              for (const auto& [pname, pw] : cbox->in_ports) {
                if (pname == port) {
                  covered = true;
                  break;
                }
              }
              if (!covered) {
                return fail("comb box for '" + defname + "' has a connected input '" + port
                            + "' outside its UF input layout (builder/encoder drift)");
              }
            }
            Term in_concat;
            for (const auto& [port, piece_w] : cbox->in_ports) {
              Val pv;
              if (auto it2 = bb_in_by_port.find(port); it2 != bb_in_by_port.end()) {
                pv = it2->second;
              } else {
                pv = Val{bv_const(tm_, piece_w, 0), piece_w, false};  // unconnected port -> 0
              }
              Term piece = fit(pv, piece_w);
              in_concat  = in_concat.isNull() ? piece : tm_.mkTerm(Kind::BITVECTOR_CONCAT, {in_concat, piece});
            }
            for (const auto& e : node.out_edges()) {
              auto        dp   = e.driver;
              auto        nit  = out_name.find(dp.get_port_id());
              std::string port = nit != out_name.end() ? nit->second : std::to_string(dp.get_port_id());
              auto        vit  = pin2val.find(pinkey(dp));
              if (vit == pin2val.end()) {
                continue;  // no phase-1 value (unreachable: phase 1 seeds every out edge)
              }
              auto ofn = cbox->out_fn.find(port);
              if (ofn == cbox->out_fn.end()) {
                return fail("comb box for '" + defname + "' has no output UF for port '" + port + "'");
              }
              // The UF codomain is the cross-design union width; the phase-1
              // symbol is this side's LOCAL pin width. Fit the application down
              // to the local width (connection-truncation semantics).
              int  uf_w   = cbox->out_w.count(port) ? cbox->out_w.at(port) : vit->second.width;
              bool uf_sgn = cbox->out_sgn.count(port) ? cbox->out_sgn.at(port) : vit->second.is_signed;
              Val  app{tm_.mkTerm(Kind::APPLY_UF, {ofn->second, in_concat}), uf_w, uf_sgn};
              out.equalities.emplace_back(vit->second.term, fit(app, vit->second.width));
            }
          }
          done.insert(nk);
          progress = true;
          continue;
        }
        for (const auto& [port, v] : bb_in_by_port) {  // inputs = miter compare points
          if (const char* de = std::getenv("LEC_DUMP_ENC"); de != nullptr && std::string_view(de) == "bbin") {
            std::string ts = v.term.toString();
            if (ts.size() > 700) {
              ts = ts.substr(0, 200) + " ...LEAVES... " + ts.substr(ts.size() - 500);
            }
            std::fprintf(stderr,
                         "[LEC_ENC pfx=%s] bbin %s:%s w=%d xmask=%d term=%s\n",
                         std::string(prefix).c_str(),
                         bk.c_str(),
                         port.c_str(),
                         v.width,
                         v.x_mask.isNull() ? 0 : 1,
                         ts.c_str());
          }
          // The bbin points are OBLIGATIONS (they justify the shared output
          // symbols), not golden outputs: excluding ref-X bits from them (the
          // generic gold_x=ignore compare rule) would let the inputs differ on
          // those bits while the box outputs stay forced equal — a false-PASS
          // channel. Strip the X plane so the obligation compares full values;
          // an X-driven divergence at worst refutes, which is flat-confirmed.
          Val ov2                                                      = v;
          ov2.x_mask                                                   = cvc5::Term{};
          out.outputs[std::string("\x02") + "bbin:" + bk + ":" + port] = ov2;
        }
        // Presence marker: a PAIRED box (state-aware or true blackbox) with no
        // connected inputs emits no bbin obligations, so an instance-count
        // mismatch would otherwise be invisible to the unmatched-correspondence
        // gate — its free/shared outputs could then refute (or vacuously prove)
        // with the correspondence looking complete. The marker is a constant on
        // both sides: two-sided it compares trivially equal (the miters skip it
        // as a diff), one-sided it lands in unmatched_* and gates the verdict.
        out.outputs[std::string("\x02") + "bbox:" + bk] = Val{bv_const(tm_, 1, 0), 1, false};
        if (sbox != nullptr) {
          // next-state = UF_next(inputs, state): the state TRANSITION depends on the
          // inputs, but it feeds the state cut a cycle later (no combinational loop).
          // The concat follows the box's NAME-SORTED cross-design layout — NOT this
          // side's decl order, which can be permuted between front-ends (see
          // State_box::in_ports); a port this instance does not drive contributes 0.
          Term in_concat;
          for (const auto& [pname, piece_w] : sbox->in_ports) {
            Val pv;
            if (auto it = bb_in_by_port.find(pname); it != bb_in_by_port.end()) {
              pv = it->second;
            } else {
              pv = Val{bv_const(tm_, piece_w, 0), piece_w, false};  // unconnected port -> 0
            }
            Term piece = fit(pv, piece_w);
            in_concat  = in_concat.isNull() ? piece : tm_.mkTerm(Kind::BITVECTOR_CONCAT, {in_concat, piece});
          }
          std::vector<Term> next_args{sbox->next_fn};
          if (sbox->in_w > 0 && !in_concat.isNull()) {
            next_args.push_back(in_concat);
          }
          next_args.push_back(bb_state_sym[nk].term);
          // Deliberately NO x_mask on the next-state (see the Comb_box phase-1
          // note): the old "any X input/state smears the whole next-state
          // unknown" rule became sticky through the BMC state threading and made
          // every later compare of the box's cone vacuous — a false PROVEN with
          // no fallback. An X reaching an opaque box now at worst diverges the
          // UF applications, and that spurious refute is flat-confirmed by the
          // driver with the precise per-cone X planes.
          Val nsv{tm_.mkTerm(Kind::APPLY_UF, next_args), sbox->state_w, false};
          out.outputs[std::string("\x01") + "nxt:\x01leafstate:" + bk] = nsv;
        }
        done.insert(nk);
        progress = true;
        continue;
      }

      // A four-microstep schedule knows the root clock's value at every point:
      // low at Close_low/Fall, high at Rise/Close_high. That makes a Clock_cell
      // an exactly encodable Boolean when RTL deliberately reads its output as
      // data inside a latch-control cone. Minion's prim_write_commit_rst_en is
      // the measured shape: `reset || (gated_clock && held_enable)`. The latch
      // is DATA-role because reset can open it asynchronously, so absorbing the
      // whole control as timing would drop reset. Giving the cell its scheduled
      // waveform preserves both terms instead.
      //
      // This is deliberately unavailable in P==1 (`microstep_ < 0`): one step
      // there is an edge, not a level, so a clock monitor has no defined DATA
      // value and must still fail closed.
      if (op == Ntype_op::Clock_cell && phased && !single_step()) {
        auto cell_sink = [&](std::string_view name) -> hhds::Occurrence_pin {
          const auto pid = Ntype::get_sink_pid(Ntype_op::Clock_cell, name);
          for (const auto& e : node.inp_edges()) {
            if (e.sink.get_port_id() == pid) {
              return e.driver;
            }
          }
          return {};
        };

        Term ref_hot;
        auto ref = cell_sink("clk_ref");
        if (ref.is_invalid()) {
          return fail_unsupported("Clock_cell '" + gu::debug_name(node) + "' has no clk_ref");
        }
        auto ref_root = livehd::latch_contract::control_root(ref, /*stop_at_clock_cell=*/true);
        if (!ref_root.net.is_invalid() && gu::is_graph_input_pin(ref_root.net)) {
          bool high = microstep_ == static_cast<int>(Phase::Rise) || microstep_ == static_cast<int>(Phase::Close_high);
          high      = high != ref_root.inverted;
          ref_hot   = tm_.mkBoolean(high);
        } else {
          bool ref_ok = true;
          Val  rv     = driver_val(ref, ref_ok);
          if (!ref_ok || rv.term.isNull()) {
            continue;  // an inner Clock_cell or wrapper is not encoded yet
          }
          ref_hot = tm_.mkTerm(Kind::DISTINCT, {rv.term, bv_const(tm_, rv.width, 0)});
        }

        Term en_hot = tm_.mkBoolean(true);
        if (auto en = cell_sink("en"); !en.is_invalid()) {
          bool en_ok = true;
          Val  ev    = driver_val(en, en_ok);
          if (!en_ok || ev.term.isNull()) {
            continue;
          }
          en_hot = tm_.mkTerm(Kind::DISTINCT, {ev.term, bv_const(tm_, ev.width, 0)});
        }

        bool active_low = false;
        if (auto inv = cell_sink("invert"); inv.is_const()) {
          active_low = !gu::const_of(inv).is_known_false();
        }
        Term level            = active_low ? tm_.mkTerm(Kind::OR, {ref_hot, tm_.mkTerm(Kind::NOT, {en_hot})})
                                           : tm_.mkTerm(Kind::AND, {ref_hot, en_hot});
        auto dpin             = node.get_driver_pin(0);
        pin2val[pinkey(dpin)] = Val{tm_.mkTerm(Kind::ITE, {level, bv_const(tm_, 1, 1), bv_const(tm_, 1, 0)}), 1, false};
        done.insert(nodekey(node));
        progress = true;
        continue;
      }

      // In P==1 a width-mask wrapper sitting ON a clock carries no data value
      // either. A
      // typed 1-bit port read picks up `Get_mask(clk, 1)`, so a gated clock
      // usually reaches its flop THROUGH one of these rather than directly --
      // measured on minion, where every such wrapper became "operand has no
      // encodable driver". Clockness propagates through the identity wrappers,
      // exactly the ones clock_cell_on already walks.
      bool clock_wrapper = false;
      if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask || op == Ntype_op::Sext || op == Ntype_op::And
          || op == Ntype_op::Or) {
        clock_wrapper = !clock_cell_on(node.get_driver_pin(0)).cell.is_invalid();
      }
      const bool step_granular_timing = single_step() && (clock_wrapper || clock_control_timing.contains(nodekey(node)));
      if (op == Ntype_op::Clock_cell || step_granular_timing) {
        // 2f-latch M9: a Clock_cell has NO DATA VALUE. Its output is a CLOCK, and
        // the binding rule is that it may only reach a clock sink -- the flop and
        // memory paths below resolve THROUGH it to a commit condition rather than
        // reading it as a bit. Encoding it as data is precisely the modelling
        // error the cell exists to remove: a gated clock read as a value is
        // indistinguishable from an ungated one to a step-granular encoder.
        //
        // Not an error, just no value: a DATA consumer then fails with "has no
        // encodable driver", which is the honest refusal for the DFT /
        // clock-monitor shape that samples a gated clock.
        done.insert(nodekey(node));
        progress = true;
        continue;
      }

      auto dpin = node.get_driver_pin(0);
      int  W    = gu::real_width(dpin);
      if (W == 0) {
        // Comparisons/reductions are 1-bit; default unknown width to that.
        W = 1;
      }
      bool out_signed = !gu::is_unsign(dpin);

      // Bucket input edges by sink port id, resolving every driver to a Val.
      absl::flat_hash_map<hhds::Port_id, std::vector<Val>> by_pid;
      std::vector<Val>                                     all;  // in edge order
      bool                                                 ok       = true;
      bool                                                 deferred = false;
      for (const auto& e : node.inp_edges()) {
        Val v = driver_val(e.driver, ok);
        if (!ok) {
          deferred = true;  // operand not yet encoded — retry in a later fixpoint pass
          break;
        }
        by_pid[e.sink.get_port_id()].push_back(v);
        all.push_back(v);
      }
      if (deferred) {
        continue;
      }

      auto pid = [&](hhds::Port_id p) -> std::vector<Val>& {
        static std::vector<Val> empty;
        auto                    it = by_pid.find(p);
        return it == by_pid.end() ? empty : it->second;
      };

      // Filled by the Concat arm and re-read by the X-plane block after the
      // switch. The lane table is the ONLY source of a lane's window width (see
      // the Concat arm), and decoding it a second time down there would pay the
      // whole inp_edges walk again on every packed bus.
      std::vector<gu::Concat_lane> concat_tbl;

      Term result;

      switch (op) {
        case Ntype_op::And:
        case Ntype_op::Or:
        case Ntype_op::Xor: {
          Kind k = (op == Ntype_op::And) ? Kind::BITVECTOR_AND : (op == Ntype_op::Or) ? Kind::BITVECTOR_OR : Kind::BITVECTOR_XOR;
          // A bitwise op is a width-preserving pass-through of its operands'
          // VALUES, so a signed value flowing into one is still signed coming out
          // -- and that decides how a later consumer WIDENS it. The pin's own hint
          // cannot answer: tolg's bind_result drops it on every op output, which
          // is why cgen carries the same walk-through in operand_reads_signed.
          //
          // Without this the two ends disagreed about our OWN emitted code. cgen
          // widens a shift's left operand with `$signed(N'sb0) | $signed(val)`; on
          // the way back in that is Or(const 0, val_signed), the Or read UNSIGNED,
          // so `fit` in the SHL case zero-extended a negative operand: `sa <<
          // ua` with sa = 3'sb100 (-4) encoded as 4, and lec REFUTED
          // tests/equiv/signed_shift_widen against a golden that iverilog says is
          // identical on all 64 inputs. A wrong verdict, and on a shape our own
          // back end emits for every signed shift.
          // Or ONLY, and only when every operand is signed: that is the widening
          // PAD our own back end emits (`$signed(N'sb0) | $signed(val)`), where a
          // zero operand carries no sign of its own but is stored signed. An
          // `And(val, MASK)` -- the width mask on every net read -- is NOT this:
          // a masked value is non-negative, and marking it signed made later
          // widenings sign-extend it. Measured: the broad rule fixed 2 tests and
          // broke 6.
          if (op == Ntype_op::Or && !all.empty()) {
            bool every = true;
            for (const auto& v : all) {
              every &= v.is_signed;
            }
            out_signed |= every;
          }
          for (const auto& v : all) {
            Term t = fit(v, W);
            result = result.isNull() ? t : tm_.mkTerm(k, {result, t});
          }
          if (result.isNull()) {
            // Degenerate 0-operand reduction (can arise after the front-end folds
            // every operand away): the identity element. AND of nothing = all-ones,
            // OR/XOR of nothing = 0.
            result = (op == Ntype_op::And) ? tm_.mkTerm(Kind::BITVECTOR_NOT, {bv_const(tm_, W, 0)}) : bv_const(tm_, W, 0);
          }
          break;
        }
        case Ntype_op::Mult: {
          for (const auto& v : all) {
            Term t = fit(v, W);
            result = result.isNull() ? t : tm_.mkTerm(Kind::BITVECTOR_MULT, {result, t});
          }
          if (result.isNull()) {
            result = bv_const(tm_, W, 1);  // empty product = 1
          }
          break;
        }
        case Ntype_op::Sum: {
          // Verilog's own context rule, exactly: an addition is SIGNED iff every
          // operand is signed. The pin's own hint cannot answer -- tolg's
          // bind_result stamps every op output unsigned -- so without this walk a
          // Sum whose value goes negative was zero-extended by its consumer.
          // Measured on tests/equiv/sext_scalar_net: `$signed(w) + b` spans
          // [-9..7], the stamped-unsigned model read -1 at 5 bits as 31 where
          // iverilog says 255 -- and the side lec got wrong was the BASELINE, so
          // an equivalent pair refuted (pass.bitfuzz's re-inferred side carried
          // the accurate sign and exposed the modelling gap).
          //
          // A NON-NEGATIVE constant is transparent, not a veto: cgen emits every
          // constant as a SIGNED literal (`2'sh1`), so in the emitted text it
          // does not demote the expression -- and it zero- and sign-extends
          // alike, so const_val's is_signed=false for it is a modelling
          // convenience, not a sign. (Same stance as cgen's
          // operand_reads_signed.) A negative constant reads signed already.
          //
          // This is NOT the reverted "any operand signed" bitwise rule: ALL
          // matches Verilog's semantics for `+`/`-` exactly, the same way the Or
          // arm above matches `|` and the SRA arm below matches `>>>`.
          {
            bool every = !all.empty();
            for (const auto& e : node.inp_edges()) {
              if (e.driver.is_const()) {
                if (gu::const_of(e.driver).is_negative()) {
                  continue;  // negative literal: signed in the emitted text
                }
                continue;  // non-negative constant: sign-transparent
              }
              bool ok2 = true;
              Val  v   = driver_val(e.driver, ok2);
              if (!ok2) {
                every = false;
                break;
              }
              every &= v.is_signed;
            }
            out_signed |= every;
          }
          Term add_acc;
          for (const auto& v : pid(0)) {  // "a" pins: added
            Term t  = fit(v, W);
            add_acc = add_acc.isNull() ? t : tm_.mkTerm(Kind::BITVECTOR_ADD, {add_acc, t});
          }
          if (add_acc.isNull()) {
            add_acc = bv_const(tm_, W, 0);
          }
          for (const auto& v : pid(1)) {  // "b" pins: subtracted
            add_acc = tm_.mkTerm(Kind::BITVECTOR_SUB, {add_acc, fit(v, W)});
          }
          result = add_acc;
          break;
        }
        case Ntype_op::Div: {
          if (pid(0).size() != 1 || pid(1).size() != 1) {
            return fail("Div expects single a/b drivers");
          }
          // Division is NOT modular like add/mult: fitting the operands to the
          // (often tiny) output width W corrupts it. Bitwidth analysis stamps the
          // QUOTIENT width on the Div pin — e.g. `c/100` with c:u3 has a quotient
          // that is always 0, so W is ~1 bit; `fit(100, 1)` would truncate the
          // divisor to 0 => UDIV-by-zero => all-ones garbage. Compute the quotient
          // at a width that holds both operands, then narrow to the output width.
          int  dw = std::max({pid(0)[0].width, pid(1)[0].width, W});
          Term a  = fit(pid(0)[0], dw);
          Term b  = fit(pid(1)[0], dw);
          Term q  = tm_.mkTerm(out_signed ? Kind::BITVECTOR_SDIV : Kind::BITVECTOR_UDIV, {a, b});
          result  = fit(Val{q, dw, out_signed}, W);
          break;
        }
        case Ntype_op::Rem: {
          if (pid(0).size() != 1 || pid(1).size() != 1) {
            return fail("Rem expects single a/b drivers");
          }
          // Same width discipline as Div, and it matters MORE here: the result
          // width is legitimately tiny (`a % 4` is 2 bits), so fitting the DIVISOR
          // to W would truncate 4 to 0 and hand cvc5 a remainder-by-zero, which is
          // all-ones rather than an error. Compute at a width holding both
          // operands, then narrow.
          //
          // SREM, never SMOD: the op is TRUNCATED remainder (sign follows the
          // dividend), matching Verilog `%`, Dlop::rem_op and the tolg lowering.
          // SMOD is floored and would flip the sign for a negative dividend --
          // and because BOTH sides of the miter would use the same wrong kind,
          // lec would still report "equivalent". One kind, no sign switch: every
          // value here is signed and unsigned is just the non-negative subset.
          int  dw = std::max({pid(0)[0].width, pid(1)[0].width, W});
          Term a  = fit(pid(0)[0], dw);
          Term b  = fit(pid(1)[0], dw);
          Term r  = tm_.mkTerm(Kind::BITVECTOR_SREM, {a, b});
          result  = fit(Val{r, dw, true}, W);
          break;
        }
        case Ntype_op::Not: {
          if (all.empty()) {
            return fail("Not has no operand");
          }
          result = tm_.mkTerm(Kind::BITVECTOR_NOT, {fit(all[0], W)});
          break;
        }
        case Ntype_op::Ror: {
          // reduction-OR: 1 iff any input bit set.
          Term concat;
          for (const auto& v : all) {
            concat = concat.isNull() ? v.term : tm_.mkTerm(Kind::BITVECTOR_CONCAT, {concat, v.term});
          }
          if (concat.isNull()) {
            return fail("Ror has no operand");
          }
          int cw = 0;
          for (const auto& v : all) {
            cw += v.width;
          }
          result = pred_to_bv(tm_.mkTerm(Kind::DISTINCT, {concat, bv_const(tm_, cw, 0)}));
          break;
        }
        case Ntype_op::EQ: {
          if (all.size() < 2) {
            return fail("EQ expects >= 2 operands");
          }
          // Verilog comparison signedness: the operation is signed ONLY if EVERY
          // operand is signed; one unsigned operand makes the whole compare
          // unsigned. The width extension must follow that effective sign, not each
          // operand's own sign — else a 1-bit `signed` control (value 1 == -1)
          // would sign-extend to all-ones inside an `== 1` and never match.
          int  cw         = 0;
          bool eff_signed = true;
          for (const auto& v : all) {
            cw         = std::max(cw, v.width);
            eff_signed = eff_signed && v.is_signed;
          }
          auto ext = [&](const Val& v) { return fit_to(tm_, Val{v.term, v.width, eff_signed}, cw); };
          Term acc;
          for (size_t i = 1; i < all.size(); ++i) {
            Term eq = tm_.mkTerm(Kind::EQUAL, {ext(all[0]), ext(all[i])});
            acc     = acc.isNull() ? eq : tm_.mkTerm(Kind::AND, {acc, eq});
          }
          result = pred_to_bv(acc);
          break;
        }
        case Ntype_op::LT:
        case Ntype_op::GT: {
          auto& as = pid(0);
          auto& bs = pid(1);
          if (as.empty() || bs.empty()) {
            return fail("LT/GT missing a/b operand");
          }
          Term acc;
          for (const auto& a : as) {
            for (const auto& b : bs) {
              // Pyrope compares by VALUE, so the comparison is signed whenever
              // EITHER operand is signed (not only when BOTH are) — a signed value
              // vs a non-negative constant (which the front end stamps unsigned)
              // must still order by signed value, else e.g. the saturation guard
              // `(s8)-24 > 7` reads -24 as unsigned 232 and wrongly holds. Extend
              // each operand per ITS OWN sign (sign-extend the signed one,
              // zero-extend the unsigned one); for a MIXED pair add one bit of
              // headroom so the unsigned operand's top bit is not misread as a
              // sign bit under the signed compare. This matches the cgen/yosys
              // (value) semantics the lgyosys backend proves against.
              bool use_signed = a.is_signed || b.is_signed;
              int  cw         = std::max(a.width, b.width);
              if (a.is_signed != b.is_signed) {
                cw += 1;  // mixed sign: keep the unsigned operand non-negative
              }
              Term la  = fit_to(tm_, Val{a.term, a.width, a.is_signed}, cw);
              Term lb  = fit_to(tm_, Val{b.term, b.width, b.is_signed}, cw);
              Kind cmp = (op == Ntype_op::LT) ? (use_signed ? Kind::BITVECTOR_SLT : Kind::BITVECTOR_ULT)
                                              : (use_signed ? Kind::BITVECTOR_SGT : Kind::BITVECTOR_UGT);
              Term one = tm_.mkTerm(cmp, {la, lb});
              acc      = acc.isNull() ? one : tm_.mkTerm(Kind::AND, {acc, one});
            }
          }
          result = pred_to_bv(acc);
          break;
        }
        case Ntype_op::SHL: {
          if (pid(0).empty()) {
            return fail("SHL missing a");
          }
          const Val& a = pid(0)[0];
          Term       acc;
          for (const auto& b : pid(1)) {  // one-hot amounts, ORed
            // A shift count is UNSIGNED (a bit position), so zero-extend it. Reading
            // it as signed would sign-extend a wrapped 3-bit amount like 7 (== -1)
            // to a huge value, overshifting `1 << amt` to 0 (e.g. RISC-V vlmax).
            // It is also SELF-DETERMINED: never truncate it to the (possibly
            // narrower) result width. A one-bit value shifted by the unsized
            // constant 2 must become zero, not `a << (2 truncated to 1 bit)` = a.
            // cvc5 requires equal operand widths, so shift at a width that holds
            // the value, result, AND full count, then narrow the result.
            const int cw    = std::max({a.width, b.width, W, 1});
            Term      af    = fit(a, cw);
            Term      shamt = fit_to(tm_, Val{b.term, b.width, false}, cw);
            Term      wide  = tm_.mkTerm(Kind::BITVECTOR_SHL, {af, shamt});
            Term      sh    = fit_to(tm_, Val{wide, cw, a.is_signed}, W);
            acc             = acc.isNull() ? sh : tm_.mkTerm(Kind::BITVECTOR_OR, {acc, sh});
          }
          result      = acc.isNull() ? fit(a, W) : acc;
          // Verilog's rule for a shift: the result's sign is the LEFT operand's
          // (the amount never counts). Same walk-through as the SRA arm below and
          // for the same reason -- the pin's own hint is stamped unsigned by
          // tolg's bind_result, so a shifted NEGATIVE value was zero-extended by
          // its consumer. Measured on tests/equiv/signed_shift_widen: `sa << ua`
          // with sa = -4 was materialized at its 10-bit stamped width as 1020 and
          // zero-extended into the 16-bit output where iverilog says 65532 -- on
          // the BASELINE side of the miter, so an equivalent pair refuted once
          // pass.bitfuzz gave the other side the accurate sign.
          out_signed |= pid(0)[0].is_signed;
          break;
        }
        case Ntype_op::SRA: {
          if (pid(0).empty() || pid(1).empty()) {
            return fail("SRA missing a/b");
          }
          // A right shift pulls bits DOWN from higher positions, so the operand
          // must be at its full width BEFORE shifting — fitting it to the (possibly
          // narrower) output width W first would drop the very bits the shift moves
          // into range (e.g. (w[63:0] >> 1)[0] = w[1], but ((w[0]) >> 1) = 0). Shift
          // at max(operand,output) width, then fit the result to W. Arithmetic
          // (sign-replicating) only for a signed operand; logical otherwise — this
          // mirrors Verilog `>>>` (arithmetic iff the operand is signed).
          const Val& a    = pid(0)[0];
          int        cw   = std::max({a.width, pid(1)[0].width, W, 1});
          Term       af   = fit(a, cw);
          Term       shf  = fit_to(tm_, Val{pid(1)[0].term, pid(1)[0].width, false}, cw);  // shift amount: unsigned
          Kind       k    = a.is_signed ? Kind::BITVECTOR_ASHR : Kind::BITVECTOR_LSHR;
          result          = fit_to(tm_, Val{tm_.mkTerm(k, {af, shf}), cw, a.is_signed}, W);
          // An arithmetic shift PRESERVES the operand's sign, so the result is
          // signed whenever `a` is -- and the pin's own hint cannot say so, since
          // tolg's bind_result drops it on every op output. cgen already carries
          // exactly this walk-through (operand_reads_signed recurses into an SRA's
          // `a`); without the same rule here a CHAINED right shift demoted its
          // OUTER shift to logical, because the inner SRA's result Val said
          // unsigned. Measured on tests/equiv/shift_nested_sra: `((a >>> b) >>> b)
          // ^ (a << b)` at a = -72, b = 1 encoded 30 where both iverilog and our
          // own emitted Verilog say 158 -- and the side lec got wrong was the
          // hand-written GOLDEN, on all 1024 input pairs of which the two designs
          // agree.
          out_signed     |= a.is_signed;
          break;
        }
        case Ntype_op::Sext: {
          if (pid(0).empty() || pid(1).empty()) {
            return fail("Sext missing a/pos");
          }
          const Val&           a = pid(0)[0];
          // pos must be a constant we can read.
          hhds::Occurrence_pin pos_pin;
          for (const auto& e : node.inp_edges()) {
            if (e.sink.get_port_id() == 1) {
              pos_pin = e.driver;
              break;
            }
          }
          if (pos_pin.is_invalid() || !pos_pin.is_const()) {
            return fail_unsupported("Sext with non-constant position not supported (M1)");
          }
          Dlop posc = gu::const_of(pos_pin);
          if (!posc.is_just_i64()) {
            return fail_unsupported("Sext position too wide (M1)");
          }
          int pos = static_cast<int>(posc.to_just_i64());
          if (pos < 1) {
            return fail_unsupported("Sext position out of range (M1)");
          }
          // pos may exceed the operand's stored width: the bits at/above a.width
          // are a's natural extension (sign-replicated if signed, else zero), so
          // widen `a` to cover [0,pos) rather than failing on an out-of-range
          // slice (mirrors the Get_mask handling above). fit() extends per a's
          // sign, so a[pos-1] then reads the correct new sign bit.
          Term aw  = (pos > a.width) ? fit(a, pos) : a.term;
          Term low = (pos == a.width) ? a.term : bv_extract(tm_, aw, pos - 1, 0);
          if (W <= pos) {
            result = (W == pos) ? low : bv_extract(tm_, low, W - 1, 0);
          } else {
            auto op2 = tm_.mkOp(Kind::BITVECTOR_SIGN_EXTEND, {static_cast<uint32_t>(W - pos)});
            result   = tm_.mkTerm(op2, {low});
          }
          break;
        }
        case Ntype_op::Get_mask: {
          if (pid(0).empty()) {
            return fail("Get_mask missing a");
          }
          const Val&           a = pid(0)[0];
          hhds::Occurrence_pin mask_pin;
          for (const auto& e : node.inp_edges()) {
            if (e.sink.get_port_id() == Ntype::get_sink_pid(op, "mask")) {
              mask_pin = e.driver;
              break;
            }
          }
          if (mask_pin.is_invalid() || !mask_pin.is_const()) {
            return fail_unsupported("Get_mask with non-constant mask not supported (M1)");
          }
          Dlop mask = gu::const_of(mask_pin);
          if (mask.is_just_i64() && mask.to_just_i64() == -1) {
            // zero-extend (sign -> unsigned cast)
            Val zext{a.term, a.width, false};
            result = fit(zext, W);
            break;
          }
          auto range = mask.get_mask_range();  // [begin, end)
          int  rb = range.first, re = range.second;
          if (rb < 0 || re <= rb) {
            return fail_unsupported("Get_mask non-contiguous mask not supported (M1)");
          }
          // Bits at/above the operand width are its sign/zero extension (matching
          // the bit-blast's per-bit extension, lec.md bit-width trap), so widen
          // `a` to cover [rb,re) rather than failing on an out-of-range slice.
          Term aw    = re > a.width ? fit(a, re) : a.term;
          Term slice = bv_extract(tm_, aw, re - 1, rb);
          Val  sv{slice, re - rb, false};
          result = fit(sv, W);
          break;
        }
        case Ntype_op::Set_mask: {
          hhds::Occurrence_pin mask_pin;
          for (const auto& e : node.inp_edges()) {
            if (e.sink.get_port_id() == Ntype::get_sink_pid(op, "mask")) {
              mask_pin = e.driver;
              break;
            }
          }
          if (mask_pin.is_invalid() || !mask_pin.is_const()) {
            return fail_unsupported("Set_mask with non-constant mask not supported (M1)");
          }
          if (pid(0).empty()) {
            return fail("Set_mask missing a");
          }
          const Val& a    = pid(0)[0];
          Dlop       mask = gu::const_of(mask_pin);
          if (mask.is_known_zero()) {
            result = fit(a, W);  // nothing replaced
            break;
          }
          // Full contiguous-mask bit-insert (the bit-blast's output concat): out[i]
          // = (rb<=i<re) ? value[i-rb] : a[i]. `a` and `value` are already stored
          // at their own literal widths; fit() reconciles them to the window/result widths.
          int  Wm    = std::max(1, W);
          auto range = mask.get_mask_range();
          int  rb = range.first, re = range.second;
          if (rb < 0 || re <= rb) {
            // Non-contiguous mask: insert value into each contiguous run, LSB-first
            // (value's compacted bits map onto the set positions in ascending order).
            auto runs = mask.get_mask_range_pairs();  // ascending [begin,end) runs
            if (runs.empty()) {
              result = fit(a, Wm);
              break;
            }
            auto& vvec = pid(Ntype::get_sink_pid(op, "value"));
            if (vvec.empty()) {
              return fail("Set_mask missing value");
            }
            int total = 0;
            for (auto& pr : runs) {
              total += pr.second - pr.first;
            }
            Term aw  = fit(a, Wm);
            Term val = fit(vvec[0], std::max(1, total));
            int  vi  = 0;
            for (auto& pr : runs) {
              int b = pr.first, e = std::min(pr.second, Wm);
              int w = pr.second - pr.first;
              if (b >= Wm) {
                vi += w;
                continue;
              }
              std::vector<Term> parts;  // MSB first
              if (e < Wm) {
                parts.push_back(bv_extract(tm_, aw, Wm - 1, e));
              }
              parts.push_back(bv_extract(tm_, val, vi + (e - b) - 1, vi));
              if (b > 0) {
                parts.push_back(bv_extract(tm_, aw, b - 1, 0));
              }
              Term r = parts.front();
              for (size_t k = 1; k < parts.size(); ++k) {
                r = tm_.mkTerm(Kind::BITVECTOR_CONCAT, {r, parts[k]});
              }
              aw  = r;
              vi += w;
            }
            result = fit(Val{aw, Wm, false}, W);
            break;
          }
          if (rb >= Wm) {
            result = fit(a, Wm);  // replaced region entirely above the result
            break;
          }
          auto& vvec = pid(Ntype::get_sink_pid(op, "value"));
          if (vvec.empty()) {
            return fail("Set_mask missing value");
          }
          int               re_c = std::min(re, Wm);
          Term              aw   = fit(a, Wm);
          std::vector<Term> parts;  // MSB first
          if (re_c < Wm) {
            parts.push_back(bv_extract(tm_, aw, Wm - 1, re_c));  // unchanged high bits of a
          }
          parts.push_back(fit(vvec[0], re_c - rb));  // value's low bits fill the masked window
          if (rb > 0) {
            parts.push_back(bv_extract(tm_, aw, rb - 1, 0));  // unchanged low bits of a
          }
          Term r = parts.front();
          for (size_t k = 1; k < parts.size(); ++k) {
            r = tm_.mkTerm(Kind::BITVECTOR_CONCAT, {r, parts[k]});
          }
          result = fit(Val{r, Wm, false}, W);  // result pin's declared width
          break;
        }
        case Ntype_op::Concat: {
          // SMT-LIB has a native concat, and that is the whole point of the cell:
          // the Set_mask chain it replaces encodes as one nested insert per lane
          // (a 62-deep tower for a wide bus), all of which the bit-blaster then
          // has to see through to prove the assembly is a permutation of its
          // inputs. Here it is one term.
          //
          // The lane table comes from the SHARED decoder: a lane's window width is
          // an explicit operand precisely because it is NOT recoverable from the
          // driver (bits_of is an upper bound that bitwidth/cprop narrow freely,
          // and the value's significant bits are narrower still). Re-deriving a
          // width here would shift every lane above the one it got wrong -- a
          // silent miscompile that reads as a legitimate refutation. Empty means
          // malformed: fail closed rather than encode a shorter bus.
          concat_tbl = gu::concat_lanes(node.base_node());
          if (concat_tbl.empty()) {
            return fail_unsupported("malformed Concat (missing or non-constant lane width operand) (M1)");
          }
          Term acc;
          int  total = 0;
          for (size_t i = 0; i < concat_tbl.size(); ++i) {
            auto& lv = pid(static_cast<hhds::Port_id>(2 * i));  // lane i value = sink pid 2i
            if (lv.empty()) {
              return fail("Concat lane " + std::to_string(i) + " has no value driver");
            }
            // out = SUM_i (value_i mod 2^w_i) << offset_i -- each lane occupies
            // its OWN window, so a negative lane lands as its two's-complement
            // pattern (-1 at w=3 is 0b111). fit() extends per the lane's own
            // sign, which IS `mod 2^w`. An over-wide driver truncates here, per
            // the Concat cell contract; the explicit window still fixes every
            // neighboring lane's offset, so no lane can shift.
            Term lt  = fit(lv[0], concat_tbl[i].width);
            acc      = acc.isNull() ? lt : tm_.mkTerm(Kind::BITVECTOR_CONCAT, {acc, lt});
            total   += concat_tbl[i].width;
          }
          // The result is always non-negative and carries exactly sum(w) bits.
          // Widen-only, never narrow: an
          // under-stamped pin (or an unstamped one, defaulted to W=1 above) would
          // otherwise silently drop the most significant lanes.
          W          = std::max(W, total);
          out_signed = false;
          result     = fit(Val{acc, total, false}, W);
          break;
        }
        case Ntype_op::AttrSet: {
          // pass-through of the parent driver (pid0).
          if (pid(0).empty()) {
            return fail("AttrSet without parent driver");
          }
          result = fit(pid(0)[0], W);
          break;
        }
        case Ntype_op::Mux:
        case Ntype_op::Hotmux: {
          // pid0 = selector; values on pid 1..N.
          if (pid(0).empty()) {
            return fail("Mux/Hotmux missing selector");
          }
          const Val&       sel = pid(0)[0];
          std::vector<Val> arms;
          for (hhds::Port_id p = 1;; ++p) {
            auto it = by_pid.find(p);
            if (it == by_pid.end()) {
              break;
            }
            arms.push_back(it->second.front());
          }
          if (arms.empty()) {
            return fail("Mux/Hotmux has no arms");
          }
          // The result pin is the Mux contract. A typed result may deliberately
          // be narrower than an arm, in which case selecting that arm truncates
          // it to W bits. fit() below models both that truncation and extension;
          // do not widen W from producer metadata and defeat the sink type.
          //
          // UNSTAMPED is not "1 bit", though: `W` was defaulted to 1 above for a
          // pin with no bits attribute at all (readers that size a `casez`
          // result from the SELECTOR, pass.bitfuzz which deliberately strips the
          // stamp). Truncating whole data arms to bit 0 there is a silent false
          // verdict, so an unstamped pin takes its width from its arms.
          if (gu::bits_of(dpin) == 0) {
            for (const auto& a : arms) {
              W = std::max(W, a.width);
            }
          }
          // default else = last arm (covers in-range exactly + out-of-range det.)
          result = fit(arms.back(), W);
          for (int k = static_cast<int>(arms.size()) - 2; k >= 0; --k) {
            Term cond = mux_arm_cond(tm_, op == Ntype_op::Hotmux, sel.term, sel.width, k);
            if (cond.isNull()) {
              return fail_unsupported("Hotmux arm index past the selector width");
            }
            result = tm_.mkTerm(Kind::ITE, {cond, fit(arms[k], W), result});
          }
          break;
        }
        default: return fail_unsupported("unsupported op '" + std::string(Ntype::get_name(op)) + "' (M1)");
      }

      if (result.isNull()) {
        return fail("op '" + std::string(Ntype::get_name(op)) + "' produced no term");
      }
      Val out_val{result, W, out_signed};
      if (x_dontcare_) {
        // Undef bit-plane propagation (formal.lec.gold_x=ignore, REF side). Exact for
        // Mux/Hotmux (an ITE over the arms' planes — the invalid-payload idiom
        // `valid ? data : X` stays checkable on the valid path); conservative
        // whole-value smear for every other op with an unknown operand.
        bool any_undef = false;
        for (const auto& v : all) {
          if (!v.x_mask.isNull()) {
            any_undef = true;
            break;
          }
        }
        if (any_undef) {
          auto zero_w = tm_.mkBitVector(static_cast<uint32_t>(W), 0);
          auto ones_w = tm_.mkTerm(Kind::BITVECTOR_NOT, {zero_w});
          if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
            const Val&       sel = pid(0)[0];
            std::vector<Val> arms;
            for (hhds::Port_id p = 1;; ++p) {
              auto it = by_pid.find(p);
              if (it == by_pid.end()) {
                break;
              }
              arms.push_back(it->second.front());
            }
            if (arms.empty()) {
              out_val.x_mask = ones_w;
            } else {
              auto arm_xm = [&](const Val& a) -> Term {
                Val aw   = a;
                aw.width = a.width;
                Term u   = fit_x_mask_to(tm_, aw, W);
                return u.isNull() ? zero_w : u;
              };
              Term u = arm_xm(arms.back());
              for (int k = static_cast<int>(arms.size()) - 2; k >= 0; --k) {
                Term cond = mux_arm_cond(tm_, op == Ntype_op::Hotmux, sel.term, sel.width, k);
                if (cond.isNull()) {
                  return fail_unsupported("Hotmux arm index past the selector width");
                }
                u = tm_.mkTerm(Kind::ITE, {cond, arm_xm(arms[k]), u});
              }
              // A non-null selector plane is not necessarily asserted: it can
              // be a data-dependent ITE which is zero on this path. Smearing
              // the output merely because the term exists turns a conditional
              // X into an unconditional don't-care and can vacuously prove a
              // bad circuit. Conservatively smear only while a selector bit is
              // dynamically unknown; otherwise keep the selected arm's plane.
              if (!sel.x_mask.isNull()) {
                // sel.x_mask is already at sel's own width (fit_x_mask_to would
                // be the identity here), so compare it against a zero of that
                // same width -- clamped like the conservative arm below.
                auto zero_sel    = tm_.mkBitVector(static_cast<uint32_t>(sel.width < 1 ? 1 : sel.width), 0);
                Term sel_unknown = tm_.mkTerm(Kind::DISTINCT, {sel.x_mask, zero_sel});
                u                = tm_.mkTerm(Kind::ITE, {sel_unknown, ones_w, u});
              }
              out_val.x_mask = u;
            }
          } else if (op == Ntype_op::Set_mask && !pid(Ntype::get_sink_pid(op, "mask")).empty()
                     && pid(Ntype::get_sink_pid(op, "mask"))[0].x_mask.isNull() && !pid(Ntype::get_sink_pid(op, "value")).empty()
                     && pid(Ntype::get_sink_pid(op, "value"))[0].x_mask.isNull() && !pid(0).empty()) {
            // A bit-insert is EXACT on the X plane: every lane the (constant)
            // mask selects is OVERWRITTEN by `value`, so it stops being unknown;
            // the rest keep `a`'s plane. The conservative smear below would mark
            // the WHOLE result unknown — and since the readers emit a multi-bit
            // output as `out = 0ub????????` followed by one Set_mask per bit,
            // that smear leaves EVERY bit X and the output compares NOTHING. A
            // real difference on such an output then comes back PROVEN (cva6's
            // bug1 `tag_cmp.hit_way_o`, which yosys refutes). Only the exact,
            // no-X-in-`value` case is claimed here; anything else still smears.
            Term ax        = fit_x_mask_to(tm_, pid(0)[0], W);
            Term keep      = tm_.mkTerm(Kind::BITVECTOR_NOT, {fit(pid(Ntype::get_sink_pid(op, "mask"))[0], W)});
            out_val.x_mask = ax.isNull() ? zero_w : tm_.mkTerm(Kind::BITVECTOR_AND, {ax, keep});
          } else if (Term gx = exact_get_mask_x_plane(tm_, node, op, pid(0), W); !gx.isNull()) {
            // A bit-EXTRACT is exact on the X plane for exactly the reason the
            // Concat arm below is: unknowns are POSITIONAL, so a slice keeps the
            // plane of the bits it actually reads and nothing else. The
            // conservative smear marked the WHOLE slice unknown, so reading any
            // window of a bus that carries an unknown ANYWHERE made every
            // extracted bit X -- and an output assembled from such slices then
            // compares NOTHING, i.e. a false PROVEN. That is `lec_combarray`'s
            // corrupted chunk: `mem[b][63:32] = din[b][31:0]` differs from the
            // golden in bits the unknown fill never reaches, yet the whole
            // 128-bit `dout` came back don't-care and the miter was vacuous.
            out_val.x_mask = gx;
          } else if (Term sx = exact_sext_x_plane(tm_, node, op, pid(0), W); !sx.isNull()) {
            // Sign extension is positional too: the replicated copies are unknown
            // exactly when the kept sign bit is (that is what fit_x_mask_to's
            // signed branch already does for a plain width fit).
            out_val.x_mask = sx;
          } else if (op == Ntype_op::Concat && !concat_tbl.empty()) {
            // A Concat is EXACT on the X plane, and it has to be: unknowns here are
            // POSITIONAL, so a lane's unknown bits belong in that lane's window and
            // nowhere else. The conservative smear below would mark the whole
            // assembled bus unknown -- the same false-PROVEN mechanism the Set_mask
            // arm above documents (an output whose every bit reads X compares
            // NOTHING), and a Concat IS the output-assembly cell, so smearing it
            // would relight that bug on every packed bus rather than on the readers'
            // per-bit Set_mask idiom alone.
            Term xacc;
            int  xw = 0;
            for (size_t i = 0; i < concat_tbl.size(); ++i) {
              const int lw = concat_tbl[i].width;
              auto&     lv = pid(static_cast<hhds::Port_id>(2 * i));
              Term      lx;
              if (!lv.empty()) {
                // The plane must extend the way the VALUE did (fit() in the arm
                // above): a sign-extended lane replicates an unknown msb, a
                // zero-extended one appends known-0 bits.
                lx = fit_x_mask_to(tm_, lv[0], lw);
              }
              if (lx.isNull()) {
                lx = tm_.mkBitVector(static_cast<uint32_t>(lw), 0);  // fully known lane
              }
              xacc  = xacc.isNull() ? lx : tm_.mkTerm(Kind::BITVECTOR_CONCAT, {xacc, lx});
              xw   += lw;
            }
            // Any over-wide unsigned pin stamp is known zero above the concat.
            out_val.x_mask = xacc.isNull() ? zero_w : fit(Val{xacc, xw, false}, W);
          } else if (op == Ntype_op::Not && !all.empty()) {
            // Bitwise NOT preserves the unknown positions exactly.
            out_val.x_mask = fit_x_mask_to(tm_, all.front(), W);
          } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
            // Exact per-lane four-state propagation. XOR is unknown wherever
            // any operand is unknown. AND/OR additionally honor their
            // controlling values (0 & X == 0, 1 | X == 1); the previous
            // whole-word smear discarded those known results and let real LEC
            // mismatches hide behind an unrelated X lane.
            Term any_x      = zero_w;
            Term controlled = zero_w;
            for (const auto& v : all) {
              Term vx = fit_x_mask_to(tm_, v, W);
              if (vx.isNull()) {
                vx = zero_w;
              }
              any_x = tm_.mkTerm(Kind::BITVECTOR_OR, {any_x, vx});
              if (op != Ntype_op::Xor) {
                Term known = tm_.mkTerm(Kind::BITVECTOR_NOT, {vx});
                Term value = fit(v, W);
                Term ctrl = op == Ntype_op::And ? tm_.mkTerm(Kind::BITVECTOR_AND, {tm_.mkTerm(Kind::BITVECTOR_NOT, {value}), known})
                                                : tm_.mkTerm(Kind::BITVECTOR_AND, {value, known});
                controlled = tm_.mkTerm(Kind::BITVECTOR_OR, {controlled, ctrl});
              }
            }
            out_val.x_mask = op == Ntype_op::Xor
                                 ? any_x
                                 : tm_.mkTerm(Kind::BITVECTOR_AND, {any_x, tm_.mkTerm(Kind::BITVECTOR_NOT, {controlled})});
          } else {
            // any operand dynamically unknown anywhere -> whole result unknown
            Term any;
            for (const auto& v : all) {
              if (v.x_mask.isNull()) {
                continue;
              }
              auto zero_v = tm_.mkBitVector(static_cast<uint32_t>(v.width < 1 ? 1 : v.width), 0);
              Term nz     = tm_.mkTerm(Kind::DISTINCT, {v.x_mask, zero_v});
              any         = any.isNull() ? nz : tm_.mkTerm(Kind::OR, {any, nz});
            }
            out_val.x_mask = tm_.mkTerm(Kind::ITE, {any, ones_w, zero_w});
          }
        }
      }
      pin2val[pinkey(dpin)] = out_val;
      {
        static const std::set<uint64_t> dbg_nodes = [] {
          std::set<uint64_t> s;
          if (const char* e = std::getenv("LEC_DBG_NIDS")) {
            std::string str(e), cur;
            for (char c : str) {
              if (c == ',') {
                if (!cur.empty()) {
                  s.insert(std::stoull(cur));
                }
                cur.clear();
              } else {
                cur += c;
              }
            }
            if (!cur.empty()) {
              s.insert(std::stoull(cur));
            }
          }
          return s;
        }();
        if (!dbg_nodes.empty()) {
          uint64_t nid = static_cast<uint64_t>(node.get_debug_nid());
          if (dbg_nodes.count(nid)) {
            out.outputs[std::string("\x03"
                                    "dbg:")
                        + std::string(prefix) + std::to_string(nid)] = Val{result, W, out_signed};
          }
        }
      }
      done.insert(nodekey(node));
      progress = true;
    }
  }
  // Fixpoint converged. Any combinational node still unresolved is a genuine
  // structural error (a real combinational cycle, or a driver the encoder can't
  // build) — surface it instead of silently dropping logic.
  for (auto node : g->occurrences(opaque).nodes(hhds::Node_order::forward)) {
    auto op = gu::type_op_of(node);
    if (!node.base_node().has_out_edges() || op == Ntype_op::Flop || op == Ntype_op::Memory) {
      continue;
    }
    if (phased && op == Ntype_op::Latch) {
      continue;  // scheduled state endpoint, cut like a Flop (Q seeded, next-state emitted)
    }
    if (op == Ntype_op::Sub && node.get_subnode_graph() != nullptr) {
      continue;  // descended
    }
    if (!done.contains(nodekey(node))) {
      // Diagnose the ROOT of the stall, not the first undone node in walk
      // order: follow undone operands until either (a) a node repeats — a real
      // word-level cycle, print its members — or (b) a node whose blocking
      // operand is not another undone node (an unmapped input-pin name, an
      // opaque driver, ...), print that operand.
      std::vector<std::string>      chain;
      absl::flat_hash_set<uint64_t> seen;
      auto                          cur       = node;
      bool                          diagnosed = false;
      std::string                   diag;
      while (!diagnosed) {
        if (!seen.insert(static_cast<uint64_t>(cur.get_debug_nid())).second) {
          diag = "WORD-LEVEL CYCLE through: ";
          for (const auto& c : chain) {
            diag += c + " -> ";
          }
          diag      += diagnostic_node_name(g, cur);
          diagnosed  = true;
          break;
        }
        chain.push_back(diagnostic_node_name(g, cur));
        bool hopped = false;
        for (const auto& e : cur.inp_edges()) {
          const auto& drv = e.driver;
          if (drv.is_const()) {
            continue;
          }
          if (gu::is_graph_input_pin(drv)) {
            std::string in_name{drv.get_pin_name()};
            if (!out.inputs.contains(in_name)) {
              diag      = "input pin '" + in_name + "' of '" + gu::debug_name(cur) + "' is not among the declared inputs";
              diagnosed = true;
            }
            continue;
          }
          auto mn = drv.get_master_node();
          if (!done.contains(nodekey(mn))) {
            cur    = mn;
            hopped = true;
            break;  // follow the first undone operand
          }
          // A producer can be structurally "done" without carrying a DATA
          // value: Clock_cell and the identity wrappers on its output are
          // deliberately timing-only.  If such a pin reaches an ordinary
          // data operator, driver_val() is still unresolved even though the
          // producer node is in `done`.  Calling that "all operands resolved"
          // hides the exact edge which crossed from the clock domain into the
          // data cone (and made Minion's intpipe_csr_file look like an
          // inexplicable deferred Mux/Or). Diagnose the missing pin directly.
          bool drv_ok = true;
          (void)driver_val(drv, drv_ok);
          if (!drv_ok) {
            diag  = "data input of '" + gu::debug_name(cur) + "' is driven by timing-only or unencoded pin '" + gu::debug_name(mn)
                    + "' (op " + std::string(Ntype::get_name(gu::type_op_of(mn))) + ", key " + missing_driver_key + ")";
            diag += "; consumer inputs{";
            bool first = true;
            for (const auto& ie : cur.inp_edges()) {
              if (!first) {
                diag += ", ";
              }
              first  = false;
              diag  += "p" + std::to_string(static_cast<int>(ie.sink.get_port_id())) + "="
                       + gu::debug_name(ie.driver.get_master_node()) + ":"
                       + std::string(Ntype::get_name(gu::type_op_of(ie.driver.get_master_node())));
            }
            diag  += "}; consumer outputs{";
            first  = true;
            for (const auto& oe : cur.out_edges()) {
              if (!first) {
                diag += ", ";
              }
              first  = false;
              diag  += gu::debug_name(oe.sink.get_master_node()) + ":"
                       + std::string(Ntype::get_name(gu::type_op_of(oe.sink.get_master_node()))) + ".p"
                       + std::to_string(static_cast<int>(oe.sink.get_port_id()));
            }
            diag      += "}";
            diagnosed  = true;
            break;
          }
        }
        if (diagnosed) {
          break;
        }
        if (!hopped) {
          diag      = "'" + gu::debug_name(cur) + "' has all operands resolved yet never encoded (deferred op?)";
          diagnosed = true;
        }
      }
      // A REFUSAL, not a give-up: the encoder cannot ORDER this cone (a real
      // word-level combinational cycle, or a driver it cannot build), and no
      // amount of extra budget changes that. It must therefore ride
      // `unsupported`, which hard-fails by NAME regardless of formal.strict
      // (exit 7, "could not decide"). It must NOT ride `nothing_compared`:
      // the driver maps that to `equiv_fail` (exit 10, "here is a
      // counterexample"), so flagging it there would report a structural
      // refusal — and, worse, a plain `formal.timeout` budget-out — as a
      // DISPROOF, with a headline claiming the module is empty. rc 7 and rc 10
      // must never be conflated (pass/lec/tests/lec_verdict_policy_test.sh).
      return fail_unsupported("operand of '" + gu::debug_name(node)
                              + "' has no encodable driver (combinational cycle?); root: " + diag);
    }
  }

  // ---- 2f-verify property obligations (gated: set_emit_props). Each `fproperty`
  // Sub carries a 1-bit `cond` sink plus "<kind>\x1f<loc>\x1f<msg>" in its name
  // attr (see graph_util::fproperty_module_name). Its cond CONE was encoded by
  // the fixpoint above (the cone nodes have out-edges; the fproperty itself has
  // none and was skipped), so the cond driver resolves here. Emit it as a
  // synthetic output "\x04prop:<occ>\x1f<kind>\x1f<loc>\x1f<msg>" — occ is a
  // walk-order counter, deterministic across repeated encodes of the same graph,
  // so the engine's per-cycle encodes agree on which property is which.
  if (emit_props_) {
    int prop_occ = 0;
    for (auto node : g->occurrences(opaque).nodes(hhds::Node_order::forward)) {
      if (gu::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      auto sio = node.get_subnode_io();
      if (sio == nullptr || sio->get_name() != gu::fproperty_module_name) {
        continue;
      }
      // Resolve the cond driver across instance boundaries via the HIER
      // inp_edges (get_driver_of_sink_name stops at a sub's GraphIO pin).
      const auto           cond_pid = sio->get_input_port_id("cond");
      hhds::Occurrence_pin cond_drv;
      for (const auto& e : node.inp_edges()) {
        if (e.sink.get_port_id() == cond_pid) {
          cond_drv = e.driver;
          break;
        }
      }
      const int occ = prop_occ++;  // count even a skipped prop: keys stay walk-stable
      if (cond_drv.is_invalid()) {
        continue;  // unconnected cond: nothing to prove (tolg never emits this)
      }
      bool ok = true;
      Val  cv = driver_val(cond_drv, ok);
      if (!ok) {
        return fail("fproperty '" + gu::debug_name(node) + "' cond has no encodable driver");
      }
      auto        nm  = gu::node_name_of(node);
      std::string raw = nm.empty() ? std::string{"assert"} : std::string{nm};
      out.outputs[std::string("\x04") + "prop:" + std::to_string(occ) + "\x1f" + raw] = cv;
      // Which assumes are ACTIVE hypotheses (see Encoded::prop_active_assume).
      // Record the decision by occ so the design-assumption asserter never has to
      // re-derive it from the name alone.
      //   * `assume_nocheck` is an environment contract BY SPELLING: the user
      //     declared it never to be checked, so it holds whether or not
      //     pass.formal ran. Gating it on `proven` silently dropped every assume
      //     on a side built at O0 (or a `lg:` library loaded straight into lec),
      //     turning a contract-excluded input into a REFUTED counterexample.
      //   * a CHECKED `assume` still needs the `proven` stamp — undischarged, it
      //     would restrict the compared space without justification.
      // Note the spelling rule deliberately survives pass.formal RETRACTING a
      // jointly-contradictory set (which clears `proven` so the runtime checks
      // come back): lec must still assert the pair, see the UNSAT hypothesis set
      // and report CONTRADICTORY rather than hand out an ordinary verdict.
      const std::string_view raw_kind{std::string_view{raw}.substr(0, raw.find('\x1f'))};
      // A `proven` stamped by the hierarchy preflight (kFormalAssumeHier) holds
      // only under the parents' bindings in the design that stamped it; it does
      // not license restricting the compared space of this def on its own.
      const bool proven_here = gu::has_proven(node.base_node()) && gu::proven_of(node.base_node()) != gu::kFormalAssumeHier;
      if (is_assume_kind(raw_kind) && (raw_kind == "assume_nocheck" || proven_here)) {
        out.prop_active_assume.insert(occ);
      }
      // A Sub occurrence's path includes the site itself.  Consequently a
      // property authored in the selected root has exactly one step (the
      // fproperty site), while a property in a child has the parent call-site
      // step(s) followed by the fproperty site.
      const auto steps = node.get_occurrence_index().path.steps();
      if (steps.size() <= 1) {
        out.prop_top.insert(occ);
      } else {
        // Name only the containing occurrence.  get_hier_name() includes the
        // fproperty site's packed "kind<US>loc<US>msg" name, which is useful
        // for graph debugging but is not an instance path and leaks delimiter
        // bytes into diagnostics.  Build the parent path explicitly and keep
        // anonymous sites distinguishable with a stable definition fallback.
        std::string instance;
        auto*       lib = g->get_io() ? g->get_io()->get_library() : nullptr;
        for (size_t si = 0; si + 1 < steps.size(); ++si) {
          const auto& step = steps[si];
          std::string segment;
          if (lib != nullptr) {
            if (auto parent = lib->get_graph(step.subnode.gid)) {
              auto site = parent->get_node(hhds::Class_index{step.subnode.value});
              if (site.is_valid() && !site.get_name().empty()) {
                segment = std::string{site.get_name()};
              }
            }
          }
          if (segment.empty()) {
            segment = "@" + std::to_string(static_cast<uint64_t>(step.subnode.gid)) + ":"
                      + std::to_string(static_cast<uint64_t>(step.subnode.value));
          }
          if (step.ordinal) {
            segment += "__li" + std::to_string(*step.ordinal);
          }
          if (!instance.empty()) {
            instance += '.';
          }
          instance += segment;
        }
        out.prop_instance.emplace(occ, std::move(instance));
      }

      // R1 Phase 2 — the optional raw GUARD of a property written inside an
      // `if`/`match` arm, emitted as "\x04guard:<occ>" so prove_properties can
      // ask whether the antecedent is ever satisfiable (a guard that can never
      // hold makes `guard implies cond` trivially Proven — true, but it checked
      // nothing). DIAGNOSTIC ONLY: `cond` above already carries the folded
      // implication, so dropping this emission can never change a verdict, and
      // every failure path below simply omits it. The distinct `guard:` prefix
      // is invisible to both `\x04prop:` parsers.
      if (!sio->has_input("guard")) {
        continue;
      }
      const auto           guard_pid = sio->get_input_port_id("guard");
      hhds::Occurrence_pin guard_drv;
      for (const auto& e : node.inp_edges()) {
        if (e.sink.get_port_id() == guard_pid) {
          guard_drv = e.driver;
          break;
        }
      }
      if (guard_drv.is_invalid()) {
        continue;  // unguarded property: nothing to report
      }
      bool gok = true;
      Val  gv  = driver_val(guard_drv, gok);
      if (gok) {
        out.outputs[std::string("\x04") + "guard:" + std::to_string(occ)] = gv;
      }
    }
  }

  // ---- 2f-verify submodule port taps (gated: set_port_taps). For every Sub
  // instance whose hier name was requested, emit each resolvable input/output
  // port value as a synthetic output "\x05tap:<inst>.<port>", fitted to the
  // port DECLARATION's width — the same width the verify CLI types the monitor
  // input with, so a formal block binds a submodule port exactly like a top
  // output. Best-effort per port: an unconnected / unresolved port simply has
  // no tap (the CLI validated the referenced names upfront; the engine's bind
  // step fails loudly if a referenced tap is missing).
  if (port_taps_ != nullptr && !port_taps_->empty()) {
    for (auto node : g->occurrences(opaque).nodes(hhds::Node_order::forward)) {
      if (gu::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      const std::string hname{node.get_hier_name()};
      if (!port_taps_->contains(hname)) {
        continue;
      }
      auto sio = node.get_subnode_io();
      if (sio == nullptr) {
        continue;
      }
      auto emit_tap = [&](const std::string& port, const Val& v, int decl_w, bool decl_sgn) {
        const int w                                  = decl_w > 0 ? decl_w : (v.width > 0 ? v.width : 1);
        out.outputs["\x05tap:" + hname + "." + port] = Val{fit(v, w), w, decl_sgn};
      };
      // INPUT ports: the parent-side driver, resolved across the instance
      // boundary via the HIER inp_edges (the fproperty cond pattern).
      for (const auto& d : sio->get_input_pin_decls()) {
        const auto           pid = sio->get_input_port_id(d.name);
        hhds::Occurrence_pin drv;
        for (const auto& e : node.inp_edges()) {
          if (e.sink.get_port_id() == pid) {
            drv = e.driver;
            break;
          }
        }
        if (drv.is_invalid()) {
          continue;
        }
        bool ok = true;
        Val  v  = driver_val(drv, ok);
        if (ok) {
          emit_tap(d.name, v, static_cast<int>(d.bits), !sio->is_unsign(d.name));
        }
      }
      // OUTPUT ports: in a HIER walk the instance pin itself carries no value —
      // edges hop THROUGH boundaries to real leaves. Recover the internal
      // driver from any consumer: the consumer's inp_edge whose sink is this
      // same pin resolves its driver DOWN into the callee, to the real
      // (memoized) leaf. An output nobody reads has no tap.
      auto same_pin = [](const hhds::Occurrence_pin& a, const hhds::Occurrence_pin& b) { return a == b; };
      for (const auto& d : sio->get_output_pin_decls()) {
        const auto pid = sio->get_output_port_id(d.name);
        bool       ok  = false;
        Val        v;
        if (auto dp = node.get_driver_pin(pid); !dp.is_invalid()) {
          v = driver_val(dp, ok);  // flat/collapsed case: the pin value exists
        }
        if (!ok) {
          for (const auto& e : node.out_edges()) {
            if (e.driver.get_port_id() != pid || e.sink.is_invalid()) {
              continue;
            }
            auto consumer = e.sink.get_master_node();
            for (const auto& ce : consumer.inp_edges()) {
              if (same_pin(ce.sink, e.sink)) {
                v = driver_val(ce.driver, ok);
                break;
              }
            }
            if (ok) {
              break;
            }
          }
        }
        if (ok) {
          emit_tap(d.name, v, static_cast<int>(d.bits), !sio->is_unsign(d.name));
        }
      }
    }
  }

  // ---- M2 next-state functions. For each cut flop emit the value it latches
  // next cycle as a synthetic output keyed "<nxt><statekey>", so the miter
  // compares the two designs' next-state for every corresponding register (the
  // inductive step: equal current state + equal inputs => equal next state &
  // outputs). N = reset_active ? initial : (enable ? din : Q). reset_pin is a
  // shared primary input, so the same query covers the reset/base case.
  //
  // hier_sink_driver above uses occurrence edges rather than class-local pins,
  // which is required for a state endpoint inside an instance.
  // ---- CLOCK AWARENESS (todo/livehd/2f-latch M4) ----------------------------
  // This encoder never read `clock_pin` or `posclk`: the sink classifier
  // DISCARDED them, so the transition function was
  //     N = ITE(reset, initial, ITE(enable, din, Q))
  // and `lhd lec` FALSELY PROVED gated == ungated and two-clock == one-clock.
  //
  // SCOPE, and why it is this narrow. The BMC's reset prologue assumes ONE STEP
  // = ONE COMMIT; gate a commit on a real edge and a prologue step where the
  // clock does not toggle commits NOTHING, so a SYNC reset never lands. (Root
  // cause of ~40 false REFUTEDs on an earlier attempt: a one-flop design vs its
  // own round-trip, where the Pyrope side carries a reset_pin and the
  // round-tripped Verilog spells the same reset as `always @(posedge clock) if
  // (reset)`.) So gating is applied ONLY where that assumption is ALREADY
  // false — never to a plain flop on the design's single clock:
  //
  //   * an ICG clock cone `<clock-input> & <enables>`: it rises exactly on the
  //     reference rises where the enables are high, and one step IS one
  //     reference period, so it folds to "commit iff the enables are true". No
  //     edge detection, no prologue interaction. (Same fold sim uses.)
  //   * a design with TWO OR MORE distinct clock INPUT nets: "the one clock"
  //     does not exist, so each flop commits on a detected edge of its own net.
  //
  // A single-clock design gets NO gating term at all and encodes byte-for-byte
  // as before — which is what bounds the blast radius to the designs this is
  // meant to change.
  //
  // NOT closed by this: negedge-vs-posedge on the SAME clock. Distinguishing
  // those needs sub-cycle resolution (a rise phase then a fall phase, with the
  // negedge side reading post-rise values), i.e. the two-phase tick sim got in
  // M5. In a relational encoding that means encoding each step TWICE with the
  // posedge Qs re-seeded to their next-state — real work, and deliberately not
  // attempted here.
  // Distinct clock INPUT nets across this design's flops.
  absl::flat_hash_set<std::string> clk_inputs;
  for (const auto& fn : flops) {
    for (const auto& e : fn.inp_edges()) {
      if (e.sink.get_port_id() != Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin")) {
        continue;
      }
      if (auto ci = resolve_clk_input(e.driver); !ci.is_invalid()) {
        clk_inputs.insert(std::string(gu::pin_name_of(ci)));
      }
      break;
    }
  }
  const bool multi_clock = clk_inputs.size() >= 2;

  for (size_t fi = 0; fi < flops.size(); ++fi) {
    const auto& node  = flops[fi];
    const int   depth = flop_depths[fi];
    auto        qpin  = node.get_driver_pin(0);
    int         w     = gu::real_width(qpin);
    if (w == 0) {
      w = 1;
    }
    bool       sgn = !gu::is_unsign(qpin);
    const Val& qv  = pin2val[pinkey(qpin)];  // Q (output stage) current-state, seeded above
    bool       ok  = true;

    // ---- PHASE SCHEDULE (2f-lec / 2f-latch M10) -------------------------------
    // Which microstep does this endpoint commit in, and is it THIS one? An
    // endpoint outside its batch simply HOLDS, which is how a later batch in the
    // same source period observes what an earlier one committed.
    const Phase_endpoint* pe         = nullptr;
    bool                  commit_now = true;
    if (phased) {
      if (auto pit = phase_plan_->ep.find(box_node_key(node)); pit != phase_plan_->ep.end()) {
        pe = &pit->second;
      }
      const int my_ms = pe != nullptr ? static_cast<int>(pe->phase) : static_cast<int>(Phase::Rise);
      commit_now      = single_step() || my_ms == microstep_;
    }
    // A CLOCK-ROLE latch's gate is TIMING, not data: the schedule already placed
    // it in the microstep where its window closes, so the gate must be ABSORBED
    // (commit unconditionally there, din = the transparent arm of tolg's hold
    // mux) and never AND-ed into the enable. AND-ing it evaluates the clock as a
    // free data input once per microstep -- measured as latch outputs stuck at
    // `x` under iverilog, and as a retyped flop with no clock at all.
    const bool absorb_gate     = pe != nullptr && pe->clock_role_latch;
    // Hier-safe transparent arm: latch_contract's version resolves class-local
    // pins, which carry no hier position and so would miss pin2val for a latch
    // inside an instance.
    auto       transparent_arm = [&](const hhds::Occurrence_node& n) -> hhds::Occurrence_pin {
      auto q  = n.get_driver_pin(0);
      auto dd = hier_sink_driver(n, "din");
      if (dd.is_invalid() || dd.is_const() || gu::is_graph_input_pin(dd)) {
        return {};
      }
      auto mux = dd.get_master_node();
      if (gu::type_op_of(mux) != Ntype_op::Mux) {
        return {};  // raw yosys D/EN shape: `din` IS already the transparent value
      }
      bool                 has_q_arm = false;
      hhds::Occurrence_pin other;
      for (const auto& e : mux.inp_edges()) {
        if (e.sink.get_port_id() == 0) {
          continue;  // the selector is the gate, not an arm
        }
        if (!e.driver.is_invalid() && e.driver.get_class_index() == q.get_class_index()) {
          has_q_arm = true;
        } else if (other.is_invalid()) {
          other = e.driver;
        }
      }
      if (has_q_arm) {
        return other;
      }
      return {};
    };

    // din: the value the FIRST stage latches each cycle (no enable/reset yet).
    bool                 has_din = false;
    Term                 din;
    Term                 din_xm;  // undef plane of din, fitted to w (null = fully known)
    hhds::Occurrence_pin din_pin = hier_sink_driver(node, "din");
    if (absorb_gate) {
      if (auto arm = transparent_arm(node); !arm.is_invalid()) {
        din_pin = arm;
      }
    }
    if (!din_pin.is_invalid()) {
      Val dv = driver_val(din_pin, ok);
      if (!ok) {
        return fail("flop '" + gu::debug_name(node) + "' din not encodable");
      }
      din     = fit(dv, w);
      din_xm  = fit_x_mask_to(tm_, dv, w);
      has_din = true;
    }

    // enable: a low write-enable holds the whole shift register (no din=q
    // feedback mux in the graph). const-false => the register never writes.
    bool enable_const_false = false;
    bool has_enable         = false;
    Term en_hot;
    if (auto en_d = absorb_gate ? hhds::Occurrence_pin{} : hier_sink_driver(node, "enable"); !en_d.is_invalid()) {
      if (en_d.is_const()) {
        enable_const_false = gu::const_of(en_d).is_known_false();
      } else {
        Val ev = driver_val(en_d, ok);
        if (!ok) {
          return fail("flop '" + gu::debug_name(node) + "' enable not encodable");
        }
        en_hot     = tm_.mkTerm(Kind::DISTINCT, {ev.term, bv_const(tm_, ev.width, 0)});
        has_enable = true;
      }
    }

    // reset: a non-const reset overrides every stage with the initial value when
    // asserted (cgen resets all stages identically).
    //
    // ASYNC vs SYNC matters ONLY under the phase schedule. At P == 1 one step is
    // one commit, so applying the reset outside the commit gate is exact for
    // both. At P == 4 it is not: a SYNC reset lands on the endpoint's own clock
    // edge, so applying it on all four microsteps resets a negedge flop three
    // microsteps early -- and against a front-end that spells the same reset
    // inside `din` (`if reset { r = init }`) that is a real, and spurious,
    // divergence. Measured on tests/equiv/flop_reset_matrix, whose six flops
    // cover the async/sync x posedge/negedge matrix.
    bool async_reset = false;
    if (auto a_d = hier_sink_driver(node, "async"); a_d.is_const()) {
      async_reset = !gu::const_of(a_d).is_known_false();
    }
    bool has_reset = false;
    Term rst_hot;
    Term initv;
    if (auto rst_d = hier_sink_driver(node, "reset_pin"); !rst_d.is_invalid() && !rst_d.is_const()) {
      Val rv = driver_val(rst_d, ok);
      if (!ok) {
        return fail("flop '" + gu::debug_name(node) + "' reset not encodable");
      }
      Term rbit     = tm_.mkTerm(Kind::DISTINCT, {rv.term, bv_const(tm_, rv.width, 0)});
      bool negreset = false;
      if (auto neg_d = hier_sink_driver(node, "negreset"); neg_d.is_const()) {
        negreset = !gu::const_of(neg_d).is_known_false();
      }
      rst_hot = negreset ? tm_.mkTerm(Kind::NOT, {rbit}) : rbit;
      initv   = bv_const(tm_, w, 0);
      if (auto init_d = hier_sink_driver(node, "initial"); !init_d.is_invalid()) {
        Val iv = driver_val(init_d, ok);
        if (ok) {
          initv = fit(iv, w);
        }
      }
      has_reset = true;
    }

    // Stage current-state values, din-side -> Q-side: [internals..., Q]. A
    // depth-1 flop is just [Q] (the original single-flop behavior). Each stage k
    // latches the previous stage's value (stage 0 latches din), gated by the
    // shared enable/reset.
    const std::vector<Val>& internals = flop_internals[fi];
    auto stage_cur       = [&](int k) -> const Term& { return (k + 1 < depth) ? internals[static_cast<size_t>(k)].term : qv.term; };
    const std::string nm = flop_key(node.get_hier_name());
    // F7 source-map: resolve this flop's declaration to "file:line" once (the key
    // feeds every stage's \x01nxt: output and the downstream wit_state cuts). The
    // srcid rides tolg for a Pyrope node and the cgen ECMA-426 sourcemap for a
    // Verilog one; absent ⇒ no entry (the witness just renders the bare name).
    if (out.src_of_key.find(nm) == out.src_of_key.end()) {
      if (auto ref = node.base_node().attr(hhds::attrs::srcid); ref.has()) {
        auto span = g->source_locator().resolve_span(ref.get());
        if (!span.file.empty() && span.start_line.has_value()) {
          out.src_of_key[nm] = span.file + ":" + std::to_string(*span.start_line);
        }
      }
    }
    // Commit condition for the two shapes where "one step = one commit" is
    // already false (see the SCOPE note above). Null => commit every step, i.e.
    // exactly the previous behavior.
    Term       commits;
    // Set by a branch that fully MODELLED the clock even though it produced no
    // commit term (a Clock_cell with no enable is an identity buffer: it commits
    // every step, which is exactly a null `commits`). Without this the
    // fail-closed guard below could not tell "modelled, commits always" from
    // "could not model at all".
    bool       clock_modelled = false;
    // With two or more unrelated clock roots the schedule owns LATCHES only (the
    // legacy path refuses the cell); every flop and memory keeps the encoder's
    // detected-edge model, which is the only thing that distinguishes their
    // clocks. See Phase_plan::multi_root.
    const bool plan_owns      = phased && (!phase_plan_->multi_root() || gu::type_op_of(node) == Ntype_op::Latch);
    if (plan_owns) {
      // The SCHEDULE is the clock model. Everything the legacy branches below
      // derive per node -- gate folding, edge detection, the derived-clock
      // refusal -- was decided once, design-wide and hierarchy-aware, by
      // plan_phases(). Here it reduces to two facts: is this my microstep, and
      // is my clock gated.
      clock_modelled = true;
      if (!commit_now) {
        commits = tm_.mkBoolean(false);
      } else if (pe != nullptr && !pe->guard_key.empty()) {
        if (pe->live_guard) {
          auto git = phase_plan_->guard_cones.find(pe->guard_key);
          if (git == phase_plan_->guard_cones.end()) {
            return fail_unsupported("latch '" + gu::debug_name(node) + "' has no encodable live enable cone");
          }
          Term acc;
          for (const auto& cone : git->second) {
            bool ok2 = true;
            Val  gv  = driver_val(cone, ok2);
            if (!ok2 || gv.term.isNull()) {
              return fail_unsupported("latch '" + gu::debug_name(node) + "' has no encodable live enable cone");
            }
            Term hot = tm_.mkTerm(Kind::DISTINCT, {gv.term, bv_const(tm_, gv.width, 0)});
            acc      = acc.isNull() ? hot : tm_.mkTerm(Kind::AND, {acc, hot});
          }
          commits = acc;
        } else if (single_step()) {
          // P == 1: one encoder step IS the reference edge, so the guard is the
          // enable held at that edge -- exactly M9's fold, but built from the
          // schedule's CANONICALIZED chain, so a gate of a gate contributes
          // `en0 & en1` instead of refusing as "clk_ref is itself derived".
          commits = guard_term(pe->guard_key, ok);
          if (!ok) {
            return fail_unsupported("flop '" + gu::debug_name(node)
                                    + "' is clocked by a gate whose enable cone has no encodable driver");
          }
        } else {
          // The gate's guard was SAMPLED (see the guard-state emission after this
          // loop) on the inactive phase before the cell's own active edge; the
          // consumer reads that sample, never the live cone. Pinning the sample to
          // the consumer's own microstep reads an inverted cell's guard half a
          // period early -- a wrong verdict, not an UNKNOWN.
          const Val gv = seed_state(pe->guard_key, 1, false);
          commits      = tm_.mkTerm(Kind::DISTINCT, {gv.term, bv_const(tm_, 1, 0)});
        }
      }
    } else if (auto clk_d = hier_sink_driver(node, "clock_pin"); !clk_d.is_invalid()) {
      // Dispatch on the clock's SHAPE, with pure width adjustment peeled off
      // (see peel_clock_width): a round-tripped `clk & en` reaches the flop as
      // get_mask(and(...)) and must still fold as the ICG it is.
      auto       cn     = peel_clock_width(clk_d).get_master_node();
      const auto cop    = gu::type_op_of(cn);
      const auto clk_in = resolve_clk_input(clk_d);
      if (cop == Ntype_op::Clock_cell) {
        // 2f-latch M9 -- the ONE recognized clock operator. The cell encodes the
        // glitch-free ICG CONTRACT: `en` is sampled AT clk_ref's active edge.
        // One encoder step IS that edge, so the commit condition is simply
        // "en held at this step" -- the same term the inline M8 fold produces,
        // now reachable when the gate was INSTANTIATED rather than spelled out.
        hhds::Occurrence_pin ref_d, en_d, div_d, inv_d;
        for (const auto& e : cn.inp_edges()) {
          switch (static_cast<int>(e.sink.get_port_id())) {
            case 2 : ref_d = e.driver; break;  // clk_ref
            case 3 : div_d = e.driver; break;  // div
            case 4 : en_d = e.driver; break;   // en
            case 6 : inv_d = e.driver; break;  // invert
            default: break;
          }
        }
        int divv = 1;
        if (div_d.is_const()) {
          const auto& dc = gu::const_of(div_d);
          divv           = dc.is_just_i64() ? static_cast<int>(dc.to_just_i64()) : 0;
        }
        if (divv != 1) {
          // Deliberately NOT approximated. A divider's INITIAL PHASE has to be
          // part of its identity or two 180-degrees-apart div-by-2s compare
          // equal -- the clock-blindness false-PROVEN class.
          return fail_unsupported("flop '" + gu::debug_name(node) + "' is clocked by a Clock_cell with div=" + std::to_string(divv)
                                  + ", which is not implemented (v1 is div=1 only)");
        }
        if (inv_d.is_const() && !inv_d.is_known_false()) {
          // An inverted gate output commits on the OPPOSITE edge, which is a
          // negedge flop -- pass.single_edge's job (P=2), not something a
          // one-step-is-one-commit encoding can express.
          return fail_unsupported("flop '" + gu::debug_name(node)
                                  + "' is clocked by an INVERTED Clock_cell (the opposite edge); "
                                    "pass.single_edge must normalize it first");
        }
        if (resolve_clk_input(ref_d).is_invalid()) {
          return fail_unsupported("flop '" + gu::debug_name(node)
                                  + "' is clocked by a Clock_cell whose clk_ref is itself derived "
                                    "(gate of gate must be canonicalized to one cell first)");
        }
        clock_modelled = true;
        if (!en_d.is_invalid()) {
          bool ok2 = true;
          Val  gv  = driver_val(en_d, ok2);
          if (!ok2 || gv.term.isNull()) {
            return fail_unsupported("flop '" + gu::debug_name(node)
                                    + "' is clocked by a Clock_cell whose enable cone has no encodable driver");
          }
          // NOTE on X: `DISTINCT(v, 0)` reads an X plane as a concrete value,
          // so an enable cone carrying don't-care bits is modelled as a
          // definite commit. That is a real (pre-existing) imprecision, but it
          // is NOT fixed here: the sibling And-ICG fold below builds its guards
          // exactly the same way, and X policy for the whole encoder already
          // lives in `formal.lec.gold_x`. Refusing on a non-null x_mask was
          // MEASURED on minion: it fires on 7 defs that otherwise encode and
          // compare, so it trades a broad loss of coverage for a narrow gain in
          // precision, inconsistently with the path beside it. Settle it as one
          // X-policy decision across both folds, not as a Clock_cell special
          // case.
          commits = tm_.mkTerm(Kind::DISTINCT, {gv.term, bv_const(tm_, gv.width, 0)});
        }
      } else if (cop == Ntype_op::And && clk_in.is_invalid()) {
        // ICG: `<clock-input> & <enables>` -> commit iff every non-clock
        // operand is true. Requires one operand to BE a clock input, or this is
        // some other derived clock and is left ungated (sound: unchanged).
        // EXACTLY ONE operand is the clock; the rest are enables. Both are
        // graph inputs in the canonical `clk & en`, so "is it an input?" cannot
        // tell them apart — that mistake classified `en` as a clock too, left
        // no guards, and silently produced no gating at all. Match by NAME
        // against the nets this design already uses as clocks, falling back to
        // the conventional spellings when (as in a pure-ICG design) no flop is
        // wired straight to a clock input. A miss just leaves the flop ungated,
        // i.e. the previous behavior — never a wrong verdict.
        // A clock that reaches EVERY flop through the gate never lands in
        // `clk_inputs` (that census walks flop clock_pins, and Get_mask wrappers
        // only -- an And stops it). Requiring membership therefore missed the
        // second-domain ICG entirely: `gclk = clk_b and gate` with another flop
        // on the default `clock` left `clk_inputs = {clock}`, neither operand
        // matched, and the flop was refused as an unmodellable derived clock.
        // Measured on tests/equiv/mclk_derived.
        //
        // So fall back to the conventional-spelling heuristic PER OPERAND. It is
        // the same disambiguator the empty-census case already used, and it is
        // deliberately narrow in both directions (`clk_b` matches; `gate`,
        // `clock_en`, `gclk_gate` do not -- Design_clocks::name_looks_like_clock).
        auto is_clock_operand = [&](const hhds::Occurrence_pin& d) {
          auto ci = resolve_clk_input(d);
          if (ci.is_invalid()) {
            return false;
          }
          const std::string input_name{gu::pin_name_of(ci)};
          return clk_inputs.count(input_name) > 0 || livehd::latch_contract::Design_clocks::name_looks_like_clock(input_name);
        };
        // AMBIGUITY IS A REFUSAL, not a coin flip: if two operands both look
        // like clocks this is not an ICG (it is an AND of two clocks), and
        // picking the first would silently drop the other.
        int n_clockish = 0;
        for (const auto& e : cn.inp_edges()) {
          n_clockish += is_clock_operand(e.driver) ? 1 : 0;
        }
        bool                 saw_clock = n_clockish != 1;  // !=1 -> never fold below
        hhds::Occurrence_pin gate_ref;                     // the gate's reference clock input
        std::vector<Term>    guards;
        bool                 gok = n_clockish == 1;
        for (const auto& e : cn.inp_edges()) {
          if (gok && !saw_clock && is_clock_operand(e.driver)) {
            saw_clock = true;
            gate_ref  = resolve_clk_input(e.driver);
            continue;
          }
          if (!gok) {
            break;
          }
          bool ok2 = true;
          Val  gv  = driver_val(e.driver, ok2);
          if (!ok2 || gv.term.isNull()) {
            gok = false;
            break;
          }
          guards.push_back(tm_.mkTerm(Kind::DISTINCT, {gv.term, bv_const(tm_, gv.width, 0)}));
        }
        if (gok && saw_clock && !guards.empty()) {
          commits = guards.size() == 1 ? guards[0] : tm_.mkTerm(Kind::AND, guards);
          // SECOND-DOMAIN GATE. "commit iff the enables hold" is relative to the
          // REFERENCE clock, which is only the whole story when the gate hangs
          // off the design's one clock. If the gate's reference is a DIFFERENT
          // net from the flops' clock, the enable alone would let this flop
          // commit on the other domain's edges -- conflating two clocks, which
          // is exactly the false-PROVEN class the gated-vs-ungated guards exist
          // to stop. Combine it with a DETECTED EDGE of the gate's own
          // reference, the same model the multi-clock path uses, so the flop
          // commits only on clk_b's rise AND with the enable true.
          if (!gate_ref.is_invalid()) {
            const std::string rnm{gu::pin_name_of(gate_ref)};
            if (!clk_inputs.empty() && clk_inputs.count(rnm) == 0) {
              bool rok = true;
              Val  rv  = driver_val(gate_ref, rok);
              if (!rok || rv.term.isNull()) {
                return fail_unsupported("flop '" + gu::debug_name(node)
                                        + "' is gated by a clock whose reference has no encodable driver");
              }
              const std::string ckey = frame_tag(prefix) + std::string("\x01clk:") + rnm;
              auto              pit  = clk_prev_.find(ckey);
              if (pit == clk_prev_.end()) {
                pit = clk_prev_.emplace(ckey, seed_state(std::string("\x01clk:") + rnm, 1, false)).first;
              }
              const Term prev_hot = tm_.mkTerm(Kind::DISTINCT, {pit->second.term, bv_const(tm_, 1, 0)});
              const Term cur_hot  = tm_.mkTerm(Kind::DISTINCT, {rv.term, bv_const(tm_, rv.width, 0)});
              const Term rise     = tm_.mkTerm(Kind::AND, {tm_.mkTerm(Kind::NOT, {prev_hot}), cur_hot});
              commits             = tm_.mkTerm(Kind::AND, {rise, commits});
              out.outputs[std::string("\x01nxt:\x01clk:") + rnm]
                  = Val{tm_.mkTerm(Kind::ITE, {cur_hot, bv_const(tm_, 1, 1), bv_const(tm_, 1, 0)}), 1, false};
            }
          }
        }
      } else if (multi_clock && !clk_in.is_invalid()) {
        // Two or more clock inputs: no net is "the" clock, soeach flop commits on
        // a detected edge of its own. The previous level is SHARED by both
        // sides of the miter (clk_prev_) — without that the solver simply picks
        // opposite initial levels and refutes equivalent designs.
        bool cok = true;
        Val  cv  = driver_val(clk_d, cok);
        if (cok && !cv.term.isNull()) {
          const Term        cur_hot = tm_.mkTerm(Kind::DISTINCT, {cv.term, bv_const(tm_, cv.width, 0)});
          const std::string pkey    = std::string("\x01clkprev:") + std::string(gu::pin_name_of(clk_in));
          const std::string ckey    = frame_tag(prefix) + pkey;
          auto              pit     = clk_prev_.find(ckey);
          if (pit == clk_prev_.end()) {
            pit = clk_prev_.emplace(ckey, seed_state(pkey, 1, false)).first;
          }
          const Term prev_hot = tm_.mkTerm(Kind::DISTINCT, {pit->second.term, bv_const(tm_, 1, 0)});
          bool       posedge  = true;
          if (auto pc = hier_sink_driver(node, "posclk"); pc.is_const()) {
            posedge = !gu::const_of(pc).is_known_false();
          }
          commits = posedge ? tm_.mkTerm(Kind::AND, {tm_.mkTerm(Kind::NOT, {prev_hot}), cur_hot})
                            : tm_.mkTerm(Kind::AND, {prev_hot, tm_.mkTerm(Kind::NOT, {cur_hot})});
          out.outputs[std::string("\x01nxt:") + pkey]
              = Val{tm_.mkTerm(Kind::ITE, {cur_hot, bv_const(tm_, 1, 1), bv_const(tm_, 1, 0)}), 1, false};
        }
      }
      // ---- FAIL CLOSED on a clock this encoder does not model ---------------
      // Reaching here with `commits` still null and no resolvable clock INPUT
      // means the clock_pin is a DERIVED net that neither branch above could
      // handle: a divider (`div <= ~div`), a mux-selected clock, an inverted
      // clock, or an ICG cone whose clock operand could not be identified. The
      // old behaviour was to leave the flop UNGATED and encode it as committing
      // every step, with the derivation as dead code -- and the comment above
      // called that "never a wrong verdict". It is exactly a wrong verdict:
      // measured, `always @(posedge (clk ^ sel))` came back PROVEN equal to
      // `always @(posedge clk)`, a clock-divided flop PROVEN equal to an
      // undivided one, and `lhd formal verify` PROVED a property that is FALSE
      // in hardware. `lhd sim` has always refused this class outright
      // (`gated-clock-unsupported`); this is the same guard on the formal side.
      //
      // Latches and negedge flops do not reach this: pass.single_edge lowers
      // them away first, and it declines (loudly) what it cannot lower.
      if (commits.isNull() && !clock_modelled && resolve_clk_input(clk_d).is_invalid()) {
        return fail_unsupported("flop '" + gu::debug_name(node) + "' has a derived clock the encoder cannot model (driver op is "
                                + std::string(Ntype::get_name(gu::type_op_of(clk_d.get_master_node())))
                                + ", which is neither a clock input nor a foldable `<clock> & <enables>` gate); refusing "
                                  "rather than encode it as committing every step");
      }
    }

    for (int k = 0; k < depth; ++k) {
      const Term& self   = stage_cur(k);
      Term        source = (k == 0) ? (has_din ? din : self) : stage_cur(k - 1);
      Term        nval   = source;
      if (enable_const_false) {
        nval = self;  // never writes -> hold
      } else if (has_enable) {
        nval = tm_.mkTerm(Kind::ITE, {en_hot, source, self});
      }
      // A SYNC reset is part of the transition and belongs INSIDE the commit
      // gate; an ASYNC one overrides regardless of the clock and stays outside.
      // (Without a phase schedule the two are indistinguishable -- one step is
      // one commit -- so the legacy shape is preserved bit-for-bit.)
      const bool sync_inside = has_reset && !async_reset && phased && !single_step();
      if (sync_inside) {
        nval = tm_.mkTerm(Kind::ITE, {rst_hot, initv, nval});
      }
      // No commit this step => HOLD. Placed INSIDE the async reset ITE below so
      // an async reset still overrides, matching the shape this encoder had.
      if (!commits.isNull()) {
        nval = tm_.mkTerm(Kind::ITE, {commits, nval, self});
      }
      if (has_reset && !sync_inside) {
        nval = tm_.mkTerm(Kind::ITE, {rst_hot, initv, nval});
      }
      std::string key = (k + 1 < depth) ? (nm + "\x02p" + std::to_string(k)) : nm;
      Val         nv{nval, w, sgn};
      if (x_dontcare_) {
        // Mirror the next-state mux over the undef planes so a ?-fed flop's
        // next-state compare can be masked (the write path may carry X; the
        // hold path inherits the Q's plane; a reset override is known).
        Term self_u = (k + 1 < depth) ? fit_x_mask_to(tm_, internals[static_cast<size_t>(k)], w) : fit_x_mask_to(tm_, qv, w);
        Term src_u  = (k == 0) ? (has_din ? din_xm : self_u) : fit_x_mask_to(tm_, internals[static_cast<size_t>(k - 1)], w);
        if (!self_u.isNull() || !src_u.isNull()) {
          auto zw = tm_.mkBitVector(static_cast<uint32_t>(w), 0);
          Term su = self_u.isNull() ? zw : self_u;
          Term du = src_u.isNull() ? zw : src_u;
          Term nu = du;
          if (enable_const_false) {
            nu = su;
          } else if (has_enable) {
            nu = tm_.mkTerm(Kind::ITE, {en_hot, du, su});
          }
          if (has_reset) {
            nu = tm_.mkTerm(Kind::ITE, {rst_hot, zw, nu});
          }
          nv.x_mask = nu;
        }
      }
      out.outputs[std::string("\x01nxt:") + key] = nv;
      if (const char* dump_enc = std::getenv("LEC_DUMP_ENC");
          dump_enc != nullptr && dump_enc[0] != '\0' && nm.find(dump_enc) != std::string::npos) {
        std::string ts = nval.toString();
        if (ts.size() > 4000) {
          ts.resize(4000);
          ts += "...<truncated>";
        }
        std::fprintf(stderr,
                     "[LEC_ENC pfx=%s] nxt '%s' stage=%d/%d w=%d xmask=%d nval=%s\n",
                     std::string(prefix).c_str(),
                     key.c_str(),
                     k,
                     depth,
                     w,
                     nv.x_mask.isNull() ? 0 : 1,
                     ts.c_str());
      }
    }
  }

  // ---- SAMPLED CLOCK GUARDS (2f-lec M10) ------------------------------------
  // A recognized clock gate is timing metadata, never an SMT data value and
  // never a scheduled endpoint. Its only effect is to gate the endpoints that
  // consume its output, through ONE guard sampled per source period on the
  // inactive phase immediately before the CELL's own active edge -- parity of
  // the cell, not of the consumer. A chain of gates canonicalizes to one guard
  // (`gate(gate(clk,en0),en1)` -> `en0 & en1`), which is why the plan carries a
  // cone LIST per key.
  //
  // The cut is always WRITTEN before it is READ inside a period (an ordinary
  // gate samples at microstep 0 and its consumers fire at 1 or 3; an inverted
  // one samples at 2 and its consumers fire at 3), so the power-on value is
  // unobservable and the two sides need not agree on it.
  if (phased && !single_step()) {
    // ONE pass over the endpoints instead of two nested scans per guard key
    // (the old shape was O(guard_cones * endpoints) twice over, on the design's
    // whole endpoint map). A key is a SAMPLED cut only when some endpoint reads
    // it that way; `live_guard` endpoints read their cone directly.
    absl::flat_hash_map<std::string, int> sampled_key_ms;
    for (const auto& [nk, pep] : phase_plan_->ep) {
      if (pep.guard_key.empty() || pep.live_guard) {
        continue;
      }
      // Every consumer of one cell shares the sample microstep, so the first
      // endpoint that names this key decides it.
      sampled_key_ms.try_emplace(pep.guard_key, static_cast<int>(pep.guard_sample));
    }
    for (const auto& [gkey, cones] : phase_plan_->guard_cones) {
      const auto sit = sampled_key_ms.find(gkey);
      if (sit == sampled_key_ms.end()) {
        continue;
      }
      const Val cur       = seed_state(gkey, 1, false);
      const int sample_ms = sit->second;
      Term      nxt       = cur.term;
      if (sample_ms == microstep_) {
        Term acc;
        bool gok = true;
        for (const auto& cone : cones) {
          bool ok2 = true;
          Val  gv  = driver_val(cone, ok2);
          if (!ok2 || gv.term.isNull()) {
            gok = false;
            break;
          }
          Term hot = tm_.mkTerm(Kind::DISTINCT, {gv.term, bv_const(tm_, gv.width, 0)});
          acc      = acc.isNull() ? hot : tm_.mkTerm(Kind::AND, {acc, hot});
        }
        if (!gok || acc.isNull()) {
          return fail_unsupported("clock gate guard '" + gkey + "' has no encodable enable cone");
        }
        nxt = tm_.mkTerm(Kind::ITE, {acc, bv_const(tm_, 1, 1), bv_const(tm_, 1, 0)});
      }
      out.outputs[std::string("\x01nxt:") + gkey] = Val{nxt, 1, false};
    }
  }

  // ---- M4 memory cut, phase 2: now that write addr/din/enable are resolved,
  // build the next-state array and tie each fresh read dout to its real value.
  for (auto& mc : mem_cuts) {
    // BLACKBOXED (formal.ignore_memory): phase 1 already minted each read dout
    // as a SHARED free symbol across the two designs. Emit nothing else — no
    // dout tie, no next-state array, no read_all, no whole-array update — so
    // the memory contributes exactly one unconstrained shared value per (port,
    // cycle) and its CONTENTS are never compared. That is the whole meaning of
    // "excluded from formal": the reads still connect the two designs (so the
    // logic AROUND the memory is still proved), but nothing is claimed about
    // what the memory holds. Disclosed by the driver, like a trusted def.
    if (mc.ignored) {
      continue;
    }
    auto fit_unsigned = [&](const Val& v, int wd) -> Term { return fit_to(tm_, Val{v.term, v.width, false}, wd); };
    bool ok           = true;

    // Scatter a size*bits bus into an array: entry i = bus[(i+1)*bits-1 : i*bits]
    // (entry 0 in the low bits), STOREd over `base`.
    auto array_from_bus = [&](const Term& base, const Term& bus) -> Term {
      Term arr = base;
      for (int i = 0; i < mc.sig.size; ++i) {
        Term slice = bv_extract(tm_, bus, (i + 1) * mc.sig.bits - 1, i * mc.sig.bits);
        arr        = tm_.mkTerm(Kind::STORE, {arr, bv_const(tm_, mc.sig.addr_w, static_cast<uint64_t>(i)), slice});
      }
      return arr;
    };

    // 2f-latch M9 -- the memory's COMMIT condition, when a recognized
    // Clock_cell gates its clock. Built once here (phase 2, where the
    // combinational fixpoint has run) and folded into ALL THREE places a memory
    // commits: the whole-array update, each per-port write mask, and the
    // sync-read register. Missing any one of them re-registers or re-writes on
    // a step the clock never ticked -- the "commits every step" mis-model this
    // whole guard exists to remove, just moved somewhere less obvious.
    Term mem_commit;
    if (!mc.commit_en.is_invalid()) {
      Val cv = driver_val(mc.commit_en, ok);
      if (!ok || cv.term.isNull()) {
        return fail_unsupported("memory '" + gu::debug_name(mc.node)
                                + "' is clocked by a Clock_cell whose enable cone has no encodable driver");
      }
      // See the flop path's note on X: the enable's don't-care plane is
      // deliberately NOT refused here, for consistency with the And-ICG fold
      // and with `formal.lec.gold_x`.
      mem_commit = tm_.mkTerm(Kind::DISTINCT, {cv.term, bv_const(tm_, cv.width, 0)});
    }
    // PHASE SCHEDULE (M10): a memory is scheduled like any other endpoint. On a
    // microstep that is not its own it commits NOTHING -- the same three points
    // (whole-array update, per-port write mask, sync-read register) all hold --
    // which is what lets a rise-written array be READ by a fall-registered port
    // in the same source period.
    // A COMBINATIONAL memory (type==2: no clock at all) is PURE LOGIC and must be
    // evaluated on EVERY microstep -- it is a lookup table, not an endpoint. It
    // has no entry in the schedule (which enumerates CLOCKED endpoints only), so
    // without this guard it fell to the `Phase::Rise` default below and was
    // suppressed on the other three microsteps: its reads then served STALE data
    // to anything sampling at fall.
    //   Measured: a comb array feeding an output, in a design with any negedge
    // flop (which is what forces P>1), REFUTED against its own Verilog source --
    // `logic [3:0] rf_read [4]` + `assign o = rf_read[addr]`. It needs BOTH the
    // negedge endpoint (else single_step() and the gate never fires) and the
    // comb ARRAY (a scalar read is not a Memory cell and is never gated), which
    // is why it hid behind a register-file shape. See
    // lhd/tests/mem_partsel_write_test.sh.
    if (phased && !phase_plan_->multi_root() && !mc.is_comb) {
      const Phase_endpoint* mpe = nullptr;
      if (auto pit = phase_plan_->ep.find(box_node_key(mc.node)); pit != phase_plan_->ep.end()) {
        mpe = &pit->second;
      }
      const int my_ms = mpe != nullptr ? static_cast<int>(mpe->phase) : static_cast<int>(Phase::Rise);
      if (!single_step() && my_ms != microstep_) {
        mem_commit = tm_.mkBoolean(false);
      } else if (mpe != nullptr && !mpe->guard_key.empty()) {
        if (single_step()) {
          mem_commit = guard_term(mpe->guard_key, ok);
          if (!ok) {
            return fail_unsupported("memory '" + gu::debug_name(mc.node)
                                    + "' is clocked by a gate whose enable cone has no encodable driver");
          }
        } else {
          const Val gv = seed_state(mpe->guard_key, 1, false);
          mem_commit   = tm_.mkTerm(Kind::DISTINCT, {gv.term, bv_const(tm_, 1, 0)});
        }
      }
    }

    // Whole-array bulk `update` is the BASE next-state (lowest priority); per-port
    // writes below STORE on top of it (per-port wins). update_enable gates the
    // whole array (hold = a_cur). A plain memory starts from a_cur unchanged.
    Term a_next = mc.a_cur;
    if (mc.is_whole) {
      Val uv = driver_val(mc.update, ok);
      if (!ok) {
        return fail("memory '" + gu::debug_name(mc.node) + "' update bus not encodable");
      }
      // Fit the bus to the full size*bits width before the per-entry scatter: a
      // const-broadcast update (e.g. a reset/flush arm writing all-zeros) folds to
      // a NARROW const, and array_from_bus would then extract past its end. cgen
      // (wire [busw-1:0]) and cgen_sim (operand(.,W)) zero-extend the same way.
      Term               upd_bus = fit_unsigned(uv, mc.sig.size * mc.sig.bits);
      Term               a_upd   = array_from_bus(mc.a_cur, upd_bus);
      // Mirror every arm applied below into `whole` so the cones consumer can
      // discharge this array cut with bit-vector obligations instead of the
      // array theory (Encoded::Mem_whole). An arm the encode SKIPS (a
      // non-encodable update_enable) makes the record inexact, which the
      // consumer reads as "keep the array cut" rather than as "unconditional".
      Encoded::Mem_whole whole;
      whole.bus = upd_bus;
      if (!mc.update_enable.is_invalid()) {
        Val ev = driver_val(mc.update_enable, ok);
        if (ok) {
          Term en_hot = tm_.mkTerm(Kind::DISTINCT, {ev.term, bv_const(tm_, ev.width, 0)});
          a_upd       = tm_.mkTerm(Kind::ITE, {en_hot, a_upd, mc.a_cur});
          whole.cond  = en_hot;
        } else {
          whole.exact = false;
        }
      }
      if (!mem_commit.isNull()) {
        a_upd      = tm_.mkTerm(Kind::ITE, {mem_commit, a_upd, mc.a_cur});  // gated clock did not tick: HOLD
        whole.cond = whole.cond.isNull() ? mem_commit : tm_.mkTerm(Kind::AND, {whole.cond, mem_commit});
      }
      if (whole.cond.isNull()) {
        whole.cond = tm_.mkTrue();
      }
      // The hold arm of the record is `a_cur`, so the obligations it stands for
      // only imply equal next-states when BOTH designs hold the same a_cur.
      // build_shared_mems declines a memory with no out_edges or an ambiguous
      // shape bucket, and then each side minted its own free array symbol --
      // equal cond and equal bus would "prove" a pair whose hold behaviour was
      // never compared. Mark it inexact so the consumer keeps the array cut.
      if (!mc.a_cur_shared) {
        whole.exact = false;
      }
      out.mem_whole[mc.key] = whole;
      a_next                = a_upd;
    } else if (mc.is_comb && !mc.init.is_invalid()) {
      // type==2 combinational array / ROM: the base contents each cycle are the
      // comptime `init` constant, built PER-DESIGN (not the shared free symbol),
      // so a read with no covering write this cycle returns the initial value.
      // Soundness: pinning the SHARED symbol to each side's init would be unsound
      // when the two inits differ (contradictory assumptions -> vacuous proof);
      // building per-design and comparing OUTPUTS makes equal inits PROVE and
      // differing inits REFUTE. Per-port writes below override within the cycle;
      // reads see the post-write array (rd_src == a_next for is_comb). No next_mem.
      Val iv = driver_val(mc.init, ok);
      if (!ok) {
        return fail("memory '" + gu::debug_name(mc.node) + "' init contents not encodable");
      }
      a_next = array_from_bus(mc.a_cur, fit_unsigned(iv, mc.sig.size * mc.sig.bits));
    } else if (mc.is_rom) {
      // no-write PERSISTENT ROM (type==0/1): pin the per-design a_cur to the
      // comptime init constant so committed reads (rd_src == a_cur) return init.
      // a_cur is fresh per design here (not the shared symbol), so this is sound —
      // equal inits prove, differing inits refute via the compared outputs /
      // next-state. a_next stays a_cur (no writes) -> a constant next-state.
      Val iv = driver_val(mc.init, ok);
      if (!ok) {
        return fail("memory '" + gu::debug_name(mc.node) + "' init contents not encodable");
      }
      out.equalities.emplace_back(mc.a_cur, array_from_bus(mc.a_cur, fit_unsigned(iv, mc.sig.size * mc.sig.bits)));
    }
    // a_after[m] = the array after the first m APPLIED write ports (a_after[0]
    // == a_cur for a plain memory). `applied_upto[w]` maps a cell write-port
    // ordinal to its a_after index, since a port with no din is skipped by the
    // fold but still occupies a column of the `fwd` matrix.
    std::vector<Term>                  a_after;
    std::vector<size_t>                applied_upto;
    // Per cell write-port ordinal (lockstep with `applied_upto`): the resolved
    // (address, per-bit write mask) this port drives, for the `undef` X-plane
    // below. A null address = "this column writes nothing this cycle".
    std::vector<std::pair<Term, Term>> wr_am;
    a_after.emplace_back(a_next);
    for (auto& p : mc.ports) {
      if (p.rd) {
        continue;
      }
      applied_upto.emplace_back(a_after.size() - 1);
      if (p.din.is_invalid()) {
        wr_am.emplace_back(Term{}, Term{});
        continue;
      }
      Val av = driver_val(p.addr, ok);
      Val dv = driver_val(p.din, ok);
      if (!ok) {
        return fail("memory '" + gu::debug_name(mc.node) + "' write addr/din not encodable");
      }
      Term addr = fit_unsigned(av, mc.sig.addr_w);
      Term din  = fit_to(tm_, Val{dv.term, dv.width, false}, mc.sig.bits);
      // Per-lane write mask: word-enable (wensize<=1) replicates the enable hot
      // bit across the whole word. Otherwise each of the `wensize` enable bits
      // controls one contiguous `bits/wensize` lane. This must expand every lane,
      // not treat an 8-lane byte/chunk mask as one whole-word boolean: cgen's
      // memory wrapper lowers each lane assignment to a separate write port, and
      // the two equivalent port decompositions only match under the real lane
      // semantics.
      Term wmask;
      if (p.en.is_invalid()) {
        wmask = tm_.mkTerm(Kind::BITVECTOR_NOT, {bv_const(tm_, mc.sig.bits, 0)});
      } else {
        Val ev = driver_val(p.en, ok);
        if (!ok) {
          return fail("memory '" + gu::debug_name(mc.node) + "' enable not encodable");
        }
        if (mc.wensize > 1 && mc.sig.bits % mc.wensize == 0) {
          const int  lane_bits = mc.sig.bits / mc.wensize;
          const Term lanes     = fit_to(tm_, Val{ev.term, ev.width, false}, mc.wensize);
          Term       mask;
          for (int lane = mc.wensize - 1; lane >= 0; --lane) {
            const Term bit = bv_extract(tm_, lanes, lane, lane);
            const Term hot = tm_.mkTerm(Kind::DISTINCT, {bit, bv_const(tm_, 1, 0)});
            const Term lane_mask
                = tm_.mkTerm(Kind::ITE,
                             {hot, tm_.mkTerm(Kind::BITVECTOR_NOT, {bv_const(tm_, lane_bits, 0)}), bv_const(tm_, lane_bits, 0)});
            mask = mask.isNull() ? lane_mask : tm_.mkTerm(Kind::BITVECTOR_CONCAT, {mask, lane_mask});
          }
          wmask = mask;
        } else {
          Term en_hot = tm_.mkTerm(Kind::DISTINCT, {ev.term, bv_const(tm_, ev.width, 0)});
          wmask       = tm_.mkTerm(
              Kind::ITE,
              {en_hot, tm_.mkTerm(Kind::BITVECTOR_NOT, {bv_const(tm_, mc.sig.bits, 0)}), bv_const(tm_, mc.sig.bits, 0)});
        }
      }
      // Fold the commit into the MASK rather than into `a_next` alone: the
      // per-port record pushed into out.mem_wr below is an ALTERNATIVE proof
      // route (equal inputs => equal write chains, decided without the array
      // theory). Gating only the array term would leave the two routes
      // disagreeing about whether this step wrote at all.
      if (!mem_commit.isNull()) {
        wmask = tm_.mkTerm(Kind::ITE, {mem_commit, wmask, bv_const(tm_, mc.sig.bits, 0)});
      }
      // Record AFTER the commit gate so the X-plane inherits the enable, the
      // per-bit wensize mask and the gated clock for free.
      wr_am.emplace_back(addr, wmask);
      Term old      = tm_.mkTerm(Kind::SELECT, {a_next, addr});
      Term keep     = tm_.mkTerm(Kind::BITVECTOR_AND, {tm_.mkTerm(Kind::BITVECTOR_NOT, {wmask}), old});
      Term set      = tm_.mkTerm(Kind::BITVECTOR_AND, {wmask, din});
      Term new_word = tm_.mkTerm(Kind::BITVECTOR_OR, {keep, set});
      a_next        = tm_.mkTerm(Kind::STORE, {a_next, addr, new_word});
      // Snapshot after each write port so a read port can source the array as
      // of its own program position: `fwd` row r is a PREFIX of the write ports
      // under ordering="program" (and all/none under "fwd"/"none"), so
      // a_after[m] is exactly what read port r with m forwarded writes sees.
      a_after.emplace_back(a_next);
      // The bit-vector inputs this port contributes to the chain: equal inputs on
      // both sides => equal chains, provable without the array theory.
      out.mem_wr[mc.key].push_back(Encoded::Mem_wr_port{addr, wmask, din});
    }

    // Reset (highest priority) overrides per-port + update for a registered
    // whole-array: a_next = reset ? <init bus array> : a_next. The init bus may be
    // runtime (e.g. enqPtrVec resets to a computed wire), or absent (=> zero).
    if (mc.is_whole && !mc.reset.is_invalid()) {
      Val rv = driver_val(mc.reset, ok);
      if (ok) {
        Term rst_hot = tm_.mkTerm(Kind::DISTINCT, {rv.term, bv_const(tm_, rv.width, 0)});
        Term init_bus;
        if (!mc.init.is_invalid()) {
          Val iv   = driver_val(mc.init, ok);
          // Same widening the update arm needs: an all-zeros / broadcast reset
          // value folds to a NARROW const, and array_from_bus would then
          // bv_extract past its end (which throws, aborting the run).
          init_bus = ok ? fit_unsigned(iv, mc.sig.size * mc.sig.bits) : bv_const(tm_, mc.sig.size * mc.sig.bits, 0);
        } else {
          init_bus = bv_const(tm_, mc.sig.size * mc.sig.bits, 0);
        }
        Term a_init                 = array_from_bus(mc.a_cur, init_bus);
        a_next                      = tm_.mkTerm(Kind::ITE, {rst_hot, a_init, a_next});
        out.mem_whole[mc.key].reset = rst_hot;
        out.mem_whole[mc.key].init  = init_bus;  // raw, as fed to array_from_bus; the consumer sort-checks
      } else {
        out.mem_whole[mc.key].exact = false;  // a reset arm we could not model tops the next-state
      }
    }

    // Tie each fresh read dout to select(read-source, addr). A combinational
    // whole-array reads its POST-update contents (a_next == the live array); a
    // registered whole-array and plain memory read committed state (a_cur, since
    // fwd is forced 0 for whole-arrays); fwd!=0 plain memories forward writes.
    applied_upto.emplace_back(a_after.size() - 1);  // sentinel: "all write ports"
    const size_t n_wr      = applied_upto.empty() ? 0 : applied_upto.size() - 1;
    // `fwd` is a per-(read,write) MATRIX (graph/cell.cpp): bit (r*n_wr + w) =>
    // read port r sees write port w's new data. ordering="program" makes each
    // row a PREFIX of the write ports, so read r simply sources the array as of
    // its own program position (a_after[...]); "fwd" is the full row and "none"
    // an empty one. A legacy hand-written non-prefix mask cannot be expressed
    // as a snapshot, so it keeps the historical coarse behavior (any bit set =>
    // read the fully-written array).
    auto         fwd_bit   = [&](size_t r, size_t w) { return mc.fwd && mc.fwd->bit_test(static_cast<int>(r * n_wr + w)); };
    auto         rd_source = [&](size_t r) -> const Term& {
      if (mc.is_comb || mc.is_whole || n_wr == 0) {
        return mc.is_comb ? a_next : ((mc.fwd && !mc.fwd->is_known_false()) ? a_next : mc.a_cur);
      }
      size_t forwarded_writes = 0;
      while (forwarded_writes < n_wr && fwd_bit(r, forwarded_writes)) {
        ++forwarded_writes;
      }
      // Scan for a set bit BEYOND the prefix FIRST: a row with a hole (e.g.
      // 0b10 from a legacy `fwd=2` or an explicit __memory matrix) is not
      // expressible as a snapshot. Falling through to `a_cur` here would both
      // UNDER-forward and — because `rd_src == a_cur` sets `shared_cur` — let
      // the two designs' douts be assumed equal on equal addresses, which is a
      // false PROVEN. Over-forwarding (a_next) is the safe direction.
      for (size_t w = forwarded_writes; w < n_wr; ++w) {
        if (fwd_bit(r, w)) {
          return a_next;  // historical coarse behavior
        }
      }
      if (forwarded_writes == 0) {
        return mc.a_cur;
      }
      return a_after[applied_upto[forwarded_writes]];
    };
    for (size_t k = 0; k < mc.rd_fresh.size(); ++k) {
      const Term& rd_src = rd_source(k);
      Val         av     = driver_val(mc.rd_addr[k], ok);
      if (!ok) {
        return fail("memory '" + gu::debug_name(mc.node) + "' read addr not encodable");
      }
      Term addr = fit_unsigned(av, mc.sig.addr_w);
      Term real = tm_.mkTerm(Kind::SELECT, {rd_src, addr});
      // ordering="none": tie the deferred X bit-plane minted in phase 1 to this
      // read's collision predicate. The plane is the OR of every undefined write
      // column's write mask, gated on that column hitting THIS read address —
      // so it is bit-exact (a per-bit wensize write only clouds the bits it
      // writes) and empty on every non-colliding cycle. The VALUE above is
      // untouched: `rd_src` stays a_cur, so the `shared_cur` dout-merge route
      // keeps working and no false PROVEN is introduced.
      if (k < mc.rd_xmask.size() && !mc.rd_xmask[k].isNull()) {
        Term zero    = bv_const(tm_, mc.sig.bits, 0);
        Term plane   = zero;
        Term claimed = zero;  // bits a HIGHER-priority write column already resolved
        // Walk the write columns HIGH -> LOW, the priority gen_mem_wrapper's ITE
        // chain gives the emitted RTL: the first rung that HITS decides the bit,
        // and a plain (neither fwd nor undef) column emits no rung, so it does
        // not stop the chain. Bits an upper FWD column forwards are DEFINED —
        // they read that column's din — so a lower undef column must not cloud
        // them. ORing every undef column unconditionally masked those bits too
        // and let a real difference inside the window pass as PROVEN.
        for (size_t wi = std::min(n_wr, wr_am.size()); wi-- > 0;) {
          const bool u = mc.undef && mc.undef->bit_test(static_cast<int>(k * n_wr + wi));
          if (!u && !fwd_bit(k, wi)) {
            continue;
          }
          if (wr_am[wi].first.isNull()) {
            continue;  // no din this cycle: the column writes nothing and claims nothing
          }
          Term hit  = tm_.mkTerm(Kind::EQUAL, {wr_am[wi].first, addr});
          Term rung = tm_.mkTerm(Kind::ITE, {hit, wr_am[wi].second, zero});
          if (u) {
            // UNDEF is emitted BEFORE FWD inside one write port's step, so it
            // wins a pair that sets both (upass_tolg rejects that spelling; a
            // matrix built elsewhere still lands on the wrapper's answer).
            plane = tm_.mkTerm(Kind::BITVECTOR_OR,
                               {plane, tm_.mkTerm(Kind::BITVECTOR_AND, {rung, tm_.mkTerm(Kind::BITVECTOR_NOT, {claimed})})});
          }
          claimed = tm_.mkTerm(Kind::BITVECTOR_OR, {claimed, rung});
        }
        out.equalities.emplace_back(mc.rd_xmask[k], plane);
      }
      // A dout that reads the SHARED committed contents is a candidate for
      // merging with the other design's matching port: equal address + the same
      // array term => provably equal values. A forwarding / combinational read
      // sources this design's OWN a_next, so nothing can be assumed about it.
      const bool shared_cur = !mc.is_comb && !mc.is_rom && rd_src == mc.a_cur;
      out.mem_rd[mc.key].push_back(Encoded::Mem_rd_port{mc.rd_fresh[k], addr, mc.mtype == 1 ? Term{} : real, shared_cur});
      if (mc.mtype == 1 && shared_reads != nullptr) {
        // Sync read (latency-1): rd_fresh is the CURRENT registered dout (seeded
        // from shared_reads in phase 1); THIS cycle's read is its NEXT state,
        // threaded forward by the caller like next_mem. The dout thus lands one
        // cycle after the address. (Gated on shared_reads so callers that do not
        // thread it keep the latency-0 tie below — a sound conservative default.)
        //
        // The dout REGISTER is clocked by the same (possibly gated) clock, so a
        // step the gate suppressed must HOLD it. This is the third commit point
        // and the easiest to miss: writes look correctly gated while the read
        // port quietly re-registers every step.
        Val nv{mem_commit.isNull() ? real : tm_.mkTerm(Kind::ITE, {mem_commit, real, mc.rd_fresh[k]}), mc.sig.bits, false};
        // ordering="none": the dout REGISTER captures this cycle's X plane along
        // with the value, so a gate-suppressed step must hold BOTH — otherwise
        // the plane would appear a cycle early or vanish. `is_signed` stays false:
        // the consumer only re-seeds this map and phase 1 re-derives the sign
        // from the dout pin.
        if (k < mc.rd_xmask.size() && !mc.rd_xmask[k].isNull()) {
          Term held = (k < mc.rd_xcur.size() && !mc.rd_xcur[k].isNull()) ? mc.rd_xcur[k] : bv_const(tm_, mc.sig.bits, 0);
          nv.x_mask = mem_commit.isNull() ? mc.rd_xmask[k] : tm_.mkTerm(Kind::ITE, {mem_commit, mc.rd_xmask[k], held});
        }
        out.next_read[mc.rd_key[k]] = nv;
      } else {
        out.equalities.emplace_back(mc.rd_fresh[k], real);
      }
    }
    // Combinational read_all: tie its deferred symbol to CONCAT(SELECT(a_next,i))
    // — a comb array reads its POST-update contents (rd_source() returns a_next
    // for is_comb).
    if (mc.is_comb && !mc.ra_fresh.isNull()) {
      Term bus = tm_.mkTerm(Kind::SELECT, {a_next, bv_const(tm_, mc.sig.addr_w, 0)});
      for (int i = 1; i < mc.sig.size; ++i) {
        Term ei = tm_.mkTerm(Kind::SELECT, {a_next, bv_const(tm_, mc.sig.addr_w, static_cast<uint64_t>(i))});
        bus     = tm_.mkTerm(Kind::BITVECTOR_CONCAT, {ei, bus});
      }
      out.equalities.emplace_back(mc.ra_fresh, bus);
    }

    // A combinational whole-array has no persistent state -> no next_mem (its
    // contents are a pure function of this cycle's update, compared as outputs).
    if (!mc.is_comb) {
      out.next_mem[mc.key] = a_next;
    }
  }

  // ---- Outputs: value driving each output sink, fit to the declared width.
  // Read through the HIER resolver, not the class edges: an output driven
  // DIRECTLY by a descended sub-instance's output pin (the pass.partition /
  // pass.abc wrapper shape: `out <- u_top__c0.f_o` with no comb node between)
  // has no pin2val entry for the boundary pin itself — the encoded producer
  // lives inside the child body and is keyed by ITS hier frame. A hier-context
  // handle's inp_edges() resolves each edge to the real leaf driver (and stops
  // at an opaque collapsed boundary, whose box outputs ARE keyed on the
  // boundary pin — the occurrence view's owned policy covers this walk too).
  auto output_view = g->occurrences(opaque);
  for (const auto& d : gio->get_output_pin_decls()) {
    auto spin = g->get_output_pin(d.name);
    if (spin.is_invalid()) {
      continue;
    }
    auto occurrence_spin = output_view.lift(spin);
    auto output_edges    = occurrence_spin.inp_edges();
    if (output_edges.empty()) {
      return fail("output '" + d.name + "' is undriven");
    }
    bool ok = true;
    Val  v  = driver_val(output_edges.front().driver, ok);
    if (!ok) {
      return fail("output '" + d.name + "' driver not encodable (missing " + missing_driver_key + ", values "
                  + std::to_string(pin2val.size()) + (pin2val.empty() ? ")" : ", first " + pin2val.begin()->first + ")"));
    }
    int ow = gu::real_width(spin, *gio, d.name);
    if (ow == 0) {
      // A width-less output port is a 1-bit scalar (Verilog: a port with no
      // range is one bit) — NOT the width of whatever drives it. Defaulting to
      // the driver width left a scalar output carrying a wide internal value
      // (e.g. a 65-bit mux feeding a 1-bit `busy_o`), which then mismatched the
      // other reader's correctly-1-bit output. fit() truncates the driver to it.
      ow = 1;
    }
    Val ov{fit(v, ow), ow, !gio->is_unsign(d.name)};
    ov.x_mask           = fit_x_mask_to(tm_, v, ow);
    out.outputs[d.name] = ov;
  }

  return out;
}

}  // namespace livehd::lec
