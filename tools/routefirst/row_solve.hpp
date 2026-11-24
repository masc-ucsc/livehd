// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace livehd::routefirst {
// A variable is a cell X or a pin-piece end. Supply exactly the legal sites
// for this attachment and track choice, in increasing order. Fixed bodies
// have singleton domains. Costs are signed wire-length coefficients.
struct Row_variable {
  std::string      name;
  std::vector<int> sites;
  int              cost = 0;
};
struct Row_constraint {
  size_t      from, to;
  int         gap;  // x[to] >= x[from] + gap
  std::string reason;
};
struct Row_solution {
  bool                     feasible = false;
  std::vector<int>         x;
  int64_t                  cost = 0;
  std::vector<std::string> conflict;
};
Row_solution solve_row(const std::vector<Row_variable>&, const std::vector<Row_constraint>&);
}  // namespace livehd::routefirst
