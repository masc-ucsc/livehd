// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "encode.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
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
  int b = gu::bits_of(pin, gio, name);
  if (b == 0) {
    return 0;
  }
  bool uns = pin.is_invalid() ? gio.is_unsign(name) : gu::is_unsign(pin);
  return uns ? b - 1 : b;
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

// Concrete reset/initial value of a flop's `initial` pin (the reset value), as a
// `width`-bit BV. Returns nullopt for a reset-less flop (no constant initial) —
// its power-on value is arbitrary, so the BMC caller seeds a fresh shared symbol
// instead. Unknown bits in the initial are masked to 0 (a defined reset).
std::optional<Val> flop_initial(cvc5::TermManager& tm, const hhds::Node_class& node, int width) {
  auto init_d = gu::get_driver_of_sink_name(node, "initial");
  if (init_d.is_invalid() || !gu::is_const_pin(init_d)) {
    return std::nullopt;
  }
  Dlop c = gu::hydrate_const(init_d);
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
std::string mem_state_key(const Mem_sig& sig, int occ) {
  return std::format("\x01m:{}x{}:r{}w{}#{}", sig.size, sig.bits, sig.n_rd, sig.n_wr, occ);
}

Encoded Encoder::encode(hhds::Graph* g, const absl::flat_hash_map<std::string, Val>* shared_inputs, std::string_view prefix,
                        const absl::flat_hash_map<std::string, cvc5::Term>* shared_mems) {
  Encoded out;
  auto    gio = g->get_io();

  // driver-pin Class_index -> Val (SSA value table)
  absl::flat_hash_map<hhds::Class_index, Val> pin2val;
  absl::flat_hash_map<std::string, int>       bbox_occ;  // blackbox Sub occurrence per def-name (forward_class order)

  auto fail = [&](const std::string& msg) -> Encoded& {
    if (out.ok) {
      out.ok    = false;
      out.error = msg;
    }
    return out;
  };

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
      // (Refine to a shared free symbol if X-sensitive equivalence is needed.)
      auto bin = c.to_binary();
      for (auto& ch : bin) {
        if (ch != '0' && ch != '1') {
          ch = '0';
        }
      }
      if (bin.empty()) {
        bin = "0";
      }
      width = static_cast<int>(bin.size());
      t     = tm_.mkBitVector(static_cast<uint32_t>(width), bin, 2);
    }
    return Val{t, width, sgn};
  };

  // Resolve a driver pin to its Val (constant literal or a computed SSA value).
  auto driver_val = [&](const hhds::Pin_class& dpin, bool& ok) -> Val {
    ok = true;
    if (gu::is_const_pin(dpin)) {
      return const_val(dpin);
    }
    auto it = pin2val.find(dpin.get_class_index());
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
        // Reconcile a width disagreement across the two designs by EXTENDING up
        // to the wider view, never truncating: the readers can undercount an
        // input's bus width by a sign-bit slot (the "bit-width trap"), and
        // truncating the shared symbol to the narrower side would drop the top
        // bit (e.g. d=0x80 -> 0), a spurious mismatch. The shared symbol is built
        // at the max width in query.cpp, so this only ever extends (defensively).
        if (v.width < w) {
          v.term  = fit(v, w);
          v.width = w;
        }
      }
    }
    if (v.term.isNull()) {
      v = Val{tm_.mkConst(bv(w), std::string(prefix) + d.name), w, sgn};
    }
    out.inputs[d.name] = v;
    if (!dpin.is_invalid()) {
      pin2val[dpin.get_class_index()] = v;
    }
  }

  // ---- M2 flop cut-points (register-correspondence SEC). Each Flop's Q (driver
  // pin 0) is a CURRENT-STATE symbol, shared across the two designs by its
  // preserved name (so a 1:1 latch map falls out of name equality). Seeded here,
  // before the combinational loop, so downstream comb reads resolve it like an
  // input; the matching NEXT-STATE value is emitted as a synthetic output after
  // the loop, and the miter then compares next-states alongside primary outputs.
  std::vector<hhds::Node_class> flops;
  for (auto node : g->forward_class()) {
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
    std::string nm  = flop_state_key(*g, node);
    Val         v;
    if (shared_inputs != nullptr) {
      if (auto it = shared_inputs->find(nm); it != shared_inputs->end()) {
        v = it->second;
        if (v.width < w) {  // extend up only; never truncate (see input note above)
          v.term  = fit(v, w);
          v.width = w;
        }
      }
    }
    if (v.term.isNull()) {
      v = Val{tm_.mkConst(bv(w), std::string(prefix) + nm), w, sgn};
    }
    pin2val[qpin.get_class_index()] = v;
    out.inputs[nm]                  = v;
    flops.push_back(node);
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
    cvc5::Term          a_cur;
    std::vector<Term>   rd_fresh;  // fresh dout symbol per read port (port order)
    std::vector<hhds::Pin_class> rd_addr;
  };
  std::vector<MemCut>                   mem_cuts;
  absl::flat_hash_map<std::string, int> mem_occ;  // per-signature occurrence -> stable key
  for (auto node : g->forward_class()) {
    if (gu::type_op_of(node) != Ntype_op::Memory || !node.has_out_edges()) {
      continue;
    }
    MemCut mc;
    mc.node = node;
    mc.sig  = read_mem_sig(node);
    if (mc.sig.bits <= 0 || mc.sig.size <= 0) {
      return fail("memory '" + gu::debug_name(node) + "' missing bits/size");
    }
    std::string sg = std::to_string(mc.sig.size) + "x" + std::to_string(mc.sig.bits) + ":r" + std::to_string(mc.sig.n_rd)
                   + "w" + std::to_string(mc.sig.n_wr);
    mc.key = mem_state_key(mc.sig, mem_occ[sg]++);
    for (auto e : node.inp_edges()) {
      auto        raw_pid = static_cast<int>(e.sink.get_port_id());
      std::string pn      = Ntype::get_sink_name(Ntype_op::Memory, raw_pid);
      size_t      pid     = static_cast<size_t>(raw_pid) / 12;
      if (pn == "wensize") {
        mc.wensize = static_cast<int>(gu::hydrate_const(e.driver).to_just_i64());
      } else if (pn == "fwd") {
        mc.fwd = static_cast<int>(gu::hydrate_const(e.driver).to_just_i64());
      } else if (pn == "bits" || pn == "size" || pn == "type" || pn == "init" || pn == "posclk"
                 || ends_with(pn, "clock_pin")) {
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
    // Current contents: shared array symbol (collapse) or fresh.
    Sort asort = tm_.mkArraySort(bv(mc.sig.addr_w), bv(mc.sig.bits));
    if (shared_mems != nullptr) {
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
      auto dout_dpin = node.create_driver_pin(static_cast<hhds::Port_id>(mc.sig.n_wr + n_rd_pos));
      bool sgn       = dout_dpin.is_invalid() ? false : !gu::is_unsign(dout_dpin);
      Term fresh     = tm_.mkConst(bv(mc.sig.bits), std::string(prefix) + mc.key + ":rd" + std::to_string(n_rd_pos));
      if (!dout_dpin.is_invalid()) {
        pin2val[dout_dpin.get_class_index()] = Val{fresh, mc.sig.bits, sgn};
      }
      mc.rd_fresh.push_back(fresh);
      mc.rd_addr.push_back(p.addr);
      ++n_rd_pos;
    }
    mem_cuts.push_back(std::move(mc));
  }

  // ---- Combinational nodes in topological order.
  for (auto node : g->forward_class()) {
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
    // Sub (instance) flattening (M5): encode the def inline with its inputs
    // bound to this instance's input Vals, and wire its outputs onto this
    // instance's output pins. Combinational defs only (e.g. ABC standard cells);
    // anything unresolved or stateful keeps the sound `Sub -> fail`.
    if (op == Ntype_op::Sub) {
      auto                                            sub_io = node.get_subnode_io();
      absl::flat_hash_map<hhds::Port_id, std::string> in_name;
      absl::flat_hash_map<hhds::Port_id, std::string> out_name;
      for (const auto& d : sub_io->get_input_pin_decls()) {
        in_name[sub_io->get_input_port_id(d.name)] = d.name;
      }
      for (const auto& d : sub_io->get_output_pin_decls()) {
        out_name[sub_io->get_output_port_id(d.name)] = d.name;
      }

      // Resolve to a *combinational* def for inline flattening (M5): only when a
      // resolution library is supplied AND the def has no state. Otherwise the
      // instance is a BLACKBOX and is collapsed below (shared outputs / mitered
      // inputs) — sound when both designs carry the corresponding instance.
      hhds::Graph* def = nullptr;
      if (sub_lib_ != nullptr && sub_depth_ <= 32) {
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
        absl::flat_hash_map<std::string, Val> bound;
        for (const auto& e : node.inp_edges()) {
          auto nit = in_name.find(e.sink.get_port_id());
          if (nit == in_name.end()) {
            return fail("Sub instance '" + gu::debug_name(node) + "' input pin has no IO name");
          }
          bool sok = true;
          Val  v   = driver_val(e.driver, sok);
          if (!sok) {
            return fail("Sub instance '" + gu::debug_name(node) + "' input has no encodable driver");
          }
          bound[nit->second] = v;
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
          pin2val[dp.get_class_index()] = oit->second;
        }
        continue;
      }

      // ---- BLACKBOX COLLAPSE. Key by module name + occurrence (forward_class
      // order), reader-invariant so corresponding instances align across designs.
      std::string defname(sub_io->get_name());
      std::string bk = defname + "#" + std::to_string(bbox_occ[defname]++);
      for (const auto& e : node.out_edges()) {  // outputs = shared free symbols
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
        pin2val[dp.get_class_index()] = ov;
      }
      for (const auto& e : node.inp_edges()) {  // inputs = miter comparison points
        auto        nit  = in_name.find(e.sink.get_port_id());
        std::string port = nit != in_name.end() ? nit->second : std::to_string(e.sink.get_port_id());
        bool        sok  = true;
        Val         v    = driver_val(e.driver, sok);
        if (!sok) {
          return fail("blackbox Sub '" + defname + "' input '" + port + "' has no encodable driver");
        }
        out.outputs[std::string("\x02") + "bbin:" + bk + ":" + port] = v;
      }
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
    bool                                                 ok = true;
    for (const auto& e : node.inp_edges()) {
      Val v = driver_val(e.driver, ok);
      if (!ok) {
        return fail("operand of '" + gu::debug_name(node) + "' has no encodable driver");
      }
      by_pid[e.sink.get_port_id()].push_back(v);
      all.push_back(v);
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
        Term a = fit(pid(0)[0], W);
        Term b = fit(pid(1)[0], W);
        result = tm_.mkTerm(out_signed ? Kind::BITVECTOR_SDIV : Kind::BITVECTOR_UDIV, {a, b});
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
            // Signed compare only if BOTH operands are signed; the extension must
            // match (zero-extend both when the compare is unsigned).
            bool both_signed = a.is_signed && b.is_signed;
            int  cw          = std::max(a.width, b.width);
            Term la          = fit_to(tm_, Val{a.term, a.width, both_signed}, cw);
            Term lb          = fit_to(tm_, Val{b.term, b.width, both_signed}, cw);
            Kind cmp = (op == Ntype_op::LT) ? (both_signed ? Kind::BITVECTOR_SLT : Kind::BITVECTOR_ULT)
                                            : (both_signed ? Kind::BITVECTOR_SGT : Kind::BITVECTOR_UGT);
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
        if (pos < 1 || pos > a.width) {
          return fail("Sext position out of range (M1)");
        }
        Term low = (pos == a.width) ? a.term : bv_extract(tm_, a.term, pos - 1, 0);
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
        // = (rb<=i<re) ? value[i-rb] : a[i]. Enabled when a resolution library is
        // present (an ABC standard-cell netlist), where the result width is the
        // RAW net width — its unsigned nets carry no spare sign bit, so use
        // bits_of(dpin), not the front-end magnitude+1 real_width.
        if (sub_lib_ == nullptr) {
          return fail("Set_mask (non-trivial) not supported (M1)");
        }
        int  Wm    = std::max(1, gu::bits_of(dpin));
        auto range = mask.get_mask_range();
        int  rb = range.first, re = range.second;
        if (rb < 0 || re <= rb) {
          return fail("Set_mask non-contiguous mask not supported (M1)");
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
    pin2val[dpin.get_class_index()] = Val{result, W, out_signed};
  }

  // ---- M2 next-state functions. For each cut flop emit the value it latches
  // next cycle as a synthetic output keyed "<nxt><statekey>", so the miter
  // compares the two designs' next-state for every corresponding register (the
  // inductive step: equal current state + equal inputs => equal next state &
  // outputs). N = reset_active ? initial : (enable ? din : Q). reset_pin is a
  // shared primary input, so the same query covers the reset/base case.
  for (const auto& node : flops) {
    auto qpin = node.get_driver_pin(0);
    int  w    = real_width(qpin);
    if (w == 0) {
      w = 1;
    }
    bool       sgn = !gu::is_unsign(qpin);
    const Val& qv  = pin2val[qpin.get_class_index()];  // current-state symbol (seeded above)
    bool       ok  = true;

    Term nval;
    if (auto din_d = gu::get_driver_of_sink_name(node, "din"); !din_d.is_invalid()) {
      Val dv = driver_val(din_d, ok);
      if (!ok) {
        return fail("flop '" + gu::debug_name(node) + "' din not encodable");
      }
      nval = fit(dv, w);
    } else {
      nval = qv.term;  // no din -> the register holds
    }

    // enable: a low write-enable holds Q (no din=q feedback mux in the graph).
    if (auto en_d = gu::get_driver_of_sink_name(node, "enable"); !en_d.is_invalid()) {
      if (gu::is_const_pin(en_d)) {
        if (gu::hydrate_const(en_d).is_known_false()) {
          nval = qv.term;  // never writes
        }
      } else {
        Val ev = driver_val(en_d, ok);
        if (!ok) {
          return fail("flop '" + gu::debug_name(node) + "' enable not encodable");
        }
        Term en_hot = tm_.mkTerm(Kind::DISTINCT, {ev.term, bv_const(tm_, ev.width, 0)});
        nval        = tm_.mkTerm(Kind::ITE, {en_hot, nval, qv.term});
      }
    }

    // reset: a non-const reset overrides with the initial value when asserted.
    if (auto rst_d = gu::get_driver_of_sink_name(node, "reset_pin"); !rst_d.is_invalid() && !gu::is_const_pin(rst_d)) {
      Val rv = driver_val(rst_d, ok);
      if (!ok) {
        return fail("flop '" + gu::debug_name(node) + "' reset not encodable");
      }
      Term rbit     = tm_.mkTerm(Kind::DISTINCT, {rv.term, bv_const(tm_, rv.width, 0)});
      bool negreset = false;
      if (auto neg_d = gu::get_driver_of_sink_name(node, "negreset"); !neg_d.is_invalid() && gu::is_const_pin(neg_d)) {
        negreset = !gu::hydrate_const(neg_d).is_known_false();
      }
      Term rst_hot = negreset ? tm_.mkTerm(Kind::NOT, {rbit}) : rbit;
      Term initv   = bv_const(tm_, w, 0);
      if (auto init_d = gu::get_driver_of_sink_name(node, "initial"); !init_d.is_invalid()) {
        Val iv = driver_val(init_d, ok);
        if (ok) {
          initv = fit(iv, w);
        }
      }
      nval = tm_.mkTerm(Kind::ITE, {rst_hot, initv, nval});
    }

    out.outputs[std::string("\x01nxt:") + flop_state_key(*g, node)] = Val{nval, w, sgn};
  }

  // ---- M4 memory cut, phase 2: now that write addr/din/enable are resolved,
  // build the next-state array and tie each fresh read dout to its real value.
  for (auto& mc : mem_cuts) {
    auto fit_unsigned = [&](const Val& v, int wd) -> Term { return fit_to(tm_, Val{v.term, v.width, false}, wd); };

    // Apply writes in port order: array' = store(array, addr, masked_din).
    Term a_next = mc.a_cur;
    bool ok     = true;
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

    // Tie each fresh read dout to select(read-source, addr). fwd!=0 reads see
    // this cycle's writes (a_next); else the pre-write contents (a_cur).
    const Term& rd_src = (mc.fwd != 0) ? a_next : mc.a_cur;
    for (size_t k = 0; k < mc.rd_fresh.size(); ++k) {
      Val av = driver_val(mc.rd_addr[k], ok);
      if (!ok) {
        return fail("memory '" + gu::debug_name(mc.node) + "' read addr not encodable");
      }
      Term addr = fit_unsigned(av, mc.sig.addr_w);
      Term real = tm_.mkTerm(Kind::SELECT, {rd_src, addr});
      out.equalities.emplace_back(mc.rd_fresh[k], real);
    }

    out.next_mem[mc.key] = a_next;
  }

  // ---- Outputs: value driving each output sink, fit to the declared width.
  for (const auto& d : gio->get_output_pin_decls()) {
    auto spin = g->get_output_pin(d.name);
    if (spin.is_invalid()) {
      continue;
    }
    auto edges = spin.inp_edges();
    if (edges.empty()) {
      return fail("output '" + d.name + "' is undriven");
    }
    bool ok = true;
    Val  v  = driver_val(edges.front().driver, ok);
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
    out.outputs[d.name] = Val{fit(v, ow), ow, !gio->is_unsign(d.name)};
  }

  return out;
}

}  // namespace livehd::lec
