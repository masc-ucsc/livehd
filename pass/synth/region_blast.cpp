// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// A region body -> its RAW Lnet (region_blast.hpp). Lifted from pass.abc's
// map_region: every choice here is backend-neutral, so any mapper replays or
// reads the same translation.
#include "region_blast.hpp"

#include <algorithm>
#include <format>
#include <print>

#include "absl/container/btree_map.h"
#include "absl/container/node_hash_map.h"
#include "cell.hpp"
#include "hhds/attrs/srcid.hpp"
#include "host_mem.hpp"
#include "lnet_ops.hpp"
#include "node_util.hpp"
#include "synthesis_cost.hpp"

namespace livehd::synth {
namespace gu = livehd::graph_util;

// A pipeline Flop stores one word at every declared cycle of delay.
[[nodiscard]] int pipeline_depth(const hhds::Node_class& node) {
  if (auto pm = gu::get_driver_of_sink_name(node, "pipe_min"); pm.is_const()) {
    return static_cast<int>(std::max<int64_t>(1, gu::const_of(pm).to_just_i64()));
  }
  return 1;
}

// Source span of a region node. Best-effort: a node with no srcid -- or a
// library whose srcmap was not loaded -- yields a null span, which renders
// location-less rather than wrong (diag::to_text never fabricates a location).
[[nodiscard]] livehd::diag::Span node_span(const livehd::partition::Region_body& rb, const hhds::Node_class& n) {
  if (rb.src == nullptr || n.is_invalid()) {
    return {};
  }
  auto a = n.attr(hhds::attrs::srcid);
  if (!a.has() || a.get() == 0) {
    return {};
  }
  return rb.src->source_locator().resolve_span(a.get());
}

namespace {
// Nearest user-visible signal name for `n`. Synthesis-stage nodes are mostly
// UNNAMED (only what the source named keeps a name attr), so `debug_name`
// alone -- "shl_10620" -- tells a user nothing. Anchor on a real signal
// instead: this node's own named driver pin, else a bounded breadth-first walk
// of the fan-out (the named value it feeds, or the module port it reaches),
// else of the fan-in (the named operand it reads). `relation` comes back as
// "" / "feeds" / "reads" so the caller can say which way it had to look.
std::string nearest_named_signal(const hhds::Node_class& n, std::string_view& relation) {
  // Two independent budgets, because two different things can blow up here: the
  // NODE budget bounds how far the search spreads, and the EDGE budget bounds
  // one step of it -- out_edges() is a lazy view over live storage and a
  // clock/reset-like pin fans out to 100k+ sinks, so bounding dequeues alone
  // would still let a single node enqueue the whole fan-out.
  constexpr size_t kMaxNodes = 256;
  constexpr size_t kMaxEdges = 4096;
  relation                   = {};
  if (n.is_invalid()) {
    return {};
  }
  // The value a node produces, named. out_pins() is the node's driver pins
  // directly -- walking out_edges() instead would re-visit one pin once per
  // consumer.
  const auto named_of = [](const hhds::Node_class& node) -> std::string {
    for (const auto& p : node.out_pins()) {
      if (auto nm = gu::pin_name_of(p); !nm.empty()) {
        return std::string{nm};
      }
    }
    auto nn = gu::node_name_of(node);
    return nn.empty() ? std::string{} : std::string{nn};
  };
  if (auto own = named_of(n); !own.empty()) {
    relation = "is";
    return own;
  }
  const auto walk = [&](bool downstream) -> std::string {
    absl::flat_hash_set<hhds::Node_class> seen{n};
    std::vector<hhds::Node_class>         frontier{n};
    size_t                                nodes = 0;
    size_t                                edges = 0;
    while (!frontier.empty() && nodes < kMaxNodes && edges < kMaxEdges) {
      std::vector<hhds::Node_class> next;
      for (const auto& cur : frontier) {
        if (++nodes > kMaxNodes || edges >= kMaxEdges) {
          break;
        }
        if (downstream) {
          for (const auto& e : cur.out_edges()) {
            if (++edges > kMaxEdges) {
              break;
            }
            // A module port is the best anchor of all: it is the name the
            // instantiating design uses for this value.
            if (gu::is_graph_output_pin(e.sink)) {
              if (auto nm = gu::pin_name_of(e.sink); !nm.empty()) {
                return std::string{nm};
              }
            }
            auto sn = e.sink.get_master_node();
            if (sn.is_invalid() || !seen.insert(sn).second) {
              continue;
            }
            if (auto nm = named_of(sn); !nm.empty()) {
              return nm;
            }
            next.push_back(sn);
          }
        } else {
          for (const auto& in_pin : cur.inp_sorted_pins()) {
            const auto in_drv = in_pin.get_driver_pin();
            if (++edges > kMaxEdges) {
              break;
            }
            if (auto nm = gu::pin_name_of(in_drv); !nm.empty()) {  // also resolves a module INPUT port
              return std::string{nm};
            }
            auto dn = in_drv.get_master_node();
            if (dn.is_invalid() || !seen.insert(dn).second) {
              continue;
            }
            if (auto nm = named_of(dn); !nm.empty()) {
              return nm;
            }
            next.push_back(dn);
          }
        }
      }
      frontier.swap(next);
    }
    return {};
  };
  if (auto down = walk(true); !down.empty()) {
    relation = "feeds";
    return down;
  }
  if (auto up = walk(false); !up.empty()) {
    relation = "reads";
    return up;
  }
  return {};
}

}  // namespace

// A constant rendered for a ONE-LINE diagnostic: the literal, its PAYLOAD width,
// and how many of those bits are unknown.
//
// The width and the unknown count are not decoration. Dlop's carrier is SIGNED,
// so a non-negative value always renders with one leading `0` beyond its payload
// (`Dlop::get_payload_bits`): a u6 whose every bit is unknown prints as
// `0ub0??????`, and that leading digit reads as a seventh value bit -- or, worse,
// as a sign -- to anyone who did not write the renderer. Spelling out
// "(6 bits, 6 unknown)" says plainly that this is a six-bit value, entirely
// unknown, and not a negative one.
//
// An all-unknown 4096-bit literal would otherwise take the whole message
// hostage, so a long spelling is elided in the middle.
[[nodiscard]] std::string const_brief(const Dlop& v) {
  auto       s   = std::format("{}", v);  // Dlop's formatter renders to_pyrope()
  const auto bin = v.to_binary();
  const auto unk = static_cast<size_t>(std::count(bin.begin(), bin.end(), '?'));
  if (s.size() > 44) {
    s = std::format("{}...{}", s.substr(0, 28), s.substr(s.size() - 6));
  }
  return std::format("{} ({} bits, {} unknown)", s, std::max(1, v.get_payload_bits()), unk);
}

// "<cell>_<nid>" plus the nearest named signal: `shl_10620 (feeds 'mshr_d')`.
[[nodiscard]] std::string node_identity(const hhds::Node_class& n) {
  std::string_view rel;
  const auto       nm = nearest_named_signal(n, rel);
  if (nm.empty()) {
    return gu::debug_name(n);
  }
  if (rel == "is") {
    return std::format("{} '{}'", gu::debug_name(n), nm);
  }
  return std::format("{} ({} '{}')", gu::debug_name(n), rel, nm);
}

// Rewrite the TRIVIALLY convertible remainders in a region into a mask, in
// place, and report whether any survived.
//
// `a % 2^k` is `a & (2^k - 1)` -- but ONLY for a non-negative dividend. The op
// is truncated remainder (the sign follows the dividend), so `-9 % 8` is -1,
// not 7, and masking a negative value is simply a different function.
//
// upass/tolg's lower_mod already folds this shape when it lowers Pyrope, so a
// Rem arriving from THAT path is non-trivial by construction. This pass exists
// for the readers that build the cell directly -- inou/yosys turns `$mod` into
// a Rem with whatever divisor the Verilog had, so a perfectly ordinary
// `x % 8` read from Verilog would otherwise hit the error below despite being
// one AND gate.
void rewrite_trivial_rems(hhds::Graph* g) {
  std::vector<hhds::Node_class> to_fix;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Rem) {
      continue;
    }
    auto b = gu::get_driver_of_sink_name(n, "b");
    auto a = gu::get_driver_of_sink_name(n, "a");
    if (b.is_invalid() || a.is_invalid() || !b.is_const()) {
      continue;
    }
    const auto& bc = gu::const_of(b);
    if (bc.has_unknowns() || !bc.is_just_i64()) {
      continue;
    }
    const int64_t bv = bc.to_just_i64();
    const int64_t ba = bv < 0 ? -bv : bv;
    // A negative-capable dividend cannot use the mask (see above). `is_unsign`
    // is the same non-negativity test lower_mod uses.
    if (ba < 2 || (ba & (ba - 1)) != 0 || !gu::is_unsign(a)) {
      continue;
    }
    to_fix.push_back(n);
  }
  for (auto n : to_fix) {
    auto          a  = gu::get_driver_of_sink_name(n, "a");
    auto          b  = gu::get_driver_of_sink_name(n, "b");  // named: gcc -Wdangling-reference on const_of(temporary)
    const auto&   bc = gu::const_of(b);
    const int64_t bv = bc.to_just_i64();
    const int64_t ba = bv < 0 ? -bv : bv;

    auto andn = gu::create_typed_node(*g, Ntype_op::And, gu::bits_of(n.get_driver_pin(0)));
    gu::setup_sink_by_name(andn, "as").connect_driver(a);
    gu::setup_sink_by_name(andn, "as").connect_driver(gu::create_const(*g, *Dlop::create_integer(ba - 1)));
    auto                         newd = andn.create_driver_pin(0);
    // SNAPSHOT the fan-out: connect_driver() below adds an edge, which
    // invalidates the lazy out_edges() view this used to rewire from.
    std::vector<hhds::Pin_class> readers;
    for (const auto& e : n.get_driver_pin(0).out_edges()) {
      readers.push_back(e.sink);
    }
    for (const auto& reader : readers) {
      reader.connect_driver(newd);
    }
    n.del_node();
  }
}


