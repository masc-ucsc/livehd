//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef IMPORT_VERILOG_H
#define IMPORT_VERILOG_H

#include "import_library.hpp"

class Import_verilog : public Import_library {

private:
  Tech_library *tlib;

public:
  Import_verilog(Tech_options_pack opack) : Import_library(opack) {
    tlib = Tech_library::instance(opack.lgdb_path);
  }

  void update() override;
};

#endif
