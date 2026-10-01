//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// DIRECTION 2 -- the LGraph certificate (`<Top>_designCert`), and nothing else.
//
// `formal.lean.mode=verified_compiler` emits ONE artifact: a `Compiler.DesignCert`
// value.  There is no model here and no proof script: the model IS
// `Compiler.compileDesign` applied to this certificate, and its correctness is
// `Compiler.compileDesign_correct`, proved once for every design.  All this file
// does is remap the shared scan onto the dense slot space and format it -- plus
// carry the clock provenance the step semantics needs (`DesignCert.clocks`, and
// a clock ordinal on every FlopDesc/MemoryDesc).
//
// Everything above this file is REUSED, not reimplemented: the same topo walk,
// the same `cert_node_expr` operator spellings, the same memory decomposition,
// all handed over in `Design_scan` (see lean_common.hpp).  The direction owns
// only what is below that line, which is the point of the split -- a change here
// cannot reach the legacy emission, and vice versa.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "design_cert_export.hpp"
#include "diag.hpp"
#include "latch_contract.hpp"
#include "lean_common.hpp"

namespace livehd::lean_ir::ir_semantics {

namespace lc = livehd::latch_contract;
namespace gu = livehd::graph_util;

// Emit the certificate for `scan`, or refuse through `fatal`.  Returning from
// here ends the pass's work on this graph, exactly as the inlined block did.
void emit(const Design_scan& scan) {
  // Name the scan's members so the body below reads as it did when it was a
  // block inside emit_for_graph.  Pure aliasing -- no copies.
  const auto&  ctx                  = scan.ctx;
  auto*        g                    = scan.g;
  const auto&  raw_name             = scan.raw_name;
  const auto&  base_name            = scan.base_name;
  const auto&  lean_path            = scan.lean_path;
  const auto&  flop_nodes           = scan.flop_nodes;
  const auto&  flop_reset           = scan.flop_reset;
  const auto&  flop_async           = scan.flop_async;
  const auto&  flop_active_low      = scan.flop_active_low;
  const auto&  flop_initial         = scan.flop_initial;
  const auto&  flop_negreset        = scan.flop_negreset;
  auto&        cert_build           = scan.cert_build;
  const auto&  cert_infos           = scan.cert_infos;
  const auto&  mem_cert_ids         = scan.mem_cert_ids;
  const auto&  output_cert_ids      = scan.output_cert_ids;
  const auto&  flop_din_cert_ids    = scan.flop_din_cert_ids;
  const auto&  flop_reset_cert_ids  = scan.flop_reset_cert_ids;
  const auto&  flop_enable_cert_ids = scan.flop_enable_cert_ids;
  const auto&  source_ids           = scan.source_ids;
  (void)g;
  (void)flop_active_low;

    lean_design_cert::DesignIn din;

    // Ordinals must match the runtime arrays: `RuntimeInput[k]`, `.flops[k]`,
    // `.mems[k]`.  Both maps below are std::map, so the order is deterministic.
    std::map<uint32_t, uint32_t> input_ordinal;   // source id -> input index
    // NON-SEMANTIC IO METADATA, collected in the SAME loops that assign the
    // ordinals. An external driver needs names to build a stimulus, and the
    // certificate is positional by design -- but deriving the mapping by
    // re-walking GraphIO and assuming it agrees is exactly how a sidecar drifts
    // from the model it claims to describe. Nothing here touches DesignCert or
    // compileDesign_correct.
    struct Io_meta {
      std::string name;
      uint32_t    ordinal;
      uint32_t    width;
    };
    std::vector<Io_meta> io_inputs;
    std::vector<Io_meta> io_outputs;
    std::vector<std::string> io_clocks;
    {
      uint32_t k = 0;
      for (const auto& kv : ctx.input_field) {
        if (auto it = ctx.input_source_id.find(kv.first); it != ctx.input_source_id.end()) {
          const auto wit = ctx.input_width.find(kv.first);
          io_inputs.push_back({kv.first, k, wit == ctx.input_width.end() ? 0 : wit->second});
          input_ordinal[it->second] = k++;
        }
      }
    }
    std::map<uint32_t, uint32_t> flop_ordinal;    // flop nid (== its Q source id) -> index
    std::vector<uint32_t>        flop_order;
    {
      uint32_t k = 0;
      for (const auto& kv : ctx.flop_field) {
        flop_ordinal[kv.first] = k++;
        flop_order.push_back(kv.first);
      }
      // A SYNC memory's registered read port is state, even when its table is
      // immutable.  cert_memory_expand already models it as
      // `if read_enable then table[addr] else old` (an Op_MuxBool over the
      // register's own source), so it is an ordinary flop here -- appended after
      // the real flops so `RuntimeState.flops` indices stay stable.  Without
      // this the export fatals on "flop source id N has no flop ordinal",
      // because these source ids are synthetic (>= 1e9), not LGraph nids.
      for (const auto& kv : cert_build.sync_read_regs) {
        flop_ordinal[kv.first] = k++;
        flop_order.push_back(kv.first);
      }
    }
    std::map<uint32_t, uint32_t> mem_ordinal;     // array_src id -> index
    std::vector<uint32_t>        mem_order;
    {
      uint32_t k = 0;
      for (const auto& kv : mem_cert_ids) {
        // ROMs are immutable: no RuntimeState.mems entry, so no ordinal and no
        // MemoryDesc.  Their contents ride SourceDesc.memConst instead.
        if (ctx.memory_info.at(kv.first).is_rom) {
          continue;
        }
        mem_ordinal[kv.second.array_src] = k++;
        mem_order.push_back(kv.first);
      }
    }

    for (auto sid : source_ids) {
      lean_design_cert::SourceIn s;
      s.id    = sid;
      auto width_it = cert_build.source_width.find(sid);
      if (width_it == cert_build.source_width.end()) {
        fatal(ctx, "internal: certificate source id " + std::to_string(sid) + " has no width");
      }
      s.width = width_it->second;
      auto kind_it = cert_build.source_kind.find(sid);
      const int kind = kind_it == cert_build.source_kind.end() ? -1 : kind_it->second;
      switch (kind) {
        case 0:
          s.kind    = lean_design_cert::SourceKind::Input;
          if (auto it = input_ordinal.find(sid); it != input_ordinal.end()) {
            s.ordinal = it->second;
          } else {
            fatal(ctx, "internal: input source id " + std::to_string(sid) + " has no input ordinal");
          }
          break;
        case 1:
          s.kind      = lean_design_cert::SourceKind::Const;
          if (auto it = cert_build.source_const_int.find(sid); it != cert_build.source_const_int.end()) {
            s.const_int = it->second;
          } else {
            fatal(ctx, "internal: constant source id " + std::to_string(sid) + " has no value");
          }
          break;
        case 2: {
          s.kind    = lean_design_cert::SourceKind::Flop;
          if (auto it = flop_ordinal.find(sid); it != flop_ordinal.end()) {
            s.ordinal = it->second;
          } else {
            fatal(ctx, "internal: flop source id " + std::to_string(sid) + " has no flop ordinal");
          }
          // `sid` IS the flop's nid (see cert_dep_id's flop arm).  An ASYNCHRONOUS
          // reset changes Q immediately, so a combinational reader in the same
          // cycle must already see the reset value -- a plain `flopQ`, which reads
          // only the stored state, would give SYNCHRONOUS semantics.
          if (flop_async.count(sid) != 0) {
            // `sourceValue` runs before any slot exists, so it can only read
            // `RuntimeInput`; the async reset must therefore be a primary input.
            // That is the hardware pattern (`negedge rst_ni` off a top-level
            // port).  Anything else is refused, never modeled as synchronous.
            Node_pin rp;
            if (auto it2 = flop_reset.find(sid); it2 != flop_reset.end()) {
              rp = it2->second;
            } else {
              fatal(ctx, "flop n_" + std::to_string(sid)
                             + " is marked `async` but drives no `reset_pin` net.");
            }
            // Follow width-only reshaping.  After cprop a top-level `rst_ni` reaches
            // the flop through resize nodes (arity-1 Op_Or / all-ones Op_GetMask),
            // so testing the immediate driver for graph-input-ness reports "computed
            // inside the design" for what is plainly a port.
            Node_pin src = resolve_resize_chain(rp);
            if (!pin_is_input(src)) {
              fatal(ctx, "flop n_" + std::to_string(sid)
                             + " has an ASYNCHRONOUS reset that is not driven by a primary input. "
                               "`SourceDesc.flopQAsync` reads the reset out of RuntimeInput, so it cannot "
                               "express a reset computed inside the design.");
            }
            const auto rname = input_name_for_pin(ctx, src);
            auto       rsid  = ctx.input_source_id.find(rname);
            if (rsid == ctx.input_source_id.end() || input_ordinal.count(rsid->second) == 0) {
              fatal(ctx, "flop n_" + std::to_string(sid) + " async reset input `" + std::string(rname)
                             + "` has no input ordinal.");
            }
            const bool active_low = flop_active_low.count(sid) != 0;
            s.async_reset      = true;
            s.reset_input      = input_ordinal.at(rsid->second);
            s.reset_active_low = active_low;
            if (auto it3 = flop_initial.find(sid); it3 != flop_initial.end()) {
              s.reset_value = it3->second;
            }
          }
          break;
        }
        case 4: {
          // Immutable table.  No `RuntimeState.mems` entry and no ordinal: a ROM
          // carries nothing from cycle to cycle.
          s.kind = lean_design_cert::SourceKind::RomConst;
          auto it = cert_build.source_rom_contents.find(sid);
          if (it == cert_build.source_rom_contents.end() || it->second.empty()) {
            fatal(ctx, "internal: ROM source id " + std::to_string(sid) + " has no contents");
          }
          s.rom_contents = it->second;
          for (const auto& kv : mem_cert_ids) {
            if (kv.second.array_src == sid) {
              s.addr_w = ctx.memory_info.at(kv.first).addr_width;
              break;
            }
          }
          break;
        }
        case 3: {
          s.kind    = lean_design_cert::SourceKind::MemImage;
          if (auto it = mem_ordinal.find(sid); it != mem_ordinal.end()) {
            s.ordinal = it->second;
          } else {
            fatal(ctx, "internal: memory source id " + std::to_string(sid) + " has no memory ordinal");
          }
          // address width of the owning memory
          bool found_owner = false;
          for (const auto& kv : mem_cert_ids) {
            if (kv.second.array_src == sid) {
              s.addr_w = ctx.memory_info.at(kv.first).addr_width;
              found_owner = true;
              break;
            }
          }
          if (!found_owner) {
            fatal(ctx, "internal: memory source id " + std::to_string(sid) + " has no owning memory");
          }
          break;
        }
        default:
          fatal(ctx, "internal: certificate source id " + std::to_string(sid) + " has no kind");
      }
      din.sources.push_back(s);
    }

    for (size_t i = 0; i < cert_infos.size(); ++i) {
      const auto& ci = cert_infos[i];
      din.nodes.push_back({ci.nid, ci.op_expr, ci.width, ci.deps});
    }

    for (const auto& kv : ctx.output_field) {
      auto it = output_cert_ids.find(kv.first);
      if (it == output_cert_ids.end()) {
        continue;  // undriven output: no slot to name
      }
      // Appended in lockstep with din.outputs, so RuntimeResult.outputs[k] and
      // the metadata's k-th entry cannot disagree: an UNDRIVEN output is
      // skipped by both or by neither.
      io_outputs.push_back({kv.first, static_cast<uint32_t>(din.outputs.size()), ctx.output_width.at(kv.first)});
      din.outputs.push_back({it->second, ctx.output_width.at(kv.first)});
    }

    // ---- Clock provenance -------------------------------------------------
    // The certificate names its clock DOMAINS and every flop / memory carries
    // the ordinal of the one it commits on, so the Lean step semantics can be
    // told which clocks fire (`interpretDesign D edges i s`). Identity is
    // latch_contract's resolved ROOT net -- the same notion pass.single_edge
    // and the LEC clock forest use -- so a domain here is a domain there.
    //
    // Before this the exporter READ the clock (`Memory_port_info::clock`, the
    // flop `clock_pin` arm above) and dropped it, which is what made single-edge
    // normalization a wholly trusted precondition: the certificate could not
    // even say which edge an element committed on. Two things are refused here
    // rather than silently modelled:
    //   * a GATED clock still on `clock_pin` (`clk & en`): this model has no
    //     per-step commit term for a gate. pass.single_edge folds it into the
    //     element's enable; a design that reached here with the gate in place
    //     would have every gated element modelled as committing on every edge;
    //   * a FALLING-edge commit spelled as an inversion in the clock cone: the
    //     `posclk` arm only sees the pin form, and `always @(posedge ~clk)` is
    //     the same machine as a negedge flop.
    // Ordinal 0 is the domain carrying the most state (ties by key), the same
    // rule pass.single_edge picks its reference by. A Pyrope `reg x = 0`
    // (implicit clock) joins the unique other root when there is exactly one --
    // it IS the module clock, which tolg spells `clock` -- and is otherwise a
    // domain of its own, named `clock`.
    const lc::Design_clocks            dclocks(g);
    std::map<std::string, int>         clock_weight;   // net key -> #elements committing on it
    std::map<std::string, std::string> clock_display;  // net key -> name written to the certificate
    std::map<uint32_t, std::string>    flop_clock_key; // flop nid -> net key
    std::map<uint32_t, std::string>    mem_clock_key;  // memory nid -> net key
    const std::string implicit_key = [] {
      lc::Commit_class c;
      c.implicit_clock = true;
      return c.net_key();
    }();
    const auto display_of = [](const lc::Commit_class& cc) -> std::string {
      if (cc.implicit_clock) {
        return "clock";
      }
      if (!cc.net.is_invalid() && gu::is_graph_input_pin(cc.net)) {
        return std::string(gu::pin_name_of(cc.net));
      }
      return "derived:" + gu::debug_name(cc.net.get_master_node());
    };
    const auto note_clock = [&](const lc::Commit_class& cc) {
      const auto k = cc.net_key();
      ++clock_weight[k];
      clock_display.try_emplace(k, display_of(cc));
      return k;
    };
    for (auto& fn : flop_nodes) {
      const auto nid = node_id(fn);
      if (auto clk = gu::get_driver_of_sink_name(fn, "clock_pin"); !clk.is_invalid()) {
        if (auto icg = lc::resolve_icg(clk, dclocks)) {
          fatal(ctx, "flop n_" + std::to_string(nid) + " is clocked through a gated-clock cone (`clk & en`, "
                         + std::to_string(icg->enables.size())
                         + " enable(s)) that pass.single_edge has not folded into its enable. This model has no "
                           "per-step commit term for a gate; run pass.single_edge first.");
        }
      }
      auto cc = lc::commit_class_of(fn, &dclocks);
      if (!cc) {
        fatal(ctx, "flop n_" + std::to_string(nid)
                       + " has a clock cone that does not resolve to a root net, so the certificate cannot name "
                         "its clock domain.");
      }
      if (!cc->rising) {
        fatal(ctx, "flop n_" + std::to_string(nid)
                       + " commits on a FALLING edge (an inversion in its clock cone). pass.single_edge normalizes "
                         "negedge state away; it evidently did not run.");
      }
      flop_clock_key[nid] = note_clock(*cc);
    }
    // Every memory that COMMITS something needs a domain -- which is not the same
    // set as `mem_order`.  A SYNCHRONOUS ROM is immutable, so it has no ordinal
    // and no `MemoryDesc` (its contents ride `SourceDesc.memConst`), but its
    // registered read port IS state: `cert_memory_expand` gives it a synthetic
    // flop in `flop_order`, and that flop asks this map for its clock.  Keying
    // the walk on `mem_order` left the sync ROM out and turned the lookup below
    // into an undiagnosed `map::at` crash -- measured on `txfma_f1`, which used
    // to fail with a diagnosed memory refusal.  An ASYNCHRONOUS ROM is skipped:
    // it commits nothing and owns no register, so giving it a domain would
    // invent a clock for a combinational table.
    std::vector<uint32_t> mem_clocked;
    for (const auto& kv : mem_cert_ids) {
      const auto& mi = ctx.memory_info.at(kv.first);
      // An immutable ROM with NO synchronous read port commits nothing and
      // owns no register. One sync read port is enough to give it a domain,
      // because that port's read-data register commits on an edge.
      if (mi.is_rom && !mi.any_sync_read) {
        continue;
      }
      mem_clocked.push_back(kv.first);
    }
    for (auto mnid : mem_clocked) {
      const auto& mi = ctx.memory_info.at(mnid);
      if (mi.posclk == Ntype::Memory_posclk_mixed) {
        fatal(ctx, "memory n_" + std::to_string(mnid)
                       + " commits on more than one clock edge across its ports (posclk=mixed); one MemoryDesc "
                         "carries one clock, and pass.single_edge refuses this shape too.");
      }
      std::optional<lc::Commit_class> mcc;
      for (const auto& port : mi.ports) {
        // Skip only the ASYNCHRONOUS read ports. `port.rdport && !mi.sync`
        // asked a memory-level question of a port: on a mixed memory it
        // skipped every read port, including the synchronous ones whose
        // read-data registers do commit on an edge.
        if (port.is_async_read()) {
          continue;  // an async read commits nothing
        }
        if (port.clock.is_invalid()) {
          continue;  // implicit module clock
        }
        if (gu::is_const_pin(port.clock)) {
          fatal(ctx, "memory n_" + std::to_string(mnid) + " port " + std::to_string(port.port_id)
                         + " has a constant clock: a level-sensitive (latch-array) write, which this "
                           "edge-triggered model does not represent.");
        }
        if (auto icg = lc::resolve_icg(port.clock, dclocks)) {
          fatal(ctx, "memory n_" + std::to_string(mnid) + " port " + std::to_string(port.port_id)
                         + " is clocked through a gated-clock cone that pass.single_edge has not folded into the "
                           "port's enable; run pass.single_edge first.");
        }
        const auto cr = lc::control_root(port.clock);
        if (cr.net.is_invalid() || gu::is_const_pin(cr.net)) {
          fatal(ctx, "memory n_" + std::to_string(mnid) + " port " + std::to_string(port.port_id)
                         + " has a clock cone that does not resolve to a root net.");
        }
        lc::Commit_class cc;
        cc.role   = lc::Net_role::Clock;
        cc.net    = cr.net;
        cc.rising = (mi.posclk != 0) != cr.inverted;
        if (!cc.rising) {
          fatal(ctx, "memory n_" + std::to_string(mnid) + " port " + std::to_string(port.port_id)
                         + " commits on a FALLING edge; pass.single_edge normalizes negedge state away.");
        }
        if (mcc && mcc->net_key() != cc.net_key()) {
          fatal(ctx, "memory n_" + std::to_string(mnid) + " has committing ports on different clock nets (`"
                         + display_of(*mcc) + "` and `" + display_of(cc) + "`); one MemoryDesc carries one clock.");
        }
        mcc = cc;
      }
      if (!mcc) {
        lc::Commit_class cc;
        cc.implicit_clock = true;
        cc.role           = lc::Net_role::Clock;
        cc.rising         = true;
        mcc               = cc;
      }
      mem_clock_key[mnid] = note_clock(*mcc);
    }
    if (clock_weight.count(implicit_key) != 0 && clock_weight.size() == 2) {
      // The implicit module clock IS the one named root (the LEC clock forest's
      // "unique other root" rule); two spellings of one domain must not become two.
      std::string other;
      for (const auto& [k, w] : clock_weight) {
        if (k != implicit_key) {
          other = k;
        }
      }
      clock_weight[other] += clock_weight[implicit_key];
      clock_weight.erase(implicit_key);
      for (auto& [n, k] : flop_clock_key) {
        if (k == implicit_key) {
          k = other;
        }
      }
      for (auto& [n, k] : mem_clock_key) {
        if (k == implicit_key) {
          k = other;
        }
      }
    }
    std::vector<std::pair<std::string, int>> clock_order(clock_weight.begin(), clock_weight.end());
    std::sort(clock_order.begin(), clock_order.end(), [](const auto& a, const auto& b) {
      return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    std::map<std::string, uint32_t> clock_ordinal;
    for (const auto& [k, w] : clock_order) {
      clock_ordinal[k] = static_cast<uint32_t>(din.clocks.size());
      din.clocks.push_back({clock_display.at(k)});
      io_clocks.push_back(clock_display.at(k));
    }
    if (din.clocks.empty()) {
      // A purely combinational design still declares its (unused) domain:
      // `checkDesign` requires ordinal 0 to exist.
      din.clocks.push_back({"clock"});
      io_clocks.push_back("clock");
    }
    const auto flop_clock = [&](uint32_t fid) -> uint32_t {
      if (auto it = flop_clock_key.find(fid); it != flop_clock_key.end()) {
        return clock_ordinal.at(it->second);
      }
      if (auto ow = cert_build.sync_read_owner.find(fid); ow != cert_build.sync_read_owner.end()) {
        if (auto mk = mem_clock_key.find(ow->second); mk != mem_clock_key.end()) {
          return clock_ordinal.at(mk->second);
        }
        fatal(ctx, "internal: sync read register of memory n_" + std::to_string(ow->second)
                       + " has no clock domain");
      }
      fatal(ctx, "internal: flop source id " + std::to_string(fid) + " has no clock domain");
      return 0;
    };

    for (auto fid : flop_order) {
      lean_design_cert::FlopIn f;
      f.clock = flop_clock(fid);
      // A synthetic sync-read register: width and next value come from the
      // memory decomposition, and it has no enable and no reset -- the enable is
      // already folded into its next value by cert_memory_expand.
      if (auto sr = cert_build.sync_read_regs.find(fid); sr != cert_build.sync_read_regs.end()) {
        f.width = sr->second.first;
        f.din   = sr->second.second;
        din.flops.push_back(f);
        continue;
      }
      f.width = ctx.flop_width.at(fid);
      if (auto it = flop_din_cert_ids.find(fid); it != flop_din_cert_ids.end()) {
        f.din = it->second;
      } else {
        fatal(ctx, "flop n_" + std::to_string(fid) + " has no `din` driver.");
      }
      if (auto it = flop_enable_cert_ids.find(fid); it != flop_enable_cert_ids.end()) {
        f.enable = it->second;
      }
      if (auto it = flop_reset_cert_ids.find(fid); it != flop_reset_cert_ids.end()) {
        f.reset_pin = it->second;
      }
      if (auto it = flop_initial.find(fid); it != flop_initial.end()) {
        f.reset_value = it->second;
      }
      f.reset_active_low = flop_negreset.count(fid) != 0;
      f.async_reset      = flop_async.count(fid) != 0;
      din.flops.push_back(f);
    }

    for (auto mnid : mem_order) {
      const auto& mi = ctx.memory_info.at(mnid);
      din.memories.push_back(
          {mi.addr_width, mi.bits, mem_cert_ids.at(mnid).next_chain, clock_ordinal.at(mem_clock_key.at(mnid))});
    }

    const std::string vc_tmp = lean_path + ".tmp";
    std::ofstream     vofs(vc_tmp);
    if (!vofs) {
      livehd::diag::warn("pass.lean", "write-failed", "io").msg("could not write {}", vc_tmp).emit();
      return;
    }
    lean_design_cert::RemapError err;
    if (!lean_design_cert::emit_design_cert(base_name, din, vofs, err)) {
      vofs.close();
      std::remove(vc_tmp.c_str());
      fatal(ctx, "verified_compiler export: " + err.message);
    }
    vofs.close();
    if (std::rename(vc_tmp.c_str(), lean_path.c_str()) != 0) {
      livehd::diag::warn("pass.lean", "write-failed", "io").msg("could not rename {}", vc_tmp).emit();
      return;
    }

    // IO sidecar, written ONLY once the certificate itself is on disk, and via
    // a temp + rename like the certificate: a refused or half-written design
    // must not leave behind metadata that looks valid and describes nothing.
    {
      const std::string io_path = lean_path.substr(0, lean_path.rfind("_Lgraph.lean")) + "_io.json";
      const std::string io_tmp  = io_path + ".tmp";
      std::ofstream iofs(io_tmp, std::ios::trunc);
      if (iofs.is_open()) {
        auto esc = [](const std::string& v) {
          std::string o;
          for (char c : v) {
            if (c == '"' || c == '\\') {
              o += '\\';
            }
            o += c;
          }
          return o;
        };
        iofs << "{\n  \"schema_version\": 1,\n  \"kind\": \"lean_design_io\",\n";
        iofs << "  \"top\": \"" << esc(raw_name) << "\",\n";
        iofs << "  \"note\": \"Positional map for DesignCert. inputs[k].ordinal indexes RuntimeInput; "
                "outputs[k].ordinal indexes RuntimeResult.outputs; clocks[k] indexes ClockEdges. "
                "NON-SEMANTIC: no DesignCert field and no part of compileDesign_correct.\",\n";
        auto dump = [&](const char* key, const std::vector<Io_meta>& v) {
          iofs << "  \"" << key << "\": [";
          for (size_t i = 0; i < v.size(); ++i) {
            iofs << (i ? ",\n    " : "\n    ") << "{\"name\": \"" << esc(v[i].name) << "\", \"ordinal\": " << v[i].ordinal
                 << ", \"width\": " << v[i].width << "}";
          }
          iofs << (v.empty() ? "" : "\n  ") << "],\n";
        };
        dump("inputs", io_inputs);
        dump("outputs", io_outputs);
        iofs << "  \"clocks\": [";
        for (size_t i = 0; i < io_clocks.size(); ++i) {
          iofs << (i ? ",\n    " : "\n    ") << "{\"name\": \"" << esc(io_clocks[i]) << "\", \"ordinal\": " << i << "}";
        }
        iofs << (io_clocks.empty() ? "" : "\n  ") << "]\n}\n";
        iofs.close();
        if (std::rename(io_tmp.c_str(), io_path.c_str()) != 0) {
          livehd::diag::warn("pass.lean", "write-failed", "io").msg("could not rename {}", io_tmp).emit();
          std::remove(io_tmp.c_str());
        }
      }
    }
    std::cout << "pass.lean: " << raw_name << " -> " << lean_path << " (verified_compiler: " << din.sources.size()
              << " sources, " << din.nodes.size() << " nodes, " << din.flops.size() << " flops, "
              << din.memories.size() << " memories)\n";
    return;
}

}  // namespace livehd::lean_ir::ir_semantics
