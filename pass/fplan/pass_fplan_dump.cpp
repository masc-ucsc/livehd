#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

#include "fmt/format.h"  // for more advanced formatting
#include "hier_tree.hpp"
#include "i_resolve_header.hpp"  // for iassert
#include "pass_fplan.hpp"

void Pass_fplan_dump::dump_hier(Eprp_var &var) {
  Hier_tree h(var);

  auto& ginfo = h.collapsed_gis[0];

  // although the graph lib has a way to make dotfiles out of a graph, it doesn't print out enough information.

  std::stringstream dotstr;

  dotstr << "digraph g {\n\tnode [fontname = \"Source Code Pro\", shape=record];\n";

  for (auto v : ginfo.al.verts()) {
    auto name  = ginfo.debug_names(v);
    auto id    = ginfo.ids(v);
    auto label = ginfo.labels(v);
    auto area  = ginfo.areas(v);
    dotstr << fmt::format("\t{} [label=\"{{{} | {{lb {} | id {} | area {:.2f}}}}}\"];\n", id, name, label, id, area);
  }

  for (auto e : ginfo.al.edges()) {
    auto src = ginfo.ids(ginfo.al.tail(e));
    auto dst = ginfo.ids(ginfo.al.head(e));
    dotstr << fmt::format("\t{} -> {} [label={}];\n", src, dst, ginfo.weights(e));
  }

  dotstr << "}";

  std::ofstream     dumpfile;
  const std::string path = "hier_dump.dot";
  dumpfile.open(path);

  I(dumpfile);
  dumpfile << dotstr.str();

  dumpfile.close();

  fmt::print("  wrote file {}.\n", path);
}

void Pass_fplan_dump::dump_tree(Eprp_var &var) {
  Hier_tree h(var);

  h.discover_hierarchy(1);

  std::stringstream dotstr;

  dotstr << "digraph g {\n\tnode [fontname = \"Source Code Pro\", shape=record];\n";

  std::function<void(std::shared_ptr<Hier_node>, const Graph_info<g_type>&)> dump_graph_names = [&](std::shared_ptr<Hier_node> n, const Graph_info<g_type>& gi) {
    dotstr << fmt::format("\t{};\n", n->name);
    if (n->is_leaf()) {
      for (auto v : n->graph_set) {
        std::string name = gi.debug_names(v);
        name.append("_");
        name.append(std::to_string(gi.ids(v)));  // create a unique label for each node, not just each node type

        dotstr << fmt::format("\t{} [label=\"{{{} | {{lb {} | id {} | area {:.2f}}}}}\", color=red];\n",
                              gi.ids(v),
                              name,
                              gi.labels(v),
                              gi.ids(v),
                              gi.areas(v));
        dotstr << fmt::format("\t{} -> {};\n", gi.ids(v), n->name);
      }
    }

    if (n->children[0] != nullptr) {
      dump_graph_names(n->children[0], gi);
      dotstr << fmt::format("\t{} -> {};\n", n->children[0]->name, n->name);
    }

    if (n->children[1] != nullptr) {
      dump_graph_names(n->children[1], gi);
      dotstr << fmt::format("\t{} -> {};\n", n->children[1]->name, n->name);
    }
  };

  const double mta = std::stod(var.get("min_tree_area").data());
  fmt::print("  generating collapsed hierarchy (min area: {})...", mta);
  h.collapse(1, mta);
  fmt::print("done.\n");

  dump_graph_names(h.hiers[1], h.collapsed_gis[1]);

  dotstr << "}";

  std::ofstream     dumpfile;
  const std::string path = "tree_dump.dot";
  dumpfile.open(path);

  I(dumpfile);
  dumpfile << dotstr.str();

  dumpfile.close();

  fmt::print("  wrote file {}.\n", path);
}