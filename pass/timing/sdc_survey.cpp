// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Reads each SDC named on the command line and prints what the typed reader could not honor.
#include <fstream>
#include <iostream>
#include <sstream>

#include "sdc.hpp"

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::ifstream     in(argv[i]);
    std::stringstream text;
    text << in.rdbuf();
    const auto sdc = livehd::timing::fast::read_sdc(text.str());
    std::cout << argv[i] << ": " << sdc.clocks.size() << " clocks, " << sdc.delays.size() << " delays, " << sdc.loads.size()
              << " loads, " << sdc.false_paths.size() << " false paths\n";
    for (const auto& u : sdc.unsupported) {
      std::cout << "    unsupported: " << u << "\n";
    }
  }
}
