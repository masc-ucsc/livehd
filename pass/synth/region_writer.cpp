// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The read-back of a mapped region (region_writer.hpp), lifted from pass.abc's
// map_region onto the backend-neutral Cell_netlist.
#include "region_writer.hpp"

#include <algorithm>
#include <format>
#include <numeric>

#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "diag.hpp"
#include "dlop.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"

namespace livehd::synth {
namespace gu = livehd::graph_util;

namespace {
// ABC uses compact one-bit SRA selectors at native wide boundaries. When the
// boundary is a Set_mask chain, those selectors make every intermediate packed
// value observable and force cgen/Yosys to retain a quadratic chain of wide
// copies. Resolve each demanded bit through the constant masks to the actual
// base/value driver. This is the same bit identity used by abc_bit() while
// mapping, applied to the reconstructed body after every connection exists.
void bypass_setmask_bit_reads(hhds::Graph* g) {
  struct Rewrite {
    hhds::Node_class node;
    hhds::Pin_class  source;
    int64_t          bit;
  };
  std::vector<Rewrite> rewrites;

  for (auto node : g->body().nodes()) {
    if (gu::type_op_of(node) != Ntype_op::SRA || gu::bits_of(node.get_driver_pin(0)) != 1) {
      continue;
    }
    auto source = gu::get_driver_of_sink_name(node, "a");
    auto amount = gu::get_driver_of_sink_name(node, "b");
    if (source.is_invalid() || amount.is_invalid() || !amount.is_const() || !gu::const_of(amount).is_just_i64()) {
      continue;
    }
    int64_t bit = gu::const_of(amount).to_just_i64();
    if (bit < 0 || source.is_const() || gu::type_op_of(source.get_master_node()) != Ntype_op::Set_mask) {
      continue;
    }

    absl::flat_hash_set<hhds::Class_index> visited;
    bool                                   resolved = false;
    while (!source.is_const() && gu::type_op_of(source.get_master_node()) == Ntype_op::Set_mask) {
      auto writer = source.get_master_node();
      if (!visited.insert(writer.get_class_index()).second) {
        resolved = false;
        break;
      }
      auto mask  = gu::get_driver_of_sink_name(writer, "mask");
      auto base  = gu::get_driver_of_sink_name(writer, "a");
      auto value = gu::get_driver_of_sink_name(writer, "value");
      if (mask.is_invalid() || base.is_invalid() || value.is_invalid() || !mask.is_const()) {
        resolved = false;
        break;
      }
      auto window = gu::mask_window_of(gu::const_of(mask));
      if (!window) {
        resolved = false;
        break;
      }
      const auto [begin, end] = *window;
      resolved                = true;
      if (bit >= begin && bit < end) {
        source  = value;
        bit    -= begin;
        break;
      }
      source = base;
    }
    if (resolved && !source.is_invalid()) {
      rewrites.push_back({node, source, bit});
    }
  }

  for (const auto& rewrite : rewrites) {
    hhds::Pin_class replacement = rewrite.source;
    if (rewrite.bit != 0 || gu::bits_of(replacement) != 1) {
      auto select = gu::create_typed_node(*g, Ntype_op::SRA);
      replacement.connect_sink(gu::setup_sink_by_name(select, "a"));
      gu::create_const(*g, *Dlop::create_integer(rewrite.bit)).connect_sink(gu::setup_sink_by_name(select, "b"));
      replacement = select.create_driver_pin(0);
      gu::set_bits(replacement, 1);
      gu::set_unsign(replacement);
    }
    // SNAPSHOT the fan-out: connect_driver() below adds an edge, which
    // invalidates the lazy out_edges() view this used to rewire from.
    std::vector<hhds::Pin_class> readers;
    for (const auto& edge : rewrite.node.out_edges()) {
      readers.push_back(edge.sink);
    }
    for (const auto& reader : readers) {
      reader.connect_driver(replacement);
    }
    rewrite.node.del_node();
  }
}
}  // namespace

bool Region_writer::write(const livehd::partition::Region_body& rb, const Region_blast& blast, const Cell_netlist& mapped,
                          const Cell_library& lib, const Registers& registers, Counts& counts,
                          const std::function<void(std::string_view)>& trace_stage) {
  const auto& outputs              = blast.lnet.outputs();
  const auto& flops                = blast.flops;
  const auto& bboxes               = blast.bboxes;
  const auto& native_comb_logic    = blast.native_comb_logic;
  const auto& region_in_name       = blast.region_in_name;
  const auto& pi_order             = blast.pi_order;
  const auto& all_pi_order         = blast.all_pi_order;
  const auto& bbox_pi              = blast.bbox_pi;
  const auto& po_order             = blast.po_order;
  const auto& bbox_po              = blast.bbox_po;
  const auto& direct_native_output = blast.direct_native_output;
  const bool  has_dummy_po         = blast.has_dummy_po;
  const bool  map_register         = registers.map;
  const auto& dff                  = *registers.cell;
  const auto& dff_ladder           = *registers.ladder;
  bool        unsupported          = false;  // a black box whose child def is missing

  // --- read back: each mapped gate -> a 1-bit blackbox Sub in the body ---
  auto* body = rb.body;

  // Source-map carry-through (task 2a-abc): ABC's strash/dch destroy per-node
  // provenance, so re-mint each output port's original driver srcid. Into the
  // output LIBRARY's shared srcmap, never the body's own locator: a per-body
  // import re-copies the per-FILE metadata (line-offset tables) into every
  // region body -- the pass.partition std::bad_alloc shape -- while the body
  // resolves the id through its library base chain either way.
  auto&                       out_srcmap = body->get_io()->get_library()->source_map();
  std::vector<hhds::SourceId> po_srcid(rb.outputs.size(), hhds::SourceId_invalid);
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    auto drv = rb.outputs[po].src_driver;
    if (drv.is_invalid()) {
      continue;
    }
    auto onode = drv.get_master_node();
    if (onode.is_invalid()) {
      continue;
    }
    if (auto a = onode.attr(hhds::attrs::srcid); a.has() && a.get() != 0) {
      po_srcid[po] = out_srcmap.import_from(rb.src->source_locator(), a.get());
    }
  }

  // find-or-declare a 1-bit blackbox cell def (Liberty pins) in the out library
  auto cell_desc = [&](uint32_t type) -> const Cell_decl& { return lib.decl(*outlib_, type); };

  // Select one bit as an explicit Get_mask with the one-hot `(1 << b)` mask.
  //
  // This used to be `(bus >> b)` stamped to one bit, which kept a W-bit
  // boundary's selector constants at O(W log W) instead of the O(W^2) the
  // one-hot bigints cost. That form is NOT reload-safe (see below), so the
  // one-hot is the price of correctness; a W in the tens of thousands (whole
  // design flatten forces this path at every width, below) pays ~W^2/8 bytes of
  // pooled constants. Revisit as `Get_mask(SRA(bus, b), mask=1)` -- 3 nodes per
  // bit but log-sized constants -- if that ever becomes the bottleneck.
  auto extract_body_bit = [&](const hhds::Pin_class& bus, int b) {
    // A shift with a one-bit hint is still an unlimited-precision shift.
    // Its high bits reappear when a saved mapped graph is compiled again,
    // violating any Concat lane it feeds. Select the bit explicitly.
    auto select = gu::create_get_mask(*body, bus, b, b + 1);
    auto out    = select.create_driver_pin(0);
    gu::set_ubits(out, 1);
    return out;
  };

