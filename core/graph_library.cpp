//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <dirent.h>
#include <sys/stat.h>

#include <regex>
#include <fstream>
#include <set>

#include "fmt/format.h"

#include "pass.hpp"
#include "graph_library.hpp"

std::unordered_map<std::string, Graph_library *> Graph_library::global_instances;
std::unordered_map<std::string, std::unordered_map<std::string, LGraph *>> Graph_library::global_name2lgraph;

Lg_type_id Graph_library::reset_id(const std::string &name, const std::string &source) {
  const auto &it = name2id.find(name);
  if(it != name2id.end()) {
    graph_library_clean           = false;
    attribute[it->second].version = max_next_version.value++;
    if (attribute[it->second].source != source) {
      if (attribute[it->second].source == "-") {
        attribute[it->second].source = source;
        Pass::warn("overwrite lgraph:{} source from {} to {}",name, attribute[it->second].source, source); // LCOV_EXCL_LINE
      }else if (source == "-") {
        Pass::warn("keeping lgraph:{} source {}",name, attribute[it->second].source); // LCOV_EXCL_LINE
      }else{
        Pass::error("No overwrite lgraph:{} because it changed source from {} to {} (LGraph::delete first)",name, attribute[it->second].source, source); // LCOV_EXCL_LINE
      }
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

bool Graph_library::exists(const std::string &path, const std::string &name) {

  const Graph_library *lib = instance(path);

  return lib->name2id.find(name) != lib->name2id.end();
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

Lg_type_id Graph_library::add_name(const std::string &name, const std::string &source) {

  assert(source!="");

  Graph_attributes a;
  a.name = name;
  a.source = source;
  a.version = max_next_version.value++;

  Lg_type_id id = try_get_recycled_id();
  if (id==0) {
    id = attribute.size();
    attribute.emplace_back(a);
  }else{
    assert(id<attribute.size());
    attribute[id] = a;
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
  Lg_type_id id = it->second;

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
  attribute[id].version = max_next_version.value++;
  assert(attribute[id].source != ""); // Keep source

  DIR *dir = opendir(path.c_str());
  assert(dir);

  struct dirent *dent;
  while((dent = readdir(dir)) != nullptr) {
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

void Graph_library::update(Lg_type_id lgid) {
  assert(lgid < attribute.size());

  if(attribute[lgid].version == (max_next_version - 1))
    return;

  graph_library_clean     = false;
  attribute[lgid].version = max_next_version.value++;
}

void Graph_library::update_nentries(Lg_type_id lgid, uint64_t nentries) {
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

  if(!graph_list.is_open()) { // No reload, just empty
    mkdir(path.c_str(), 0755); // At least make sure directory exists for future
    return;
  }

  size_t n_graphs = 0;
  graph_list >> n_graphs;
  attribute.resize(n_graphs + 1);

  for(size_t idx = 0; idx < n_graphs; ++idx) {
    std::string name;
    std::string source;
    Lg_type_id    graph_version;
    size_t      graph_id;
    size_t      nentries;

    graph_list >> name >> graph_id >> graph_version.value >> nentries;
    graph_list >> source;

    if(graph_version >= max_next_version)
      max_next_version = graph_version.value + 1;

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

  while((dent = readdir(dir)) != nullptr) {
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

void Graph_library::each_graph(std::function<void(const std::string &, Lg_type_id lgid)> f1) const {
  for(const std::pair<const std::string, Lg_type_id> entry : name2id) {
    f1(entry.first, entry.second);
  }
}

Lg_type_id Graph_library::try_get_recycled_id() {
  if(recycled_id.none())
    return 0;

  Lg_type_id lgid = recycled_id.get_first();
  assert(lgid<=attribute.size());
  recycled_id.clear(lgid);

  return lgid;
}

void Graph_library::recycle_id(Lg_type_id lgid) {

  if (lgid < attribute.size()) {
    recycled_id.set_bit(lgid);
    return;
  }

  size_t end_pos = attribute.size();
  while(attribute.back().version==0) {
    attribute.pop_back();
    if (attribute.empty())
      break;
  }

  recycled_id.set_range(attribute.size(), end_pos, false);
}

bool Graph_library::expunge_lgraph(const std::string &name, const LGraph *lg) {
  if(global_name2lgraph[path][name] != lg) {
    Pass::warn("graph_library::expunge_lgraph({}) for a wrong graph??? path:{}", name, path);
    return true;
  }
  global_name2lgraph[path].erase(global_name2lgraph[path].find(name));
  Lg_type_id id   = name2id[name];
  name2id[name] = 0;

  if(attribute[id].nopen == 0) {
    attribute[id].clear();
    recycle_id(id);
    return true;
  }

  // WARNING: Do not recycle attribute (multiple ::create, and overwrite existing name)
  return false;
}

Lg_type_id Graph_library::register_lgraph(const std::string &name, const std::string &source, LGraph *lg) {
  global_name2lgraph[path][name] = lg;

  Lg_type_id id = reset_id(name, source);

  const auto &it = name2id.find(name);
  assert(it != name2id.end());
  attribute[id].nopen++;

  return id;
}

bool Graph_library::unregister_lgraph(const std::string &name, Lg_type_id lgid, const LGraph *lg) {
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

void Graph_library::sync_all() {
  assert(false); // FIXME: to implement, once we migrate lgraph.cpp to core
#if 0
  for(auto &it:global_name2lgraph) {
    for(auto &it2:it.second) {
      it2.second->sync();
    }
  }
#endif
}

void Graph_library::clean_library() {
  if(graph_library_clean)
    return;

  std::ofstream graph_list;

  graph_list.open(path + "/" + library_file);
  graph_list << (attribute.size() - 1) << std::endl;
  for(size_t id = 1; id < attribute.size(); ++id) {
    const auto &it = attribute[id];
    if (it.version==0) // Clear/delete sets version to zero
      continue;
    graph_list << it.name << " " << id << " " << it.version << " " << it.nentries << " " << it.source << std::endl;
  }

  graph_list.close();

  graph_library_clean = true;
}

void Graph_library::each_type(std::function<bool(Lg_type_id, const std::string &)> f1) const {

  for(size_t id = 1; id < attribute.size(); ++id) {
    const auto &it = attribute[id];
    if (it.version==0) // Clear/delete sets version to zero
      continue;

    bool cont = f1(id, it.name);
    if (!cont)
      break;
  }

}

void Graph_library::each_type(const std::string &match, std::function<bool(Lg_type_id, const std::string &)> f1) const {

  const std::regex txt_regex(match);

  for(size_t id = 1; id < attribute.size(); ++id) {
    const auto &it = attribute[id];
    if (it.version==0) // Clear/delete sets version to zero
      continue;
    if (!std::regex_search(it.name, txt_regex))
      continue;

    bool cont = f1(id, it.name);
    if (!cont)
      break;
  }

}

