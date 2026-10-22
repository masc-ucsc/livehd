// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sat_solver.hpp"

#include <algorithm>
#include <array>
#include <bit>
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
int   lhd_sat_failed(void*, int);
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
bool Sat_solver::failed(int literal) const {
  if (status != Sat_status::unsatisfiable || literal == 0 || literal == std::numeric_limits<int>::min()
      || static_cast<unsigned>(std::abs(literal)) > variables) {
    throw std::logic_error("SAT conflict core is unavailable");
  }
  return lhd_sat_failed(solver, literal) != 0;
}
bool Sat_solver::value(int variable) const {
  if (status != Sat_status::satisfiable || variable <= 0 || static_cast<unsigned>(variable) > variables) {
    throw std::logic_error("SAT model is unavailable");
  }
  return lhd_sat_value(solver, static_cast<unsigned>(variable)) > 0;
}
Sat_choice_cost::Sat_choice_cost(Sat_solver& sat, const std::vector<std::vector<std::pair<int, uint64_t>>>& groups) : solver(sat) {
  std::vector<std::vector<int>> words;
  for (const auto& group : groups) {
    uint64_t maximum = 0;
    for (const auto& [literal, weight] : group) {
      if (literal <= 0) {
        throw std::invalid_argument("SAT choice costs require positive candidate variables");
      }
      maximum = std::max(maximum, weight);
    }
    if (maximum > std::numeric_limits<uint64_t>::max() - max_cost) {
      throw std::overflow_error("SAT choice cost overflow");
    }
    max_cost += maximum;
    std::vector<int> word;
    for (int bit = 0; bit < std::bit_width(maximum); ++bit) {
      std::vector<int> choices;
      for (const auto& [literal, weight] : group) {
        if ((weight >> bit) & 1) {
          choices.push_back(literal);
        }
      }
      if (choices.empty()) {
        word.push_back(0);
      } else if (choices.size() == 1) {
        word.push_back(choices.front());
      } else {
        const int gate = solver.new_variable();
        for (int choice : choices) {
          solver.clause({-choice, gate});
        }
        choices.push_back(-gate);
        solver.clause(choices);
        word.push_back(gate);
      }
    }
    if (!word.empty()) {
      words.push_back(std::move(word));
    }
  }
  const auto xor_bit = [&](int a, int b) {
    if (!a || !b) {
      return a ? a : b;
    }
    const int gate = solver.new_variable();
    solver.parity(std::array{a, b, gate}, false);
    return gate;
  };
  const auto carry_bit = [&](int a, int b, int c) {
    if (!a) {
      std::swap(a, c);
    }
    if (!b) {
      std::swap(b, c);
    }
    if (!a || !b) {
      return 0;
    }
    const int gate = solver.new_variable();
    if (!c) {
      solver.clause({-a, -b, gate});
      solver.clause({a, -gate});
      solver.clause({b, -gate});
    } else {
      solver.clause({-a, -b, gate});
      solver.clause({-a, -c, gate});
      solver.clause({-b, -c, gate});
      solver.clause({a, b, -gate});
      solver.clause({a, c, -gate});
      solver.clause({b, c, -gate});
    }
    return gate;
  };
  // Balanced addition keeps the encoding linear in endpoints times cost bits,
  // instead of expanding each DBU of displacement into a cardinality literal.
  while (words.size() > 1) {
    std::vector<std::vector<int>> next;
    for (size_t i = 0; i < words.size(); i += 2) {
      if (i + 1 == words.size()) {
        next.push_back(std::move(words[i]));
        break;
      }
      const auto&      a = words[i];
      const auto&      b = words[i + 1];
      std::vector<int> sum;
      int              carry = 0;
      for (size_t bit = 0; bit < std::max(a.size(), b.size()); ++bit) {
        const int x = bit < a.size() ? a[bit] : 0, y = bit < b.size() ? b[bit] : 0;
        sum.push_back(xor_bit(xor_bit(x, y), carry));
        carry = carry_bit(x, y, carry);
      }
      if (carry) {
        sum.push_back(carry);
      }
      next.push_back(std::move(sum));
    }
    words = std::move(next);
  }
  if (!words.empty()) {
    bits = std::move(words.front());
  }
}
int Sat_choice_cost::at_most(uint64_t bound) {
  const int activation = solver.new_variable();
  if (bound >= max_cost) {
    return activation;
  }
  // Exclude the first higher bit where sum=1 and bound=0. Guard each clause
  // so changing a bound does not permanently restrict later SAT trials.
  for (size_t bit = 0; bit < bits.size(); ++bit) {
    if (!bits[bit] || (bit < 64 && ((bound >> bit) & 1))) {
      continue;
    }
    std::vector<int> clause{-activation, -bits[bit]};
    bool             tautology = false;
    for (size_t higher = bit + 1; higher < bits.size(); ++higher) {
      const bool one = higher < 64 && ((bound >> higher) & 1);
      if (one && !bits[higher]) {
        tautology = true;
        break;
      }
      if (bits[higher]) {
        clause.push_back(one ? -bits[higher] : bits[higher]);
      }
    }
    if (!tautology) {
      solver.clause(clause);
    }
  }
  return activation;
}
}  // namespace livehd::placement
