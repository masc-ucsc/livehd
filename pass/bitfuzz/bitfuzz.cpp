//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bitfuzz.hpp"

#include "bitwidth.hpp"
#include "cell.hpp"
#include "diag.hpp"
#include "hash_util.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

namespace livehd::bitfuzz {

namespace {

// Bitwidth_range's "I could not bound this" encoding: set_sbits_range(0) /
// set_ubits_range(0) park max/min at +-32768 in overflow mode, and get_sbits()
// hands that back as a ~32769-bit width. Anything at or past the sentinel is an
// inference FAILURE wearing a concrete number, so it is classified with
// bits==0 rather than reported as a 32769-bit disagreement.
constexpr int32_t kSentinelBits = 32768;

[[nodiscard]] bool is_pathological(int32_t bits) { return bits <= 0 || bits >= kSentinelBits; }

// Cell types bw_pass has NO branch for (bitwidth.cpp:1528-1573). Clearing one
// of these is guaranteed unrecoverable, which is a gap in pass.bitwidth rather
// than a translation bug -- report it as its own class so a sweep triage does
// not confuse the two.
[[nodiscard]] bool has_no_inference_rule(Ntype_op op) {
  switch (op) {
    case Ntype_op::Div:
    case Ntype_op::Rem:
    case Ntype_op::LUT:
    case Ntype_op::Clock_cell: return true;
    default                  : return false;
  }
}

// Deterministic per-pin coin flip: a pure function of (seed, node, port), never
// of traversal order, so `--set compile.bitfuzz.seed=N` reproduces exactly the
// same cleared set on a re-run (what M3's bisect needs).
[[nodiscard]] bool selected(uint64_t seed, const hhds::Node_class& node, hhds::Port_id pid, int pct) {
  if (pct >= 100) {
    return true;
  }
  if (pct <= 0) {
    return false;
  }
  const auto h = hash_util::combine64(seed, (static_cast<uint64_t>(node.get_debug_nid()) << 16) ^ pid);
  return static_cast<int>(h % 100) < pct;
}

// Does any driver feeding `node` still lack a usable width? Graph inputs and
// constants never count: an IO port's declared width lives on the GraphIO decl
// rather than the pin attr (node_util.hpp bits_of(pin, gio, name)), and a
// constant carries its width in its value, so reading 0 off either pin says
// nothing about whether inference was starved.
[[nodiscard]] bool has_unsized_input(const hhds::Node_class& node) {
  if (node.is_invalid()) {
    return false;
  }
  for (const auto& e : node.inp_edges()) {
    const auto& d = e.driver;
    if (d.is_invalid() || gu::is_graph_input_pin(d) || gu::is_const_pin(d)) {
      continue;
    }
    if (gu::type_op_of(d.get_master_node()) == Ntype_op::Nconst) {
      continue;
    }
    if (is_pathological(gu::bits_of(d))) {
      return true;
    }
  }
  return false;
}

struct Snap {
  hhds::Node_class node;
  hhds::Pin_class  pin;
  std::string      name;
  Ntype_op         op       = Ntype_op::Invalid;
  int32_t          bits     = 0;
  bool             unsign   = true;
  bool             is_state = false;
  bool             restored = false;
  bool             cyclic   = false;
};

// Every driver pin this pass is allowed to strip.
//
// KEPT, and why:
//   graph IO      - the source RTL interface; bitwidth pins them anyway
//                   (bitwidth.cpp:167-172) and the LEC comparison is only
//                   well-defined while both sides keep the same port widths.
//   Nconst        - the width IS the value (process_const re-derives it).
//   const pins    - same, and they live on the shared CONST_NODE singleton.
//   Memory        - state contract, plus its geometry sinks must stay constant
//                   (process_memory hard-errors otherwise).
//   Sub           - instance boundary; treated like IO in v1.
//   Flop/Latch    - state, unless Mode::All.
//
// body().nodes() never emits the INPUT/OUTPUT/CONST singletons (hhds graph.hpp),
// so IO and constant pins are excluded by construction.
[[nodiscard]] std::vector<Snap> collect(hhds::Graph* g, const Options& opts) {
  std::vector<Snap> out;
  for (auto node : g->body().nodes()) {
    const auto op = gu::type_op_of(node);
    if (op == Ntype_op::Invalid || op == Ntype_op::Nconst || op == Ntype_op::Sub || op == Ntype_op::Memory) {
      continue;
    }
    const bool is_state = op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch;
    if (is_state && opts.mode != Mode::All) {
      continue;
    }
    // out_pins() only yields driver pins that actually feed something
    // (hhds Graph::get_driver_pins breaks on the first outgoing edge), so add
    // port 0 explicitly when it exists but drives nothing yet -- that is the
    // pin every downstream consumer, and bitwidth's own report_unbounded,
    // reads.
    auto dpins = node.out_pins();
    if (auto d0 = node.get_driver_pin(0); !d0.is_invalid()) {
      bool have0 = false;
      for (const auto& p : dpins) {
        if (p.get_port_id() == 0) {
          have0 = true;
          break;
        }
      }
      if (!have0) {
        dpins.push_back(d0);
      }
    }
    for (const auto& dpin : dpins) {
      if (dpin.is_invalid() || !dpin.is_driver() || gu::is_const_pin(dpin)) {
        continue;
      }
      if (is_state && !selected(opts.seed, node, dpin.get_port_id(), opts.reg_pct)) {
        continue;
      }
      const auto bits = gu::bits_of(dpin);
      if (bits == 0) {
        continue;  // already unannotated: nothing to strip, nothing to check
      }
      Snap s;
      s.node     = node;
      s.pin      = dpin;
      s.name     = gu::wire_name(dpin);
      s.op       = op;
      s.bits     = bits;
      s.unsign   = gu::is_unsign(dpin);
      s.is_state = is_state;
      out.push_back(std::move(s));
    }
  }
  return out;
}

void strip(const Snap& s) {
  if (s.pin.is_invalid()) {
    return;
  }
  // bits and sign go TOGETHER. `pin_signed` has no tri-state -- absence reads
  // back as *unsigned*, not *unknown* (node_util.hpp:180-185) -- so leaving a
  // sign attr on a width-less pin would feed inference a hint the front end no
  // longer backs, and leaving the pin marked unsigned would do the same in the
  // other direction. Both are recomputed together by set_bits_sign.
  if (s.pin.attr(livehd::attrs::bits).has()) {
    s.pin.attr(livehd::attrs::bits).del();
  }
  if (s.pin.attr(livehd::attrs::pin_signed).has()) {
    s.pin.attr(livehd::attrs::pin_signed).del();
  }
}

void restore(const Snap& s) {
  if (s.pin.is_invalid() || s.node.is_invalid()) {
    return;
  }
  gu::set_bits(s.pin, s.bits);
  if (s.unsign) {
    gu::set_unsign(s.pin);
  } else {
    gu::set_sign(s.pin);
  }
}

void infer(const std::shared_ptr<hhds::Graph>& g, const Options& opts) {
  Bitwidth bw(opts.max_iterations);
  bw.do_trans(g);
}

}  // namespace

std::string_view mode_name(Mode m) {
  switch (m) {
    case Mode::Wires: return "wires";
    case Mode::All  : return "all";
    default         : return "off";
  }
}

bool mode_from_string(std::string_view s, Mode* out) {
  if (s == "off" || s.empty() || s == "false") {
    *out = Mode::Off;
  } else if (s == "wires") {
    *out = Mode::Wires;
  } else if (s == "all") {
    *out = Mode::All;
  } else {
    return false;
  }
  return true;
}

Stats fuzz(const std::shared_ptr<hhds::Graph>& g, const Options& opts) {
  Stats st;
  if (!g || opts.mode == Mode::Off) {
    return st;
  }

  auto snaps = collect(g.get(), opts);
  if (snaps.empty()) {
    return st;
  }
  for (const auto& s : snaps) {
    strip(s);
    ++st.cleared;
    if (s.is_state) {
      ++st.cleared_state;
    }
  }

  infer(g, opts);

  // Repair, in ROUNDS.
  //
  // A pathological result means inference could not bound the pin at all;
  // leaving it poisons every downstream consumer (cgen would emit a width-less
  // net) without testing anything, so the front-end annotation goes back and we
  // re-infer. bw_pass seeds bwmap from an already-sized pin and skips its
  // inference branch (bitwidth.cpp:1513-1526), so a re-run only works on pins
  // still at zero: restored anchors propagate and nothing already resolved is
  // disturbed.
  //
  // Rounds, rather than one bulk restore, are what make the report mean
  // anything. Widths cascade -- one unbounded cell starves its whole fan-out --
  // so a bulk restore reports the root cause and every downstream victim
  // identically, and the failure count says more about cone depth than about
  // the bug. Each round therefore restores only pins whose OWN inputs are
  // already sized. Those had everything inference needed and still failed:
  // genuine root causes. Their fan-out gets another inference round and is
  // classified on its own merits.
  if (opts.repair) {
    constexpr int kMaxRounds = 8;
    while (st.repair_rounds < kMaxRounds) {
      std::vector<Snap*> patho;
      for (auto& s : snaps) {
        if (!s.pin.is_invalid() && !s.node.is_invalid() && !s.restored && is_pathological(gu::bits_of(s.pin))) {
          patho.push_back(&s);
        }
      }
      if (patho.empty()) {
        break;
      }
      std::vector<Snap*> roots;
      for (auto* s : patho) {
        if (!has_unsized_input(s->node)) {
          roots.push_back(s);
        }
      }
      // Every survivor blocked by another survivor: a combinational ring of
      // unbounded cells with no frontier to start from. Break it by restoring
      // the whole ring and say so, rather than spinning the round budget.
      const bool cyclic = roots.empty();
      if (cyclic) {
        roots = patho;
      }
      for (auto* s : roots) {
        restore(*s);
        s->restored = true;
        s->cyclic   = cyclic;
        ++st.repaired;
      }
      ++st.repair_rounds;
      infer(g, opts);
    }
    // Round budget spent with pins still unbounded: restore them unconditionally
    // so the invariant "bitfuzz never leaves a pin unsized" holds regardless.
    for (auto& s : snaps) {
      if (!s.pin.is_invalid() && !s.node.is_invalid() && !s.restored && is_pathological(gu::bits_of(s.pin))) {
        restore(s);
        s.restored = true;
        ++st.repaired;
      }
    }
  }

  for (const auto& s : snaps) {
    Finding f;
    f.pin        = s.name;
    f.op         = std::string{Ntype::get_name(s.op)};
    f.was_bits   = s.bits;
    f.was_unsign = s.unsign;
    f.is_state   = s.is_state;
    f.no_rule    = has_no_inference_rule(s.op);
    f.cyclic     = s.cyclic;

    if (s.pin.is_invalid() || s.node.is_invalid()) {
      f.kind = "vanished";
      ++st.vanished;
      st.findings.push_back(std::move(f));
      continue;
    }

    const auto now = gu::bits_of(s.pin);
    f.now_bits     = now;
    f.now_unsign   = gu::is_unsign(s.pin);

    if (s.restored || is_pathological(now)) {
      f.kind = "unrecovered";
      ++st.unrecovered;
      if (f.no_rule) {
        ++st.no_rule;
      }
      st.findings.push_back(std::move(f));
      continue;
    }

    if (now == s.bits) {
      ++st.same;
      if (f.now_unsign != f.was_unsign) {
        f.kind = "sign";
        ++st.sign_changed;
        st.findings.push_back(std::move(f));
      }
      continue;
    }
    if (now < s.bits) {
      // The front end declared more bits than the value can occupy. Harmless
      // for equivalence (the extra bits were provably dead) but worth counting:
      // it is free QoR data and it changes the shape of the emitted Verilog.
      f.kind = "narrower";
      ++st.narrower;
    } else {
      // THE finding. Inference, which only ever sees explicit cells, needs MORE
      // bits than the annotation claimed -- so something upstream was relying
      // on the annotation to truncate. That is a Get_mask the translation owed
      // and did not emit.
      f.kind = "wider";
      ++st.wider;
    }
    if (f.now_unsign != f.was_unsign) {
      ++st.sign_changed;
    }
    st.findings.push_back(std::move(f));
  }

  return st;
}

}  // namespace livehd::bitfuzz
