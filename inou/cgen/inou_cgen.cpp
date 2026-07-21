//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"

#include "cgen_sim.hpp"
#include "cgen_verilog.hpp"
#include "diag.hpp"  // livehd::diag::err — flag-value validation
#include "file_utils.hpp"
#include "perf_tracing.hpp"

static Pass_plugin sample("inou_cgen", Inou_cgen::setup);

Inou_cgen::Inou_cgen(const Eprp_var& var) : Pass("inou.cgen", var) {
  auto v  = var.get("verbose");
  verbose = v != "false" && v != "0";
  auto m  = var.get("srcmap");
  srcmap  = m == "true" || m == "1";
}

void Inou_cgen::setup() {
  Eprp_method m1("inou.cgen.verilog", "export verilog from an Lgraph", &Inou_cgen::to_cgen_verilog);

  m1.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");
  m1.add_label_optional("srcmap", "emit an ECMA-426 source-map sidecar (.v.map + sourceMappingURL comment)", "false");
  register_inou("cgen", m1);

  // inou.cgen.sim — executable Slop C++ from an Lgraph (TODO 3d). Per-module
  // <name>.hpp written into `odir`; the standalone Bazel module scaffold around
  // them is written by the kernel's emit_sim_outputs.
  Eprp_method m2("inou.cgen.sim", "export executable slop C++ from an Lgraph", &Inou_cgen::to_cgen_sim);
  m2.add_label_optional("vcd",
                        "baked-in VCD trace path (the sim.vcd knob: the kernel maps false->none, true-><top>.vcd, "
                        "FILE->that path); empty = no VCD",
                        "");
  m2.add_label_optional("top", "top module name; only it emits VCD (avoids file collisions)", "");
  m2.add_label_optional("vcd_fake_delay",
                        "VCD data settles a few ticks after each clock edge, with X during the settle window "
                        "(sim.vcd_fake_delay); false = plain edge-aligned updates (no X, no delay)",
                        "true");
  // Host-build include roots for the generated driver. Normally resolved
  // automatically — bazel runfiles, or the sibling ../hlop and ../iassert of a
  // source checkout — so these are only needed to point the sim build at a
  // DIFFERENT checkout, which is how you test new slop/vcd_writer code without
  // reinstalling it. Registered (not just read from --set) so they are
  // discoverable via `lhd list options compile.cgen`.
  m2.add_label_optional("sim_hlop_dir",
                        "hlop checkout to build the sim driver against (resolves slop.hpp/blop.hpp/vcd_writer.hpp); "
                        "empty = auto (bazel runfiles, else the sibling ../hlop) — set it to test a WIP hlop",
                        "");
  m2.add_label_optional("sim_iassert_dir",
                        "iassert checkout to build the sim driver against (resolves iassert.hpp, which slop.hpp "
                        "pulls in); empty = auto (bazel runfiles, else the sibling ../iassert/src)",
                        "");
  register_inou("cgen", m2);
}

void Inou_cgen::to_cgen_verilog(Eprp_var& var) {
  TRACE_EVENT("inou", "verilog_gen");

  Inou_cgen pp(var);

  auto dir     = pp.get_odir(var);
  auto verbose = pp.verbose;
  auto srcmap  = pp.srcmap;

  // Consume var.graphs (HHDS handle): Eprp_var::add(Lgraph*) pushes the paired
  // shadow into var.graphs, so legacy producers (yosys.tolg, lgraph.match)
  // automatically feed this path. One .v per module, generated inline.
  // (Per-module Verilog cgen used to be dispatched onto a custom thread pool,
  // but that was an unused performance optimization; build-level parallelism
  // comes from independent lhd invocations. If sim/cgen ever needs
  // intra-process parallelism it will be reintroduced via taskflow.)
  // Internal graph names are the hierarchical, always-unique `file.entity`
  // (two files may define the same simple module name). Verilog module names are
  // flat, so pre-compute one flat name per co-emitted graph, shared by every
  // module header and sub-instance reference: a bare `entity` when it is unique
  // across this set, else the sanitized `file_entity` (keeps the netlist
  // collision-free without reintroducing the `lg="…"` override).
  absl::flat_hash_map<std::string, std::string> flat_names;
  {
    absl::flat_hash_map<std::string, std::vector<std::string>> by_entity;
    for (const auto& g : var.graphs) {
      if (!g) {
        continue;
      }
      std::string full(g->get_name());
      auto        pos = full.find_last_of("./");
      by_entity[pos == std::string::npos ? full : full.substr(pos + 1)].push_back(std::move(full));
    }
    auto sanitize = [](std::string s) {
      for (auto& c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (!ok) {
          c = '_';
        }
      }
      return s;
    };
    for (auto& [entity, fulls] : by_entity) {
      if (fulls.size() == 1) {
        flat_names.emplace(fulls.front(), entity);  // unique entity -> clean flat name
      } else {
        for (auto& f : fulls) {
          flat_names.emplace(f, sanitize(f));  // shared entity -> sanitized file_entity
        }
      }
    }
  }

  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    Cgen_verilog p(verbose, dir, srcmap, &flat_names);
    p.do_from_graph(g);
  }
}

void Inou_cgen::to_cgen_sim(Eprp_var& var) {
  TRACE_EVENT("inou", "sim_gen");

  Inou_cgen pp(var);
  auto      dir       = pp.get_odir(var);
  auto      vcd_out   = var.get("vcd");
  auto      top       = var.get("top");
  auto      fakedelay = var.get("vcd_fake_delay");
  // Boolean grammar, validated loudly: anything outside the canonical set would
  // otherwise silently mean "true" (the sim.* namespace validates its own copy,
  // but the sim.vcd_fake_delay knob reaches this label directly).
  if (!fakedelay.empty() && fakedelay != "true" && fakedelay != "1" && fakedelay != "on" && fakedelay != "false"
      && fakedelay != "0" && fakedelay != "off") {
    livehd::diag::err("inou.cgen.sim", "bad-flag-value", "usage")
        .msg("sim.vcd_fake_delay expects true|false, got '{}'", fakedelay)
        .emit();
    return;
  }

  // Synchronous (one .hpp per module): the designs are small and the kernel's
  // sim_into() checks each <module>.hpp exists right after this returns.
  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    Cgen_sim p(dir, vcd_out, top, fakedelay);
    p.do_from_graph(g);
  }
}
