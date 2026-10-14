// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cvc5/cvc5.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace livehd::lec {
struct Solver_query_ctx {
  bool      has_mem         = false;
  bool      has_boxes       = false;
  bool      witness         = true;
  bool      compiler_budget = false;
  bool      int_blast       = false;
  long long timeout_ms      = 0;
};
struct Solver_profile {
  std::string                                      name;
  std::vector<std::pair<std::string, std::string>> options;
  bool                                             incremental      = false;
  bool                                             needs_models_off = false;
  bool                                             pure_bv_only     = false;
  bool                                             arrays_ok        = true;
};
struct Solver_arm {
  std::string engine, profile;
};
std::vector<Solver_arm>            parse_portfolio(std::string_view text);
const std::vector<Solver_profile>& solver_profiles();
const Solver_profile&              solver_profile(std::string_view name);
bool                               profile_eligible(const Solver_profile& profile, const Solver_query_ctx& ctx);
void configure_solver(cvc5::Solver& solver, const Solver_profile& profile, const Solver_query_ctx& ctx);
}  // namespace livehd::lec
