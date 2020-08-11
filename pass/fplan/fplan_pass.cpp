#include "fplan_pass.hpp"

void setup_pass_fplan() {
  Livehd_parser::setup();
}

void Livehd_parser::setup() {
  // register the method with lgraph in order to use it
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Livehd_parser::pass);
  register_pass(m);
}

void Livehd_parser::makefp(LGraph *l) {
  std::cout << "making fplan..." << std::endl;
  l->each_node_fast([&](const Node &n) {
    std::cout << n.get_name() << std::endl;
  });
}

void Livehd_parser::pass(Eprp_var &var) {
  Livehd_parser p(var);
  std::cout << "running pass..." << std::endl;
  // loop over each lgraph
  for (auto l : var.lgs) {
    p.makefp(l);
  }

  // make a graph_info thing and call every alg on it
  // 1. print out node information
  // 2. write node information into a Graph_info struct and run code on that
  // 3. <finish HiReg>
  // 4. write code to use the existing hierarchy instead of throwing it away

}