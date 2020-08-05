#include <iostream>

#include "json_inou.hpp"
#include "hier_tree.hpp"

// this is a mini-testbed, currently.
int main() {
	std::cout << "starting floorplan." << std::endl;
  
  std::cout << "loading netlist and organizing hierarchy...";
  Json_inou_parser p("/home/kneil/code/real/livehd/pass/fplan/tests/hier_test.json");
  auto gi = p.make_tree();
	std::cout << "done." << std::endl;
  
  std::cout << "discovering hierarchies...";
  Hier_tree t(std::move(gi), 1);
  std::cout << "done." << std::endl;
  
  std::cout << "collapsing trees...";
  t.collapse(0.01);
  std::cout << "done." << std::endl;
  t.print();

  std::cout << "finished floorplan." << std::endl;
}
