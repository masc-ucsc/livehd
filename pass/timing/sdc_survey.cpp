// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Reads each SDC named on the command line, alone (no design: every object a query names is taken to exist),
// and prints what the typed reader holds and what it could not honor.
#include <fstream>
#include <iostream>
#include <sstream>

#include "sdc.hpp"

int main(int argc, char** argv) {
  size_t clean = 0;
  for (int i = 1; i < argc; ++i) {
    std::ifstream     in(argv[i]);
    std::stringstream text;
    text << in.rdbuf();
    const auto sdc = livehd::timing::fast::read_sdc(text.str());
    std::cout << argv[i] << ": " << sdc.clocks.size() << " clocks, " << sdc.delays.size() << " delays, " << sdc.loads.size() << " loads, "
              << sdc.drives.size() << " driving cells, " << sdc.false_paths.size() << " false paths, " << sdc.clock_groups.size()
              << " clock groups" << (sdc.max_fanout ? ", max fanout" : "") << "\n";
    for (const auto& u : sdc.unsupported) {
      std::cout << "    unsupported: " << u << "\n";
    }
    clean += sdc.unsupported.empty();
  }
  std::cout << clean << " of " << argc - 1 << " files without an unsupported item\n";
}