// A region consisting solely of a constant shift is pure bus wiring (plus
// sign extension for SRA). ABC turns its padding into one mapped object per
// output bit (Rob has hundreds of these regions, growing to 10k bits each).
// Rebuild the typed wiring node directly; OpenTimer tracks constant shift bit
// identity and no Liberty delay is being skipped because there is no Boolean
// gate here.
bool rewrite_single_shift(const livehd::partition::Region_body& rb) {
  absl::flat_hash_map<hhds::Pin_class, std::string> region_in_name;
  for (const auto& port : rb.inputs) {
    region_in_name.emplace(port.src_driver, port.name);
  }
  const auto single_shift_op = rb.nodes.size() == 1 ? gu::type_op_of(rb.nodes.front()) : Ntype_op::Invalid;
  if (single_shift_op == Ntype_op::SHL || single_shift_op == Ntype_op::SRA) {
    const auto src_node = rb.nodes.front();
    const auto a        = gu::get_driver_of_sink_name(src_node, "a");
    const auto b        = gu::get_driver_of_sink_name(src_node, "b");
    auto       ait      = region_in_name.find(a);
    if (!a.is_invalid() && b.is_const() && (a.is_const() || ait != region_in_name.end())) {
      auto node = gu::create_typed_node(*rb.body, single_shift_op);
      if (a.is_const()) {
        gu::create_const(*rb.body, gu::const_of(a)).connect_sink(gu::setup_sink_by_name(node, "a"));
      } else {
        rb.body->get_input_pin(ait->second).connect_sink(gu::setup_sink_by_name(node, "a"));
      }
      gu::create_const(*rb.body, gu::const_of(b)).connect_sink(gu::setup_sink_by_name(node, "b"));
      auto out  = node.create_driver_pin(0);
      int  bits = 1;
      for (const auto& port : rb.outputs) {
        bits = std::max(bits, port.bits);
      }
      gu::set_bits(out, bits);
      if (!gu::is_unsign(src_node.create_driver_pin(0))) {
        gu::set_sign(out);
      }
      if (auto pn = gu::pin_name_of(src_node.create_driver_pin(0)); !pn.empty()) {
        gu::set_pin_name(out, pn);
      }
      for (const auto& port : rb.outputs) {
        out.connect_sink(rb.body->get_output_pin(port.name));
      }
      return true;
    }
  }
  return false;

}

