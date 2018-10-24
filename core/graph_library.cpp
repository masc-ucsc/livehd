//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/types.h>
#include <dirent.h>

#include "graph_library.hpp"

std::unordered_map<std::string, Graph_library *> Graph_library::global_instances;

std::map<std::string, std::map<std::string, LGraph *>> Graph_library::global_name2lgraph;

uint32_t Graph_library::reset_id(const std::string &name) {
  const auto &it = name2id.find(name);
  if(it != name2id.end()) {
    graph_library_clean = false;
    attribute[it->second].version = ++max_version;
    return it->second;
  }
  return add_name(name);
}

LGraph *Graph_library::find_lgraph(const std::string &path, const std::string &name) {

  if(global_name2lgraph.find(path) != global_name2lgraph.end() && global_name2lgraph[path].find(name) != global_name2lgraph[path].end()) {
    LGraph *lg = global_name2lgraph[path][name];
    assert(global_instances.find(path) != global_instances.end());
    global_instances[path]->register_lgraph(name, lg);

    return lg;
  }

  return nullptr;
}

uint32_t Graph_library::add_name(const std::string &name) {

  uint32_t id = attribute.size();
  if (!recycled_id.empty()) {
    assert(id>recycled_id.back());
    id = recycled_id.back();
    recycled_id.pop_back();
  }else{
    attribute.resize(id+1);
  }

  graph_library_clean = false;
  attribute[id].name  = name;
  attribute[id].version  = ++max_version;
  attribute[id].nopen  = 0;

  assert(name2id.find(name) == name2id.end());
  name2id[name] = id;

  return id;
}

LGraph *Graph_library::get_graph(uint32_t id) const {
  assert(attribute.size() > (size_t)id);

  const std::string &name = attribute[id].name;

  return find_lgraph(path, name);
}

void Graph_library::update(uint32_t lgid) {
  assert(lgid < attribute.size());

  if (attribute[lgid].version == max_version)
    return;

  graph_library_clean = false;
  attribute[lgid].version = ++max_version;
}

void Graph_library::reload() {

	assert(graph_library_clean);

  max_version = 0;
  std::ifstream graph_list;

  graph_list.open(path + "/" + library_file);

  name2id.clear();
  attribute.resize(1); // 0 is not a valid ID

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
  attribute.resize(n_graphs+1);

  for(size_t idx = 0; idx < n_graphs; ++idx) {
    std::string name;
    int         graph_version;
    size_t      graph_id;

    graph_list >> name >> graph_id >> graph_version;

    if (graph_version>=max_version)
      max_version = graph_version;

    name2id[name] = graph_id;

    // this is only true in case where we skip graph ids
    if(attribute.size() <= graph_id)
      attribute.resize(graph_id + 1);

    attribute[graph_id].name     = name;
    attribute[graph_id].version  = graph_version;
  }

  graph_list.close();
}

Graph_library::Graph_library(const std::string &_path)
  : path(_path)
  , library_file("graph_library") {

	graph_library_clean = true;
  reload();
}

void Graph_library::each_graph(std::function<void(const std::string &, uint32_t lgid)> f1) const {
  for (const std::pair<const std::string, uint32_t> entry : name2id) {
    f1(entry.first, entry.second);
  }
}

bool Graph_library::expunge_lgraph(const std::string &name, const LGraph *lg) {
  if (global_name2lgraph[path][name] == lg) {
    console->warn("graph_library::delete_lgraph({}) for a wrong graph??? path:{}", name, path);
    return true;
  }
  global_name2lgraph[path].erase(global_name2lgraph[path].find(name));
  uint32_t id = name2id[name];
  name2id[name] = 0;

  if (attribute[id].nopen == 0) {
    attribute[id].clear();
    recycled_id.push_back(id);
    return true;
  }

  // WARNING: Do not recycle attribute (multiple ::create, and overwrite existing name)
  return false;
}

uint32_t Graph_library::register_lgraph(const std::string &name, LGraph *lg) {
  global_name2lgraph[path][name] = lg;

  uint32_t id = reset_id(name);

  const auto &it = name2id.find(name);
  assert(it != name2id.end());
  attribute[id].nopen++;

  return id;
}

bool Graph_library::unregister_lgraph(const std::string &name, uint32_t lgid, const LGraph *lg) {
  assert(attribute.size() > (size_t) lgid);

  // WARNING: NOT ALWAYS, it can ::create multiple times before calling unregister 
  // assert(name2id[name] == lgid);
  // assert(global_name2lgraph[path][name] == lg);

  attribute[lgid].nopen--;

  if (attribute[lgid].nopen==0) {
    fmt::print("TODO: garbage collect lgraph {}\n", name);
    bool done = expunge_lgraph(name, lg);
    if (done)
      return true;
    recycled_id.push_back(lgid);
    return true;
  }

  return false;
}

void Graph_library::clean_library() {
  if(graph_library_clean)
    return;

  std::ofstream graph_list;

  graph_list.open(path + "/" + library_file);
  graph_list << attribute.size() << std::endl;
  uint32_t id=0;
  for(const auto &it : attribute) {
    graph_list << it.name << " " << id << " " << it.version << std::endl;
    id++;
  }

  graph_list.close();

  graph_library_clean = true;
}
