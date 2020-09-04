#include <fstream>
#include <sstream>

#include "pass_fplan.hpp"

void Pass_fplan_dump::pass(Eprp_var &var) {
  Pass_fplan p(var);

  p.make_graph(var);

  using namespace graph::attributes;
  std::stringstream tempstr;
  // note that we have to be careful about what attributes we give a node because they could coincide with something graphviz uses
  tempstr << p.gi.al.dot_format("fplan_weight"_of_edge     = p.gi.weights,
                                "label"_of_vert           = p.gi.debug_names,
                                "fplan_area"_of_vert       = p.gi.areas,
                                "fplan_lgid_label"_of_vert = p.gi.labels,
                                "fplan_id"_of_vert         = p.gi.ids)
          << std::endl;

  std::string modstring = tempstr.str();
  size_t      pos       = modstring.find('\n', 0) + 1;
  modstring.insert(pos, "\tnode [fontname = \"Source Code Pro\"];\n");  // patch the dotfile to get kgraphviewer to work properly

  std::ofstream     dumpfile;
  const std::string path = "fplan_dump.dot";
  dumpfile.open(path);

  I(dumpfile);
  dumpfile << modstring;

  dumpfile.close();

  std::cout << "  wrote file " << path << "." << std::endl;
}