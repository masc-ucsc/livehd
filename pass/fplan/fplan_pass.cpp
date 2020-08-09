#include "fplan_pass.hpp"

void setup_pass_fplan() {
  Livehd_parser::setup();
}

Livehd_parser::Livehd_parser(const Eprp_var &var) : Pass("pass.fplan", var) { }

void Livehd_parser::setup() {
  // register the method with lgraph in order to use it
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", Livehd_parser::tofp);
  register_pass(m);
}


void Livehd_parser::tofp(Eprp_var &v) {
  std::cout << "Hi!" << std::endl;

  // make a graph_info thing and call every alg on it
  // 1. print out node information
  // 2. write node information into a Graph_info struct and run code on that
  // 3. <finish HiReg>
  // 4. write code to use the existing hierarchy instead of throwing it away

}