// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "solver_profile.hpp"

#include <stdexcept>

namespace livehd::lec {
const std::vector<Solver_profile>& solver_profiles() {
  static const std::vector<Solver_profile> profiles = {
      {"internal", {}, false, false, false, true},
      {"lazy",
       {{"bv-solver", "bitblast"},
        {"bitblast", "lazy"},
        {"bv-sat-solver", "cadical"},
        {"bv-abstraction", "true"},
        {"bv-abstraction-size", "8"}}},
      {"eager-kissat", {{"bv-solver", "bitblast"}, {"bitblast", "eager"}, {"bv-sat-solver", "kissat"}}, false, true, true, false},
      {"eager-cms",
       {{"bv-solver", "bitblast"}, {"bitblast", "eager"}, {"bv-sat-solver", "cryptominisat"}},
       false, true,
       true, false},
      {"intblast", {{"bv-solver", "bitblast"}, {"bitblast", "lazy"}, {"solve-bv-as-int", "iand"}}},
      {"witness", {{"produce-models", "true"}}},
      {"verify", {}, true, false, false, true},
  };
  return profiles;
}
const Solver_profile& solver_profile(std::string_view name) {
  if (name.empty() || name == "auto") {
    name = "internal";
  }
  for (const auto& profile : solver_profiles()) {
    if (profile.name == name) {
      return profile;
    }
  }
  throw std::invalid_argument("unknown solver profile '" + std::string(name) + "'");
}
std::vector<Solver_arm> parse_portfolio(std::string_view text) {
  std::vector<Solver_arm> arms;
  while (!text.empty()) {
    const auto comma = text.find(',');
    const auto entry = text.substr(0, comma);
    const auto colon = entry.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == entry.size()) {
      throw std::invalid_argument("formal.portfolio expects engine:profile entries");
    }
    Solver_arm arm{std::string(entry.substr(0, colon)), std::string(entry.substr(colon + 1))};
    if (arm.engine != "bmc" && arm.engine != "ind") {
      throw std::invalid_argument("portfolio engines are bmc or ind");
    }
    if (solver_profile(arm.profile).incremental) {
      throw std::invalid_argument("verify is not a LEC portfolio profile");
    }
    arms.push_back(std::move(arm));
    if (comma == std::string_view::npos) {
      break;
    }
    text.remove_prefix(comma + 1);
    if (text.empty()) {
      throw std::invalid_argument("formal.portfolio has an empty arm");
    }
  }
  if (arms.empty()) {
    throw std::invalid_argument("formal.portfolio must contain at least one arm");
  }
  return arms;
}
bool profile_eligible(const Solver_profile& p, const Solver_query_ctx& c) {
  return !(p.needs_models_off && c.witness) && !(p.pure_bv_only && (c.has_mem || c.has_boxes || c.int_blast))
         && (p.arrays_ok || !c.has_mem);
}
void configure_solver(cvc5::Solver& solver, const Solver_profile& p, const Solver_query_ctx& c) {
#ifndef LIVEHD_CVC5_SOURCE_BUILD
  if (p.name == "eager-kissat" || p.name == "eager-cms") {
    throw std::invalid_argument("eager profiles require the source build with optional backends and the fresh-assumption adapter");
  }
#endif
  if (!profile_eligible(p, c)) {
    throw std::invalid_argument("solver profile '" + p.name + "' is incompatible with this query");
  }
  solver.setOption("incremental", p.incremental ? "true" : "false");
  // The compiler's deterministic resource tier stays on MiniSat until the
  // reproducibility experiment establishes a portable CaDiCaL allowance.
  solver.setOption("sat-solver", c.compiler_budget ? "minisat" : "cadical");
  if (p.name == "internal" || p.name == "witness" || p.name == "verify") {
    solver.setOption("bv-solver", !c.has_mem && !c.has_boxes && !c.int_blast ? "bitblast-internal" : "bitblast");
    if (c.has_mem || c.has_boxes || c.int_blast) {
      solver.setOption("bitblast", "lazy");
    }
  }
  solver.setOption("arrays-prop", "1");
  if (c.has_mem) {
    solver.setOption("arrays-exp", "true");
  }
  if (c.witness) {
    solver.setOption("produce-models", "true");
  }
  if (c.timeout_ms > 0) {
    solver.setOption("tlimit-per", std::to_string(c.timeout_ms));
  }
  for (const auto& [key, value] : p.options) {
    solver.setOption(key, value);
  }
}
}  // namespace livehd::lec
