//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include "inou_graphviz.hpp"

#include <atomic>
#include <fstream>

#include "eprp_utils.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"
#include "lnast_parser.hpp"

void setup_inou_graphviz() { Inou_graphviz::setup(); }

void Inou_graphviz::setup() {
  Eprp_method m1("inou.graphviz.fromlg", "export lgraph to graphviz dot format", &Inou_graphviz::fromlg);

  m1.add_label_optional("bits", "dump bits (true/false)", "false");
  m1.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");
  // m1.add_label_optional("odir",    "path to put the dot", ".");

  register_inou("graphviz", m1);

  Eprp_method m2("inou.graphviz.fromlnast", "export lnast to graphviz dot format", &Inou_graphviz::fromlnast);

  // m2.add_label_required("files",  "cfg_text files to process (comma separated)");
  // m2.add_label_optional("odir",   "path to put the dot", ".");

  register_inou("graphviz", m2);

  Eprp_method m3("inou.graphviz.fromlg.hierarchy", "export lgraph hierarchy to graphviz dot format", &Inou_graphviz::hierarchy);

  // m3.add_label_optional("odir",   "path to put the dot", ".");

  register_inou("graphviz", m3);
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

void Inou_graphviz::fromlg(Eprp_var &var) {
  Inou_graphviz p(var);

  for (const auto &l : var.lgs) {
    p.do_fromlg(l);
  }
}

void Inou_graphviz::hierarchy(Eprp_var &var) {
  Inou_graphviz p(var);

  for (const auto &l : var.lgs) {
    p.do_hierarchy(l);
  }
}

void Inou_graphviz::fromlnast(Eprp_var &var) {
  Inou_graphviz p(var);

  for (const auto &f : absl::StrSplit(p.files, ',')) {
    p.do_fromlnast(f);
  }
}

void Inou_graphviz::do_hierarchy(LGraph *g) {
  std::string data = "digraph {\n";

  g->dump_down_nodes();

  for (const auto node : g->fast(true)) {
    if (!node.is_type_sub()) continue;
    // fmt::print("lg:{} node:{} type:{}\n", node.get_class_lgraph()->get_name(), node.debug_name(), node.get_type().get_name());

    const auto &child_sub = node.get_type_sub_node();
    data += fmt::format(" {}->{};", node.get_class_lgraph()->get_name(), child_sub.get_name());
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

void Inou_graphviz::do_fromlg(LGraph *lg_parent) {
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

  g->each_node_fast([&data](const Node &node) {
    auto node_name = node.has_name() ? node.get_name() : "";

    if (node.get_type().op == U32Const_Op)
      data += fmt::format(" {} [label=\"{} :{} :{}\"];\n", node.debug_name(), node.debug_name(), node_name,
                          node.get_type_const_value());
    else
      data += fmt::format(" {} [label=\"{} :{}\"];\n", node.debug_name(), node.debug_name(), node_name);

    for (auto &out : node.out_edges()) {
      auto &dpin       = out.driver;
      auto  dnode_name = dpin.get_node().debug_name();
      auto  snode_name = out.sink.get_node().debug_name();
      auto  dpin_name  = dpin.has_name() ? dpin.get_name() : "";
      auto  dbits      = dpin.get_bits();

      data += fmt::format(" {}->{}[label=\"{}b :{} :{} :{}\"];\n", dnode_name, snode_name, dbits, dpin.get_pid(),
                          out.sink.get_pid(), dpin_name);
    }
  });

  g->each_graph_output([&data](const Node_pin &pin) {
    std::string_view dst_str = "virtual_dst_module";
    auto             dbits   = pin.get_bits();
    data += fmt::format(" {}->{}[label=\"{}b\"];\n", pin.get_node().debug_name(), dst_str, dbits);
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

void Inou_graphviz::do_fromlnast(std::string_view f) {
  Lnast_parser lnast_parser(f);

  auto *lnast = lnast_parser.ref_lnast();
  lnast->ssa_trans();
  std::string data = "digraph {\n";

  for (const auto &itr : lnast->depth_preorder(lnast->get_root())) {
    auto node_data = lnast->get_data(itr);

    auto subs = node_data.subs;
    auto name = node_data.token.get_text();

    auto id = std::to_string(itr.level) + std::to_string(itr.pos);
    if (node_data.type.is_ref()) {
      data += fmt::format(" {} [label=\"{}, {}[{}]\"];\n", id, node_data.type.debug_name(), name, subs);
    } else {
      data += fmt::format(" {} [label=\"{}, {}\"];\n", id, node_data.type.debug_name(), name);
    }

    if (node_data.type.is_top()) continue;

    // get parent data for link
    auto        p = lnast->get_parent(itr);
    std::string pname(lnast->get_data(p).token.get_text());

    auto parent_id = std::to_string(p.level) + std::to_string(p.pos);
    data += fmt::format(" {}->{};\n", parent_id, id);
  }

  data += "}\n";

  auto f2 = f.substr(0, f.length()-4); // remove ".cfg" in the end
  std::string file = absl::StrCat(odir, "/", f2, ".lnast.dot");
  int         fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
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
