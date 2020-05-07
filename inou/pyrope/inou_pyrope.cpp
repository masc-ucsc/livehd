//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_pyrope.hpp"

#include "annotate.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void setup_inou_pyrope() { Inou_pyrope::setup(); }

void Inou_pyrope::setup() {
  Eprp_method m2("inou.pyrope", "Parse the input file and convert to an LNAST", &Inou_pyrope::parse_to_lnast);
  m2.add_label_required("files", "pyrope files to process (comma separated)");

  register_pass(m2);
}

Inou_pyrope::Inou_pyrope(const Eprp_var &var) : Pass("inou.pyrope", var) {}

void Inou_pyrope::parse_to_lnast(Eprp_var &var){
  Lbench b("inou.pyrope");
  Inou_pyrope p(var);
  Prp_lnast converter;

  for (auto f : absl::StrSplit(p.files, ',')) {
    converter.parse_file(f);

    std::string name{f};
    auto found_path = name.find_last_of('/');
    if (found_path != std::string::npos)
      name = name.substr(found_path+1);

    auto found_dot  = name.find_last_of('.');
    if (found_dot != std::string::npos)
      name = name.substr(0,found_dot);

    var.add(std::move(converter.prp_ast_to_lnast(name)));
  }
}
