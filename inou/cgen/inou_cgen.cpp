//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"

#include "cgen_verilog.hpp"
#include "eprp_utils.hpp"
#include "thread_pool.hpp"

static Pass_plugin sample("inou_cgen", Inou_cgen::setup);

Inou_cgen::Inou_cgen(const Eprp_var &var) : Pass("inou.cgen", var) {
  auto v  = var.get("verbose");
  verbose = v != "false" && v != "0";
}

void Inou_cgen::setup() {
  Eprp_method m1(mmap_lib::str("inou.cgen.verilog"), mmap_lib::str("export verilog from an Lgraph"), &Inou_cgen::to_cgen_verilog);

  m1.add_label_optional("verbose", mmap_lib::str("dump bits and wirename (true/false)"), "false");
  register_inou("cgen", m1);
}

void Inou_cgen::to_cgen_verilog(Eprp_var &var) {
  Inou_cgen pp(var);

  auto dir     = pp.get_odir(var);
  auto verbose = pp.verbose;

  for (const auto &l : var.lgs) {
    thread_pool.add([l, verbose, dir]() {
      Cgen_verilog p(verbose, dir);
      p.do_from_lgraph(l);
    });
  }

  // no need to sync for cgen. It will sync before exit lgshell if needed
}

