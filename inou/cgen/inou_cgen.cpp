//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"

#include <algorithm>

#include "cgen_sim.hpp"
#include "cgen_verilog.hpp"
#include "file_utils.hpp"
#include "perf_tracing.hpp"
#include "thread_pool.hpp"

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
  register_inou("cgen", m2);
}

void Inou_cgen::to_cgen_verilog(Eprp_var& var) {
  TRACE_EVENT("inou", "verilog_gen");

  Inou_cgen pp(var);

  auto dir     = pp.get_odir(var);
  auto verbose = pp.verbose;
  auto srcmap  = pp.srcmap;

  // Migrated to consume var.graphs (HHDS handle). Eprp_var::add(Lgraph*)
  // pushes the paired shadow into var.graphs, so legacy producers
  // (yosys.tolg, lgraph.match) automatically feed this path.
  std::vector<std::shared_ptr<hhds::Graph>> graphs;
  graphs.reserve(var.graphs.size());
  for (const auto& g : var.graphs) {
    if (g) {
      graphs.push_back(g);
    }
  }

  // Estimate "size" via node_table — proxy for LiveHD's lg->size(). We sort
  // largest-first so the thread pool gets the heavy work in flight early.
  // Use forward_class iteration size as an approximation; if it's not cheap
  // enough at scale we can stash a node count on Graph and read it here.
  std::sort(graphs.begin(), graphs.end(), [](const std::shared_ptr<hhds::Graph>& a, const std::shared_ptr<hhds::Graph>& b) {
    size_t a_n = 0;
    for (auto _ : a->fast_class()) {
      (void)_;
      ++a_n;
    }
    size_t b_n = 0;
    for (auto _ : b->fast_class()) {
      (void)_;
      ++b_n;
    }
    return a_n > b_n;
  });

  for (auto& g : graphs) {
    thread_pool.add([g, verbose, dir, srcmap]() -> void {
      Cgen_verilog p(verbose, dir, srcmap);
      p.do_from_graph(g);
    });
  }
}

void Inou_cgen::to_cgen_sim(Eprp_var& var) {
  TRACE_EVENT("inou", "sim_gen");

  Inou_cgen pp(var);
  auto      dir     = pp.get_odir(var);
  auto      vcd_out = var.get("vcd");
  auto      top     = var.get("top");

  // Synchronous (one .hpp per module): the designs are small and the kernel's
  // sim_into() checks each <module>.hpp exists right after this returns.
  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    Cgen_sim p(dir, vcd_out, top);
    p.do_from_graph(g);
  }
}
