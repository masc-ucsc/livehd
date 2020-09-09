#include <fstream>
#include <sstream>
#include <string>

#include "fmt/format.h" // for more advanced formatting

#include <functional>

#include <iostream>

#include "i_resolve_header.hpp" // for iassert

#include "hier_tree.hpp"
#include "pass_fplan.hpp"

void Pass_fplan_dump::dump_hier(Eprp_var &var) {
  Pass_fplan p(var);

  p.make_graph(var);

  using namespace graph::attributes;
  std::stringstream tempstr;
  // note that we have to be careful about what attributes we give a node because they could coincide with something graphviz uses
  tempstr << p.gi.al.dot_format("label"_of_edge            = p.gi.weights,
                                "label"_of_vert            = p.gi.debug_names,
                                "fplan_area"_of_vert       = p.gi.areas,
                                "fplan_lgid_label"_of_vert = p.gi.labels,
                                "fplan_id"_of_vert         = p.gi.ids)
          << std::endl;

  std::string modstring = tempstr.str();
  size_t      pos       = modstring.find('\n', 0) + 1;
  modstring.insert(
      pos,
      "\tnode [fontname = \"Source Code Pro\", shape=record];\n");  // patch the dotfile to get kgraphviewer to work properly

  std::ofstream     dumpfile;
  const std::string path = "hier_dump.dot";
  dumpfile.open(path);

  I(dumpfile);
  dumpfile << modstring;

  dumpfile.close();

  fmt::print("  wrote file {}.\n", path);
}

void Pass_fplan_dump::dump_tree(Eprp_var &var) {
  Pass_fplan p(var);
  p.make_graph(var);

  Hier_tree h(std::move(p.gi));

  h.discover_hierarchy(1);

  std::stringstream dotstr;

  dotstr << "digraph g {\n\tnode [fontname = \"Source Code Pro\", shape=record];\n";

  std::function<void(std::shared_ptr<Hier_node>)> dump_graph_names = [&](std::shared_ptr<Hier_node> root) {
    dotstr << fmt::format("\t{};\n", root->name);
    if (root->is_leaf()) {
      for (auto v : h.ginfo.sets[root->graph_subset]) {
        std::string name = h.ginfo.debug_names(v);
        name.append("_");
        name.append(std::to_string(h.ginfo.ids(v))); // create a unique label for each node, not just each node type

        dotstr << fmt::format("\t{} [label=\"{{{} | {{lb {} | id {}}}}}\", color=red];\n", name, name, h.ginfo.labels(v), h.ginfo.ids(v));
        dotstr << fmt::format("\t{} -> {};\n", name, root->name);
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