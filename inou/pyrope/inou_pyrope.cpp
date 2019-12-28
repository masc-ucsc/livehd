//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "annotate.hpp"
#include "inou_pyrope.hpp"

void setup_inou_pyrope() {
  Inou_pyrope::setup();
}

void Inou_pyrope::setup() {
  Eprp_method m1("inou.pyrope", "convert from pyrope to LNAST/lgraph", &Inou_pyrope::work);
  m1.add_label_required("files",  "pyrope files to process (comma separated)");
  m1.add_label_optional("path",   "path to put the lgraph[s]", "lgdb");

  register_pass(m1);
}

Inou_pyrope::Inou_pyrope(const Eprp_var &var)
  : Pass("inou.pyrope", var) {
}

void Inou_pyrope::work(Eprp_var &var) {
  Inou_pyrope p(var);

  for (auto f : absl::StrSplit(p.files, ',')) {
    p.to_lgraph(f);
  }
}

void Inou_pyrope::to_lgraph(std::string_view file) {
  Lbench b("inou.pyrope");

  I(false); // FIXME: add code here
}

