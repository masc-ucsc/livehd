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

  // although the graph lib has a way to make dotfiles out of a graph, it doesn't print out enough information.

  std::stringstream dotstr;

  dotstr << "digraph g {\n\tnode [fontname = \"Source Code Pro\", shape=record];\n";

  for (auto v : h.ginfo.al.verts()) {
    auto name  = h.ginfo.debug_names(v);
    auto id    = h.ginfo.ids(v);
    auto label = h.ginfo.labels(v);
    auto area  = h.ginfo.areas(v);
    dotstr << fmt::format("\t{} [label=\"{{{} | {{lb {} | id {} | area {:.2f}}}}}\"];\n", id, name, label, id, area);
  }

  for (auto e : h.ginfo.al.edges()) {
    auto src = h.ginfo.ids(h.ginfo.al.tail(e));
    auto dst = h.ginfo.ids(h.ginfo.al.head(e));
    dotstr << fmt::format("\t{} -> {} [label={}];\n", src, dst, h.ginfo.weights(e));
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

  std::function<void(std::shared_ptr<Hier_node>)> dump_graph_names = [&](std::shared_ptr<Hier_node> root) {
    dotstr << fmt::format("\t{};\n", root->name);
    if (root->is_leaf()) {
      for (auto v : root->graph_set) {
        std::string name = h.ginfo.debug_names(v);
        name.append("_");
        name.append(std::to_string(h.ginfo.ids(v)));  // create a unique label for each node, not just each node type

        dotstr << fmt::format("\t{} [label=\"{{{} | {{lb {} | id {} | area {:.2f}}}}}\", color=red];\n",
                              h.ginfo.ids(v),
                              name,
                              h.ginfo.labels(v),
                              h.ginfo.ids(v),
                              h.ginfo.areas(v));
        dotstr << fmt::format("\t{} -> {};\n", h.ginfo.ids(v), root->name);
      }
    }

    if (root->children[0] != nullptr) {
      dump_graph_names(root->children[0]);
      dotstr << fmt::format("\t{} -> {};\n", root->children[0]->name, root->name);
    }

    if (root->children[1] != nullptr) {
      dump_graph_names(root->children[1]);
      dotstr << fmt::format("\t{} -> {};\n", root->children[1]->name, root->name);
    }
  };

  dump_graph_names(h.hiers[0]);

  dotstr << "}";

  std::ofstream     dumpfile;
  const std::string path = "tree_dump.dot";
  dumpfile.open(path);

  I(dumpfile);
  dumpfile << dotstr.str();

  dumpfile.close();

  fmt::print("  wrote file {}.\n", path);
}