
#include <format>
#include <iostream>
#include <fstream>

#include "invariant.hpp"
#include "pass.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    Pass::error("invariant stats takes exactly one argument");
    exit(1);
  }
  std::string bounds_name = argv[1];

  std::ifstream ifs(bounds_name);
  if (!ifs.good()) {
    Pass::error(std::format("there was an issue opening the file {}", bounds_name));
    exit(2);
  }

  Invariant_boundaries* bound = Invariant_boundaries::deserialize(ifs);

  std::cout << "\n\n#########################################################\n";
  std::cout << std::format("stats on bounds: top {}, hier_sep {}\n\n", bound->top, bound->hierarchical_separator);
  std::cout << "invar_cones\n";
  for (auto& cones : bound->invariant_cones) {
    std::cout << std::format("id: {} count: {}\n", cones.first.first, cones.second.size());
  }

  ifs.close();

  return 0;
}
