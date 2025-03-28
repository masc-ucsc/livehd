//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_prp.hpp"

#include "absl/strings/str_split.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "perf_tracing.hpp"
#include "prp2lnast.hpp"

static Pass_plugin sample("inou_prp", Inou_prp::setup);

void Inou_prp::setup() {
  Eprp_method m1("inou.prp", "Parse the input file and convert to an LNAST", &Inou_prp::parse_to_lnast);
  m1.add_label_required("files", "prp files to process (comma separated)");
  m1.add_label_optional("parse_only", "skip LNAST translation", "false");

  register_pass(m1);
}

Inou_prp::Inou_prp(const Eprp_var &var) : Pass("inou.prp", var) {}

void Inou_prp::parse_to_lnast(Eprp_var &var) {
  TRACE_EVENT("inou", "PRP_parse_to_lnast");

  Inou_prp p(var);
  bool     parse_only = (var.get("parse_only") == "true");

  for (const auto &f : absl::StrSplit(p.files, ',')) {
    auto basename       = str_tools::get_str_after_last_if_exists(f, '/');
    auto basename_noext = str_tools::get_str_before_first(basename, '.');

    Prp2lnast converter(f, basename_noext, parse_only);

    auto lnast = converter.get_lnast();

    var.add(std::move(lnast));
  }
}
