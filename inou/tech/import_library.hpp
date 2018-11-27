//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef IMPORT_LIBRARY_H
#define IMPORT_LIBRARY_H

#include "core/tech_library.hpp"
#include "tech_options.hpp"

class Import_library {

protected:
  Tech_options_pack opack;

public:
  Import_library(Tech_options_pack opack)
      : opack(opack) {
  }

  // read tech file and updates the tech library
  virtual void update() = 0;
};

#endif
