// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_semdiff.hpp"

#include <cstdlib>
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
  m.add_label_optional("state_pairing", "tier-2: full-match (SRP/ERP signature) pairing of name-unmatched state cells", "false");
  m.add_label_optional("id_granularity", "id per matched node-pair (pair) or per connected region (region)", "pair");
  m.add_label_optional("verbose", "print per-side match statistics", "false");
  m.add_label_optional("dump_state", "print per-state-cell pairing outcome (matcher iteration aid)", "false");
  m.add_label_optional("hier", "sweep every def pair (entity-paired; --top scopes to its subtree) and aggregate state stats", "false");
  m.add_label_optional("save", "save both lg: libraries back (default: on for single-pair, off for hier)", "");
  m.add_label_optional("name_noise", "experiment: destroy this fraction of impl-side state keys before tier-1 (0..1)", "0");
  m.add_label_optional("noise_seed", "selects which keys name_noise destroys (deterministic)", "1");
  m.add_label_optional("synalign_maxiter", "tier-2 fixed-point round cap (default 64 = converge; 1 = no propagation)", "64");
  m.add_label_optional("explain_noise", "deep-dump up to N noised-but-unrecovered cells (twin signature diff + bucket)", "0");
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
  o.state_pairing  = parse_bool(var.get("state_pairing", "false"));
  o.dump_state     = parse_bool(var.get("dump_state", "false"));
  o.id_granularity = std::string{var.get("id_granularity", "pair")};
  o.verbose        = parse_bool(var.get("verbose", "false"));
  o.name_noise     = std::strtod(std::string{var.get("name_noise", "0")}.c_str(), nullptr);
  o.noise_seed     = std::strtoull(std::string{var.get("noise_seed", "1")}.c_str(), nullptr, 10);
  o.synalign_maxiter = static_cast<uint32_t>(std::strtoul(std::string{var.get("synalign_maxiter", "64")}.c_str(), nullptr, 10));

  semdiff::structural_match(var.graphs[0].get(), var.graphs[1].get(), o);
}
