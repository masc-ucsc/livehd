
#include <vector>
#include <boost/filesystem.hpp>

#include "pass.hpp"

std::map<std::string,Pass *> Pass::passes;

Pass::Pass(std::string name) {
  passes[name] = this;
}

