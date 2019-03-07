//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "pass_sample.hpp"

void setup_pass_sample() {
  Pass_sample p;
  p.setup();
}

void Pass_sample::setup() {
  Eprp_method m1("pass.sample", "counts number of nodes in an lgraph", &Pass_sample::work);

  register_pass(m1);
}

Pass_sample::Pass_sample()
    : Pass("sample") {
}

void Pass_sample::work(Eprp_var &var) {
  Pass_sample pass;

  for(const auto &g : var.lgs) {
    pass.do_work(g);
  }
}

void Pass_sample::do_work(const LGraph *g) {
  LGBench b("pass.sample");

#if 0
  g->each_sub_graph_fast([g](const Index_ID &idx, const Lg_type_id &lgid, std::string_view iname) {
    fmt::print("1.base:{} idx:{} lgid:{} iname:{}\n",g->get_name(), idx, lgid, iname);
    return true;
  });

  g->each_sub_graph_fast([g](const Index_ID &idx, const Lg_type_id &lgid, std::string_view iname) {
    fmt::print("3.base:{} idx:{} lgid:{} iname:{}\n",g->get_name(), idx, lgid, iname);
    return true;
  });

  g->each_sub_graph_fast([g](const Index_ID &idx, const Lg_type_id &lgid, std::string_view iname) {
    fmt::print("3.base:{} idx:{} lgid:{} iname:{}\n",g->get_name(), idx, lgid, iname);
  });

  g->each_sub_graph_fast([g](const Index_ID &idx, const Lg_type_id &lgid) {
    fmt::print("1.base:{} idx:{} lgid:{}\n",g->get_name(), idx, lgid);
  });

  g->each_sub_graph_fast([g](const Index_ID &idx, const Lg_type_id &lgid) {
    fmt::print("1.base:{} idx:{} lgid:{}\n",g->get_name(), idx, lgid);
    return false;
  });

  std::function<void(const Index_ID &, const Lg_type_id &, std::string_view )> fn = [g](const Index_ID &idx, const Lg_type_id &lgid, std::string_view iname) {
    fmt::print("2.base:{} idx:{} lgid:{} iname:{}\n",g->get_name(), idx, lgid, iname);
  };
  g->each_sub_graph_fast(fn);

  std::function fn2 = [g](const Index_ID &idx, const Lg_type_id &lgid, std::string_view iname) {
    fmt::print("2.base:{} idx:{} lgid:{} iname:{}\n",g->get_name(), idx, lgid, iname);
    return false;
  };
  g->each_sub_graph_fast(fn2);

  auto fn3 = [g](const Index_ID &idx, const Lg_type_id &lgid) {
    fmt::print("2.base:{} idx:{} lgid:{}\n",g->get_name(), idx, lgid);
  };
  g->each_sub_graph_fast(fn3);

#else
  std::map<std::string, int> histogram;

  int cells = 0;
  for(const auto &nid : g->forward()) {
    cells++;
    const auto &node = g->get_node(nid);
    std::string name = node.get_type().get_name();
    for(const auto &in_edge : g->inp_edges(nid)) {
      name.append("_i");
      name.append(std::to_string(in_edge.get_bits()));
    }
    for(const auto &out_edge : g->out_edges(nid)) {
      name.append("_o");
      name.append(std::to_string(out_edge.get_bits()));
    }

    histogram[name]++;
  }

  for(auto it=histogram.begin(); it != histogram.end(); it++) {
    fmt::print("{} {}\n",it->first,it->second);
  }

  fmt::print("Pass: cells {}\n", cells);
#endif
}