Region_blast blast_region(const livehd::partition::Region_body& rb, const Blast_options& options, const Blast_hooks& hooks) {
  Region_blast result;
  auto&        lnet                 = result.lnet;
  auto&        flops                = result.flops;
  auto&        bboxes               = result.bboxes;
  auto&        region               = result.region;
  auto&        native_comb_logic    = result.native_comb_logic;
  auto&        region_in_name       = result.region_in_name;
  auto&        pi_order             = result.pi_order;
  auto&        all_pi_order         = result.all_pi_order;
  auto&        bbox_pi              = result.bbox_pi;
  auto&        po_order             = result.po_order;
  auto&        bbox_po              = result.bbox_po;
  auto&        direct_native_output = result.direct_native_output;
  auto&        has_dummy_po         = result.has_dummy_po;
  const auto   trace_stage          = [&](std::string_view name) {
    if (hooks.stage) {
      hooks.stage(name);
    }
  };
  const auto   since                = [&] { return hooks.elapsed_ms ? hooks.elapsed_ms() : 0.0; };

  // The region's logic is recorded on a RAW Lnet (lnet_ops.hpp); a backend
  // replays it once translation succeeds.
  Lnet_ops ops(lnet);

  // bit i of an original driver pin -> the ABC net carrying it. The OUTER map is
  // a node_hash_map (pointer-stable values): several sites bind `auto& slots =
  // bitnet[pin]` and then keep writing through it while `abc_bit` inserts *new*
  // outer keys (input/const drivers). A flat_hash_map would rehash on those
  // inserts and leave `slots` dangling — harmless for a small colored region but
  // a use-after-free once an uncolored design folds the whole graph into one
  // large region. node_hash_map keeps each inner map's address fixed across
  // outer rehashes, so every held `slots` reference stays valid.
  absl::node_hash_map<hhds::Pin_class, absl::flat_hash_map<int, Lid>> bitnet;
  // Region node membership (handles into rb.src).
  for (const auto& n : rb.nodes) {
    region.insert(n);
  }

  // Set when a region node cannot be mapped (unsupported cell / mask); the
  // region is abandoned after the blast loop. Declared here so abc_bit can
  // suppress its per-bit unmaterialized-driver diagnostics once the ONE real
  // unsupported-cell error has fired (the producer wrote no slots, so every
  // downstream read would otherwise flood the log).
  bool unsupported = false;

  // Per-node refusal, with provenance. Unlike the region-level message it
  // replaces, every record now carries the offending node's identity and its
  // original source location -- so two of these no longer collapse into one
  // line by the sink's (code, span, message) dedup. That is the point (a
  // 12k-node region can hold dozens of independently broken cells), but it
  // also means a systematically broken region would print one line per node:
  // report the first kMaxRefusals in full and summarize the rest.
  constexpr size_t kMaxRefusals = 10;
  size_t           refusals     = 0;
  const auto       refuse       = [&](const hhds::Node_class& bad,
                                      std::string_view        code,
                                      std::string_view        category,
                                      std::string_view        what,
                                      std::string_view        hint     = {},
                                      const hhds::Pin_class&  note_pin = {},
                                      std::string_view        note_msg = {}) {
    unsupported = true;
    if (refusals++ >= kMaxRefusals) {
      return;  // counted; the post-loop summary reports the total
    }
    auto b = livehd::diag::err("pass.abc", code, category);
    b.at(node_span(rb, bad));
    b.msg("pass.abc: {} in region '{}': {}", node_identity(bad), rb.module_name, what);
    if (!hint.empty()) {
      b.hint(hint);
    }
    if (!note_pin.is_invalid() && !note_msg.empty()) {
      if (auto sp = node_span(rb, note_pin.get_master_node()); !sp.is_null()) {
        b.note(note_msg, sp);
      }
    }
    b.emit();
  };

  // SHL / SRA share this: the amount is a constant the mapper cannot use.
  const auto refuse_shift_amount
      = [&](const hhds::Node_class& bad, std::string_view op_name, const Dlop& amt, const hhds::Pin_class& amt_pin) {
          // UNKNOWN is tested FIRST, and that order is load-bearing: Dlop::unknown()
          // fills the base plane with -1 (hlop init_unknown) and Dlop::is_negative()
          // reads only that plane, so EVERY x-carrying value answers "negative".
          // `!has_unknowns() && is_negative()` is the idiom upass/tolg already uses
          // (pin_can_be_negative); with the tests the other way round every unknown
          // amount is misreported as a livehd internal bug.
          if (!amt.has_unknowns() && amt.is_negative()) {
            refuse(
                bad,
                "negative-shift-amount",
                "internal",
                std::format("{} shift amount is the NEGATIVE constant {} -- a shift count must be >= 0", op_name, const_brief(amt)),
                "no pass should have produced this: upass.bitwidth rejects a negative shift count, so a negative "
                "constant reaching synthesis is a folding/lowering bug in livehd, not a design error",
                amt_pin,
                "shift amount defined here");
            return;
          }
          refuse(bad,
                 "unknown-shift-amount",
                 "unsupported",
                 std::format("{} shift amount is the constant {}, which carries unknown (?) bits -- ABC has no X value, so "
                             "the shift cannot be technology-mapped",
                             op_name,
                             const_brief(amt)),
                 "a RUNTIME (non-constant) shift amount is supported and becomes a barrel shifter; only an UNKNOWN constant "
                 "is not. Trace the amount back to the value that was never given a definite assignment",
                 amt_pin,
                 "shift amount defined here");
        };

  Wiring_blaster<Lid> wiring_blaster;

  // Region inputs are bit-demanded, not eagerly exploded. Wide packed-state
  // ports often expose tens of thousands of bits while this region reads only
  // a small slice; creating every unused PI also forces readback to build an
  // equally large selector forest. `pi_order` records the exact lazy creation
  // order, so the mapped PI readback remains positional and deterministic.
  absl::flat_hash_map<hhds::Pin_class, size_t> region_input_index;
  // Native boundary outputs follow the same demand-driven rule as region
  // inputs.  Wide packed-wiring boundaries can expose tens of thousands of
  // bits while the mapped cone reads only a handful; eagerly creating every PI
  // made those unused bits survive through ABC readback as an equally large
  // selector forest.  The origin table is populated by the boundary scan below
  // before any combinational node is bit-blasted.
  using Bbox_output_origin = std::pair<int, int>;  // (bbox index, output index)
  absl::flat_hash_map<hhds::Pin_class, Bbox_output_origin> bbox_output_index;
  for (size_t pi = 0; pi < rb.inputs.size(); ++pi) {
    region_input_index.emplace(rb.inputs[pi].src_driver, pi);
  }

  // --- bit i of an original driver pin, with sign/zero extension past width ---
  std::function<Lid(const hhds::Pin_class&, int)> abc_bit = [&](const hhds::Pin_class& drv, int i) -> Lid {
    if (drv.is_invalid()) {
      return ops.konst(false);
    }
    int  w    = gu::bits_of(drv);
    bool sign = !gu::is_unsign(drv);
    int  eff  = i;
    if (w != 0 && i >= w) {
      eff = sign ? w - 1 : -1;  // -1 => constant 0 above an unsigned width
    }
    if (eff < 0) {
      return ops.konst(false);
    }
    auto& slots = bitnet[drv];
    if (auto it = slots.find(eff); it != slots.end()) {
      return it->second;
    }
    if (drv.is_const()) {
      const auto& val = gu::const_of(drv);
      const auto  net = ops.konst(val.bit_test(eff));
      slots[eff]      = net;
      return net;
    }
    if (auto it = region_input_index.find(drv); it != region_input_index.end()) {
      const size_t pi = it->second;
      const auto   net = lnet.add_input(std::format("{}_b{}", rb.inputs[pi].name, eff));
      slots[eff]     = net;
      all_pi_order.push_back({Pi_kind::region_input, pi_order.size()});
      pi_order.emplace_back(pi, eff);
      return net;
    }
    if (auto it = bbox_output_index.find(drv); it != bbox_output_index.end()) {
      const auto net = lnet.add_input({});  // unnamed
      slots[eff]     = net;
      all_pi_order.push_back({Pi_kind::bbox_output, bbox_pi.size()});
      bbox_pi.emplace_back(it->second.first, it->second.second, eff);
      return net;
    }
    auto master = drv.get_master_node();
    if (gu::type_op_of(master) == Ntype_op::Set_mask || gu::type_op_of(master) == Ntype_op::Concat) {
      const auto net = wiring_blaster.bit(drv, eff, abc_bit, [&] { return ops.zero(); }, [&](std::string_view why) {
        if (!unsupported) {
          refuse(master, "unsupported-cell", "unsupported", why);
        }
      });
      slots[eff] = net;
      return net;
    }
    // A region-internal node not yet materialized (a genuine node-level cycle
    // the fixpoint scheduler could not resolve) or an unexpected boundary:
    // use a temporary constant only to let translation unwind, but reject the
    // region. Accepting that placeholder would silently miscompile the whole
    // downstream cone. Suppress follow-on diagnostics once the first missing
    // producer has identified the region-level failure.
    if (!unsupported) {
      refuse(drv.get_master_node(),
             "unmaterialized-driver",
             "internal",
             std::format("bit {} of this driver could not be materialized", eff),
             "the colored region contains a combinational cycle or an invalid boundary; refusing to emit a wrong netlist");
    }
    const auto net = ops.konst(false);
    slots[eff]     = net;
    return net;
  };

  auto real_width = [&](const hhds::Pin_class& p) -> int { return std::max(1, gu::real_width(p)); };


  // Region-input driver -> port name. Used twice: to reconnect a flop
  // boundary's control pins natively (see the boundary scan below), and to
  // decide whether a register's clock even HAS a native source on read-back.
  for (const auto& port : rb.inputs) {
    region_in_name.emplace(port.src_driver, port.name);
  }

  // Registers that cannot be represented by the selected plain Liberty DFF
  // stay native boundaries. A derived clock has no native source after latch
  // read-back. An ASYNCHRONOUS reset is an event, not data: folding it into D
  // would make the reset land only on a clock edge (and pass/lec's encode.cpp
  // models async and sync resets differently under the phase schedule). A
  // SYNCHRONOUS reset is exactly a D-cone mux with priority over the enable
  // (`if (rst) q <= rval; else if (en) q <= din;` is what cgen emits and what
  // the LEC encodes, ITE(rst, init, ITE(en, din, q))), so it crosses ABC like
  // any other next-state logic and maps to a plain DFF cell; its `initial` is
  // the reset value, realized on D, never a power-on value (cvc5 + lgyosys both
  // prove the folded netlist against the reset_pin+initial source). Keeping
  // sync-reset registers native cost br_delay's Pyrope flow 32 native flops
  // that yosys's normalize then mapped to DFFHQNx1 + 64 INVx1 + 24 extra HB1
  // (18.196 vs 17.729 um^2, 114.5 vs 102.8 ps on ASAP7), and left every
  // reset-cone node native with fanout 77-113 (br_amba_axi_demux 2045 ps).
  absl::flat_hash_set<hhds::Node_class> clk_demoted;
  absl::flat_hash_set<hhds::Node_class> reset_demoted;
  if (options.map_register) {
    for (const auto& n : rb.nodes) {
      if (!gu::is_type_flop(n)) {
        continue;
      }
      Seq_flop f;
      f.node  = n;
      f.q_pin = n.create_driver_pin(0);
      f.bits  = gu::bits_of(f.q_pin);
      if (f.bits == 0) {
        f.bits = 1;
      }
      f.root = gu::wire_name(f.q_pin);  // the register's signal name (e.g. "r")
      if (f.root.empty()) {
        f.root = std::format("{}__flop{}", rb.module_name, n.get_debug_nid());
      }
      f.din_drv  = gu::get_driver_of_sink_name(n, "din");
      f.en_drv   = gu::get_driver_of_sink_name(n, "enable");
      f.rst_drv  = gu::get_driver_of_sink_name(n, "reset_pin");
      f.rval_drv = gu::get_driver_of_sink_name(n, "initial");
      f.clk_drv  = gu::get_driver_of_sink_name(n, "clock_pin");
      if (auto edge = gu::get_driver_of_sink_name(n, "posclk"); edge.is_const()) {
        f.neg_clock = gu::const_of(edge).is_known_false();
      }
      // ABC latches and the selected plain Liberty DFF have no reset pin. Only
      // an ASYNCHRONOUS reset needs one (see the set's comment above): keep
      // that flop native so cgen retains the `or posedge rst` event and its
      // reset value; its data cone still crosses the boundary and is mapped.
      // The `async` sink is a comptime flavour pin set by tolg (`async=true` /
      // `sync=false`, upass.reset_style=async) and the slang reader for an
      // `always_ff @(posedge clk or posedge rst)`; cgen and pass/lec read it
      // the same way -- const and not known-false => asynchronous, anything
      // else (absent, or a malformed non-const driver) => synchronous -- so
      // the fold agrees with both the emitted Verilog and the LEC model.
      if (!f.rst_drv.is_invalid()) {
        auto async = gu::get_driver_of_sink_name(n, "async");
        if (async.is_const() && !gu::const_of(async).is_known_false()) {
          reset_demoted.insert(n);
          continue;
        }
        f.has_reset = true;
      }
      // tolg may wrap a call-site clock in 1-bit Get_mask coercions (`x:u1`
      // casts survive cprop when the source is signed). On a 1-bit operand they are wire
      // identities regardless of the declared output width — trace to the root
      // so the register's clock is recognized as region-input-driven and the
      // DFF clock pin connects DIRECTLY to it (never through mapped logic).
      //
      // Whole-design flatten can stack one such coercion per hierarchy level,
      // so trace the identity chain to the structural clock source.
      for (int guard = 0; guard < 64 && !f.clk_drv.is_invalid(); ++guard) {  // guard: cycle net, > any sane hierarchy depth
        // Stop AT the partition boundary. A region input IS the structural
        // clock source as far as this region is concerned (its port is what
        // the read-back wires the DFF clk pin from), and peeling past it lands
        // on a pin the region never sees -- which then fails the
        // region_in_name test below and demoted every such register to a
        // native flop. The shipped `cones` coloring routinely leaves a
        // clock-carrying Sext in a neighbour region, so this is the common
        // case, not a corner: without the break, a yosys-read design's only
        // register stays native (lhd_macro_declarations_test).
        if (region_in_name.contains(f.clk_drv)) {
          break;
        }
        auto m = f.clk_drv.get_master_node();
        if (gu::type_op_of(m) == Ntype_op::Sext && real_width(f.clk_drv) == 1) {
          // Yosys' signed clock-pin carrier preserves the single input bit.
          auto a = gu::get_driver_of_sink_name(m, "a");
          if (a.is_invalid() || real_width(a) != 1) {
            break;
          }
          f.clk_drv = a;
          continue;
        }
        if (gu::type_op_of(m) != Ntype_op::Get_mask) {
          break;
        }
        auto a    = gu::get_driver_of_sink_name(m, "a");
        auto mask = gu::get_driver_of_sink_name(m, "mask");
        if (a.is_invalid() || real_width(a) != 1 || mask.is_invalid() || !mask.is_const() || !gu::const_of(mask).bit_test(0)) {
          break;
        }
        f.clk_drv = a;  // get_mask(bit0) of a 1-bit wire == the wire
      }
      // A clock driven by region-INTERNAL logic (a genuinely gated/derived
      // clock — a shape whole-design flatten makes reachable, since everything
      // is one region) cannot cross as a latch: the read-back has no native
      // source for the DFF/flop clock pin and used to silently drop the
      // connection. Demote the register to a boundary box (the register=false
      // machinery): it stays a native flop and its clock cone is
      // technology-mapped and reconnected like any comb-driven boundary input.
      if (!f.clk_drv.is_invalid() && !f.clk_drv.is_const() && !region_in_name.contains(f.clk_drv)) {
        clk_demoted.insert(n);
        continue;
      }
      if (auto nr = gu::get_driver_of_sink_name(n, "negreset"); nr.is_const()) {
        f.neg_reset = gu::const_of(nr).bit_test(0);
      }
      bool has_rval                     = f.rval_drv.is_const();
      auto rval                         = has_rval ? gu::const_of(f.rval_drv) : Dlop{};
      f.has_init                        = has_rval;
      f.init_val                        = rval;  // read-back cannot re-resolve the source pin (see Seq_flop::has_init)
      // A resetless init is a TRUE power-on value: that bit is rebuilt native
      // on read-back and must keep the honest encoding (its latch init would
      // be complemented too). With a reset the init is the reset value, folded
      // into D below, and the bit maps to a cell like an init-less one.
      const bool power_on_init          = has_rval && !f.has_reset;
      f.d_inverted                      = options.qn_encode && !power_on_init;
      // pipe_min encodes real clocked storage, not an optimization hint.
      // Cross every stage into ABC and expose only the final Q to graph users.
      const int               depth     = pipeline_depth(n);
      const auto              prototype = f;
      std::vector<Lid>       previous;
      for (int stage = 0; stage < depth; ++stage) {
        f = prototype;
        if (stage + 1 < depth) {
          // Match Cgen_verilog's stage spelling (get_append_to_name(name,
          // "___pipe<i>_"), a PREFIX, stage 0 closest to D -- same order as
          // here) so the post-synthesis LEC still pairs the hidden stages by
          // name instead of dropping them into a flat SAT solve.
          f.root = std::format("___pipe{}_{}", stage, prototype.root);
        }
        f.previous = previous;
        for (int b = 0; b < f.bits; ++b) {
          char init = 'x';
          // Only a power-on init is told to ABC. A reset-backed register powers
          // on X exactly like the DFF cell it maps to (the reset value arrives
          // through D on the first asserted edge); declaring its reset value as
          // the latch init would let a sequential user flow (`dretime`, `scorr`)
          // assume a start state the cell never provides. The built-in flows
          // contain no sequential optimization, so this is about honesty, not QoR.
          if (power_on_init && !rval.unknown_bit_test(b)) {
            init = rval.bit_test(b) ? '1' : '0';
          }
          // Source signal names need not be unique (a generated reset counter
          // can share a spelling with a user register). ABC's netlist converter
          // merges CI nets by name, so distinguish every crossed register.
          // Readback recovers source identities from the ordered snapshot.
          f.latch.push_back(lnet.add_latch(std::format("{}_%r{}_{}", f.root, flops.size(), b), init));
          const auto qnet = lnet.latch(f.latch.back()).q;
          f.qbits.push_back(qnet);  // stage-local Q
          if (stage + 1 == depth) {
            bitnet[f.q_pin][b] = qnet;
          }
        }
        previous = f.qbits;
        flops.push_back(std::move(f));
      }
    }
    // Name the registers, not just their count: "3 register(s)" in a 12k-node
    // region is unactionable. First few by identity + declaration site; the set
    // is unordered, so sort by nid to keep the report reproducible.
    auto report_demoted = [&](const absl::flat_hash_set<hhds::Node_class>& set, std::string_view diag_id, const std::string& why) {
      if (set.empty()) {
        return;
      }
      std::vector<hhds::Node_class> demoted(set.begin(), set.end());
      std::sort(demoted.begin(), demoted.end(), [](const auto& a, const auto& b) { return a.get_debug_nid() < b.get_debug_nid(); });
      auto w = livehd::diag::warn("pass.abc", diag_id, "unsupported");
      w.at(node_span(rb, demoted.front())).msg("pass.abc region '{}': {} {}", rb.module_name, demoted.size(), why);
      constexpr size_t kMaxNamed = 5;
      for (size_t k = 0; k < std::min(kMaxNamed, demoted.size()); ++k) {
        w.note(std::format("kept native: {}", node_identity(demoted[k])), node_span(rb, demoted[k]));
      }
      if (demoted.size() > kMaxNamed) {
        w.note(std::format("... and {} more register(s)", demoted.size() - kMaxNamed));
      }
      w.emit();
    };
    report_demoted(clk_demoted,
                   "derived-clock-native",
                   "register(s) clocked by region-internal logic (a gated/derived clock) kept as native flops — a DFF "
                   "cell cannot take its clock from mapped logic; the clock cone is still mapped and reconnected");
    report_demoted(reset_demoted,
                   "reset-native",
                   "asynchronous-reset register(s) kept as native flops — the selected plain DFF cell has no "
                   "asynchronous reset pin (a synchronous reset is folded into D and mapped); their surrounding data "
                   "cones are still mapped");
  }

  // A very wide OR of non-overlapping, constant-position shifts is a packed-bus
  // assembly, not Boolean logic. Sending its thousands of identity bits through
  // ABC is pathological (Rob's 24x511 -> 10911 pack spent minutes in &nf).
  // Keep the SHLs and their OR as native zero-delay wiring, just like
  // Get_mask/Set_mask at the mapper boundary. pass.opentimer's pin tracker
  // understands both operations, so timing identity is preserved bit-for-bit.
  // Width at or above which a pure-wiring cell (Concat / constant-control
  // slice, pack, shift, sext) is kept as a NATIVE boundary instead of being
  // bit-blasted. Below it the bit-level cone is both cheaper and better
  // (constant lanes reach ABC, and a one-bit boundary net does not acquire
  // hundreds of dead mapped-cell loads); above it, materializing one ABC
  // PI/PO per bit is pathological (Backend carries megabit-scale slice/pack
  // colors). Native reconstruction is the scalability escape hatch for
  // genuinely wide buses, not the default representation for ordinary structs.
  constexpr int kNativeWiringBits = 4096;

  // Native boundary nodes. Most are zero-delay packed wiring discovered below;
  // a structurally cyclic combinational remainder is added after the wiring
  // scan because ABC itself only accepts acyclic Boolean networks.
  absl::flat_hash_set<hhds::Node_class> native_wiring;
  auto                                  node_output_width = [](const hhds::Node_class& n) {
    int width = 0;
    for (const auto& out_pin : n.out_sorted_pins()) {  // widest driver pin; consumers do not matter
      width = std::max(width, gu::bits_of(out_pin));
    }
    return width != 0 ? width : gu::bits_of(n.create_driver_pin(0));
  };

  // These cells only rearrange or select bits when their control operand is a
  // constant. Rebuilding them as exact typed nodes is both more faithful to
  // their zero-delay wiring role and dramatically smaller than materializing
  // one Liberty buffer/inverter per bit (Backend contains hundreds of
  // megabit-scale slice/pack colors). Variable shifts remain real logic and
  // still cross ABC. And/Or are deliberately excluded: only the proven
  // disjoint wide-OR shape below is wiring rather than Boolean logic.
  const auto const_operand = [](const hhds::Node_class& n, std::string_view sink) {
    const auto d = gu::get_driver_of_sink_name(n, sink);
    return d.is_const();
  };
  // A Concat lane whose driver is WIDER than its declared window is a width
  // boundary only the native reconstruction path can spell (see fit_native_ins,
  // which recreates the missing low-bit cast). Bit-blasting such a cell instead
  // trips abc_bit's lane-table invariant, so keep it native regardless of width.
  const auto concat_has_overwide_lane = [](const hhds::Node_class& n) {
    const auto lanes = gu::concat_lanes(n);
    return !lanes.empty() && !gu::concat_lane_violation(lanes).empty();
  };
  for (const auto& n : rb.nodes) {
    const auto op     = gu::type_op_of(n);
    const int  width  = node_output_width(n);
    // Below kNativeWiringBits a wiring cell stays inside the bit-level cone.
    bool       wiring = op == Ntype_op::Concat && (width >= kNativeWiringBits || concat_has_overwide_lane(n));
    // The control operand must be CONSTANT for the cell to be a bit rename.
    // A non-constant mask/position is real logic -- and, crucially, one the
    // bit-blast loop below REFUSES with a precise per-node diagnostic. Marking
    // it native here would skip that refusal and silently hand pass.opentimer a
    // node its pin tracker also cannot model, turning a clean ABC refusal into a
    // fatal in a later pass.
    if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) {
      wiring = width >= kNativeWiringBits && const_operand(n, "mask");
    } else if (op == Ntype_op::Sext || op == Ntype_op::SRA || op == Ntype_op::SHL) {
      wiring = width >= kNativeWiringBits && const_operand(n, "b");
    }
    if (wiring) {
      native_wiring.insert(n);
    }
  }
  for (const auto& n : rb.nodes) {
    if (gu::type_op_of(n) != Ntype_op::Or || node_output_width(n) < kNativeWiringBits) {
      continue;
    }
    struct Span {
      int lo;
      int hi;
    };
    std::vector<Span>             spans;
    std::vector<hhds::Node_class> shifts;
    bool                          packing         = true;
    int                           unshifted_lanes = 0;
    std::string                   reject;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      if (in_drv.is_const()) {
        // A constant lane is already synthesized: zero is padding and one
        // fixes the corresponding output bit. It needs no Liberty cell and
        // does not participate in variable-lane overlap.
        continue;
      }
      const auto shl = in_drv.get_master_node();
      if (gu::type_op_of(shl) != Ntype_op::SHL) {
        // Packed assemblies commonly leave the low lane unshifted (Rob's low
        // 20 bits arrive from an extracted Sub) and shift every higher lane.
        // One such lane is safe: interval overlap below proves it is disjoint.
        const int width = gu::bits_of(in_drv);
        if (++unshifted_lanes > 1 || width <= 0) {
          packing = false;
          reject  = "multiple or widthless unshifted inputs";
          break;
        }
        spans.push_back({0, width});
        continue;
      }
      const auto a = gu::get_driver_of_sink_name(shl, "a");
      const auto b = gu::get_driver_of_sink_name(shl, "b");
      if (a.is_invalid() || !b.is_const()) {
        packing = false;
        reject  = "shift lacks data or constant amount";
        break;
      }
      const int width     = gu::bits_of(a);
      const int shl_width = node_output_width(shl);
      if (width <= 0 || shl_width < width) {
        packing = false;
        reject  = "invalid shift width stamps";
        break;
      }
      const int out_width = node_output_width(n);
      // For a non-negative constant SHL, bitwidth stamps exactly
      // input-width+amount on the result. Recover the occupied interval from
      // those stamps, avoiding a lossy int64 conversion of an arbitrary-size
      // Dlop constant.
      const int lo        = std::min(shl_width - width, out_width);
      const int hi        = std::min(shl_width, out_width);
      if (lo < hi) {
        spans.push_back({lo, hi});
      }
      shifts.push_back(shl);
    }
    if (!packing || shifts.empty()) {
      if (options.verbose) {
        std::print("[pass.abc] region '{}': rejected {}-bit shift/OR pack after {} shift(s): {}\n",
                   rb.module_name,
                   node_output_width(n),
                   shifts.size(),
                   reject.empty() ? "no shifted lane" : reject);
      }
      continue;
    }
    std::ranges::sort(spans, {}, &Span::lo);
    for (size_t i = 1; i < spans.size(); ++i) {
      if (spans[i].lo < spans[i - 1].hi) {
        packing = false;
        reject  = std::format("overlap {}..{} with {}..{}", spans[i - 1].lo, spans[i - 1].hi, spans[i].lo, spans[i].hi);
        break;
      }
    }
    if (!packing) {
      if (options.verbose) {
        std::print("[pass.abc] region '{}': rejected {}-bit shift/OR pack after {} shift(s): {}\n",
                   rb.module_name,
                   node_output_width(n),
                   shifts.size(),
                   reject);
      }
      continue;
    }
    native_wiring.insert(n);
    for (const auto& shl : shifts) {
      if (region.contains(shl)) {
        native_wiring.insert(shl);
      }
    }
    if (options.verbose) {
      std::print("[pass.abc] region '{}': keeping {}-bit disjoint shift/OR pack as native wiring ({} shifts)\n",
                 rb.module_name,
                 node_output_width(n),
                 shifts.size());
    }
  }
  // A wide packed-bus assembler exported directly by the region is also
  // wiring, not a Boolean cone. Keeping it as a native boundary avoids an ABC
  // PO for every bit of every exported packed bus (Rob c33: 27.7M interface
  // bits for only 19.8k GE). Its narrow computed lane inputs still become ABC
  // POs, while wide base/region-input lanes reconnect natively below.
  std::vector<hhds::Node_class> exported_wiring;
  for (const auto& port : rb.outputs) {
    const auto n            = port.src_driver.get_master_node();
    const auto op           = gu::type_op_of(n);
    const bool constant_shl = op == Ntype_op::SHL && gu::get_driver_of_sink_name(n, "b").is_const();
    const bool wide_pack    = node_output_width(n) >= kNativeWiringBits && (op == Ntype_op::Concat || op == Ntype_op::Set_mask);
    if (!region.contains(n) || (!constant_shl && !wide_pack)) {
      continue;
    }
    if (native_wiring.insert(n).second) {
      exported_wiring.push_back(n);
    }
  }
  for (size_t head = 0; head < exported_wiring.size(); ++head) {
    for (const auto& in_pin : exported_wiring[head].inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      if (in_drv.is_invalid() || in_drv.is_const()) {
        continue;
      }
      const auto parent = in_drv.get_master_node();
      const auto op     = gu::type_op_of(parent);
      if (!region.contains(parent) || node_output_width(parent) < kNativeWiringBits
          || (op != Ntype_op::Concat && op != Ntype_op::Set_mask)) {
        continue;
      }
      if (native_wiring.insert(parent).second) {
        exported_wiring.push_back(parent);
      }
    }
  }
  if (options.verbose && !exported_wiring.empty()) {
    std::print("[pass.abc] region '{}': kept {} exported packed-wiring node(s) native including ancestors\n",
               rb.module_name,
               exported_wiring.size());
  }

  // ABC cannot ingest a combinational SCC. Preserve such logic exactly as
  // native typed nodes and map every acyclic cone around it. This is a boundary
  // cut, not an approximation: native->mapped edges become ABC PIs,
  // mapped->native edges become POs, and read-back reconnects the original
  // feedback. Kahn's unpeeled remainder includes the SCC plus any nodes whose
  // only schedule predecessor is that SCC; keeping the whole remainder native
  // is conservative and prevents an arbitrary edge choice from changing with
  // traversal order.
  {
    std::vector<hhds::Node_class>         comb;
    absl::flat_hash_set<hhds::Node_class> comb_set;
    for (const auto& n : rb.nodes) {
      const auto op = gu::type_op_of(n);
      if (gu::is_type_register(n) || op == Ntype_op::Sub || op == Ntype_op::Clock_cell || op == Ntype_op::Rem) {
        continue;
      }
      comb.push_back(n);
      comb_set.insert(n);
    }
    absl::flat_hash_map<hhds::Node_class, size_t>                        indegree;
    absl::flat_hash_map<hhds::Node_class, std::vector<hhds::Node_class>> consumers;
    std::vector<hhds::Node_class>                                        queue;
    indegree.reserve(comb.size());
    consumers.reserve(comb.size());
    queue.reserve(comb.size());
    for (const auto& n : comb) {
      size_t degree = 0;
      for (const auto& in_pin : n.inp_sorted_pins()) {
        const auto in_drv   = in_pin.get_driver_pin();
        const auto producer = in_drv.get_master_node();
        if (!producer.is_invalid() && comb_set.contains(producer)) {
          ++degree;
          consumers[producer].push_back(n);
        }
      }
      indegree.emplace(n, degree);
      if (degree == 0) {
        queue.push_back(n);
      }
    }
    size_t peeled = 0;
    for (size_t head = 0; head < queue.size(); ++head) {
      const auto n = queue[head];
      ++peeled;
      if (auto it = consumers.find(n); it != consumers.end()) {
        for (const auto& consumer : it->second) {
          auto& degree = indegree.at(consumer);
          if (--degree == 0) {
            queue.push_back(consumer);
          }
        }
      }
    }
    if (peeled != comb.size()) {
      std::vector<hhds::Node_class> cyclic_remainder;
      cyclic_remainder.reserve(comb.size() - peeled);
      for (const auto& n : comb) {
        if (indegree.at(n) != 0) {
          cyclic_remainder.push_back(n);
          native_wiring.insert(n);
          native_comb_logic.insert(n);
        }
      }
      auto w = livehd::diag::warn("pass.abc", "comb-loop-native", "unsupported");
      w.at(node_span(rb, cyclic_remainder.front()))
          .msg(
              "pass.abc region '{}': preserved {} node(s) in a combinational-cycle remainder as native logic; acyclic cones "
              "around it are still technology-mapped",
              rb.module_name,
              cyclic_remainder.size())
          .hint("remove the combinational feedback to obtain an all-standard-cell region and a complete timing score");
      constexpr size_t kMaxNamed = 5;
      for (size_t k = 0; k < std::min(kMaxNamed, cyclic_remainder.size()); ++k) {
        w.note(std::format("kept native: {}", node_identity(cyclic_remainder[k])), node_span(rb, cyclic_remainder[k]));
      }
      if (cyclic_remainder.size() > kMaxNamed) {
        w.note(std::format("... and {} more node(s)", cyclic_remainder.size() - kMaxNamed));
      }
      w.emit();
    }
  }

  if (options.verbose) {
    std::fflush(stdout);
  }

  // --- blackbox boundary nodes (Sub instances + memories): never bit-blasted.
  // Each consumed output driver pin becomes fresh ABC PIs (a source for the
  // surrounding logic, seeded into bitnet); each combinationally-driven input
  // becomes ABC POs (the cone feeding it, created after the comb loop); constant
  // inputs are recreated directly on read-back. The node itself is rebuilt
  // natively and reconnected. Boundary PIs/POs are appended AFTER the region
  // ports so the region-port read-back stays index-aligned (region first). ---
  // region_in_name (built above the register scan) reconnects a flop
  // boundary's control pins (clock/reset/enable that come straight from a
  // region input) NATIVELY on read-back. Routing such a clock through the
  // combinational AIG would map it to a logic buffer and make the rebuilt flop
  // clock on `posedge <data-wire>` -- logically correct but unusable as a real
  // netlist (breaks clock-tree synthesis and timing). Only the flop's din cone
  // (genuine comb logic) crosses into ABC. A demoted register's derived-clock
  // or reset data cone, by contrast, IS genuine logic and crosses as a PO.
  // Convert the trivially-mappable remainders BEFORE the boundary scan, so the
  // refusal below only fires for a shape that genuinely has no gate translation.
  if (hooks.rewrite_rems) {
    hooks.rewrite_rems();
  }

  for (const auto& n : rb.nodes) {
    auto op = gu::type_op_of(n);
    // A materialized PROPERTY marker (`fproperty` from a user assert/assume,
    // `lgassert` from a runtime `a#[lo..=hi]` guard) is not hardware: it has no
    // Liberty cell and no body, and the mapped netlist is a synthesis artifact
    // that cannot represent it. Dropping it here is what makes the netlist
    // WELL-FORMED. Carrying it as a blackbox boundary instead left a Sub whose
    // module was never declared in the output library, so `get_subnode_io()`
    // came back null downstream: cgen silently emitted nothing for it (the
    // runtime check was already lost), and `pass.opentimer` refused the WHOLE
    // design over a black box with an empty type name -- which is what took out
    // every cva6 synthesis run, and any design containing one runtime bit-range
    // select. The check lives on in the pre-ABC design, which is what pass.formal
    // and the source-level flows read.
    if (gu::is_property_marker(n)) {
      continue;
    }
    // A flop in a !seq (combinational-only) map is kept as a native boundary,
    // exactly like a Sub/Memory: its Q feeds the mapped logic as a fresh PI, its
    // din/enable/clock/reset are cut as POs (or recreated when const), and the
    // Flop node is rebuilt unchanged on read-back (never bit-blasted). In seq
    // mode flops instead cross into ABC as 1-bit latches (handled above), so they
    // are excluded from the boundary set there — EXCEPT registers demoted for a
    // region-internal clock or an asynchronous reset, which take this boundary
    // path.
    bool       flop_boundary = gu::is_type_flop(n) && (!options.map_register || clk_demoted.contains(n) || reset_demoted.contains(n));
    // A LATCH is a boundary in BOTH modes, unconditionally (2f-latch M2).
    // TERMINOLOGY TRAP: an ABC/AIGER "latch" is an edge-triggered unit-delay
    // register on an implicit global clock, NOT a level-sensitive latch — and
    // ABC's BLIF reader silently DISCARDS the `.latch` control tokens. So
    // letting a real latch cross into ABC in seq mode (the way a flop does)
    // would not be an error, it would be a silent MISMODEL. Keeping it native
    // means q feeds the mapped logic as a fresh PI and din/enable are cut as
    // POs, exactly like a Sub/Memory. Before this, a Latch matched none of the
    // cases below and fell into the bit-blast loop, aborting the whole region.
    const bool latch_boundary = op == Ntype_op::Latch;
    if (op != Ntype_op::Sub && op != Ntype_op::Memory && op != Ntype_op::Clock_cell && op != Ntype_op::Rem && !flop_boundary
        && !latch_boundary && !native_wiring.contains(n)) {
      continue;
    }
    if (op == Ntype_op::Rem) {
      // REMAINDER is where the synthesis constraint lives, and it is an ERROR
      // rather than a warning. The rest of LiveHD handles `%` as an ordinary
      // op -- bitwidth ranges it, constprop folds it, the LEC encoder proves it
      // (SREM), the simulator runs it -- because none of those need it to become
      // gates. Only the netlist mapper does. Raising it HERE, rather than at
      // lowering time, is what lets a design that merely CONTAINS `%` compile,
      // simulate and verify.
      //
      // Anything trivially convertible was already rewritten to a mask by
      // rewrite_trivial_rems() above, so reaching this point means the shape
      // genuinely has no easy gate-level translation.
      livehd::diag::err("pass.abc", "rem-unsupported", "unsupported")
          .at(node_span(rb, n))
          .msg("pass.abc: {} in region '{}': remainder (`%`) has no gate-level translation", node_identity(n), rb.module_name)
          .hint(
              "only a power-of-two divisor over a non-negative dividend converts trivially (to a mask); keep other "
              "remainders out of the synthesized region")
          .emit();
    }
    Bbox bb;
    bb.node                              = n;
    bb.op                                = op;
    int                           bb_idx = static_cast<int>(bboxes.size());
    absl::flat_hash_map<int, int> concat_lane_width;
    if (op == Ntype_op::Concat) {
      const auto lanes = gu::concat_lanes(n);
      for (size_t lane = 0; lane < lanes.size(); ++lane) {
        concat_lane_width.emplace(static_cast<int>(2 * lane), lanes[lane].width);
      }
    }
    // outputs: distinct driver pins that feed region logic -> fresh PI sources.
    // btree_map (ascending port_id) so the fresh-PI creation order — hence ABC
    // ObjId assignment and the read-back `g<id>_<cell>` gate names — is
    // deterministic; a flat_hash_map iterates in run-to-run-varying order.
    absl::btree_map<int, hhds::Pin_class> out_pins;
    for (const auto& out_pin : n.out_sorted_pins()) {  // the node's driver pins, once each
      out_pins.emplace(static_cast<int>(out_pin.get_port_id()), out_pin);
    }
    for (auto& [pid, op_pin] : out_pins) {
      int w = gu::bits_of(op_pin);
      if (w == 0) {
        w = 1;
      }
      // A one-node native boundary has no combinational consumer inside this
      // region. Its output can reconnect straight to the region output; making
      // one ABC PI/PO buffer per bit is pure overhead (Rob has 20k--42k-bit
      // register-only regions).
      bool needs_abc = rb.nodes.size() != 1 && !native_wiring.contains(n);
      if (!needs_abc) {
        for (const auto& e : op_pin.out_edges()) {
          const auto sink_node = e.sink.get_master_node();
          if (region.contains(sink_node) && !native_wiring.contains(sink_node)) {
            needs_abc = true;
            break;
          }
        }
      }
      int oi = static_cast<int>(bb.outs.size());
      bb.outs.push_back({op_pin, pid, w, !gu::is_unsign(op_pin), needs_abc});
      if (!needs_abc) {
        continue;  // boundary-to-boundary bus reconnects natively on read-back
      }
      bbox_output_index.emplace(op_pin, Bbox_output_origin{bb_idx, oi});
    }
    // inputs: const-driven recreated directly; comb-driven cut as POs. Any pin
    // driven straight by a region input is reconnected natively instead: there
    // is no Boolean logic for ABC to optimize. This is essential for wide
    // shared-Sub inputs (Rob carries a 10,260-bit source bus into hundreds of
    // instances); routing a direct wire through ABC otherwise materializes one
    // output buffer per bit. It also subsumes the clock/reset/enable treatment
    // for native flop/latch boundaries.
    for (const auto& in_pin : n.inp_sorted_pins()) {
      for (auto in_drv : in_pin.get_driver_pins()) {
        // The compact carry edge means previous ordinal, not same-instance
        // Boolean feedback. Keep it out of ABC and restore it with the descriptor.
        if (n.is_loop_subnode() && in_drv.get_master_node() == n) {
          continue;
        }

        int pid = static_cast<int>(in_pin.get_port_id());
        if (in_drv.is_const()) {
          bb.const_ins.emplace_back(pid, in_drv);
        } else {
          const auto lane_fit      = concat_lane_width.find(pid);
          const bool needs_fit     = lane_fit != concat_lane_width.end() && gu::bits_of(in_drv) > lane_fit->second;
          const bool native_driver = region_in_name.contains(in_drv) || native_wiring.contains(in_drv.get_master_node());
          if (native_driver) {
            if (needs_fit) {
              // A Concat lane is an explicit width boundary. The source normally
              // has a Get_mask in front of an over-wide packed-array carrier, but
              // that zero-delay wrapper can disappear while native boundaries
              // are cut and reconstructed. Recreate it natively below rather
              // than mapping W identity buffers through ABC.
              bb.fit_native_ins.emplace_back(pid, in_drv, lane_fit->second, !gu::is_unsign(in_drv));
            } else {
              bb.native_ins.emplace_back(pid, in_drv);
            }
            continue;
          }
          int w = gu::bits_of(in_drv);
          if (w == 0) {
            w = 1;
          }
          if (lane_fit != concat_lane_width.end()) {
            w = std::min(w, lane_fit->second);
          }
          bb.ins.push_back({pid, in_drv, w, !gu::is_unsign(in_drv)});
        }
      }
    }
    bboxes.push_back(std::move(bb));
  }

  // --- bit-blast each region node in dependency order. `rb.nodes` (the order
  // the partitioner collected the region in) is
  // *mostly* topological, but it can emit a reader before its producer (the
  // same phenomenon the LEC encoder fixpoints around for forward_hier — seen
  // on the DINO top, where a wide packed-bus Get_mask was read by an Sra a
  // thousand nodes before the Get_mask was visited). A single pass would then
  // read the unmaterialized operand as const0 and silently miscompile the
  // whole cone. Schedule with a dependency queue: a node is ready when every
  // comb operand is a constant, a region input, a seeded boundary, or an
  // earlier-blasted node. A repeated whole-pending-list fixpoint is quadratic
  // on reverse-ordered cones (Rob has 12k-node regions); the queue visits every
  // dependency once. A stuck remainder (a genuine node-level cycle or broken
  // boundary) is appended in traversal order so abc_bit's unmaterialized-driver
  // diagnostic pinpoints the const0 reads.
  std::vector<hhds::Node_class> blast_order;
  {
    std::vector<hhds::Node_class> pending;
    for (const auto& n : rb.nodes) {
      auto op = gu::type_op_of(n);
      if (op == Ntype_op::Sub || op == Ntype_op::Memory || op == Ntype_op::Clock_cell || op == Ntype_op::Rem
          || native_wiring.contains(n)) {
        continue;  // native boundary -- never eagerly bit-blasted
      }
      if (gu::is_type_flop(n)) {
        continue;  // flop: a 1-bit latch in seq mode, a native boundary in !seq mode -- never bit-blasted
      }
      if (op == Ntype_op::Latch) {
        continue;  // level-sensitive latch: always a native boundary (2f-latch M2), never bit-blasted
      }
      pending.push_back(n);
    }
    absl::flat_hash_set<hhds::Pin_class> ready;  // driver pins with materialized (or scheduled) bit slots
    ready.reserve(bitnet.size() + pending.size());
    for (const auto& kv : bitnet) {
      ready.insert(kv.first);
    }
    // Native boundary outputs are demand-created PIs, so they need not have
    // any entry in bitnet yet. They are nevertheless available immediately.
    // Otherwise their consumers enter the stuck remainder in arbitrary order
    // and a later slice can be read before it has been bit-blasted.
    for (const auto& kv : bbox_output_index) {
      ready.insert(kv.first);
    }
    absl::flat_hash_map<hhds::Node_class, size_t>                       unresolved;
    absl::flat_hash_map<hhds::Pin_class, std::vector<hhds::Node_class>> waiters;
    std::vector<hhds::Node_class>                                       queue;
    unresolved.reserve(pending.size());
    waiters.reserve(pending.size());
    queue.reserve(pending.size());
    for (const auto& n : pending) {
      size_t count = 0;
      for (const auto& in_pin : n.inp_sorted_pins()) {
        const auto  in_drv = in_pin.get_driver_pin();
        const auto& d      = in_drv;
        if (d.is_invalid() || d.is_const() || ready.contains(d) || region_input_index.contains(d)) {
          continue;
        }
        ++count;
        waiters[d].push_back(n);
      }
      unresolved.emplace(n, count);
      if (count == 0) {
        queue.push_back(n);
      }
    }
    blast_order.reserve(pending.size());
    size_t scheduled_count = 0;
    for (size_t head = 0; head < queue.size(); ++head) {
      const auto n = queue[head];
      ++scheduled_count;
      // Concat has no Boolean logic. Keep it in this dependency queue so all
      // lane producers precede its consumers, but resolve only demanded bits
      // through abc_bit instead of eagerly copying every bit of every lane.
      if (gu::type_op_of(n) != Ntype_op::Concat) {
        blast_order.push_back(n);
      }
      absl::flat_hash_set<hhds::Pin_class> produced;
      for (const auto& out_pin : n.out_sorted_pins()) {  // what this node PRODUCES: its driver pins
        produced.insert(out_pin);
      }
      if (produced.empty()) {
        produced.insert(n.create_driver_pin(0));
      }
      for (const auto& d : produced) {
        ready.insert(d);
        auto wit = waiters.find(d);
        if (wit == waiters.end()) {
          continue;
        }
        for (const auto& consumer : wit->second) {
          auto& count = unresolved.at(consumer);
          if (--count == 0) {
            queue.push_back(consumer);
          }
        }
      }
    }
    if (scheduled_count != pending.size()) {
      std::vector<hhds::Node_class> stuck;
      stuck.reserve(pending.size() - scheduled_count);
      for (const auto& n : pending) {
        if (unresolved.at(n) != 0) {
          stuck.push_back(n);
        }
      }
      if (options.verbose) {
        std::print("[pass.abc] region '{}': scheduler stuck with {} node(s); unresolved dependencies:\n",
                   rb.module_name,
                   stuck.size());
        size_t shown = 0;
        for (const auto& n : stuck) {
          for (const auto& in_pin : n.inp_sorted_pins()) {
            const auto  in_drv = in_pin.get_driver_pin();
            const auto& d      = in_drv;
            if (d.is_invalid() || d.is_const() || ready.contains(d) || region_input_index.contains(d)) {
              continue;
            }
            const auto dn = d.get_master_node();
            std::print("  {} ({}) <- {} ({}) p{} region={} seeded={}\n",
                       gu::debug_name(n),
                       Ntype::get_name(gu::type_op_of(n)),
                       gu::debug_name(dn),
                       Ntype::get_name(gu::type_op_of(dn)),
                       d.get_port_id(),
                       region.contains(dn),
                       bitnet.contains(d));
            if (++shown == 32) {
              break;
            }
          }
          if (shown == 32) {
            break;
          }
        }
        std::fflush(stdout);
      }
      for (const auto& n : stuck) {
        if (gu::type_op_of(n) != Ntype_op::Concat) {
          blast_order.push_back(n);
        }
      }
    }
  }
  trace_stage("scheduled");
  // Memory admission: sample our own RSS as the region is bit-blasted, and stop
  // before the ABC flow if this region will not fit. The first sample is at ~5%
  // of the work (early enough that a hopeless region dies cheaply), then every
  // 2% so a region that grows non-linearly is still caught. RSS is a syscall, so
  // it is sampled -- never read per node.
  const uint64_t rss_before  = !hooks.over_budget ? 0 : cost::process_footprint_bytes();
  const size_t   blast_total = blast_order.size();
  result.rss_before          = rss_before;
  result.blast_total         = blast_total;
  const size_t   sample_step = std::max<size_t>(1, blast_total / 50);
  size_t         blasted     = 0;

  for (const auto& n : blast_order) {
    if (options.verbose && blasted != 0 && blasted % 1000 == 0) {
      std::print("[pass.abc] region '{}': blast {}/{} before {} at {:.0f} ms\n",
                 rb.module_name,
                 blasted,
                 blast_total,
                 Ntype::get_name(gu::type_op_of(n)),
                 since());
      std::fflush(stdout);
    }
    if (hooks.over_budget && ++blasted % sample_step == 0 && blasted >= blast_total / 20) {
      if (hooks.over_budget(rss_before, blasted, blast_total, lnet.size() - 1 + lnet.outputs().size())) {
        result.status = Region_blast::Status::over_budget;
        return result;  // no partial result; the caller reports the refusal
      }
    }
    auto op       = gu::type_op_of(n);
    auto out_pin  = n.create_driver_pin(0);
    int  out_bits = gu::bits_of(out_pin);
    if (out_bits == 0) {
      out_bits = 1;
    }
    if (op == Ntype_op::Sum) {
      // All Sum outputs are realizations of the same integer expression.
      // Build one adder at the largest requested width and share its prefixes.
      for (const auto& op_pin : n.out_sorted_pins()) {  // widest driver pin; consumers do not matter
        out_bits = std::max(out_bits, gu::bits_of(op_pin));
      }
    }
    if (op == Ntype_op::SHL || op == Ntype_op::Not) {
      const auto demand = gu::masked_output_width(n, [&](const auto& consumer) { return region.contains(consumer); });
      if (demand > 0) {
        out_bits = std::min(out_bits, static_cast<int>(demand));
      }
    }
    auto& slots = bitnet[out_pin];

    blast_comb(n, out_bits, slots, ops, abc_bit, options, rb, region, refuse, refuse_shift_amount);
    if (op == Ntype_op::Sum) {
      absl::flat_hash_set<hhds::Pin_class> outputs;
      for (const auto& op_pin : n.out_sorted_pins()) {  // the node's driver pins, once each
        outputs.insert(op_pin);
      }
      for (const auto& output : outputs) {
        if (output == out_pin) {
          continue;
        }
        auto& target = bitnet[output];
        for (int bit = 0; bit < std::max(1, gu::bits_of(output)); ++bit) {
          target[bit] = slots.at(bit);
        }
      }
    }
  }
  if (unsupported) {
    result.status = Region_blast::Status::refused;
    if (refusals > kMaxRefusals) {
      livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
          .msg("pass.abc: region '{}': {} further node(s) were refused; only the first {} are reported above",
               rb.module_name,
               refusals - kMaxRefusals,
               kMaxRefusals)
          .emit();
    }
    return result;
  }
  trace_stage("blast-complete");

  // --- sequential: wire each latch's data-in (D) to the folded next-state ---
  // Asynchronous-reset flops were kept as native boundaries above because the
  // selected plain DFF cannot represent the reset event. Every crossed flop's
  // next state is therefore `rst ? rval : (en ? din : Q)` -- a synchronous
  // reset has priority over the enable, exactly cgen's
  // `if (rst) q <= rval; else if (en) q <= din;` and pass/lec's
  // ITE(rst, init, ITE(en, din, q)) -- and a missing `initial` resets to 0
  // like tolg's nil init. Folding enable and reset into the AIG means the
  // reconstructed flop is a plain D-flop (only clock + a resetless power-on
  // init reattached), and ABC sees the true next-state function so
  // retiming/sweeping stays sound.
  // enable/reset are single control signals: an N-bit pin asserts on (pin != 0),
  // i.e. the OR-reduction of its bits (matches cgen/yosys reg semantics and the
  // LEC's `rst != 0`). Reduce once per flop, not per data bit.
  auto reduce_or = [&](const hhds::Pin_class& p) -> Lid {
    int w = gu::bits_of(p);
    if (w <= 0) {
      w = 1;
    }
    Lid acc = abc_bit(p, 0);
    for (int k = 1; k < w; ++k) {
      acc = ops.or_(acc, abc_bit(p, k));
    }
    return acc;
  };
  for (auto& f : flops) {
    std::optional<Lid> en_active;
    std::optional<Lid> rst_active;
    if (!f.en_drv.is_invalid()) {
      en_active = reduce_or(f.en_drv);
    }
    if (!f.rst_drv.is_invalid()) {
      rst_active = reduce_or(f.rst_drv);
      if (f.neg_reset) {
        rst_active = ops.inv(*rst_active);
      }
    }
    for (int b = 0; b < f.bits; ++b) {
      Lid d = f.previous.empty() ? abc_bit(f.din_drv, b) : f.previous[b];
      if (en_active) {
        d = ops.mux(*en_active, d, f.qbits[b]);  // (en != 0)? din : Q
      }
      if (rst_active) {
        const Lid rval = f.rval_drv.is_invalid() ? ops.konst(false) : abc_bit(f.rval_drv, b);
        d               = ops.mux(*rst_active, rval, d);  // reset? rval : (en? din : Q)
      }
      // QN cell under the built-in flow: the latch stores ~next_state (abc_not
      // folds constants; strash turns it into a complemented edge), so `&nf`
      // maps ~f as part of its own phase assignment and mints an INV only where
      // nothing absorbs it (a D fed straight by a port). It does perturb the
      // mapping either way -- same binary, br_arb_rr comb 205 gates / 14.70
      // um^2 -> 244 / 17.96, br_credit_sender 67.0 -> 60.2 -- but the aggregate
      // is far cheaper than the read-back absorption (Seq_flop::d_inverted),
      // which the other latches take.
      lnet.set_latch_input(f.latch[b], f.d_inverted ? ops.inv(d) : d);
    }
  }

  // --- region outputs -> per-bit ABC POs ---
  direct_native_output.assign(rb.outputs.size(), false);
  absl::flat_hash_set<hhds::Pin_class> direct_boundary_outputs;
  for (const auto& bb : bboxes) {
    for (const auto& out : bb.outs) {
      if (!out.abc_bits) {
        direct_boundary_outputs.insert(out.src_pin);
      }
    }
  }
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    const auto& port = rb.outputs[po];
    if (native_wiring.contains(port.src_driver.get_master_node()) || direct_boundary_outputs.contains(port.src_driver)) {
      direct_native_output[po] = true;
      continue;
    }
    int w = port.bits == 0 ? 1 : port.bits;
    for (int b = 0; b < w; ++b) {
      const auto value = abc_bit(port.src_driver, b);
      lnet.add_output(value, std::format("__po{}_{}_b{}", po, port.name, b));
      // A PO is already a connectivity boundary, so the source node gets a
      // uniquely named NET alias rather than an explicit identity node (ABC's
      // netlist checker requires unique CO net names; an explicit node would
      // cost wide shared-Sub inputs one cell per boundary bit, Rob: 523 x
      // 10,260). The alias alone does NOT avoid a cell, though: `&put`
      // re-decouples every CO driver (Abc_NtkLogicMakeSimpleCos), so a PO fed
      // straight by a PI, a latch Q or a blackbox output comes back as a
      // Liberty buffer anyway -- 512 of br_demux_onehot's 528 cells were
      // exactly that. What removes them is the identity-buffer bypass in the
      // read-back (is_identity_gate / only_co_fanouts, pass 1b), which aliases
      // the buffer's output net to its input net and mints no Sub. Read-back
      // pairs POs by creation order.
      po_order.emplace_back(po, b);
    }
  }
  trace_stage("region-pos");

  // --- blackbox combinational inputs -> per-bit ABC POs (appended after the
  // region outputs so the region-output read-back stays index-aligned) ---
  // One ABC PO per UNIQUE (source driver, bit), with every blackbox input that
  // consumes it. Repeated shared instances often read the same very wide bus;
  // emitting a PO per consumer duplicates pure interface work quadratically.
  absl::flat_hash_map<hhds::Pin_class, std::vector<int32_t>> bbox_po_index;
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    auto& bb = bboxes[bi];
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      const auto& in    = bb.ins[ii];
      auto&       index = bbox_po_index[in.drv];
      if (static_cast<int>(index.size()) < in.bits) {
        index.resize(in.bits, -1);
      }
      for (int b = 0; b < in.bits; ++b) {
        if (index[b] < 0) {
          const auto value = abc_bit(in.drv, b);
          lnet.add_output(value, std::format("__bb{}_i{}_b{}", bi, ii, b));
          index[b] = static_cast<int32_t>(bbox_po.size());
          bbox_po.emplace_back();
        }
        bbox_po[static_cast<size_t>(index[b])].push_back({static_cast<int>(bi), static_cast<int>(ii), b});
      }
    }
  }
  trace_stage("bbox-pos");

  // A region made entirely of direct native boundaries has no real ABC
  // outputs. ABC's dch implementation crashes on that empty network; retain a
  // single unobserved constant PO as a mapper sentinel. Readback intentionally
  // ignores it because it is absent from both po_order and bbox_po.
  if (lnet.outputs().empty()) {
    has_dummy_po     = true;
    const auto value = ops.konst(false);
    lnet.add_output(value, "__livehd_dummy_po");
  }

  return result;
}

}  // namespace livehd::synth
