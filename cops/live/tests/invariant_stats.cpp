
#include <fstream>

#include "pass.hpp"

#include "invariant.hpp"

int main(int argc, char** argv) {

  if(argc != 2) {
    Pass::error("invariant stats takes exactly one argument");
    exit(1);
  }
  std::string bounds_name = argv[1];

  std::ifstream ifs(bounds_name);
  if(!ifs.good()) {
    Pass::error(fmt::format("there was an issue opening the file {}", bounds_name));
    exit(2);
  }

  Invariant_boundaries* bound = Invariant_boundaries::deserialize(ifs);

  fmt::print("\n\n#########################################################\n");
  fmt::print("stats on bounds: top {}, hier_sep {}\n\n", bound->top, bound->hierarchical_separator);
  fmt::print("invar_cones\n");
  for(auto & cones : bound->invariant_cones) {
    fmt::print("id: {} count: {}\n", cones.first.first, cones.second.size());
  }

  ifs.close();

  return 0;
}

