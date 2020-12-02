//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_submatch.hpp"

#include "woothash.hpp"
#include "waterhash.hpp"
#include "annotate.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"

static Pass_plugin sample("pass_submatch", pass_submatch::setup);

void pass_submatch::setup() {
  Eprp_method m1("pass.submatch", "Find identical subgraphs", &pass_submatch::work);

  register_pass(m1);
}

pass_submatch::pass_submatch(const Eprp_var &var) : Pass("pass.submatch", var) {}

void pass_submatch::do_work(LGraph *g) { check_lec(g); }

void pass_submatch::work(Eprp_var &var) {
  pass_submatch p(var);

  for (const auto &g : var.lgs) {
    p.do_work(g);
  }
}

void pass_submatch::check_lec(LGraph *g) {
  fmt::print("TODO: implement pass\n");
  //-----------------------------------------------------------------------------------------------------

  struct Hash_attr {
    int depth;
    int n;
  };
  absl::flat_hash_map<uint64_t, Hash_attr> hash2attr;
  absl::flat_hash_map<Node_pin::Compact_driver, uint64_t> dpin2hash;
  absl::flat_hash_map<Node_pin::Compact_driver, uint64_t> dpin2depth;

  for (const auto node : g->forward()) {
    // sat.generate_model();

    std::vector<uint64_t> i_hash;

    uint64_t i_depth = 0;
    for(auto e:node.inp_edges()) {
      auto it = dpin2hash.find(e.driver.get_compact_driver());
      if (it == dpin2hash.end()) {
        uint64_t n = e.driver.get_pid();
        n <<= 8;
        n |= static_cast<uint64_t>(e.driver.get_node().get_type_op());
        n <<= 16;
        n |= e.sink.get_pid();
        i_hash.emplace_back(n);
      }else{
        i_hash.emplace_back(it->second ^ e.sink.get_pid());

        const auto it2 = dpin2depth.find(e.driver.get_compact_driver());
        if (it2->second>i_depth)
          i_depth = it2->second;
      }
    }
    std::sort(i_hash.begin(), i_hash.begin()+i_hash.size());

    uint64_t i_key = mmap_lib::woothash64(i_hash.data(), i_hash.size()*8);
    if (node.is_type_loop_breaker())
      i_depth = 0;

    for(auto dpin:node.out_connected_pins()) {
      uint64_t n = dpin.get_pid();
      n <<= 8;
      n |= static_cast<uint64_t>(node.get_type_op());
      uint32_t c_key = mmap_lib::waterhash(&n,4, i_key&0xFFFF);

      uint64_t key = (i_key<<2)^c_key;

      fmt::print("dpin:{} i_key:{} c_key:{} key:{} d:{}\n",dpin.debug_name(), i_key, c_key, key, i_depth + 1);

      dpin2hash[dpin.get_compact_driver()] = key;
      dpin2depth[dpin.get_compact_driver()] = i_depth + 1;
      hash2attr[key].depth = i_depth + 1;
      hash2attr[key].n++;
    }
  }

  for(const auto it:hash2attr) {
    const auto &attr = it.second;
    fmt::print("hash:{} depth:{} n:{} s:{}\n", it.first, attr.depth, attr.n, attr.depth*attr.n);
  }
}
