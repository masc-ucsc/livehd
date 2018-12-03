//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <dirent.h>
#include <sys/types.h>
#include <set>

#include "graph_library.hpp"
#include "pass.hpp"
#include "nodetype.hpp"

std::unordered_map<std::string, Graph_library *> Graph_library::global_instances;

std::map<std::string, std::map<std::string, LGraph *>> Graph_library::global_name2lgraph;

uint32_t Graph_library::reset_id(const std::string &name, const std::string &source) {
  const auto &it = name2id.find(name);
  if(it != name2id.end()) {
    graph_library_clean           = false;
    attribute[it->second].version = max_next_version++;
    if (attribute[it->second].source != source) {
      Pass::error("No overwrite lgraph:{} because it changed source from {} to {} (LGraph::delete first)",name, attribute[it->second].source, source); // LCOV_EXCL_LINE
    }
    return it->second;
  }
  return add_name(name, source);
}

LGraph *Graph_library::try_find_lgraph(const std::string &path, const std::string &name) {

  if(global_name2lgraph.find(path) != global_name2lgraph.end() &&
     global_name2lgraph[path].find(name) != global_name2lgraph[path].end()) {
    return global_instances[path]->try_find_lgraph(name);
  }

  return nullptr;
}

LGraph *Graph_library::try_find_lgraph(const std::string &name) {

  if(global_name2lgraph.find(path) == global_name2lgraph.end())
    return nullptr;

  if(global_name2lgraph[path].find(name) != global_name2lgraph[path].end()) {
    LGraph *lg = global_name2lgraph[path][name];
    assert(global_instances.find(path) != global_instances.end());
    assert(get_id(name)!=0);
    register_lgraph(name, attribute[get_id(name)].source, lg);

    return lg;
  }

  return nullptr;
}

uint32_t Graph_library::add_name(const std::string &name, const std::string &source) {

  uint32_t id = attribute.size();
  if(!recycled_id.empty()) {
    assert(id > recycled_id.back());
    id = recycled_id.back();
    recycled_id.pop_back();
    attribute[id].name    = name;
    assert(source!="");
    attribute[id].source  = source;
    attribute[id].version = max_next_version++;
  } else {
    Graph_attributes a;
    a.name = name;
    a.source = source;
    a.version = max_next_version++;
    attribute.emplace_back(a);
  }

  graph_library_clean   = false;

  assert(name2id.find(name) == name2id.end());
  name2id[name] = id;

  return id;
}

bool Graph_library::rename_name(const std::string &orig, const std::string &dest) {

  auto it = name2id.find(orig);
  if(it == name2id.end())
    return false;
  uint32_t id = it->second;

  // The dest name should not exist. Call expunge_lgraph/delete if it does
  assert(global_name2lgraph[path].find(dest) == global_name2lgraph[path].end());
  assert(name2id.find(dest) == name2id.end());

  // The orig name should exist, BUT the lgraph should be in CLOSE state
  assert(global_name2lgraph[path].find(orig) == global_name2lgraph[path].end()); // No orig open
  assert(global_name2lgraph[path].find(dest) == global_name2lgraph[path].end()); // No dest open

  name2id.erase(it); // Erase orig

  name2id[dest] = id;

  graph_library_clean   = false;
  attribute[id].name    = dest;
  attribute[id].version = max_next_version++;
  assert(attribute[id].source != ""); // Keep source

  DIR *dir = opendir(path.c_str());
  assert(dir);

  struct dirent *dent;
  while((dent = readdir(dir)) != NULL) {
    if(dent->d_type != DT_REG) // Only regular files
      continue;

    if(strncmp(dent->d_name, "lgraph_", 7) != 0) // only if starts with lgraph_
      continue;

    const std::string name(dent->d_name + 7);
    auto              pos = name.find(orig + "_");
    if(pos == std::string::npos)
      continue;
    const std::string ext(dent->d_name + 7 + orig.size() + 1);

    const std::string dest_file(path + "/lgraph_" + dest + "_" + ext);
    const std::string orig_file(path + "/lgraph_" + orig + "_" + ext);

    fmt::print("renaming {} to {}\n", orig_file, dest_file);
    int s = rename(orig_file.c_str(), dest_file.c_str());
    assert(s >= 0);
  }
  closedir(dir);

  clean_library();

  return id;
}

void Graph_library::update(uint32_t lgid) {
  assert(lgid < attribute.size());

  if(attribute[lgid].version == (max_next_version - 1))
    return;

  graph_library_clean     = false;
  attribute[lgid].version = max_next_version++;
}

void Graph_library::update_nentries(uint32_t lgid, uint64_t nentries) {
  assert(lgid < attribute.size());

  if(attribute[lgid].nentries != nentries) {
    graph_library_clean      = false;
    attribute[lgid].nentries = nentries;
  }
}

