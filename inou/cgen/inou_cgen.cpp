//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"

#include "cgen_sim.hpp"
#include "cgen_verilog.hpp"
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
  m2.add_label_optional("vcd", "VCD trace file baked into the sim (compile.sim.vcd); empty = no VCD", "");
  m2.add_label_optional("top", "top module name; only it emits VCD (avoids file collisions)", "");
  m2.add_label_optional("vcdfakedelay",
                        "VCD data settles a few ticks after each clock edge, with X during the settle window "
                        "(compile.sim.vcdfakedelay); false = plain edge-aligned updates (no X, no delay)",
                        "true");
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
  auto      fakedelay = var.get("vcdfakedelay");

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
