// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The VERIFIED-COMPILER exporter: emit `<Top>_designCert` and nothing else.
//
// Split out of `pass_lean.cpp` so the two artifacts stop sharing a file.  The
// legacy emitter there produces a fast model, a certificate and per-design
// bridge proofs; this one produces only the certificate, because the model and
// its correctness come from `Compiler.compileDesign` -- one theorem covering
// every design the compiler accepts, rather than proofs regenerated per design.
//
// The graph walk is NOT duplicated.  `pass_lean.cpp` runs it once and hands the
// result over in `VerifiedCompilerInputs`; all this file adds is the remap onto
// the dense slot space and the formatting.  See `design_cert_export.hpp`.

#include "verified-compiler.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "design_cert_export.hpp"
#include "diag.hpp"

namespace lean_pass {

void emit_verified_compiler(const LeanCtx& ctx, const CertBuild& cert_build,
                            const VerifiedCompilerInputs& in, const std::string& raw_name,
                            const std::string& base_name, const std::string& lean_path) {
  lean_design_cert::DesignIn din;

  // Ordinals must match the runtime arrays: `RuntimeInput[k]`, `.flops[k]`,
  // `.mems[k]`.  Both maps below are std::map, so the order is deterministic.
  std::map<uint32_t, uint32_t> input_ordinal;   // source id -> input index
  {
    uint32_t k = 0;
    for (const auto& kv : ctx.input_field) {
      if (auto it = ctx.input_source_id.find(kv.first); it != ctx.input_source_id.end()) {
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
    for (const auto& kv : in.mem_cert_ids) {
      // ROMs are immutable: no RuntimeState.mems entry, so no ordinal and no
      // MemoryDesc.  Their contents ride SourceDesc.memConst instead.
      if (ctx.memory_info.at(kv.first).is_rom) {
        continue;
      }
      mem_ordinal[kv.second.array_src] = k++;
      mem_order.push_back(kv.first);
    }
  }

  for (auto sid : in.source_ids) {
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
        if (in.flop_async.count(sid) != 0) {
          // `sourceValue` runs before any slot exists, so it can only read
          // `RuntimeInput`; the async reset must therefore be a primary input.
          // That is the hardware pattern (`negedge rst_ni` off a top-level
          // port).  Anything else is refused, never modeled as synchronous.
          Node_pin rp;
          if (auto it2 = in.flop_reset.find(sid); it2 != in.flop_reset.end()) {
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
          const bool active_low = in.flop_active_low.count(sid) != 0;
          s.async_reset      = true;
          s.reset_input      = input_ordinal.at(rsid->second);
          s.reset_active_low = active_low;
          if (auto it3 = in.flop_initial.find(sid); it3 != in.flop_initial.end()) {
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
        for (const auto& kv : in.mem_cert_ids) {
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
        for (const auto& kv : in.mem_cert_ids) {
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

  for (size_t i = 0; i < in.cert_infos.size(); ++i) {
    const auto& ci = in.cert_infos[i];
    din.nodes.push_back({ci.nid, ci.op_expr, ci.width, ci.deps});
  }

  for (const auto& kv : ctx.output_field) {
    auto it = in.output_cert_ids.find(kv.first);
    if (it == in.output_cert_ids.end()) {
      continue;  // undriven output: no slot to name
    }
    din.outputs.push_back({it->second, ctx.output_width.at(kv.first)});
  }

  for (auto fid : flop_order) {
    lean_design_cert::FlopIn f;
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
    if (auto it = in.flop_din_cert_ids.find(fid); it != in.flop_din_cert_ids.end()) {
      f.din = it->second;
    } else {
      fatal(ctx, "flop n_" + std::to_string(fid) + " has no `din` driver.");
    }
    if (auto it = in.flop_enable_cert_ids.find(fid); it != in.flop_enable_cert_ids.end()) {
      f.enable = it->second;
    }
    if (auto it = in.flop_reset_cert_ids.find(fid); it != in.flop_reset_cert_ids.end()) {
      f.reset_pin = it->second;
    }
    if (auto it = in.flop_initial.find(fid); it != in.flop_initial.end()) {
      f.reset_value = it->second;
    }
    f.reset_active_low = in.flop_negreset.count(fid) != 0;
    din.flops.push_back(f);
  }

  for (auto mnid : mem_order) {
    const auto& mi = ctx.memory_info.at(mnid);
    din.memories.push_back({mi.addr_width, mi.bits, in.mem_cert_ids.at(mnid).next_chain});
  }

  const std::string vc_tmp = lean_path + ".tmp";
  std::ofstream     vofs(vc_tmp);
  if (!vofs) {
    livehd::diag::warn("pass.lean", "write-failed", "io").msg("could not write {}", vc_tmp).emit();
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
  }
  std::cout << "pass.lean: " << raw_name << " -> " << lean_path << " (verified_compiler: " << din.sources.size()
            << " sources, " << din.nodes.size() << " nodes, " << din.flops.size() << " flops, "
            << din.memories.size() << " memories)\n";
}

}  // namespace lean_pass
