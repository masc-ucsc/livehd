//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/types.h>
#include <dirent.h>

#include "graph_library.hpp"
#include "lgraph.hpp"

std::unordered_map<std::string, Graph_library *> Graph_library::instances;

LGraph *Graph_library::get_graph(int id) const {
  assert(id2name.size() > (size_t)id);
  return LGraph::find_lgraph(path,id2name[id]);
}

void Graph_library::reload() {

	assert(graph_library_clean);

  max_version = 0;
  std::ifstream graph_list;

  graph_list.open(path + "/" + library_file);

  id2name.push_back(""); // reserved for 0

  if(!graph_list.is_open()) {
    DIR *dir = opendir(path.c_str());
    if(dir) {
      struct dirent *dent;
      while((dent=readdir(dir))!=NULL) {
        if (dent->d_type != DT_REG) // Only regular files
          continue;
        if (strncmp(dent->d_name,"lgraph_", 7) != 0) // only if starts with lgraph_
          continue;
        int len = strlen(dent->d_name);
        if (strcmp(dent->d_name + len - 5,"_type") != 0) // and finish with _type
          continue;
        std::string name(dent->d_name + 7, len-5-7); 

        add_name(name);
      }
      closedir(dir);
    }
    return;
  }

  uint32_t n_graphs = 0;
  graph_list >> n_graphs;
  id2name.resize(n_graphs+1);

  for(size_t idx = 0; idx < n_graphs; ++idx) {
    std::string name;
    int         graph_version;
    size_t      graph_id;

    graph_list >> name >> graph_id >> graph_version;

    if (graph_version>=max_version)
      max_version = graph_version;

    // this is only true in case where we skip graph ids
    if(id2name.size() <= graph_id)
      id2name.resize(graph_id + 1);
    id2name[graph_id] = name;
    attribute[name].id  = graph_id;
    attribute[name].version  = graph_version;
  }

  graph_list.close();
}

Graph_library::Graph_library(const std::string &_path)
  : path(_path)
  , library_file("graph_library") {

	graph_library_clean = true;
  reload();
}

void Graph_library::clean_library() {
  if(graph_library_clean)
    return;

  std::ofstream graph_list;

  graph_list.open(path + "/" + library_file);
  graph_list << attribute.size() << std::endl;
  for(const auto &it : attribute) {
    graph_list << it.first << " " << it.second.id << " " << it.second.version << std::endl;
  }

  graph_list.close();

  graph_library_clean = true;
}
