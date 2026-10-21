// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Compiled into cvc5's combined object BEFORE its solver symbols are renamed.
// Only this C ABI crosses that boundary; no private solver types escape.
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "satheaders/cadical/src/cadical.hpp"
#include "satheaders/cms/src/cryptominisat.h"

struct Lhd_sat_deadline : CaDiCaL::Terminator {
  std::chrono::steady_clock::time_point end;
  bool                                  terminate() override { return std::chrono::steady_clock::now() >= end; }
};
struct Lhd_sat {
  // A connected callback must outlive the solver that holds its pointer.
  Lhd_sat_deadline                  deadline;
  std::unique_ptr<CaDiCaL::Solver>  cad;
  std::unique_ptr<CMSat::SATSolver> cms;
  unsigned                          variables = 0;
};
extern "C" {
void* lhd_sat_create(int backend) {
  auto* solver = new Lhd_sat;
  if (backend == 0) {
    solver->cad = std::make_unique<CaDiCaL::Solver>();
    solver->cad->set("quiet", 1);
    solver->cad->connect_terminator(&solver->deadline);
  } else {
    solver->cms = std::make_unique<CMSat::SATSolver>();
    solver->cms->set_num_threads(1);
    solver->cms->set_verbosity(0);
    solver->cms->set_no_bve();
    solver->cms->set_no_equivalent_lit_replacement();
  }
  return solver;
}
void lhd_sat_destroy(void* opaque) { delete static_cast<Lhd_sat*>(opaque); }
void lhd_sat_variables(void* opaque, unsigned count) {
  auto& s = *static_cast<Lhd_sat*>(opaque);
  if (s.cms) {
    s.cms->new_vars(count - s.variables);
  } else {
    s.cad->reserve(static_cast<int>(count));
  }
  s.variables = count;
}
void lhd_sat_clause(void* opaque, const int* lits, unsigned count) {
  auto& s = *static_cast<Lhd_sat*>(opaque);
  if (s.cad) {
    for (unsigned i = 0; i < count; ++i) {
      s.cad->add(lits[i]);
    }
    s.cad->add(0);
  } else {
    std::vector<CMSat::Lit> clause;
    for (unsigned i = 0; i < count; ++i) {
      clause.emplace_back(static_cast<unsigned>(std::abs(lits[i]) - 1), lits[i] < 0);
    }
    s.cms->add_clause(clause);
  }
}
void lhd_sat_xor(void* opaque, const unsigned* vars, unsigned count, int rhs) {
  auto&                 s = *static_cast<Lhd_sat*>(opaque);
  std::vector<unsigned> clause;
  for (unsigned i = 0; i < count; ++i) {
    clause.push_back(vars[i] - 1);
  }
  s.cms->add_xor_clause(clause, rhs != 0);
}
int lhd_sat_solve(void* opaque, const int* assumptions, unsigned count, unsigned conflicts, unsigned milliseconds) {
  auto& s = *static_cast<Lhd_sat*>(opaque);
  if (s.cad) {
    s.deadline.end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    s.cad->limit("conflicts", static_cast<int>(conflicts));
    for (unsigned i = 0; i < count; ++i) {
      s.cad->assume(assumptions[i]);
    }
    return s.cad->solve();
  }
  std::vector<CMSat::Lit> lits;
  for (unsigned i = 0; i < count; ++i) {
    lits.emplace_back(static_cast<unsigned>(std::abs(assumptions[i]) - 1), assumptions[i] < 0);
  }
  s.cms->set_max_confl(conflicts);
  s.cms->set_max_time(milliseconds / 1000.0);
  const auto result = s.cms->solve(&lits);
  return result == CMSat::l_True ? 10 : result == CMSat::l_False ? 20 : 0;
}
int lhd_sat_value(void* opaque, unsigned var) {
  auto& s = *static_cast<Lhd_sat*>(opaque);
  if (s.cad) {
    return s.cad->val(static_cast<int>(var)) > 0 ? 1 : -1;
  }
  const auto value = s.cms->get_model().at(var - 1);
  return value == CMSat::l_True ? 1 : value == CMSat::l_False ? -1 : 0;
}
}
