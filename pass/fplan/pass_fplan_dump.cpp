#include <fstream>
#include <sstream>

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

  std::cout << "  wrote file " << path << "." << std::endl;
}

void Pass_fplan_dump::dump_tree(Eprp_var &var) {
  Pass_fplan p(var);
  p.make_graph(var);

  Hier_tree h(std::move(p.gi));

  h.discover_hierarchy(1);

  std::stringstream dotstr;

  dotstr << "digraph g {\n\tnode [fontname = \"Source Code Pro\", shape=record];\n";

  std::function<void(std::shared_ptr<Hier_node>)> dump_graph_names = [&](std::shared_ptr<Hier_node> root) {
    dotstr << "\t" << root->name;
    if (root->is_leaf()) {
      // graph subset
      dotstr << " [label=\"{" << root->graph_subset << "}\"]";
    }
    dotstr << ";\n";

    if (root->children[0] != nullptr) {
      dump_graph_names(root->children[0]);
      dotstr << "\t" << root->children[0]->name << " -> " << root->name << ";\n";
    }

    if (root->children[1] != nullptr) {
      dump_graph_names(root->children[1]);
      dotstr << "\t" << root->children[1]->name << " -> " << root->name << ";\n";
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

  std::cout << "  wrote file " << path << "." << std::endl;
}