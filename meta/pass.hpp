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

  // Transform enhance/changes the lgraph in-situ
  // FIXME: maybe call it "transform"
  virtual void    trans(LGraph *orig) = 0;

  // regenerate, creates a new lgraph db
  // FIXME: it should get a set of lgraphs and return a new set of lgraphs
  // FIXME: the names are not clear. Maybe call "replace"
  virtual LGraph *regen(const LGraph *orig) = 0;

  // Set options for the pass
  virtual void set(const std::string &key, const std::string &value) = 0;
};

#endif
