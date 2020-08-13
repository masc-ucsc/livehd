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
	std::cout << " done (" << duration_cast<milliseconds>(t2 - t1).count() << " ms)." << std::endl;
  
  int m_size = 1;
  std::cout << "discovering hierarchies (minimum component size " << m_size << ")...";
  t1 = high_resolution_clock::now();
  
  Hier_tree t(std::move(gi), m_size);
  
  t2 = high_resolution_clock::now();
	std::cout << " done (" << duration_cast<milliseconds>(t2 - t1).count() << " ms)." << std::endl;
  
  for (double thresh = 0.01; thresh <= 2.0; thresh *= 2.0) {
    std::cout << "collapsing tree (minimum area " << thresh << " mm^2)...";
    t1 = high_resolution_clock::now();

    t.collapse(thresh);

    t2 = high_resolution_clock::now();
    std::cout << " done (" << duration_cast<milliseconds>(t2 - t1).count() << " ms)." << std::endl;
  }
  
  t.print();

  std::cout << "finished floorplan." << std::endl;
}