void Graph_library::reload() {

  assert(graph_library_clean);

  max_next_version = 1;
  std::ifstream graph_list;

  graph_list.open(path + "/" + library_file);

  name2id.clear();
  attribute.resize(1); // 0 is not a valid ID

  if(!graph_list.is_open())
    return;

  uint32_t n_graphs = 0;
  graph_list >> n_graphs;
  attribute.resize(n_graphs + 1);

  for(size_t idx = 0; idx < n_graphs; ++idx) {
    std::string name;
    std::string source;
    uint32_t    graph_version;
    size_t      graph_id;
    size_t      nentries;

    graph_list >> name >> graph_id >> graph_version >> nentries;
    graph_list >> source;

    if(graph_version >= max_next_version)
      max_next_version = graph_version + 1;

    name2id[name] = graph_id;

    // this is only true in case where we skip graph ids
    if(attribute.size() <= graph_id)
      attribute.resize(graph_id + 1);

    attribute[graph_id].name     = name;
    attribute[graph_id].source   = source;
    attribute[graph_id].version  = graph_version;
    attribute[graph_id].nentries = nentries;
  }

  graph_list.close();

  DIR *dir = opendir(path.c_str());
  if(!dir) {
    Pass::error("graph_library.reload: could not open {} directory",path);
    return;
  }
  struct dirent *dent;

  std::set<std::string> lg_found;

  while((dent = readdir(dir)) != NULL) {
    if(dent->d_type != DT_REG) // Only regular files
      continue;
    if(strncmp(dent->d_name, "lgraph_", 7) != 0) // only if starts with lgraph_
      continue;
    int len = strlen(dent->d_name);
    if (len <= (7+5))
      continue;
    if(strcmp(dent->d_name + len - 5, "_type") != 0) // and finish with _type
      continue;

    std::string name(&dent->d_name[7],len-7-5);
    fmt::print("name_end[{}] name[{}]\n", dent->d_name + len - 5, name);
    assert(lg_found.find(name) == lg_found.end());
    lg_found.insert(name);

    if (name2id.find(name) == name2id.end()) {
      Pass::warn("graph_library does not have {} but {} directory has it", name, path);
    }
  }
  closedir(dir);

  for(auto it=name2id.begin(); it != name2id.end() ; ++it) {
    if (lg_found.find(it->first) == lg_found.end()) {
      Pass::error("graph_library has {} but {} the directory does not have it", it->first, path);
    }
  }
}

Graph_library::Graph_library(const std::string &_path)
    : path(_path)
    , library_file("graph_library") {

  graph_library_clean = true;
  reload();
}

void Graph_library::each_graph(std::function<void(const std::string &, uint32_t lgid)> f1) const {
  for(const std::pair<const std::string, uint32_t> entry : name2id) {
    f1(entry.first, entry.second);
  }
}

bool Graph_library::expunge_lgraph(const std::string &name, const LGraph *lg) {
  if(global_name2lgraph[path][name] != lg) {
    Pass::warn("graph_library::expunge_lgraph({}) for a wrong graph??? path:{}", name, path);
    return true;
  }
  global_name2lgraph[path].erase(global_name2lgraph[path].find(name));
  uint32_t id   = name2id[name];
  name2id[name] = 0;

  if(attribute[id].nopen == 0) {
    attribute[id].clear();
    recycled_id.push_back(id);
    return true;
  }

  // WARNING: Do not recycle attribute (multiple ::create, and overwrite existing name)
  return false;
}

uint32_t Graph_library::register_lgraph(const std::string &name, const std::string &source, LGraph *lg) {
  global_name2lgraph[path][name] = lg;

  uint32_t id = reset_id(name, source);

  const auto &it = name2id.find(name);
  assert(it != name2id.end());
  attribute[id].nopen++;

  return id;
}

bool Graph_library::unregister_lgraph(const std::string &name, uint32_t lgid, const LGraph *lg) {
  assert(attribute.size() > (size_t)lgid);

  // WARNING: NOT ALWAYS, it can ::create multiple times before calling unregister
  // assert(name2id[name] == lgid);
  // assert(global_name2lgraph[path][name] == lg);

  if(attribute[lgid].nopen == 0)
    return true;

  attribute[lgid].nopen--;
  if(attribute[lgid].nopen == 0) {
    // fmt::print("TODO: garbage collect lgraph mmaps {}\n", name);
    return true;
  }

  return false;
}

void Graph_library::clean_library() {
  if(graph_library_clean)
    return;

  std::ofstream graph_list;

  graph_list.open(path + "/" + library_file);
  graph_list << (attribute.size() - 1) << std::endl;
  for(size_t id = 1; id < attribute.size(); id++) {
    const auto &it = attribute[id];
    graph_list << it.name << " " << id << " " << it.version << " " << it.nentries << " " << it.source << std::endl;
  }

  graph_list.close();

  graph_library_clean = true;
}
