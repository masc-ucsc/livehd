// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sat_solver.hpp"

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>
extern "C" {
void* lhd_sat_create(int);
void  lhd_sat_destroy(void*);
void  lhd_sat_variables(void*, unsigned);
void  lhd_sat_clause(void*, const int*, unsigned);
void  lhd_sat_xor(void*, const unsigned*, unsigned, int);
int   lhd_sat_solve(void*, const int*, unsigned, unsigned, unsigned);
int   lhd_sat_value(void*, unsigned);
}
namespace livehd::placement {
Sat_backend sat_backend(const std::string& name) {
  if (name == "cadical") {
    return Sat_backend::cadical;
  }
  if (name == "cms-cnf") {
    return Sat_backend::cms_cnf;
  }
  if (name == "cms-xor") {
    return Sat_backend::cms_xor;
  }
  throw std::invalid_argument("satlegalize backend must be cadical, cms-cnf or cms-xor");
}
Sat_solver::Sat_solver(Sat_backend choice) : solver(lhd_sat_create(choice == Sat_backend::cadical ? 0 : 1)), backend(choice) {}
Sat_solver::~Sat_solver() { lhd_sat_destroy(solver); }
int Sat_solver::new_variable() {
  if (variables == static_cast<unsigned>(std::numeric_limits<int>::max())) {
    throw std::overflow_error("SAT variable limit");
  }
  lhd_sat_variables(solver, ++variables);
  status = Sat_status::unknown;
  return static_cast<int>(variables);
}
void Sat_solver::clause(std::span<const int> literals) {
  for (int lit : literals) {
    if (lit == 0 || lit == std::numeric_limits<int>::min() || static_cast<unsigned>(std::abs(lit)) > variables) {
      throw std::invalid_argument("invalid SAT literal");
    }
  }
  lhd_sat_clause(solver, literals.data(), static_cast<unsigned>(literals.size()));
  status = Sat_status::unknown;
}
void Sat_solver::parity(std::span<const int> positive_variables, bool odd) {
  std::vector<unsigned> native;
  for (int var : positive_variables) {
    if (var <= 0 || static_cast<unsigned>(var) > variables) {
      throw std::invalid_argument("invalid SAT parity variable");
    }
    native.push_back(static_cast<unsigned>(var));
  }
  if (backend == Sat_backend::cms_xor) {
    lhd_sat_xor(solver, native.data(), static_cast<unsigned>(native.size()), odd);
    status = Sat_status::unknown;
    return;
  }
  if (positive_variables.empty()) {
    if (odd) {
      clause({});
    }
    return;
  }
  int previous = positive_variables.front();
  for (size_t i = 1; i < positive_variables.size(); ++i) {
    int next = positive_variables[i], gate = new_variable();
    clause({previous, next, -gate});
    clause({-previous, -next, -gate});
    clause({previous, -next, gate});
    clause({-previous, next, gate});
    previous = gate;
  }
  clause({odd ? previous : -previous});
}
Sat_status Sat_solver::solve(std::span<const int> assumptions, unsigned conflicts, unsigned milliseconds) {
  for (int lit : assumptions) {
    if (lit == 0 || lit == std::numeric_limits<int>::min() || static_cast<unsigned>(std::abs(lit)) > variables) {
      throw std::invalid_argument("invalid SAT assumption");
    }
  }
  if (conflicts == 0 || milliseconds == 0) {
    return status = Sat_status::unknown;
  }
  if (conflicts > static_cast<unsigned>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("SAT conflict limit too large");
  }
  int result    = lhd_sat_solve(solver, assumptions.data(), static_cast<unsigned>(assumptions.size()), conflicts, milliseconds);
  return status = result == 10 ? Sat_status::satisfiable : result == 20 ? Sat_status::unsatisfiable : Sat_status::unknown;
}
bool Sat_solver::value(int variable) const {
  if (status != Sat_status::satisfiable || variable <= 0 || static_cast<unsigned>(variable) > variables) {
    throw std::logic_error("SAT model is unavailable");
  }
  return lhd_sat_value(solver, static_cast<unsigned>(variable)) > 0;
}
}  // namespace livehd::placement
