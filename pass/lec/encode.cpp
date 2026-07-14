// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "encode.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/source_locator.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace livehd::lec {

using cvc5::Kind;
using cvc5::Sort;
using cvc5::Term;
namespace gu = livehd::graph_util;

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
  return std::string{s};
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

}  // namespace

// Real bus width of a pin: bits attribute is the signed magnitude+1 count;
// an unsigned pin drops the spare sign bit (see lec.md "Bit-width trap").
int real_width(const hhds::Pin_class& pin) {
  int b = gu::bits_of(pin);
  if (b == 0) {
    return 0;
  }
  return gu::is_unsign(pin) ? b - 1 : b;
}

int real_width_io(const hhds::Pin_class& pin, const hhds::GraphIO& gio, std::string_view name) {
  // A module PORT's `bits` attribute is the LITERAL declared bus width
  // (`[3:0]` -> 4), NOT the internal magnitude+1 convention that real_width()
  // assumes for ordinary nets. A port therefore carries exactly `bits`
  // observable bits regardless of sign; subtracting a "spare sign bit" here
  // would drop a REAL data bit (e.g. a 4-bit `output [3:0] io_out` driven by a
  // signed net 0b1010 got truncated to 0b010 = 2, a false PROVEN). cgen
  // declares every port as `signed [bits-1:0]` (full `bits` width), so the
  // encoder must use the same width or the lg: LEC and the re-emitted-Verilog
  // LEC disagree on the same design. (Readers that disagree on a port's width
  // by a sign-bit slot are still reconciled at the max width in query.cpp.)
  return gu::bits_of(pin, gio, name);
}

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
  uint32_t d = static_cast<uint32_t>(width - v.width);
  // sign-extension replicates the top VALUE bit, so the copies are unknown iff
  // that bit is unknown (replicate the undef msb); zero-extension appends
  // known-0 bits (zero-extend the plane).
  auto op = tm.mkOp(v.is_signed ? Kind::BITVECTOR_SIGN_EXTEND : Kind::BITVECTOR_ZERO_EXTEND, {d});
  return tm.mkTerm(op, {v.x_mask});
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
// instead. Unknown bits in the initial are masked to 0 (a defined reset), UNLESS
// `x_as_undefined` (lec.gold_x=ignore): a partially-unknown initial is a
// DON'T-CARE power-on, so it behaves like no initial at all — both designs then
// seed the same fresh shared symbol, i.e. the reference's X-init is bound to
// whatever the implementation starts with (any impl choice is a legal
// resolution). Concretizing it instead (the old behavior) split the two sides
// at cycle 0 when only ONE side carries the ?-constant (BypassNetwork:
// ref init `0sb1<64x?>` -> 2^64 vs the recompiled impl's 0-init).
std::optional<Val> flop_initial(cvc5::TermManager& tm, const hhds::Node_class& node, int width, bool x_as_undefined) {
  auto init_d = gu::get_driver_of_sink_name(node, "initial");
  if (init_d.is_invalid() || !gu::is_const_pin(init_d)) {
    return std::nullopt;
  }
  Dlop c = gu::hydrate_const(init_d);
  if (x_as_undefined && c.has_unknowns()) {
    return std::nullopt;
  }
  if (c.is_just_i64()) {
    return Val{bv_const(tm, width, static_cast<uint64_t>(c.to_just_i64())), width, c.is_negative()};
  }
  auto bin = c.to_binary();
  for (auto& ch : bin) {
    if (ch != '0' && ch != '1') {
      ch = '0';
    }
  }
  if (bin.empty()) {
    bin = "0";
  }
  Val v{tm.mkBitVector(static_cast<uint32_t>(bin.size()), bin, 2), static_cast<int>(bin.size()), c.is_negative()};
  return Val{fit_to(tm, v, width), width, c.is_negative()};
}

