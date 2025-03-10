//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"

#include "cgen_verilog.hpp"
#include "file_utils.hpp"
#include "perf_tracing.hpp"
#include "thread_pool.hpp"

static Pass_plugin sample("inou_cgen", Inou_cgen::setup);

Inou_cgen::Inou_cgen(const Eprp_var &var) : Pass("inou.cgen", var) {
  auto v  = var.get("verbose");
  verbose = v != "false" && v != "0";
}

void Inou_cgen::setup() {
  Eprp_method m1("inou.cgen.verilog", "export verilog from an Lgraph", &Inou_cgen::to_cgen_verilog);

  m1.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");
  register_inou("cgen", m1);
}

void Inou_cgen::to_cgen_verilog(Eprp_var &var) {
  TRACE_EVENT("inou", "verilog_gen");

  Inou_cgen pp(var);

  auto dir     = pp.get_odir(var);
  auto verbose = pp.verbose;

  std::sort(var.lgs.begin(), var.lgs.end(), [](Lgraph *a, Lgraph *b) { return a->size() > b->size(); });

  for (auto *lg : var.lgs) {
    thread_pool.add([lg, verbose, dir]() -> void {
      Cgen_verilog p(verbose, dir);
      p.do_from_lgraph(lg);
    });
  }

  // no need to sync for cgen. It will sync before exit lgshell if needed
}
