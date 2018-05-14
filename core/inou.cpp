
#include <boost/filesystem.hpp>
#include <vector>

#include "inou.hpp"

void Inou::generate(const LGraph *g) {

  std::vector<const LGraph *> lgs;
  lgs.push_back(g);
  generate(lgs);
}

void Inou::generate(const LGraph &g) {

  std::vector<const LGraph *> lgs;
  lgs.push_back(&g);
  generate(lgs);
}

void Inou::generate(LGraph &g) {

  std::vector<const LGraph *> lgs;
  lgs.push_back(&g);
  generate(lgs);
}

// Trivial inou
//
bool Inou_trivial::is_graph_name_provided() const {
  return opack.graph_name != "";
}

std::vector<LGraph *> Inou_trivial::generate() {

  std::vector<LGraph *> lgs;

  if(opack.graph_name != "") {
    char cadena[4096];
    snprintf(cadena, 4096, "%s/lgraph_%s_nodes", opack.lgdb_path.c_str(), opack.graph_name.c_str());
    if(access(cadena, R_OK | W_OK) == -1) {
      console->error("ERROR: graph_name {} can not be opened in lgdb {}", opack.graph_name, opack.lgdb_path);
      exit(-3);
    }
    lgs.push_back(new LGraph(opack.lgdb_path, opack.graph_name, false)); // Do not clear
    // No need to sync because it is a reload. Already sync
  } else {
    lgs.push_back(new LGraph(opack.lgdb_path));
    lgs[0]->sync();
  }

  return lgs;
}

void Inou_trivial::generate(std::vector<const LGraph *> out) {
  // No output
}