  // Lazily build bit b of a body input pin (compact shift-select; pin itself if 1-bit).
  std::vector<std::vector<hhds::Pin_class>> in_bit(rb.inputs.size());
  std::vector<hhds::Pin_class>              body_input_pin(rb.inputs.size());
  std::vector<hhds::Node_class>             body_input_splitter(rb.inputs.size());
  for (size_t port_idx = 0; port_idx < rb.inputs.size(); ++port_idx) {
    body_input_pin[port_idx] = body->get_input_pin(rb.inputs[port_idx].name);
  }
  // Which bits of each region input the mapping actually reads, MOST
  // SIGNIFICANT FIRST. `pi_order` is FINAL here -- abc_bit appends to it during
  // bit-blast and creates each (port, bit) PI at most once -- so the read-back
  // knows every port's exact demand before it materializes anything. Without it
  // the unpacker below would have to assume the whole bus is read.
  //
  // The descending sort is load-bearing, not cosmetic: hhds keeps a node's pins
  // in ascending port order and both insert paths break at the list head, so a
  // strictly DECREASING sequence costs O(1) per pin -- sparse or dense --
  // whereas ascending rescans the whole list for every bit.
  std::vector<std::vector<int>> port_demand(rb.inputs.size());
  for (const auto& [demanded_pi, demanded_bit] : pi_order) {
    port_demand[demanded_pi].push_back(demanded_bit);
  }
  for (auto& bits : port_demand) {
    std::sort(bits.begin(), bits.end(), std::greater<int>());
  }
  auto shared_input_splitter = [&](int width) -> Input_splitter& {
    if (auto it = input_splitters_.find(width); it != input_splitters_.end()) {
      return it->second;
    }

    Input_splitter split;
    auto           name = std::format("__livehd_abc_input_bits_{}", width);
    split.bit_port.resize(width);
    // FIND-or-create, exactly like blackbox_io above: an incremental cache HIT
    // re-declares the splitter defs a reused body references, and a second
    // pass.abc into the same output library sees the ones this run created.
    // A second create_io under a live name aborts the library.
    split.io         = outlib_->find_io(name);
    const bool fresh = !split.io;
    if (fresh) {
      split.io = outlib_->create_io(name);
      split.io->add_input("a", 1);
      split.io->set_bits("a", width);
    }
    // HHDS stores a node's nonzero-port pins in ascending order. Adding ports
    // in descending order always inserts at the head; ascending order rescans
    // the whole list for every bit (O(width^2), 52M comparisons at 10260b).
    for (int b = width - 1; b >= 0; --b) {
      split.bit_port[b] = static_cast<hhds::Port_id>(b + 2);
      if (fresh) {
        auto output = std::format("b{}", b);
        split.io->add_output(output, split.bit_port[b]);
        split.io->set_bits(output, 1);
      }
    }
    if (!split.io->get_graph()) {  // a re-declared def carries the IO only
      auto split_body = split.io->create_graph();
      auto input      = split_body->get_input_pin("a");
      for (int b = width - 1; b >= 0; --b) {
        auto output = std::format("b{}", b);
        auto shift  = gu::create_typed_node(*split_body, Ntype_op::SRA);
        input.connect_sink(gu::setup_sink_by_name(shift, "a"));
        gu::create_const(*split_body, *Dlop::create_integer(b)).connect_sink(gu::setup_sink_by_name(shift, "b"));
        auto bit = shift.create_driver_pin(0);
        gu::set_bits(bit, 1);
        gu::set_unsign(bit);
        bit.connect_sink(split_body->get_output_pin(output));
      }
      split_body->commit();
    }
    auto [it, inserted] = input_splitters_.emplace(width, std::move(split));
    I(inserted);
    return it->second;
  };
  auto input_bit = [&](size_t port_idx, int b) -> hhds::Pin_class {
    auto&       cache = in_bit[port_idx];
    const auto& port  = rb.inputs[port_idx];
    const int   w     = port.bits == 0 ? 1 : port.bits;
    // Sized by b, not by w: abc_bit only clamps a demanded bit against a
    // NON-ZERO width (`if (w != 0 && i >= w)`), so an unstamped port -- w
    // forced to 1 here -- can legitimately demand bit 5 and index past a
    // w-sized cache.
    if (static_cast<int>(cache.size()) <= std::max(b, w - 1)) {
      cache.resize(std::max(b + 1, w));
    }
    if (!cache[b].is_invalid()) {
      return cache[b];
    }
    auto ipin = body_input_pin[port_idx];
    if (w == 1 && b == 0) {
      cache[b] = ipin;  // the pin IS its only bit
      return ipin;
    }
    // The shared splitter DEF is all-or-nothing (its body and its w output
    // decls are minted together and carried through the region cache as one
    // unit), so it only pays when this region reads MOST of a wide bus: on a
    // region reading 20 bits of a 10k-bit port the def alone is ~2000x the work
    // the lazy PI path just avoided, and one port can never repay it. It is not
    // free otherwise either -- it puts a non-cell module in the emitted netlist
    // (a whole-design flatten is contracted to emit exactly ONE module, see
    // lhd_abc_flat_test) and it has to be carried through the region cache.
    // Below any of the three gates, extract the demanded bits in place -- the
    // same shift-select the blackbox/latch read-back uses, at 2 nodes per
    // DEMANDED bit, with no def at all.
    constexpr int kSharedSplitterMinBits = 256;
    const auto&   demand                 = port_demand[port_idx];
    if (flat_ || w < kSharedSplitterMinBits || static_cast<int>(demand.size()) < w - w / 2) {
      cache[b] = extract_body_bit(ipin, b);
      return cache[b];
    }
    auto& inst = body_input_splitter[port_idx];
    if (inst.is_invalid()) {
      auto& split = shared_input_splitter(w);
      inst        = gu::create_typed_node(*body, Ntype_op::Sub);
      inst.set_subnode(split.io);
      ipin.connect_sink(livehd::graph_util::setup_sink_pid(inst, 1));
      // Only the DEMANDED bits, and in the descending order `demand` is already
      // sorted into, so each pin is a head insert. The INSTANCE may be partial
      // even though the def is not: every consumer of a Sub resolves its ports
      // from EDGES (cgen create_subs, cgen_sim, lec encode), and a pin for an
      // unread bit would carry no edge in either case. Retaining every handle
      // also keeps the PI loop from searching the node's long pin list.
      for (int bit : demand) {
        cache[bit] = inst.create_driver_pin(split.bit_port[bit]);
      }
    }
    if (cache[b].is_invalid()) {  // a bit outside the precomputed demand
      cache[b] = inst.create_driver_pin(shared_input_splitter(w).bit_port[b]);
    }
    return cache[b];
  };

  // Signals and cells are dense indices. Direct vectors avoid two hash
  // operations for every mapped edge during read-back (millions on Rob).
  const auto                    signal_slots = mapped.signals.size();
  std::vector<hhds::Pin_class>  net2drv(signal_slots);
  std::vector<hhds::Node_class> mapped_node2sub(mapped.cells.size());
  // Identity-buffer bypass (is_identity_gate above): a bypassed buffer's OUTPUT
  // net resolves to its INPUT net. Resolved lazily in get_net_driver rather
  // than copied at bypass time because the input net's driver may not exist
  // yet -- a latch Q net is only filled in pass 1c, after the gate pass -- and
  // every consumer (gate fanins, flop/DFF din, POs, blackbox inputs) already
  // goes through get_net_driver, so no pass has to move.
  std::vector<int32_t>          net_alias(signal_slots, -1);
  int                           bypassed_bufs  = 0;
  auto                          set_net_driver = [&](uint32_t net, const hhds::Pin_class& driver) {
    I(net != Cell_netlist::kNone);
    const auto id = static_cast<size_t>(net);
    I(id < net2drv.size());
    net2drv[id] = driver;
  };
  auto get_net_driver = [&](uint32_t net) -> hhds::Pin_class {
    if (net == Cell_netlist::kNone) {
      return {};
    }
    auto id = static_cast<size_t>(net);
    // Follow the alias chain (a bypassed buffer fed by another bypassed
    // buffer). An alias only ever points from a buffer's output net to its
    // input net, so the chain is acyclic and ends at a real driver.
    while (id < net_alias.size() && net_alias[id] >= 0) {
      id = static_cast<size_t>(net_alias[id]);
    }
    if (id >= net2drv.size()) {
      return {};
    }
    return net2drv[id];
  };
  // Readers as the backend counts them: every cell fanin, output and latch input.
  const auto readers      = mapped.readers();
  const auto cell_readers = mapped.cell_readers();

