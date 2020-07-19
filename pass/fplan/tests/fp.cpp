#include <iostream>

#include "iassert.hpp"
#include "json_inou.hpp"
#include "hier_tree.hpp"

// this is a mini-testbed, currently.

int main() {
	std::cout << "starting floorplan." << std::endl;
  
  std::cout << "loading netlist and organizing hierarchy...";
  Json_inou_parser p("/home/kneil/code/real/fp/test/hier_test.json");
	std::cout << "done." << std::endl;

  Hier_tree t = p.make_tree();
  t.set_num_components(1);
  t.set_min_node_area(0.0); // don't collapse anything
  
  std::cout << "discovering hierarchies...";
  //t.discover_hierarchy();
  std::cout << "done." << std::endl;

  std::cout << "finished floorplan." << std::endl;
}
