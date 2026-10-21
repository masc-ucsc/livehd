// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <initializer_list>
#include <span>
#include <string>

namespace livehd::placement {
enum class Sat_backend { cadical, cms_cnf, cms_xor };
enum class Sat_status { unknown, satisfiable, unsatisfiable };
// DIMACS literals (positive, one-based variables). Each solve replaces its
// assumptions; persistent clauses and learned information remain in the solver.
class Sat_solver {
  void*       solver;
  Sat_backend backend;
  unsigned    variables = 0;
  Sat_status  status    = Sat_status::unknown;

public:
  explicit Sat_solver(Sat_backend choice);
  ~Sat_solver();
  Sat_solver(const Sat_solver&)            = delete;
  Sat_solver& operator=(const Sat_solver&) = delete;
  int         new_variable();
  void        clause(std::span<const int> literals);
  void        clause(std::initializer_list<int> literals) { clause(std::span(literals.begin(), literals.size())); }
  void        parity(std::span<const int> positive_variables, bool odd);
  Sat_status  solve(std::span<const int> assumptions = {}, unsigned conflicts = 10000, unsigned milliseconds = 50);
  bool        value(int variable) const;
};
Sat_backend sat_backend(const std::string& name);
}  // namespace livehd::placement
