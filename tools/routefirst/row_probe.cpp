// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "row_solve.hpp"

// V "name" cost site ...; E "from" "to" gap "reason". Coordinates and
// coefficients are integers. The exact result is conditional on these choices.
int main(int argc, char** argv) {
  namespace rf = livehd::routefirst;
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: row_probe row-constraints.txt");
    }
    std::ifstream input(argv[1]);
    if (!input) {
      throw std::runtime_error("cannot open row constraints");
    }
    std::vector<rf::Row_variable>   vars;
    std::vector<rf::Row_constraint> rules;
    std::map<std::string, size_t>   indices;
    std::string                     line;
    while (std::getline(input, line)) {
      if (line.empty() || line.front() == '#') {
        continue;
      }
      std::istringstream row(line);
      char               kind;
      std::string        name;
      if (!(row >> kind >> std::quoted(name))) {
        throw std::invalid_argument("expected a named vertex or constraint");
      }
      if (kind == 'V') {
        rf::Row_variable var{name, {}, 0};
        if (!(row >> var.cost) || !indices.emplace(name, vars.size()).second) {
          throw std::invalid_argument("invalid or duplicate row vertex: " + name);
        }
        while (!(row >> std::ws).eof()) {
          int site;
          if (!(row >> site)) {
            throw std::invalid_argument("invalid site for " + name);
          }
          var.sites.push_back(site);
        }
        vars.push_back(std::move(var));
      } else if (kind == 'E') {
        std::string to, reason;
        int         gap;
        if (!(row >> std::quoted(to) >> gap >> std::quoted(reason)) || !(row >> std::ws).eof()) {
          throw std::invalid_argument("invalid constraint: " + line);
        }
        rules.push_back({indices.at(name), indices.at(to), gap, reason});
      } else {
        throw std::invalid_argument("unknown row record: " + line);
      }
    }
    const auto solution = rf::solve_row(vars, rules);
    if (!solution.feasible) {
      for (const auto& reason : solution.conflict) {
        std::cerr << reason << '\n';
      }
      return 2;
    }
    std::cout << "cost\t" << solution.cost << '\n';
    for (size_t i = 0; i < vars.size(); ++i) {
      std::cout << vars[i].name << '\t' << solution.x[i] << '\n';
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
