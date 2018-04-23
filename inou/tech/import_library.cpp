
// Imports a tech library to lgdb
// Tech library can be in Verilog, LEF or LIB

#include "tech_library.hpp"
#include "tech_options.hpp"

#include "import_verilog.hpp"

void mock() {
  Tech_library* tlib = Tech_library::instance("lgdb");
  if(true) {
    Tech_cell* acell = tlib->get_cell(tlib->create_cell_id("and2X0"));
    uint8_t a = acell->add_pin("a");
    uint8_t b = acell->add_pin("b");
    uint8_t y = acell->add_pin("y");

    acell->set_direction(a, Tech_cell::Direction::input);
    acell->set_direction(b, Tech_cell::Direction::input);
    acell->set_direction(y, Tech_cell::Direction::output);

    acell = tlib->get_cell(tlib->create_cell_id("and2X1"));
    a = acell->add_pin("a");
    b = acell->add_pin("b");
    y = acell->add_pin("y");

    acell->set_direction(a, Tech_cell::Direction::input);
    acell->set_direction(b, Tech_cell::Direction::input);
    acell->set_direction(y, Tech_cell::Direction::output);


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

