#include <iostream>
#include <mockturtle/algorithms/cleanup.hpp>
#include <mockturtle/algorithms/collapse_mapped.hpp>
#include <mockturtle/algorithms/cut_enumeration.hpp>
#include <mockturtle/algorithms/lut_mapping.hpp>
#include <mockturtle/algorithms/node_resynthesis.hpp>
#include <mockturtle/algorithms/node_resynthesis/akers.hpp>
#include <mockturtle/algorithms/node_resynthesis/direct.hpp>
#include <mockturtle/algorithms/node_resynthesis/mig_npn.hpp>
#include <mockturtle/algorithms/node_resynthesis/xmg_npn.hpp>
#include <mockturtle/algorithms/refactoring.hpp>
#include <mockturtle/algorithms/resubstitution.hpp>
#include <mockturtle/generators/arithmetic.hpp>
#include <mockturtle/io/write_bench.hpp>
#include <mockturtle/networks/aig.hpp>
#include <mockturtle/networks/klut.hpp>
#include <mockturtle/networks/mig.hpp>
#include <mockturtle/views/mapping_view.hpp>
#include <sstream>

using namespace mockturtle;

int main() {
  klut_network lut;
  auto         p1 = lut.create_pi();
  auto         p2 = lut.create_pi();
  auto         o1 = lut.create_xor(p1, p2);
  lut.create_po(o1);
  lut.foreach_node([&](auto const& n) {
    auto func = lut.node_function(n);
    fmt::print("n{} func = {}\n", lut.node_to_index(n), kitty::to_hex(func));
  });
  write_bench(lut, std::cout);
}

// g++ -std=c++17 -I ../../lib/sparsepp/ -I ../../lib/fmt/ -I ../../lib/ez/ -I ../../lib/kitty/ -I ../../include/ test1.cpp
// ../../lib/fmt/fmt/*.cc
