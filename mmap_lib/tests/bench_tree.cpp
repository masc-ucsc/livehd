
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <assert.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <numeric>
#include <chrono>
#include <algorithm>
#include <ctime>

#include "lbench.hpp"
#include "mmap_tree.hpp"

class TreeBench {
public:
    mmap_lib::tree<std::string> tree1;
    mmap_lib::tree<std::string> tree2;

    void set1() {

        // Smaller tree, complete binary tree with 15 nodes
        tree1.set_root("tree1.root");
        auto n10 = tree1.add_child(mmap_lib::Tree_index(0, 0), "n1.0");
        auto n11 = tree1.add_child(mmap_lib::Tree_index(0, 0), "n1.1");
        auto n20 = tree1.add_child(mmap_lib::Tree_index(1, 0), "n2.0");
        auto n21 = tree1.add_child(mmap_lib::Tree_index(1, 0), "n2.1");
        auto n24 = tree1.add_child(mmap_lib::Tree_index(1, 1), "n2.4");
        auto n25 = tree1.add_child(mmap_lib::Tree_index(1, 1), "n2.5");

        auto n30 = tree1.add_child(mmap_lib::Tree_index(2, 0), "n3.0");
        auto n31 = tree1.add_child(mmap_lib::Tree_index(2, 0), "n3.1");
        auto n34 = tree1.add_child(mmap_lib::Tree_index(2, 1), "n3.4");
        auto n35 = tree1.add_child(mmap_lib::Tree_index(2, 1), "n3.5");
        auto n38 = tree1.add_child(mmap_lib::Tree_index(2, 4), "n3.8");
        auto n39 = tree1.add_child(mmap_lib::Tree_index(2, 4), "n3.9");
        auto n313 = tree1.add_child(mmap_lib::Tree_index(2, 5), "n3.12");
        auto n312 = tree1.add_child(mmap_lib::Tree_index(2, 5), "n3.13");
    };

    void set2() {
      // Larger tree
      tree2.set_root("tree2.root");
      auto c11 = tree2.add_child(mmap_lib::Tree_index(0, 0), "child1.0");
      auto c12 = tree2.add_child(mmap_lib::Tree_index(0, 0), "child1.2");
      auto c111 = tree2.add_child(c11, "child1.0.0");
      auto c112 = tree2.add_child(c11, "child1.0.1");
      auto c1111 = tree2.add_child(c111, "child1.0.0.0");

      mmap_lib::Tree_index s;
      s = tree2.insert_next_sibling(c1111, "child1.0.0.6");
      s = tree2.insert_next_sibling(c1111, "child1.0.0.5");
      s = tree2.insert_next_sibling(c1111, "child1.0.0.4");
      s = tree2.insert_next_sibling(c1111, "child1.0.0.3");
      s = tree2.insert_next_sibling(c1111, "child1.0.0.2");
      s = tree2.insert_next_sibling(c1111, "child1.0.0.1");

      auto c121 = tree2.add_child(c12, "child1.2.0");
      auto c122 = tree2.add_child(c12, "child1.2.1");
      auto c123 = tree2.add_child(c12, "child1.2.2");
      auto c113 = tree2.add_child(c11, "child1.0.3");
      tree2.add_child(c113, "child1.0.3.0");
      tree2.add_child(c113, "child1.0.3.1");
      tree2.add_child(c113, "child1.0.3.2");

      s = tree2.add_child(c112, "child1.0.1.0");
      s = tree2.add_child(c112, "child1.0.1.1");
      tree2.insert_next_sibling(s, "child1.0.1.6");
      s = tree2.insert_next_sibling(s, "child1.0.1.2");
      tree2.add_child(s, "child1.0.1.2.0");
      tree2.add_child(s, "child1.0.1.2.1");
      s = tree2.insert_next_sibling(s, "child1.0.1.3");
      s = tree2.insert_next_sibling(s, "child1.0.1.4");
      s = tree2.insert_next_sibling(s, "child1.0.1.5");
      s = tree2.add_child(c112, "child1.0.1.7");
      tree2.insert_next_sibling(s, "child1.0.1.16");
      s = tree2.insert_next_sibling(s, "child1.0.1.8");
      tree2.insert_next_sibling(s, "child1.0.1.15");
      tree2.insert_next_sibling(s, "child1.0.1.14");
      s = tree2.insert_next_sibling(s, "child1.0.1.9");
      tree2.insert_next_sibling(s, "child1.0.1.13");
      tree2.insert_next_sibling(s, "child1.0.1.12");
      tree2.insert_next_sibling(s, "child1.0.1.11");
      s = tree2.insert_next_sibling(s, "child1.0.1.10");

      auto c114 = tree2.insert_next_sibling(c113, "child1.0.4");
      tree2.add_child(c113, "child1.0.3.3");
      tree2.add_child(c113, "child1.0.3.4");
      tree2.add_child(c113, "child1.0.3.5");
      auto c115 = tree2.append_sibling(c113, "child1.0.5");
      auto c116 = tree2.append_sibling(c115, "child1.0.6");
      auto c13 = tree2.append_sibling(c12, "child1.3");
      tree2.insert_next_sibling(c112, "child1.0.2");
      tree2.insert_next_sibling(c11, "child1.1");
    };
};



int main() {
    typedef std::chrono::duration<float> float_sec;

    enum { times = 10000, max_times = 100000 };
    auto ts = std::chrono::high_resolution_clock::now();
    float_sec total_time = (ts - ts);

    for (size_t num = times; num <= max_times; num += times) {
      for (auto i = 0; i < num; i++)
      {
          // Recreate the tree every loop
          TreeBench tbench;
          tbench.set1();
          // tbench.set2();

          // tbench.tree1.dump_data();
          // tbench.tree2.dump_data();

          // record start time
          auto tstart = std::chrono::high_resolution_clock::now();

          // Testing delete_subtree and delete_leaf on the constructed trees
          // Delete 7 nodes of the 15 total nodes in tree1
          tbench.tree1.delete_subtree(mmap_lib::Tree_index(1,0));

          // Delete 3 nodes of the 15 total nodes in tree1
          // tbench.tree1.delete_subtree(mmap_lib::Tree_index(2,0));

          // tbench.tree2.delete_subtree(mmap_lib::Tree_index(2,0));

          // record end time
          auto tend = std::chrono::high_resolution_clock::now();
          std::chrono::duration<float> diff = (tend - tstart);
          total_time += diff;

          // tbench.tree1.dump_data();
          // tbench.tree2.dump_data();
      }
    std::cout << "{" << num << " Runs} Total Time:\t" << total_time.count() << "s = ";
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count() << "ms" << std::endl;
    // std::cout << std::chrono::duration_cast<std::chrono::microseconds>(total_time).count() << "us" << std::endl;
    // std::cout << std::chrono::duration_cast<std::chrono::nanoseconds>(total_time).count() << "ns" << std::endl;

    total_time -= total_time;
    }
}