  // pass 1.bbox: rebuild each blackbox node (Sub instance / memory) natively.
  // Its output pins drive the boundary PIs (mapped in pass 1a); its inputs are
  // wired in pass 2c once their driving cones resolve. Const inputs are wired now.
  struct Bbox_recon {
    hhds::Node_class                          node;
    std::vector<hhds::Pin_class>              out_pin;  // [out idx] -> reconstructed full-width driver
    std::vector<std::vector<hhds::Pin_class>> out_bit;  // [out idx][bit] -> body driver
    std::vector<std::vector<hhds::Pin_class>> in_bit;   // [in idx][bit] -> body driver (filled pass 3)
  };
  // Only these boundary bits became ABC PIs.  Recreate selectors for exactly
  // that set; extracting every bit of the full bus here would merely move the
  // eager explosion from translation to readback.
  std::vector<std::vector<std::vector<int>>> bbox_demand(bboxes.size());
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    bbox_demand[bi].resize(bboxes[bi].outs.size());
  }
  for (const auto& [bx, oi, bit] : bbox_pi) {
    I(bx >= 0 && static_cast<size_t>(bx) < bbox_demand.size());
    I(oi >= 0 && static_cast<size_t>(oi) < bbox_demand[static_cast<size_t>(bx)].size());
    bbox_demand[static_cast<size_t>(bx)][static_cast<size_t>(oi)].push_back(bit);
  }
  std::vector<Bbox_recon> bbox_recon(bboxes.size());
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    auto& bb = bboxes[bi];
    auto& br = bbox_recon[bi];
    auto  nn = gu::create_typed_node(*body, bb.op);
    if (bb.op == Ntype_op::Sub) {
      if (auto child = bb.node.get_subnode_io()) {
        // A def with a body was partitioned children-first; a body-less black
        // box (e.g. a liberty cell when re-mapping an already-mapped netlist)
        // is cloned as an IO-only decl so the instance stays opaque.
        if (auto out_child = livehd::partition::resolve_or_clone_subdef(outlib_, bb.node)) {
          if (auto loop = bb.node.subnode_loop()) {
            nn.set_subnode(out_child, *loop);
          } else {
            nn.set_subnode(out_child);
          }
        } else {
          livehd::diag::err("pass.abc", "missing-subdef", "unsupported")
              .msg("pass.abc: sub-instance in region '{}' references child def '{}' missing from the output library",
                   rb.module_name,
                   std::string{child->get_name()})
              .emit();
          unsupported = true;
        }
      }
    }
    if (auto nm = gu::node_name_of(bb.node); !nm.empty()) {
      nn.attr(hhds::attrs::name).set(std::string{nm});
    }
    if (auto sid = bb.node.attr(hhds::attrs::srcid); sid.has() && sid.get() != 0) {
      nn.attr(hhds::attrs::srcid).set(out_srcmap.import_from(rb.src->source_locator(), sid.get()));
    }
    if (native_comb_logic.contains(bb.node)) {
      nn.attr(livehd::attrs::native_comb_boundary).set({});
    }
    br.node = nn;
    br.out_pin.resize(bb.outs.size());
    br.out_bit.resize(bb.outs.size());
    for (size_t oi = 0; oi < bb.outs.size(); ++oi) {
      const auto& o  = bb.outs[oi];
      auto        dp = nn.create_driver_pin(o.port_id);
      gu::set_bits(dp, o.bits);
      if (o.sign) {
        gu::set_sign(dp);
      }
      br.out_pin[oi] = dp;
      // A native boundary output is split into one ABC PI per bit and then
      // reassembled here.  Width/sign alone are not enough: for a latch the
      // driver pin is the state bus, and its pin_name is the stable RTL name
      // used by cgen/OpenTimer after the mapped region is read back.  Losing it
      // renames a wide latch to the synthetic node name (or, for non-zero
      // ports, <node>_<pid>) and makes the per-bit boundary impossible to map
      // back to the original bus.  Keep the same pin metadata partition does.
      if (auto pn = gu::pin_name_of(o.src_pin); !pn.empty()) {
        gu::set_pin_name(dp, pn);
      }
      if (auto off = o.src_pin.attr(livehd::attrs::pin_offset); off.has()) {
        dp.attr(livehd::attrs::pin_offset).set(off.get());
      }
      br.out_bit[oi].resize(o.bits);
      const auto& demand = bbox_demand[bi][oi];
      if (!o.abc_bits || demand.empty()) {
        continue;
      }
      if (o.bits == 1) {
        br.out_bit[oi][0] = dp;
      } else {
        for (int b : demand) {
          I(b >= 0 && b < o.bits);
          br.out_bit[oi][b] = extract_body_bit(dp, b);
        }
      }
    }
    // Declared outputs with no consumer in the source region still get a driver
    // pin (edge-less), mirroring tolg: readers probe every declared output
    // (cgen create_subs, LEC pairing) and hhds find_pin asserts on a pin that
    // was never created. Width/sign come from the child decl (the source pin
    // is edge-less too, so hhds cannot enumerate it).
    if (bb.op == Ntype_op::Sub) {
      if (auto sio = nn.get_subnode_io()) {
        absl::flat_hash_set<int> made;
        for (const auto& o : bb.outs) {
          made.insert(o.port_id);
        }
        for (const auto& d : sio->get_output_pin_decls()) {
          if (!made.insert(static_cast<int>(d.port_id)).second) {
            continue;
          }
          auto dp = nn.create_driver_pin(d.port_id);
          gu::set_bits(dp, d.bits != 0 ? static_cast<int>(d.bits) : 1);
          // No sign stamp: decl.unsign==false also means "unspecified" (e.g.
          // blackbox cell decls), and an edge-less pin has no reader — leave
          // the attr absent (the unsigned default) rather than plant `signed`.
        }
      }
    }
    if (bb.node.is_loop_subnode()) {
      for (const auto& carry : bb.node.subnode_group().carries()) {
        nn.create_driver_pin(carry.output_port()).connect_sink(nn.create_sink_pin(carry.input_port()));
      }
    }
    for (const auto& [pid, cdrv] : bb.const_ins) {
      gu::create_const(*body, gu::const_of(cdrv)).connect_sink(nn.create_sink_pin(pid));
    }
    // flop boundary: control pins straight from a region input reconnect to the
    // body input pin directly (the clock/reset never enters the AIG).
    for (const auto& [pid, src_drv] : bb.native_ins) {
      if (auto it = region_in_name.find(src_drv); it != region_in_name.end()) {
        body->get_input_pin(it->second).connect_sink(nn.create_sink_pin(pid));
      }
    }
    br.in_bit.resize(bb.ins.size());
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      br.in_bit[ii].assign(bb.ins[ii].bits, hhds::Pin_class{});
    }
  }
  // Reconnect native boundary-to-boundary buses without exploding them into
  // ABC PIs/POs. This is what makes a wide SHL -> packing OR chain remain one
  // named bus on read-back instead of millions of per-bit interface objects.
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> native_boundary_driver;
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    for (size_t oi = 0; oi < bboxes[bi].outs.size(); ++oi) {
      native_boundary_driver.emplace(bboxes[bi].outs[oi].src_pin, bbox_recon[bi].out_pin[oi]);
    }
  }
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    for (const auto& [pid, src_drv] : bboxes[bi].native_ins) {
      if (region_in_name.contains(src_drv)) {
        continue;  // already connected above from the body input pin
      }
      if (auto it = native_boundary_driver.find(src_drv); it != native_boundary_driver.end()) {
        it->second.connect_sink(bbox_recon[bi].node.create_sink_pin(pid));
      }
    }
    for (const auto& [pid, src_drv, bits, sign] : bboxes[bi].fit_native_ins) {
      hhds::Pin_class source;
      if (auto nit = region_in_name.find(src_drv); nit != region_in_name.end()) {
        source = body->get_input_pin(nit->second);
      } else if (auto bit = native_boundary_driver.find(src_drv); bit != native_boundary_driver.end()) {
        source = bit->second;
      }
      if (source.is_invalid()) {
        continue;
      }
      auto fit    = gu::create_get_mask(*body, source, 0, bits);
      auto fitted = fit.create_driver_pin(0);
      gu::set_bits(fitted, bits);
      sign ? gu::set_sign(fitted) : gu::set_unsign(fitted);
      fitted.connect_sink(bbox_recon[bi].node.create_sink_pin(pid));
    }
  }
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    if (!direct_native_output[po]) {
      continue;
    }
    if (auto it = native_boundary_driver.find(rb.outputs[po].src_driver); it != native_boundary_driver.end()) {
      it->second.connect_sink(body->get_output_pin(rb.outputs[po].name));
    }
  }
  if (unsupported) {
    return false;
  }
  trace_stage("readback-boundaries");

  // pass 1a: PI nets -> body input bit drivers (match by creation order — ABC
  // preserves CI/CO order across the flow, more robust than name parsing).
  for (int i = 0; i < static_cast<int>(mapped.sources.size()); ++i) {
    if (i >= static_cast<int>(all_pi_order.size())) {
      continue;
    }
    const auto origin = all_pi_order[i];
    if (origin.kind == Pi_kind::region_input) {
      const auto [pi, b] = pi_order[origin.index];
      set_net_driver(mapped.sources[i], input_bit(pi, b));
    } else {
      const auto [bx, oi, b] = bbox_pi[origin.index];
      set_net_driver(mapped.sources[i], bbox_recon[bx].out_bit[oi][b]);
    }
  }
  trace_stage("readback-pis");

  // Surviving latches (stable vBoxes order) and their source-flop
  // correspondence. Shared by pass 1b's QN-inversion absorption and pass 1c's
  // register read-back.
  //
  // Per-latch source flop, so clock, reset and init are decided PER FLOP (not
  // region-wide): the crossing creates latches in flops order, one per bit, so
  // when the latch count is preserved (the default flow does not retime) latch
  // k maps to its origin flop. A retime-reshaped count falls back to the first
  // flop (clock/reset) and to ABC's own latch init.
  std::vector<const Cell_netlist::Latch*> lat;
  std::vector<const Seq_flop*>            latch_owner;
  std::vector<int>             latch_owner_bit;
  int                          crossed_bits = 0;
  if (map_register && !flops.empty()) {
    for (const auto& l : mapped.latches) {
      lat.push_back(&l);
    }
    for (const auto& f : flops) {
      crossed_bits += f.bits;
    }
    if (static_cast<int>(lat.size()) == crossed_bits) {
      for (const auto& f : flops) {
        for (int b = 0; b < f.bits; ++b) {
          latch_owner.push_back(&f);
          latch_owner_bit.push_back(b);
        }
      }
    }
  }
  // A latch init is a TRUE power-on value only when its source flop has NO
  // reset. With a (synchronous) reset, the init is the reset value — already
  // folded into the D cone — so the flop resets to it and LEC pins reset; a
  // plain DFF cell (power-on X, exactly like the reset flop's own cgen) is then
  // equivalent and the bit maps to a cell. Only a resetless init must keep its
  // native flop.
  //
  // Retime-reshaped fallback (no per-latch owner): a retimed latch has no
  // single source register, so "reset-backed" is a REGION property there. It
  // holds only when every crossed register has a reset -- then no latch can
  // carry a power-on contract and all of them may take cells. With even one
  // resetless-init register in the mix a latch with a concrete init is kept
  // native (with that init): dropping a real power-on value is the silent
  // miscompile, keeping an extra native flop is merely conservative. This is
  // reachable only under a user retime flow (`dretime`) on a region mixing
  // memory-init flops (mem_lower) with reset registers.
  auto owner_has_reset = [&](int k) -> bool {
    if (k < static_cast<int>(latch_owner.size())) {
      return latch_owner[k]->has_reset;
    }
    return std::all_of(flops.begin(), flops.end(), [](const Seq_flop& f) { return f.has_reset; });
  };
  // ABC is allowed to pick a concrete value for a don't-care latch init while
  // optimizing. That choice is an internal optimization witness, NOT a new
  // hardware power-on guarantee. Use the source snapshot to decide WHETHER
  // the bit has an init. Its VALUE must come from the transformed ABC latch:
  // ABC can complement a known-one latch and invert its readers, even without
  // changing the latch count. Restoring the original one then initializes the
  // complemented state incorrectly. An init-less/unknown source still stays
  // unconstrained regardless of ABC's internal witness.
  //
  // Answers the POWER-ON init only: a reset-backed bit's `initial` is its
  // reset value, realized on D, and is dropped here on purpose -- a rebuilt
  // native flop or a DFF cell carrying it as a power-on value would claim a
  // start state the source register (power-on X, then reset) never had.
  auto source_init_bit = [&](int k) -> std::optional<bool> {
    if (owner_has_reset(k)) {
      return std::nullopt;
    }
    if (k < static_cast<int>(latch_owner.size())) {
      const auto* f = latch_owner[k];
      if (!f->has_init || f->init_val.unknown_bit_test(latch_owner_bit[k])) {
        return std::nullopt;
      }
      const auto transformed = lat[k]->init;
      if (transformed == Cell_netlist::Init::zero || transformed == Cell_netlist::Init::one) {
        return transformed == Cell_netlist::Init::one;
      }
      return f->init_val.bit_test(latch_owner_bit[k]);
    }
    const auto v = lat[k]->init;
    if (v == Cell_netlist::Init::zero || v == Cell_netlist::Init::one) {
      return v == Cell_netlist::Init::one;
    }
    return std::nullopt;
  };
  // resetless power-on init: such a bit must keep a native flop so the value
  // survives (a plain DFF cell has no init pin)
  auto needs_native = [&](int k) -> bool { return source_init_bit(k).has_value(); };

  // QN cell (dff->q_inverted): the cell computes QN(t+1) = !D(t) and the
  // read-back wires its QN pin as the register's Q, so the D pin must see ~f.
  // A latch crossed with Seq_flop::d_inverted already holds it (built-in flow).
  // For every OTHER cell-bound latch -- a user or size-tier flow, which may
  // retime, or a reshaped latch count -- `qn_dnet` holds the data-in nets that
  // feed NOTHING but that latch's BI, so their driver can be rewritten to
  // compute ~f without touching any other consumer. Pass 1b absorbs the
  // inversion there: a root inverter is dropped (the net aliases to the
  // inverter's input), any other root gate is swapped for its inverting twin
  // when the library has one (twin_index_; AND2x2 -> NAND2xp33 is a saving,
  // AOI21xp33 -> AO21x1 costs 0.0146, both below INVx1's 0.0437), and what it
  // could not absorb gets one inverter on D in pass 2b. Exact under any flow
  // (it is a local rewrite of the MAPPED netlist, not of the machine ABC
  // optimized). yosys keeps the inversion on the Q side as an INV cell that
  // survives on ~40% of ASAP7 flops; the D side has fanout 1, so a min-size
  // cell always suffices and the register's own drive ladder carries the Q
  // fanout.
  absl::flat_hash_set<uint32_t> qn_dnet;
  absl::flat_hash_set<uint32_t> qn_absorbed;  // subset whose driver now computes ~f
  if (dff.has_value() && dff->q_inverted) {
    for (size_t k = 0; k < lat.size(); ++k) {
      if (needs_native(static_cast<int>(k)) || (k < latch_owner.size() && latch_owner[k]->d_inverted)) {
        continue;
      }
      const auto dnet = lat[k]->d;
      if (dnet != Cell_netlist::kNone && readers[dnet] == 1) {
        qn_dnet.insert(dnet);
      }
    }
  }
  const auto   mio_inv  = lib.inverter();
  const double inv_area = mio_inv ? lib.type(*mio_inv).area : 0.0;
  int qn_dropped = 0;  // root inverters removed
  int qn_swapped = 0;  // root gates replaced by their inverting twin

  // pass 1b: each mapped gate -> a Sub; map its output net -> Sub output pin.
  // A decoupling buffer -- single-input identity gate whose output feeds only
  // COs -- is not a gate at all (see is_identity_gate): alias its output net to
  // its input net and mint nothing, so the CO reads the buffer's own driver
  // (a PI bit, a latch Q, a blackbox output, or the gate that `&put -o` would
  // otherwise have duplicated). No cell_desc call either: a bypassed cell must
  // not leave an unused cell decl in the output library.
  std::vector<std::pair<hhds::Node_class, uint32_t>> gates;  // (Sub, cell)
  std::vector<uint32_t>                              cell_type(mapped.cells.size());
  double                                             emitted_area = 0.0;
  for (uint32_t c = 0; c < mapped.cells.size(); ++c) {
    const auto& cell = mapped.cells[c];
    auto        g    = cell.type;
    const auto  onet = cell.output;
    if (cell.fanins.size() == 1 && lib.is_identity(g)) {
      if (readers[onet] > 0 && cell_readers[onet] == 0) {  // its output feeds only outputs and latch inputs
        // output net -> input net; mapped_node2sub stays invalid for this cell
        // (the srcmap attribution walk below guards on it)
        net_alias[onet] = static_cast<int32_t>(cell.fanins.front());
        ++bypassed_bufs;
        continue;
      }
    }
    if (qn_dnet.contains(onet)) {
      // The D-cone root of a QN-cell register (see qn_dnet): absorb the
      // inversion here when that is free or cheaper than an inverter.
      if (cell.fanins.size() == 1 && lib.is_inverter(g)) {
        net_alias[onet] = static_cast<int32_t>(cell.fanins.front());
        qn_absorbed.insert(onet);
        ++qn_dropped;
        continue;
      }
      if (const auto twin = lib.inverting_twin(g); twin && lib.type(*twin).area - lib.type(g).area < inv_area) {
        // The twin computes ~g over the same pins in the same order, so pass 2
        // wires it exactly like g.
        g = *twin;
        qn_absorbed.insert(onet);
        ++qn_swapped;
      }
    }
    cell_type[c]     = g;
    const auto& desc = cell_desc(g);
    auto        sub  = gu::create_typed_node(*body, Ntype_op::Sub);
    sub.set_subnode(desc.io);
    sub.attr(hhds::attrs::name).set(std::format("g{}_{}", c, desc.name));
    auto outpin = sub.create_driver_pin(desc.output_name);
    gu::set_bits(outpin, 1);
    gu::set_unsign(outpin);
    set_net_driver(onet, outpin);
    mapped_node2sub[c] = sub;
    gates.emplace_back(sub, c);
    emitted_area += lib.type(g).area;
  }
  // QoR accounting. `gates`/`area` were read off the mapped LOGIC network
  // above, which still carries the decoupling buffers -- and Abc_NtkToNetlist
  // can add a few more of its own (a `buffer -N` leaf left feeding several
  // COs), which that count never saw. abc.json `total.area` is what lhdtrack
  // scores as lhd_area and the incremental cache stores this same row, so
  // both must describe the cells actually minted: br_demux_onehot reported
  // 528 gates / 31.26 um^2 for a netlist of 16 cells. Mio's gate area is the
  // Liberty area (the GENLIB is derived from the parsed SC_Lib), i.e. the
  // same quantity the SCL timer summed. `delay` is deliberately left alone:
  // it still includes one buffer on a feed-through path (pessimistic by a
  // buffer delay only when such a path is the region's critical one).
  // The QN twin swap changes a cell's area without changing the count, and a
  // dropped root inverter is a cell the ABC network still holds: both make the
  // minted netlist the only truthful source too.
  if (bypassed_bufs != 0 || qn_dropped != 0 || qn_swapped != 0 || static_cast<int>(gates.size()) != counts.gates) {
    counts.bypassed = bypassed_bufs;
    counts.gates    = static_cast<int>(gates.size());
    counts.area     = emitted_area;
  }
  trace_stage("readback-gates");

  // pass 1c (seq): each ABC latch -> a native LGraph Flop. Flops are never
  // mapped to library DFFs (locked design decision): the latch only carried the
  // register across ABC so it could optimize/retime the surrounding logic. The
  // latch output net (Q) is mapped into net2drv so the comb fanins/outputs read
  // the flop's Q; the latch input net (D) is recorded and wired in pass 2b (its
  // driving gate is created in pass 2). Reassembly: when the latch count is
  // preserved (the default flow does not retime) each source register is
  // rebuilt as ONE multi-bit flop with its ORIGINAL name — including any bit
  // the DFF-cell path must keep native for a resetless power-on init. A
  // retime-reshaped count falls back to a single-root collapse (one register
  // name in the region) or per-latch deterministically-named 1-bit flops.
  struct Recon_flop {
    hhds::Node_class        node;
    int                     bits = 0;
    std::vector<uint32_t>   dnet;  // per-bit latch data-in net (wired in pass 2b)
  };
  std::vector<Recon_flop> recon;
  // register=true DFF-cell mapping: one library DFF Sub per surviving latch (its
  // din is wired in pass 2b, like a native flop's). `dff` is set only when the
  // Liberty had a plain posedge D-flop; otherwise the native path below runs.
  struct Recon_dff {
    hhds::Node_class         sub;
    uint32_t                 dnet;
    const liberty::Dff_cell* cell;   // the ladder rung this Sub instantiates (pass 2b wires its d_pin)
    bool                     d_inv;  // QN cell whose D-cone root could not absorb the inversion: add an inverter on D
  };
  std::vector<Recon_dff> dff_recon;
  int                    clock_inv_cells = 0;
  bool                   init_dropped    = false;  // a concrete power-on init lost to a plain DFF cell
  if (map_register && !flops.empty()) {
    // src external driver -> body driver pin (region input port, or recreated const)
    absl::flat_hash_map<hhds::Pin_class, std::string> src_in_to_name;
    for (const auto& port : rb.inputs) {
      src_in_to_name[port.src_driver] = port.name;
    }
    auto body_pin_for_src = [&](const hhds::Pin_class& d) -> hhds::Pin_class {
      if (d.is_invalid()) {
        return {};
      }
      if (auto it = src_in_to_name.find(d); it != src_in_to_name.end()) {
        return body->get_input_pin(it->second);
      }
      if (d.is_const()) {
        return gu::create_const(*body, gu::const_of(d));
      }
      return {};
    };
    auto region_clk = body_pin_for_src(flops.front().clk_drv);

    // surviving latches (`lat`, filled before pass 1b) in stable vBoxes order
    int m = static_cast<int>(lat.size());

    auto owner_clk = [&](int k) -> hhds::Pin_class {
      return k < static_cast<int>(latch_owner.size()) ? body_pin_for_src(latch_owner[k]->clk_drv) : region_clk;
    };
    auto owner_negedge
        = [&](int k) { return k < static_cast<int>(latch_owner.size()) ? latch_owner[k]->neg_clock : flops.front().neg_clock; };
    absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> inverted_clocks;
    auto                                                  mapped_owner_clk = [&](int k) -> hhds::Pin_class {
      auto clk = owner_clk(k);
      if (clk.is_invalid() || !owner_negedge(k)) {
        return clk;
      }
      if (auto it = inverted_clocks.find(clk); it != inverted_clocks.end()) {
        return it->second;
      }
      // The selected Liberty cell is posedge-only. Share one physical clock
      // inverter across the negative-edge register bits on this clock.
      I(mio_inv.has_value());
      const auto& desc = cell_desc(*mio_inv);
      auto        inv  = gu::create_typed_node(*body, Ntype_op::Sub);
      inv.set_subnode(desc.io);
      inv.attr(hhds::attrs::name).set(std::format("abc_clock_inv_{}", clock_inv_cells));
      clk.connect_sink(inv.create_sink_pin(desc.input_names.front()));
      auto inverted = inv.create_driver_pin(desc.output_name);
      gu::set_ubits(inverted, 1);
      inverted_clocks.emplace(clk, inverted);
      ++clock_inv_cells;
      return inverted;
    };

    // Original-name reconstruction: with the latch count preserved, latches
    // [start, start+bits) are flops[i]'s bits in crossing order, so a native
    // read-back can rebuild each source register as ONE multi-bit flop under
    // its ORIGINAL (hierarchical) name. That keeps the flop-name
    // correspondence across synthesis — the LEC collapses same-name state
    // pairs instead of solving thousands of anonymous 1-bit registers, which
    // is load-bearing for the whole-design flatten (one region holds EVERY
    // register of the design).
    struct Span {
      const Seq_flop* f;
      int             start;
    };
    std::vector<Span> spans;
    if (m == crossed_bits) {
      int s = 0;
      for (const auto& f : flops) {
        spans.push_back({&f, s});
        s += f.bits;
      }
    }
    // Nothing enforces wire-name uniqueness across a region's registers; two
    // same-named rebuilt flops would cgen as two `reg` declarations of one name.
    //
    // BUT THE SUFFIX IS A LAST RESORT, NOT A FIX. It renames the IMPL side only,
    // so a duplicate that the reference still spells one way becomes an
    // asymmetric pair: the post-synthesis LEC corresponds state BY NAME, and
    // every name-keyed map between here and cgen has to pick one of the two.
    // Measured (bedrock br_fifo_shared_*): a genvar-loop
    // instantiation whose instances shared a name flattened to two registers
    // under one name, and the mapped netlist then computed
    // `{credit_initial[5:3], credit_initial[5:3]}` where the RTL gives
    // `credit_initial` — 56 of 64 values wrong, confirmed by an independent
    // iverilog simulation against the PDK's own cell models. So say it out loud
    // rather than quietly renaming: a collision here means something upstream
    // minted two distinct registers under one hierarchical name.
    absl::flat_hash_map<std::string, int> name_used;
    int                                   dup_names = 0;
    std::string                           dup_first;
    auto                                  unique_flop_name = [&](const std::string& base) {
      int& n = name_used[base];
      ++n;
      if (n > 1) {
        if (dup_names++ == 0) {
          dup_first = base;
        }
        return std::format("{}__dup{}", base, n - 1);
      }
      return base;
    };
    // Rebuild one native flop covering the latches `idx` (bit 0 first): Q bits
    // feed the mapped logic via net2drv, din is Concat-reassembled in pass
    // 2b, and a POWER-ON init is recovered from the transformed latch values
    // only where the source promised one. A synchronous reset is
    // already in the D cone, so the rebuilt flop carries neither a reset_pin
    // nor the reset value as an init -- the plain `always @(posedge)` with the
    // mux on D is the same machine (proven by cvc5/lgyosys in lhd_abc_seq_test).
    auto build_native_flop = [&](const std::string& name, const hhds::Pin_class& clk, const std::vector<int>& idx) {
      int  k = static_cast<int>(idx.size());
      auto F = gu::create_typed_node(*body, Ntype_op::Flop);
      F.attr(hhds::attrs::name).set(name);
      auto Fq = F.create_driver_pin(0);
      gu::set_bits(Fq, k);
      gu::set_unsign(Fq);
      if (!clk.is_invalid()) {
        clk.connect_sink(gu::setup_sink_by_name(F, "clock_pin"));
      }
      if (!idx.empty() && owner_negedge(idx.front())) {
        gu::create_const(*body, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(F, "posclk"));
      }
      // Build MSB->LSB so widths past 64 bits stay exact. Uninitialized bits
      // remain unknown even when grouped with initialized bits in one register.
      bool        any_init = false;
      std::string init_bits(k, '?');  // index 0 = MSB (bit k-1)
      for (int b = 0; b < k; ++b) {
        if (auto v = source_init_bit(idx[b]); v.has_value()) {
          any_init             = true;
          init_bits[k - 1 - b] = *v ? '1' : '0';
        }
      }
      if (any_init) {
        gu::create_const(*body, *Dlop::from_binary(init_bits, /*unsigned_result=*/true))
            .connect_sink(gu::setup_sink_by_name(F, "initial"));
      }
      Recon_flop rf;
      rf.node = F;
      rf.bits = k;
      for (int b = 0; b < k; ++b) {
        const auto*     L    = lat[idx[b]];
        const auto      qnet = L->q;
        const auto      dnet = L->d;
        hhds::Pin_class qd;
        if (k == 1) {
          qd = Fq;
        } else {
          qd = extract_body_bit(Fq, b);
        }
        set_net_driver(qnet, qd);
        rf.dnet.push_back(dnet);
      }
      recon.push_back(std::move(rf));
    };

    if (dff.has_value()) {
      // --- register=true: map each surviving latch to a library DFF-cell Sub ---
      // (D/CLK/Q). Q feeds the comb read-back via net2drv; D is wired in pass 2b
      // from the latch's data-in net; CLK comes straight from the region clock
      // (never through the AIG). A plain posedge D-flop cell has NO init pin, so a
      // latch carrying a concrete power-on init CANNOT be represented by the cell
      // without changing power-on behavior — such a bit stays a native flop (the
      // netlist stays equivalent); init-less and synchronous-reset bits (their
      // init is the reset value on D) become DFF cells under the register name.
      // The AIG-side QN encoding (Seq_flop::d_inverted) is exact only while
      // latch k is still the latch flop-bit k crossed as: with the count
      // reshaped there is no telling which BI nets carry ~f, and reading a QN
      // pin as Q on the wrong one is a silent miscompile. Unreachable (only the
      // built-in flow sets d_inverted, and it never retimes); a guard, not a
      // path -- every other flow takes the read-back absorption.
      if (spans.empty() && std::any_of(flops.begin(), flops.end(), [](const Seq_flop& f) { return f.d_inverted; })) {
        livehd::diag::err("pass.abc", "abc-readback", "internal")
            .msg(
                "pass.abc region '{}': the latch set was reshaped ({} latches for {} register bits) under the QN-only DFF "
                "cell '{}' -- the D-side inversion cannot be attributed; the built-in flow never retimes",
                rb.module_name,
                m,
                crossed_bits,
                dff->name)
            .fatal();
        return false;
      }
      // One IO decl per ladder rung actually used (create_dff_io is find-or-
      // create, so an unused rung leaves no stray decl in the output library).
      std::vector<std::shared_ptr<hhds::GraphIO>> rung_io(dff_ladder.size());
      auto                                        rung_for_fanout = [&](int fanout) -> size_t {
        // <=8 loads: x1 (the fastest rung there per the NLDM tables, and 97%
        // of registers); <=16: x2; above: x3 -- clamped to the ladder the
        // library has (test.lib / sky130 have one rung; ASAP7 three).
        const size_t want = fanout <= 8 ? 0 : (fanout <= 16 ? 1 : 2);
        return std::min(want, dff_ladder.size() - 1);
      };
      auto rung_io_for = [&](size_t r) {
        if (!rung_io[r]) {
          rung_io[r] = liberty::create_dff_io(*outlib_, dff_ladder[r]);
        }
        return rung_io[r];
      };
      // `owner` names the SOURCE register bit this latch came from (empty when
      // the latch count was reshaped and no correspondence survives). A mapped
      // DFF cell otherwise lands as `g<abcId>_<cell>`, which drops the register
      // name that the post-synthesis LEC's tier-1 state correspondence pairs on
      // — `id_q` then has no counterpart in the netlist and the def can only come
      // back inconclusive (every //bench:*_synth_lec_* target).
      // Scalar-replacement provenance for a bit-blasted register. `pass.semdiff`
      // reassembles the group from these and pairs it with the ref's single wide
      // flop; without them a mapped `q_0..q_7` faces a ref `q` that tier-1 cannot
      // match one-to-many, the whole register lands in `tier-2 unpaired state`,
      // and the flop-cut inductive miter then cuts only the flops that DID pair
      // and returns PROVEN off an obligation set covering a fraction of the
      // state (br_arb_weighted_rr: 18 cuts against 17 ref / 96 impl flops left
      // unpaired, PROVEN while lgyosys held a real 7-cycle counterexample).
      // Every attribute below is required by semdiff's valid_aggregate_group;
      // a register whose bits do not ALL become cells simply fails its ordinal
      // cover there and stays unpaired, exactly as before.
      struct Blast_lane {
        const std::string* root   = nullptr;
        int32_t            lane   = 0;
        int32_t            extent = 0;
        int32_t            source = 0;
      };
      auto map_dff_cell = [&](int k, const std::string& owner = {}, const Blast_lane* lane = nullptr) {
        const auto* L    = lat[k];
        const auto  qnet = L->q;
        const auto  dnet = L->d;
        const auto  r    = rung_for_fanout(static_cast<int>(readers[qnet]));
        const auto& cell = dff_ladder[r];
        auto        sub  = gu::create_typed_node(*body, Ntype_op::Sub);
        sub.set_subnode(rung_io_for(r));
        sub.attr(hhds::attrs::name).set(owner.empty() ? std::format("g{}_{}", k, cell.name) : unique_flop_name(owner));
        // The cell's output pin IS the register's Q for every consumer -- for
        // a QN cell because the D side carries ~f: crossed that way
        // (Seq_flop::d_inverted), absorbed in pass 1b, or an inverter added in
        // pass 2b (`d_inv`).
        auto q = sub.create_driver_pin(cell.q_pin);
        gu::set_bits(q, 1);
        gu::set_unsign(q);
        set_net_driver(qnet, q);
        if (auto lclk = mapped_owner_clk(k); !lclk.is_invalid()) {
          lclk.connect_sink(sub.create_sink_pin(cell.clk_pin));
        }
        if (lane != nullptr) {
          sub.attr(livehd::attrs::aggregate_origin).set(*lane->root);
          sub.attr(livehd::attrs::aggregate_extent).set(lane->extent);
          sub.attr(livehd::attrs::aggregate_lane_ordinal).set(lane->lane);
          sub.attr(livehd::attrs::aggregate_source_index).set(lane->source);
          // One bit per lane, packed low-to-high, so the reassembled ranges are
          // [b, b+1) and cover [0, bits) with no gap.
          sub.attr(livehd::attrs::aggregate_bit_offset).set(lane->lane);
          sub.attr(livehd::attrs::aggregate_bit_width).set(1);
        }
        const bool crossed_inverted = k < static_cast<int>(latch_owner.size()) && latch_owner[k]->d_inverted;
        dff_recon.push_back({sub, dnet, &cell, cell.q_inverted && !crossed_inverted && !qn_absorbed.contains(dnet)});
      };
      auto native_single = [&](int k) {
        init_dropped = true;
        build_native_flop(unique_flop_name(std::format("{}__rinit{}", rb.module_name, k)), owner_clk(k), {k});
      };
      // Distinguishes two registers that were blasted in the same region; the
      // matcher only requires every lane of ONE group to agree on it.
      int32_t blast_source_index = 0;
      if (!spans.empty()) {
        for (const auto& sp : spans) {
          ++blast_source_index;
          bool native = false;
          bool cell   = false;
          for (int b = 0; b < sp.f->bits; ++b) {
            (needs_native(sp.start + b) ? native : cell) = true;
          }
          if (native && !cell) {
            // the whole register keeps its power-on init: rebuild it as one
            // native flop under its original name
            init_dropped = true;
            std::vector<int> idx(sp.f->bits);
            std::iota(idx.begin(), idx.end(), sp.start);
            build_native_flop(unique_flop_name(sp.f->root), body_pin_for_src(sp.f->clk_drv), idx);
          } else {
            // init-less or sync-reset register -> per-bit DFF cells; a mixed
            // register (should not occur: init is stamped per register)
            // degrades to per-bit handling, never to a dropped init
            for (int b = 0; b < sp.f->bits; ++b) {
              int k = sp.start + b;
              // Per-bit name under the source register: a 1-bit register keeps
              // its plain name, a wider one indexes (`id_q[0]`) — the spelling a
              // hand-flattened design uses, which canon_flop_name already folds.
              if (needs_native(k)) {
                native_single(k);
              } else if (sp.f->bits == 1) {
                map_dff_cell(k, sp.f->root);  // pairs by name; nothing was blasted
              } else {
                const Blast_lane lane{&sp.f->root, b, sp.f->bits, blast_source_index};
                map_dff_cell(k, std::format("{}_{}", sp.f->root, b), &lane);
              }
            }
          }
        }
      } else {
        // retime-reshaped latch count: no per-register correspondence survives
        for (int k = 0; k < m; ++k) {
          needs_native(k) ? native_single(k) : map_dff_cell(k);
        }
      }
    } else {
      if (!spans.empty()) {
        // one flop per source register under its ORIGINAL name — multi-root
        // regions (the whole-design flatten) keep the name correspondence
        for (const auto& sp : spans) {
          std::vector<int> idx(sp.f->bits);
          std::iota(idx.begin(), idx.end(), sp.start);
          build_native_flop(unique_flop_name(sp.f->root), body_pin_for_src(sp.f->clk_drv), idx);
        }
      } else {
        absl::flat_hash_set<std::string> roots;
        for (const auto& f : flops) {
          roots.insert(f.root);
        }
        if (roots.size() == 1) {
          // retime-reshaped single-root region (one register name): collapse
          // every surviving latch into one named flop
          std::vector<int> idx(m);
          std::iota(idx.begin(), idx.end(), 0);
          build_native_flop(unique_flop_name(flops.front().root), region_clk, idx);
        } else {
          // retime-reshaped multi-register region: per-latch 1-bit flops --
          // always LEC-correct regardless of how retiming reshaped or reordered
          // the latches (each latch is faithfully its own 1-bit register; no
          // cross-register order assumption), clocked from its OWN source flop
          for (int k = 0; k < m; ++k) {
            build_native_flop(unique_flop_name(std::format("{}__r{}", rb.module_name, k)), owner_clk(k), {k});
          }
        }
      }
    }  // else (native-flop read-back)
    if (dup_names != 0) {
      livehd::diag::warn("pass.abc", "duplicate-register-name", "internal")
          .msg(
              "pass.abc region '{}': {} register(s) share a name with another register in the same region (first: '{}'); "
              "the read-back had to suffix them '__dup<N>' on the IMPLEMENTATION side only. Two distinct registers under "
              "one hierarchical name is an upstream naming defect: it breaks the post-synthesis LEC's name-based state "
              "correspondence and can alias the registers outright",
              rb.module_name,
              dup_names,
              dup_first)
          .emit();
    }
  }
  if (init_dropped) {
    livehd::diag::warn("pass.abc", "dff-init-kept-native", "unsupported")
        .msg(
            "pass.abc region '{}': register(s) carry a power-on init value that the plain DFF cell '{}' has no pin for; "
            "they were kept as native flops (still correct) while init-less registers mapped to DFF cells",
            rb.module_name,
            dff->name)
        .emit();
  }
  trace_stage("readback-cells");

  // pass 2: wire each Sub's fanins (fanin k <-> Liberty pin k)
  auto const0_pin = [&]() { return gu::create_const(*body, *Dlop::create_integer(0)); };
  for (auto& [sub, c] : gates) {
    const auto& pins   = cell_desc(cell_type[c]).input_names;
    const auto& fanins = mapped.cells[c].fanins;
    for (int k = 0; k < static_cast<int>(fanins.size()); ++k) {
      const auto fin = fanins[static_cast<size_t>(k)];
      if (k >= static_cast<int>(pins.size())) {
        break;
      }
      auto spin   = sub.create_sink_pin(pins[k]);
      // No set_bits on this cell-input SINK: `bits` is a driver-pin property (the
      // 1-bit width lives on the gate's GraphIO port + the 1-bit driver net).
      auto driver = get_net_driver(fin);
      if (!driver.is_invalid()) {
        driver.connect_sink(spin);
      } else {
        const0_pin().connect_sink(spin);  // structurally complete; should not occur
      }
    }
  }

  // Reassemble a vector of LSB-first one-bit drivers as one canonical Concat
  // (whose lanes are MSB-first). A Set_mask chain creates W full-width
  // intermediate values; downstream Verilog tools then expand W*W bits.
  hhds::Pin_class concat_width_one;
  auto            assemble_bits = [&](std::vector<hhds::Pin_class>& dbit, bool sign, hhds::SourceId sid = hhds::SourceId_invalid) {
    I(!dbit.empty());
    const auto      w = static_cast<int>(dbit.size());
    hhds::Pin_class out;
    if (w == 1) {
      out = dbit.front();
    } else {
      if (concat_width_one.is_invalid()) {
        concat_width_one = gu::create_const(*body, *Dlop::create_integer(1));
      }
      auto concat = gu::create_typed_node(*body, Ntype_op::Concat);
      if (sid != hhds::SourceId_invalid) {
        concat.attr(hhds::attrs::srcid).set(sid);
      }
      // Port IDs still encode MSB-first lanes, but create them in descending
      // order. HHDS's per-node pin list is sorted; ascending creation rescans
      // the growing list for every pin and makes a W-bit Concat O(W^2).
      for (size_t b = 0; b < dbit.size(); ++b) {
        auto data_pid = static_cast<hhds::Port_id>(2 * (dbit.size() - 1 - b));
        concat_width_one.connect_sink(livehd::graph_util::setup_sink_pid(concat, data_pid + 1));
        dbit[b].connect_sink(concat.create_sink_pin(data_pid));
      }
      out = concat.create_driver_pin(0);
      gu::set_bits(out, w);
      // A Concat driver is UNSIGNED by construction and every consumer relies
      // on it: each lane masks into its own window, so the value is in
      // [0, 2^sum(w)).
      gu::set_unsign(out);
    }
    if (!sign) {
      return out;
    }
    // Preserve the operand's signedness on the reassembled value. For a Div
    // the LEC fit()s each operand by its sign (SDIV/UDIV sign-extend vs
    // zero-extend), so a signed operand narrower than the divider's width must
    // stay signed or ref/impl diverge.
    auto sx = gu::create_typed_node(*body, Ntype_op::Sext);
    if (sid != hhds::SourceId_invalid) {
      sx.attr(hhds::attrs::srcid).set(sid);
    }
    out.connect_sink(gu::setup_sink_by_name(sx, "a"));
    gu::create_const(*body, *Dlop::create_integer(w)).connect_sink(gu::setup_sink_by_name(sx, "b"));
    auto sout = sx.create_driver_pin(0);
    gu::set_bits(sout, w);
    gu::set_sign(sout);
    return sout;
  };

  // pass 2b (seq): wire each reconstructed flop's din from the body driver that
  // feeds its latch D net (now resolvable: PIs in 1a, gates in 1b/2).
  for (auto& rf : recon) {
    int                          k = rf.bits;
    std::vector<hhds::Pin_class> dbits(k);
    for (int b = 0; b < k; ++b) {
      auto driver = get_net_driver(rf.dnet[b]);
      dbits[b]    = !driver.is_invalid() ? driver : const0_pin();
    }
    assemble_bits(dbits, false).connect_sink(gu::setup_sink_by_name(rf.node, "din"));
  }

  // pass 2b (register=true): wire each mapped DFF Sub's D pin from its latch's
  // data-in net (each DFF cell is 1-bit, so no Set_mask reassembly is needed).
  // A QN cell whose D-cone root could not absorb the inversion (pass 1b: the
  // root feeds other logic too, is a port / another register's Q, or has no
  // inverting twin) gets one min-size inverter here, on the D side: fanout 1,
  // so INVx1 always suffices, and the register's own drive ladder carries the
  // Q fanout. Still 0.2916 + 0.0437 against DFFHQx4's 0.3645.
  int    qn_inv_cells = 0;
  double qn_inv_area  = 0;
  for (auto& rd : dff_recon) {
    auto d = get_net_driver(rd.dnet);
    if (d.is_invalid()) {
      d = const0_pin();
    }
    if (rd.d_inv) {
      I(mio_inv.has_value());  // a backend refuses a library without an inverter
      const auto& desc = cell_desc(*mio_inv);
      auto        inv  = gu::create_typed_node(*body, Ntype_op::Sub);
      inv.set_subnode(desc.io);
      inv.attr(hhds::attrs::name).set(std::format("{}__dinv", std::string{rd.sub.attr(hhds::attrs::name).get_or("")}));
      d.connect_sink(inv.create_sink_pin(desc.input_names.front()));
      d = inv.create_driver_pin(desc.output_name);
      gu::set_bits(d, 1);
      gu::set_unsign(d);
      ++qn_inv_cells;
      qn_inv_area += inv_area;
    }
    d.connect_sink(rd.sub.create_sink_pin(rd.cell->d_pin));
  }
  // Those inverters are minted standard cells the mapped LOGIC network never
  // saw: count them where the identity-buffer bypass corrected the same row,
  // so abc.json `gates`/`area` (what lhdtrack scores as lhd_area, and what the
  // incremental cache persists) describe the netlist that was actually written.
  if (qn_inv_cells != 0 || clock_inv_cells != 0) {
    counts.gates += qn_inv_cells + clock_inv_cells;
    counts.area  += qn_inv_area + clock_inv_cells * inv_area;
  }
  trace_stage("readback-fanins");

  // pass 3: POs -> reassemble multi-bit outputs (one Concat). Match by
  // creation order (po_order), consistent with the PI readback.
  std::vector<std::vector<hhds::Pin_class>> out_bits(rb.outputs.size());
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    int w = rb.outputs[po].bits == 0 ? 1 : rb.outputs[po].bits;
    out_bits[po].resize(w);
  }
  if (mapped.outputs.size() != po_order.size() + bbox_po.size() + (has_dummy_po ? 1 : 0)) {
    livehd::diag::warn("pass.abc", "abc-readback", "internal")
        .msg("pass.abc: region '{}': mapped PO count {} != created {} (region {} + bbox {}) — read-back misaligned",
             rb.module_name,
             mapped.outputs.size(),
             po_order.size() + bbox_po.size(),
             po_order.size(),
             bbox_po.size())
        .emit();
  }
  for (int i = 0; i < static_cast<int>(mapped.outputs.size()); ++i) {
    if (has_dummy_po && i >= static_cast<int>(po_order.size() + bbox_po.size())) {
      continue;  // the all-native sentinel PO: no read-back target, and its net
                 // has no driver entry (a lookup here would warn and leak a const)
    }
    auto drv = get_net_driver(mapped.outputs[static_cast<size_t>(i)]);
    if (drv.is_invalid()) {
      livehd::diag::warn("pass.abc", "abc-readback", "internal")
          .msg("pass.abc: region '{}': PO {} ('{}') fanin net has no read-back driver — emitted const0",
               rb.module_name,
               i,
               i < static_cast<int>(outputs.size()) ? outputs[static_cast<size_t>(i)].name : std::string{})
          .emit();
    }
    if (drv.is_invalid()) {
      drv = const0_pin();
    }
    if (i < static_cast<int>(po_order.size())) {
      out_bits[po_order[i].first][po_order[i].second] = drv;
    } else if (int j = i - static_cast<int>(po_order.size()); j < static_cast<int>(bbox_po.size())) {
      for (const auto& target : bbox_po[static_cast<size_t>(j)]) {
        bbox_recon[target.bx].in_bit[target.input][target.bit] = drv;  // wired to the recon node sink below
      }
    }
  }

  // pass 3b: wire each rebuilt blackbox node's combinational inputs from the
  // captured PO drivers (multi-bit reassembled with one Concat).
  // The same source can feed more than one native boundary through different
  // explicit width windows (two Concat lanes are the common case).  Width and
  // signedness are part of the cast, so caching only by source can reconnect a
  // previously assembled 6-bit value to a later 5-bit lane and leave an
  // invalid over-wide Concat in the mapped graph.
  using Bbox_input_key = std::tuple<hhds::Pin_class, int, bool>;
  absl::flat_hash_map<Bbox_input_key, hhds::Pin_class> reassembled_bbox_input;
  for (size_t bx = 0; bx < bboxes.size(); ++bx) {
    auto& bb = bboxes[bx];
    auto& br = bbox_recon[bx];
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      int                  w    = bb.ins[ii].bits;
      auto                 sink = br.node.create_sink_pin(bb.ins[ii].port_id);
      auto&                dbit = br.in_bit[ii];
      const Bbox_input_key cache_key{bb.ins[ii].drv, w, bb.ins[ii].sign};
      if (auto it = reassembled_bbox_input.find(cache_key); it != reassembled_bbox_input.end()) {
        it->second.connect_sink(sink);
        continue;
      }
      for (int b = 0; b < w; ++b) {
        if (dbit[b].is_invalid()) {
          dbit[b] = const0_pin();
        }
      }
      if (w == 1 && !bb.ins[ii].sign) {
        dbit[0].connect_sink(sink);  // unsigned 1-bit: drive the sink directly
        reassembled_bbox_input.emplace(cache_key, dbit[0]);
        continue;
      }
      auto acc = assemble_bits(dbit, bb.ins[ii].sign);
      acc.connect_sink(sink);
      reassembled_bbox_input.emplace(cache_key, acc);
    }
  }

  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    if (direct_native_output[po]) {
      continue;
    }
    const auto& port = rb.outputs[po];
    int         w    = port.bits == 0 ? 1 : port.bits;
    auto        opin = body->get_output_pin(port.name);
    auto&       bits = out_bits[po];
    for (int b = 0; b < w; ++b) {
      if (bits[b].is_invalid()) {
        bits[b] = const0_pin();
      }
    }
    if (w == 1) {
      bits[0].connect_sink(opin);
      continue;
    }
    auto acc = assemble_bits(bits, port.sign, po_srcid[po]);
    acc.connect_sink(opin);
  }
  trace_stage("readback-outputs");

  // --- source-map carry-through: stamp each mapped gate with the srcid of the
  // original output cone it feeds. Walk output roots in ascending order with
  // ONE global visited set, so each mapped gate is attributed to the first
  // (lowest-index) output that reaches it. A shared gate therefore gets a stable
  // primary anchor rather than a combined source set. ABC's optimization is
  // lossy either way, and this keeps attribution linear in gates+edges instead
  // of re-walking the whole cone once per boundary output (minutes on Rob). ---
  {
    // roots = the mapped gate driving each PO bit, grouped by output port
    // The cell driving a signal, or kNone for a source or a latch.
    const auto driving_cell = [&](uint32_t sig) {
      return sig != Cell_netlist::kNone && mapped.signals[sig].kind == Cell_netlist::Kind::cell ? mapped.signals[sig].index
                                                                                               : Cell_netlist::kNone;
    };
    std::vector<std::vector<uint32_t>> port_roots(rb.outputs.size());
    std::vector<hhds::SourceId>        cone_srcid(po_srcid);
    for (int i = 0; i < static_cast<int>(mapped.outputs.size()); ++i) {
      if (i >= static_cast<int>(po_order.size())) {
        continue;
      }
      if (const auto drv = driving_cell(mapped.outputs[static_cast<size_t>(i)]); drv != Cell_netlist::kNone) {
        port_roots[po_order[i].first].push_back(drv);
      }
    }
    // Latch-input pseudo-outputs (seq): a gate feeding a register din gets the
    // ORIGINAL register's srcid, so a post-map critical path ending at a flop
    // still points at source. Latch k maps to its source flop by creation
    // order — valid only when the latch count survived the flow unchanged
    // (the same assumption the 1:1 flop read-back makes); a retime-reshaped
    // region keeps PO-cone attribution only.
    if (map_register && !flops.empty()) {
      int total_bits = 0;
      for (const auto& f : flops) {
        total_bits += f.bits;
      }
      const auto& lat_objs = mapped.latches;
      if (static_cast<int>(lat_objs.size()) == total_bits) {
        std::vector<const Seq_flop*> owner;
        owner.reserve(static_cast<size_t>(total_bits));
        for (const auto& f : flops) {
          for (int b = 0; b < f.bits; ++b) {
            owner.push_back(&f);
          }
        }
        for (size_t k = 0; k < lat_objs.size(); ++k) {
          auto sid_attr = owner[k]->node.attr(hhds::attrs::srcid);
          if (!sid_attr.has() || sid_attr.get() == 0) {
            continue;
          }
          const auto drv = driving_cell(lat_objs[k].d);
          if (drv == Cell_netlist::kNone) {
            continue;
          }
          port_roots.push_back({drv});
          // Into the library srcmap, not the body locator (see po_srcid above).
          cone_srcid.push_back(body->get_io()->get_library()->source_map().import_from(rb.src->source_locator(), sid_attr.get()));
        }
      }
    }
    // Per output, claim every not-yet-attributed gate in its fanin cone.
    std::vector<uint8_t> attributed(mapped.cells.size());
    for (size_t po = 0; po < port_roots.size(); ++po) {
      if (cone_srcid[po] == hhds::SourceId_invalid) {
        continue;  // no provenance to attribute this cone with
      }
      std::vector<uint32_t> stack = port_roots[po];
      while (!stack.empty()) {
        const auto g = stack.back();
        stack.pop_back();
        // `attributed` is both the global claim set and this walk's visited set:
        // a gate claimed by an earlier output already had its whole fanin cone
        // claimed by that same walk, so stopping here loses nothing.
        const auto gid = static_cast<size_t>(g);
        if (gid >= attributed.size() || attributed[gid]) {
          continue;
        }
        attributed[gid] = 1;
        if (!mapped_node2sub[gid].is_invalid()) {
          mapped_node2sub[gid].attr(hhds::attrs::srcid).set(cone_srcid[po]);
        }
        for (const auto fin : mapped.cells[g].fanins) {
          if (const auto d = driving_cell(fin); d != Cell_netlist::kNone) {
            const auto did = static_cast<size_t>(d);
            if (did >= attributed.size() || attributed[did]) {
              continue;
            }
            stack.push_back(d);
          }
        }
      }
    }
  }
  trace_stage("readback-srcmap");

  bypass_setmask_bit_reads(body);
  trace_stage("readback-packed-bits");

  trace_stage("readback-complete");
  return true;
}

}  // namespace livehd::synth
