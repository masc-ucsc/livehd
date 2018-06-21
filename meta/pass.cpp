//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <boost/filesystem.hpp>
#include <vector>

#include "pass.hpp"

std::map<std::string, Pass *> Pass::passes;

Pass::Pass(std::string name) {
  passes[name] = this;
}
