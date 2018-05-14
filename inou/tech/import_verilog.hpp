
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