// Pipeline depth of a flop: the comptime `pipe_min` pin makes one Flop cell
// model a depth-d shift register (cgen expands it to d chained flops, see
// inou/cgen/cgen_verilog.cpp). Unset/<=1 => the plain single flop. The encoder
// must honor it or it UNDER-counts the latency — reading a `stage[3]`/`@[3]`
// pipeline flop as a single 1-cycle delay, which false-REFUTEs the v->prp miter
// (the emitted side expands the stages into d real flops).
static int flop_depth(const hhds::Node_class& node) {
  auto pm = gu::get_driver_of_sink_name(node, "pipe_min");
  if (!pm.is_invalid() && gu::is_const_pin(pm)) {
    auto d = gu::hydrate_const(pm).to_just_i64();
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
      if (gu::is_const_pin(e.driver)) {
        sig.bits = static_cast<int>(gu::hydrate_const(e.driver).to_just_i64());
      }
    } else if (pin_name == "size") {
      if (gu::is_const_pin(e.driver)) {
        sig.size = static_cast<int>(gu::hydrate_const(e.driver).to_just_i64());
      }
    } else if (std::string_view(pin_name).find("rdport") != std::string_view::npos) {
      if (gu::is_const_pin(e.driver)) {
        if (gu::hydrate_const(e.driver).is_known_false()) {
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
// count of prior same-signature memories in forward_class() order) disambiguates
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
std::string mem_state_key(const Mem_sig& sig, int occ) {
  return std::format("\x01m:{}x{}#{}", sig.size, sig.bits, occ);
}

// Structural node identity within one design (see encode.hpp). Must stay in
// lock-step with the encoder's pinkey convention (INVALID -> ROOT), so the
// query-side box-correspondence builder and the encoder name the same instance
// identically no matter which walk found it.
std::string box_node_key(const hhds::Node_class& n) {
  auto hp = n.get_hier_pos();
  if (hp == hhds::INVALID) {
    hp = hhds::ROOT;
  }
  return std::to_string(static_cast<uint64_t>(n.get_current_gid())) + ":"
         + std::to_string(static_cast<int64_t>(hp)) + ":" + std::to_string(static_cast<uint64_t>(n.get_debug_nid()));
}

Encoded Encoder::encode(hhds::Graph* g, const Io_name_map<Val>* shared_inputs, std::string_view prefix,
                        const Io_name_map<cvc5::Term>* shared_mems, const Io_name_map<cvc5::Term>* shared_reads) {
  Encoded out;
  auto    gio = g->get_io();

  // driver-pin -> Val (SSA value table). Keyed by a HIER-stable string
  // "gid:hier_pos:pid" so that, under forward_hier(), a producer deep in an
  // instance and a consumer reading it across the boundary (inp_edges resolves
  // the real leaf driver, carrying its instance hier_pos) agree on one key.
  absl::flat_hash_map<std::string, Val> pin2val;
  auto pinkey = [](const hhds::Pin_class& p) -> std::string {
    // A top-level pin reached via the class API (g->get_output_pin, a node's
    // driver pin outside a hier walk) carries hier_pos == INVALID, while the
    // same node reached via forward_hier carries ROOT. Both denote the top
    // frame, so canonicalize INVALID -> ROOT (a real sub-instance always has a
    // distinct child tree_pos, so this never aliases two different pins).
    auto hp = p.get_hier_pos();
    if (hp == hhds::INVALID) {
      hp = hhds::ROOT;
    }
    return std::to_string(static_cast<uint64_t>(p.get_current_gid())) + ":"
           + std::to_string(static_cast<int64_t>(hp)) + ":"
           + std::to_string(static_cast<uint64_t>(p.get_debug_pid()));
  };
  Io_name_map<int> bbox_occ;  // LEGACY blackbox occurrence per def-name — only when no box_keys_ map is set
  // Two-phase blackbox bookkeeping (keyed by structural nodekey): the assigned
  // correspondence key once its OUTPUTS are emitted, and the seeded state symbol
  // for a state-aware box. Outputs emit on the first visit (they need only the
  // state / nothing); the input-dependent bbin / UF-tie + next-state are emitted
  // in a later pass.
  absl::flat_hash_map<std::string, std::string> bb_outkey;
  absl::flat_hash_map<std::string, Val>          bb_state_sym;

  auto fail = [&](const std::string& msg) -> Encoded& {
    if (out.ok) {
      out.ok    = false;
      out.error = msg;
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
    return fail("encode budget exceeded (lec.timeout); raise --set lec.timeout to allow more encode time");
  }

  // bit-vector literal of `width` bits from a constant pin's Dlop.
  auto const_val = [&](const hhds::Pin_class& dpin) -> Val {
    Dlop c     = gu::hydrate_const(dpin);
    int  width = std::max(1, c.get_bits());
    bool sgn   = c.is_negative();
    Term t;
    if (c.is_just_i64()) {
      t = bv_const(tm_, width, static_cast<uint64_t>(c.to_just_i64()));
    } else {
      // Wide / partially-unknown constant: build from the MSB-first binary string
      // (get_bits() chars, no prefix). Unknown (X / don't-care) bits are masked
      // to 0 — consistent across two designs reading the same source constant.
      // Under x_dontcare (lec.gold_x=ignore, REF side) the '?' positions ALSO
      // source the undef bit-plane, so the miter can exclude ref-unknown bits
      // from the compare instead of comparing an arbitrary concretization.
      auto bin = c.to_binary();
      std::string ubin;
      ubin.reserve(bin.size());
      bool any_unknown = false;
      for (auto& ch : bin) {
        if (ch != '0' && ch != '1') {
          ch          = '0';
          any_unknown = true;
          ubin += '1';
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
  // Under forward_hier(), inp_edges() resolves a sink across instance boundaries
  // to the real LEAF driver: a constant, a top primary input (a pin on the root
  // INPUT_NODE — resolved by port name from the seeded, cross-design-shared
  // inputs), or an ordinary producer node (looked up by its hier pinkey).
  auto driver_val = [&](const hhds::Pin_class& dpin, bool& ok) -> Val {
    ok = true;
    if (gu::is_const_pin(dpin)) {
      return const_val(dpin);
    }
    if (gu::is_graph_input_pin(dpin)) {
      auto it = out.inputs.find(std::string(dpin.get_pin_name()));
      if (it != out.inputs.end()) {
        return it->second;
      }
      ok = false;
      return {};
    }
    auto it = pin2val.find(pinkey(dpin));
    if (it == pin2val.end()) {
      ok = false;
      return {};
    }
    return it->second;
  };

  // 1-bit BV from a Bool predicate.
  auto pred_to_bv = [&](const Term& b) -> Term {
    return tm_.mkTerm(Kind::ITE, {b, bv_const(tm_, 1, 1), bv_const(tm_, 1, 0)});
  };

  // ---- Inputs: shared symbol or a fresh one, mapped onto the input driver pin.
  for (const auto& d : gio->get_input_pin_decls()) {
    auto dpin = g->get_input_pin(d.name);
    int  w    = real_width_io(dpin, *gio, d.name);
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
        v = it->second;
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
  // forward_hier() descends the whole instance tree, so flops at EVERY level
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
  ankerl::unordered_dense::set<hhds::Gid> opaque_subs;
  if (collapse_defs_ != nullptr) {
    for (auto sn : g->forward_hier()) {
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
  // Make the opacity AMBIENT for the whole encode: forward_hier(opaque) below
  // skips descent, and the cross-boundary edge resolver (inp_edges threading) must
  // ALSO stop at a collapsed sub's boundary, or a consumer would read its
  // un-encoded internal driver. The scope covers every walk + inp_edges() here.
  hhds::Hier_opaque_scope opaque_scope(opaque);

  std::vector<hhds::Node_class> flops;
  std::vector<int>              flop_depths;     // pipe_min depth per flop (>=1)
  std::vector<std::vector<Val>> flop_internals;  // depth>1: the d-1 INTERNAL stage
                                                 // current-states, din-side -> Q-side
  // Internal pipeline-stage current-state for a depth-d flop with output key K:
  // K (the Q stage, seeded below like any flop) plus d-1 hidden stages keyed
  // "K\x02p<i>". Shared/threaded by key exactly like Q (the BMC unroll re-seeds
  // them from each cycle's emitted next-state; the power-on cycle gets a fresh
  // arbitrary value, sound for a reset-less pipeline register).
  auto seed_state = [&](const std::string& key, int w, bool sgn) -> Val {
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
          v.term  = fit(v, w);
          v.width = w;
        }
        v.is_signed = sgn;
      }
    }
    if (v.term.isNull()) {
      v = Val{tm_.mkConst(bv(w), std::string(prefix) + key), w, sgn};
    }
    return v;
  };
  for (auto node : g->forward_hier(true, false, opaque)) {
    auto op = gu::type_op_of(node);
    if (op != Ntype_op::Flop) {
      continue;
    }
    auto qpin = node.get_driver_pin(0);
    if (qpin.is_invalid()) {
      continue;
    }
    int w = real_width(qpin);
    if (w == 0) {
      w = 1;
    }
    bool        sgn = !gu::is_unsign(qpin);
    std::string nm  = flop_key(node.get_hier_name());
    Val         v   = seed_state(nm, w, sgn);
    bool        was_shared
        = shared_inputs != nullptr && shared_inputs->find(nm) != shared_inputs->end();
    if (const char* dump_enc = std::getenv("LEC_DUMP_ENC");
        dump_enc != nullptr && (dump_enc[0] == '\0' || nm.find(dump_enc) != std::string::npos)) {
      std::fprintf(stderr, "[LEC_ENC pfx=%s] flop hier='%s' key='%s' w=%d sgn=%d shared=%d xmask=%d term=%s\n",
                   std::string(prefix).c_str(), node.get_hier_name().c_str(), nm.c_str(), w, sgn ? 1 : 0,
                   was_shared ? 1 : 0, v.x_mask.isNull() ? 0 : 1, v.term.toString().c_str());
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
  struct MPort {
    bool            rd = false;
    hhds::Pin_class addr, din, en;
  };
  struct MemCut {
    hhds::Node_class    node;
    std::string         key;
    Mem_sig             sig;
    std::vector<MPort>  ports;
    int                 wensize = 0;
    int                 fwd     = 0;
    int                 mtype   = -1;
    bool                is_rom  = false;
    cvc5::Term          a_cur;
    std::vector<Term>   rd_fresh;  // dout symbol per read port (port order): a
                                   // fresh within-cycle var (async/comb, tied via
                                   // equalities) OR a seeded current-state symbol
                                   // (type==1 sync, threaded via next_read).
    std::vector<hhds::Pin_class> rd_addr;
    std::vector<std::string>     rd_key;  // "<key>:rd<N>" per read port (sync threading)
    // Whole-array support (the `update` bus is driven): one update/read_all bus
    // instead of N per-entry ports. `is_comb` (type==2, no clock) => no persistent
    // state (no next_mem); reads/read_all reflect the post-update array.
    hhds::Pin_class update, update_enable, reset, init;
    bool             is_whole = false;
    bool             is_comb  = false;
    hhds::Pin_class  ra_pin;    // async read_all driver pin (size*bits)
    cvc5::Term       ra_fresh;  // deferred read_all symbol (tied in phase 2)
  };
  std::vector<MemCut>                   mem_cuts;
  Io_name_map<int> mem_occ;  // per-signature occurrence -> stable key
  for (auto node : g->forward_hier(true, false, opaque)) {
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
    mc.key = mem_state_key(mc.sig, mem_occ[sg]++);
    int mtype = -1;
    for (auto e : node.inp_edges()) {
      auto        raw_pid = static_cast<int>(e.sink.get_port_id());
      std::string pn      = Ntype::get_sink_name(Ntype_op::Memory, raw_pid);
      size_t      pid     = static_cast<size_t>(raw_pid) / Ntype::Memory_port_stride;
      if (pn == "wensize") {
        mc.wensize = static_cast<int>(gu::hydrate_const(e.driver).to_just_i64());
      } else if (pn == "fwd") {
        mc.fwd = static_cast<int>(gu::hydrate_const(e.driver).to_just_i64());
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
        if (gu::is_const_pin(e.driver)) {
          mtype = static_cast<int>(gu::hydrate_const(e.driver).to_just_i64());
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
          mc.ports[pid].rd = !gu::hydrate_const(e.driver).is_known_false();
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
        mc.ra_pin = node.create_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
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
    Sort asort  = tm_.mkArraySort(bv(mc.sig.addr_w), bv(mc.sig.bits));
    mc.is_rom   = (!mc.is_comb && !mc.is_whole && mc.sig.n_wr == 0 && !mc.init.is_invalid());
    if (shared_mems != nullptr && !mc.is_rom) {
      if (auto it = shared_mems->find(mc.key); it != shared_mems->end()) {
        mc.a_cur = it->second;
      }
    }
    if (mc.a_cur.isNull()) {
      mc.a_cur = tm_.mkConst(asort, std::string(prefix) + mc.key);
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
      Term fresh;
      if (mtype == 1 && shared_reads != nullptr) {
        if (auto it = shared_reads->find(rk); it != shared_reads->end()) {
          fresh = it->second;
        }
      }
      if (fresh.isNull()) {
        fresh = tm_.mkConst(bv(mc.sig.bits), std::string(prefix) + rk);
      }
      if (!dout_dpin.is_invalid()) {
        pin2val[pinkey(dout_dpin)] = Val{fresh, mc.sig.bits, sgn};
      }
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

  // ---- Combinational nodes. forward_hier() virtual-flattens the design: it
  // descends into every sub-instance body (so a StageReg/ALU instance's internal
  // nodes are visited here) and inp_edges() resolves drivers across instance
  // boundaries. Flop/Memory state is cut (seeded above), so the combinational
  // graph is ACYCLIC — but forward_hier() can still emit a node before a driver
  // that lives in a loop_break sub-instance emitted earlier (e.g. a register
  // file's combinational read, whose read-address is computed in the parent).
  // So process to a FIXPOINT: a node whose operands are not yet resolved is
  // deferred to a later pass; an acyclic graph converges, and a node still stuck
  // after no-progress is a genuine error (a real comb cycle / missing driver).
  absl::flat_hash_set<std::string> done;
  auto nodekey = [](const hhds::Node_class& n) -> std::string { return box_node_key(n); };
  bool         progress    = true;
  unsigned int budget_tick = 0;  // throttles the per-node deadline check (avoids a now() per node)
  while (progress) {
    // Budget-aware encode: bail before another full fixpoint pass over the
    // virtually-flattened design — this loop, not cvc5, is the deep-parent time sink.
    if (over_budget()) {
      return fail("encode budget exceeded during combinational fixpoint (lec.timeout); raise --set lec.timeout");
    }
    progress = false;
    for (auto node : g->forward_hier(true, false, opaque)) {
      // A single forward_hier pass over a huge flattened design can itself blow
      // the budget before the while-head re-checks; sample the clock every 1024 nodes.
      if ((++budget_tick & 0x3ff) == 0 && over_budget()) {
        return fail("encode budget exceeded during combinational fixpoint (lec.timeout); raise --set lec.timeout");
      }
      auto op = gu::type_op_of(node);

      // Skip nodes with no consumers (cgen does the same). A memory with no read
      // ports is unobservable (its contents are never sampled) -> sound to skip.
      if (!node.has_out_edges()) {
        continue;
      }
      // Constants are resolved on demand at use sites.
      if (op == Ntype_op::Nconst) {
        continue;
      }
      // Flops were cut above (Q seeded; next-state emitted below) — skip the cell.
      if (op == Ntype_op::Flop) {
        continue;
      }
      // Memory is cut in two phases (read douts seeded before this loop, writes +
      // dout-tie emitted after) — like a flop. Skip the cell here.
      if (op == Ntype_op::Memory) {
        continue;
      }

      // Still out of scope: Fflop (no reset model yet), latches.
      if (op == Ntype_op::Fflop || op == Ntype_op::Latch) {
        return fail("sequential op '" + std::string(Ntype::get_name(op)) + "' not supported yet (M2 = Flop only)");
      }
      if (done.contains(nodekey(node))) {
        continue;  // already encoded in an earlier fixpoint pass
      }
    // Sub (instance) flattening (M5): encode the def inline with its inputs
    // bound to this instance's input Vals, and wire its outputs onto this
    // instance's output pins. Combinational defs only (e.g. ABC standard cells);
    // anything unresolved or stateful keeps the sound `Sub -> fail`.
    if (op == Ntype_op::Sub) {
      auto sub_io = node.get_subnode_io();
      // Proven-module collapse (lec.collapse): a def the driver already proved
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
        in_pw[sub_io->get_input_port_id(d.name)] = static_cast<int>(d.bits);
      }
      for (const auto& d : sub_io->get_output_pin_decls()) {
        out_name[sub_io->get_output_port_id(d.name)] = d.name;
      }

      // Resolve to a *combinational* def for inline flattening (M5): only when a
      // resolution library is supplied AND the def has no state. Otherwise the
      // instance is a BLACKBOX and is collapsed below (shared outputs / mitered
      // inputs) — sound when both designs carry the corresponding instance.
      hhds::Graph* def = nullptr;
      if (!force_collapse && sub_lib_ != nullptr && sub_depth_ <= 32) {
        if (auto git = sub_lib_->find(node.get_subnode_gid()); git != sub_lib_->end() && git->second != nullptr) {
          def        = git->second;
          for (auto dn : def->forward_class()) {  // combinational defs only (sound)
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
          return fail("Sub def '" + std::string(def->get_name()) + "' encode failed: " + sub_out.error);
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
            int ow = real_width(dp);
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
                ov        = Val{fit(cit->second, ow), ow, osgn};
              } else {
                ov = Val{tm_.mkConst(bv(ow), std::string(prefix) + "cb:" + bk + ":" + port), ow, osgn};  // unshared (sound)
              }
            } else {
              // NOT shared across the designs: sharing without the bbin input
              // obligations would be an unjustified equality. The phase-2 UF tie
              // provides the (pairing-free) congruence instead.
              // NO x_mask is minted here even under lec.gold_x=ignore: a box is
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
          Val S = seed_state(std::string("\x01") + "leafstate:" + bk, sbox->state_w, false);
          bb_state_sym[nk] = S;
          for (const auto& e : node.out_edges()) {
            auto        dp   = e.driver;
            auto        nit  = out_name.find(dp.get_port_id());
            std::string port = nit != out_name.end() ? nit->second : std::to_string(dp.get_port_id());
            auto        ofn  = sbox->out_fn.find(port);
            if (ofn == sbox->out_fn.end()) {
              return fail("state box for '" + defname + "' has no output UF for port '" + port + "'");
            }
            int ow = sbox->out_w.count(port) ? sbox->out_w.at(port) : std::max(1, real_width(dp));
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
              int w = real_width(dp);
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
          std::fprintf(stderr, "[LEC_ENC pfx=%s] bbin %s:%s w=%d xmask=%d term=%s\n", std::string(prefix).c_str(),
                       bk.c_str(), port.c_str(), v.width, v.x_mask.isNull() ? 0 : 1, ts.c_str());
        }
        // The bbin points are OBLIGATIONS (they justify the shared output
        // symbols), not golden outputs: excluding ref-X bits from them (the
        // generic gold_x=ignore compare rule) would let the inputs differ on
        // those bits while the box outputs stay forced equal — a false-PASS
        // channel. Strip the X plane so the obligation compares full values;
        // an X-driven divergence at worst refutes, which is flat-confirmed.
        Val ov2 = v;
        ov2.x_mask = cvc5::Term{};
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

    auto dpin = node.get_driver_pin(0);
    int  W    = real_width(dpin);
    if (W == 0) {
      // Comparisons/reductions are 1-bit; default unknown width to that.
      W = 1;
    }
    // An ABC standard-cell netlist's Set_mask output concat uses RAW net widths
    // (its unsigned nets carry no spare sign bit), so the front-end magnitude+1
    // real_width would truncate the running concatenation a bit at a time. Use
    // the raw bits there so the stored value keeps full width.
    if (sub_lib_ != nullptr && op == Ntype_op::Set_mask) {
      W = std::max(1, gu::bits_of(dpin));
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

    Term result;

    switch (op) {
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor: {
        Kind k = (op == Ntype_op::And) ? Kind::BITVECTOR_AND : (op == Ntype_op::Or) ? Kind::BITVECTOR_OR : Kind::BITVECTOR_XOR;
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
        Term a = fit(pid(0)[0], W);
        Term acc;
        for (const auto& b : pid(1)) {  // one-hot amounts, ORed
          // A shift count is UNSIGNED (a bit position), so zero-extend it. Reading
          // it as signed would sign-extend a wrapped 3-bit amount like 7 (== -1)
          // to a huge value, overshifting `1 << amt` to 0 (e.g. RISC-V vlmax).
          Term shamt = fit_to(tm_, Val{b.term, b.width, false}, W);
          Term sh    = tm_.mkTerm(Kind::BITVECTOR_SHL, {a, shamt});
          acc        = acc.isNull() ? sh : tm_.mkTerm(Kind::BITVECTOR_OR, {acc, sh});
        }
        result = acc.isNull() ? a : acc;
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
        const Val& a   = pid(0)[0];
        int        cw  = std::max(a.width, std::max(W, 1));
        Term       af  = fit(a, cw);
        Term       shf = fit_to(tm_, Val{pid(1)[0].term, pid(1)[0].width, false}, cw);  // shift amount: unsigned
        Kind       k   = a.is_signed ? Kind::BITVECTOR_ASHR : Kind::BITVECTOR_LSHR;
        result         = fit_to(tm_, Val{tm_.mkTerm(k, {af, shf}), cw, a.is_signed}, W);
        break;
      }
      case Ntype_op::Sext: {
        if (pid(0).empty() || pid(1).empty()) {
          return fail("Sext missing a/pos");
        }
        const Val& a = pid(0)[0];
        // pos must be a constant we can read.
        hhds::Pin_class pos_pin;
        for (const auto& e : node.inp_edges()) {
          if (e.sink.get_port_id() == 1) {
            pos_pin = e.driver;
            break;
          }
        }
        if (pos_pin.is_invalid() || !gu::is_const_pin(pos_pin)) {
          return fail("Sext with non-constant position not supported (M1)");
        }
        Dlop posc = gu::hydrate_const(pos_pin);
        if (!posc.is_just_i64()) {
          return fail("Sext position too wide (M1)");
        }
        int pos = static_cast<int>(posc.to_just_i64());
        if (pos < 1) {
          return fail("Sext position out of range (M1)");
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
        const Val&      a = pid(0)[0];
        hhds::Pin_class mask_pin;
        for (const auto& e : node.inp_edges()) {
          if (e.sink.get_port_id() == Ntype::get_sink_pid(op, "mask")) {
            mask_pin = e.driver;
            break;
          }
        }
        if (mask_pin.is_invalid() || !gu::is_const_pin(mask_pin)) {
          return fail("Get_mask with non-constant mask not supported (M1)");
        }
        Dlop mask = gu::hydrate_const(mask_pin);
        if (mask.is_just_i64() && mask.to_just_i64() == -1) {
          // zero-extend (sign -> unsigned cast)
          Val zext{a.term, a.width, false};
          result = fit(zext, W);
          break;
        }
        auto range = mask.get_mask_range();  // [begin, end)
        int  rb = range.first, re = range.second;
        if (rb < 0 || re <= rb) {
          return fail("Get_mask non-contiguous mask not supported (M1)");
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
        hhds::Pin_class mask_pin;
        for (const auto& e : node.inp_edges()) {
          if (e.sink.get_port_id() == Ntype::get_sink_pid(op, "mask")) {
            mask_pin = e.driver;
            break;
          }
        }
        if (mask_pin.is_invalid() || !gu::is_const_pin(mask_pin)) {
          return fail("Set_mask with non-constant mask not supported (M1)");
        }
        if (pid(0).empty()) {
          return fail("Set_mask missing a");
        }
        const Val& a    = pid(0)[0];
        Dlop       mask = gu::hydrate_const(mask_pin);
        if (mask.is_known_zero()) {
          result = fit(a, W);  // nothing replaced
          break;
        }
        // Full contiguous-mask bit-insert (the bit-blast's output concat): out[i]
        // = (rb<=i<re) ? value[i-rb] : a[i].  Works for both the front-end path
        // (W = magnitude+1 real_width) and an ABC standard-cell netlist (W = RAW
        // net width — set above at the dpin width branch, its unsigned nets carry
        // no spare sign bit).  `a` and `value` are already stored at their own
        // real_widths; fit() reconciles them to the window/result widths.
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
            aw = r;
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
        // A multiplexer's result is exactly as wide as its (equal-width) arms — the
        // declared output-pin width is advisory and a reader may UNDER-stamp it. The
        // native slang reader stamps a `casez` result width from the SELECTOR (e.g. a
        // 2-bit forwardB) rather than the 64-bit data arms, so real_width(dpin) came
        // back 1 and the arms were truncated to bit 0 (`casez_tmp = data[0]`), a false
        // REFUTE against a reader that keeps full width. Take the max of the declared
        // width and the widest arm so the data is never narrowed by a bad pin stamp.
        // A mux selects whole arms, so its result is the full declared bus width,
        // NOT the sign-magnitude-reduced real_width(dpin) (which drops the spare
        // sign bit for an unsigned pin). Without this, an unsigned output fed a
        // narrower SIGNED arm — e.g. yosys-slang lowers `we ? 8'hFF : 8'h00` with a
        // true-arm `1'sh1` (1-bit signed -1) — sign-extends into the dropped MSB and
        // truncates (0xFF -> 0x7F), a false REFUTE. Clamp up to bits_of (widen only).
        if (int db = gu::bits_of(dpin); db > W) {
          W = db;
        }
        for (const auto& a : arms) {
          W = std::max(W, a.width);  // never narrow the data below its arms
        }
        // default else = last arm (covers in-range exactly + out-of-range det.)
        result = fit(arms.back(), W);
        for (int k = static_cast<int>(arms.size()) - 2; k >= 0; --k) {
          int64_t key  = (op == Ntype_op::Mux) ? k : (int64_t{1} << k);
          Term    cond = tm_.mkTerm(Kind::EQUAL, {sel.term, bv_const(tm_, sel.width, static_cast<uint64_t>(key))});
          result       = tm_.mkTerm(Kind::ITE, {cond, fit(arms[k], W), result});
        }
        break;
      }
      default: return fail("unsupported op '" + std::string(Ntype::get_name(op)) + "' (M1)");
    }

      if (result.isNull()) {
        return fail("op '" + std::string(Ntype::get_name(op)) + "' produced no term");
      }
      Val out_val{result, W, out_signed};
      if (x_dontcare_) {
        // Undef bit-plane propagation (lec.gold_x=ignore, REF side). Exact for
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
            if (!sel.x_mask.isNull() || arms.empty()) {
              out_val.x_mask = ones_w;  // unknown selector: everything may be X
            } else {
              auto arm_xm = [&](const Val& a) -> Term {
                Val aw = a;
                aw.width = a.width;
                Term u = fit_x_mask_to(tm_, aw, W);
                return u.isNull() ? zero_w : u;
              };
              Term u = arm_xm(arms.back());
              for (int k = static_cast<int>(arms.size()) - 2; k >= 0; --k) {
                int64_t key  = (op == Ntype_op::Mux) ? k : (int64_t{1} << k);
                Term    cond = tm_.mkTerm(Kind::EQUAL, {sel.term, bv_const(tm_, sel.width, static_cast<uint64_t>(key))});
                u            = tm_.mkTerm(Kind::ITE, {cond, arm_xm(arms[k]), u});
              }
              out_val.x_mask = u;
            }
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
                if (!cur.empty()) s.insert(std::stoull(cur));
                cur.clear();
              } else {
                cur += c;
              }
            }
            if (!cur.empty()) s.insert(std::stoull(cur));
          }
          return s;
        }();
        if (!dbg_nodes.empty()) {
          uint64_t nid = static_cast<uint64_t>(node.get_debug_nid());
          if (dbg_nodes.count(nid)) {
            out.outputs[std::string("\x03" "dbg:") + std::string(prefix) + std::to_string(nid)] = Val{result, W, out_signed};
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
  for (auto node : g->forward_hier(true, false, opaque)) {
    auto op = gu::type_op_of(node);
    if (!node.has_out_edges() || op == Ntype_op::Nconst || op == Ntype_op::Flop || op == Ntype_op::Memory) {
      continue;
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
      std::vector<std::string>       chain;
      absl::flat_hash_set<uint64_t>  seen;
      auto cur      = node;
      bool diagnosed = false;
      std::string diag;
      while (!diagnosed) {
        if (!seen.insert(static_cast<uint64_t>(cur.get_debug_nid())).second) {
          diag = "WORD-LEVEL CYCLE through: ";
          for (const auto& c : chain) {
            diag += c + " -> ";
          }
          diag += gu::debug_name(cur);
          diagnosed = true;
          break;
        }
        chain.push_back(gu::debug_name(cur));
        bool hopped = false;
        for (const auto& e : cur.inp_edges()) {
          const auto& drv = e.driver;
          if (gu::is_const_pin(drv)) {
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
        }
        if (diagnosed) {
          break;
        }
        if (!hopped) {
          diag      = "'" + gu::debug_name(cur) + "' has all operands resolved yet never encoded (deferred op?)";
          diagnosed = true;
        }
      }
      return fail("operand of '" + gu::debug_name(node) + "' has no encodable driver (combinational cycle?); root: " + diag);
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
    for (auto node : g->forward_hier(true, false, opaque)) {
      if (gu::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      auto sio = node.get_subnode_io();
      if (sio == nullptr || sio->get_name() != gu::fproperty_module_name) {
        continue;
      }
      // Resolve the cond driver across instance boundaries via the HIER
      // inp_edges (get_driver_of_sink_name stops at a sub's GraphIO pin).
      const auto      cond_pid = sio->get_input_port_id("cond");
      hhds::Pin_class cond_drv;
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
    for (auto node : g->forward_hier(true, false, opaque)) {
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
        const int w = decl_w > 0 ? decl_w : (v.width > 0 ? v.width : 1);
        out.outputs["\x05tap:" + hname + "." + port] = Val{fit(v, w), w, decl_sgn};
      };
      // INPUT ports: the parent-side driver, resolved across the instance
      // boundary via the HIER inp_edges (the fproperty cond pattern).
      for (const auto& d : sio->get_input_pin_decls()) {
        const auto      pid = sio->get_input_port_id(d.name);
        hhds::Pin_class drv;
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
      auto same_pin = [](const hhds::Pin_class& a, const hhds::Pin_class& b) {
        auto ah = a.get_hier_pos();
        auto bh = b.get_hier_pos();
        if (ah == hhds::INVALID) {
          ah = hhds::ROOT;
        }
        if (bh == hhds::INVALID) {
          bh = hhds::ROOT;
        }
        return a.get_current_gid() == b.get_current_gid() && ah == bh && a.get_debug_pid() == b.get_debug_pid();
      };
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
  // A flop deep in a sub-instance has its din/enable/reset driven across the
  // instance boundary, so resolve the named sink via the flop's HIER inp_edges()
  // (which inp_edges() threads through the hierarchy) rather than the class-local
  // get_driver_of_sink_name (which would stop at the sub's GraphIO pin).
  auto hier_sink_driver = [](const hhds::Node_class& n, std::string_view sink_name) -> hhds::Pin_class {
    auto pid = Ntype::get_sink_pid(Ntype_op::Flop, sink_name);
    for (const auto& e : n.inp_edges()) {
      if (e.sink.get_port_id() == pid) {
        return e.driver;
      }
    }
    return {};
  };
  for (size_t fi = 0; fi < flops.size(); ++fi) {
    const auto& node  = flops[fi];
    const int   depth = flop_depths[fi];
    auto        qpin  = node.get_driver_pin(0);
    int         w     = real_width(qpin);
    if (w == 0) {
      w = 1;
    }
    bool       sgn = !gu::is_unsign(qpin);
    const Val& qv  = pin2val[pinkey(qpin)];  // Q (output stage) current-state, seeded above
    bool       ok  = true;

    // din: the value the FIRST stage latches each cycle (no enable/reset yet).
    bool has_din = false;
    Term din;
    Term din_xm;  // undef plane of din, fitted to w (null = fully known)
    if (auto din_d = hier_sink_driver(node, "din"); !din_d.is_invalid()) {
      Val dv = driver_val(din_d, ok);
      if (!ok) {
        return fail("flop '" + gu::debug_name(node) + "' din not encodable");
      }
      din       = fit(dv, w);
      din_xm = fit_x_mask_to(tm_, dv, w);
      has_din   = true;
    }

    // enable: a low write-enable holds the whole shift register (no din=q
    // feedback mux in the graph). const-false => the register never writes.
    bool enable_const_false = false;
    bool has_enable         = false;
    Term en_hot;
    if (auto en_d = hier_sink_driver(node, "enable"); !en_d.is_invalid()) {
      if (gu::is_const_pin(en_d)) {
        enable_const_false = gu::hydrate_const(en_d).is_known_false();
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
    bool has_reset = false;
    Term rst_hot;
    Term initv;
    if (auto rst_d = hier_sink_driver(node, "reset_pin"); !rst_d.is_invalid() && !gu::is_const_pin(rst_d)) {
      Val rv = driver_val(rst_d, ok);
      if (!ok) {
        return fail("flop '" + gu::debug_name(node) + "' reset not encodable");
      }
      Term rbit     = tm_.mkTerm(Kind::DISTINCT, {rv.term, bv_const(tm_, rv.width, 0)});
      bool negreset = false;
      if (auto neg_d = hier_sink_driver(node, "negreset"); !neg_d.is_invalid() && gu::is_const_pin(neg_d)) {
        negreset = !gu::hydrate_const(neg_d).is_known_false();
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
    auto                    stage_cur = [&](int k) -> const Term& {
      return (k + 1 < depth) ? internals[static_cast<size_t>(k)].term : qv.term;
    };
    const std::string nm = flop_key(node.get_hier_name());
    // F7 source-map: resolve this flop's declaration to "file:line" once (the key
    // feeds every stage's \x01nxt: output and the downstream wit_state cuts). The
    // srcid rides tolg for a Pyrope node and the cgen ECMA-426 sourcemap for a
    // Verilog one; absent ⇒ no entry (the witness just renders the bare name).
    if (out.src_of_key.find(nm) == out.src_of_key.end()) {
      if (auto ref = node.attr(hhds::attrs::srcid); ref.has()) {
        auto span = g->source_locator().resolve_span(ref.get());
        if (!span.file.empty() && span.start_line.has_value()) {
          out.src_of_key[nm] = span.file + ":" + std::to_string(*span.start_line);
        }
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
      if (has_reset) {
        nval = tm_.mkTerm(Kind::ITE, {rst_hot, initv, nval});
      }
      std::string key = (k + 1 < depth) ? (nm + "\x02p" + std::to_string(k)) : nm;
      Val         nv{nval, w, sgn};
      if (x_dontcare_) {
        // Mirror the next-state mux over the undef planes so a ?-fed flop's
        // next-state compare can be masked (the write path may carry X; the
        // hold path inherits the Q's plane; a reset override is known).
        Term self_u = (k + 1 < depth) ? fit_x_mask_to(tm_, internals[static_cast<size_t>(k)], w) : fit_x_mask_to(tm_, qv, w);
        Term src_u  = (k == 0) ? (has_din ? din_xm : self_u)
                               : fit_x_mask_to(tm_, internals[static_cast<size_t>(k - 1)], w);
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
        std::fprintf(stderr, "[LEC_ENC pfx=%s] nxt '%s' stage=%d/%d w=%d xmask=%d nval=%s\n", std::string(prefix).c_str(),
                     key.c_str(), k, depth, w, nv.x_mask.isNull() ? 0 : 1, ts.c_str());
      }
    }
  }

  // ---- M4 memory cut, phase 2: now that write addr/din/enable are resolved,
  // build the next-state array and tie each fresh read dout to its real value.
  for (auto& mc : mem_cuts) {
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
      Term a_upd = array_from_bus(mc.a_cur, fit_unsigned(uv, mc.sig.size * mc.sig.bits));
      if (!mc.update_enable.is_invalid()) {
        Val ev = driver_val(mc.update_enable, ok);
        if (ok) {
          Term en_hot = tm_.mkTerm(Kind::DISTINCT, {ev.term, bv_const(tm_, ev.width, 0)});
          a_upd       = tm_.mkTerm(Kind::ITE, {en_hot, a_upd, mc.a_cur});
        }
      }
      a_next = a_upd;
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
    for (auto& p : mc.ports) {
      if (p.rd || p.din.is_invalid()) {
        continue;
      }
      Val av = driver_val(p.addr, ok);
      Val dv = driver_val(p.din, ok);
      if (!ok) {
        return fail("memory '" + gu::debug_name(mc.node) + "' write addr/din not encodable");
      }
      Term addr = fit_unsigned(av, mc.sig.addr_w);
      Term din  = fit_to(tm_, Val{dv.term, dv.width, false}, mc.sig.bits);
      // Per-bit write mask: word-enable (wensize<=1) replicates the enable hot
      // bit across all bits; per-bit-enable (wensize>=bits) uses the mask.
      Term wmask;
      if (p.en.is_invalid()) {
        wmask = tm_.mkTerm(Kind::BITVECTOR_NOT, {bv_const(tm_, mc.sig.bits, 0)});
      } else {
        Val ev = driver_val(p.en, ok);
        if (!ok) {
          return fail("memory '" + gu::debug_name(mc.node) + "' enable not encodable");
        }
        if (mc.wensize >= mc.sig.bits && mc.wensize > 1) {
          wmask = fit_to(tm_, Val{ev.term, ev.width, false}, mc.sig.bits);
        } else {
          Term en_hot = tm_.mkTerm(Kind::DISTINCT, {ev.term, bv_const(tm_, ev.width, 0)});
          wmask       = tm_.mkTerm(Kind::ITE, {en_hot, tm_.mkTerm(Kind::BITVECTOR_NOT, {bv_const(tm_, mc.sig.bits, 0)}),
                                               bv_const(tm_, mc.sig.bits, 0)});
        }
      }
      Term old      = tm_.mkTerm(Kind::SELECT, {a_next, addr});
      Term keep     = tm_.mkTerm(Kind::BITVECTOR_AND, {tm_.mkTerm(Kind::BITVECTOR_NOT, {wmask}), old});
      Term set      = tm_.mkTerm(Kind::BITVECTOR_AND, {wmask, din});
      Term new_word = tm_.mkTerm(Kind::BITVECTOR_OR, {keep, set});
      a_next        = tm_.mkTerm(Kind::STORE, {a_next, addr, new_word});
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
          init_bus = ok ? iv.term : bv_const(tm_, mc.sig.size * mc.sig.bits, 0);
        } else {
          init_bus = bv_const(tm_, mc.sig.size * mc.sig.bits, 0);
        }
        Term a_init = array_from_bus(mc.a_cur, init_bus);
        a_next      = tm_.mkTerm(Kind::ITE, {rst_hot, a_init, a_next});
      }
    }

    // Tie each fresh read dout to select(read-source, addr). A combinational
    // whole-array reads its POST-update contents (a_next == the live array); a
    // registered whole-array and plain memory read committed state (a_cur, since
    // fwd is forced 0 for whole-arrays); fwd!=0 plain memories forward writes.
    const Term& rd_src = mc.is_comb ? a_next : ((mc.fwd != 0) ? a_next : mc.a_cur);
    for (size_t k = 0; k < mc.rd_fresh.size(); ++k) {
      Val av = driver_val(mc.rd_addr[k], ok);
      if (!ok) {
        return fail("memory '" + gu::debug_name(mc.node) + "' read addr not encodable");
      }
      Term addr = fit_unsigned(av, mc.sig.addr_w);
      Term real = tm_.mkTerm(Kind::SELECT, {rd_src, addr});
      if (mc.mtype == 1 && shared_reads != nullptr) {
        // Sync read (latency-1): rd_fresh is the CURRENT registered dout (seeded
        // from shared_reads in phase 1); THIS cycle's read is its NEXT state,
        // threaded forward by the caller like next_mem. The dout thus lands one
        // cycle after the address. (Gated on shared_reads so callers that do not
        // thread it keep the latency-0 tie below — a sound conservative default.)
        out.next_read[mc.rd_key[k]] = real;
      } else {
        out.equalities.emplace_back(mc.rd_fresh[k], real);
      }
    }
    // Combinational read_all: tie its deferred symbol to CONCAT(SELECT(rd_src,i)).
    if (mc.is_comb && !mc.ra_fresh.isNull()) {
      Term bus = tm_.mkTerm(Kind::SELECT, {rd_src, bv_const(tm_, mc.sig.addr_w, 0)});
      for (int i = 1; i < mc.sig.size; ++i) {
        Term ei = tm_.mkTerm(Kind::SELECT, {rd_src, bv_const(tm_, mc.sig.addr_w, static_cast<uint64_t>(i))});
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
  // boundary pin — the ambient Hier_opaque_scope covers this walk too).
  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> out_driver;
  {
    hhds::Node_class out_hier(g, static_cast<hhds::Gid>(hhds::Gid_invalid), static_cast<hhds::Tree_pos>(hhds::ROOT),
                              hhds::Graph::OUTPUT_NODE);
    for (const auto& e : out_hier.inp_edges()) {
      out_driver.emplace(e.sink.get_port_id(), e.driver);
    }
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    auto spin = g->get_output_pin(d.name);
    if (spin.is_invalid()) {
      continue;
    }
    auto dit = out_driver.find(gio->get_output_port_id(d.name));
    if (dit == out_driver.end()) {
      return fail("output '" + d.name + "' is undriven");
    }
    bool ok = true;
    Val  v  = driver_val(dit->second, ok);
    if (!ok) {
      return fail("output '" + d.name + "' driver not encodable");
    }
    int ow = real_width_io(spin, *gio, d.name);
    if (ow == 0) {
      // A width-less output port is a 1-bit scalar (Verilog: a port with no
      // range is one bit) — NOT the width of whatever drives it. Defaulting to
      // the driver width left a scalar output carrying a wide internal value
      // (e.g. a 65-bit mux feeding a 1-bit `busy_o`), which then mismatched the
      // other reader's correctly-1-bit output. fit() truncates the driver to it.
      ow = 1;
    }
    Val ov{fit(v, ow), ow, !gio->is_unsign(d.name)};
    ov.x_mask            = fit_x_mask_to(tm_, v, ow);
    out.outputs[d.name] = ov;
  }

  return out;
}

}  // namespace livehd::lec
