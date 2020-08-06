#include <iostream>
#include <chrono>

#include "json_inou.hpp"
#include "hier_tree.hpp"

// this is a mini-testbed, currently.
int main() {
  using namespace std::chrono;

	std::cout << "starting floorplan." << std::endl;
  
  std::cout << "loading netlist and organizing hierarchy...";
  auto t1 = high_resolution_clock::now();
  
  Json_inou_parser p("/home/kneil/code/real/livehd/pass/fplan/tests/hier_test.json");
  auto gi = p.make_tree();
  
  auto t2 = high_resolution_clock::now();
	std::cout << "done (" << duration_cast<milliseconds>(t2 - t1).count() << " ms)." << std::endl;
  
  std::cout << "discovering hierarchies...";
  t1 = high_resolution_clock::now();
  
  Hier_tree t(std::move(gi), 1);
  
  t2 = high_resolution_clock::now();
	std::cout << "done (" << duration_cast<milliseconds>(t2 - t1).count() << " ms)." << std::endl;
  
  std::cout << "collapsing trees...";
  t1 = high_resolution_clock::now();
  
  for (double thresh = 0.0; thresh <= 0.08; thresh += 0.02) {
    t.collapse(thresh);
  }
  
  t2 = high_resolution_clock::now();
	std::cout << "done (" << duration_cast<milliseconds>(t2 - t1).count() << " ms)." << std::endl;
  
  t.print();

  std::cout << "finished floorplan." << std::endl;
}
