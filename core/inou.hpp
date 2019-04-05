//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef INOU_H
#define INOU_H

#include "lgraph.hpp"
#include "options.hpp"

// Abstract class that any inou block must support
class Inou {
private:
protected:
public:
  Inou() {}

  virtual std::vector<LGraph *> tolg() = 0;
  // Output modules like to verilog target
  virtual void fromlg(std::vector<LGraph *> &out) = 0;
  // Set the options/arguments for the pass
  virtual void set(const std::string &key, const std::string &value) = 0;
};

#endif
