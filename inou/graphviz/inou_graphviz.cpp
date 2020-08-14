//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include "inou_graphviz.hpp"

#include <atomic>
#include <fstream>
#include <regex>

#include "eprp_utils.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"

void setup_inou_graphviz() { Inou_graphviz::setup(); }

void Inou_graphviz::populate_lg_handle_xedge(const Node &node, const XEdge &out, std::string &data) {
  auto dp_pid  = out.driver.get_pid();
  auto sp_pid  = out.sink.get_pid();
  auto dn_name = graphviz_legalize_name(out.driver.get_node().debug_name());
  if (out.driver.is_graph_io()) {
    dn_name = graphviz_legalize_name(out.driver.get_name());
  }
  auto sn_name = graphviz_legalize_name(out.sink.get_node().debug_name());
  if (out.sink.is_graph_io()) {
    sn_name = graphviz_legalize_name(out.sink.get_name());
  }
  auto dbits   = out.driver.get_bits();
  auto dp_name = out.driver.has_name() ? out.driver.get_name() : "";

  if (node.get_type().op == Const_Op)
    data += fmt::format(" {}->{}[label=<{}b:({},{})>];\n", dn_name, sn_name, dbits, dp_pid, sp_pid);
  else if (node.get_type().op == TupRef_Op)
    data += fmt::format(" {}->{}[label=<({},{}):<font color=\"#0000ff\">{}</font>>];\n", dn_name, sn_name, dp_pid, sp_pid, dp_name);
  else if (node.get_type().op == TupAdd_Op)
    data += fmt::format(" {}->{}[label=<{}b:({},{}):<font color=\"#0000ff\">{}</font>>];\n",
                        dn_name,
                        sn_name,
                        dbits,
                        dp_pid,
                        sp_pid,
                        dp_name);
  else
    data += fmt::format(" {}->{}[label=<{}b:({},{}):{}>];\n", dn_name, sn_name, dbits, dp_pid, sp_pid, dp_name);
}

std::string Inou_graphviz::graphviz_legalize_name(std::string_view name) {
  std::string legal;

  for (auto c : name) {
    if (std::isalnum(c))
      legal.append(1,c);
    else
      legal.append(1,'_');
  }

  return legal;
}

void Inou_graphviz::setup() {
  Eprp_method m1("inou.graphviz.from", "export lgraph/lnast to graphviz dot format", &Inou_graphviz::from);

  m1.add_label_optional("bits", "dump bits (true/false)", "false");
  m1.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");
  register_inou("graphviz", m1);


  Eprp_method m2("inou.graphviz.fromlg.hierarchy", "export lgraph hierarchy to graphviz dot format", &Inou_graphviz::hierarchy);
  register_inou("graphviz", m2);
}

Inou_graphviz::Inou_graphviz(const Eprp_var &var) : Pass("inou.graphviz", var) {
  if (var.has_label("bits")) {
    auto b = var.get("bits");
    bits   = b != "false" && b != "0";
  } else {
    bits = false;
  }

  auto v  = var.get("verbose");
  verbose = v != "false" && v != "0";
}

void Inou_graphviz::from(Eprp_var &var) {
  Inou_graphviz p(var);

  for (const auto &l : var.lgs) {
    p.do_from_lgraph(l);
  }
  for (const auto &l : var.lnasts) {
    p.do_from_lnast(l);
  }
}

void Inou_graphviz::hierarchy(Eprp_var &var) {
  Inou_graphviz p(var);

  for (const auto &l : var.lgs) {
    p.do_hierarchy(l);
  }
}


void Inou_graphviz::do_hierarchy(LGraph *g) {
  std::string data = "digraph {\n";

  g->dump_down_nodes();

  for (const auto node : g->fast(true)) {
    if (!node.is_type_sub())
      continue;
    // fmt::print("lg:{} node:{} type:{}\n", node.get_class_lgraph()->get_name(), node.debug_name(), node.get_type().get_name());

    const auto &child_sub = node.get_type_sub_node();
    data += fmt::format(" {}->{};", graphviz_legalize_name(node.get_class_lgraph()->get_name()), graphviz_legalize_name(child_sub.get_name()));
  }

  data += "\n}\n";

  std::string file = absl::StrCat(odir, "/", g->get_name(), "_hier.dot");
  int         fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    Pass::error("inou.graphviz.do_hierarchy unable to create {}", file);
    return;
  }
  size_t sz = write(fd, data.c_str(), data.size());
  if (sz != data.size()) {
    Pass::error("inou.graphviz.do_hierarchy unexpected write missmatch");
    return;
  }
  close(fd);
}

