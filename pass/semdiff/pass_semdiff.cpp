// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_semdiff.hpp"

#include <string>
#include <string_view>

#include "diag.hpp"
#include "semdiff.hpp"

using namespace livehd;

static Pass_plugin plugin("pass_semdiff", Pass_semdiff::setup);

Pass_semdiff::Pass_semdiff(const Eprp_var& var) : Pass("pass.semdiff", var) {}

namespace {
bool parse_bool(std::string_view v) { return v != "false" && v != "0" && !v.empty(); }
}  // namespace

void Pass_semdiff::setup() {
  Eprp_method m("pass.semdiff",
                "Structural diff/match: mark corresponding nodes of ref=graph0 and impl=graph1 with a shared `match` id",
                &Pass_semdiff::semdiff);
  m.add_label_optional("alg", "matching engine: structural (v1; region|functional later)", "structural");
  m.add_label_optional("matching_names", "anchor internal flops/mems by hierarchical name", "false");
  m.add_label_optional("id_granularity", "id per matched node-pair (pair) or per connected region (region)", "pair");
  m.add_label_optional("verbose", "print per-side match statistics", "false");
  register_pass(m);
}

void Pass_semdiff::semdiff(Eprp_var& var) {
  if (var.graphs.size() < 2) {
    livehd::diag::err("pass.semdiff", "semdiff-needs-pair", "internal")
        .msg("pass.semdiff needs two designs (ref, impl); got {} graph(s)", var.graphs.size())
        .fatal();
    return;
  }

  semdiff::Semdiff_options o;
  o.alg            = std::string{var.get("alg", "structural")};
  o.matching_names = parse_bool(var.get("matching_names", "false"));
  o.id_granularity = std::string{var.get("id_granularity", "pair")};
  o.verbose        = parse_bool(var.get("verbose", "false"));

  semdiff::structural_match(var.graphs[0].get(), var.graphs[1].get(), o);
}
