// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graphviz.hpp"

#include <fstream>
#include <regex>

#include "cell.hpp"
#include "pass.hpp"

Graphviz::Graphviz(bool _bits, bool _verbose, std::string_view _odir) : verbose(_verbose), odir(_odir) {
  // NOTE: since 'bits' is removed as a private member (unused), '_bits' is unused but might be used in the future
  (void)_bits;
}

void Graphviz::populate_lg_handle_xedge(const Node &node, const XEdge &out, std::string &data, bool verbose) {
  std::string dp_pid, sp_pid;
  if (verbose) {
    dp_pid = graphviz_legalize_name(out.driver.get_pin_name());
    sp_pid = graphviz_legalize_name(out.sink.get_pin_name());
  } else {
    dp_pid = graphviz_legalize_name(std::to_string(out.driver.get_pid()));
    sp_pid = graphviz_legalize_name(std::to_string(out.sink.get_pid()));
  }

  std::string dn_name;
  if (out.driver.is_graph_io()) {
    dn_name = graphviz_legalize_name(out.driver.get_pin_name());
  } else {
    dn_name = graphviz_legalize_name(out.driver.get_node().debug_name());
  }

  std::string sn_name;
  if (out.sink.is_graph_io()) {
    sn_name = graphviz_legalize_name(out.sink.get_pin_name());
  } else {
    sn_name = graphviz_legalize_name(out.sink.get_node().debug_name());
  }

  auto dbits   = out.driver.get_bits();
  auto dp_name = graphviz_legalize_name(out.driver.has_name() ? out.driver.get_name() : "unk");

  if (node.get_type_op() == Ntype_op::Const)
    data += fmt::format(" {}->{}[label=<{}b:({},{})>];\n", dn_name, sn_name, dbits, dp_pid, sp_pid);
  else if (node.get_type_op() == Ntype_op::TupRef)
    data += fmt::format(" {}->{}[label=<({},{}):<font color=\"#0000ff\">{}</font>>];\n", dn_name, sn_name, dp_pid, sp_pid, dp_name);
  else if (node.get_type_op() == Ntype_op::TupAdd)
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

std::string Graphviz::graphviz_legalize_name(std::string_view name) {
  std::string legal;
  for (auto c : name) {
    if (std::isalnum(c)) {
      legal.append(1, c);
    } else if (c == 32) {
      legal += " ";
    } else if (c == 35) {
      legal += "#";
    } else if (c == 37) {
      legal += "_percent_";
    } else if (c == 36) {
      legal += "_dollar_";
    } else if (c == 58) {
      legal += ":";
    } else if (c == 95) {
      legal += "_";
    } else {
      legal += "_c" + std::to_string(c) + "_";
    }
  }

  return legal;
}

void Graphviz::do_hierarchy(Lgraph *g) {
  // include a font name to get graph to render properly with kgraphviewer
  std::string data = "digraph {\n node [fontname = \"Source Code Pro\"];\n";

  const auto &root_tree = g->get_htree();

  absl::flat_hash_set<std::pair<Hierarchy_index, Hierarchy_index>> added;

  for (auto hidx : root_tree.depth_preorder()) {
    auto *lg = root_tree.ref_lgraph(hidx);
    fmt::print("visiting node:{} level:{} pos:{}\n", lg->get_name(), (int)hidx.level, (int)hidx.pos);

    Node h_inp(g, hidx, Hardcoded_input_nid);
    for (auto e : h_inp.inp_edges()) {
      fmt::print("edge from:{} to:{} level:{} pos:{}\n",
                 e.driver.get_class_Lgraph()->get_name(),
                 e.sink.get_class_Lgraph()->get_name(),
                 (int)hidx.level,
                 (int)hidx.pos);

      auto p = std::pair(e.driver.get_hidx(), e.sink.get_hidx());
      if (p.first == p.second)
        continue;  // no itself edges
      if (added.contains(p))
        continue;
      added.insert(p);

      data += fmt::format(" {}_l{}p{}->{}_l{}p{};\n",
                          graphviz_legalize_name(e.driver.get_class_Lgraph()->get_name()),
                          (int)e.driver.get_hidx().level,
                          (int)e.driver.get_hidx().pos,
                          graphviz_legalize_name(e.sink.get_class_Lgraph()->get_name()),
                          (int)e.sink.get_hidx().level,
                          (int)e.sink.get_hidx().pos);
    }

    Node h_out(g, hidx, Hardcoded_output_nid);
    for (auto e : h_out.out_edges()) {
      fmt::print("edge from:{} to:{} level:{} pos:{}\n",
                 e.driver.get_class_Lgraph()->get_name(),
                 e.sink.get_class_Lgraph()->get_name(),
                 (int)hidx.level,
                 (int)hidx.pos);

      auto p = std::pair(e.driver.get_hidx(), e.sink.get_hidx());
      if (p.first == p.second)
        continue;  // no itself edges
      if (added.contains(p))
        continue;
      added.insert(p);

      data += fmt::format(" {}_l{}p{}->{}_l{}p{};\n",
                          graphviz_legalize_name(e.driver.get_class_Lgraph()->get_name()),
                          (int)e.driver.get_hidx().level,
                          (int)e.driver.get_hidx().pos,
                          graphviz_legalize_name(e.sink.get_class_Lgraph()->get_name()),
                          (int)e.sink.get_hidx().level,
                          (int)e.sink.get_hidx().pos);
    }
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

void Graphviz::do_from_lgraph(Lgraph *lg_parent, std::string_view dot_postfix) {
  populate_lg_data(lg_parent, dot_postfix);

  lg_parent->each_local_sub_fast([&, this](Node &node, Lg_type_id lgid) {
    // no need to populate firrtl_op_subgraph, it's just tmap cells.
    if (node.get_type_sub_node().get_name().substr(0, 5) == "__fir")
      return;

    (void)node;
    fmt::print("subgraph lgid:{}\n", lgid);
    Lgraph *lg_child = Lgraph::open(lg_parent->get_path(), lgid);
    populate_lg_data(lg_child, dot_postfix);
  });
}

void Graphviz::populate_lg_data(Lgraph *g, std::string_view dot_postfix) {
  std::string data = "digraph {\n";

  for (auto node : g->fast(false)) {
    if (!node.has_inputs() && !node.has_outputs())
      continue;
    std::string node_info;
    if (!verbose) {
      auto pos  = node.debug_name().find("_lg");
      node_info = node.debug_name().substr(0, pos);  // get rid of the lgraph name
      node_info = graphviz_legalize_name(std::regex_replace(node_info, std::regex("node_"), "n"));
    } else {
      node_info = graphviz_legalize_name(node.debug_name());
    }

    auto gv_name = graphviz_legalize_name(node.debug_name());
    if (node.get_type_op() == Ntype_op::Const)
      data += fmt::format(" {} [label=<{}:{}>];\n", gv_name, node_info, node.get_type_const().to_pyrope());
    else
      data += fmt::format(" {} [label=<{}>];\n", gv_name, node_info);

    for (const auto &out : node.out_edges()) {
      populate_lg_handle_xedge(node, out, data, verbose);
    }
  }

  g->each_graph_input([&](const Node_pin &pin) {
    auto io_name = graphviz_legalize_name(pin.get_pin_name());
    data += fmt::format(" {} [label=<{}>];\n", io_name, io_name);  // pin.debug_name());

    for (const auto &out : pin.out_edges()) {
      populate_lg_handle_xedge(pin.get_node(), out, data, verbose);
    }
  });

  // we need this to show outputs bits in graphviz
  g->each_graph_output([&](const Node_pin &pin) {
    std::string_view dst_str = "virtual_dst_module";
    auto             dbits   = pin.get_bits();
    data += fmt::format(" {}->{}[label=<{}b>];\n", graphviz_legalize_name(pin.get_name()), dst_str, dbits);
    for (const auto &out : pin.out_edges()) {
      populate_lg_handle_xedge(pin.get_node(), out, data, verbose);
    }
  });

  data += "}\n";

  std::string file;
  if (dot_postfix == "")
    file = absl::StrCat(odir, "/", g->get_name(), ".dot");
  else
    file = absl::StrCat(odir, "/", g->get_name(), ".", dot_postfix, ".dot");

  int fd = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
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

void Graphviz::do_from_lnast(std::shared_ptr<Lnast> lnast, std::string_view dot_postfix) {
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
    } else if (node_data.type.is_select()) {
      data += fmt::format(" {} [label=<{}, {}>];\n", id, "selc", name);
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

  auto f2 = lnast->get_top_module_name();

  std::string file;
  if (dot_postfix == "")
    file = absl::StrCat(odir, "/", f2, ".lnast.dot");
  else
    file = absl::StrCat(odir, "/", f2, ".lnast", ".", dot_postfix, ".dot");

  int fd = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
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