void Inou_graphviz::do_from_lgraph(LGraph *lg_parent) {
  populate_lg_data(lg_parent);

  lg_parent->each_sub_fast([lg_parent, this](Node &node, Lg_type_id lgid) {
    (void)node;
    fmt::print("subgraph lgid:{}\n", lgid);
    LGraph *lg_child = LGraph::open(lg_parent->get_path(), lgid);
    populate_lg_data(lg_child);
  });
}

void Inou_graphviz::populate_lg_data(LGraph *g) {
  std::string data = "digraph {\n";

  for(auto node:g->fast(false)) {
    if (!node.has_inputs() && !node.has_outputs())
      continue;
    std::string node_info;
    if (!verbose) {
      auto pos  = node.debug_name().find("_lg_");
      node_info = node.debug_name().substr(0, pos);  // get rid of the lgraph name
      node_info = std::regex_replace(node_info, std::regex("node_"), "n");

      /* if (node.has_cfcnt()) // for temporarily dbg cfcnt*/
      /*   node_info = node_info + "_" + std::to_string(node.get_cfcnt()); */
    } else {
      node_info = node.debug_name();
    }

    auto gv_name = graphviz_legalize_name(node.debug_name());
    if (node.get_type().op == Const_Op)
      data += fmt::format(" {} [label=<{}:{}>];\n", gv_name, node_info, node.get_type_const().to_pyrope());
    else
      data += fmt::format(" {} [label=<{}>];\n", gv_name, node_info);

    for (const auto &out : node.out_edges()) {
      populate_lg_handle_xedge(node, out, data);
    }
  }

  g->each_graph_input([&data](const Node_pin &pin) {
    std::string_view io_name = pin.get_name();
    data += fmt::format(" {} [label=<{}>];\n", graphviz_legalize_name(io_name), io_name);  // pin.debug_name());

    for (const auto &out : pin.out_edges()) {
      populate_lg_handle_xedge(pin.get_node(), out, data);
    }
  });

  // we need this to show outputs bits in graphviz
  g->each_graph_output([&data](const Node_pin &pin) {
    std::string_view dst_str = "virtual_dst_module";
    auto             dbits   = pin.get_bits();
    data += fmt::format(" {}->{}[label=<{}b>];\n", graphviz_legalize_name(pin.get_name()), dst_str, dbits);
  });

  data += "}\n";

  std::string file = absl::StrCat(odir, "/", g->get_name(), ".dot");
  int         fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    Pass::error("inou.graphviz unable to create {}", file);
    return;
  }
  size_t sz = write(fd, data.c_str(), data.size());
  if (sz != data.size()) {
    Pass::error("inou.graphviz unexpected write missmatch");
    return;
  }
  close(fd);
}

void Inou_graphviz::do_from_lnast(std::shared_ptr<Lnast> lnast) {
  std::string data = "digraph {\n";

  for (const auto &itr : lnast->depth_preorder(lnast->get_root())) {
    auto node_data = lnast->get_data(itr);

    auto subs = node_data.subs;
    auto name = node_data.token.get_text();

    auto id = std::to_string(itr.level) + std::to_string(itr.pos);
    if (node_data.type.is_ref()) {
      data += fmt::format(" {} [label=<{}, {}<I><SUB><font color=\"#ff1020\">{}</font></SUB></I>>];\n",
                          id,
                          node_data.type.debug_name(),
                          name,
                          subs);
    } else {
      data += fmt::format(" {} [label=<{}, {}>];\n", id, node_data.type.debug_name(), name);
    }

    if (node_data.type.is_top())
      continue;

    // get parent data for link
    auto        p = lnast->get_parent(itr);
    std::string pname(lnast->get_data(p).token.get_text());

    auto parent_id = std::to_string(p.level) + std::to_string(p.pos);
    data += fmt::format(" {}->{};\n", parent_id, id);
  }

  data += "}\n";

  auto f2   = lnast->get_top_module_name();
  auto file = absl::StrCat(odir, "/", f2, ".lnast.dot");
  int  fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    Pass::error("inou.graphviz_lnast unable to create {}", file);
    return;
  }
  size_t sz = write(fd, data.c_str(), data.size());
  if (sz != data.size()) {
    Pass::error("inou.graphviz_lnast unexpected write missmatch");
    return;
  }
  close(fd);
}
