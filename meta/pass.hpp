//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_H
#define PASS_H

#include "lgraph.hpp"
#include "options.hpp"

// Abstract class that any Pass block must support
class Pass {
private:
protected:
public:
  Pass() {
  };

  // Either trans or regen called

  virtual void    trans(LGraph *orig) {
    assert(false);
  }

  virtual LGraph *regen(const LGraph *orig) {
    assert(false);
    return 0;
  }

  // Set options for the pass
  virtual void set(const std::string &key, const std::string &value) {
    assert(false); // Overload if used
  };
};

#endif
