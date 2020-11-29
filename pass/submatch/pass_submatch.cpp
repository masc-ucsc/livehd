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

  absl::flat_hash_map<uint64_t, Node_pin::Compact> hash2dpin;
  absl::flat_hash_map<Node_pin::Compact, uint64_t> dpin2hash;

  for (const auto node : g->forward()) {
    // sat.generate_model();

    std::vector<uint64_t> i_hash;
    for(auto e:node.inp_edges()) {
      auto it = dpin2hash.find(e.driver.get_compact());
      if (it == dpin2hash.end()) {
        uint64_t n = e.driver.get_pid();
        n <<= 8;
        n |= static_cast<uint64_t>(e.driver.get_node().get_type_op());
        n <<= 16;
        n |= e.sink.get_pid();
        i_hash.emplace_back(n);
      }else{
        i_hash.emplace_back(it->second ^ e.sink.get_pid());
      }
    }
    std::sort(i_hash.begin(), i_hash.begin()+i_hash.size());

    uint64_t i_key = mmap_lib::woothash64(i_hash.data(), i_hash.size()*8);

    for(auto dpin:node.out_connected_pins()) {
      uint64_t n = dpin.get_pid();
      n <<= 8;
      n |= static_cast<uint64_t>(node.get_type_op());
      uint32_t c_key = mmap_lib::waterhash(&n,4, i_key&0xFFFF);

      uint64_t key = (i_key<<2)^c_key;

      fmt::print("dpin:{} i_key:{} c_key:{} key:{}\n",dpin.debug_name(), i_key, c_key, key);

      dpin2hash[dpin.get_compact()] = key;
      hash2dpin[key] = dpin.get_compact();
    }
  }
}
