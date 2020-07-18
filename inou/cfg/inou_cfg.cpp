//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cfg.hpp"

#include "annotate.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void setup_inou_cfg() { Inou_cfg::setup(); }

void Inou_cfg::setup() {
  Eprp_method m1("inou.cfg", "Parse the cfg file and convert to an LNAST", &Inou_cfg::parse_to_lnast);
  m1.add_label_required("files", "cfg files to process (comma separated)");

  register_pass(m1);
}

Inou_cfg::Inou_cfg(const Eprp_var &var) : Pass("inou.cfg", var) {}

void Inou_cfg::parse_to_lnast(Eprp_var &var) {
  Lbench   b("inou.cfg");
  Inou_cfg p(var);

  for (auto &itr_f : absl::StrSplit(p.files, ',')) {
    const auto f = std::string(itr_f);
    Cfg_parser cfg_parser(f);

    std::unique_ptr<Lnast> lnast;
    lnast = cfg_parser.ref_lnast();
    /* lnast->ssa_trans(); */

    var.add(std::move(lnast));
  }
}
