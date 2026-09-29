//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_prp.hpp"

#include "absl/strings/str_split.h"
#include "perf_tracing.hpp"
#include "prp2lnast.hpp"

static Pass_plugin sample("inou_prp", Inou_prp::setup);

void Inou_prp::setup() {
  Eprp_method m1("inou.prp", "Parse the input file and convert to an LNAST", &Inou_prp::parse_to_lnast);
  m1.add_label_required("files", "prp files to process (comma separated)");

  register_pass(m1);
}

Inou_prp::Inou_prp(const Eprp_var& var) : Pass("inou.prp", var) {}

void Inou_prp::parse_to_lnast(Eprp_var& var) {
  TRACE_EVENT("inou", "PRP_parse_to_lnast");

  Inou_prp p(var);

  for (const auto& f : absl::StrSplit(p.files, ',')) {
    auto basename = str_tools::get_str_after_last_if_exists(f, '/');
    // Unit name = the FULL stem (strip only the trailing extension, LAST dot).
    // pass.prp_writer names an emitted sibling file by its full internal unit
    // name (`file.entity.prp`) and the top imports it as
    // `import("file.entity.entity")`; a first-dot cut collapses every sibling
    // of a design into one unit name and no `file.entity` unit ever exists.
    // Mirrors `lhd scan` (fs::path::stem) and discover_imports' unit_name_of
    // in lhd_kernel_compile.cpp.
    auto dot            = basename.rfind('.');
    auto basename_noext = dot == std::string_view::npos ? basename : basename.substr(0, dot);

    Prp2lnast converter(f, basename_noext);

    auto lnast = converter.get_lnast();

    var.add(std::move(lnast));
  }
}
