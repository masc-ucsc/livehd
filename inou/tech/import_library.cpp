//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Imports a tech library to lgdb
// Tech library can be in Verilog, LEF or LIB

#include "tech_library.hpp"
#include "tech_options.hpp"

#include "import_verilog.hpp"

void mock() {
  Tech_library *tlib = Tech_library::instance("lgdb");
  if(true) {
    Tech_cell *acell = tlib->get_cell(tlib->create_cell_id("and2X0"));
    acell->add_pin("a", Tech_cell::Direction::input);
    acell->add_pin("b", Tech_cell::Direction::input);
    acell->add_pin("y", Tech_cell::Direction::output);

    acell = tlib->get_cell(tlib->create_cell_id("and2X1"));
    acell->add_pin("a", Tech_cell::Direction::input);
    acell->add_pin("b", Tech_cell::Direction::input);
    acell->add_pin("y", Tech_cell::Direction::output);

    tlib->sync();
  } else {
    tlib->load();
  }
}

int main(int argc, const char **argv) {

  Options::setup(argc, argv);
  Tech_options_pack opack;
  Options::setup_lock();

  mock();
  exit(0);

  if(opack.type == Tech_file_type::Verilog) {
    Import_verilog worker(opack);
    worker.update();
  } else {
    console->error("Tech file type not supported yet!\n");
  }

  return 0;
}
