//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_library.hpp"
#include "lgraph.hpp"

std::unordered_map<std::string, Graph_library *> Graph_library::instances;

LGraph *Graph_library::get_graph(int id) const {
  assert(id2name.size() > id);
  return LGraph::find_lgraph(path,id2name[id]);
}

Graph_library::Graph_library(const std::string &_path)
  : path(_path)
  , library_file("graph_library") {

  std::ifstream graph_list;
  graph_library_clean = true;

  graph_list.open(path + "/" + library_file);

  if(graph_list.is_open()) {
    uint32_t n_graphs = 0;
    graph_list >> n_graphs;
    id2name.resize(n_graphs);

    for(size_t idx = 0; idx < n_graphs; ++idx) {
      std::string name;
      size_t      graph_id = 0;
      graph_list >> name >> graph_id;

      // this is only true in case where we skip graph ids
      if(id2name.size() <= graph_id)
        id2name.resize(graph_id + 1);
      id2name[graph_id] = name;
      name2id[name]     = graph_id;
    }
  }

  graph_list.close();
}

void Graph_library::clean_library() {
  if(graph_library_clean)
    return;

  std::ofstream graph_list;

  graph_list.open(path + "/" + library_file);
  graph_list << name2id.size() << std::endl;
  for(auto nameid : name2id) {
    graph_list << nameid.first << " " << nameid.second << std::endl;
  }

  graph_list.close();

  graph_library_clean = true;
}
