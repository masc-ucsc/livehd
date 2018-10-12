//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include <fstream>

#include "inou_ramgen.hpp"

#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"

void Inou_ramgen_options::set(const std::string &key, const std::string &value) {

  try {
    if ( is_opt(key,"odir") ) {
      odir = value;
    }else{
      set_val(key,value);
    }
  } catch (const std::invalid_argument& ia) {
    fmt::print("ERROR: key {} has an invalid argument {}\n",key);
  }

  console->info("inou_ramgen odir:{}", odir);
}

Inou_ramgen::Inou_ramgen() {
}

Inou_ramgen::~Inou_ramgen() {
}

std::vector<LGraph *> Inou_ramgen::tolg() {
  assert(false); // generates SRAMs from a lgraph, not
}

void Inou_ramgen::fromlg(std::vector<const LGraph *> &lgs) {

  assert(!opack.odir.empty());

  int total = 0;
  for(const auto g : lgs) {
    for(auto idx : g->fast()) {
      total++;
    }
  }
  fmt::print("total {}\n",total);

}
