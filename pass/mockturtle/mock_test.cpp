
#include <iostream>
#include <sstream>

#include <mockturtle/algorithms/cut_enumeration.hpp>

#include <mockturtle/algorithms/resubstitution.hpp>

#include <mockturtle/algorithms/refactoring.hpp>

#include <mockturtle/algorithms/collapse_mapped.hpp>

#include <mockturtle/algorithms/node_resynthesis.hpp>
#include <mockturtle/algorithms/node_resynthesis/akers.hpp>
#include <mockturtle/algorithms/node_resynthesis/direct.hpp>
#include <mockturtle/algorithms/node_resynthesis/mig_npn.hpp>
#include <mockturtle/algorithms/node_resynthesis/xmg_npn.hpp>

#include <mockturtle/algorithms/cleanup.hpp>
#include <mockturtle/generators/arithmetic.hpp>
#include <mockturtle/io/write_bench.hpp>
#include <mockturtle/networks/klut.hpp>
#include <mockturtle/networks/mig.hpp>
#include <mockturtle/networks/aig.hpp>

#include <mockturtle/algorithms/lut_mapping.hpp>
#include <mockturtle/views/mapping_view.hpp>

using namespace mockturtle;

int main() {
#if 0
  klut_network lut;
  auto p1 = lut.create_pi();
  auto p2 = lut.create_pi();
  auto o1 = lut.create_xor(p1, p2);
  lut.create_po(o1);
  lut.foreach_node([&](auto const& n){
     auto func = lut.node_function(n);
     fmt::print("n{} func = {}\n", lut.node_to_index(n),kitty::to_hex(func));
  });
#endif

  mig_network net;

  std::vector<mig_network::signal> array;
  mig_network::signal array2[32];

  for(int i=0;i<32;i++) {
    array.emplace_back(net.create_pi());
  }

  array2[0] = net.create_or(array[0],array[1]);
  for(int i=1;i<32;i++) {
    array2[i] = net.create_or(array[0],array2[i-1]);
  }

  net.create_po( array2[31] );
  //net.create_po( net.create_nary_xor(array) );

  // OPT1
  refactoring_params rf_ps;
  rf_ps.max_pis = 4;
  mig_npn_resynthesis resyn1;
  refactoring(net, resyn1, rf_ps);
  net = cleanup_dangling(net);

  // OPT1
  //resubstitution(net);
  //net = cleanup_dangling(net);

  // OPT3
  akers_resynthesis<mig_network> resyn2;
  const auto mig = node_resynthesis<mig_network>( net, resyn2 );
  net = cleanup_dangling(net);

  mapping_view<mig_network, true> mapped_mig{net};

  lut_mapping_params ps;
  ps.cut_enumeration_ps.cut_size = 6;
  lut_mapping<mapping_view<mig_network, true>, true>( mapped_mig, ps);
  mockturtle::klut_network nt_lut = *mockturtle::collapse_mapped_network<mockturtle::klut_network>(mapped_mig);

  write_bench(nt_lut, std::cout);
}

// g++ -std=c++17 -I ../../lib/sparsepp/ -I ../../lib/fmt/ -I ../../lib/ez/ -I ../../lib/kitty/ -I ../../include/ test1.cpp ../../lib/fmt/fmt/*.cc

